#include "NexusUtils.h"

#include "dag.h"
#include "nexusdata.h"
#include "UnrealNexusData.h"
#include "UnrealNexusNodeData.h"
#include "corto/decoder.h"
#include "Kismet/KismetArrayLibrary.h"

nx::Node DataUtils::ReadNode(uint8*& Buffer)
{

    auto ReadShort = [&](uint8*& Ptr) {
        uint16 Value = Utils::Read16(Ptr);
        return *(reinterpret_cast<short*>(&Value));
    };
    using namespace Utils;
    nx::Node TheNode;
    TheNode.offset = Read32(Buffer);
    TheNode.nvert = Read16(Buffer);
    TheNode.nface = Read16(Buffer);
    TheNode.error = ReadFloat(Buffer);
    TheNode.cone.n[0] = ReadShort(Buffer);
    TheNode.cone.n[1] = ReadShort(Buffer);
    TheNode.cone.n[2] = ReadShort(Buffer);
    TheNode.cone.n[3] = ReadShort(Buffer);
    TheNode.sphere = vcg::Sphere3f {
                { ReadFloat(Buffer), ReadFloat(Buffer), ReadFloat(Buffer)},
                ReadFloat(Buffer)
            };
    TheNode.tight_radius = ReadFloat(Buffer);
    TheNode.first_patch = Read32(Buffer);
    return TheNode;
}

nx::Patch DataUtils::ReadPatch(uint8*& Buffer)
{
    using namespace Utils;
    nx::Patch ThePatch;
    ThePatch.node = Read32(Buffer);
    ThePatch.triangle_offset = Read32(Buffer);
    ThePatch.texture = Read32(Buffer);
    return ThePatch;
}

nx::Texture DataUtils::ReadTexture(uint8*& Buffer)
{
    using namespace Utils;
    nx::Texture Tex;
    Tex.offset = Read32(Buffer);
    for (int i = 0; i < 16; i ++)
    {
        Tex.matrix[i] = ReadFloat(Buffer);
    }
    return Tex;
}

void LoadUtils::LoadNodeData(Header& Header, int VertCount, int FacesCount, NodeData& TheNodeData, const UINT64 DataSizeOnDisk)
{
	nx::Signature& Signature = Header.signature;
	if (!Signature.isCompressed())
	{
		return;
	} else if (Signature.flags & nx::Signature::CORTO)
	{
		const UINT32 RealSize = VertCount * Signature.vertex.size() + FacesCount * Signature.face.size();
		char* Buffer = new char[DataSizeOnDisk];
		FMemory::Memcpy(Buffer, TheNodeData.memory, DataSizeOnDisk);
		delete[] TheNodeData.memory;
		TheNodeData.memory = new char[RealSize];
		UINT32 Magic = *reinterpret_cast<UINT32*>(Buffer);
		char Magic0 = Buffer[0];
		char Magic1 = Buffer[1];
		char Magic2 = Buffer[2];
		char Magic3 = Buffer[3];

		try
		{
			crt::Decoder Decoder(DataSizeOnDisk, reinterpret_cast<unsigned char*>(Buffer));
			Decoder.setPositions(reinterpret_cast<float*>(TheNodeData.coords()));
			if(Signature.vertex.hasNormals())
				Decoder.setNormals(reinterpret_cast<int16_t*>(TheNodeData.normals(Signature, VertCount)));
			if(Signature.vertex.hasColors())
				Decoder.setColors(reinterpret_cast<unsigned char*>(TheNodeData.colors(Signature, VertCount)));
			if(Signature.vertex.hasTextures())
				Decoder.setUvs(reinterpret_cast<float*>(TheNodeData.texCoords(Signature, VertCount)));
			if(FacesCount)
				Decoder.setIndex(TheNodeData.faces(Signature, VertCount));

			Decoder.decode();
		} catch (const char* Error)
		{
			UE_LOG(NexusInfo, Error, TEXT("WHAT THE FUCK %s"), ANSI_TO_TCHAR(Error));
		}
        // delete Buffer;
    } else if (Signature.isCompressed())
    {
        UE_LOG(NexusInfo, Error, TEXT("Only CORTO compression is supported"));
        return;
    }
	
	// Shuffle points in compressed point clouds
	if(!Signature.face.hasIndex()) {
		TArray<int> Order;
		Order.SetNum(VertCount);
		for(int i = 0; i < VertCount; i++)
			Order[i] = i;

		UKismetArrayLibrary::Array_Shuffle(Order);
			
		TArray<vcg::Point3f> Coords;
		Coords.SetNum(VertCount);
		for(int i = 0; i < VertCount; i++)
			Coords[i] = TheNodeData.coords()[Order[i]];
		FMemory::Memcpy(TheNodeData.coords(), Coords.GetData(), sizeof(vcg::Point3f) * VertCount);

		if(Signature.vertex.hasNormals()) {
			vcg::Point3s *n = TheNodeData.normals(Signature, VertCount);
            TArray<vcg::Point3s> Normals;
            Normals.SetNum(VertCount);
			for(int i = 0; i < VertCount; i++)
                Normals[i] = n[Order[i]];
			FMemory::Memcpy(n, Normals.GetData(), sizeof(vcg::Point3f) * VertCount);
		}

		if(Signature.vertex.hasColors()) {
			vcg::Color4b *c = TheNodeData.colors(Signature, VertCount);
            TArray<vcg::Color4b> Colors;
            Colors.SetNum(VertCount);
			for(int i =0; i < VertCount; i++)
				Colors[i] = c[Order[i]];
			FMemory::Memcpy(c, Colors.GetData(), sizeof(vcg::Color4b) * VertCount);
		}
	}

#if 0
	if(Header.n_textures) {
		//be sure to load images
		for(uint32_t p = TheNode.first_patch; p < Node.last_patch(); p++) {
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
#endif
    
}

FVector VcgPoint3FToVector(const vcg::Point3f& Point3)
{
	return FVector(Point3.X(), Point3.Z(), Point3.Y());
}

FStreamableManager& GetStreamableManager()
{
    static FStreamableManager Manager;
    return Manager;
}
