#include "NexusJobExecutorThread.h"

#include "nexusfile.h"
#include "UnrealNexusData.h"
#include "UnrealNexusNodeData.h"

FNexusJobExecutorThread::FNexusJobExecutorThread(nx::NexusFile* InFile)
    : File(InFile)
{
    QueueJobInsertedEvent = FGenericPlatformProcess::GetSynchEventFromPool(false);
}

uint32 FNexusJobExecutorThread::Run()
{
    bShouldBeRunning = true;
    const int MaxTaskWaitTime = 100; // ms;
    while(bShouldBeRunning)
    {
        while(QueuedJobs.IsEmpty() && bShouldBeRunning)
        {
            // TODO Is there a way to avoid waiting for x ms?
            QueueJobInsertedEvent->Wait(MaxTaskWaitTime);
        }
        
        FNexusJob Job;
        while(QueuedJobs.Dequeue(Job))
        {
            auto NodeId = Job.NodeIndex;
            auto& Node = Job.Node;
            auto& Data = Job.NodeData;
            Data->DecodeData(Job.Data->Header, Node->NexusNode.nvert, Node->NexusNode.nface);
            
            if (Job.Data->Header.signature.face.hasTextures())
            {
                // TODO Decode texture
            }
            JobsDone.Enqueue(Job);
        }
#ifdef NEXUS_RUNNING_QUEUE_TESTS
        float MaxSleepTime = 0.5f;
        FPlatformProcess::Sleep(FMath::FRand() * MaxSleepTime);
#endif
    }
    return 0;
}

void FNexusJobExecutorThread::Stop()
{
    bShouldBeRunning = false;
}

void FNexusJobExecutorThread::AddNewJobs(TArray<FNexusJob> Jobs) noexcept
{
    if (Jobs.Num() == 0) return;
    
    for (auto Job : Jobs)
    {
        QueuedJobs.Enqueue(Job);
    }
    QueueJobInsertedEvent->Trigger();
}
