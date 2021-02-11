#pragma once
#include "dag.h"
#include "signature.h"

namespace nx {
    class NexusFile;
}

DECLARE_LOG_CATEGORY_EXTERN(NexusInfo, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(NexusErrors, Log, All)

namespace Utils
{
    inline uint8 Read8(uint8*& Buf) { return * Buf++; }

    inline uint16 Read16(uint8*& Buf)
    {
        const uint8 Lo = Read8(Buf);
        const uint8 Hi = Read8(Buf);
        return (static_cast<uint16>(Hi) << 8) | Lo;
    }

    inline uint32 Read32(uint8*& Buf)
    {
        const uint16 Lo = Read16(Buf);
        const uint16 Hi = Read16(Buf);
        return (static_cast<uint32>(Hi) << 16) | Lo;
    }

    inline uint64 Read64(uint8*& Buf)
    {
        const uint32 Lo = Read32(Buf);
        const uint32 Hi = Read32(Buf);
        return (static_cast<uint64>(Hi) << 32) | Lo;
    }

    inline float ReadFloat(uint8*& Buf)
    {
        uint32 Value = Read32(Buf);
        const float ValueAsFloat = *reinterpret_cast<float*>(&Value);
        return ValueAsFloat;
    }
}

namespace DataUtils {
    inline nx::Attribute ReadAttribute(uint8*& Ptr)
    {
        nx::Attribute Attr;
        Attr.type = static_cast<char>(Utils::Read8(Ptr));
        Attr.number = static_cast<char>(Utils::Read8(Ptr));
        return Attr;
    }

    inline nx::Signature ReadSignature(uint8*& Ptr)
    {
        nx::VertexElement Vertex;
        for (int i = 0; i < 8; i ++)
        {
            Vertex.attributes[i] = ReadAttribute(Ptr);
        }
        nx::FaceElement Face;
        for (int i = 0; i < 8; i ++)
        {
            Face.attributes[i] = ReadAttribute(Ptr);
        }

        nx::Signature Sig;
        Sig.vertex = Vertex;
        Sig.face = Face;
        Sig.flags = Utils::Read32(Ptr);
        return Sig;
    }

    nx::Node ReadNode()
    {
#if 0
        auto ReadShort = [&](uint8*& Ptr) {
            uint16 Value = Utils::Read16(Ptr);
            return *(reinterpret_cast<short*>(&Value));
        };
        using namespace Utils;
        const int Size = 44;
        uint8 Header[Size];
        uint8* Ptr = Header;
        File->read(reinterpret_cast<char*>(Header), Size);
        nx::Node TheNode;
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
#endif
        return nx::Node();
    }

    inline nx::Patch ReadPatch()
    {
#if 0
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
#endif
        return nx::Patch();
    }
    nx::Texture ReadTexture()
    {
#if 0
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
#endif
        return nx::Texture();
    }
}
