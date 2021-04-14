#include "NexusFactory.h"

#include "UnrealNexusData.h"
#include "UnrealNexusNodeData.h"
#include "NexusUtils.h"
#include "NodeDataFactory.h"

DEFINE_LOG_CATEGORY(NexusEditorInfo)
DEFINE_LOG_CATEGORY(NexusEditorErrors)

void LogHeaderInfo(const Header& Header)
{
    UE_LOG(NexusEditorInfo, Log, TEXT("NEXUS: Read file with header version %d"), Header.version);
    UE_LOG(NexusEditorInfo, Log, TEXT("\t %d vertices"), Header.nvert);
    UE_LOG(NexusEditorInfo, Log, TEXT("\t %d faces"), Header.nface);
    UE_LOG(NexusEditorInfo, Log, TEXT("\t %d nodes"), Header.n_nodes);
    UE_LOG(NexusEditorInfo, Log, TEXT("\t %d patches"), Header.n_patches);
    UE_LOG(NexusEditorInfo, Log, TEXT("\t %d textures"), Header.n_textures);
    UE_LOG(NexusEditorInfo, Log, TEXT("\t sphere: center (%f, %f, %f) radius %f"), Header.sphere.Center().X(),Header.sphere.Center().Y(),Header.sphere.Center().Z(), Header.sphere.Radius());
}

UNexusFactory::UNexusFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SupportedClass = UUnrealNexusData::StaticClass();
    Formats.Add(TEXT("nxs;Nexus Uncompressed File"));
    Formats.Add(TEXT("nxz;Nexus Compressed File"));
    bEditorImport = true;
    bCreateNew = false;
}

UObject* UNexusFactory::FactoryCreateBinary(UClass* Class, UObject* InParent, const FName Name, const EObjectFlags Flags,
                                        UObject* Context, const TCHAR* FileType, const uint8*& Buffer,
                                        const uint8* BufferEnd, FFeedbackContext* Warn)
{
    // 1 Create Nexus data object
    GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, Name, FileType);
    
    // 2 Create package to hold it
	const FString GroupName = InParent->GetFullGroupName(false);
    FString FilePackageName = InParent->GetOutermost()->GetName();
    FilePackageName += "_Nexus";
    UPackage* NexusDataPackage = CreatePackage(nullptr, *FilePackageName);
    UUnrealNexusData* NexusImportedFile = NewObject<UUnrealNexusData>(NexusDataPackage, Name, Flags);

    uint8* BufferPtr = const_cast<uint8*>(Buffer);
    if (!ReadDataIntoNexusFile(NexusImportedFile, BufferPtr, BufferEnd))
    {
        delete NexusImportedFile;
        NexusImportedFile = nullptr;
    }

    InitData(NexusImportedFile, BufferPtr, Buffer);
    
    GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, NexusImportedFile);
    return NexusImportedFile;
}

bool UNexusFactory::ParseHeader(UUnrealNexusData* NexusData, uint8*& Buffer, const uint8* BufferEnd)
{
    using namespace Utils;
    using namespace DataUtils;
    const int Nexus_Header_Size = 88;
    const int Nexus_Header_Magic = 0x4E787320;

    if (BufferEnd - Buffer < Nexus_Header_Size)
    {
        UE_LOG(NexusEditorErrors, Error, TEXT("Failure reading the header: Either the file is too short or it can't be accessed"));
        return false;
    }
    
    NexusData->Header.magic = Read32(Buffer); // 4 bytes
    if (NexusData->Header.magic != Nexus_Header_Magic)
    {
        UE_LOG(NexusEditorErrors, Error, TEXT("Magic Number mismatch, read %#x expected %#x"), NexusData->Header.magic, Nexus_Header_Magic);
        return false;
    }
    NexusData->Header.version = Read32(Buffer); // 8
    NexusData->Header.nvert = Read64(Buffer); // 16
    NexusData->Header.nface = Read64(Buffer); // 24
    NexusData->Header.signature = ReadSignature(Buffer); // 24 + 36 = 60
    NexusData->Header.n_nodes = Read32(Buffer); // 64
    NexusData->Header.n_patches = Read32(Buffer); // 68
    NexusData->Header.n_textures = Read32(Buffer); // 72
    NexusData->Header.sphere = vcg::Sphere3f {
        { ReadFloat(Buffer), ReadFloat(Buffer), ReadFloat(Buffer)},
        ReadFloat(Buffer)
    }; // 72 + 16 = 88
    
    LogHeaderInfo(NexusData->Header);
    return true;
}

bool UNexusFactory::ReadDataIntoNexusFile(UUnrealNexusData* UnrealNexusData, uint8*& Buffer, const uint8* BufferEnd)
{
    return ParseHeader(UnrealNexusData, Buffer, BufferEnd);
}

void UNexusFactory::InitData(UUnrealNexusData* Data, uint8*& Buffer, const uint8* FileBegin) const
{
    using namespace Utils;
    using namespace DataUtils;

    const auto PackagePath = Data->GetOutermost()->GetName();
    // Read all nodes
    UNodeDataFactory* NodeFactory = NewObject<UNodeDataFactory>();
    for (uint32 i = 0; i < Data->Header.n_nodes; i ++)
    {
        const auto Node = ReadNode(Buffer);
        Data->Nodes.Add(FUnrealNexusNode {Node});
    }

    // Fill their NodeData memory
    for (uint32 i = 0; i < Data->Header.n_nodes - 1; i ++)
    {
        
        // Create a Node .uasset and register it
        auto NodePathString = FString::Printf(TEXT("%s_Node%d"), *PackagePath, i);
        auto& UCurrentNode = Data->Nodes[i];
        auto& UNextNode = Data->Nodes[i + 1];
        auto NodeName = FPaths::GetBaseFilename(NodePathString);
        UPackage* NodeDataPackage = CreatePackage(nullptr, *NodePathString);
        UUnrealNexusNodeData* UNodeData = NodeFactory->CreateNodeAssetFile(NodeDataPackage, NodeName, RF_Public | RF_Standalone);
        // ReSharper disable once CppExpressionWithoutSideEffects
        UNodeData->MarkPackageDirty();
        UNodeData->NodeSize = UNextNode.NexusNode.getBeginOffset() - UCurrentNode.NexusNode.getBeginOffset();
        UNodeData->NexusNodeData.memory = new char[UNodeData->NodeSize];

        FMemory::Memcpy(UNodeData->NexusNodeData.memory, (FileBegin + UCurrentNode.NexusNode.getBeginOffset()), UNodeData->NodeSize);
        UCurrentNode.NodeDataPath = UNodeData;
    }

    // Read patches
    TArray<Patch> Patches;
    Patches.SetNum(Data->Header.n_patches);
    for (uint32 i = 0; i < Data->Header.n_patches; i ++)
    {
        Patches[i] = ReadPatch(Buffer);
    }

    // Assign patches
    for (uint32 i = 0; i < Data->Header.n_nodes - 1; i ++)
    {
        auto& Node = Data->Nodes[i];
        auto& NextNode = Data->Nodes[i + 1];
        const uint32 LastPatch = NextNode.NexusNode.first_patch;
        for (uint32 PatchId = Node.NexusNode.first_patch; PatchId < LastPatch; PatchId ++)
        {
            Node.NodePatches.Add(Patches[PatchId]);
        }
    }
    for (uint32 i = 0; i < Data->Header.n_textures; i ++)
    {
        // Data->Textures[i] = ReadTexture(Buffer);
    }

    //find number of roots:
    Data->RootsCount = Data->Header.n_nodes;
    for(int j = 0; j < Data->RootsCount; j++) {
        auto& Node = Data->Nodes[j].NexusNode;
        auto& LastNode = Data->Nodes[j + 1].NexusNode;
        for(uint32_t i = Node.first_patch; i < LastNode.first_patch; i++)
            if(Patches[i].node < static_cast<UINT32>(Data->RootsCount))
                Data->RootsCount = Patches[i].node;
    }
}
