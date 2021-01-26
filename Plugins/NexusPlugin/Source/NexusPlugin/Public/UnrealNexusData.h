#pragma once

#include "nexusdata.h"
#include "UnrealNexusFile.h"
#include "NexusUtils.h"

class FUnrealNexusData final :
    public nx::NexusData
{
private:

    bool ParseHeader();
    bool InitData();
    TSet<int> LoadedNodes;
    
public:
    FUnrealNexusData()
    {
        file = new FUnrealNexusFile();
    }

    bool Init();
    void LoadIntoRam(int N);
    void DropFromRam(int N);
    
    ~FUnrealNexusData()
    {
    }
    virtual void loadImageFromData(nx::TextureData& Data, int TextureIndex) override;
};
