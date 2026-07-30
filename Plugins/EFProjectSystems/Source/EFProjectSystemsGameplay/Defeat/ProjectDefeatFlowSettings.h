#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "ProjectDefeatFlowSettings.generated.h"

class UProjectKnockoutStruggleWidget;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Defeat Flow"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDefeatFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectDefeatFlowSettings();

	static const UProjectDefeatFlowSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "1.0"))
	float PainKnockoutThreshold = 100.f;

	UPROPERTY(EditAnywhere, Config, Category = "Pain")
	bool bEnableAdvancedDefeatFlow = true;

	UPROPERTY(EditAnywhere, Config, Category = "Pain")
	bool bEnablePainDebugWidget = false;

	UPROPERTY(EditAnywhere, Config, Category = "Pain")
	bool bEnablePainDebugLogs = true;

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "0"))
	int32 PainDebugWidgetZOrder = 260;

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "0.0"))
	float PainPerAppliedDamage = 0.01f;

	static float ComputePainFromAppliedDamage(float AppliedDamage, float PainPerAppliedDamage);

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "0.1"))
	float KnockoutDurationSeconds = 10.f;

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "0.1"))
	float PainDecayDelaySeconds = 5.f;

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "0.1"))
	float PainDecayIntervalSeconds = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Pain", meta = (ClampMin = "0.0"))
	float PainDecayAmountPerTick = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat", meta = (ClampMin = "0.1"))
	float DefeatCombatWindowSeconds = 5.f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LosingHealthThresholdPct = 0.30f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat")
	TArray<FString> QualifiedEnemyClassNameHints = {
		TEXT("DummyMale"),
		TEXT("MeleeMale"),
		TEXT("RangedMale"),
		TEXT("MageMale"),
		TEXT("ACFMeleeEnemyBP"),
		TEXT("ACFRangedEnemyBP"),
		TEXT("ACFMageEnemyBP")
	};

	UPROPERTY(EditAnywhere, Config, Category = "Knockout", meta = (ClampMin = "0.0"))
	float StruggleGraceSeconds = 0.75f;

	UPROPERTY(EditAnywhere, Config, Category = "Knockout", meta = (ClampMin = "0.0"))
	float StruggleEnemyRadius = 1000.f;

	UPROPERTY(EditAnywhere, Config, Category = "Knockout")
	FName KnockoutStatusName = TEXT("KnockedOut");

	UPROPERTY(EditAnywhere, Config, Category = "Knockout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KnockoutRecoveryHealthPct = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Knockout", meta = (ClampMin = "0.0"))
	float KnockoutOutOfCombatRecoverySeconds = 30.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0"))
	int32 BaseStruggleNoteCount = 4;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "1"))
	int32 MaxStruggleNoteCount = 24;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "1"))
	int32 MinTotalStruggleNoteCount = 20;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0.01"))
	float BaseHitWindowSeconds = 0.30f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0.01"))
	float MinHitWindowSeconds = 0.18f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0.05"))
	float BaseTravelTimeSeconds = 1.75f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0.05"))
	float MinTravelTimeSeconds = 1.20f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "1"))
	int32 MaxStruggleMisses = 5;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0.05"))
	float StruggleNoteSpacingMinSeconds = 0.32f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0.05"))
	float StruggleNoteSpacingMaxSeconds = 0.56f;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle", meta = (ClampMin = "0"))
	int32 KnockoutStruggleWidgetZOrder = 240;

	UPROPERTY(EditAnywhere, Config, Category = "Struggle")
	TSoftClassPtr<UProjectKnockoutStruggleWidget> KnockoutStruggleWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory", meta = (ClampMin = "0"))
	int32 ModerateLossMinEntries = 2;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ModerateLossRatio = 0.20f;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory", meta = (ClampMin = "0"))
	int32 ModerateLossMaxEntries = 4;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory", meta = (ClampMin = "0"))
	int32 DefeatedRetainedMinEntries = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory", meta = (ClampMin = "0"))
	int32 DefeatedRetainedMaxEntries = 2;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefeatedRetainedRatio = 0.10f;

	UPROPERTY(EditAnywhere, Config, Category = "Inventory")
	TArray<FString> NonRemovableNameHints = {
		TEXT("Quest"),
		TEXT("Story"),
		TEXT("Locked"),
		TEXT("KeyItem"),
		TEXT("NoDrop"),
		TEXT("Soulbound")
	};

	UPROPERTY(EditAnywhere, Config, Category = "Defeated")
	FName DefeatedMapName = TEXT("DungeonGeneration");

	UPROPERTY(EditAnywhere, Config, Category = "Defeated")
	bool bUseSameMapDefeatedTravelFastPath = true;

	UPROPERTY(EditAnywhere, Config, Category = "Defeated", meta = (ClampMin = "0.0"))
	float DefeatedTravelDelaySeconds = 2.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Defeated", meta = (ClampMin = "0.0"))
	float DefeatedCancelMovementRestoreDelaySeconds = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Defeated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefeatedArrivalHealthPct = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Defeated", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DefeatedArrivalCurseMaximum = 60.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Defeated")
	FProjectDefeatSceneDefinition DefaultSceneDefinition;
};
