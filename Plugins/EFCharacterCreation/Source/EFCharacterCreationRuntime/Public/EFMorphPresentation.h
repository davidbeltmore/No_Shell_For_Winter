#pragma once

#include "CoreMinimal.h"
#include "EFCharacterCreationTypes.h"

/** Presentation only: never changes morph identity, target, range or saved values. */
namespace EFMorphPresentation
{
	struct FClassification
	{
		FName Category = TEXT("Body");
		FString Section = TEXT("General & Proportions");
		bool bRecognized = false;
	};
	EFCHARACTERCREATIONRUNTIME_API FClassification Classify(const FString& Name);
	EFCHARACTERCREATIONRUNTIME_API FString CleanLabel(const FString& Name, const FString& Section);
	EFCHARACTERCREATIONRUNTIME_API const TArray<FString>& Sections(FName Category);
	EFCHARACTERCREATIONRUNTIME_API FString NormalizeSection(const FString& Section);
}
