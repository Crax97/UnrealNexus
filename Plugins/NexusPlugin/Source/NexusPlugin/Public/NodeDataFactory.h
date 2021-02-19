#pragma once
#include "dag.h"

#include "NodeDataFactory.generated.h"

namespace nx {
    class NodeData;
}

UCLASS()
class UNodeDataFactory final : public UFactory
{
    GENERATED_BODY()
    private:

    virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

    
public:
    explicit UNodeDataFactory(const FObjectInitializer& ObjectInitializer);
    class UUnrealNexusNodeData* CreateNodeAssetFile(UPackage* NodePackage, FString& NodeName);
};
