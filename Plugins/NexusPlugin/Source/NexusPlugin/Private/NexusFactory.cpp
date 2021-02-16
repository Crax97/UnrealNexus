#include "NexusFactory.h"
#include "UnrealNexusData.h"
#include "NexusUtils.h"

void LogHeaderInfo(const Header& Header)
{
    UE_LOG(NexusInfo, Log, TEXT("NEXUS: Read file with header version %d"), Header.version);
    UE_LOG(NexusInfo, Log, TEXT("\t %d vertices"), Header.nvert);
    UE_LOG(NexusInfo, Log, TEXT("\t %d faces"), Header.nface);
    UE_LOG(NexusInfo, Log, TEXT("\t %d nodes"), Header.n_nodes);
    UE_LOG(NexusInfo, Log, TEXT("\t %d patches"), Header.n_patches);
    UE_LOG(NexusInfo, Log, TEXT("\t %d textures"), Header.n_textures);
    UE_LOG(NexusInfo, Log, TEXT("\t sphere: center (%f, %f, %f) radius %f"), Header.sphere.Center().X(),Header.sphere.Center().Y(),Header.sphere.Center().Z(), Header.sphere.Radius());
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

    InitData(NexusImportedFile, BufferPtr);
    
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
        UE_LOG(NexusErrors, Error, TEXT("Failure reading the header: Either the file is too short or it can't be accessed"));
        return false;
    }
    
    uint8 RawHeader[Nexus_Header_Size];
    uint8* Ptr = reinterpret_cast<uint8*>(&RawHeader);
    NexusData->Header.magic = Read32(Buffer); // 4 bytes
    if (NexusData->Header.magic != Nexus_Header_Magic)
    {
        UE_LOG(NexusErrors, Error, TEXT("Magic Number mismatch, read %#x expected %#x"), NexusData->Header.magic, Nexus_Header_Magic);
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

    check(Ptr - RawHeader == Nexus_Header_Size);
    
    LogHeaderInfo(NexusData->Header);
    return true;
}

bool UNexusFactory::ReadDataIntoNexusFile(UUnrealNexusData* UnrealNexusData, uint8*& Buffer, const uint8* BufferEnd)
{
    return ParseHeader(UnrealNexusData, Buffer, BufferEnd);
}

void UNexusFactory::InitData(UUnrealNexusData* Data, uint8*& Buffer)
{
    using namespace Utils;
    using namespace DataUtils;
    
    Data->Nodes = new Node[Data->Header.n_nodes];
    Data->Patches = new Patch[Data->Header.n_patches];
    Data->Textures = new Texture[Data->Header.n_textures];
    Data->NodeData = new NodeData[Data->Header.n_nodes];
    Data->TextureData = new TextureData[Data->Header.n_textures];

    for (uint32 i = 0; i < Data->Header.n_nodes; i ++)
    {
        Data->Nodes[i] = ReadNode(Buffer);
    }
    
    for (uint32 i = 0; i < Data->Header.n_patches; i ++)
    {
        Data->Patches[i] = ReadPatch(Buffer);
    }
    
    for (uint32 i = 0; i < Data->Header.n_textures; i ++)
    {
        Data->Textures[i] = ReadTexture(Buffer);
    }

    //find number of roots:
    Data->RootsCount = Data->Header.n_nodes;
    for(uint32_t j = 0; j < Data->RootsCount; j++) {
        for(uint32_t i = Data->Nodes[j].first_patch; i < Data->Nodes[j].last_patch(); i++)
            if(Data->Patches[i].node < Data->RootsCount)
                Data->RootsCount = Data->Patches[i].node;
    }
}
