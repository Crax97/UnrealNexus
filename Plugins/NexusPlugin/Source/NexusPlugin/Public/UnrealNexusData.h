#pragma once
#include "dag.h"
#include "nexusdata.h"
#include "Engine/StreamableManager.h"

#include "UnrealNexusData.generated.h"

using namespace nx;

USTRUCT()
struct FUnrealNexusNode {
    GENERATED_BODY()
    
    nx::Node NexusNode;
    TArray<nx::Patch> NodePatches;
    FSoftObjectPath NodeDataPath;
    void Serialize(FArchive& Ar);
};

UCLASS()
class NEXUSPLUGIN_API UUnrealNexusData final : public UObject
{
    GENERATED_BODY()

    void SerializeHeader(FArchive& Archive);

    void SerializeSignature(FArchive& Archive);
    static void SerializeAttribute(FArchive& Archive, Attribute& Attribute);
    static void SerializeVertexAttributes(FArchive& Archive, VertexElement& Vertex);
    static void SerializeFaceAttributes(FArchive& Archive, FaceElement& Face);
    static void SerializeSphere(FArchive& Archive, vcg::Sphere3f& Sphere);
    void SerializeNodes(FArchive& Archive);
    // void SerializePatches(FArchive& Archive) const;
    void SerializeTextures(FArchive& Archive) const;
    
public:
    UUnrealNexusData();
    virtual ~UUnrealNexusData();

    Header Header;

    UPROPERTY()
    TArray<FUnrealNexusNode> Nodes;
    
    UPROPERTY()
    int RootsCount;
    
    TMap<UINT32, TSharedPtr<FStreamableHandle>> NodeHandles;

    vcg::Sphere3f &BoundingSphere();
    bool Intersects(vcg::Ray3f &Ray, float &Distance);
    uint32_t Size(uint32_t Node);
    void LoadNodeAsync(const UINT32 NodeID, FStreamableDelegate Callback);
    void UnloadNode(const int NodeID);
    
    class UUnrealNexusNodeData* GetNode(UINT32 NodeId);

    // Unreal engine specific stuff
    // Begin UObject interface
    virtual void Serialize( FArchive& Archive ) override;
    // End UObject interface
};