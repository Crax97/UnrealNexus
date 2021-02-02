﻿#include "UnrealNexusComponent.h"

#include "UnrealNexusData.h"
#include "UnrealNexusProxy.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"

// One unit in Unreal is 100cms
constexpr float GUnrealScaleConversion = 100.0f;

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
    
    if (Proxy && Proxy->IsReady())
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
    ComponentBoundsRadius = ComponentData->boundingSphere().Radius() * GUnrealScaleConversion;
    UpdateBodySetup();
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
    return CalculatedError * GUnrealScaleConversion;
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
    CameraInfo.ViewportSize = Player->ViewportClient->Viewport->GetSizeXY();
    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        Player->ViewportClient->Viewport,
        GetWorld()->Scene,
        Player->ViewportClient->EngineShowFlags)
        .SetRealtimeUpdate(true));
    FSceneView* SceneView = Player->CalcSceneView(&ViewFamily, CameraInfo.ViewpointLocation, CameraInfo.ViewpointRotation, Player->ViewportClient->Viewport);

    FViewMatrices& ViewMatrices = SceneView->ViewMatrices;
    // TODO: Check(SceneView)
    CameraInfo.Model = GetComponentTransform().ToMatrixWithScale();
    CameraInfo.View = ViewMatrices.GetViewMatrix();
    CameraInfo.Projection = ViewMatrices.GetProjectionMatrix();
    
    CameraInfo.ModelView = CameraInfo.View * CameraInfo.Model;
    
    CameraInfo.InvertedModelView = CameraInfo.ModelView.Inverse(); 
    
    CameraInfo.ModelViewProjection = CameraInfo.Projection * CameraInfo.View * CameraInfo.Model;
    CameraInfo.InvertedModelViewProjection = CameraInfo.ModelViewProjection.Inverse();

    CameraInfo.ViewFrustum = SceneView->ViewFrustum;

    for (auto& Plane : CameraInfo.ViewFrustum.Planes)
    {
        float PlaneNorm = FMath::Sqrt(Plane.X * Plane.X + Plane.Y * Plane.Y + Plane.Z * Plane.Z);
        Plane *= 1.0f / PlaneNorm;
    }

    const FMatrix InverseModel = CameraInfo.Model.Inverse();
    CameraInfo.ViewpointLocation = InverseModel.TransformPosition(FVector(0, 0, 0));
    CameraInfo.ViewDirection = InverseModel.TransformPosition(FVector(0, 1, 0)) - CameraInfo.ViewpointLocation;
    CameraInfo.ViewDirection *= 1.0f / CameraInfo.ViewDirection.SizeSquared();

    const FMatrix InvertedProjection = CameraInfo.Projection.Inverse();
    const FVector SceneCenter = InvertedProjection.TransformPosition(FVector(0, 1, 0));
    const FVector SceneSide = InvertedProjection.TransformPosition(FVector(1, 0, -1));

    const float ZNear = SceneCenter.Size();
    const float Side = (SceneSide - SceneCenter).Size();
    const int ViewportWidth = CameraInfo.ViewportSize.X;
    const float ResolutionThisFrame = (2 * Side / ZNear) / ViewportWidth;
    CameraInfo.IsUsingSameResolutionAsBefore = CameraInfo.CurrentResolution == ResolutionThisFrame;
    CameraInfo.CurrentResolution = ResolutionThisFrame;
}

void UUnrealNexusComponent::UpdateBodySetup()
{
    if (!BodySetup)
        BodySetup = NewObject<UBodySetup>(this, UBodySetup::StaticClass());
    
	BodySetup->AggGeom.EmptyElements( );
    const FVector BoxSize = FVector(ComponentBoundsRadius, ComponentBoundsRadius, ComponentBoundsRadius);
    FKBoxElem& BoxElem = *new ( BodySetup->AggGeom.BoxElems ) FKBoxElem( BoxSize.X, BoxSize.Y, BoxSize.Z );
    BoxElem.Center = GetComponentLocation();
    BoxElem.Rotation = GetComponentRotation();

    GetBodySetup()->ClearPhysicsMeshes();
    GetBodySetup()->CreatePhysicsMeshes();
    BodyInstance.SetResponseToAllChannels( ECR_Overlap );
    BodyInstance.SetResponseToChannel( ECC_Camera, ECR_Ignore );
    BodyInstance.SetResponseToChannel( ECC_Visibility, ECR_Block );
    BodyInstance.SetInstanceNotifyRBCollision( true );
    
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
}

void UUnrealNexusComponent::BeginPlay()
{
    Super::BeginPlay();
    if (NexusFile.IsEmpty()) return;
    Open(NexusFile);

    if (bIsLoaded && Proxy)
        Proxy->GetReady();
}

UBodySetup* UUnrealNexusComponent::GetBodySetup()
{
    return BodySetup;
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
