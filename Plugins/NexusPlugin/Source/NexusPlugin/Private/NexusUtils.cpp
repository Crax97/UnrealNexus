#include "NexusUtils.h"

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

FStreamableManager& GetStreamableManager()
{
    static FStreamableManager Manager;
    return Manager;
}