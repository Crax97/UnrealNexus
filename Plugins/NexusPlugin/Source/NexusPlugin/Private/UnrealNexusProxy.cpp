#include "UnrealNexusProxy.h"
#include "NexusJobExecutorThread.h"

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
        Indices[i * 3 + (WindingOrder == EWindingOrder::Clockwise ? 0 : 2)] = FaceIndices[i * 3 + 0];
        Indices[i * 3 + 1] = FaceIndices[i * 3 + 1];
        Indices[i * 3 + (WindingOrder == EWindingOrder::Clockwise ? 2 : 0)] = FaceIndices[i * 3 + 2];
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
        const FVector Vertex{ Point.X(), Point.Y(), Point.Z() };
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

void FUnrealNexusProxy::Update()
{
    if (this->PendingCount >= this->MaxPending)
        return;


    const auto OptionalBestNode = FindBestNode();
    if (!OptionalBestNode)
    {
        return;
    }
    
    Node* BestNode = OptionalBestNode.GetValue().Value;
    const UINT32 BestNodeID = OptionalBestNode.GetValue().Key;

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

void FUnrealNexusProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
    const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
    {

        const auto& EngineShowFlags = ViewFamily.EngineShowFlags;
	
        if (!(VisibilityMap & (1 << ViewIndex))) continue;
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
    }
}

FPrimitiveViewRelevance FUnrealNexusProxy::GetViewRelevance(const FSceneView* View) const
{
    // Partially copied from StaticMeshRenderer.cpp
    FPrimitiveViewRelevance Result;
    Result.bOpaque = true;
    Result.bDrawRelevance = true;
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
