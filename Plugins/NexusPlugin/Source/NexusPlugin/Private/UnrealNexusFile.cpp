#include "UnrealNexusFile.h"

#include "HAL/PlatformFilemanager.h"

FUnrealNexusFile::FUnrealNexusFile()
    : FileHandle(nullptr), MappedFileHandle(nullptr) { }

FUnrealNexusFile::~FUnrealNexusFile()
{
    if (FileHandle)
    {
        FileHandle->Flush();
        delete FileHandle;
    }
    if (MappedFileHandle)
        delete MappedFileHandle;
}

void FUnrealNexusFile::setFileName(const char* URI)
{
    FilePath = FString(URI);
}

bool FUnrealNexusFile::open(OpenMode Mode)
{
    if(!FPaths::FileExists(FilePath))
    {
        return false;
    }
    auto& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (Mode == OpenMode::Read)
    {
        FileHandle = PlatformFile.OpenRead(*FilePath, Mode & OpenMode::Write);
        MappedFileHandle = PlatformFile.OpenMapped(*FilePath);
        if(!MappedFileHandle) { return false; }
    } else if (Mode == OpenMode::Write)
    {
        FileHandle = PlatformFile.OpenWrite(*FilePath, false);
    }
    return FileHandle != nullptr;
}

int FUnrealNexusFile::read(char* Where, unsigned int Length)
{
    if (FileHandle->Read(reinterpret_cast<uint8*>(Where), Length))
    {
        return Length;
    }
    return -1;
}


int FUnrealNexusFile::write(char* From, unsigned int Length)
{
    if (FileHandle->Write(reinterpret_cast<uint8*>(From), Length))
    {
        return Length;
    }
    return -1;
}

int FUnrealNexusFile::size()
{
    return FileHandle->Size();
}

void* FUnrealNexusFile::map(unsigned int From, unsigned int Size)
{
    checkf(MappedFileHandle, TEXT("Tried mapping from a file that either isn't read or is write only"))
    IMappedFileRegion* Region = MappedFileHandle->MapRegion(From, Size);
    void* Mapped = static_cast<void*>(const_cast<uint8*>(Region->GetMappedPtr()));
    MappedRegions.Add(Mapped, Region);
    return Mapped;
}

bool FUnrealNexusFile::unmap(void* Mapped)
{
    if(!MappedRegions.Contains(Mapped)) return false;
    const auto Region = MappedRegions[Mapped];

    MappedRegions.Remove(Region);
    delete Region;
    return true;
}

bool FUnrealNexusFile::seek(unsigned int To)
{
    return FileHandle->Seek(To);
}
