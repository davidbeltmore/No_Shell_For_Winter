#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"

#define LOCTEXT_NAMESPACE "ProjectInnerDoctrineSettings"

namespace
{
	FProjectDoctrineMilestoneDefinition MakeMilestone(
		const TCHAR* AbilityId,
		const EProjectDoctrineAttribute Attribute,
		const int32 RequiredLevel,
		const FText& DisplayName,
		const FText& Description)
	{
		FProjectDoctrineMilestoneDefinition Definition;
		Definition.AbilityId = FName(AbilityId);
		Definition.Attribute = Attribute;
		Definition.RequiredLevel = RequiredLevel;
		Definition.DisplayName = DisplayName;
		Definition.Description = Description;
		return Definition;
	}
}

UProjectInnerDoctrineSettings::UProjectInnerDoctrineSettings()
{
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName MadnessName(TEXT("Madness"));
	const FName PainName(TEXT("Pain"));

	for (const TPair<FName, bool>& Entry : {
		TPair<FName, bool>(HungerName, false),
		TPair<FName, bool>(ThirstName, false),
		TPair<FName, bool>(SleepName, false),
		TPair<FName, bool>(MadnessName, true),
		TPair<FName, bool>(PainName, true) })
	{
		FProjectInnerDoctrineDynamicMaxRule Rule;
		Rule.EntryName = Entry.Key;
		Rule.bIsSensation = Entry.Value;
		Rule.Attribute = EProjectDoctrineAttribute::Willpower;
		Rule.PercentBonusPerLevel = WillpowerDynamicMaxPercentPerLevel;
		DynamicMaximumRules.Add(Rule);
	}

	WillpowerImmunityStatusNames = { TEXT("Fear"), TEXT("Dizzy") };
	MilestoneDefinitions = {
		MakeMilestone(TEXT("SecondBreath"), EProjectDoctrineAttribute::Willpower, 5, LOCTEXT("SecondBreathName", "Second Breath"), LOCTEXT("SecondBreathDesc", "Recover survival needs and reduce Pain and Madness once per floor.")),
		MakeMilestone(TEXT("UnbrokenMind"), EProjectDoctrineAttribute::Willpower, 10, LOCTEXT("UnbrokenMindName", "Unbroken Mind"), LOCTEXT("UnbrokenMindDesc", "Ignore Fear and Dizzy.")),
		MakeMilestone(TEXT("QuietMind"), EProjectDoctrineAttribute::Faith, 5, LOCTEXT("QuietMindName", "Quiet Mind"), LOCTEXT("QuietMindDesc", "Recover Madness over time.")),
		MakeMilestone(TEXT("SanctifiedRest"), EProjectDoctrineAttribute::Faith, 10, LOCTEXT("SanctifiedRestName", "Sanctified Rest"), LOCTEXT("SanctifiedRestDesc", "Sleep restores half of maximum Madness.")),
		MakeMilestone(TEXT("SteadyHands"), EProjectDoctrineAttribute::Cunning, 5, LOCTEXT("SteadyHandsName", "Steady Hands"), LOCTEXT("SteadyHandsDesc", "Improve lockpicking and allow more struggle misses.")),
		MakeMilestone(TEXT("CleanGetaway"), EProjectDoctrineAttribute::Cunning, 10, LOCTEXT("CleanGetawayName", "Clean Getaway"), LOCTEXT("CleanGetawayDesc", "Retain inventory and equipment after defeat.")),
		MakeMilestone(TEXT("PrivateSocialAccess"), EProjectDoctrineAttribute::Charisma, 10, LOCTEXT("PrivateSocialAccessName", "Private Social Access"), LOCTEXT("PrivateSocialAccessDesc", "Unlock optional adult social interactions."))
	};
}

const UProjectInnerDoctrineSettings* UProjectInnerDoctrineSettings::Get()
{
	return GetDefault<UProjectInnerDoctrineSettings>();
}

int32 UProjectInnerDoctrineSettings::ComputeAttributeUpgradeCost(const int32 CurrentLevel)
{
	const float SafeLevel = static_cast<float>(FMath::Max(CurrentLevel, 0) + 1);
	return FMath::Max(1, FMath::RoundToInt(80.f * FMath::Pow(SafeLevel, 1.35f)));
}

float UProjectInnerDoctrineSettings::ComputeDefensiveFlatDamageNegation(const int32 CurrentLevel, const float FlatNegationPerLevel)
{
	return CurrentLevel > 0 && FlatNegationPerLevel > 0.f ? static_cast<float>(CurrentLevel) * FlatNegationPerLevel : 0.f;
}

float UProjectInnerDoctrineSettings::ComputeFaithPassiveBonus(const int32 CurrentLevel, const float BonusPerLevel)
{
	return CurrentLevel > 0 && BonusPerLevel > 0.f ? static_cast<float>(CurrentLevel) * BonusPerLevel : 0.f;
}

float UProjectInnerDoctrineSettings::ComputeFaithMadnessRecovery(const float DeltaSeconds, const float RecoveryPerSecond)
{
	return DeltaSeconds > 0.f && RecoveryPerSecond > 0.f ? DeltaSeconds * RecoveryPerSecond : 0.f;
}

float UProjectInnerDoctrineSettings::ComputeFaithSleepMadnessRestore(const float MadnessMax, const float RestorePct)
{
	return MadnessMax > 0.f && RestorePct > 0.f ? MadnessMax * FMath::Clamp(RestorePct, 0.f, 1.f) : 0.f;
}

float UProjectInnerDoctrineSettings::ComputeCunningPassiveRatio(const int32 CurrentLevel, const float Pivot)
{
	const float SafeLevel = static_cast<float>(FMath::Max(CurrentLevel, 0));
	return SafeLevel > 0.f ? SafeLevel / (SafeLevel + FMath::Max(Pivot, 0.001f)) : 0.f;
}

float UProjectInnerDoctrineSettings::ComputeCunningLockpickSpeedMultiplier(
	const int32 Difficulty,
	const int32 CurrentLevel,
	const float SpeedBaseMultiplier,
	const float MaxSlowPct,
	const float TimePivot)
{
	const float BaseSpeed = 1.f + static_cast<float>(FMath::Clamp(Difficulty, 1, 100)) * 0.025f;
	return FMath::Clamp(
		BaseSpeed * FMath::Max(SpeedBaseMultiplier, 0.01f)
			* (1.f - FMath::Clamp(MaxSlowPct, 0.f, 0.95f) * ComputeCunningPassiveRatio(CurrentLevel, TimePivot)),
		0.85f,
		4.f);
}

float UProjectInnerDoctrineSettings::ComputeCunningLockpickTargetHalfRange(
	const int32 Difficulty,
	const int32 CurrentLevel,
	const float ZonePivot)
{
	const float BaseHalfRange = FMath::Clamp(0.10f - static_cast<float>(FMath::Clamp(Difficulty, 1, 100)) * 0.00045f, 0.035f, 0.18f);
	return FMath::Clamp(BaseHalfRange * (1.f + ComputeCunningPassiveRatio(CurrentLevel, ZonePivot)), 0.035f, 0.18f);
}

float UProjectInnerDoctrineSettings::ComputeCunningStruggleSpeedMultiplier(
	const int32 CurrentLevel,
	const float SpeedBaseMultiplier,
	const float MaxSlowPct,
	const float TimePivot)
{
	return FMath::Clamp(
		FMath::Max(SpeedBaseMultiplier, 0.01f)
			* (1.f - FMath::Clamp(MaxSlowPct, 0.f, 0.95f) * ComputeCunningPassiveRatio(CurrentLevel, TimePivot)),
		0.65f,
		1.30f);
}

float UProjectInnerDoctrineSettings::ComputeCunningScaledStruggleSeconds(
	const float BaseSeconds,
	const float StruggleSpeedMultiplier,
	const float MinSeconds,
	const float MaxSeconds)
{
	const float SafeMin = FMath::Max(MinSeconds, 0.f);
	const float SafeMax = FMath::Max(MaxSeconds, SafeMin);
	return BaseSeconds > 0.f
		? FMath::Clamp(BaseSeconds / FMath::Max(StruggleSpeedMultiplier, 0.001f), SafeMin, SafeMax)
		: SafeMin;
}

int32 UProjectInnerDoctrineSettings::ComputeCunningStruggleMaxMisses(
	const int32 CurrentLevel,
	const int32 DefaultMaxMisses,
	const int32 MilestoneLevel,
	const int32 MilestoneMaxMisses)
{
	const int32 SafeDefault = FMath::Max(DefaultMaxMisses, 1);
	return FMath::Max(CurrentLevel, 0) < FMath::Max(MilestoneLevel, 0)
		? SafeDefault
		: FMath::Max(SafeDefault, MilestoneMaxMisses);
}

void UProjectInnerDoctrineSettings::AccumulateDynamicMaxRule(
	const FProjectInnerDoctrineDynamicMaxRule& Rule,
	const int32 AttributeLevel,
	float& InOutFlatBonus,
	float& InOutPercentMultiplier)
{
	if (AttributeLevel <= 0)
	{
		return;
	}

	float FlatContribution = static_cast<float>(AttributeLevel) * FMath::Max(0.f, Rule.FlatBonusPerLevel);
	if (Rule.bFloorFlatContribution)
	{
		FlatContribution = FMath::FloorToFloat(FlatContribution);
	}
	InOutFlatBonus += FlatContribution;

	const float PercentBonus = static_cast<float>(AttributeLevel) * FMath::Max(0.f, Rule.PercentBonusPerLevel);
	if (PercentBonus > 0.f)
	{
		InOutPercentMultiplier *= 1.f + PercentBonus;
	}
}

float UProjectInnerDoctrineSettings::ComputeDynamicMaxValue(
	const float BaseMax,
	const float FlatBonusTotal,
	const float PercentMultiplier)
{
	return FMath::Max(
		(FMath::Max(BaseMax, 0.001f) + FMath::Max(0.f, FlatBonusTotal)) * FMath::Max(0.f, PercentMultiplier),
		0.001f);
}

FName UProjectInnerDoctrineSettings::GetCategoryName() const
{
	return TEXT("Game");
}

#undef LOCTEXT_NAMESPACE
