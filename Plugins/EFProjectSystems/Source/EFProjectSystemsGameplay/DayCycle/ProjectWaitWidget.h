#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "ProjectWaitWidget.generated.h"

// Project-owned wait UI; ACF's stack owns focus, input locking and modal pause.
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectWaitWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()
public:
	UProjectWaitWidget(const FObjectInitializer& Initializer);
	virtual TSharedRef<SWidget> RebuildWidget() override;
protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
private:
	float SelectedHours = 1.f;
	bool bPreviousCursorVisible = false;
};
