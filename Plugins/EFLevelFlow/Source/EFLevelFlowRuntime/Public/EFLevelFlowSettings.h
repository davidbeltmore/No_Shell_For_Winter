#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFLevelFlowSettings.generated.h"

class UUserWidget;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Level Flow"))
class EFLEVELFLOWRUNTIME_API UEFLevelFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFLevelFlowSettings();

	static const UEFLevelFlowSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	TArray<FString> DelayedSpawnMapNames;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	int32 MaxResolveAttempts = 900;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	float LoadingPollIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	double MinimumLoadingScreenSeconds = 10.0;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	bool bFreezePawnDuringLoading = true;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	bool bBlockPlayerInputDuringLoading = true;

	UPROPERTY(EditAnywhere, Config, Category = "UI")
	TSoftClassPtr<UUserWidget> LoadingScreenWidgetClass;
};
