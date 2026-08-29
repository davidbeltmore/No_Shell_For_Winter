#include "Intimacy/ProjectIntimacySettings.h"

#include "ContentPolicy/ProjectContentPolicyTypes.h"

UProjectIntimacySettings::UProjectIntimacySettings()
{
	HudToggleKey = EKeys::Hyphen;
	HudSecondaryToggleKey = EKeys::Subtract;
	SocialCardRowsTable = FSoftObjectPath(TEXT("/Game/_Game/Data/Intimacy/DT_ProjectSocialCardRows.DT_ProjectSocialCardRows"));
	HubSocialCompanionClass = FSoftClassPath(
		TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C"));
	CharismaTargetedPartnerClasses = {
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C")),
		FSoftClassPath(TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C"))
	};
}

const UProjectIntimacySettings* UProjectIntimacySettings::Get()
{
	return GetDefault<UProjectIntimacySettings>();
}

bool UProjectIntimacySettings::MeetsAdultInteractionCharismaRequirement(
	const int32 CharismaLevel)
{
	return FProjectContentPolicyRules::IsMatureUnlockedByCharismaLevel(CharismaLevel);
}

float UProjectIntimacySettings::ClampClimax(
	const float Climax,
	const UProjectIntimacySettings* Settings)
{
	const UProjectIntimacySettings* ResolvedSettings = Settings ? Settings : Get();
	const float Maximum = ResolvedSettings
		? FMath::Clamp(ResolvedSettings->ClimaxMaximum, 1.0f, 100.0f)
		: 100.0f;
	return FMath::Clamp(Climax, 0.0f, Maximum);
}

float UProjectIntimacySettings::NormalizeClimaxPercent(
	const float CurrentClimax,
	const float ClimaxMaximum)
{
	return FMath::Clamp(
		(FMath::Max(0.0f, CurrentClimax) / FMath::Max(1.0f, ClimaxMaximum)) * 100.0f,
		0.0f,
		100.0f);
}

float UProjectIntimacySettings::ComputePleaseClimaxGain(
	const int32 SuccessfulHits,
	const UProjectIntimacySettings* Settings)
{
	const UProjectIntimacySettings* ResolvedSettings = Settings ? Settings : Get();
	const float PerHit = ResolvedSettings
		? FMath::Max(0.0f, ResolvedSettings->PleaseClimaxPerSuccessfulHit)
		: 5.0f;
	return PerHit * static_cast<float>(FMath::Max(0, SuccessfulHits));
}

int32 UProjectIntimacySettings::ConsumeClimax(
	const float CurrentClimax,
	const float ClimaxGain,
	const float ClimaxMaximum,
	float& OutRemainingClimax)
{
	const float SafeMaximum = FMath::Max(1.0f, ClimaxMaximum);
	const float TotalClimax = FMath::Max(0.0f, CurrentClimax) + FMath::Max(0.0f, ClimaxGain);
	const int32 OrgasmCount = FMath::FloorToInt(TotalClimax / SafeMaximum);
	OutRemainingClimax = FMath::Fmod(TotalClimax, SafeMaximum);
	return FMath::Max(0, OrgasmCount);
}

float UProjectIntimacySettings::ComputeClimaxAnticipationMultiplier(
	const float CurrentClimax,
	const float ClimaxMaximum,
	const UProjectIntimacySettings* Settings)
{
	const UProjectIntimacySettings* ResolvedSettings = Settings ? Settings : Get();
	const float TargetMultiplier = ResolvedSettings
		? FMath::Max(1.0f, ResolvedSettings->OrgasmRushIntensityMultiplier)
		: 1.25f;
	const float Window = ResolvedSettings
		? FMath::Max(0.0f, ResolvedSettings->ClimaxAnticipationWindow)
		: 5.0f;
	const float SafeMaximum = FMath::Max(1.0f, ClimaxMaximum);
	if (Window <= 0.0f)
	{
		return 1.0f;
	}

	const float Remaining = SafeMaximum - FMath::Clamp(CurrentClimax, 0.0f, SafeMaximum);
	if (Remaining > Window)
	{
		return 1.0f;
	}

	const float Alpha = FMath::Clamp(1.0f - (Remaining / Window), 0.0f, 1.0f);
	return FMath::Lerp(1.0f, TargetMultiplier, Alpha);
}

float UProjectIntimacySettings::ClampSessionProgress(
	const float SessionProgress,
	const UProjectIntimacySettings* Settings)
{
	return ClampClimax(SessionProgress, Settings);
}

float UProjectIntimacySettings::ComputePleaseProgressGain(
	const int32 SuccessfulHits,
	const UProjectIntimacySettings* Settings)
{
	return ComputePleaseClimaxGain(SuccessfulHits, Settings);
}

float UProjectIntimacySettings::ComputePleasePulsePeriod(
	const int32 PartnerLevel,
	const UProjectIntimacySettings* Settings)
{
	const UProjectIntimacySettings* ResolvedSettings = Settings ? Settings : Get();
	const float Base = ResolvedSettings ? ResolvedSettings->PleaseBasePulsePeriod : 1.20f;
	const float LevelPenalty = ResolvedSettings ? ResolvedSettings->PleasePartnerLevelSpeedPenalty : 0.004f;
	const float MinPeriod = ResolvedSettings ? ResolvedSettings->PleaseMinPulsePeriod : 0.55f;
	const float MaxPeriod = ResolvedSettings ? ResolvedSettings->PleaseMaxPulsePeriod : 2.20f;
	const float Period = Base - static_cast<float>(FMath::Clamp(PartnerLevel, 1, 100)) * LevelPenalty;
	return FMath::Clamp(Period, FMath::Max(0.01f, MinPeriod), FMath::Max(MinPeriod, MaxPeriod));
}

float UProjectIntimacySettings::ComputePleaseTargetHalfRange(
	const int32 PartnerLevel,
	const UProjectIntimacySettings* Settings)
{
	const UProjectIntimacySettings* ResolvedSettings = Settings ? Settings : Get();
	const float Base = ResolvedSettings ? ResolvedSettings->PleaseBaseTargetHalfRange : 0.075f;
	const float LevelPenalty = ResolvedSettings ? ResolvedSettings->PleasePartnerLevelRangePenalty : 0.00025f;
	const float MinRange = ResolvedSettings ? ResolvedSettings->PleaseMinTargetHalfRange : 0.045f;
	const float MaxRange = ResolvedSettings ? ResolvedSettings->PleaseMaxTargetHalfRange : 0.22f;
	const float Range = Base - static_cast<float>(FMath::Clamp(PartnerLevel, 1, 100)) * LevelPenalty;
	return FMath::Clamp(Range, FMath::Max(0.001f, MinRange), FMath::Max(MinRange, MaxRange));
}

int32 UProjectIntimacySettings::ConsumeSessionPeak(
	const float CurrentPeak,
	const float ProgressGain,
	const float PeakThreshold,
	float& OutRemainingPeak)
{
	return ConsumeClimax(CurrentPeak, ProgressGain, PeakThreshold, OutRemainingPeak);
}

float UProjectIntimacySettings::ComputeSessionPeakAnticipationMultiplier(
	const float CurrentPeak,
	const float PeakThreshold,
	const UProjectIntimacySettings* Settings)
{
	return ComputeClimaxAnticipationMultiplier(CurrentPeak, PeakThreshold, Settings);
}

EProjectIntimacyEligibilityFailure UProjectIntimacySettings::EvaluateEligibility(
	const FProjectIntimacyEligibilityContext& Context)
{
	if (!Context.bContentAllowed)
	{
		return EProjectIntimacyEligibilityFailure::ContentDisabled;
	}
	if (!Context.bCharismaMasteryUnlocked)
	{
		return EProjectIntimacyEligibilityFailure::CharismaMasteryRequired;
	}
	if (!Context.bPlayerAdultVerified)
	{
		return EProjectIntimacyEligibilityFailure::PlayerNotAdultVerified;
	}
	if (!Context.bPartnerAdultVerified)
	{
		return EProjectIntimacyEligibilityFailure::PartnerNotAdultVerified;
	}
	if (!Context.bExplicitConsent)
	{
		return EProjectIntimacyEligibilityFailure::ConsentMissing;
	}
	if (!Context.bPlayerAlive || !Context.bPartnerAlive)
	{
		return EProjectIntimacyEligibilityFailure::ParticipantDead;
	}
	if (!Context.bPlayerConscious || !Context.bPartnerConscious)
	{
		return EProjectIntimacyEligibilityFailure::ParticipantUnconscious;
	}
	if (!Context.bPartnerNonHostile)
	{
		return EProjectIntimacyEligibilityFailure::PartnerHostile;
	}
	if (!Context.bOutsideCombat)
	{
		return EProjectIntimacyEligibilityFailure::InCombat;
	}
	if (!Context.bZoneAllowed)
	{
		return EProjectIntimacyEligibilityFailure::ZoneNotAllowed;
	}
	return EProjectIntimacyEligibilityFailure::None;
}

FText UProjectIntimacySettings::GetEligibilityFailureText(
	const EProjectIntimacyEligibilityFailure Failure)
{
	switch (Failure)
	{
	case EProjectIntimacyEligibilityFailure::ContentDisabled:
		return FText::FromString(TEXT("Optional mature content is disabled."));
	case EProjectIntimacyEligibilityFailure::CharismaMasteryRequired:
		return FText::FromString(TEXT("Charisma level 10 is required."));
	case EProjectIntimacyEligibilityFailure::PlayerNotAdultVerified:
	case EProjectIntimacyEligibilityFailure::PartnerNotAdultVerified:
		return FText::FromString(TEXT("Every participant must be verified as an adult."));
	case EProjectIntimacyEligibilityFailure::ConsentMissing:
		return FText::FromString(TEXT("Explicit consent is required."));
	case EProjectIntimacyEligibilityFailure::ParticipantDead:
		return FText::FromString(TEXT("Every participant must be alive."));
	case EProjectIntimacyEligibilityFailure::ParticipantUnconscious:
		return FText::FromString(TEXT("Every participant must be conscious."));
	case EProjectIntimacyEligibilityFailure::PartnerHostile:
		return FText::FromString(TEXT("The selected character must be non-hostile."));
	case EProjectIntimacyEligibilityFailure::InCombat:
		return FText::FromString(TEXT("Intimacy is unavailable during combat."));
	case EProjectIntimacyEligibilityFailure::ZoneNotAllowed:
		return FText::FromString(TEXT("Intimacy is unavailable in this area."));
	case EProjectIntimacyEligibilityFailure::None:
	default:
		return FText();
	}
}

FName UProjectIntimacySettings::GetCategoryName() const
{
	return TEXT("Project");
}
