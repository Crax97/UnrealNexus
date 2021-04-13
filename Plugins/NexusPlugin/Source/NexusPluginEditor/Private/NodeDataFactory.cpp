#include "NodeDataFactory.h"


#include "AssetRegistryModule.h"
#include "UnrealNexusNodeData.h"

UObject* UNodeDataFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
    UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<UUnrealNexusNodeData>(InParent, InName, Flags);
}

UNodeDataFactory::UNodeDataFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SupportedClass = UUnrealNexusNodeData::StaticClass();
    bEditorImport = false;
    bCreateNew = false;
}

UUnrealNexusNodeData* UNodeDataFactory::CreateNodeAssetFile(UPackage* NodePackage, FString& NodeName, EObjectFlags Flags)
{
    UObject* ObjectCreated = FactoryCreateNew(SupportedClass, NodePackage, *NodeName, Flags, nullptr, GWarn);
    UUnrealNexusNodeData* NewNode = Cast<UUnrealNexusNodeData>(ObjectCreated);

    FAssetRegistryModule::AssetCreated(NewNode);
    if(!NewNode->MarkPackageDirty()) { /* What? */}
    if(!NodePackage->MarkPackageDirty()) { /* What? 2 */}
    NewNode->PostEditChange();
    NewNode->AddToRoot();
    return NewNode;
}
