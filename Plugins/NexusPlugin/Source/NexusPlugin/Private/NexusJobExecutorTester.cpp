#include "NexusJobExecutorTester.h"
#include "NexusJobExecutorThread.h"
#include "UnrealNexusComponent.h"
#include "UnrealNexusData.h"

UNexusJobExecutorTester::UNexusJobExecutorTester(const FObjectInitializer& Initializer)
        : UPrimitiveComponent(Initializer)
{

    TestData = new FUnrealNexusData;
    auto& TestFile = TestData->file;
    auto GameContentPath = FPaths::ProjectContentDir();
    const auto FilePath = FPaths::Combine(GameContentPath, TEXT("skull.nxs"));
    char* CStr = TCHAR_TO_ANSI(*FilePath);
    TestFile->setFileName(CStr);
    check(TestData->Init());
    
    ExecuterThread = new FNexusJobExecutorThread(TestData->file);
    Thread = FRunnableThread::Create(ExecuterThread, TEXT("Nexus Test Thread"));
    ChildComponent = CreateDefaultSubobject<UUnrealNexusComponent>(TEXT("TEST Component"));

    auto& Data = ChildComponent->ComponentData;
    auto& Header = Data->header;
    for (uint32 i = 0; i < Header.n_nodes; i ++)
    {
        ValidNodes.Add(i);
    }
    
    PrimaryComponentTick.bCanEverTick = true;
}


void UNexusJobExecutorTester::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* ThisTickFunction)
{
    CurrentTime += DeltaTime;
    if (CurrentTime >= NextWaitTime)
    {
        CurrentTime = 0.0f;
        NextWaitTime = GetNextRandomTimer();
        const auto Jobs = GenerateRandomJobs();
        
        UE_LOG(NexusInfo, Log, TEXT("------NEXUS THREAD TEST------"));
        UE_LOG(NexusInfo, Log, TEXT("Sending %d Jobs"), Jobs.Num());
        UE_LOG(NexusInfo, Log, TEXT("-----------------------------"));
        ExecuterThread->AddNewJobs(Jobs);
    }

    FNexusJob CompletedJob;
    TQueue<FNexusJob>& CompletedJobs = ExecuterThread->GetJobsDone();
    while (CompletedJobs.Dequeue(CompletedJob))
    {
        LogJob(CompletedJob);
        TestData->DropFromRam(CompletedJob.NodeIndex);
        ValidNodes.Add(CompletedJob.NodeIndex);
    }
}

void UNexusJobExecutorTester::LogJob(const FNexusJob& NexusJob)
{
    UE_LOG(NexusInfo, Log, TEXT("------NEXUS THREAD TEST------"));
    UE_LOG(NexusInfo, Log, TEXT("Loaded node %d"), NexusJob.NodeIndex);
    UE_LOG(NexusInfo, Log, TEXT("-----------------------------"));
}

TArray<FNexusJob> UNexusJobExecutorTester::GenerateRandomJobs()
{
    auto& Data = ChildComponent->ComponentData;
    const uint32 JobsToCreate = FMath::RandRange(0, ValidNodes.Num() - 1);
    TArray<FNexusJob> Jobs;
    for (uint32 i = 0; i < JobsToCreate; i ++)
    {
        const uint32 JobIndex = FMath::RandRange(0, ValidNodes.Num() - 1);
        const uint32 NodeIndex = ValidNodes[JobIndex];
        ValidNodes.RemoveAt(JobIndex);
        Jobs.Add(FNexusJob {EJobKind::Load, NodeIndex, Data});
    }
    return Jobs;
}



