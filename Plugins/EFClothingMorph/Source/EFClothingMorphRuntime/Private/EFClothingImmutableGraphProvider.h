#pragma once

#include "CoreMinimal.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "OptimusValue.h"
#include "EFClothingImmutableGraphProvider.generated.h"

class UOptimusDeformerInstance;
struct FEFClothingImmutableGraphBuffers;

/** Per-instance adapter for the owned surface graph. Scalars remain live; binding arrays do not change. */
UCLASS()
class UEFClothingImmutableGraphProvider : public UComputeDataProvider
{
    GENERATED_BODY()
public:
    static bool Install(UOptimusDeformerInstance* Instance, FString& Failure);
    FComputeDataProviderRenderProxy* GetRenderProxy() override;
    void Reset() override;

    struct FVariable
    {
        FOptimusValueIdentifier Id;
        int32 Offset = 0;
        int32 Size = 0;
    };
    TWeakObjectPtr<UOptimusDeformerInstance> Instance;
    TArray<FVariable> Variables;
    int32 ParameterBytes = 0;
    TSharedPtr<FEFClothingImmutableGraphBuffers, ESPMode::ThreadSafe> Buffers;
};
