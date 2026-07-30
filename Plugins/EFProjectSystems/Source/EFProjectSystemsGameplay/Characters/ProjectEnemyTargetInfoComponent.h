#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectEnemyTargetInfoComponent.generated.h"

class UProjectTargetLevelWidget;
class UProjectTargetPointWidget;
class UWidgetComponent;

USTRUCT()
struct FProjectEnemyTargetDisplayData
{
	GENERATED_BODY()

	FText EnemyType = FText::FromString(TEXT("Enemy"));
	FText EnemyName = FText::FromString(TEXT("Enemy"));
	int32 Level = 1;
	float CurrentHealth = -1.0f;
	float MaxHealth = -1.0f;
	float HealthRatio = 0.0f;
};

UCLASS(ClassGroup = (Project), meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyTargetInfoComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectEnemyTargetInfoComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	void ShowTargetInfo();

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	void HideTargetInfo();

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	void RefreshTargetInfo();

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool HasConstructedTargetWidgets() const;

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool IsTargetLevelOverlayVisible() const;

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool IsTargetStatsOverlayVisible() const;

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool IsTargetPointOverlayVisible() const;

	FText GetEnemyTypeText() const;
	bool TryBuildDisplayData(FProjectEnemyTargetDisplayData& OutDisplayData) const;
	bool TryGetTargetInfoAnchorWorldLocation(FVector& OutWorldLocation) const;

private:
	void EnsureWidgetComponents();
	void UpdateWidgetAttachment();
	void DisableLegacyTargetWidgets();
	UProjectTargetLevelWidget* ResolveTargetWidget() const;
	UProjectTargetPointWidget* ResolvePointWidget() const;
	FText GetEnemyNameText() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> TargetWidgetComponent;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> StatsWidgetComponent;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> PointWidgetComponent;
};
