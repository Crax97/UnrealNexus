#pragma once
#include "dag.h"
#include "nexusdata.h"

#include "UnrealNexusData.generated.h"

using namespace nx;

UCLASS()
class UUnrealNexusData final : public UObject
{
    GENERATED_BODY()
    
public:
    UUnrealNexusData();
    virtual ~UUnrealNexusData();

    Header Header;
    Node *Nodes;
    Patch *Patches;
    Texture *Textures;
    NodeData *NodeData;
    TextureData *TextureData;
    uint32_t RootsCount;

    vcg::Sphere3f &BoundingSphere();
    bool Intersects(vcg::Ray3f &Ray, float &Distance);
    uint32_t Size(uint32_t Node) const;
};
