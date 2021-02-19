#pragma once
#include "dag.h"
#include "nexusdata.h"


#include "UnrealNexusNodeData.generated.h"

UCLASS()
class UUnrealNexusNodeData final : public UObject
{
    GENERATED_BODY()

public:
    nx::NodeData NexusNodeData;
    UINT32 NodeSize = 0;

    // Begin UObject interface
    virtual void Serialize( FArchive& Archive ) override;
    // End UObject interface
};

