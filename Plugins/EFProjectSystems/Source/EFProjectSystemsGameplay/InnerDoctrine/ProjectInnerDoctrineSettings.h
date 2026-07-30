#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineSettings.generated.h"

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerDoctrineDynamicMaxRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Maximums")
	FName EntryName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Maximums")
	bool bIsSensation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Maximums")
	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Maximums", meta = (ClampMin = "0.0"))
	float FlatBonusPerLevel = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Maximums", meta = (ClampMin = "0.0"))
	float PercentBonusPerLevel = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Maximums")
	bool bFloorFlatContribution = false;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Inner Doctrine"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineSettings();

	static const UProjectInnerDoctrineSettings* Get();
	static int32 ComputeAttributeUpgradeCost(int32 CurrentLevel);
	static float ComputeDefensiveFlatDamageNegation(int32 CurrentLevel, float FlatNegationPerLevel);
	static float ComputeFaithPassiveBonus(int32 CurrentLevel, float BonusPerLevel);
	static float ComputeFaithMadnessRecovery(float DeltaSeconds, float RecoveryPerSecond);
	static float ComputeFaithSleepMadnessRestore(float MadnessMax, float RestorePct);
	static float ComputeCunningPassiveRatio(int32 CurrentLevel, float Pivot);
	static float ComputeCunningLockpickSpeedMultiplier(int32 Difficulty, int32 CurrentLevel, float SpeedBaseMultiplier, float MaxSlowPct, float TimePivot);
	static float ComputeCunningLockpickTargetHalfRange(int32 Difficulty, int32 CurrentLevel, float ZonePivot);
	static float ComputeCunningStruggleSpeedMultiplier(int32 CurrentLevel, float SpeedBaseMultiplier, float MaxSlowPct, float TimePivot);
	static float ComputeCunningScaledStruggleSeconds(float BaseSeconds, float StruggleSpeedMultiplier, float MinSeconds, float MaxSeconds);
	static int32 ComputeCunningStruggleMaxMisses(int32 CurrentLevel, int32 DefaultMaxMisses, int32 MilestoneLevel, int32 MilestoneMaxMisses);
	static void AccumulateDynamicMaxRule(const FProjectInnerDoctrineDynamicMaxRule& Rule, int32 AttributeLevel, float& InOutFlatBonus, float& InOutPercentMultiplier);
	static float ComputeDynamicMaxValue(float BaseMax, float FlatBonusTotal, float PercentMultiplier);

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "DXP", meta = (ClampMin = "1"))
	int32 MaxDoctrineAttributeLevel = 100;

	UPROPERTY(EditAnywhere, Config, Category = "DXP", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DeathRunLossRatio = 0.90f;

	UPROPERTY(EditAnywhere, Config, Category = "DXP")
	FString SaveSlotName = TEXT("ProjectInnerDoctrineV1");

	UPROPERTY(EditAnywhere, Config, Category = "DXP", meta = (ClampMin = "0"))
	int32 SaveUserIndex = 0;

	UPROPERTY(EditAnywhere, Config, Category = "DXP", meta = (ClampMin = "0"))
	int32 ExchangeMenuZOrder = 285;

	UPROPERTY(EditAnywhere, Config, Category = "DXP", meta = (ClampMin = "0"))
	int32 CombatHitDxp = 3;

	UPROPERTY(EditAnywhere, Config, Category = "DXP", meta = (ClampMin = "0"))
	int32 EnemyKillDxp = 15;

	UPROPERTY(EditAnywhere, Config, Category = "Sensations", meta = (ClampMin = "1.0"))
	float MadnessMax = 100.f;

	UPROPERTY(EditAnywhere, Config, Category = "Sensations", meta = (ClampMin = "1.0"))
	float PainMax = 100.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0"))
	float CurseWarningThreshold = 50.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0"))
	float CurseCriticalThreshold = 75.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0"))
	float CurseDecayDelaySeconds = 10.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0"))
	float CurseDecayPerSecond = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CurseMinimumResistibleMultiplier = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "1"))
	int32 ProcessedCurseApplicationHistory = 256;

	UPROPERTY(EditAnywhere, Config, Category = "Curse|Cursed", meta = (ClampMin = "0.1"))
	float CursedDurationSeconds = 8.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse|Cursed", meta = (ClampMin = "0.0"))
	float CursedResidualCurse = 60.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse|Cursed", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CursedMovementMultiplier = 0.80f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse|Recovery", meta = (ClampMin = "0.0"))
	float CursedRecoveryDurationSeconds = 8.f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse|Recovery", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CursedRecoveryMovementMultiplier = 0.85f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0"))
	float DefeatCurseClamp = 60.f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat", meta = (ClampMin = "0.1"))
	float CombatStateExitSeconds = 8.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes", meta = (ClampMin = "0.0"))
	float OffensiveDamagePerLevel = 2.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes", meta = (ClampMin = "0.0"))
	float FaithSpellDamagePerLevel = 2.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes", meta = (ClampMin = "0.0"))
	float FaithSpellDefensePerLevel = 1.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Defensive", meta = (ClampMin = "0.0"))
	float DefensiveFlatDamageNegationPerLevel = 4.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Willpower", meta = (ClampMin = "0.0"))
	float WillpowerDynamicMaxPercentPerLevel = 0.015f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Willpower", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WillpowerCurseResistancePerLevel = 0.01f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Willpower", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WillpowerCurseResistanceCap = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Willpower", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WillpowerSecondBreathRecoveryPct = 0.20f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Willpower", meta = (ClampMin = "0.0"))
	float WillpowerSecondBreathSensationReduction = 20.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Willpower", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WillpowerCursedDurationReduction = 0.30f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Offensive", meta = (ClampMin = "0.0"))
	float OffensiveHexPressureDamageBonusPct = 0.15f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Offensive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OffensiveExecuteHealthThresholdPct = 0.20f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Offensive", meta = (ClampMin = "0.0"))
	float OffensiveCleanFinishCurseReduction = 10.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Offensive", meta = (ClampMin = "0.0"))
	float OffensiveCleanFinishDamageBonusPct = 0.15f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Offensive", meta = (ClampMin = "0.0"))
	float OffensiveCleanFinishBuffSeconds = 6.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Offensive", meta = (ClampMin = "0.0"))
	float OffensiveCleanFinishCooldownSeconds = 10.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GuardRecoveryPoolPct = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PainSpikePoolPct = 0.40f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.1"))
	float GuardRecoveryDurationSeconds = 3.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float GuardRecoveryEndPain = 50.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float GuardRecoveryEndCurse = 70.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float PainSpikeEndPain = 0.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float PainSpikeEndCurse = 60.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float GuardRecoveryRecoverySeconds = 8.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float PainSpikeRecoverySeconds = 5.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float PainSpikeRadius = 350.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float PainSpikePoiseDamage = 25.f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive", meta = (ClampMin = "0.0"))
	float GuardRecoveryFeedbackCooldownSeconds = 0.08f;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones|Defensive")
	bool bGuardRecoverySuppressDamageText = true;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Faith", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FaithCurseResistancePerLevel = 0.01f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Faith", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FaithCurseResistanceCap = 0.30f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Faith", meta = (ClampMin = "0.0"))
	float FaithMadnessRecoveryPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Faith", meta = (ClampMin = "0.0"))
	float FaithCurseDecayBonusPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Faith", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FaithSleepMadnessRestorePct = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Faith", meta = (ClampMin = "0.0"))
	float FaithSanctifiedRestCurseReduction = 25.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.01"))
	float CunningLockpickSpeedBaseMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float CunningLockpickMaxSlowPct = 0.45f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.01"))
	float CunningLockpickTimePivot = 10.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.01"))
	float CunningLockpickZonePivot = 5.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.01"))
	float CunningStruggleSpeedBaseMultiplier = 1.30f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float CunningStruggleMaxSlowPct = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.01"))
	float CunningStruggleTimePivot = 10.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "1"))
	int32 CunningStruggleMilestoneMaxMisses = 10;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CunningCurseResistancePerLevel = 0.01f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Cunning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CunningCurseResistanceCap = 0.40f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Celerity", meta = (ClampMin = "0.0"))
	float CelerityMovementBonusPerLevel = 0.005f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Celerity", meta = (ClampMin = "0.0"))
	float CelerityMovementBonusCap = 0.15f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Celerity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CelerityStatusPenaltyMitigation = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Celerity", meta = (ClampMin = "0.0"))
	float CelerityRecoveredMomentumBonusPct = 0.10f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Celerity", meta = (ClampMin = "0.0"))
	float CelerityRecoveredMomentumSeconds = 5.f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Charisma", meta = (ClampMin = "0.0"))
	float CharismaDxpBonusPerLevel = 0.01f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Charisma", meta = (ClampMin = "0.0"))
	float CharismaDxpBonusCap = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Charisma", meta = (ClampMin = "0.0"))
	float CharismaRapportCleanseBonusPct = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Charisma", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CharismaCompanionCurseResistance = 0.15f;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes|Charisma", meta = (ClampMin = "0.0"))
	float CharismaCompanionRadius = 1200.f;

	UPROPERTY(EditAnywhere, Config, Category = "Dynamic Maximums")
	TArray<FProjectInnerDoctrineDynamicMaxRule> DynamicMaximumRules;

	UPROPERTY(EditAnywhere, Config, Category = "Milestones")
	TArray<FProjectDoctrineMilestoneDefinition> MilestoneDefinitions;

	UPROPERTY(EditAnywhere, Config, Category = "Status")
	TArray<FName> WillpowerImmunityStatusNames;
};
