#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "GameplayTagContainer.h"
#include "Intimacy/ProjectIntimacyTypes.h"
#include "UObject/SoftObjectPath.h"
#include "ProjectIntimacySettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Intimacy"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectIntimacySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectIntimacySettings();

	static const UProjectIntimacySettings* Get();
	static bool MeetsAdultInteractionCharismaRequirement(int32 CharismaLevel);
	static float ClampClimax(float Climax, const UProjectIntimacySettings* Settings = nullptr);
	static float NormalizeClimaxPercent(float CurrentClimax, float ClimaxMaximum);
	static float ComputePleaseClimaxGain(int32 SuccessfulHits, const UProjectIntimacySettings* Settings = nullptr);
	static int32 ConsumeClimax(float CurrentClimax, float ClimaxGain, float ClimaxMaximum, float& OutRemainingClimax);
	static float ComputeClimaxAnticipationMultiplier(float CurrentClimax, float ClimaxMaximum, const UProjectIntimacySettings* Settings = nullptr);

	/** Legacy compatibility wrappers. New gameplay should use the Climax APIs above. */
	static float ClampSessionProgress(float SessionProgress, const UProjectIntimacySettings* Settings = nullptr);
	static float ComputePleaseProgressGain(int32 SuccessfulHits, const UProjectIntimacySettings* Settings = nullptr);
	static float ComputePleasePulsePeriod(int32 PartnerLevel, const UProjectIntimacySettings* Settings = nullptr);
	static float ComputePleaseTargetHalfRange(int32 PartnerLevel, const UProjectIntimacySettings* Settings = nullptr);
	static int32 ConsumeSessionPeak(float CurrentPeak, float ProgressGain, float PeakThreshold, float& OutRemainingPeak);
	static float ComputeSessionPeakAnticipationMultiplier(float CurrentPeak, float PeakThreshold, const UProjectIntimacySettings* Settings = nullptr);
	static EProjectIntimacyEligibilityFailure EvaluateEligibility(const FProjectIntimacyEligibilityContext& Context);
	static FText GetEligibilityFailureText(EProjectIntimacyEligibilityFailure Failure);

	virtual FName GetCategoryName() const override;

	/** Repeating, session-local meter maximum for both participants. */
	UPROPERTY(EditAnywhere, Config, Category = "Climax", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float ClimaxMaximum = 100.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Climax", meta = (ClampMin = "0.0"))
	float PassivePlayerClimaxPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Climax", meta = (ClampMin = "0.0"))
	float PassivePartnerClimaxPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Climax", meta = (ClampMin = "0.0"))
	float ClimaxAnticipationWindow = 5.0f;

	/** One session-local state shared by the most recent orgasm participant. */
	UPROPERTY(EditAnywhere, Config, Category = "Climax|Orgasm Rush", meta = (ClampMin = "0.0"))
	float OrgasmRushDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Climax|Orgasm Rush", meta = (ClampMin = "1.0"))
	float OrgasmRushIntensityMultiplier = 1.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Climax|Orgasm Rush")
	FName OrgasmMediaEventId = TEXT("Climax");

	/** Percentage points of the player's Curse maximum removed per second. */
	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float CurseReductionPercentPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Curse", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float CurseUpdateIntervalSeconds = 0.10f;

	UPROPERTY(Config)
	float SessionProgressMaximum = 100.0f;

	UPROPERTY(Config)
	float PassiveSessionProgressPerSecond = 1.0f;

	UPROPERTY(Config)
	float SessionPeakThreshold = 25.0f;

	UPROPERTY(Config)
	float SessionPeakAnticipationWindow = 5.0f;

	UPROPERTY(Config)
	float SessionPeakIntensityMultiplier = 1.25f;

	UPROPERTY(Config)
	float SessionPeakRecoverySeconds = 2.0f;

	UPROPERTY(Config)
	FName SessionPeakMediaEventId = TEXT("SessionPeak");

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "1"))
	int32 PleaseAttemptCount = 5;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.01"))
	float PleaseBasePulsePeriod = 1.20f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.0"))
	float PleasePartnerLevelSpeedPenalty = 0.004f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.01"))
	float PleaseMinPulsePeriod = 0.55f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.01"))
	float PleaseMaxPulsePeriod = 2.20f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.001"))
	float PleaseBaseTargetHalfRange = 0.075f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.0"))
	float PleasePartnerLevelRangePenalty = 0.00025f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.001"))
	float PleaseMinTargetHalfRange = 0.045f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.001"))
	float PleaseMaxTargetHalfRange = 0.22f;

	UPROPERTY(EditAnywhere, Config, Category = "Talk", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TalkRefusalChance = 0.10f;

	UPROPERTY(EditAnywhere, Config, Category = "Please", meta = (ClampMin = "0.0"))
	float PleaseClimaxPerSuccessfulHit = 5.0f;

	UPROPERTY(Config)
	float PleaseProgressPerSuccessfulHit = 5.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Talk", meta = (ClampMin = "0.0"))
	float TalkCooldownSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Talk", meta = (ClampMin = "0.0"))
	float CorrectTalkClimaxBonus = 5.0f;

	UPROPERTY(Config)
	float CorrectTalkProgressBonus = 5.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Relationship", meta = (ClampMin = "1"))
	int32 AffectMax = 100;

	UPROPERTY(Config)
	int32 SatisfiedWinAffectGain = 10;

	UPROPERTY(EditAnywhere, Config, Category = "Animation", meta = (ClampMin = "0.05"))
	float ChillAnimationRate = 0.75f;

	UPROPERTY(EditAnywhere, Config, Category = "Animation", meta = (ClampMin = "0.05"))
	float NiceAnimationRate = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Animation", meta = (ClampMin = "0.05"))
	float StallionAnimationRate = 1.35f;

	UPROPERTY(EditAnywhere, Config, Category = "Input")
	FKey HudToggleKey;

	UPROPERTY(EditAnywhere, Config, Category = "Input")
	FKey HudSecondaryToggleKey;

	UPROPERTY(EditAnywhere, Config, Category = "UI")
	FSoftClassPath IntimacyHudWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (ClampMin = "0"))
	int32 IntimacyHudZOrder = 325;

	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float HudRefreshIntervalSeconds = 0.10f;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath TalkOptionsTable;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath PartnerResponsesTable;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath MediaCuesTable;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath PersonalitiesTable;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath TalkAffinityTable;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath ItemEffectsTable;

	UPROPERTY(EditAnywhere, Config, Category = "DataTables")
	FSoftObjectPath SocialCardRowsTable;

	UPROPERTY(EditAnywhere, Config, Category = "Save")
	FString SaveSlotName = TEXT("ProjectIntimacy");

	UPROPERTY(EditAnywhere, Config, Category = "Save", meta = (ClampMin = "0"))
	int32 SaveUserIndex = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility")
	bool bPlayerCharacterAdultVerified = true;

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility", meta = (ClampMin = "0.0"))
	float CombatLockoutSeconds = 8.0f;

	/**
	 * Charisma mastery makes a selected allowlisted adult companion a contextual,
	 * session-scoped partner without requiring map-authored social/zone metadata.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Eligibility")
	bool bAllowCharismaMasteryTargetedPartners = true;

	/**
	 * Explicit project-owned companion and enemy classes eligible for the
	 * Charisma-10 targeted adapter. Charisma may override hostility for these
	 * classes, while adult, life, consciousness, consent, and live combat gates
	 * remain authoritative.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Eligibility")
	TArray<FSoftClassPath> CharismaTargetedPartnerClasses;

	/** Project-owned companion used by the explicit HUB social route. */
	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB")
	FSoftClassPath HubSocialCompanionClass;

	/**
	 * Disabled by default: social partners must be deliberately placed/authored
	 * instead of appearing beside the player whenever HUB starts.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB")
	bool bAutoSpawnHubSocialCompanion = false;

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB")
	FName HubSocialMapName = TEXT("HUB");

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB")
	FVector HubSocialCompanionOffset = FVector(240.0f, 140.0f, 0.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB", meta = (ClampMin = "100.0"))
	float HubSocialZoneRadius = 650.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB", meta = (ClampMin = "-100", ClampMax = "100"))
	int32 HubSocialCompanionAffinity = 50;

	UPROPERTY(EditAnywhere, Config, Category = "Eligibility|HUB", meta = (ClampMin = "-100", ClampMax = "100"))
	int32 MinimumAffinityForIntimacyConsent = 25;
};
