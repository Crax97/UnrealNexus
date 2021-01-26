#pragma once
#include "NexusJobExecutorThread.h"

#include "NexusJobExecutorTester.generated.h"

UCLASS(meta = (BlueprintSpawnableComponent))
class UNexusJobExecutorTester
     : public UPrimitiveComponent{
    
    GENERATED_BODY()
    
private:
    class FNexusJobExecutorThread* ExecuterThread;
    class FRunnableThread* Thread;
    class FUnrealNexusData* TestData;
    class UUnrealNexusComponent* ChildComponent;

    float CurrentTime = 0.0f;
    float NextWaitTime = GetNextRandomTimer();
    TArray<uint32> ValidNodes;
    
    float GetNextRandomTimer() const { return MinTimeToWait + FMath::FRand() * (MaxTimeToWait - MinTimeToWait); }
    
public:
    explicit UNexusJobExecutorTester(const FObjectInitializer& Initializer);
    static void LogJob(const FNexusJob& NexusJob);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;
    
    TArray<FNexusJob> GenerateRandomJobs();

    UPROPERTY(EditAnywhere)
    float MinTimeToWait = 1.0f;
    
    UPROPERTY(EditAnywhere)
    float MaxTimeToWait = 3.0f;
};
