﻿#pragma once
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
    nx::Node ReadNode();
    inline nx::Patch ReadPatch();
    nx::Texture ReadTexture();
}
