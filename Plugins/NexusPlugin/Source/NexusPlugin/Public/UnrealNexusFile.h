#pragma once

#include "nexusfile.h"

#include "Async/MappedFileHandle.h"
#include "GenericPlatform/GenericPlatformFile.h"

using namespace nx;

class FUnrealNexusFile final :
    public NexusFile
{
private:
    IFileHandle* FileHandle;
    IMappedFileHandle* MappedFileHandle;
    FString FilePath;
    TMap<void*, IMappedFileRegion*> MappedRegions;
    
public:
    explicit FUnrealNexusFile();
    ~FUnrealNexusFile();
    virtual void setFileName(const char* URI) override;
    virtual bool open(OpenMode Mode) override;
    virtual int read(char* Where, unsigned int Length) override;
    virtual int write(char* From, unsigned int Length) override;
    virtual int size() override;
    virtual void* map(unsigned int From, unsigned int Size) override;
    virtual bool unmap(void* Mapped) override;
    virtual bool seek(unsigned int To) override;
};
