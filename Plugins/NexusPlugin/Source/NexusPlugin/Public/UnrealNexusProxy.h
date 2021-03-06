﻿#pragma once

#include "UnrealNexusComponent.h"

class FNexusNodeRenderData
{   
public:
    FLocalVertexFactory NodeVertexFactory;
    FVertexBufferWithSRV PositionBuffer;
    FVertexBufferWithSRV ColorBuffer;
    FVertexBufferWithSRV TexCoordsBuffer;
    FVertexBufferWithSRV TangentBuffer;
    FIndexBuffer IndexBuffer;

    UMaterialInstanceDynamic* InstancedMaterial = nullptr;

    bool bHasColors = false;
    
    int NumPrimitives;
    uint32_t CurrentSetTextureID = UINT32_MAX;

    FNexusNodeRenderData(const FUnrealNexusProxy* Proxy, NodeData& Data, Node& Node, UMaterialInstanceDynamic* InInstancedMaterial = nullptr);
    void CreatePositionBuffer(nx::Node& Node, nx::NodeData& Data);
    void InitColorBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node);
    void CreateIndexBuffer(Signature& Sig, Node& Node, nx::NodeData& Data);
    void InitTexBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node);
    static void CalculateTangents(TArray<FPackedNormal>& OutTangents, Signature& TheSig,  NodeData& Data, Node& Node);
    void InitTangentsBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node);
    void InitVertexFactory();
    ~FNexusNodeRenderData();
};

struct FCandidateNode
{
    uint32 ID;
    float FirstNodeError;
    float CandidateError;

    bool operator==(const uint32 OtherID) const
    {
        return OtherID == ID;
    }
    explicit operator uint32 () const
    {
        return ID;
    }
};

enum class EFrustumCullingResult
{
    Inside,
    Intersects,
    Outside
};

class FUnrealNexusProxy final
    : public FPrimitiveSceneProxy
{
    friend class FNexusNodeRenderData;
    friend class UUnrealNexusComponent;
protected:
    class UUnrealNexusData* ComponentData;
    class UUnrealNexusComponent* Component;

    TArray<FBoxSphereBounds> MeshBounds;
    
    FCameraInfo LastCameraInfo;
    TMap<uint32, FNexusNodeRenderData*> LoadedMeshData;
    TArray<FCandidateNode> CandidateNodes;
    bool bIsWireframe = false;
    
    int PendingCount = 0;
    int MaxPending = 0;
    int MinFPS = 15;

    mutable int TotalRenderedCount = 0;
    FMaterialRenderProxy* MaterialProxy;
    FTraversalData LastTraversalData;

    void AddCandidate(uint32 CandidateID, float FirstNodeError);
    void UnloadNode(uint32 WorstID);
    
    // Removes the worst node in the cache until there's enough space to load other nodes
    void FreeCache(Node* BestNode, const uint64 BestNodeID);

    // Removes everything from the cache
    void Flush();
    
    TOptional<TTuple<uint32, Node*>> FindBestNode();

    void RemoveCandidateWithId(const uint32 NodeID);
    void BeginFrame(float DeltaSeconds);
    void Update(FCameraInfo InLastCameraInfo, FTraversalData InLastTraversalData);
    void EndFrame();

    bool IsContainedInViewFrustum(const FVector& SphereCenter, float SphereRadius) const;
    
public:
    explicit FUnrealNexusProxy(UUnrealNexusComponent* TheComponent, const int InMaxPending = 5);

    ~FUnrealNexusProxy();
    void LoadGPUData(uint32 N);
    void DropGPUData(uint32 N);
    
    virtual SIZE_T GetTypeHash() const override
    {
        static uint64 Num;
        return reinterpret_cast<SIZE_T>(&Num);
    }
    
    virtual bool CanBeOccluded() const override;
    virtual uint32 GetMemoryFootprint() const override { return sizeof *this + GetAllocatedSize(); }
    void DrawEdgeNodes(const int ViewIndex, const FSceneView* View, FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags) const;
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
                                        uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
    virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
    TArray<uint32> GetLoadedNodes() const;
};
