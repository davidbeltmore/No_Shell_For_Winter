#include "Lockpicking/ProjectLockpickableComponent.h"

#include "Components/ACFInventoryComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/ACFInteractableInterface.h"
#include "Items/ACFItem.h"
#include "Lockpicking/ProjectLockpickingSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"

namespace ProjectLockpickingComponentPrivate
{
	const FString BeginInteractionType(TEXT("Project.Lockpick.Begin"));
	const FString ConfirmPrefix(TEXT("Project.Lockpick.Confirm:"));
	constexpr float BasePulsePeriodSeconds = 2.0f;

	float ComputePulseValue(const float ElapsedSeconds, const float SpeedMultiplier)
	{
		const float PeriodSeconds = BasePulsePeriodSeconds / FMath::Max(SpeedMultiplier, 0.01f);
		const float WrappedSeconds = FMath::Fmod(FMath::Max(ElapsedSeconds, 0.0f), PeriodSeconds);
		const float Phase = WrappedSeconds / PeriodSeconds;
		return Phase <= 0.5f ? Phase * 2.0f : (1.0f - Phase) * 2.0f;
	}
}

UProjectLockpickableComponent::UProjectLockpickableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	RequiredLockpickItemClass = TSoftClassPtr<UACFItem>(FSoftObjectPath(TEXT("/Game/_Game/Lockpicking/LockPick.LockPick_C")));
	RequiredItemDisplayName = NSLOCTEXT("ProjectLockpicking", "DefaultRequiredItemDisplayName", "Lockpick");
}

void UProjectLockpickableComponent::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		bLocked = bStartsLocked;
	}
}

void UProjectLockpickableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UProjectLockpickableComponent, bLocked);
	DOREPLIFETIME(UProjectLockpickableComponent, ActiveSessionId);
	DOREPLIFETIME(UProjectLockpickableComponent, ActivePawn);
	DOREPLIFETIME(UProjectLockpickableComponent, ActiveServerStartTimeSeconds);
	DOREPLIFETIME(UProjectLockpickableComponent, ActiveSpeedMultiplier);
	DOREPLIFETIME(UProjectLockpickableComponent, ActiveTargetHalfRange);
	DOREPLIFETIME(UProjectLockpickableComponent, ActiveDifficulty);
	DOREPLIFETIME(UProjectLockpickableComponent, ActiveCunningLevel);
	DOREPLIFETIME(UProjectLockpickableComponent, LastFailureServerTimeSeconds);
}

bool UProjectLockpickableComponent::HandleACFInteraction(
	APawn* Pawn,
	const FString& InteractionType,
	EProjectLockpickInteractionGateResult& OutResult)
{
	OutResult = EProjectLockpickInteractionGateResult::RunOriginal;

	if (!bLocked)
	{
		return true;
	}

	OutResult = EProjectLockpickInteractionGateResult::Consumed;

	if (!Pawn)
	{
		return true;
	}

	int32 ConfirmSessionId = INDEX_NONE;
	if (TryParseLockpickConfirmInteractionType(InteractionType, ConfirmSessionId))
	{
		if (HasAuthority())
		{
			ConfirmLockpickSession(Pawn, ConfirmSessionId);
		}
		return true;
	}

	if (IsLockpickBeginInteractionType(InteractionType))
	{
		if (HasAuthority() && CanPawnAttemptLockpick(Pawn))
		{
			StartLockpickSession(Pawn, FString());
		}
		return true;
	}

	if (bShowPromptBeforeLockpicking)
	{
		return true;
	}

	if (!HasAuthority())
	{
		return true;
	}

	if (!CanPawnAttemptLockpick(Pawn))
	{
		return true;
	}

	StartLockpickSession(Pawn, InteractionType);
	return true;
}

bool UProjectLockpickableComponent::ConsumeACFInteractionIfLocked(APawn* Pawn, const FString& InteractionType)
{
	EProjectLockpickInteractionGateResult GateResult = EProjectLockpickInteractionGateResult::RunOriginal;
	HandleACFInteraction(Pawn, InteractionType, GateResult);
	return GateResult == EProjectLockpickInteractionGateResult::Consumed;
}

bool UProjectLockpickableComponent::HandleACFLocalInteraction(APawn* Pawn, const FString& InteractionType)
{
	UWorld* World = GetWorld();
	UProjectLockpickingSubsystem* LockpickingSubsystem = World ? World->GetSubsystem<UProjectLockpickingSubsystem>() : nullptr;
	if (!LockpickingSubsystem)
	{
		return false;
	}

	if (IsLockpickConfirmInteractionType(InteractionType))
	{
		LockpickingSubsystem->CloseLockpicking();
		return true;
	}

	if (!Pawn || !bLocked)
	{
		return false;
	}

	if (IsLockpickBeginInteractionType(InteractionType))
	{
		return CanPawnAttemptLockpick(Pawn) && LockpickingSubsystem->OpenLockpicking(GetOwner(), this, Pawn);
	}

	if (bShowPromptBeforeLockpicking)
	{
		return LockpickingSubsystem->OpenLockpickPrompt(GetOwner(), this, Pawn);
	}

	return LockpickingSubsystem->OpenLockpicking(GetOwner(), this, Pawn);
}

bool UProjectLockpickableComponent::ConsumeACFLocalInteractionIfLocked(APawn* Pawn, const FString& InteractionType)
{
	return HandleACFLocalInteraction(Pawn, InteractionType);
}

bool UProjectLockpickableComponent::CanBeInteracted(APawn* Pawn) const
{
	if (!bLocked)
	{
		return true;
	}

	if (IsSessionActiveForPawn(Pawn))
	{
		return true;
	}

	return !IsInFailureCooldown();
}

bool UProjectLockpickableComponent::ShouldAllowACFInteraction(APawn* Pawn, const bool bOriginalCanInteract) const
{
	return bOriginalCanInteract && CanBeInteracted(Pawn);
}

bool UProjectLockpickableComponent::CanPawnAttemptLockpick(APawn* Pawn) const
{
	if (!Pawn || !bLocked || IsInFailureCooldown())
	{
		return false;
	}

	if (IsSessionActive())
	{
		return IsSessionActiveForPawn(Pawn);
	}

	return HasRequiredLockpick(Pawn);
}

bool UProjectLockpickableComponent::HasRequiredLockpick(APawn* Pawn) const
{
	if (!bRequireLockpickItem)
	{
		return true;
	}

	return GetRequiredLockpickCount(Pawn) > 0;
}

int32 UProjectLockpickableComponent::GetRequiredLockpickCount(APawn* Pawn) const
{
	if (!Pawn)
	{
		return 0;
	}

	const TSubclassOf<UACFItem> ItemClass = RequiredLockpickItemClass.LoadSynchronous();
	if (!ItemClass)
	{
		return 0;
	}

	const UACFInventoryComponent* InventoryComponent = ResolveInventoryComponent(Pawn);
	return InventoryComponent ? InventoryComponent->GetTotalCountOfItemsByClass(ItemClass) : 0;
}

FText UProjectLockpickableComponent::GetRequiredItemDisplayName() const
{
	return RequiredItemDisplayName.IsEmpty()
		? NSLOCTEXT("ProjectLockpicking", "DefaultRequiredItemDisplayName", "Lockpick")
		: RequiredItemDisplayName;
}

bool UProjectLockpickableComponent::IsLocked() const
{
	return bLocked;
}

bool UProjectLockpickableComponent::IsSessionActive() const
{
	return ActiveSessionId != INDEX_NONE;
}

bool UProjectLockpickableComponent::IsSessionActiveForPawn(APawn* Pawn) const
{
	return Pawn && ActiveSessionId != INDEX_NONE && ActivePawn == Pawn;
}

bool UProjectLockpickableComponent::IsInFailureCooldown() const
{
	if (FailureCooldownSeconds <= 0.0f)
	{
		return false;
	}

	const float ElapsedSeconds = ResolveServerWorldTimeSeconds() - LastFailureServerTimeSeconds;
	return ElapsedSeconds >= 0.0f && ElapsedSeconds < FailureCooldownSeconds;
}

int32 UProjectLockpickableComponent::GetActiveSessionId() const
{
	return ActiveSessionId;
}

float UProjectLockpickableComponent::GetActiveSpeedMultiplier() const
{
	return ActiveSpeedMultiplier;
}

float UProjectLockpickableComponent::GetActiveTargetHalfRange() const
{
	return ActiveTargetHalfRange;
}

float UProjectLockpickableComponent::GetCurrentPulseValue() const
{
	return GetCurrentPulseValueForServerTime(ResolveServerWorldTimeSeconds());
}

float UProjectLockpickableComponent::GetCurrentPulseValueForServerTime(const float ServerTimeSeconds) const
{
	if (!IsSessionActive())
	{
		return 0.0f;
	}

	return ProjectLockpickingComponentPrivate::ComputePulseValue(
		ServerTimeSeconds - ActiveServerStartTimeSeconds,
		ActiveSpeedMultiplier);
}

FString UProjectLockpickableComponent::BuildConfirmInteractionType() const
{
	return IsSessionActive()
		? FString::Printf(TEXT("%s%d"), *ProjectLockpickingComponentPrivate::ConfirmPrefix, ActiveSessionId)
		: FString();
}

FString UProjectLockpickableComponent::BuildBeginInteractionType() const
{
	return ProjectLockpickingComponentPrivate::BeginInteractionType;
}

void UProjectLockpickableComponent::SetLocked(const bool bInLocked)
{
	if (!HasAuthority())
	{
		return;
	}

	bLocked = bInLocked;
	if (!bLocked)
	{
		ClearActiveSession();
	}
}

bool UProjectLockpickableComponent::ReplayOwnerACFInteraction(APawn* Pawn, const FString& InteractionType) const
{
	AActor* Owner = GetOwner();
	if (!Owner || !Pawn || !Owner->GetClass()->ImplementsInterface(UACFInteractableInterface::StaticClass()))
	{
		return false;
	}

	IACFInteractableInterface::Execute_OnInteractedByPawn(Owner, Pawn, InteractionType);
	return true;
}

bool UProjectLockpickableComponent::IsLockpickBeginInteractionType(const FString& InteractionType)
{
	return InteractionType.Equals(ProjectLockpickingComponentPrivate::BeginInteractionType, ESearchCase::CaseSensitive);
}

bool UProjectLockpickableComponent::IsLockpickConfirmInteractionType(const FString& InteractionType)
{
	return InteractionType.StartsWith(ProjectLockpickingComponentPrivate::ConfirmPrefix);
}

bool UProjectLockpickableComponent::TryParseLockpickConfirmInteractionType(const FString& InteractionType, int32& OutSessionId)
{
	OutSessionId = INDEX_NONE;
	if (!IsLockpickConfirmInteractionType(InteractionType))
	{
		return false;
	}

	const FString SessionString = InteractionType.RightChop(ProjectLockpickingComponentPrivate::ConfirmPrefix.Len());
	return LexTryParseString(OutSessionId, *SessionString);
}

float UProjectLockpickableComponent::ComputeSpeedMultiplier(const int32 InDifficulty, const int32 InCunningLevel)
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	return UProjectInnerDoctrineSettings::ComputeCunningLockpickSpeedMultiplier(
		InDifficulty,
		InCunningLevel,
		Settings ? Settings->CunningLockpickSpeedBaseMultiplier : 1.15f,
		Settings ? Settings->CunningLockpickMaxSlowPct : 0.45f,
		Settings ? Settings->CunningLockpickTimePivot : 10.f);
}

float UProjectLockpickableComponent::ComputeTargetHalfRange(const int32 InDifficulty, const int32 InCunningLevel)
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	return UProjectInnerDoctrineSettings::ComputeCunningLockpickTargetHalfRange(
		InDifficulty,
		InCunningLevel,
		Settings ? Settings->CunningLockpickZonePivot : 5.f);
}

void UProjectLockpickableComponent::StartLockpickSession(APawn* Pawn, const FString& OriginalInteractionType)
{
	if (!CanPawnAttemptLockpick(Pawn))
	{
		return;
	}

	++LastIssuedSessionId;
	if (LastIssuedSessionId <= 0)
	{
		LastIssuedSessionId = 1;
	}

	ActiveSessionId = LastIssuedSessionId;
	ActivePawn = Pawn;
	ActiveServerStartTimeSeconds = ResolveServerWorldTimeSeconds();
	ActiveDifficulty = FMath::Clamp(Difficulty, 1, 100);
	ActiveCunningLevel = ResolveCunningLevel(Pawn);
	ActiveSpeedMultiplier = ComputeSpeedMultiplier(ActiveDifficulty, ActiveCunningLevel);
	ActiveTargetHalfRange = ComputeTargetHalfRange(ActiveDifficulty, ActiveCunningLevel);
	ActiveOriginalInteractionType = OriginalInteractionType;

	OnLockpickStarted.Broadcast(Pawn, ActiveSessionId);
}

void UProjectLockpickableComponent::ConfirmLockpickSession(APawn* Pawn, const int32 SessionId)
{
	if (!Pawn || SessionId == INDEX_NONE || SessionId != ActiveSessionId || ActivePawn != Pawn || IsInFailureCooldown())
	{
		return;
	}

	const float PulseValue = GetCurrentPulseValueForServerTime(ResolveServerWorldTimeSeconds());
	const bool bSucceeded = FMath::Abs(PulseValue - FMath::Clamp(TargetCenter, 0.0f, 1.0f)) <= ActiveTargetHalfRange;
	if (bSucceeded)
	{
		CompleteLockpickSuccess(Pawn, PulseValue);
	}
	else
	{
		CompleteLockpickFailure(Pawn, PulseValue);
	}
}

void UProjectLockpickableComponent::CompleteLockpickSuccess(APawn* Pawn, const float PulseValue)
{
	const int32 CompletedSessionId = ActiveSessionId;
	const FString OriginalInteractionType = ActiveOriginalInteractionType;

	bLocked = false;
	ClearActiveSession();

	OnLockpickSucceeded.Broadcast(Pawn, CompletedSessionId, PulseValue);
	if (bExecuteOriginalOnSuccess)
	{
		if (bReplayOwnerACFInteractionOnSuccess && ReplayOwnerACFInteraction(Pawn, OriginalInteractionType))
		{
			return;
		}

		OnUnlockedInteractionRequested.Broadcast(Pawn, OriginalInteractionType);
	}
}

void UProjectLockpickableComponent::CompleteLockpickFailure(APawn* Pawn, const float PulseValue)
{
	const int32 CompletedSessionId = ActiveSessionId;
	LastFailureServerTimeSeconds = ResolveServerWorldTimeSeconds();
	ClearActiveSession();
	OnLockpickFailed.Broadcast(Pawn, CompletedSessionId, PulseValue);
}

void UProjectLockpickableComponent::ClearActiveSession()
{
	ActiveSessionId = INDEX_NONE;
	ActivePawn = nullptr;
	ActiveServerStartTimeSeconds = 0.0f;
	ActiveSpeedMultiplier = 1.0f;
	ActiveTargetHalfRange = ComputeTargetHalfRange(FMath::Clamp(Difficulty, 1, 100), 0);
	ActiveDifficulty = FMath::Clamp(Difficulty, 1, 100);
	ActiveCunningLevel = 0;
	ActiveOriginalInteractionType.Reset();
}

int32 UProjectLockpickableComponent::ResolveCunningLevel(APawn* Pawn) const
{
	if (!Pawn)
	{
		return 0;
	}

	if (const UProjectInnerDoctrineComponent* Component = Pawn->FindComponentByClass<UProjectInnerDoctrineComponent>())
	{
		return Component->GetAttributeLevel(EProjectDoctrineAttribute::Cunning);
	}

	if (const AController* Controller = Pawn->GetController())
	{
		if (const UProjectInnerDoctrineComponent* Component = Controller->FindComponentByClass<UProjectInnerDoctrineComponent>())
		{
			return Component->GetAttributeLevel(EProjectDoctrineAttribute::Cunning);
		}
	}

	return 0;
}

UACFInventoryComponent* UProjectLockpickableComponent::ResolveInventoryComponent(APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}

	if (UACFInventoryComponent* InventoryComponent = Pawn->FindComponentByClass<UACFInventoryComponent>())
	{
		return InventoryComponent;
	}

	if (AController* Controller = Pawn->GetController())
	{
		if (UACFInventoryComponent* InventoryComponent = Controller->FindComponentByClass<UACFInventoryComponent>())
		{
			return InventoryComponent;
		}
	}

	return nullptr;
}

float UProjectLockpickableComponent::ResolveServerWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}

bool UProjectLockpickableComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
