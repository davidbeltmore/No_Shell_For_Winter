#pragma once

#include "EFProjectThemedUserWidget.h"
#include "ProjectCharacterBackgroundEffectEntryWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundEffectEntryWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	void SetEffectText(const FText& InText, bool bInNegative);

private:
	void BuildWidgetTree();
	void InitializeVisualTree();

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EffectText;

	bool bNegative = false;
	bool bVisualTreeInitialized = false;
};
