#include "UnrealNexusComponent.h"

#include "UnrealNexusData.h"
#include "UnrealNexusProxy.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"

constexpr float GScale = 10.0f;

struct FNodeComparator
{
    bool operator()(const FTraversalElement& A, const FTraversalElement& B) const
    {
        return A.TheNode->error > B.TheNode->error;
    }
};

UUnrealNexusComponent::UUnrealNexusComponent(const FObjectInitializer& Initializer)
    : UPrimitiveComponent(Initializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.SetTickFunctionEnable(true);
}

UUnrealNexusComponent::~UUnrealNexusComponent()
{
    TArray<UINT32> LoadedIDs;
    NodeStatuses.GetKeys(LoadedIDs);
    for (const int N : LoadedIDs)
    {
        ComponentData->DropFromRam(N);
    }
    
    if (Proxy)
    {
        Proxy->Flush();
    }

    delete ComponentData;
}

bool UUnrealNexusComponent::Open(const FString& Source)
{
    // Assuming files on disk for now
    auto GameContentPath = FPaths::ProjectContentDir();
    const auto FilePath = FPaths::Combine(GameContentPath, Source);
    if (!FPaths::FileExists(FilePath)) return false;
    char* CStr = TCHAR_TO_ANSI(*FilePath);
    ComponentData->file->setFileName(CStr);
    bIsLoaded = ComponentData->Init();
    if(!bIsLoaded)
    {
        UE_LOG(NexusInfo, Log, TEXT("Could not open the file"));
        return false;
    }
    CalculatedErrors.SetNum(ComponentData->header.n_nodes);
    for (UINT32 RootId = 0; RootId < ComponentData->nroots; RootId++)
    {
        Node& RootNode = ComponentData->nodes[RootId];
        RootNode.error = RootNodesInitialError * RootNode.tight_radius;
    }
    return true;
}

void UUnrealNexusComponent::SetErrorForNode(UINT32 NodeID, float Error)
{
    CalculatedErrors[NodeID] = Error;
}

float UUnrealNexusComponent::GetErrorForNode(const UINT32 NodeID) const
{
    return CalculatedErrors[NodeID];
}

float UUnrealNexusComponent::CalculateErrorForNode(const UINT32 NodeID, const bool UseTight) const
{
    Node& SelectedNode = ComponentData->nodes[NodeID];
    vcg::Sphere3f& NodeBoundingSphere = SelectedNode.sphere;
    const FVector Viewpoint = CameraInfo.ViewpointLocation;
    
    vcg::Point3f BoundingSphereCenter = NodeBoundingSphere.Center();
    const FVector ViewpointToBoundingSphere { Viewpoint.X - BoundingSphereCenter.X(),
                                                    Viewpoint.Y - BoundingSphereCenter.Y(),
                                                    Viewpoint.Z - BoundingSphereCenter.Z()};
    const float ViewpointDistanceToBoundingSphere = FMath::Max(ViewpointToBoundingSphere.Size(), 0.1f);
    float CalculatedError = SelectedNode.error / (CameraInfo.CurrentResolution * ViewpointDistanceToBoundingSphere);
    
    const float SphereRadius = UseTight ? SelectedNode.tight_radius : NodeBoundingSphere.Radius();
    const float BoundingSphereDistanceFromViewFrustum = CalculateDistanceFromSphereToViewFrustum(NodeBoundingSphere, SelectedNode.tight_radius);
    if (BoundingSphereDistanceFromViewFrustum < -SphereRadius)
    {
        CalculatedError /= Outer_Node_Factor + 1.0f;
    } else if (BoundingSphereDistanceFromViewFrustum < 0)
    {
        CalculatedError /= 1.0f - (BoundingSphereDistanceFromViewFrustum / SphereRadius) * Outer_Node_Factor;
    }
    return CalculatedError * GScale;
}

float UUnrealNexusComponent::CalculateDistanceFromSphereToViewFrustum(const vcg::Sphere3f& Sphere, const float SphereTightRadius) const 
{
    float MinDistance = 1e20;
    const FConvexVolume& ViewFrustum = CameraInfo.ViewFrustum;
    const FConvexVolume::FPlaneArray& ViewPlanes = ViewFrustum.Planes;
    vcg::Point3f SphereCenter = Sphere.Center();
    for (UINT32 i = 0; i < 5; i ++)
    {
        const FPlane CurrentPlane = ViewPlanes[i];
        const float Distance =  CurrentPlane.X * SphereCenter.X() +
                                CurrentPlane.Y * SphereCenter.Y() +
                                CurrentPlane.Z * SphereCenter.Z() +
                                SphereTightRadius;
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
        }
    }
    return MinDistance;
}

FPrimitiveSceneProxy* UUnrealNexusComponent::CreateSceneProxy()
{
    Proxy = new FUnrealNexusProxy(this);
    return static_cast<FPrimitiveSceneProxy*>(Proxy);
}

void UUnrealNexusComponent::Update(float DeltaSeconds)
{
    if (!bIsLoaded || !Proxy) return;
    UpdateCameraView();
    Proxy->BeginFrame(DeltaSeconds);
    FTraversalData TraversalData = DoTraversal();
    Proxy->Update(TraversalData);
}

void UUnrealNexusComponent::UpdateCameraView()
{
    // TODO: Remove hardcoded player index
    // TODO: Check for Viewport, ViewportClient and Player
    ULocalPlayer* Player = GetWorld()->GetFirstLocalPlayerFromController();
    CameraInfo.Viewport = Player->ViewportClient->Viewport;
    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        CameraInfo.Viewport,
        GetWorld()->Scene,
        Player->ViewportClient->EngineShowFlags)
        .SetRealtimeUpdate(true));
    FSceneView* SceneView = Player->CalcSceneView(&ViewFamily, CameraInfo.ViewpointLocation, CameraInfo.ViewpointRotation, CameraInfo.Viewport);

    FViewMatrices& ViewMatrices = SceneView->ViewMatrices;
    // TODO: Check(SceneView)
    CameraInfo.Model = GetComponentTransform().ToMatrixWithScale();
    CameraInfo.View = ViewMatrices.GetViewMatrix();
    CameraInfo.Projection = ViewMatrices.GetProjectionMatrix();
    
    CameraInfo.ModelView = CameraInfo.View * CameraInfo.Model;
    
    // Composition of affine transformations so the det is always != 0, we can use InverseFast
    CameraInfo.InvertedModelView = CameraInfo.ModelView.InverseFast(); 
    
    CameraInfo.ModelViewProjection = CameraInfo.Projection * CameraInfo.View * CameraInfo.Model;
    CameraInfo.InvertedModelViewProjection = CameraInfo.ModelViewProjection.InverseFast();

    CameraInfo.ViewFrustum = SceneView->ViewFrustum;

    FVector& ViewpointLocation = CameraInfo.ViewpointLocation;

    // WTF?
    const auto& InvertedMvpMatrix = CameraInfo.InvertedModelViewProjection.M;
    const float R3 = InvertedMvpMatrix[0][3] + InvertedMvpMatrix[3][3];
    const float R0 = (InvertedMvpMatrix[0][0] + InvertedMvpMatrix[3][0]) / R3;
    const float R1 = (InvertedMvpMatrix[0][1] + InvertedMvpMatrix[3][1]) / R3;
    const float R2 = (InvertedMvpMatrix[0][2] + InvertedMvpMatrix[3][2]) / R3;

    // It's a point of what?
    const float L3 = -InvertedMvpMatrix[0][3] + InvertedMvpMatrix[3][3];
    const float L0 = (-InvertedMvpMatrix[0][0] + InvertedMvpMatrix[3][0]) / L3 - R0;
    const float L1 = (-InvertedMvpMatrix[0][1] + InvertedMvpMatrix[3][1]) / L3 - R1;
    const float L2 = (-InvertedMvpMatrix[0][2] + InvertedMvpMatrix[3][2]) / L3 - R2;

    const float Side = FMath::Sqrt(L0 * L0 + L1 * L1 + L2 * L2);
    const FVector SceneCenter {
                        InvertedMvpMatrix[3][0] / InvertedMvpMatrix[3][3] - ViewpointLocation.X,
                        InvertedMvpMatrix[3][1] / InvertedMvpMatrix[3][3] - ViewpointLocation.Y,
                        InvertedMvpMatrix[3][2] / InvertedMvpMatrix[3][3] - ViewpointLocation.Z,
    };
    const float DistanceToCenter = SceneCenter.Size();
    const int ViewportWidth = CameraInfo.Viewport->GetSizeXY().X;
    const float ResolutionThisFrame = (2 * Side * DistanceToCenter) / ViewportWidth;
    CameraInfo.IsUsingSameResolutionAsBefore = CameraInfo.CurrentResolution == ResolutionThisFrame;
    CameraInfo.CurrentResolution = ResolutionThisFrame;
}

bool UUnrealNexusComponent::IsNodeLoaded(const UINT32 NodeID) const
{
    return NodeStatuses.Contains(NodeID) && NodeStatuses[NodeID] == ENodeStatus::Loaded;
}

void UUnrealNexusComponent::SetNodeStatus(const UINT32 NodeID, const ENodeStatus Status)
{
    if (Status == ENodeStatus::Dropped)
    {
       NodeStatuses.Remove(NodeID); 
    } else
    {
        NodeStatuses.Add(TTuple<UINT32, ENodeStatus> {NodeID, Status});
    }
}

FTraversalData UUnrealNexusComponent::DoTraversal()
{
    checkf(Proxy, TEXT("Tried to traverse the tree without a proxy (cache)"));
    FTraversalData TraversalData;
    TArray<FTraversalElement>& VisitingNodes = TraversalData.TraversalQueue;
    TSet<UINT32>& BlockedNodes = TraversalData.BlockedNodes, &SelectedNodes = TraversalData.SelectedNodes;
    TArray<float>& InstanceErrors = TraversalData.InstanceErrors;
    InstanceErrors.SetNum(ComponentData->header.n_nodes);
    
    // Load roots
    for (UINT32 i = 0; i < ComponentData->nroots; i ++)
    {
        AddNodeToTraversal(TraversalData, i);
    }

    const float CurrentProxyError = Proxy->GetCurrentError();
    int RequestedCount = 0;
    CurrentlyBlockedNodes = 0;
    while(VisitingNodes.Num() > 0 && CurrentlyBlockedNodes < MaxBlockedNodes)
    {
        
        FTraversalElement CurrentElement = VisitingNodes.Pop();
        const float NodeError = CurrentElement.CalculatedError;
        const int Id = CurrentElement.Id;
        if(!IsNodeLoaded(Id) && CurrentlyBlockedNodes < MaxBlockedNodes)
        {
            Proxy->AddCandidate(Id, NodeError);
            RequestedCount ++;
        }

        const bool IsBlocked = BlockedNodes.Contains(Id) || !CanNodeBeExpanded(CurrentElement.TheNode, CurrentElement.Id, NodeError, CurrentProxyError);
        if (IsBlocked)
        {
            CurrentlyBlockedNodes ++;
        }
        else
        {
            SelectedNodes.Add(Id);
        }
        AddNodeChildren(CurrentElement, TraversalData, IsBlocked);
    }

    UpdateRemainingErrors(InstanceErrors);
    
    return TraversalData;
}

bool UUnrealNexusComponent::CanNodeBeExpanded(Node* Node, const int NodeID, const float NodeError, const float CurrentCalculatedError) const
{
    return NodeError > TargetError &&
        CurrentDrawBudget <= DrawBudget &&
        IsNodeLoaded(NodeID);
}


void UUnrealNexusComponent::AddNodeChildren(const FTraversalElement& CurrentElement, FTraversalData& TraversalData, const bool ShouldMarkBlocked)
{
    Node* CurrentNode = CurrentElement.TheNode; 
    Node* NextNode = &ComponentData->nodes[CurrentElement.Id + 1];
    for (UINT32 PatchIndex = CurrentNode->first_patch;
        PatchIndex < NextNode->first_patch; PatchIndex ++)
    {
        Patch& CurrentPatch = ComponentData->patches[PatchIndex];
        const int PatchNodeId = CurrentPatch.node;
        if (PatchNodeId == ComponentData->header.n_nodes - 1)
        {
            // This node is the sink
            return;
        }
        if (ShouldMarkBlocked)
        {
            TraversalData.BlockedNodes.Add(PatchNodeId);
        }

        if (!TraversalData.VisitedNodes.Contains(PatchNodeId))
        {
            AddNodeToTraversal(TraversalData, PatchNodeId);
        }
    }
}

void UUnrealNexusComponent::AddNodeToTraversal(FTraversalData& TraversalData, const UINT32 NewNodeId)
{
    TraversalData.VisitedNodes.Add(NewNodeId);
    Node* NewNode = &ComponentData->nodes[NewNodeId];

    const float NodeError = CalculateErrorForNode(NewNodeId, false);
    TraversalData.InstanceErrors[NewNodeId] = NodeError;
    SetErrorForNode(NewNodeId, FMath::Max(NodeError, GetErrorForNode(NewNodeId)));
    
    TraversalData.TraversalQueue.HeapPush({NewNode, NewNodeId, NodeError}, FNodeComparator());
}


void UUnrealNexusComponent::UpdateRemainingErrors(TArray<float>& InstanceErrors)
{
    TArray<UINT32> LoadedNodes = Proxy->GetLoadedNodes();
    for (auto& NodeID : LoadedNodes)
    {
        // const Node& TheNode = ComponentData->nodes[NodeID];
        const float NodeError = CalculateErrorForNode(NodeID, false);
        if (InstanceErrors[NodeID] == 0.0f)
        {
            InstanceErrors[NodeID] = NodeError;
            SetErrorForNode(NodeID, FMath::Max( GetErrorForNode(NodeID),  NodeError));
        }
    }
}

void UUnrealNexusComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_STRING_CHECKED(UUnrealNexusComponent, NexusFile))
    {
        Open(NexusFile);
    }
}

void UUnrealNexusComponent::OnComponentCreated()
{
    if (NexusFile.IsEmpty()) return;
    Open(NexusFile);
}

void UUnrealNexusComponent::BeginPlay()
{
    Super::BeginPlay();
    if (NexusFile.IsEmpty()) return;
    Open(NexusFile);
}

void UUnrealNexusComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if(!bIsLoaded) return;
    Update(DeltaTime);

}
