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
    void SerializeTextures(FArchive& Archive);    

    void SerializeNodes(FArchive& Archive);

    
public:
    UUnrealNexusData();
    virtual ~UUnrealNexusData();

    Header Header;

    UPROPERTY()
    TArray<FUnrealNexusNode> Nodes;

    UPROPERTY()
    TArray<FSoftObjectPath> NodeTexturesPaths;
    
    UPROPERTY()
    int RootsCount;

    TMap<uint32, TSharedPtr<FStreamableHandle>> NodeHandles;
    TMap<uint32, TSharedPtr<FStreamableHandle>> NodeTexturesHandles;



    vcg::Sphere3f &BoundingSphere();
    bool Intersects(vcg::Ray3f &Ray, float &Distance);
    uint32_t Size(uint32_t Node);
    void LoadNodeAsync(const uint32 NodeID, FStreamableDelegate Callback);
    void UnloadNode(const int NodeID);
    void LoadTextureForNode(const uint32 NodeID, FStreamableDelegate Callback);
    UTexture2D* GetTexture(const uint32 TextureID);
    void UnloadTexture(const int NodeID);
    
    class UUnrealNexusNodeData* GetNode(uint32 NodeId);

    // Unreal engine specific stuff
    // Begin UObject interface
    virtual void Serialize( FArchive& Archive ) override;
    // End UObject interface
};
