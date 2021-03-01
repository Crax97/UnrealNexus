#include "UnrealNexusComponent.h"
#include "NexusUtils.h"

#include "UnrealNexusData.h"
#include "UnrealNexusNodeData.h"
#include "UnrealNexusProxy.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "DrawDebugHelpers.h"

constexpr bool GBCheckInvariants = true;

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
    CurrentCacheSize = 0;
}

UUnrealNexusComponent::~UUnrealNexusComponent()
{
    TArray<UINT32> LoadedIDs;
    NodeStatuses.GetKeys(LoadedIDs);
    for (const int N : LoadedIDs)
    {
        NexusLoadedAsset->UnloadNode(N);
    }
    
    if (Proxy && Proxy->IsReady())
    {
        Proxy->Flush();
    }
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
    Node* SelectedNode = &NexusLoadedAsset->Nodes[NodeID].NexusNode;
    vcg::Sphere3f& NodeBoundingSphere = SelectedNode->sphere;
    const FVector Viewpoint = CameraInfo.ViewpointLocation;

    const float SphereRadius = UseTight ? SelectedNode->tight_radius : NodeBoundingSphere.Radius();
    const FVector BoundingSphereCenter = VcgPoint3FToVector(NodeBoundingSphere.Center());
    const FVector ViewpointToBoundingSphere = Viewpoint - BoundingSphereCenter;
    const float ViewpointDistanceToBoundingSphere = FMath::Max(ViewpointToBoundingSphere.Size() - SphereRadius, 0.1f);
    float CalculatedError = SelectedNode->error / (CameraInfo.CurrentResolution * ViewpointDistanceToBoundingSphere);
    
    const float BoundingSphereDistanceFromViewFrustum =  CalculateDistanceFromSphereToViewFrustum(NodeBoundingSphere, SphereRadius);
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
        // ? const float DistanceFromPlane = FVector::Distance(SphereCenter, CurrentPlane);
        const float DStrano =   SphereCenter.X * CurrentPlane.X +
                                SphereCenter.Y * CurrentPlane.Y +
                                SphereCenter.Z * CurrentPlane.Z +
                                CurrentPlane.W + SphereTightRadius;
        if (DStrano < MinDistance)
        {
            MinDistance = DStrano;
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
    Bounds = FBoxSphereBounds(FSphere(GetComponentLocation(), NexusLoadedAsset->BoundingSphere().Radius()));
    UpdateBounds();
    if ( !Proxy) return;
    UpdateCameraView();
    Proxy->BeginFrame(DeltaSeconds);
    const FTraversalData TraversalData = DoTraversal();
    Proxy->Update(TraversalData, CameraInfo);
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
    CameraInfo.WorldToModelMatrix = GetComponentTransform().ToInverseMatrixWithScale();
    // TODO: Check(SceneView)

    CameraInfo.ViewFrustum = SceneView->ViewFrustum;
    CameraInfo.ViewpointLocation = CameraInfo.WorldToModelMatrix.TransformPosition(SceneView->ViewLocation);

    FVector ScreenMiddle, ScreenLeft, ScreenRight;
    FVector ScreenMiddleDir, ScreenLeftDir, ScreenRightDir;
    FirstController->DeprojectScreenPositionToWorld(CameraInfo.ViewportSize.X / 2, CameraInfo.ViewportSize.Y / 2, ScreenMiddle, ScreenMiddleDir);
    FirstController->DeprojectScreenPositionToWorld(0, CameraInfo.ViewportSize.Y / 2, ScreenLeft, ScreenLeftDir);
    FirstController->DeprojectScreenPositionToWorld(CameraInfo.ViewportSize.X, CameraInfo.ViewportSize.Y / 2, ScreenRight, ScreenRightDir);

    ScreenLeft = CameraInfo.WorldToModelMatrix.TransformPosition(ScreenLeft);
    ScreenMiddle = CameraInfo.WorldToModelMatrix.TransformPosition(ScreenMiddle);
    ScreenRight = CameraInfo.WorldToModelMatrix.TransformPosition(ScreenRight);

    const auto TransposedInversedWorldToModel = CameraInfo.WorldToModelMatrix.Inverse().GetTransposed();
    
    // Transforming everything into model space
    for (UINT32 i = 0; i < 5; i ++)
    {
        FPlane& Current = CameraInfo.ViewFrustum.Planes[i];
        auto PlaneToFVec4 = FVector4(-Current.X, -Current.Y, -Current.Z, -Current.W);
        Current = TransposedInversedWorldToModel.TransformFVector4(PlaneToFVec4);
    }

    // Draw frustum
    // After extract, check non-selected i -> non-selected childs(i)
    
    const float SideLengthWorldSpace = (ScreenRight - ScreenLeft).Size();
   
    const float DistanceToCenter = (CameraInfo.ViewpointLocation - ScreenMiddle).Size();
    const int ViewportWidth = CameraInfo.ViewportSize.X;
    const float ResolutionThisFrame = (2 * SideLengthWorldSpace / DistanceToCenter) / ViewportWidth;

    if (bShowDebugStuff)
    {
        DrawDebugBox(GetWorld(), CameraInfo.ViewpointLocation, FVector(10.0f), FQuat::Identity, FColor::Purple);
        DrawDebugPoint(GetWorld(), ScreenLeft, 20.0f, FColor::Green);
        DrawDebugPoint(GetWorld(), ScreenMiddle, 20.0f, FColor::White);
        DrawDebugPoint(GetWorld(), ScreenRight, 20.0f, FColor::Red);
        for (const auto& Plane : CameraInfo.ViewFrustum.Planes)
        {
            // DrawDebugPoint(GetWorld(), Plane, 10.0f, FColor::Emerald);
            DrawDebugSolidPlane(GetWorld(), Plane, CameraInfo.ViewpointLocation, 10.0f, FColor::Emerald);
        }
    }
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

void UUnrealNexusComponent::ClearErrors()
{
    for (int i = 0; i < CalculatedErrors.Num(); i ++)
    {
        CalculatedErrors[i] = 0.0f;
    }
}


FTraversalData UUnrealNexusComponent::DoTraversal()
{
    checkf(Proxy, TEXT("Tried to traverse the tree without a proxy (cache)"));
    FTraversalData TraversalData;
    TArray<FTraversalElement>& VisitingNodes = TraversalData.TraversalQueue;
    TSet<UINT32>& BlockedNodes = TraversalData.BlockedNodes, &SelectedNodes = TraversalData.SelectedNodes;
    TArray<float>& InstanceErrors = TraversalData.InstanceErrors;
    InstanceErrors.SetNum(NexusLoadedAsset->Header.n_nodes);

    ClearErrors();
    
    // Load roots
    for (int i = 0; i < NexusLoadedAsset->RootsCount; i ++)
    {
        AddNodeToTraversal(TraversalData, i);
    }

    const float CurrentProxyError = CurrentError;
    int RequestedCount = 0;
    CurrentlyBlockedNodes = 0;
    while(VisitingNodes.Num() > 0 && CurrentlyBlockedNodes < MaxBlockedNodes)
    {
        
        FTraversalElement CurrentElement;
        VisitingNodes.HeapPop(CurrentElement, FNodeComparator{ });

        for (auto& Patch : NexusLoadedAsset->Nodes[CurrentElement.Id].NodePatches)
        {
            const float PatchNodeError = GetErrorForNode(Patch.node);
            checkf(CurrentElement.CalculatedError > PatchNodeError, TEXT("Child error is greater"));
            checkf(!SelectedNodes.Contains(Patch.node), TEXT("Invariant broken: Patch node is selected"));
        }
        
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
            
            if (bShowDebugStuff)
            {
                auto& Sphere =  NexusLoadedAsset->Nodes[Id].NexusNode.sphere;
                auto SphereCenter = VcgPoint3FToVector(Sphere.Center());
                DrawDebugSphere(GetWorld(), SphereCenter, Sphere.Radius() / 10.0f, 4, FColor::Red);
            }
        }
        AddNodeChildren(CurrentElement, TraversalData, IsBlocked);
    }

    if constexpr (GBCheckInvariants)
    {
        TSet<UINT32> AllNodes;
        for (int i = 0; i < NexusLoadedAsset->Nodes.Num(); i ++)
        {
            AllNodes.Add(i);
        }
        TSet<UINT32> NotSelected = AllNodes.Difference(SelectedNodes);
        for (auto& NodeID : NotSelected)
        {
            for(auto& Patch : NexusLoadedAsset->Nodes[NodeID].NodePatches)
            {
                // checkf(NotSelected.Contains(Patch.node), TEXT("NotSelected -> forall child : childs(NotSelected) !IsSelected(child): Broken Invariant"));
                // checkf(NexusLoadedAsset->Nodes[NodeID].NexusNode.error > NexusLoadedAsset->Nodes[Patch.node].NexusNode.error, TEXT("Node.err < Patch.node.err"));
            }
        }
    }
    
    UpdateRemainingErrors(InstanceErrors);
    return TraversalData;
}

bool UUnrealNexusComponent::CanNodeBeExpanded(Node* Node, const int NodeID, const float NodeError, const float CurrentProxyError) const
{
    return NodeError > TargetError &&
        CurrentDrawBudget <= DrawBudget &&
        IsNodeLoaded(NodeID);
}


void UUnrealNexusComponent::AddNodeChildren(const FTraversalElement& CurrentElement, FTraversalData& TraversalData, const bool ShouldMarkBlocked)
{
    auto& CurrentNode = NexusLoadedAsset->Nodes[CurrentElement.Id];
    for (auto& CurrentPatch : CurrentNode.NodePatches)
    {
        const int PatchNodeId = CurrentPatch.node;
        if (PatchNodeId == NexusLoadedAsset->Header.n_nodes - 1)
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
    Node* NewNode = &NexusLoadedAsset->Nodes[NewNodeId].NexusNode;

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
        // const Node& TheNode = NexusLoadedAsset->nodes[NodeID];
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
    if (NexusLoadedAsset)
    {
        CalculatedErrors.SetNum(NexusLoadedAsset->Nodes.Num());
        NodeStatuses.Reserve(NexusLoadedAsset->Nodes.Num());   
    }
    
    if (Proxy)
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
    if(!NexusLoadedAsset) return;
    Update(DeltaTime);

}
