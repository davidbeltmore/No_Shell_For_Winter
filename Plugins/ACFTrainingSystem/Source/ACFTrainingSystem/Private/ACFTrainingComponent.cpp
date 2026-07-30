#include "ACFTrainingComponent.h"

#include "ACFGASStatisticsComponent.h"
#include "ACFTrainingMinigameBase.h"
#include "ACFTrainingSettings.h"
#include "ARSStatisticsComponent.h"
#include "ARSTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "ACFTrainingComponent"

namespace
{
	bool IsStatisticsTag(const FGameplayTag& Tag)
	{
		return Tag.IsValid() && Tag.ToString().StartsWith(TEXT("RPG.Statistics."));
	}

	TSubclassOf<UGameplayEffect> ResolveAttributeModifierGameplayEffect()
	{
		if (const UACFTrainingSettings* Settings = GetDefault<UACFTrainingSettings>())
		{
			if (!Settings->AttributeModifierGameplayEffect.IsNull())
			{
				if (UClass* LoadedClass = Settings->AttributeModifierGameplayEffect.LoadSynchronous())
				{
					return LoadedClass;
				}
			}
		}

		static constexpr const TCHAR* FallbackGameplayEffectPaths[] = {
			TEXT("/AscentCombatFramework/GASRuntime/GameplayEffects/ACF_ModifierDefault_GE.ACF_ModifierDefault_GE_C"),
			TEXT("/AscentCombatFramework/GASRuntime/GameplayEffects/ACF_DefaultMod_GE.ACF_DefaultMod_GE_C")
		};

		for (const TCHAR* FallbackPath : FallbackGameplayEffectPaths)
		{
			if (UClass* LoadedClass = StaticLoadClass(UGameplayEffect::StaticClass(), nullptr, FallbackPath))
			{
				return LoadedClass;
			}
		}

		return nullptr;
	}
}

UACFTrainingComponent::UACFTrainingComponent()
	: ActiveTrainingId(NAME_None)
	, ActiveTrainingStartTime(0.0f)
	, ActiveTimingSessionId(INDEX_NONE)
	, ActiveTimingServerStartTimeSeconds(0.0f)
	, ActiveTimingSpeedMultiplier(1.0f)
	, ActiveTimingTargetHalfRange(0.1f)
	, ActiveTimingTargetCenter(0.5f)
	, ActiveTimingDifficulty(1)
	, ActiveTimingCunningLevel(0)
	, LastIssuedTimingSessionId(0)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UACFTrainingComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ReapplyTrainingRewardModifier();
	}
}

void UACFTrainingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UACFTrainingComponent, ActiveTrainingId);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTrainingStartTime);
	DOREPLIFETIME(UACFTrainingComponent, TrainingProgress);
	DOREPLIFETIME(UACFTrainingComponent, AccumulatedAttributeRewards);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingSessionId);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingServerStartTimeSeconds);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingSpeedMultiplier);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingTargetHalfRange);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingTargetCenter);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingDifficulty);
	DOREPLIFETIME(UACFTrainingComponent, ActiveTimingCunningLevel);
}

bool UACFTrainingComponent::StartTrainingById(FName TrainingId)
{
	if (!GetOwner())
	{
		return false;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerStartTrainingById(TrainingId);
		return true;
	}

	return StartTrainingAuthority(TrainingId);
}

void UACFTrainingComponent::CancelTraining()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerCancelTraining();
		return;
	}

	CancelTrainingAuthority();
}

void UACFTrainingComponent::CompleteTrainingMinigame(bool bSuccess)
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerCompleteTrainingMinigame(bSuccess);
		return;
	}

	CompleteTrainingAuthority(bSuccess);
}

bool UACFTrainingComponent::StartTimingMinigameSession(int32 Difficulty, int32 CunningLevel, float SpeedMultiplier, float TargetHalfRange, float TargetCenter)
{
	if (!GetOwner())
	{
		return false;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerStartTimingMinigameSession(Difficulty, CunningLevel, SpeedMultiplier, TargetHalfRange, TargetCenter);
		return true;
	}

	return StartTimingMinigameSessionAuthority(Difficulty, CunningLevel, SpeedMultiplier, TargetHalfRange, TargetCenter);
}

bool UACFTrainingComponent::ConfirmTimingMinigame()
{
	if (!GetOwner())
	{
		return false;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerConfirmTimingMinigame();
		return true;
	}

	return ConfirmTimingMinigameAuthority();
}

bool UACFTrainingComponent::CanStartTraining(FName TrainingId, FText& OutReason) const
{
	if (TrainingId.IsNone())
	{
		OutReason = LOCTEXT("InvalidTrainingId", "Invalid training id.");
		return false;
	}

	if (IsTrainingActive())
	{
		OutReason = LOCTEXT("TrainingAlreadyActive", "A training session is already active.");
		return false;
	}

	const FACFTrainingDefinition* Definition = FindTrainingDefinition(TrainingId);
	if (!Definition)
	{
		OutReason = FText::Format(LOCTEXT("TrainingNotFound", "Training '{0}' is not defined."), FText::FromName(TrainingId));
		return false;
	}

	if (!Definition->TargetPrimaryAttribute.IsValid())
	{
		OutReason = FText::Format(LOCTEXT("TrainingMissingAttribute", "Training '{0}' has no valid ACF primary attribute tag."), FText::FromName(TrainingId));
		return false;
	}

	if (Definition->SuccessReward <= 0.0f)
	{
		OutReason = FText::Format(LOCTEXT("TrainingMissingReward", "Training '{0}' must grant a positive reward."), FText::FromName(TrainingId));
		return false;
	}

	if (!FindStatisticsComponent())
	{
		OutReason = LOCTEXT("MissingStatisticsComponent", "No ACF/ARS statistics component was found on the owner.");
		return false;
	}

	const FACFTrainingFutureRequirements& Requirements = Definition->Requirements;
	if (Requirements.bEnableFailureCooldown && Requirements.FailureCooldownSeconds > 0.0f)
	{
		const float* LastFailureTime = LastFailureTimeByTrainingId.Find(TrainingId);
		const UWorld* World = GetWorld();
		if (LastFailureTime && World)
		{
			const float RemainingSeconds = Requirements.FailureCooldownSeconds - (World->GetTimeSeconds() - *LastFailureTime);
			if (RemainingSeconds > 0.0f)
			{
				OutReason = FText::Format(LOCTEXT("FailureCooldownActive", "Training is cooling down for {0} seconds."), FText::AsNumber(FMath::CeilToInt(RemainingSeconds)));
				return false;
			}
		}
	}

	if (!ValidateScalarRequirements(*Definition, OutReason))
	{
		return false;
	}

	if (!CheckInventoryRequirements(*Definition, OutReason))
	{
		return false;
	}

	if (!CheckNearbyActorRequirements(*Definition, OutReason))
	{
		return false;
	}

	OutReason = FText::GetEmpty();
	return true;
}

bool UACFTrainingComponent::GetTrainingProgress(FName TrainingId, float& OutProgress) const
{
	if (const float* Progress = FindTrainingProgressValue(TrainingId))
	{
		OutProgress = *Progress;
		return true;
	}

	OutProgress = 0.0f;
	return FindTrainingDefinition(TrainingId) != nullptr;
}

bool UACFTrainingComponent::GetTrainingDefinition(FName TrainingId, FACFTrainingDefinition& OutDefinition) const
{
	if (const FACFTrainingDefinition* Definition = FindTrainingDefinition(TrainingId))
	{
		OutDefinition = *Definition;
		return true;
	}

	OutDefinition = FACFTrainingDefinition();
	return false;
}

TArray<FACFTrainingDefinition> UACFTrainingComponent::GetAvailableTrainingDefinitions() const
{
	return ResolveTrainingDefinitions();
}

bool UACFTrainingComponent::IsTrainingActive() const
{
	return !ActiveTrainingId.IsNone();
}

FName UACFTrainingComponent::GetActiveTrainingId() const
{
	return ActiveTrainingId;
}

bool UACFTrainingComponent::GetActiveTrainingDefinition(FACFTrainingDefinition& OutDefinition) const
{
	return GetTrainingDefinition(ActiveTrainingId, OutDefinition);
}

float UACFTrainingComponent::GetAccumulatedRewardForAttribute(FGameplayTag TargetAttribute) const
{
	if (const float* Reward = FindAttributeRewardValue(TargetAttribute))
	{
		return *Reward;
	}

	return 0.0f;
}

bool UACFTrainingComponent::IsTimingMinigameSessionActive() const
{
	return IsTrainingActive() && ActiveTimingSessionId != INDEX_NONE;
}

int32 UACFTrainingComponent::GetActiveTimingSessionId() const
{
	return ActiveTimingSessionId;
}

float UACFTrainingComponent::GetActiveTimingSpeedMultiplier() const
{
	return ActiveTimingSpeedMultiplier;
}

float UACFTrainingComponent::GetActiveTimingTargetHalfRange() const
{
	return ActiveTimingTargetHalfRange;
}

float UACFTrainingComponent::GetActiveTimingTargetCenter() const
{
	return ActiveTimingTargetCenter;
}

float UACFTrainingComponent::GetCurrentTimingPulseValue() const
{
	if (!IsTimingMinigameSessionActive())
	{
		return 0.0f;
	}

	return ComputeTimingPulseValue(
		ResolveServerWorldTimeSeconds() - ActiveTimingServerStartTimeSeconds,
		ActiveTimingSpeedMultiplier);
}

bool UACFTrainingComponent::IsCurrentTimingPulseInTargetRange() const
{
	if (!IsTimingMinigameSessionActive())
	{
		return false;
	}

	const float PulseValue = GetCurrentTimingPulseValue();
	return FMath::Abs(PulseValue - ActiveTimingTargetCenter) <= ActiveTimingTargetHalfRange;
}

UACFTrainingMinigameBase* UACFTrainingComponent::CreateMinigameForActiveTraining()
{
	FACFTrainingDefinition Definition;
	if (!GetActiveTrainingDefinition(Definition) || !Definition.MinigameClass)
	{
		return nullptr;
	}

	UACFTrainingMinigameBase* Minigame = NewObject<UACFTrainingMinigameBase>(this, Definition.MinigameClass);
	if (Minigame)
	{
		Minigame->InitializeMinigame(this, Definition);
	}
	return Minigame;
}

bool UACFTrainingComponent::ResolveScalarGateValue_Implementation(const FACFTrainingScalarGate& Gate, float& OutValue) const
{
	OutValue = 0.0f;

	if (!Gate.ResourceTag.IsValid())
	{
		return false;
	}

	UARSStatisticsComponent* StatisticsComponent = FindStatisticsComponent();
	if (!StatisticsComponent)
	{
		return false;
	}

	OutValue = IsStatisticsTag(Gate.ResourceTag)
		? StatisticsComponent->GetCurrentValueForStatitstic(Gate.ResourceTag)
		: StatisticsComponent->GetCurrentAttributeValue(Gate.ResourceTag);

	return OutValue >= 0.0f;
}

bool UACFTrainingComponent::CheckInventoryRequirements_Implementation(const FACFTrainingDefinition& Definition, FText& OutReason) const
{
	if (!Definition.Requirements.bEnableInventoryTagRequirements || Definition.Requirements.RequiredInventoryTags.Num() == 0)
	{
		return true;
	}

	OutReason = LOCTEXT("InventoryRequirementsNeedAdapter", "Inventory requirements need a project-specific adapter.");
	return false;
}

bool UACFTrainingComponent::CheckNearbyActorRequirements_Implementation(const FACFTrainingDefinition& Definition, FText& OutReason) const
{
	if (!Definition.Requirements.bEnableNearbyActorRequirements || Definition.Requirements.RequiredNearbyActorTags.Num() == 0)
	{
		return true;
	}

	OutReason = LOCTEXT("NearbyRequirementsNeedAdapter", "Nearby actor requirements need a project-specific adapter.");
	return false;
}

void UACFTrainingComponent::OnRep_ActiveTrainingId()
{
	if (!ActiveTrainingId.IsNone())
	{
		if (const FACFTrainingDefinition* Definition = FindTrainingDefinition(ActiveTrainingId))
		{
			OnTrainingStarted.Broadcast(ActiveTrainingId, Definition->TargetPrimaryAttribute);
		}
	}
}

void UACFTrainingComponent::ServerStartTrainingById_Implementation(FName TrainingId)
{
	StartTrainingAuthority(TrainingId);
}

void UACFTrainingComponent::ServerCancelTraining_Implementation()
{
	CancelTrainingAuthority();
}

void UACFTrainingComponent::ServerCompleteTrainingMinigame_Implementation(bool bSuccess)
{
	CompleteTrainingAuthority(bSuccess);
}

void UACFTrainingComponent::ServerStartTimingMinigameSession_Implementation(int32 Difficulty, int32 CunningLevel, float SpeedMultiplier, float TargetHalfRange, float TargetCenter)
{
	StartTimingMinigameSessionAuthority(Difficulty, CunningLevel, SpeedMultiplier, TargetHalfRange, TargetCenter);
}

void UACFTrainingComponent::ServerConfirmTimingMinigame_Implementation()
{
	ConfirmTimingMinigameAuthority();
}

bool UACFTrainingComponent::StartTrainingAuthority(FName TrainingId)
{
	FText Reason;
	if (!CanStartTraining(TrainingId, Reason))
	{
		OnTrainingRejected.Broadcast(TrainingId, Reason);
		return false;
	}

	const FACFTrainingDefinition* Definition = FindTrainingDefinition(TrainingId);
	if (!Definition)
	{
		OnTrainingRejected.Broadcast(TrainingId, LOCTEXT("TrainingLostAfterValidation", "Training definition was not found after validation."));
		return false;
	}

	ActiveTrainingId = TrainingId;
	ActiveTrainingStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	OnTrainingStarted.Broadcast(ActiveTrainingId, Definition->TargetPrimaryAttribute);
	return true;
}

void UACFTrainingComponent::CancelTrainingAuthority()
{
	if (ActiveTrainingId.IsNone())
	{
		return;
	}

	FACFTrainingDefinition Definition;
	GetActiveTrainingDefinition(Definition);
	const FName CompletedTrainingId = ActiveTrainingId;

	ActiveTrainingId = NAME_None;
	ActiveTrainingStartTime = 0.0f;
	ClearTimingMinigameSession();

	OnTrainingCompleted.Broadcast(CompletedTrainingId, EACFTrainingSessionResult::Cancelled, Definition.TargetPrimaryAttribute, 0.0f);
}

void UACFTrainingComponent::CompleteTrainingAuthority(bool bSuccess)
{
	if (ActiveTrainingId.IsNone())
	{
		return;
	}

	FACFTrainingDefinition Definition;
	if (!GetActiveTrainingDefinition(Definition))
	{
		ActiveTrainingId = NAME_None;
		ActiveTrainingStartTime = 0.0f;
		ClearTimingMinigameSession();
		return;
	}

	const FName CompletedTrainingId = ActiveTrainingId;
	float GrantedReward = 0.0f;

	if (bSuccess)
	{
		GrantedReward = GrantTrainingReward(Definition) ? Definition.SuccessReward : 0.0f;
	}
	else
	{
		LastFailureTimeByTrainingId.FindOrAdd(CompletedTrainingId) = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	}

	ActiveTrainingId = NAME_None;
	ActiveTrainingStartTime = 0.0f;
	ClearTimingMinigameSession();

	OnTrainingCompleted.Broadcast(
		CompletedTrainingId,
		bSuccess ? EACFTrainingSessionResult::Succeeded : EACFTrainingSessionResult::Failed,
		Definition.TargetPrimaryAttribute,
		GrantedReward);
}

bool UACFTrainingComponent::StartTimingMinigameSessionAuthority(int32 Difficulty, int32 CunningLevel, float SpeedMultiplier, float TargetHalfRange, float TargetCenter)
{
	if (!IsTrainingActive())
	{
		return false;
	}

	LastIssuedTimingSessionId = LastIssuedTimingSessionId == MAX_int32 ? 1 : LastIssuedTimingSessionId + 1;
	ActiveTimingSessionId = LastIssuedTimingSessionId;
	ActiveTimingServerStartTimeSeconds = ResolveServerWorldTimeSeconds();
	ActiveTimingSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.01f, 10.0f);
	ActiveTimingTargetHalfRange = FMath::Clamp(TargetHalfRange, 0.0f, 0.5f);
	ActiveTimingTargetCenter = FMath::Clamp(TargetCenter, 0.0f, 1.0f);
	ActiveTimingDifficulty = FMath::Clamp(Difficulty, 1, 100);
	ActiveTimingCunningLevel = FMath::Max(0, CunningLevel);
	return true;
}

bool UACFTrainingComponent::ConfirmTimingMinigameAuthority()
{
	if (!IsTimingMinigameSessionActive())
	{
		return false;
	}

	const bool bSuccess = IsCurrentTimingPulseInTargetRange();
	CompleteTrainingAuthority(bSuccess);
	return true;
}

void UACFTrainingComponent::ClearTimingMinigameSession()
{
	ActiveTimingSessionId = INDEX_NONE;
	ActiveTimingServerStartTimeSeconds = 0.0f;
	ActiveTimingSpeedMultiplier = 1.0f;
	ActiveTimingTargetHalfRange = 0.1f;
	ActiveTimingTargetCenter = 0.5f;
	ActiveTimingDifficulty = 1;
	ActiveTimingCunningLevel = 0;
}

const FACFTrainingDefinition* UACFTrainingComponent::FindTrainingDefinition(FName TrainingId) const
{
	const TArray<FACFTrainingDefinition>& Definitions = GetTrainingDefinitionSource();
	return Definitions.FindByPredicate([TrainingId](const FACFTrainingDefinition& Definition)
	{
		return Definition.TrainingId == TrainingId;
	});
}

const TArray<FACFTrainingDefinition>& UACFTrainingComponent::GetTrainingDefinitionSource() const
{
	if (TrainingDefinitionOverrides.Num() > 0)
	{
		return TrainingDefinitionOverrides;
	}

	const UACFTrainingSettings* Settings = GetDefault<UACFTrainingSettings>();
	if (Settings)
	{
		return Settings->GetTrainingDefinitions();
	}

	static const TArray<FACFTrainingDefinition> BuiltInDefinitions = UACFTrainingSettings::MakeDefaultTrainingDefinitions();
	return BuiltInDefinitions;
}

TArray<FACFTrainingDefinition> UACFTrainingComponent::ResolveTrainingDefinitions() const
{
	return GetTrainingDefinitionSource();
}

UARSStatisticsComponent* UACFTrainingComponent::FindStatisticsComponent() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	if (UACFGASStatisticsComponent* GASStatisticsComponent = OwnerActor->FindComponentByClass<UACFGASStatisticsComponent>())
	{
		return GASStatisticsComponent;
	}

	return OwnerActor->FindComponentByClass<UARSStatisticsComponent>();
}

bool UACFTrainingComponent::ValidateScalarRequirements(const FACFTrainingDefinition& Definition, FText& OutReason) const
{
	const FACFTrainingFutureRequirements& Requirements = Definition.Requirements;
	if (!Requirements.bEnableScalarRequirements)
	{
		return true;
	}

	for (const FACFTrainingScalarGate& Gate : Requirements.ScalarRequirements)
	{
		float CurrentValue = 0.0f;
		if (!ResolveScalarGateValue(Gate, CurrentValue))
		{
			OutReason = FText::Format(LOCTEXT("ScalarGateUnresolved", "Resource '{0}' could not be resolved."), FText::FromName(Gate.ResourceName));
			return false;
		}

		if (Gate.bUseMinimumValue && CurrentValue < Gate.MinimumValue)
		{
			OutReason = FText::Format(LOCTEXT("ScalarGateBelowMin", "{0} must be at least {1}."), FText::FromName(Gate.ResourceName), FText::AsNumber(Gate.MinimumValue));
			return false;
		}

		if (Gate.bUseMaximumValue && CurrentValue >= Gate.MaximumValue)
		{
			OutReason = FText::Format(LOCTEXT("ScalarGateAboveMax", "{0} must be below {1}."), FText::FromName(Gate.ResourceName), FText::AsNumber(Gate.MaximumValue));
			return false;
		}
	}

	return true;
}

bool UACFTrainingComponent::GrantTrainingReward(const FACFTrainingDefinition& Definition)
{
	if (!Definition.TargetPrimaryAttribute.IsValid() || Definition.SuccessReward <= 0.0f)
	{
		return false;
	}

	FindOrAddTrainingProgressValue(Definition.TrainingId) += Definition.SuccessReward;
	float& AccumulatedReward = FindOrAddAttributeRewardValue(Definition.TargetPrimaryAttribute);
	AccumulatedReward += Definition.SuccessReward;

	const bool bAppliedToACF = ReapplyTrainingRewardModifier();
	OnTrainingRewardChanged.Broadcast(Definition.TargetPrimaryAttribute, AccumulatedReward);
	return bAppliedToACF;
}

bool UACFTrainingComponent::ReapplyTrainingRewardModifier()
{
	UARSStatisticsComponent* StatisticsComponent = FindStatisticsComponent();
	if (!StatisticsComponent)
	{
		return false;
	}

	if (AccumulatedRewardModifierHandle.IsValid())
	{
		StatisticsComponent->RemoveAttributeSetModifier(AccumulatedRewardModifierHandle);
		AccumulatedRewardModifierHandle.Invalidate();
	}

	const FAttributesSetModifier RewardModifier = BuildAccumulatedRewardModifier();
	if (RewardModifier.PrimaryAttributesMod.Num() == 0)
	{
		return true;
	}

	AccumulatedRewardModifierHandle = StatisticsComponent->AddAttributeSetModifier(RewardModifier);
	const bool bApplied = AccumulatedRewardModifierHandle.IsValid();
	if (bApplied)
	{
		StatisticsComponent->OnAttributeSetModified.Broadcast();
	}
	return bApplied;
}

FAttributesSetModifier UACFTrainingComponent::BuildAccumulatedRewardModifier() const
{
	FAttributesSetModifier RewardModifier;
	RewardModifier.GEModifierType = EGEType::ESetByCallerFromConfig;
	RewardModifier.GameplayEffectModifier = ResolveAttributeModifierGameplayEffect();

	for (const FACFTrainingAttributeRewardEntry& RewardEntry : AccumulatedAttributeRewards)
	{
		if (RewardEntry.TargetAttribute.IsValid() && RewardEntry.AccumulatedReward > 0.0f)
		{
			RewardModifier.PrimaryAttributesMod.Add(FAttributeModifier(RewardEntry.TargetAttribute, EModifierType::EAdditive, RewardEntry.AccumulatedReward));
		}
	}

	return RewardModifier;
}

float* UACFTrainingComponent::FindTrainingProgressValue(FName TrainingId)
{
	FACFTrainingProgressEntry* Entry = TrainingProgress.FindByPredicate([TrainingId](const FACFTrainingProgressEntry& Candidate)
	{
		return Candidate.TrainingId == TrainingId;
	});
	return Entry ? &Entry->Progress : nullptr;
}

const float* UACFTrainingComponent::FindTrainingProgressValue(FName TrainingId) const
{
	const FACFTrainingProgressEntry* Entry = TrainingProgress.FindByPredicate([TrainingId](const FACFTrainingProgressEntry& Candidate)
	{
		return Candidate.TrainingId == TrainingId;
	});
	return Entry ? &Entry->Progress : nullptr;
}

float& UACFTrainingComponent::FindOrAddTrainingProgressValue(FName TrainingId)
{
	if (float* ExistingValue = FindTrainingProgressValue(TrainingId))
	{
		return *ExistingValue;
	}

	FACFTrainingProgressEntry& Entry = TrainingProgress.AddDefaulted_GetRef();
	Entry.TrainingId = TrainingId;
	Entry.Progress = 0.0f;
	return Entry.Progress;
}

float* UACFTrainingComponent::FindAttributeRewardValue(FGameplayTag TargetAttribute)
{
	FACFTrainingAttributeRewardEntry* Entry = AccumulatedAttributeRewards.FindByPredicate([TargetAttribute](const FACFTrainingAttributeRewardEntry& Candidate)
	{
		return Candidate.TargetAttribute == TargetAttribute;
	});
	return Entry ? &Entry->AccumulatedReward : nullptr;
}

const float* UACFTrainingComponent::FindAttributeRewardValue(FGameplayTag TargetAttribute) const
{
	const FACFTrainingAttributeRewardEntry* Entry = AccumulatedAttributeRewards.FindByPredicate([TargetAttribute](const FACFTrainingAttributeRewardEntry& Candidate)
	{
		return Candidate.TargetAttribute == TargetAttribute;
	});
	return Entry ? &Entry->AccumulatedReward : nullptr;
}

float& UACFTrainingComponent::FindOrAddAttributeRewardValue(FGameplayTag TargetAttribute)
{
	if (float* ExistingValue = FindAttributeRewardValue(TargetAttribute))
	{
		return *ExistingValue;
	}

	FACFTrainingAttributeRewardEntry& Entry = AccumulatedAttributeRewards.AddDefaulted_GetRef();
	Entry.TargetAttribute = TargetAttribute;
	Entry.AccumulatedReward = 0.0f;
	return Entry.AccumulatedReward;
}

float UACFTrainingComponent::ResolveServerWorldTimeSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}

		return World->GetTimeSeconds();
	}

	return 0.0f;
}

float UACFTrainingComponent::ComputeTimingPulseValue(float ElapsedSeconds, float SpeedMultiplier)
{
	constexpr float BasePulsePeriodSeconds = 2.0f;
	const float SafeSpeedMultiplier = FMath::Max(SpeedMultiplier, 0.01f);
	const float PeriodSeconds = BasePulsePeriodSeconds / SafeSpeedMultiplier;
	const float Wrapped = FMath::Fmod(FMath::Max(0.0f, ElapsedSeconds), PeriodSeconds);
	const float Phase = Wrapped / PeriodSeconds;
	return Phase <= 0.5f ? Phase * 2.0f : (1.0f - Phase) * 2.0f;
}

#undef LOCTEXT_NAMESPACE
