#pragma once
#include "dag.h"
#include "nexusdata.h"

#include "UnrealNexusData.generated.h"

using namespace nx;

UCLASS()
class UUnrealNexusData final : public UObject
{
    GENERATED_BODY()

    void SerializeHeader(FArchive& Archive);
    void SerializeSignature(FArchive& Archive);
    static void SerializeAttribute(FArchive& Archive, const Attribute& Attribute);
    static void SerializeVertexAttributes(FArchive& Archive, const VertexElement& Vertex);
    static void SerializeFaceAttributes(FArchive& Archive, const FaceElement& Face);
    static void SerializeSphere(FArchive& Archive, const vcg::Sphere3f& Sphere);
    void SerializeNodes(FArchive& Archive) const;
    void SerializePatches(FArchive& Archive) const;
    void SerializeTextures(FArchive& Archive) const;
    
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
    // Unreal engine specific stuff
    // Begin UObject interface
    virtual void Serialize( FArchive& Archive ) override;
    // End UObject interface
};
