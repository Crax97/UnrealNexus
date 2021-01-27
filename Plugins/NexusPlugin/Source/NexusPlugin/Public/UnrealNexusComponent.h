#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "MeshDescription.h"
#include "nexusdata.h"
#include "UnrealNexusData.h"

#include "UnrealNexusComponent.generated.h"

enum class ENodeStatus
{
    Dropped, // The node isn't loaded
    Pending, // The node has been selected for loading from disk
    Loaded, // The node is loaded in memory
};

struct FCameraInfo
{
    FViewport* Viewport;
    FVector ViewpointLocation; // Viewport
    FRotator ViewpointRotation;
    FMatrix Model;
    FMatrix View;
    FMatrix Projection;
    FMatrix ModelView, InvertedModelView;
    FMatrix ModelViewProjection, InvertedModelViewProjection;
    FConvexVolume ViewFrustum;
    float CurrentResolution;
    bool IsUsingSameResolutionAsBefore;
};

struct FTraversalElement
{
    Node* TheNode;
    UINT32 Id;
};

struct FTraversalData
{
    
    TArray<FTraversalElement> TraversalQueue;
    TSet<UINT32> BlockedNodes, SelectedNodes, VisitedNodes;
    TArray<float> InstanceErrors;
};

UENUM(BlueprintType)
enum class EWindingOrder : uint8
{
    Clockwise UMETA(DisplayName="Clockwise"),
    Counter_Clockwise UMETA(DisplayName="Counter Clockwise"),
};


UCLASS(meta = (BlueprintSpawnableComponent))
class UUnrealNexusComponent final
    : public UPrimitiveComponent
{
    GENERATED_BODY()

    friend class FUnrealNexusProxy;  
    friend class FNexusNodeRenderData;
    friend class UNexusJobExecutorTester;
    
private:
    bool Open(const FString& Source);
    FCameraInfo CameraInfo;
    int CurrentlyBlockedNodes = 0;
    int CurrentDrawBudget = 0;
    float CurrentError = 0.0f;

    float CalculateDistanceFromSphereToViewFrustum(const vcg::Sphere3f& Sphere3, const float SphereTightRadius) const;
    float CalculateErrorForNode(const UINT32 NodeID, bool UseTight) const;
    
protected:
    FUnrealNexusData* ComponentData = new FUnrealNexusData();
    class FUnrealNexusProxy* Proxy = nullptr;
    TMap<UINT32, ENodeStatus> NodeStatuses;
    bool bIsLoaded = false;
    
public:
    explicit UUnrealNexusComponent(const FObjectInitializer& Initializer);
    ~UUnrealNexusComponent();
    
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // TODO: Replace this with assets picker
    UPROPERTY(EditAnywhere)
    FString NexusFile;

    UFUNCTION(BlueprintCallable)
    bool IsReadyToStream() { return true; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int MaxBlockedNodes = 30;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int DrawBudget = 5 * (1 << 20);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TargetError = 2.0f;
    
    UPROPERTY(EditAnywhere)
    EWindingOrder WindingOrder = EWindingOrder::Counter_Clockwise;
    /*
    UFUNCTION(BlueprintCallable)
    bool IsStreaming();
    */

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    void UpdateCameraView();
    bool IsNodeLoaded(UINT32 NodeID) const;
    void SetNodeStatus(UINT32 NodeID, ENodeStatus NewStatus);
    bool CanNodeBeExpanded(Node* Node, int NodeID, float FirstNodeError, float CurrentCalculatedError) const;
    void AddNodeToTraversal(FTraversalData& TraversalData, const UINT32 NewNodeId) const;
    void AddNodeChildren(const FTraversalElement& CurrentElement, FTraversalData& TraversalData, bool ShouldMarkBlocked) const;
    FTraversalData DoTraversal();
    void Update();
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void OnComponentCreated() override;
    virtual void BeginPlay() override;
};
