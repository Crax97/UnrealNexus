#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "MeshDescription.h"
#include "nexusdata.h"
#include "UnrealNexusData.h"

#include "UnrealNexusComponent.generated.h"

// Forward declarations
// ReSharper disable CppUE4CodingStandardNamingViolationWarning
namespace nx
{
    struct Node;
}
// ReSharper restore CppUE4CodingStandardNamingViolationWarning

using namespace nx;

enum class ENodeStatus
{
    Dropped, // The node isn't loaded
    Pending, // The node has been selected for loading from disk
    Loaded, // The node is loaded in memory
};

struct FCameraInfo
{
    FVector2D ViewportSize;
    FVector ViewpointLocation; // Viewport
    FRotator ViewpointRotation;
    FConvexVolume ViewFrustum;
    float CurrentResolution;
    bool IsUsingSameResolutionAsBefore;
    FMatrix WorldToModelMatrix;
};

struct FTraversalElement
{
    Node* TheNode;
    uint32 Id;
    float CalculatedError;
};

struct FTraversalData
{
    FVector ComponentLocation;
    TArray<FTraversalElement> TraversalQueue;
    TSet<uint32> BlockedNodes, SelectedNodes, VisitedNodes;
    TArray<float> InstanceErrors;
};


UCLASS(meta = (BlueprintSpawnableComponent))
class NEXUSPLUGIN_API UUnrealNexusComponent final
    : public UPrimitiveComponent
{
    GENERATED_BODY()

    friend class FUnrealNexusProxy;  
    friend class FNexusNodeRenderData;
    friend class UNexusJobExecutorTester;
    
private:
    FCameraInfo CameraInfo;
    int CurrentlyBlockedNodes = 0;
    int CurrentDrawBudget = 0;
    float CurrentError = 0.0f;
    bool bIsTraversalEnabled = true;
    bool bIsFrustumCullingEnabled = true;
    
    class FRunnableThread* JobThread = nullptr;
    class FNexusJobExecutorThread* JobExecutor = nullptr;

    // TODO: Load first node and calculate Radius based on that
    float ComponentBoundsRadius = 1000.0f;

    // An outer node is a node outside the view frustum
    // This is done to reduce the weight of outer nodes,
    // while being consistent with the tree
    const float Outer_Node_Factor = 100.0f;
    TArray<float> CalculatedErrors;
    uint64 CurrentCacheSize;

    float CalculateDistanceFromSphereToViewFrustum(const vcg::Sphere3f& Sphere3, const float SphereTightRadius) const;
    float CalculateErrorForNode(const uint32 NodeID, bool UseTight) const;
    void UpdateRemainingErrors(TArray<float>& InstanceErrors);
    void UpdateCameraView();
    void AllocateMemory();
    virtual void OnRegister() override;
    void CreateThreads();
    virtual void BeginPlay() override;
    void DeleteThreads();
    virtual void BeginDestroy() override;
protected:
    class FUnrealNexusProxy* Proxy = nullptr;
    TMap<uint32, ENodeStatus> NodeStatuses;

    // Updates the calculated error for the node
    void SetErrorForNode(uint32 NodeID, float Error);

    // Gets the calculated error for the node
    float GetErrorForNode(uint32 NodeID) const;
    
    bool CanNodeBeExpanded(Node* Node, int NodeID, float NodeError, float CurrentProxyError) const;
    void AddNodeToTraversal(FTraversalData& TraversalData, const uint32 NewNodeId);
    void AddNodeChildren(const FTraversalElement& CurrentElement, FTraversalData& TraversalData, bool ShouldMarkBlocked);

    
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
public:
    explicit UUnrealNexusComponent(const FObjectInitializer& Initializer);
    ~UUnrealNexusComponent();
    
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    uint64 GetNodeSize(const uint32 NodeID) const;
    void UnloadNode(uint32 UnloadedNodeID);
    void RequestNode(const uint32 BestNodeID);

    // https://docs.unrealengine.com/en-US/ProgrammingAndScripting/ProgrammingWithCPP/Assets/AsyncLoading/index.html
    // A TSoftObjectPtr is basically a TWeakObjectPtr that wraps around a FSoftObjectPath,
    // and will template to a specific class so you can restrict the editor UI to only
    // allow selecting certain classes.
    UPROPERTY(EditAnywhere)
    UUnrealNexusData* NexusLoadedAsset = nullptr;

    UFUNCTION(BlueprintCallable)
    bool IsReadyToStream() { return true; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int MaxBlockedNodes = 30;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int DrawBudget = 1024 * 1024 * 1024 * 1; // 1 GB

    UPROPERTY(EditAnywhere, BlueprintReadWrite, META=(ClampMin="0", ClampMax="50"))
    float TargetError = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, META=(ClampMin="0", ClampMax="30"))
    float MaxError;
    
    UPROPERTY(EditAnywhere)
    bool bShowDebugStuff = false;

    UPROPERTY(EditAnywhere)
    class UMaterialInterface* ModelMaterial = nullptr;

    UFUNCTION(BlueprintCallable)
    void ToggleTraversal(bool NewTraversalState);

    UFUNCTION(BlueprintCallable, BlueprintPure)
    FORCEINLINE bool IsTraversalEnabled() const { return bIsTraversalEnabled; }
    
    UFUNCTION(BlueprintCallable)
    void ToggleFrustumCulling(bool NewFrustumCullingState);

    UFUNCTION(BlueprintCallable, BlueprintPure)
    FORCEINLINE bool IsFrustumCullingEnabled() const { return bIsFrustumCullingEnabled; }
    
    /*
    UFUNCTION(BlueprintCallable)
    bool IsStreaming();
    */
    
    virtual void GetUsedMaterials(TArray <UMaterialInterface *> & OutMaterials, bool bGetDebugMaterials) const override;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    bool IsNodeLoaded(uint32 NodeID) const;
    void SetNodeStatus(uint32 NodeID, ENodeStatus NewStatus);
    void ClearErrors();
    FTraversalData DoTraversal();
};
