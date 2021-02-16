#pragma once

#include "NexusFactory.generated.h"

class UUnrealNexusData;

UCLASS(MinimalAPI, hidecategories=Object)
class UNexusFactory final : public UFactory
{
    GENERATED_BODY()
private:
    static bool ParseHeader(UUnrealNexusData* NexusData, uint8*& Buffer, const uint8* BufferEnd);   
public:
    explicit UNexusFactory(const FObjectInitializer& ObjectInitializer);
    static bool ReadDataIntoNexusFile(UUnrealNexusData* UnrealNexusData, uint8*& Buffer, const uint8* BufferEnd);
    static void InitData(UUnrealNexusData* Data, uint8*& Buffer);
    //~ Begin UFactory Interface
    virtual UObject* FactoryCreateBinary( UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* FileType, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn ) override;
    //~ End UFactory Interface
    
};
