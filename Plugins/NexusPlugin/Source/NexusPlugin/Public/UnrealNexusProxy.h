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
    void CreateIndexBuffer(Signature& Sig, Node& Node, nx::NodeData& Data, EWindingOrder WindingOrder);
    void InitTexBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node);
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
    class FUnrealNexusData* ComponentData;
    class UUnrealNexusComponent* Component;
    
    class FRunnableThread* JobThread;
    class FNexusJobExecutorThread* JobExecutor;
    
    TMap<uint32, FNexusNodeRenderData*> LoadedMeshData;
    TArray<FCandidateNode> CandidateNodes;
    bool bIsWireframe = false;
    int PendingCount = 0;
    int CurrentCacheSize = 0;
    int MaxPending = 0;
    int MaxCacheSize = 0;

    const float TargetError = 2.0f;
    float CurrentError = TargetError;
    
    void AddCandidate(UINT32 CandidateID, float FirstNodeError);

    // Removes the worst node in the cache until there's enough space to load other nodes
    void FreeCache(Node* BestNode);

    // Removes everything from the cache
    void Flush();
    
    TOptional<TTuple<UINT32, Node*>> FindBestNode();

    void RemoveCandidateWithId(const UINT32 NodeID);

    void Update();
    
public:
    explicit FUnrealNexusProxy(UUnrealNexusComponent* TheComponent, const int InMaxPending = 5,
                                const int InMaxCacheSize = 512 *(1<<20));

    ~FUnrealNexusProxy();

    void LoadGPUData(const uint32 N);
    void DropGPUData(uint32 N);
    
    float GetCurrentError() const
    {
        return CurrentError;
    }
    
    virtual SIZE_T GetTypeHash() const override
    {
        static uint64 Num;
        return reinterpret_cast<SIZE_T>(&Num);
    }
    
    virtual uint32 GetMemoryFootprint() const override { return sizeof *this + GetAllocatedSize(); }
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
    TArray<UINT32> GetLoadedNodes() const;
};
