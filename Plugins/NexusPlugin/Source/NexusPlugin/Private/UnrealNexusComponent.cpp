#include "UnrealNexusComponent.h"
#include "NexusUtils.h"

#include "UnrealNexusData.h"
#include "UnrealNexusProxy.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "DrawDebugHelpers.h"

using namespace nx;

// One unit in Unreal is 100cms
constexpr float GUnrealScaleConversion = 1.0f;

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
    bWantsInitializeComponent = true;
}

UUnrealNexusComponent::~UUnrealNexusComponent()
{
    TArray<UINT32> LoadedIDs;
    NodeStatuses.GetKeys(LoadedIDs);
    for (const int N : LoadedIDs)
    {
        // ComponentData->DropFromRam(N);
    }
    
    if (Proxy && Proxy->IsReady())
    {
        Proxy->Flush();
    }

    delete ComponentData;
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
    Node& SelectedNode = ComponentData->Nodes[NodeID];
    vcg::Sphere3f& NodeBoundingSphere = SelectedNode.sphere;
    const FVector Viewpoint = CameraInfo.ViewpointLocation;

    const float SphereRadius = UseTight ? SelectedNode.tight_radius : NodeBoundingSphere.Radius();
    const FVector BoundingSphereCenter = VcgPoint3FToVector(NodeBoundingSphere.Center());
    const FVector ViewpointToBoundingSphere = FVector( Viewpoint.X - BoundingSphereCenter.X,
                                                    Viewpoint.Y - BoundingSphereCenter.Y,
                                                    Viewpoint.Z - BoundingSphereCenter.Z);
    const float ViewpointDistanceToBoundingSphere = FMath::Max(ViewpointToBoundingSphere.Size() - SphereRadius, 0.1f);
    float CalculatedError = SelectedNode.error / (CameraInfo.CurrentResolution * ViewpointDistanceToBoundingSphere);
    
    const float BoundingSphereDistanceFromViewFrustum = CalculateDistanceFromSphereToViewFrustum(NodeBoundingSphere, SelectedNode.tight_radius);

    if (BoundingSphereDistanceFromViewFrustum < -SphereRadius)
    {
        CalculatedError /= Outer_Node_Factor + 1.0f;
    } else if (BoundingSphereDistanceFromViewFrustum < 0)
    {
        CalculatedError /= 1.0f - (BoundingSphereDistanceFromViewFrustum / SphereRadius) * Outer_Node_Factor;
    }
    return CalculatedError * GUnrealScaleConversion;
}

float UUnrealNexusComponent::CalculateDistanceFromSphereToViewFrustum(const vcg::Sphere3f& Sphere, const float SphereTightRadius) const 
{
    float MinDistance = 1e20;
    const FConvexVolume& ViewFrustum = CameraInfo.ViewFrustum;
    const FConvexVolume::FPlaneArray& ViewPlanes = ViewFrustum.Planes;
    const FVector SphereCenter = VcgPoint3FToVector(Sphere.Center());
    for (UINT32 i = 0; i < 5; i ++)
    {
        const FPlane CurrentPlane = ViewPlanes[i];
        const float DistanceFromPlane = FVector::Distance(SphereCenter, CurrentPlane);
        if (DistanceFromPlane < MinDistance)
        {
            MinDistance = DistanceFromPlane;
        }
    }
    return MinDistance;
}

void UUnrealNexusComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
    if (ModelMaterial != nullptr)
    {
        OutMaterials.Add(ModelMaterial);
    }
}

FPrimitiveSceneProxy* UUnrealNexusComponent::CreateSceneProxy()
{
    Proxy = new FUnrealNexusProxy(this);
    return static_cast<FPrimitiveSceneProxy*>(Proxy);
}

void UUnrealNexusComponent::Update(float DeltaSeconds)
{
    Bounds = FBoxSphereBounds(FSphere(GetComponentLocation(), 1000.0f));
    UpdateBounds();
    if (!bIsLoaded || !Proxy) return;
    UpdateCameraView();
    Proxy->BeginFrame(DeltaSeconds);
    const FTraversalData TraversalData = DoTraversal();
    Proxy->Update(TraversalData);
}

void UUnrealNexusComponent::UpdateCameraView()
{
    // TODO: Remove hardcoded player index
    // TODO: Check for Viewport, ViewportClient and Player
    ULocalPlayer* Player = GetWorld()->GetFirstLocalPlayerFromController();
    APlayerController* FirstController = GetWorld()->GetFirstPlayerController();

    
    const auto Viewport = Player->ViewportClient->Viewport;
    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        Viewport,
        GetWorld()->Scene,
        Player->ViewportClient->EngineShowFlags)
        .SetRealtimeUpdate(true));
    FSceneView* SceneView = Player->CalcSceneView(&ViewFamily, CameraInfo.ViewpointLocation, CameraInfo.ViewpointRotation, Viewport);

    CameraInfo.ViewportSize = Viewport->GetSizeXY();
    const auto WorldToModelMatrix = GetComponentTransform().ToInverseMatrixWithScale();
    // TODO: Check(SceneView)

    CameraInfo.ViewFrustum = SceneView->ViewFrustum;
    CameraInfo.ViewpointLocation = WorldToModelMatrix.TransformPosition(SceneView->ViewLocation);

    FVector ScreenMiddle, ScreenLeft, ScreenRight;
    FVector ScreenMiddleDir, ScreenLeftDir, ScreenRightDir;
    FirstController->DeprojectScreenPositionToWorld(CameraInfo.ViewportSize.X / 2, CameraInfo.ViewportSize.Y / 2, ScreenMiddle, ScreenMiddleDir);
    FirstController->DeprojectScreenPositionToWorld(0, CameraInfo.ViewportSize.Y / 2, ScreenLeft, ScreenLeftDir);
    FirstController->DeprojectScreenPositionToWorld(CameraInfo.ViewportSize.X, CameraInfo.ViewportSize.Y / 2, ScreenRight, ScreenRightDir);

    ScreenLeft = WorldToModelMatrix.TransformPosition(ScreenLeft);
    ScreenMiddle = WorldToModelMatrix.TransformPosition(ScreenMiddle);
    ScreenRight = WorldToModelMatrix.TransformPosition(ScreenRight);

    
    // Transforming everything into model space
    for (UINT32 i = 0; i < 5; i ++)
    {
        FPlane& Current = CameraInfo.ViewFrustum.Planes[i];
        Current = WorldToModelMatrix.TransformPosition(Current.Flip());
    }
    
    const float SideLengthWorldSpace = (ScreenRight - ScreenLeft).Size();
   
    const float DistanceToCenter = (CameraInfo.ViewpointLocation - ScreenMiddle).Size();
    const int ViewportWidth = CameraInfo.ViewportSize.X;
    const float ResolutionThisFrame = (2 * SideLengthWorldSpace / DistanceToCenter) / ViewportWidth;

    
    CameraInfo.IsUsingSameResolutionAsBefore = CameraInfo.CurrentResolution == ResolutionThisFrame;
    CameraInfo.CurrentResolution = ResolutionThisFrame;

    if (bShowDebugStuff)
    {
        DrawDebugBox(GetWorld(), CameraInfo.ViewpointLocation, FVector(10.0f), FQuat::Identity, FColor::Purple);
        DrawDebugPoint(GetWorld(), ScreenLeft, 20.0f, FColor::Green);
        DrawDebugPoint(GetWorld(), ScreenMiddle, 20.0f, FColor::White);
        DrawDebugPoint(GetWorld(), ScreenRight, 20.0f, FColor::Red);
    }
}

FVector UUnrealNexusComponent::VcgPoint3FToVector(const vcg::Point3f& Point3)
{
    return FVector(Point3.X(), Point3.Z(), Point3.Y());
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
    InstanceErrors.SetNum(ComponentData->Header.n_nodes);
    
    // Load roots
    for (UINT32 i = 0; i < ComponentData->RootsCount; i ++)
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
    Node* NextNode = &ComponentData->Nodes[CurrentElement.Id + 1];
    for (UINT32 PatchIndex = CurrentNode->first_patch;
        PatchIndex < NextNode->first_patch; PatchIndex ++)
    {
        Patch& CurrentPatch = ComponentData->Patches[PatchIndex];
        const int PatchNodeId = CurrentPatch.node;
        if (PatchNodeId == ComponentData->Header.n_nodes - 1)
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
    Node* NewNode = &ComponentData->Nodes[NewNodeId];

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

void UUnrealNexusComponent::BeginPlay()
{
    Super::BeginPlay();

    if (bIsLoaded && Proxy)
        Proxy->GetReady();
}

FBoxSphereBounds UUnrealNexusComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    const FBoxSphereBounds ComponentBounds = FBoxSphereBounds(FSphere(FVector::ZeroVector, ComponentBoundsRadius));
    return ComponentBounds.TransformBy(LocalToWorld);
}

void UUnrealNexusComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if(!bIsLoaded) return;
    Update(DeltaTime);

}
