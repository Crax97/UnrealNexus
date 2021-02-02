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
    FVector2D ViewportSize;
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
    FVector4 ViewDirection;
};

struct FTraversalElement
{
    Node* TheNode;
    UINT32 Id;
    float CalculatedError;
};

struct FTraversalData
{
    FVector ComponentLocation;
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

    float ComponentBoundsRadius = 0.0f;

    // An outer node is a node outside the view frustum
    // This is done to reduce the weight of outer nodes,
    // while being consistent with the tree
    const float Outer_Node_Factor = 100.0f;
    TArray<float> CalculatedErrors;
    UBodySetup* BodySetup = nullptr;


    float CalculateDistanceFromSphereToViewFrustum(const vcg::Sphere3f& Sphere3, const float SphereTightRadius) const;
    float CalculateErrorForNode(const UINT32 NodeID, bool UseTight) const;
    void UpdateRemainingErrors(TArray<float>& InstanceErrors);
    void UpdateCameraView();
    void UpdateBodySetup();
    
protected:
    FUnrealNexusData* ComponentData = new FUnrealNexusData();
    class FUnrealNexusProxy* Proxy = nullptr;
    TMap<UINT32, ENodeStatus> NodeStatuses;
    bool bIsLoaded = false;

    // Updates the status of the component
    void Update(float DeltaTime);

    // Updates the calculated error for the node
    void SetErrorForNode(UINT32 NodeID, float Error);

    // Gets the calculated error for the node
    float GetErrorForNode(UINT32 NodeID) const;
    
    bool CanNodeBeExpanded(Node* Node, int NodeID, float NodeError, float CurrentCalculatedError) const;
    void AddNodeToTraversal(FTraversalData& TraversalData, const UINT32 NewNodeId);
    void AddNodeChildren(const FTraversalElement& CurrentElement, FTraversalData& TraversalData, bool ShouldMarkBlocked);
    
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void OnComponentCreated() override;
    virtual void BeginPlay() override;
    virtual UBodySetup* GetBodySetup() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, META=(ClampMin="0", ClampMax="30"))
    float TargetError = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, META=(ClampMin="0"))
    float RootNodesInitialError = 1e20;
    
    UPROPERTY(EditAnywhere)
    EWindingOrder WindingOrder = EWindingOrder::Counter_Clockwise;

    UPROPERTY(EditAnywhere)
    class UMaterialInterface* ModelMaterial = nullptr;
    /*
    UFUNCTION(BlueprintCallable)
    bool IsStreaming();
    */
    virtual void GetUsedMaterials(TArray <UMaterialInterface *> & OutMaterials, bool bGetDebugMaterials) const override;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    bool IsNodeLoaded(UINT32 NodeID) const;
    void SetNodeStatus(UINT32 NodeID, ENodeStatus NewStatus);
    FTraversalData DoTraversal();
};
