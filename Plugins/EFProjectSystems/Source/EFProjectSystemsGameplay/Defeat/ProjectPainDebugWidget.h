#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "ProjectPainDebugWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;
class UProjectDefeatFlowComponent;

UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPainDebugWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectPainDebugWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void SetObservedFlowComponent(UProjectDefeatFlowComponent* InObservedFlowComponent);

private:
	void BuildWidgetTree();
	void RefreshText();

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> FrameBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DebugText;

	UPROPERTY(Transient)
	TObjectPtr<UProjectDefeatFlowComponent> ObservedFlowComponent;
};
