#pragma once

#include "CoreMinimal.h"
#include "EFBlinkTypes.generated.h"

USTRUCT(BlueprintType)
struct EFBLINKRUNTIME_API FEFBlinkMorphTarget
{
	GENERATED_BODY()

	FEFBlinkMorphTarget() = default;

	FEFBlinkMorphTarget(const FName InMorphName, const float InVisibleValue, const float InRestValue = 0.0f)
		: MorphName(InMorphName)
		, VisibleValue(InVisibleValue)
		, RestValue(InRestValue)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (DisplayName = "Morph Name"))
	FName MorphName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (DisplayName = "Visible Value"))
	float VisibleValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (DisplayName = "Rest Value"))
	float RestValue = 0.0f;
};

namespace EFBlinkDefaults
{
	EFBLINKRUNTIME_API TArray<FEFBlinkMorphTarget> MakeMorphTargets();
	EFBLINKRUNTIME_API TArray<FString> MakePreferredMeshNameTokens();
}
