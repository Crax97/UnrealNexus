#pragma once
#include "dag.h"
#include "nexusdata.h"


#include "UnrealNexusNodeData.generated.h"

UCLASS()
class NEXUSPLUGIN_API UUnrealNexusNodeData final : public UObject
{
    GENERATED_BODY()
private:
    bool DidDecodeData = false;
    
public:
    nx::NodeData NexusNodeData;
    uint32 NodeSize = 0;
    
    FORCEINLINE bool IsDataDecoded() const { return DidDecodeData; }
    void DecodeData(nx::Header& Header, int VertsCount, int FacesCount);
    void SerializeNodeData(FArchive& Archive, nx::NodeData& NodeData);

    // Begin UObject interface
    virtual void Serialize( FArchive& Archive ) override;
    // End UObject interface
};
