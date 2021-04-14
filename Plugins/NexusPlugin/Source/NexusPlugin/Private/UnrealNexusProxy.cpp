#include "UnrealNexusProxy.h"

#include "DrawDebugHelpers.h"
#include "UnrealNexusData.h"
#include "UnrealNexusNodeData.h"

#include "NexusCommons.h"
#include "NexusJobExecutorThread.h"
#include "Animation/AnimCompress.h"
#include "Materials/MaterialInstance.h"

using namespace NexusCommons;

DECLARE_STATS_GROUP(TEXT("Unreal Nexus Render Proxy"), STATGROUP_NexusRenderer, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Unreal Nexus Render Update Statistics"), STATID_NexusRenderer, STATGROUP_NexusRenderer)
DECLARE_CYCLE_STAT(TEXT("Unreal Nexus Render Node Selection Statistics"), STATID_NexusNodeSelection, STATGROUP_NexusRenderer)

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

    Signature& TheSig = Proxy->ComponentData->Header.signature;
    NumPrimitives = Node.nface;
    CreatePositionBuffer(Node, Data);
    CreateIndexBuffer(TheSig, Node, Data);

    InitColorBuffer(Proxy, TheSig, Data, Node);
    InitTexBuffer(Proxy,  TheSig, Data, Node);
    InitTangentsBuffer(Proxy, TheSig, Data, Node);
    InitVertexFactory();
}

void FNexusNodeRenderData::CreateIndexBuffer(Signature& Sig, Node& Node,  nx::NodeData& Data)
{
    check(Sig.face.hasIndex())
    uint16* FaceIndices = Data.faces(Sig, Node.nvert);
    const SIZE_T IndicesCount = Node.nface * 3;
    
    FRHIResourceCreateInfo Info;
    IndexBuffer.IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndicesCount * sizeof(uint16) , BUF_Static, Info);
    void* Pointer = RHILockIndexBuffer(IndexBuffer.IndexBufferRHI, 0, IndicesCount * sizeof(uint16), RLM_WriteOnly);
    FMemory::Memcpy(Pointer, FaceIndices, IndicesCount * sizeof(uint16));
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


void FNexusNodeRenderData::InitColorBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node)
{
    bHasColors = TheSig.vertex.hasColors();
    if (bHasColors)
    {
        ColorBuffer.VertexBufferRHI = CreateBufferAndFillWithData(Data.colors(TheSig, Node.nvert), Node.nvert * sizeof(FColor));
        ColorBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(FShaderResourceViewInitializer(ColorBuffer.VertexBufferRHI, PF_R8G8B8A8));
        BeginInitResource(&ColorBuffer);
    }
}

void FNexusNodeRenderData::InitTexBuffer(const FUnrealNexusProxy* Proxy, Signature& TheSig, NodeData& Data, Node& Node)
{
    if (TheSig.vertex.hasTextures()) {
        TexCoordsBuffer.VertexBufferRHI = CreateBufferAndFillWithData(Data.texCoords(TheSig, Node.nvert), Node.nvert * sizeof(vcg::Point2f));
    } else {
        TArray<FVector2D> TexCoords;
        TexCoords.SetNum(Node.nvert);
        UE_LOG(NexusInfo, Display, TEXT("This node has no textures"));
        FMemory::Memset(TexCoords.GetData(), 0, TexCoords.Num() * sizeof(FVector2D));
        TexCoordsBuffer.VertexBufferRHI = CreateBufferAndFillWithData(TexCoords.GetData(), TexCoords.Num() * sizeof(FVector2D));
    }
    TexCoordsBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(FShaderResourceViewInitializer(TexCoordsBuffer.VertexBufferRHI, PF_G32R32F));
    BeginInitResource(&TexCoordsBuffer);
}

void FNexusNodeRenderData::CalculateTangents(TArray<FPackedNormal>& OutTangents, Signature& TheSig,  NodeData& Data, Node& Node)
{
    TArray<FVector> T;
    TArray<FVector> TSums;
    TArray<int> Counts;
    
    Counts.SetNum(Node.nvert);
    T.SetNum(Node.nface);
    TSums.SetNum(Node.nvert);
    
    vcg::Point3f* Vertices = Data.coords();
    vcg::Point3s* Normals = Data.normals(TheSig, Node.nvert);
    vcg::Point2f* TexCoords = Data.texCoords(TheSig, Node.nvert);
    uint16* Indices = Data.faces(TheSig, Node.nvert);

    auto Point3SToVector = [](const vcg::Point3s Point) -> FVector
    {
        return FVector(Point.X(), Point.Z(), Point.Y());
    };
    
    auto Point3FToVector = [](const vcg::Point3f Point) -> FVector
    {
        return FVector(Point.X(), Point.Z(), Point.Y());
    };
    
    auto Point2FToVector = [](const vcg::Point2f Point) -> FVector2D
    {
        return FVector2D(Point.X(), Point.Y());
    };
    
    // Step 1: Find per-face T Vectors
    for (uint32 Index = 0; Index < Node.nface; Index += 3)
    {
        const uint32 Index1 = Indices[Index + 0];
        const uint32 Index2 = Indices[Index + 1];
        const uint32 Index3 = Indices[Index + 2];

        Counts[Index1] ++;
        Counts[Index2] ++;
        Counts[Index3] ++;
        
        FVector Vertex1 =  Point3FToVector(Vertices[Index1]);
        FVector Vertex2 =  Point3FToVector(Vertices[Index2]);
        FVector Vertex3 =  Point3FToVector(Vertices[Index3]);
        
        FVector2D TexCoord1 =  Point2FToVector(TexCoords[Index1]);
        FVector2D TexCoord2 =  Point2FToVector(TexCoords[Index2]);
        FVector2D TexCoord3 =  Point2FToVector(TexCoords[Index3]);

        FVector Edge21 = Vertex2 - Vertex1;
        FVector Edge31 = Vertex3 - Vertex1;

        const FVector2D TexEdge21 = TexCoord2 - TexCoord1;
        const FVector2D TexEdge31 = TexCoord3 - TexCoord1;
        
        T[Index] = (TexEdge21.Y != 0.0f ? Edge21 / TexEdge21.Y : Edge31 / TexEdge31.Y).GetSafeNormal();
        
        TSums[Index1] += T[Index];
        TSums[Index2] += T[Index];
        TSums[Index3] += T[Index];

        Counts[Index1] ++;
        Counts[Index2] ++;
        Counts[Index3] ++;
    }
    
    for (uint32 Index = 0; Index < Node.nvert; Index ++)
    {
        FVector CalculatedTangentPerVertex = TSums[Index] / Counts[Index];
        OutTangents[Index * 2 + 0] = FPackedNormal(CalculatedTangentPerVertex.GetSafeNormal());
        OutTangents[Index * 2 + 1] = FPackedNormal(Point3SToVector(Normals[Index]).GetSafeNormal());
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
        FVertexBuffer* ColorBuffer;
        FRHIShaderResourceView* ColorBufferSRV;
        // All the other members
    } Params;
    
    Params.VertexFactory = &NodeVertexFactory;
    Params.PositionBuffer = &PositionBuffer;
    Params.TexCoordsBuffer = &TexCoordsBuffer;
    Params.TangentBuffer = &TangentBuffer;
    Params.ColorBuffer = bHasColors ? static_cast<FVertexBuffer*>(&ColorBuffer) : &GNullColorVertexBuffer;
    Params.ColorBufferSRV = bHasColors ? ColorBuffer.ShaderResourceViewRHI : GNullColorVertexBuffer.VertexBufferSRV;
    
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
        
            Data.ColorIndexMask = 0;

            Data.ColorComponentsSRV = Params.ColorBufferSRV;
            Data.ColorComponent = FVertexStreamComponent(
                Params.ColorBuffer,
                0, // Struct offset to color
                sizeof(FColor), //asserted elsewhere
                VET_Color,
                (EVertexStreamUsage::ManualFetch)
            );
        
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


bool FUnrealNexusProxy::IsContainedInViewFrustum(const FVector& SphereCenter, const float SphereRadius) const
{
    return LastCameraInfo.ViewFrustum.IntersectSphere(SphereCenter, SphereRadius);
}

FUnrealNexusProxy::FUnrealNexusProxy(UUnrealNexusComponent* TheComponent, const int InMaxPending)
    : FPrimitiveSceneProxy(static_cast<UPrimitiveComponent*>(TheComponent)),
        ComponentData(TheComponent->NexusLoadedAsset),
        Component(TheComponent),
        MaxPending(InMaxPending)
{
    SetWireframeColor(FLinearColor::Green);

    MaterialProxy = Component->ModelMaterial == nullptr ?
                    UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy() : Component->ModelMaterial->GetRenderProxy();
}

void FUnrealNexusProxy::AddCandidate(UINT32 CandidateID, float FirstNodeError)
{
    CandidateNodes.Add(FCandidateNode {CandidateID, FirstNodeError});
}

void FUnrealNexusProxy::UnloadNode(UINT32 WorstID)
{
    Component->UnloadNode(WorstID);
    DropGPUData(WorstID);
}

void FUnrealNexusProxy::FreeCache(Node* BestNode, const UINT64 BestNodeID)
{
    while (Component->CurrentCacheSize > static_cast<UINT64>(Component->DrawBudget))
    {
        Node* Worst = nullptr;
        UINT32 WorstID = 0;
        TArray<UINT32> LoadedNodes;
        LoadedMeshData.GenerateKeyArray(LoadedNodes);
        for (UINT32 ID : LoadedNodes)
        {
            if(!LoadedMeshData.Contains(ID)) return;
            Node* SelectedNode = &ComponentData->Nodes[ID].NexusNode;
            const float SelectedNodeError = Component->GetErrorForNode(ID);
            if (!Worst || SelectedNodeError < Component->GetErrorForNode(WorstID))
            {
                Worst = SelectedNode;
                WorstID = ID;
            }
        }
        if (!Worst || Component->GetErrorForNode(WorstID) >= Component->GetErrorForNode(BestNodeID) * 0.9f)
        {
            return;
        }
        UnloadNode(WorstID);
    }

}

void FUnrealNexusProxy::InitializeThreads()
{
    
    if (Component->IsNodeLoaded(0))
    {
        LoadGPUData(0);
    }
    JobExecutor = new FNexusJobExecutorThread(nullptr);
    JobThread = FRunnableThread::Create(JobExecutor, TEXT("Nexus Node Loader"));
}

void FUnrealNexusProxy::Flush()
{
#if 0
    if (LoadedMeshData.Num() == 0) 
        return;
    TArray<UINT32> LoadedNodes;
    LoadedMeshData.GetKeys(LoadedNodes);
    for (UINT32 Node : LoadedNodes)
    {
        DropGPUData(Node);
    }
#endif
}

TOptional<TTuple<UINT32, Node*>> FUnrealNexusProxy::FindBestNode()
{
    Node* BestNode = nullptr;
    UINT32 BestNodeID = 0;
    for (FCandidateNode& CandidateNode : CandidateNodes)
    {
        auto& UNode = ComponentData->Nodes[CandidateNode.ID];
        Node* Candidate = &UNode.NexusNode;
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
            Component->CurrentError *= 1.05;
        } else if (Ratio < 0.9f)
        {
            Component->CurrentError *= 0.95;
        }
        Component->CurrentError = FMath::Max(Component->TargetError, FMath::Min(Component->MaxError, Component->CurrentError));
    }
    CandidateNodes.Empty();
    Component->CurrentError = FMath::Max(Component->TargetError, FMath::Min(Component->MaxError, Component->CurrentError));
}

void FUnrealNexusProxy::Update(const FCameraInfo InLastCameraInfo, const FTraversalData InLastTraversalData)
{
    DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Nexus Proxy Update"), CYCLEID_NexusRenderer, STATGROUP_NexusRenderer);
    LastCameraInfo = InLastCameraInfo;
    LastTraversalData = InLastTraversalData;
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
    FreeCache(BestNode, BestNodeID);
    
    RemoveCandidateWithId(BestNodeID);
    Component->SetNodeStatus(BestNodeID, ENodeStatus::Pending);
    ComponentData->LoadNodeAsync(BestNodeID, FStreamableDelegate::CreateLambda([&, BestNodeID]()
    {
        // Two passes: 1) Load the Unreal node data
        if (LoadedMeshData.Contains(BestNodeID)) return;
        const auto UCurrentNodeData = ComponentData->GetNode(BestNodeID);
        auto* UCurrentNode = &ComponentData->Nodes[BestNodeID];

        // 2) Decode it in a separate thread
        JobExecutor->AddNewJobs( {FNexusJob { BestNodeID, UCurrentNodeData, UCurrentNode, ComponentData}});
    }));

    
    FNexusJob DoneJob;
    TQueue<FNexusJob>& FinishedJobs = JobExecutor->GetJobsDone();
    while (FinishedJobs.Dequeue(DoneJob))
    {
        Component->SetNodeStatus(DoneJob.NodeIndex, ENodeStatus::Loaded);
        LoadGPUData(DoneJob.NodeIndex);
    }
}

FUnrealNexusProxy::~FUnrealNexusProxy()
{
    // Kill the Job thread
    if (JobExecutor)
    {
        JobExecutor->Stop();
        JobThread->Kill();
        delete JobExecutor;
        delete JobThread;
    }
    
}

void FUnrealNexusProxy::LoadGPUData(const uint32 N)
{
    if (LoadedMeshData.Contains(N)) return;
    auto& TheNode = ComponentData->Nodes[N].NexusNode;
    auto* TheNodeData = ComponentData->GetNode(N);
    NodeData& TheData = TheNodeData->NexusNodeData;
    check(TheNodeData->NexusNodeData.memory);
    Component->CurrentCacheSize += Component->GetNodeSize(N);
    
    ENQUEUE_RENDER_COMMAND(NexusLoadGPUData)([&, N](FRHICommandListImmediate& Commands)
    {
        FNexusNodeRenderData* Data = new FNexusNodeRenderData(this, TheData, TheNode);
        this->PendingCount ++;
        UE_LOG(NexusInfo, Log, TEXT("Increase cache %d by %d"), Component->CurrentCacheSize, Component->GetNodeSize(N));
        Data->NumPrimitives = TheNode.nface;
        LoadedMeshData.Add(N, Data);
        this->PendingCount --;
    });
}

void FUnrealNexusProxy::DropGPUData(uint32 N)
{
    if (!LoadedMeshData.Contains(N)) return;
    Component->CurrentCacheSize -= Component->GetNodeSize(N);
    if (LastTraversalData.SelectedNodes.Contains(N))
        LastTraversalData.SelectedNodes.Remove(N);
    ENQUEUE_RENDER_COMMAND(NexusLoadGPUData)([&, N](FRHICommandListImmediate& Commands)
    {
        LoadedMeshData.Remove(N);
    });
    UE_LOG(NexusInfo, Log, TEXT("Decrease cache %d by %d"), Component->CurrentCacheSize, Component->GetNodeSize(N));
}


bool FUnrealNexusProxy::CanBeOccluded() const
{
    return true;
}

void FUnrealNexusProxy::DrawEdgeNodes(const int ViewIndex, const FSceneView* View, FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags) const
{
    
    DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Nexus Edge Selection"), CYCLEID_NexusNodeSelection, STATGROUP_NexusRenderer);
    int RenderedCount = 0;
    const TSet<UINT32> SelectedNodes = LastTraversalData.SelectedNodes;
    for (UINT32 Id : SelectedNodes)
    {
        if (!LoadedMeshData.Contains(Id))
            continue; // This node was dropped
        FNexusNodeRenderData* Data = LoadedMeshData[Id];

        // Detecting if this node is on the edge
        auto& CurrentNode = ComponentData->Nodes[Id];
        auto& NextNode = ComponentData->Nodes[Id + 1];
        
        bool IsVisible = false;
        const UINT32 NextNodeFirstPatch = NextNode.NexusNode.first_patch;
        for (auto& Patch : CurrentNode.NodePatches)
        {
            const int ChildNode = Patch.node;
            if (!SelectedNodes.Contains(ChildNode))
            {
                IsVisible = true;
                break;
            }
        }
        if(Component->bIsFrustumCullingEnabled && !IsVisible) continue;

        if (!IsContainedInViewFrustum( VcgPoint3FToVector(CurrentNode.NexusNode.sphere.Center()),
            CurrentNode.NexusNode.tight_radius))
        {
            continue;
        }

        int Offset = 0;
        int EndIndex = 0;
        for (UINT32 PatchId = CurrentNode.NexusNode.first_patch; PatchId < NextNodeFirstPatch; PatchId ++)
        {
            const Patch& CurrentNodePatch = CurrentNode.NodePatches[PatchId - CurrentNode.NexusNode.first_patch];
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
    if(!ComponentData) return;
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
    {
        const auto& EngineShowFlags = ViewFamily.EngineShowFlags;
        if (!(VisibilityMap & (1 << ViewIndex))) continue;
        DrawEdgeNodes(ViewIndex, Views[ViewIndex], Collector, EngineShowFlags);
    }
}

void FUnrealNexusProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
    if (LoadedMeshData.Num() == 0) return;
    FNexusNodeRenderData* Data = LoadedMeshData[0];
    FMeshBatch Mesh;
    Mesh.bWireframe = false;
    Mesh.VertexFactory = &Data->NodeVertexFactory;
    Mesh.Type = PT_TriangleList;
    Mesh.DepthPriorityGroup = SDPG_World;
    Mesh.bUseAsOccluder = true;
    Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
    if (bIsWireframe)
    {
        const auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
            GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
            FLinearColor(0, 0.5f, 1.f)
            );

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
    Element.FirstIndex = 0;
    Element.NumPrimitives = Data->NumPrimitives;
    PDI->DrawMesh(Mesh, FLT_MAX);
}

FPrimitiveViewRelevance FUnrealNexusProxy::GetViewRelevance(const FSceneView* View) const
{
    // Partially copied from StaticMeshRenderer.cpp
    FPrimitiveViewRelevance Result;
    Result.bOpaque = true;
    Result.bDrawRelevance = IsShown(View);
    Result.bShadowRelevance = true;
    Result.bRenderInMainPass = true;
    Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;
    Result.bDynamicRelevance = true;
    // Result.bStaticRelevance = !bIsPlaying;
    return Result;
}

TArray<UINT32> FUnrealNexusProxy::GetLoadedNodes() const
{
    TArray<UINT32> LoadedNodes;
    LoadedMeshData.GetKeys(LoadedNodes);
    return LoadedNodes;
}
