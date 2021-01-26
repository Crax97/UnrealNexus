#include "UnrealNexusComponent.h"

#include "UnrealNexusData.h"
#include "UnrealNexusProxy.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"

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
    return true;
}

FPrimitiveSceneProxy* UUnrealNexusComponent::CreateSceneProxy()
{
    Proxy = new FUnrealNexusProxy(this);
    return static_cast<FPrimitiveSceneProxy*>(Proxy);
}

void UUnrealNexusComponent::Update()
{
    if (!bIsLoaded || !Proxy) return;
    UpdateCameraView();
    FTraversalData TraversalData = DoTraversal();
    Proxy->Update();
}

void UUnrealNexusComponent::UpdateCameraView()
{
    // TODO: Remove hardcoded player index
    // TODO: Check for Viewport, ViewportClient and Player
    ULocalPlayer* Player = GetWorld()->GetFirstLocalPlayerFromController();
    Info.Viewport = Player->ViewportClient->Viewport;
    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        Info.Viewport,
        GetWorld()->Scene,
        Player->ViewportClient->EngineShowFlags)
        .SetRealtimeUpdate(true));
    FSceneView* SceneView = Player->CalcSceneView(&ViewFamily, Info.ViewpointLocation, Info.ViewpointRotation, Info.Viewport);

    FViewMatrices& ViewMatrices = SceneView->ViewMatrices;
    // TODO: Check(SceneView)
    Info.Model = GetComponentTransform().ToMatrixWithScale();
    Info.View = ViewMatrices.GetViewMatrix();
    Info.Projection = ViewMatrices.GetProjectionMatrix();
    
    Info.ModelView = Info.Model * Info.View;
    // Composition of affine transformations so the det is always != 0, we can use InverseFast
    Info.InvertedModelView = Info.ModelView.InverseFast(); 
    
    Info.ModelViewProjection = Info.Model * Info.View * Info.Projection;
    Info.InvertedModelViewProjection = Info.ModelViewProjection.InverseFast();

    Info.ViewFrustum = SceneView->ViewFrustum;
    
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
    
    // Load roots
    for (UINT32 i = 0; i < ComponentData->nroots; i ++)
    {
        VisitingNodes.HeapPush({ &ComponentData->nodes[i], i }, FNodeComparator() );
    }

    int RequestedCount = 0;
    CurrentlyBlockedNodes = 0;
    while(VisitingNodes.Num() > 0 && CurrentlyBlockedNodes < MaxBlockedNodes)
    {
        const float FirstNodeError = ComponentData->nodes[0].error;
        FTraversalElement CurrentElement = VisitingNodes.Pop();

        const int Id = CurrentElement.Id;
        if(!IsNodeLoaded(Id) && CurrentlyBlockedNodes < MaxBlockedNodes)
        {
            Proxy->AddCandidate(Id);
            RequestedCount ++;
        }

        const bool IsBlocked = BlockedNodes.Contains(Id) || !CanNodeBeExpanded(CurrentElement.TheNode, CurrentElement.Id, FirstNodeError, CurrentError);
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
    return TraversalData;
}

bool UUnrealNexusComponent::CanNodeBeExpanded(Node* Node, const int NodeID, const float FirstNodeError, const float CurrentCalculatedError) const
{
    return FirstNodeError > CurrentCalculatedError &&
        CurrentDrawBudget < DrawBudget &&
        !IsNodeLoaded(NodeID);
}


void UUnrealNexusComponent::AddNodeChildren(const FTraversalElement& CurrentElement, FTraversalData& TraversalData, bool ShouldMarkBlocked) const
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
        if (ShouldMarkBlocked) TraversalData.BlockedNodes.Add(PatchNodeId);
        if (!TraversalData.VisitedNodes.Contains(PatchNodeId))
            AddNodeToTraversal(TraversalData, PatchNodeId);
    }
}

void UUnrealNexusComponent::AddNodeToTraversal(FTraversalData& TraversalData, const UINT32 NewNodeId) const
{
    TraversalData.VisitedNodes.Add(NewNodeId);
    Node* NewNode = &ComponentData->nodes[NewNodeId];

    // TODO: Update Errors
    
    TraversalData.TraversalQueue.HeapPush({NewNode, NewNodeId}, FNodeComparator());
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
    Update();

}
