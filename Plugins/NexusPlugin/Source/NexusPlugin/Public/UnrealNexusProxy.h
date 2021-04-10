#pragma once

#include "UnrealNexusComponent.h"

class FNexusNodeRenderData
{   
public:
    FLocalVertexFactory NodeVertexFactory;
    FVertexBufferWithSRV PositionBuffer;
    FVertexBufferWithSRV TexCoordsBuffer;
    FVertexBufferWithSRV TangentBuffer;
    FIndexBuffer IndexBuffer;
    int NumPrimitives;

    FNexusNodeRenderData(const FUnrealNexusProxy* Proxy, NodeData& Data, Node& Node);
    void CreatePositionBuffer(nx::Node& Node, nx::NodeData& Data);
    void CreateIndexBuffer(Signature& Sig, Node& Node, nx::NodeData& Data);
    void InitTexBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node);
    static void CalculateTangents(TArray<FPackedNormal>& OutTangents, Signature& TheSig,  NodeData& Data, Node& Node);
    void InitTangentsBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node);
    void InitVertexFactory();
    ~FNexusNodeRenderData();
};

struct FCandidateNode
{
    UINT32 ID;
    float FirstNodeError;
    float CandidateError;

    bool operator==(const UINT32 OtherID) const
    {
        return OtherID == ID;
    }
    explicit operator UINT32 () const
    {
        return ID;
    }
};

class FUnrealNexusProxy final
    : public FPrimitiveSceneProxy
{
    friend class FNexusNodeRenderData;
    friend class UUnrealNexusComponent;
protected:
    class UUnrealNexusData* ComponentData;
    class UUnrealNexusComponent* Component;
    
    class FRunnableThread* JobThread;
    class FNexusJobExecutorThread* JobExecutor;

    TArray<FBoxSphereBounds> MeshBounds;
    
    FCameraInfo LastCameraInfo;
    TMap<uint32, FNexusNodeRenderData*> LoadedMeshData;
    TArray<FCandidateNode> CandidateNodes;
    bool bIsWireframe = false;
    bool bIsReady = false;
    bool bIsPlaying = false;
    
    int PendingCount = 0;
    int MaxPending = 0;
    int MinFPS = 15;

    mutable int TotalRenderedCount = 0;
    FMaterialRenderProxy* MaterialProxy;

    void AddCandidate(UINT32 CandidateID, float FirstNodeError);
    void UnloadNode(UINT32 WorstID);

    // Removes the worst node in the cache until there's enough space to load other nodes
    void FreeCache(Node* BestNode, const UINT64 BestNodeID);

    // Removes everything from the cache
    void Flush();
    
    TOptional<TTuple<UINT32, Node*>> FindBestNode();

    void RemoveCandidateWithId(const UINT32 NodeID);
    void BeginFrame(float DeltaSeconds);
    void Update(FCameraInfo InLastCameraInfo);
    void EndFrame();

    bool IsNotOutsideViewFrustum(const FVector& SphereCenter, float SphereRadius) const;
    
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
    TArray<UINT32> GetLoadedNodes() const;
    void GetReady()
    {
        bIsReady = true;
        bIsPlaying = true;
    }
    bool IsReady() const { return bIsReady; }
};
