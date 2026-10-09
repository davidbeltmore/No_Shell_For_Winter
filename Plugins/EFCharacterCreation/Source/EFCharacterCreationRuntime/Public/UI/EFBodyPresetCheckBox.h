#pragma once
#include "CoreMinimal.h"
#include "Components/CheckBox.h"
#include "EFBodyPresetCheckBox.generated.h"

class UEFCharacterCreationRootWidget;

/** Catalog-backed single-choice control; the component remains selection authority. */
UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFBodyPresetCheckBox : public UCheckBox
{
	GENERATED_BODY()
public:
	void InitializeChoice(UEFCharacterCreationRootWidget* InOwner, FName InPresetId, bool bSelected);
	UPROPERTY(Transient) FName PresetId;
private:
	UPROPERTY(Transient) TWeakObjectPtr<UEFCharacterCreationRootWidget> Owner;
	UFUNCTION() void HandleChoiceChanged(bool bSelected);
};
