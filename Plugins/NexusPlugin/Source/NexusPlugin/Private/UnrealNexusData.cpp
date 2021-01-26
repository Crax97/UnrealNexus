#include "UnrealNexusData.h"

#include "corto/decoder.h"
#include "Kismet/KismetArrayLibrary.h"

DEFINE_LOG_CATEGORY(NexusInfo);
DEFINE_LOG_CATEGORY(NexusErrors);

namespace DataUtils {
    nx::Attribute ReadAttribute(uint8*& Ptr)
    {
        Attribute Attr;
        Attr.type = static_cast<char>(Utils::Read8(Ptr));
        Attr.number = static_cast<char>(Utils::Read8(Ptr));
        return Attr;
    }

    nx::Signature ReadSignature(uint8*& Ptr)
    {
        VertexElement Vertex;
        for (int i = 0; i < 8; i ++)
        {
            Vertex.attributes[i] = ReadAttribute(Ptr);
        }
        FaceElement Face;
        for (int i = 0; i < 8; i ++)
        {
            Face.attributes[i] = ReadAttribute(Ptr);
        }
        
        Signature Sig;
        Sig.vertex = Vertex;
        Sig.face = Face;
        Sig.flags = Utils::Read32(Ptr);
        return Sig;
    }

    nx::Node ReadNode(NexusFile* File)
    {
        auto ReadShort = [&](uint8*& Ptr) {
            uint16 Value = Utils::Read16(Ptr);
            return *(reinterpret_cast<short*>(&Value));
        };
        using namespace Utils;
        const int Size = 44;
        uint8 Header[Size];
        uint8* Ptr = Header;
        File->read(reinterpret_cast<char*>(Header), Size);
        Node TheNode;
        TheNode.offset = Read32(Ptr);
        TheNode.nvert = Read16(Ptr);
        TheNode.nface = Read16(Ptr);
        TheNode.error = ReadFloat(Ptr);
        TheNode.cone.n[0] = ReadShort(Ptr);
        TheNode.cone.n[1] = ReadShort(Ptr);
        TheNode.cone.n[2] = ReadShort(Ptr);
        TheNode.cone.n[3] = ReadShort(Ptr);
        TheNode.sphere = vcg::Sphere3f {
            { ReadFloat(Ptr), ReadFloat(Ptr), ReadFloat(Ptr)},
            ReadFloat(Ptr)
        };
        TheNode.tight_radius = ReadFloat(Ptr);
        TheNode.first_patch = Read32(Ptr);
        return TheNode;
    }
    nx::Patch ReadPatch(NexusFile* File)
    {
        using namespace Utils;
        const int Size = 12;
        uint8 Header[Size];
        File->read(reinterpret_cast<char*>(Header), Size);
        uint8* Ptr = Header;
        Patch ThePatch;
        ThePatch.node = Read32(Ptr);
        ThePatch.triangle_offset = Read32(Ptr);
        ThePatch.texture = Read32(Ptr);
        return ThePatch;
    }
    nx::Texture ReadTexture(NexusFile* File)
    {
        
        using namespace Utils;
        const int Size = 4 + 16 * 4;
        uint8 Header[Size];
        File->read(reinterpret_cast<char*>(Header), Size);
        uint8* Ptr = Header;
        Texture Tex;
        Tex.offset = Read32(Ptr);
        for (int i = 0; i < 16; i ++)
        {
            Tex.matrix[i] = ReadFloat(Ptr);
        }
        return Tex;
    }
}

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


char* ToRGBA8888(char* RawData, const nx::TextureData& Data)
{
    // TODO: Transform from RGB to RGBA
    return RawData;
}

void LoadImageRawData(UTexture2D* Image, TextureData& Data)
{
    char* DataPtr = static_cast<char*>(Image->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memcpy(DataPtr, Data.memory, Data.height * Data.width * 4);
    Image->PlatformData->Mips[0].BulkData.Unlock();
    Image->PlatformData->SetNumSlices(1);
    Image->NeverStream = true;
    Image->UpdateResource();
}

bool FUnrealNexusData::ParseHeader()
{
    
    using namespace Utils;
    using namespace DataUtils;
    const int Nexus_Header_Size = 88;
    const int Nexus_Header_Magic = 0x4E787320;
    
    uint8 RawHeader[Nexus_Header_Size];
    uint8* Ptr = reinterpret_cast<uint8*>(&RawHeader);
    if (!file->read(reinterpret_cast<char*>(RawHeader), Nexus_Header_Size)) {
        UE_LOG(NexusErrors, Error, TEXT("Failure reading the header: Either the file is too short or it can't be accessed"));
        return false;
    }
    
    header.magic = Read32(Ptr); // 4 bytes
    if (header.magic != Nexus_Header_Magic)
    {
        UE_LOG(NexusErrors, Error, TEXT("Magic Number mismatch, read %#x expected %#x"), header.magic, Nexus_Header_Magic);
        return false;
    }
    header.version = Read32(Ptr); // 8
    header.nvert = Read64(Ptr); // 16
    header.nface = Read64(Ptr); // 24
    header.signature = ReadSignature(Ptr); // 24 + 36 = 60
    header.n_nodes = Read32(Ptr); // 64
    header.n_patches = Read32(Ptr); // 68
    header.n_textures = Read32(Ptr); // 72
    header.sphere = vcg::Sphere3f {
        { ReadFloat(Ptr), ReadFloat(Ptr), ReadFloat(Ptr)},
        ReadFloat(Ptr)
    }; // 72 + 16 = 88

    check(Ptr - RawHeader == Nexus_Header_Size);
    
    LogHeaderInfo(header);
    return true;
}

bool FUnrealNexusData::InitData()
{
    using namespace Utils;
    using namespace DataUtils;
    
    nodes = new Node[header.n_nodes];
    patches = new Patch[header.n_patches];
    textures = new Texture[header.n_textures];
    nodedata = new NodeData[header.n_nodes];
    texturedata = new TextureData[header.n_textures];

    for (uint32 i = 0; i < header.n_nodes; i ++)
    {
        nodes[i] = ReadNode(file);
    }
    
    for (uint32 i = 0; i < header.n_patches; i ++)
    {
        patches[i] = ReadPatch(file);
    }
    
    for (uint32 i = 0; i < header.n_textures; i ++)
    {
        textures[i] = ReadTexture(file);
    }

    //find number of roots:
    nroots = header.n_nodes;
    for(uint32_t j = 0; j < nroots; j++) {
        for(uint32_t i = nodes[j].first_patch; i < nodes[j].last_patch(); i++)
            if(patches[i].node < nroots)
                nroots = patches[i].node;
    }
    return true;
}

void FUnrealNexusData::LoadIntoRam(const int N)
{
    if (LoadedNodes.Contains(N)) return;
	
	Signature &Sign = header.signature;
	Node &Node = nodes[N];
	uint64_t Offset = Node.getBeginOffset();

	NodeData &d = nodedata[N];
	uint64_t Compressed_Size = Node.getEndOffset() - Offset;

	uint64_t Size = Node.nvert * Sign.vertex.size() + Node.nface * Sign.face.size();

	if(!Sign.isCompressed()) {

		d.memory = static_cast<char*>(file->map(Offset, Size));

	} else if(Sign.flags & Signature::CORTO) {

            char* Buffer = new char[Compressed_Size];
            file->seek(Offset);
            int64_t BytesRead = file->read(Buffer, Compressed_Size);
            check(BytesRead == Compressed_Size);

            d.memory = new char[Size];

			crt::Decoder Decoder(Compressed_Size, reinterpret_cast<unsigned char*>(Buffer));

			Decoder.setPositions(reinterpret_cast<float*>(d.coords()));
			if(Sign.vertex.hasNormals())
				Decoder.setNormals(reinterpret_cast<int16_t*>(d.normals(Sign, Node.nvert)));
			if(Sign.vertex.hasColors())
				Decoder.setColors(reinterpret_cast<unsigned char*>(d.colors(Sign, Node.nvert)));
			if(Sign.vertex.hasTextures())
				Decoder.setUvs(reinterpret_cast<float*>(d.texCoords(Sign, Node.nvert)));
			if(Node.nface)
				Decoder.setIndex(d.faces(Sign, Node.nvert));

			Decoder.decode();
			delete Buffer;
	} else
	{
		UE_LOG(NexusInfo, Error, TEXT("Only CORTO compression is supported"));
		return;
	}

	//Shuffle points in compressed point clouds
	if(!Sign.face.hasIndex()) {
		TArray<int> Order;
		Order.SetNum(Node.nvert);
		for(int i = 0; i < Node.nvert; i++)
			Order[i] = i;

		UKismetArrayLibrary::Array_Shuffle(Order);
			
		TArray<vcg::Point3f> Coords;
		Coords.SetNum(Node.nvert);
		for(int i = 0; i < Node.nvert; i++)
			Coords[i] = d.coords()[Order[i]];
		FMemory::Memcpy(d.coords(), Coords.GetData(), sizeof(vcg::Point3f) * Node.nvert);

		if(Sign.vertex.hasNormals()) {
			vcg::Point3s *n = d.normals(Sign, Node.nvert);
            TArray<vcg::Point3s> Normals;
            Normals.SetNum(Node.nvert);
			for(int i = 0; i < Node.nvert; i++)
                Normals[i] = n[Order[i]];
			FMemory::Memcpy(n, Normals.GetData(), sizeof(vcg::Point3f) * Node.nvert);
		}

		if(Sign.vertex.hasColors()) {
			vcg::Color4b *c = d.colors(Sign, Node.nvert);
            TArray<vcg::Color4b> Colors;
            Colors.SetNum(Node.nvert);
			for(int i =0; i < Node.nvert; i++)
				Colors[i] = c[Order[i]];
			FMemory::Memcpy(c, Colors.GetData(), sizeof(vcg::Color4b) * Node.nvert);
		}
	}

	check(d.memory);
	if(header.n_textures) {
		//be sure to load images
		for(uint32_t p = Node.first_patch; p < Node.last_patch(); p++) {
			uint32_t t = patches[p].texture;
			if(t == 0xffffffff) continue;

			TextureData &TexData = texturedata[t];
			TexData.count_ram++;
			if(TexData.count_ram > 1)
				continue;

			Texture &Tex = textures[t];
			TexData.memory = static_cast<char*>(file->map(Tex.getBeginOffset(), Tex.getSize()));
			checkf(TexData.memory, TEXT("Failed mapping texture data"));

			loadImageFromData(TexData, t);
		}
	}
	UE_LOG(NexusInfo, Log, TEXT("Loaded Nexus node with index %d"), N);
	LoadedNodes.Add(N);
}

void FUnrealNexusData::DropFromRam(const int N)
{
    if (!LoadedNodes.Contains(N)) return;
	Node &Node = nodes[N];
    NodeData &Data = nodedata[N];

    assert(data.memory);

    if(!header.signature.isCompressed()) //not compressed.
    	file->unmap(reinterpret_cast<unsigned char*>(Data.memory));
    else
    	delete []Data.memory;

    Data.memory = nullptr;

    //report RAM size for compressed meshes.
    uint32_t Size = Node.nvert * header.signature.vertex.size() +
    			Node.nface * header.signature.face.size();

    if(header.n_textures) {
    	for(uint32_t p = Node.first_patch; p < Node.last_patch(); p++) {
	        const uint32_t TextureIndex = patches[p].texture;
    		if(TextureIndex == 0xffffffff) continue;

    		TextureData &TexData = texturedata[TextureIndex];
    		TexData.count_ram--;
    		if(TexData.count_ram != 0) continue;

    		delete []TexData.memory;
    		TexData.memory = nullptr;
    		Size += TexData.width * TexData.height * 4;
    	}
    }
	UE_LOG(NexusInfo, Log, TEXT("Dropped Nexus node with index %d"), N);
	LoadedNodes.Remove(N);
}

bool FUnrealNexusData::Init()
{
    if (!file->open(NexusFile::Read)) return false;
    return ParseHeader() && InitData();
}

void FUnrealNexusData::loadImageFromData(nx::TextureData& Data, int TextureIndex)
{
    
    Texture& Texture = textures[TextureIndex];
    Data.memory = static_cast<char*>(file->map(Texture.getBeginOffset(), Texture.getSize()));
    checkf(Data.memory, TEXT("Failed mapping texture data"));
    Data.memory = ToRGBA8888(Data.memory, Data);
   
    UTexture2D* Image = UTexture2D::CreateTransient(Data.width, Data.height, PF_R8G8B8A8);
    LoadImageRawData(Image, Data);
    file->unmap(Data.memory);
    
}
