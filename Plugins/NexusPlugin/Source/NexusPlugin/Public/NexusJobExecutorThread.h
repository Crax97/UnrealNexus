#pragma once

namespace nx {
    class NexusFile;
}

enum class EJobKind : uint8
{
    Load,
    Drop
};
struct FNexusJob
{
    EJobKind Kind;
    uint32 NodeIndex;
    class UUnrealNexusData* Data;
};

class FNexusJobExecutorThread final
    : public FRunnable
{
private:
    FEvent* QueueJobInsertedEvent;
    // FPThreadEvent JobQueueEmpty;
    FCriticalSection QueueMutex;
    class nx::NexusFile* File;
    
    bool bShouldBeRunning = false;
    
protected:
    TQueue<FNexusJob> QueuedJobs;
    TQueue<FNexusJob> JobsDone;
public:

    explicit FNexusJobExecutorThread(class nx::NexusFile* InFile);

    virtual uint32 Run() override;
    virtual void Stop() override;

    void AddNewJobs(TArray<FNexusJob> Jobs) noexcept;
    TQueue<FNexusJob>& GetJobsDone() { return JobsDone; }
    
};
