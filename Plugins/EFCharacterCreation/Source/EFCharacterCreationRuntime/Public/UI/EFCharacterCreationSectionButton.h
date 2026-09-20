#pragma once
#include "Components/Button.h"
#include "EFCharacterCreationSectionButton.generated.h"

DECLARE_DELEGATE_OneParam(FEFOnSectionSelected, const FString&);

/** Button with stable data, so dynamic anatomical sections do not need per-section UFunctions. */
UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFCharacterCreationSectionButton : public UButton
{
	GENERATED_BODY()
public:
	FString Section;
	FEFOnSectionSelected OnSectionSelected;
	void InitializeSection(const FString& InSection);
private:
	UFUNCTION() void DispatchSection();
};
