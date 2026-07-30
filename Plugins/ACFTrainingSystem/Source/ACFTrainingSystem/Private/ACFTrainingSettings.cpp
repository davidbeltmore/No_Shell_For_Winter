#include "ACFTrainingSettings.h"

#include "GameplayEffect.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "ACFTrainingSettings"

namespace
{
	FGameplayTag MakeTrainingTag(const TCHAR* TagName)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(TagName), false);
	}

	TSoftObjectPtr<UAnimationAsset> MakeDefaultIdleAnimation()
	{
		return TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/_Game/Animations/Emotes/Sit/Anim_KA_Idle53_Seiza_Loop1.Anim_KA_Idle53_Seiza_Loop1")));
	}

	FACFTrainingDefinition MakeTrainingDefinition(
		const FName TrainingId,
		const FText& DisplayName,
		const FText& Description,
		const TCHAR* AttributeTagName)
	{
		FACFTrainingDefinition Definition;
		Definition.TrainingId = TrainingId;
		Definition.DisplayName = DisplayName;
		Definition.Description = Description;
		Definition.TargetPrimaryAttribute = MakeTrainingTag(AttributeTagName);
		Definition.SuccessReward = 1.0f;
		Definition.TrainingAnimation = MakeDefaultIdleAnimation();
		Definition.MinigameDifficulty = 50;
		Definition.Requirements = UACFTrainingSettings::MakeDefaultFutureRequirements();
		return Definition;
	}

	FACFTrainingScalarGate MakeMinimumGate(const FName ResourceName, const float MinimumValue)
	{
		FACFTrainingScalarGate Gate;
		Gate.ResourceName = ResourceName;
		Gate.bUseMinimumValue = true;
		Gate.MinimumValue = MinimumValue;
		return Gate;
	}

	FACFTrainingScalarGate MakeMaximumGate(const FName ResourceName, const float MaximumValue)
	{
		FACFTrainingScalarGate Gate;
		Gate.ResourceName = ResourceName;
		Gate.bUseMaximumValue = true;
		Gate.MaximumValue = MaximumValue;
		return Gate;
	}
}

UACFTrainingSettings::UACFTrainingSettings()
	: bUseBuiltInDefinitionsWhenEmpty(true)
	, AttributeModifierGameplayEffect(FSoftClassPath(TEXT("/AscentCombatFramework/GASRuntime/GameplayEffects/ACF_ModifierDefault_GE.ACF_ModifierDefault_GE_C")))
{
	TrainingDefinitions = MakeDefaultTrainingDefinitions();
}

TArray<FACFTrainingDefinition> UACFTrainingSettings::MakeDefaultTrainingDefinitions()
{
	TArray<FACFTrainingDefinition> Defaults;
	Defaults.Reserve(4);

	Defaults.Add(MakeTrainingDefinition(
		TEXT("PushUps"),
		LOCTEXT("PushUpsName", "Push Ups"),
		LOCTEXT("PushUpsDescription", "Strength training placeholder."),
		TEXT("RPG.PrimaryAttributes.Strength")));

	Defaults.Add(MakeTrainingDefinition(
		TEXT("JumpRope"),
		LOCTEXT("JumpRopeName", "Jump Rope"),
		LOCTEXT("JumpRopeDescription", "Endurance training placeholder."),
		TEXT("RPG.PrimaryAttributes.Endurance")));

	Defaults.Add(MakeTrainingDefinition(
		TEXT("ReadBook"),
		LOCTEXT("ReadBookName", "Read Book"),
		LOCTEXT("ReadBookDescription", "Intelligence training placeholder."),
		TEXT("RPG.PrimaryAttributes.Intelligence")));

	Defaults.Add(MakeTrainingDefinition(
		TEXT("Meditate"),
		LOCTEXT("MeditateName", "Meditate"),
		LOCTEXT("MeditateDescription", "Constitution training placeholder."),
		TEXT("RPG.PrimaryAttributes.Constitution")));

	return Defaults;
}

FACFTrainingFutureRequirements UACFTrainingSettings::MakeDefaultFutureRequirements()
{
	FACFTrainingFutureRequirements Requirements;
	Requirements.bEnableScalarRequirements = false;
	Requirements.ScalarRequirements = {
		MakeMinimumGate(TEXT("Hunger"), 50.0f),
		MakeMinimumGate(TEXT("Thirst"), 50.0f),
		MakeMinimumGate(TEXT("Sleep"), 50.0f),
		MakeMaximumGate(TEXT("Curse"), 50.0f),
		MakeMaximumGate(TEXT("Madness"), 50.0f),
		MakeMaximumGate(TEXT("Pain"), 50.0f)
	};
	Requirements.bEnableFailureCooldown = false;
	Requirements.FailureCooldownSeconds = 0.0f;
	Requirements.bEnableInventoryTagRequirements = false;
	Requirements.bEnableNearbyActorRequirements = false;
	Requirements.NearbySearchRadius = 300.0f;
	return Requirements;
}

const TArray<FACFTrainingDefinition>& UACFTrainingSettings::GetTrainingDefinitions() const
{
	if (TrainingDefinitions.Num() == 0 && bUseBuiltInDefinitionsWhenEmpty)
	{
		static const TArray<FACFTrainingDefinition> BuiltInDefinitions = MakeDefaultTrainingDefinitions();
		return BuiltInDefinitions;
	}

	return TrainingDefinitions;
}

FName UACFTrainingSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#undef LOCTEXT_NAMESPACE
