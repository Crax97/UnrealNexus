﻿#include "UnrealNexusProxy.h"
#include "NexusJobExecutorThread.h"
#include "Animation/AnimCompress.h"
#include "Materials/MaterialInstance.h"

template <class T>
FVertexBufferRHIRef CreateBufferAndFillWithData(const T* Data, const SIZE_T Size)
{
    FRHIResourceCreateInfo CreateInfo;
    FVertexBufferRHIRef Buffer = RHICreateVertexBuffer(Size, BUF_Static | BUF_ShaderResource, CreateInfo);
    void* Pointer = RHILockVertexBuffer(Buffer, 0, Size, RLM_WriteOnly);
    FMemory::Memcpy(Pointer, Data, Size);
    RHIUnlockVertexBuffer(Buffer);
    return Buffer;
}

FNexusNodeRenderData::FNexusNodeRenderData(const FUnrealNexusProxy* Proxy, NodeData& Data, Node& Node)
    : NodeVertexFactory(Proxy->GetScene().GetFeatureLevel(), "NexusNodeVertexFactory")
{
    check(IsInRenderingThread());

    Signature& TheSig = Proxy->ComponentData->header.signature;
    NumPrimitives = Node.nface;
    CreatePositionBuffer(Node, Data);
    CreateIndexBuffer(TheSig, Node, Data, Proxy->Component->WindingOrder);
    
    InitTexBuffer(Proxy,  TheSig, Data, Node);
    InitTangentsBuffer(Proxy, TheSig, Data, Node);
    InitVertexFactory();
}

void FNexusNodeRenderData::CreateIndexBuffer(Signature& Sig, Node& Node,  nx::NodeData& Data, EWindingOrder WindingOrder)
{
    check(Sig.face.hasIndex())
    TArray<uint16> Indices;
    Indices.SetNum(Node.nface * 3);
    uint16* FaceIndices = Data.faces(Sig, Node.nvert);
    for (int i = 0; i < Node.nface; i++)
    {
        Indices[i * 3 + 0] = FaceIndices[i * 3 + 0];
        Indices[i * 3 + 1] = FaceIndices[i * 3 + 1];
        Indices[i * 3 + 2] = FaceIndices[i * 3 + 2];
    }

    FRHIResourceCreateInfo Info;
    IndexBuffer.IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), Indices.Num() * sizeof(uint16), BUF_Static, Info);
    void* Pointer = RHILockIndexBuffer(IndexBuffer.IndexBufferRHI, 0, Indices.Num() * sizeof(uint16), RLM_WriteOnly);
    FMemory::Memcpy(Pointer, Indices.GetData(), Indices.Num() * sizeof(uint16));
    RHIUnlockIndexBuffer(IndexBuffer.IndexBufferRHI);
    BeginInitResource(&IndexBuffer);
    
}

void FNexusNodeRenderData::CreatePositionBuffer(nx::Node& Node, nx::NodeData& Data)
{
    TArray<FVector> Vertices;
    Vertices.SetNum(Node.nvert);
    for (int i = 0; i < Node.nvert; i++)
    {
        vcg::Point3f Point = Data.coords()[i];
        const FVector Vertex{ Point.X(), Point.Z(), Point.Y()  };
        Vertices[i] = Vertex;
    }

    PositionBuffer.VertexBufferRHI = CreateBufferAndFillWithData(Vertices.GetData(), Vertices.Num() * sizeof(FVector));
    PositionBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(FShaderResourceViewInitializer(PositionBuffer.VertexBufferRHI, PF_R32_FLOAT));
    BeginInitResource(&PositionBuffer);
}

void FNexusNodeRenderData::InitTexBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node)
{
    TArray<FVector2D> TexCoords;
    TexCoords.SetNum(Node.nvert);
    if (TheSig.vertex.hasTextures()) {
        vcg::Point2f* Points = Data.texCoords(TheSig, Node.nvert);
        for (int i = 0; i < Node.nvert; i++) {
            vcg::Point2f Coord = Points[i];
            TexCoords[i] = { Coord.X(), Coord.Y() };
        }
        // Same number as verts?
    }
    else {
        FMemory::Memset(TexCoords.GetData(), 0, TexCoords.Num() * sizeof(FVector2D));
    }
    TexCoordsBuffer.VertexBufferRHI = CreateBufferAndFillWithData(TexCoords.GetData(), TexCoords.Num() * sizeof(FVector2D));
    // TODO Check if using half precision
    TexCoordsBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(FShaderResourceViewInitializer(TexCoordsBuffer.VertexBufferRHI, PF_G32R32F));
    BeginInitResource(&TexCoordsBuffer);
}

void FNexusNodeRenderData::InitTangentsBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node)
{
    TArray<FPackedNormal> Tangents;
    Tangents.SetNum(Node.nvert * 2);
    for (int i = 0; i < Node.nvert; i++)
    {
        // TODO: Actually calculate tangents
        vcg::Point3s Normal = Data.normals(TheSig, Node.nvert)[i];
        Tangents[i * 2 + 0] = (FPackedNormal{ FVector{static_cast<float>(Normal.X()), static_cast<float>(Normal.Y()), static_cast<float>(Normal.Z())} });
        Tangents[i * 2 + 1] = (FPackedNormal{ FVector{static_cast<float>(Normal.X()), static_cast<float>(Normal.Y()), static_cast<float>(Normal.Z())} });
    }
    TangentBuffer.VertexBufferRHI = CreateBufferAndFillWithData<void>(Tangents.GetData(), Node.nvert * 2 * sizeof(FPackedNormal));
    TangentBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(FShaderResourceViewInitializer(TangentBuffer.VertexBufferRHI, PF_R32_FLOAT));
    BeginInitResource(&TangentBuffer);
}

void FNexusNodeRenderData::InitVertexFactory()
{

    struct FFactoryInitParams
    {
        FLocalVertexFactory* VertexFactory;
        FVertexBufferWithSRV* PositionBuffer;
        FVertexBufferWithSRV* TexCoordsBuffer;
        FVertexBufferWithSRV* TangentBuffer;
        // All the other members
    } Params;
    Params.VertexFactory = &NodeVertexFactory;
    Params.PositionBuffer = &PositionBuffer;
    Params.TexCoordsBuffer = &TexCoordsBuffer;
    Params.TangentBuffer = &TangentBuffer;

    ENQUEUE_RENDER_COMMAND(NodeInitVertexFactory)([Params](FRHICommandListImmediate& Commands)
        {
            FLocalVertexFactory::FDataType Data;
            Data.PositionComponent = FVertexStreamComponent(
                Params.PositionBuffer,
                0,
                sizeof(FVector),
                VET_Float3
            );
            Data.PositionComponentSRV = Params.PositionBuffer->ShaderResourceViewRHI;

            Data.TextureCoordinates.Add(FVertexStreamComponent(
                Params.TexCoordsBuffer,
                0,
                sizeof(FVector2D),
                VET_Float2
            ));
            Data.TextureCoordinatesSRV = Params.TexCoordsBuffer->ShaderResourceViewRHI;

            Data.TangentBasisComponents[0] = FVertexStreamComponent(
                Params.TexCoordsBuffer,
                0,
                2 * sizeof(FPackedNormal),
                VET_PackedNormal
            );

            Data.TangentBasisComponents[1] = FVertexStreamComponent(
                Params.TexCoordsBuffer,
                sizeof(FPackedNormal),
                2 * sizeof(FPackedNormal),
                VET_PackedNormal
            );
            Data.TangentsSRV = Params.TangentBuffer->ShaderResourceViewRHI;

            Data.LightMapCoordinateComponent = FVertexStreamComponent(
                Params.TexCoordsBuffer,
                0,
                sizeof(FVector2D),
                VET_Float2
            );

            FColorVertexBuffer::BindDefaultColorVertexBuffer(Params.VertexFactory, Data, FColorVertexBuffer::NullBindStride::FColorSizeForComponentOverride);
            Params.VertexFactory->SetData(Data);
            Params.VertexFactory->InitResource();

        });
}

FNexusNodeRenderData::~FNexusNodeRenderData()
{
    BeginReleaseResource(&NodeVertexFactory);
    BeginReleaseResource(&PositionBuffer);
    BeginReleaseResource(&TexCoordsBuffer);
    BeginReleaseResource(&TangentBuffer);
    BeginReleaseResource(&IndexBuffer);
}

void FUnrealNexusProxy::AddCandidate(UINT32 CandidateID, float FirstNodeError)
{
    CandidateNodes.Add(FCandidateNode {CandidateID, FirstNodeError});
}

void FUnrealNexusProxy::FreeCache(Node* BestNode)
{
    while (this->CurrentCacheSize > this->MaxCacheSize)
    {
        Node* Worst = nullptr;
        UINT32 WorstID = 0;
        TArray<UINT32> LoadedNodes;
        LoadedMeshData.GenerateKeyArray(LoadedNodes);
        for (UINT32 ID = 0; ID < ComponentData->header.n_nodes; ID ++)
        {
            Node* SelectedNode = &ComponentData->nodes[ID];
            const float SelectedNodeError = Component->GetErrorForNode(ID);
            if (!Worst || SelectedNodeError < Component->GetErrorForNode(WorstID))
            {
                Worst = SelectedNode;
                WorstID = ID;
            }
        }
        if (!Worst || Worst->error >= BestNode->error * 0.9f)
        {
            return;
        }
        Component->SetNodeStatus(WorstID, ENodeStatus::Dropped);
        DropGPUData(WorstID);
    }

}

void FUnrealNexusProxy::Flush()
{
    if (LoadedMeshData.Num() == 0) 
        return;
    TArray<UINT32> LoadedNodes;
    LoadedMeshData.GetKeys(LoadedNodes);
    for (UINT32 Node : LoadedNodes)
    {
        DropGPUData(Node);
    }
}

TOptional<TTuple<UINT32, Node*>> FUnrealNexusProxy::FindBestNode()
{
    Node* BestNode = nullptr;
    UINT32 BestNodeID = 0;
    for (FCandidateNode& CandidateNode : CandidateNodes)
    {
        Node* Candidate = &ComponentData->nodes[CandidateNode.ID];
        if (!Component->IsNodeLoaded(CandidateNode.ID) && ( !BestNode || CandidateNode.FirstNodeError > BestNode->error))
        {
            BestNode = Candidate;
            BestNodeID = CandidateNode.ID;
        }
    }
    return BestNode == nullptr ? TOptional<TTuple<UINT32, Node*>>() : TTuple<UINT32, Node*>{BestNodeID, BestNode};
}

void FUnrealNexusProxy::RemoveCandidateWithId(const UINT32 NodeID)
{
    int NodeIndex = -1;
    for (int i = 0; i < CandidateNodes.Num(); i ++)
    {
        FCandidateNode& Candidate = CandidateNodes[i];
        if (Candidate.ID == NodeID)
        {
            NodeIndex = i;
            break;
        }
    }
    if (NodeIndex != -1)
    {
        CandidateNodes.RemoveAt(NodeIndex);
    }
}

void FUnrealNexusProxy::BeginFrame(float DeltaSeconds)
{
    const float FPS = 1.0 / DeltaSeconds;
    if (MinFPS > 0)
    {
        const float Ratio = MinFPS / FPS;
        if (Ratio > 1.1f)
        {
            CurrentError *= 1.05;
        } else if (Ratio < 0.9f)
        {
            CurrentError *= 0.95;
        }
        CurrentError = FMath::Max(TargetError, FMath::Min(MaxError, CurrentError));
    }
    CandidateNodes.Empty();
    CurrentError = FMath::Max(TargetError, FMath::Min(MaxError, CurrentError));
}

void FUnrealNexusProxy::Update(FTraversalData InTraversalData)
{
    LastTraversalData = InTraversalData;
    if (this->PendingCount >= this->MaxPending)
        return;
    

    const auto OptionalBestNode = FindBestNode();
    if (!OptionalBestNode)
    {
        return;
    }
    
    Node* BestNode = OptionalBestNode.GetValue().Value;
    const UINT32 BestNodeID = OptionalBestNode.GetValue().Key;

    // TODO: Pick a better name
    FreeCache(BestNode);
    
    RemoveCandidateWithId(BestNodeID);
    Component->SetNodeStatus(BestNodeID, ENodeStatus::Pending);
    JobExecutor->AddNewJobs({ FNexusJob{ EJobKind::Load, BestNodeID, ComponentData } });
    
    FNexusJob DoneJob;
    TQueue<FNexusJob>& FinishedJobs = JobExecutor->GetJobsDone();
    while (FinishedJobs.Dequeue(DoneJob))
    {
        Component->SetNodeStatus(DoneJob.NodeIndex, ENodeStatus::Loaded);
        LoadGPUData(DoneJob.NodeIndex);
        UE_LOG(NexusInfo, Log, TEXT("Loaded Nexus node with index %d"), DoneJob.NodeIndex);
    }
}

FUnrealNexusProxy::FUnrealNexusProxy(UUnrealNexusComponent* TheComponent, const int InMaxPending,
const int InMaxCacheSize)
    : FPrimitiveSceneProxy(static_cast<UPrimitiveComponent*>(TheComponent)),
        ComponentData(TheComponent->ComponentData),
        Component(TheComponent),
        MaxPending(InMaxPending),
        MaxCacheSize(InMaxCacheSize)
{
    JobExecutor = new FNexusJobExecutorThread(ComponentData->file);
    JobThread = FRunnableThread::Create(JobExecutor, TEXT("Nexus Node Loader"));
    SetWireframeColor(FLinearColor::Green);

    MaterialProxy = Component->ModelMaterial == nullptr ?
                    UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy() : Component->ModelMaterial->GetRenderProxy();
}

FUnrealNexusProxy::~FUnrealNexusProxy()
{
    // Kill the Job thread
    JobExecutor->Stop();
    JobThread->Kill();
    delete JobExecutor;
    delete JobThread;
    
}

void FUnrealNexusProxy::LoadGPUData(const uint32 N)
{
    Node& TheNode = ComponentData->nodes[N];
    NodeData& TheData = ComponentData->nodedata[N];
    check(TheData.memory);

    this->PendingCount ++;
    this->CurrentCacheSize += TheNode.getSize();
    
    ENQUEUE_RENDER_COMMAND(NexusLoadGPUData)([&, N](FRHICommandListImmediate& Commands)
    {
        FNexusNodeRenderData* Data = new FNexusNodeRenderData(this, TheData, TheNode);
        LoadedMeshData.Add(N, Data);
        this->PendingCount --;
    });
}

void FUnrealNexusProxy::DropGPUData(uint32 N)
{
    check(LoadedMeshData.Contains(N));
    Node* TheNode = &ComponentData->nodes[N];
    this->CurrentCacheSize -= TheNode->getSize();
    ENQUEUE_RENDER_COMMAND(NexusLoadGPUData)([&, N](FRHICommandListImmediate& Commands)
    {
        LoadedMeshData.Remove(N);
    });
}


bool FUnrealNexusProxy::CanBeOccluded() const
{
    return true;
}

void FUnrealNexusProxy::DrawEdgeNodes(const int ViewIndex, const FSceneView* View, FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags) const
{
    int RenderedCount = 0;
    const TSet<UINT32>& SelectedNodes = LastTraversalData.SelectedNodes;
    for (UINT32 Id : SelectedNodes)
    {
        FNexusNodeRenderData* Data = LoadedMeshData[Id];

        // Detecting if this node is on the edge
        Node& CurrentNode = ComponentData->nodes[Id];
        Node& NextNode = ComponentData->nodes[Id + 1];
        bool IsVisible = false;
        const UINT32 NextNodeFirstPatch = NextNode.first_patch;
        for (UINT32 PatchId = CurrentNode.first_patch; PatchId < NextNodeFirstPatch; PatchId ++)
        {
            const int ChildNode = ComponentData->patches[PatchId].node;
            if (!SelectedNodes.Contains(ChildNode))
            {
                IsVisible = true;
                break;
            }
        }
        if(!IsVisible) continue;

        int Offset = 0;
        int EndIndex = 0;
        for (UINT32 PatchId = CurrentNode.first_patch; PatchId < NextNodeFirstPatch; PatchId ++)
        {
            const Patch& CurrentNodePatch = ComponentData->patches[PatchId];
            const UINT32 ChildNode = CurrentNodePatch.node;
            if (!SelectedNodes.Contains(ChildNode))
            {
                EndIndex = CurrentNodePatch.triangle_offset;
                if (PatchId < NextNodeFirstPatch - 1) // TODO: Ask prof if moving if out can solve this
                    continue;
            }
            
            if (EndIndex > Offset)
            {
                const bool bIsWireframeView = AllowDebugViewmodes() && View->Family->EngineShowFlags.Wireframe;

                FMeshBatch& Mesh = Collector.AllocateMesh();
                Mesh.bWireframe = false;
                Mesh.VertexFactory = &Data->NodeVertexFactory;
                Mesh.Type = PT_TriangleList;
                Mesh.DepthPriorityGroup = SDPG_World;
                Mesh.bUseAsOccluder = true;
                Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
                if (bIsWireframeView)
                {
                    const auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
                        GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
                        FLinearColor(0, 0.5f, 1.f)
                        );

                    Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

                    Mesh.MaterialRenderProxy = WireframeMaterialInstance;

                    Mesh.bWireframe = true;
                    Mesh.bCanApplyViewModeOverrides = false;
                }
                else
                {
                    Mesh.MaterialRenderProxy = MaterialProxy;
                }
    
                auto& Element = Mesh.Elements[0];
                Element.IndexBuffer = &Data->IndexBuffer;
                Element.FirstIndex = Offset * 3;
                Element.NumPrimitives = (EndIndex - Offset);
                Collector.AddMesh(ViewIndex, Mesh);
                RenderedCount += (EndIndex - Offset);
            }
            Offset = CurrentNodePatch.triangle_offset;
        } 
    }

    TotalRenderedCount += RenderedCount;
}

void FUnrealNexusProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                               const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
    if(!bIsReady) return;
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
    {

        const auto& EngineShowFlags = ViewFamily.EngineShowFlags;

        /*
         * TODO: 1) Implementare algoritmo di selezione (Nexus3D.js:316)
         * Alternativa 1) Direttamente sul rendering thread
         * Alternativa 2) Sul game thread, poi mi passo la roba sul rendering thread
         */
        if (!(VisibilityMap & (1 << ViewIndex))) continue;

        DrawEdgeNodes(ViewIndex, Views[ViewIndex], Collector, EngineShowFlags);

#if 0
        for (const auto& NodeIndexAndRenderingData : LoadedMeshData) 
        {
            FNexusNodeRenderData* Data = NodeIndexAndRenderingData.Value;
            FMeshBatch& Mesh = Collector.AllocateMesh();
            Mesh.bWireframe = EngineShowFlags.Wireframe;
            Mesh.VertexFactory = &Data->NodeVertexFactory;
            Mesh.Type = PT_TriangleList;
            Mesh.DepthPriorityGroup = SDPG_World;
            Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
    
            auto& Element = Mesh.Elements[0];
            Element.IndexBuffer = &Data->IndexBuffer;
            Element.FirstIndex = 0;
            Element.NumPrimitives = Data->NumPrimitives;
            Collector.AddMesh(ViewIndex, Mesh);
        }
#endif
    }
}

FPrimitiveViewRelevance FUnrealNexusProxy::GetViewRelevance(const FSceneView* View) const
{
    // Partially copied from StaticMeshRenderer.cpp
    FPrimitiveViewRelevance Result;
    Result.bOpaque = true;
    Result.bDrawRelevance = IsShown(View);
    Result.bShadowRelevance = IsShadowCast(View);
    Result.bRenderInMainPass = true;
    Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;
    Result.bDynamicRelevance = true;
    Result.bStaticRelevance = false;
    return Result;
}

TArray<UINT32> FUnrealNexusProxy::GetLoadedNodes() const
{
    TArray<UINT32> LoadedNodes;
    LoadedMeshData.GetKeys(LoadedNodes);
    return LoadedNodes;
}
