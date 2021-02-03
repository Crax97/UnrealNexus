#include "UnrealNexusProxy.h"
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

void FNexusNodeRenderData::CalculateTangents(TArray<FPackedNormal>& OutTangents, Signature& TheSig,  NodeData& Data, Node& Node)
{
    TArray<FVector> Tang1;
    TArray<FVector> Tang2;

    Tang1.SetNum(Node.nvert);
    Tang2.SetNum(Node.nvert);
    vcg::Point3f* Vertices = Data.coords();
    vcg::Point3s* Normals = Data.normals(TheSig, Node.nvert);
    vcg::Point2f* TexCoords = Data.texCoords(TheSig, Node.nvert);
    uint16* Indices = Data.faces(TheSig, Node.nvert);

    for (uint32 Index = 0; Index < Node.nface; Index += 3)
    {
        uint32 Index1 = Indices[Index * 3 + 0];
        uint32 Index2 = Indices[Index * 3 + 1];
        uint32 Index3 = Indices[Index * 3 + 2];
        vcg::Point3f Vertex1 =  Vertices[Index1];
        vcg::Point3f Vertex2 =  Vertices[Index2];
        vcg::Point3f Vertex3 =  Vertices[Index3];
        
        vcg::Point2f TexCoord1 =  TexCoords[Index1];
        vcg::Point2f TexCoord2 =  TexCoords[Index2];
        vcg::Point2f TexCoord3 =  TexCoords[Index3];

        float X1 = Vertex2.X() - Vertex1.X();
        float X2 = Vertex3.X() - Vertex1.X();
        float Y1 = Vertex2.Y() - Vertex1.Y();
        float Y2 = Vertex3.Y() - Vertex1.Y();
        float Z1 = Vertex2.Z() - Vertex1.Z();
        float Z2 = Vertex3.Z() - Vertex1.Z();
        
        float S1 = TexCoord2.X() - TexCoord1.X();
        float S2 = TexCoord3.X() - TexCoord1.X();
        float T1 = TexCoord2.Y() - TexCoord1.Y();
        float T2 = TexCoord3.Y() - TexCoord1.Y();

        float R = 1.0f / (S1 * T2 - S2 * T1);

        // TODO Invert Y and Z
        FVector SDir = FVector((T2 * X1 - T1 * X2), (T2 * Y1 - T1* Y2), (T2 * Z1 - T1 * Z2)) * R;
        FVector TDir = FVector((S1 * X2 - S2 * X1), (S1* Y2 - S2 * Y1), (S1 * Z2 - S2 * Z1)) * R;

        Tang1[Index1] += SDir;
        Tang1[Index2] += SDir;
        Tang1[Index3] += SDir;
        
        Tang2[Index1] += TDir;
        Tang2[Index2] += TDir;
        Tang2[Index3] += TDir;
    }

    auto Point3SToVector = [](const vcg::Point3s Point)
    {
        return FVector(Point.X(), Point.Y(), Point.Z());
    };
    
    for (uint32 Index = 0; Index < Node.nvert; Index ++)
    {
        FVector Normal = Point3SToVector(Normals[Index]);
        FVector& Tangent = Tang1[Index];
        
        FVector Calculated = (Tangent - Normal * FVector::DotProduct(Normal, Tangent)).GetSafeNormal();
        float W = (FVector::DotProduct(FVector::CrossProduct(Normal, Tangent), Tang2[Index])) < 0.0f ? -1.0f : 1.0f;
        
        OutTangents[Index * 2 + 0] = FPackedNormal(FVector4(Calculated, W));
        OutTangents[Index * 2 + 1] = FPackedNormal(FVector4(FVector::CrossProduct(Calculated, Normal), W));
    }
}

void FNexusNodeRenderData::InitTangentsBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node)
{

    TArray<FPackedNormal> Tangents;
    Tangents.SetNum(Node.nvert * 2);
    CalculateTangents(Tangents, TheSig, Data, Node);
    TangentBuffer.VertexBufferRHI = CreateBufferAndFillWithData<void>(Tangents.GetData(), Node.nvert * 2 * sizeof(FPackedNormal));
    TangentBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(FShaderResourceViewInitializer(TangentBuffer.VertexBufferRHI, PF_R8G8B8A8_SNORM));
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
                Params.TangentBuffer,
                0,
                2 * sizeof(FPackedNormal),
                VET_PackedNormal
            );

            Data.TangentBasisComponents[1] = FVertexStreamComponent(
                Params.TangentBuffer,
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
        
            Data.LightMapCoordinateIndex = 0;
            Data.NumTexCoords = 1;

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
