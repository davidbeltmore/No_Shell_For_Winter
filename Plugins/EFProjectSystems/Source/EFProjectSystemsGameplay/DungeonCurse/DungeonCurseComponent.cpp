#include "DungeonCurse/DungeonCurseComponent.h"

#include "DungeonCurse/DungeonCurseTargetInterface.h"
#include "DungeonCurse/RoomCurseVolume.h"
#include "GameFramework/Actor.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Misc/Crc.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDungeonCurseComponent, Log, All);

namespace DungeonCurseComponentPrivate
{
	static const FName MadnessName(TEXT("Madness"));
	static const FName HungerName(TEXT("Hunger"));
	static const FName ThirstName(TEXT("Thirst"));

	static bool HasTargetInterface(const AActor* Actor)
	{
		return IsValid(Actor) && Actor->GetClass()->ImplementsInterface(UDungeonCurseTargetInterface::StaticClass());
	}
}

UDungeonCurseComponent::UDungeonCurseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CachedNeedsComponent = nullptr;
	CachedInnerDoctrineComponent = nullptr;
}

void UDungeonCurseComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveNeedsComponent();
	ResolveInnerDoctrineComponent();
}

void UDungeonCurseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAllRoomCurses();
	Super::EndPlay(EndPlayReason);
}

bool UDungeonCurseComponent::ApplyRoomCurse(ARoomCurseVolume* SourceVolume, const FRoomCurseDefinition& Curse, const float InTickInterval)
{
	if (!IsValid(SourceVolume) || Curse.CurseType == ERoomCurseType::None)
	{
		return false;
	}

	const FName SourceID = SourceVolume->GetCurseSourceID();
	if (SourceID.IsNone() || ActiveCurses.Contains(SourceID))
	{
		return false;
	}

	FActiveDungeonRoomCurse& Entry = ActiveCurses.Add(SourceID);
	Entry.Curse = Curse;
	Entry.SourceVolume = SourceVolume;
	Entry.TickInterval = FMath::Max(InTickInterval, 0.05f);
	Entry.ActivationId = FGuid::NewGuid();
	Entry.NextPeriodicApplicationSequence = 0;

	if (UProjectInnerDoctrineComponent* InnerDoctrineComponent =
		ResolveInnerDoctrineComponent())
	{
		InnerDoctrineComponent->RegisterCurseZonePresence(Entry.ActivationId);
	}
	ApplyEntryEffects(SourceID, Entry);
	NotifyCurseApplied(Curse);

	const bool bNeedsPeriodicTimer =
		(Curse.CurseType == ERoomCurseType::MadnessPerSecond && !FMath::IsNearlyZero(Curse.MadnessPerSecond))
		|| (Curse.CurseType == ERoomCurseType::CurseBuildupPerSecond && Curse.CurseBuildupPerSecond > 0.0f);

	if (bNeedsPeriodicTimer)
	{
		if (UWorld* World = GetWorld())
		{
			FTimerDelegate PeriodicDelegate;
			PeriodicDelegate.BindUObject(this, &ThisClass::ApplyPeriodicCurse, SourceID);
			World->GetTimerManager().SetTimer(
				Entry.PeriodicTimerHandle,
				PeriodicDelegate,
				Entry.TickInterval,
				true,
				Entry.TickInterval);
		}
	}

	if (Curse.Duration > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			FTimerDelegate DurationDelegate;
			DurationDelegate.BindUObject(this, &ThisClass::HandleDurationExpired, SourceID);
			World->GetTimerManager().SetTimer(Entry.DurationTimerHandle, DurationDelegate, Curse.Duration, false);
		}
	}

	UE_LOG(LogDungeonCurseComponent, Log, TEXT("Applied room curse %s to %s from %s."),
		*ResolveCurseID(Curse).ToString(),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(SourceVolume));

	return true;
}

bool UDungeonCurseComponent::RemoveRoomCurse(const FName SourceID)
{
	FActiveDungeonRoomCurse* Entry = ActiveCurses.Find(SourceID);
	if (!Entry)
	{
		return false;
	}

	FActiveDungeonRoomCurse EntryCopy = *Entry;
	if (UProjectInnerDoctrineComponent* InnerDoctrineComponent =
		ResolveInnerDoctrineComponent())
	{
		InnerDoctrineComponent->UnregisterCurseZonePresence(EntryCopy.ActivationId);
	}
	RemoveEntryEffects(SourceID, EntryCopy);
	NotifyCurseRemoved(EntryCopy.Curse);
	ActiveCurses.Remove(SourceID);
	WarnedMissingInnerStateSources.Remove(SourceID);
	WarnedMissingDoctrineSources.Remove(SourceID);

	UE_LOG(LogDungeonCurseComponent, Log, TEXT("Removed room curse source %s from %s."), *SourceID.ToString(), *GetNameSafe(GetOwner()));
	return true;
}

void UDungeonCurseComponent::ClearAllRoomCurses()
{
	TArray<FName> SourceIDs;
	ActiveCurses.GetKeys(SourceIDs);
	for (const FName SourceID : SourceIDs)
	{
		RemoveRoomCurse(SourceID);
	}
}

bool UDungeonCurseComponent::HasActiveCurse(const FName SourceID) const
{
	return ActiveCurses.Contains(SourceID);
}

TArray<FName> UDungeonCurseComponent::GetActiveCurseIDs() const
{
	TArray<FName> SourceIDs;
	ActiveCurses.GetKeys(SourceIDs);
	return SourceIDs;
}

FName UDungeonCurseComponent::MakeFallbackSourceID(const UObject* SourceObject, const FRoomCurseDefinition& Curse)
{
	const FString SourceName = IsValid(SourceObject) ? SourceObject->GetName() : FString(TEXT("UnknownSource"));
	const FString CurseName = !Curse.CurseID.IsNone() ? Curse.CurseID.ToString() : StaticEnum<ERoomCurseType>()->GetNameStringByValue(static_cast<int64>(Curse.CurseType));
	return FName(*FString::Printf(TEXT("%s_%s"), *SourceName, *CurseName));
}

FProjectCurseApplicationContext UDungeonCurseComponent::BuildPeriodicCurseApplicationContext(
	const UObject* TargetObject,
	AActor* SourceActor,
	const FName SourceID,
	const FGuid& ActivationId,
	const uint32 SequenceIndex,
	const float Amount,
	const EProjectCurseSourceKind SourceKind)
{
	const FString StableSeed = FString::Printf(
		TEXT("%s|%s|%s|%u"),
		*GetPathNameSafe(TargetObject),
		*SourceID.ToString(),
		*ActivationId.ToString(EGuidFormats::Digits),
		SequenceIndex);

	FGuid ApplicationId(
		FCrc::StrCrc32(*(StableSeed + TEXT("|A"))),
		FCrc::StrCrc32(*(StableSeed + TEXT("|B"))),
		FCrc::StrCrc32(*(StableSeed + TEXT("|C"))),
		FCrc::StrCrc32(*(StableSeed + TEXT("|D"))));
	if (!ApplicationId.IsValid())
	{
		ApplicationId.D = 1;
	}

	FProjectCurseApplicationContext Context;
	Context.Amount = FMath::Max(0.0f, Amount);
	Context.SourceKind = SourceKind == EProjectCurseSourceKind::Environment
		? EProjectCurseSourceKind::Environment
		: EProjectCurseSourceKind::Room;
	Context.Instigator = SourceActor;
	Context.ApplicationId = ApplicationId;
	Context.bResistible = true;
	Context.bCanTriggerCursed = true;
	return Context;
}

UProjectSurvivalNeedsComponent* UDungeonCurseComponent::ResolveNeedsComponent()
{
	if (!CachedNeedsComponent && GetOwner())
	{
		CachedNeedsComponent = GetOwner()->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	}

	return CachedNeedsComponent;
}

UProjectInnerDoctrineComponent* UDungeonCurseComponent::ResolveInnerDoctrineComponent()
{
	if (!IsValid(CachedInnerDoctrineComponent) && GetOwner())
	{
		CachedInnerDoctrineComponent = GetOwner()->FindComponentByClass<UProjectInnerDoctrineComponent>();
	}

	return CachedInnerDoctrineComponent;
}

void UDungeonCurseComponent::ApplyEntryEffects(const FName SourceID, FActiveDungeonRoomCurse& Entry)
{
	switch (Entry.Curse.CurseType)
	{
	case ERoomCurseType::HungerDrainMultiplier:
		ApplyNeedDrainMultiplier(SourceID, DungeonCurseComponentPrivate::HungerName, Entry.Curse.HungerDrainMultiplier, true);
		break;
	case ERoomCurseType::ThirstDrainMultiplier:
		ApplyNeedDrainMultiplier(SourceID, DungeonCurseComponentPrivate::ThirstName, Entry.Curse.ThirstDrainMultiplier, true);
		break;
	default:
		break;
	}
}

void UDungeonCurseComponent::RemoveEntryEffects(const FName SourceID, FActiveDungeonRoomCurse& Entry)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(Entry.PeriodicTimerHandle);
		World->GetTimerManager().ClearTimer(Entry.DurationTimerHandle);
	}

	switch (Entry.Curse.CurseType)
	{
	case ERoomCurseType::HungerDrainMultiplier:
		ApplyNeedDrainMultiplier(SourceID, DungeonCurseComponentPrivate::HungerName, Entry.Curse.HungerDrainMultiplier, false);
		break;
	case ERoomCurseType::ThirstDrainMultiplier:
		ApplyNeedDrainMultiplier(SourceID, DungeonCurseComponentPrivate::ThirstName, Entry.Curse.ThirstDrainMultiplier, false);
		break;
	default:
		break;
	}
}

void UDungeonCurseComponent::ApplyPeriodicCurse(const FName SourceID)
{
	FActiveDungeonRoomCurse* Entry = ActiveCurses.Find(SourceID);
	if (!Entry)
	{
		return;
	}

	switch (Entry->Curse.CurseType)
	{
	case ERoomCurseType::MadnessPerSecond:
		ApplyInnerStateDelta(SourceID, DungeonCurseComponentPrivate::MadnessName, Entry->Curse.MadnessPerSecond * Entry->TickInterval);
		break;
	case ERoomCurseType::CurseBuildupPerSecond:
		ApplyCurseBuildup(SourceID, *Entry);
		break;
	default:
		break;
	}
}

void UDungeonCurseComponent::HandleDurationExpired(const FName SourceID)
{
	RemoveRoomCurse(SourceID);
}

bool UDungeonCurseComponent::ApplyCurseBuildup(const FName SourceID, FActiveDungeonRoomCurse& Entry)
{
	UProjectInnerDoctrineComponent* InnerDoctrineComponent = ResolveInnerDoctrineComponent();
	if (!InnerDoctrineComponent)
	{
		if (!WarnedMissingDoctrineSources.Contains(SourceID))
		{
			WarnedMissingDoctrineSources.Add(SourceID);
			UE_LOG(
				LogDungeonCurseComponent,
				Warning,
				TEXT("Room curse %s could not find Inner Doctrine support on %s."),
				*SourceID.ToString(),
				*GetNameSafe(GetOwner()));
		}
		return false;
	}
	InnerDoctrineComponent->RegisterCurseZonePresence(Entry.ActivationId);

	const float Amount = Entry.Curse.CurseBuildupPerSecond * Entry.TickInterval;
	if (Amount <= 0.0f)
	{
		return false;
	}

	AActor* SourceActor = Entry.SourceVolume.Get();
	const FProjectCurseApplicationContext Context = BuildPeriodicCurseApplicationContext(
		GetOwner(),
		SourceActor,
		SourceID,
		Entry.ActivationId,
		Entry.NextPeriodicApplicationSequence++,
		Amount,
		Entry.Curse.CurseSourceKind);
	const FProjectCurseApplicationResult Result = InnerDoctrineComponent->ApplyCurse(Context);
	return Result.AppliedAmount > 0.0f || Result.bDuplicate;
}

bool UDungeonCurseComponent::ApplyInnerStateDelta(const FName SourceID, const FName StateName, const float DeltaValue)
{
	bool bApplied = false;
	if (UProjectSurvivalNeedsComponent* NeedsComponent = ResolveNeedsComponent())
	{
		if (NeedsComponent->HasSensation(StateName))
		{
			NeedsComponent->ModifySensationValue(StateName, DeltaValue, true);
			bApplied = true;
		}
	}

	AActor* Owner = GetOwner();
	if (DungeonCurseComponentPrivate::HasTargetInterface(Owner))
	{
		IDungeonCurseTargetInterface::Execute_ApplyInnerStateFlatModifier(Owner, StateName, DeltaValue);
		bApplied = true;
	}

	if (!bApplied && !WarnedMissingInnerStateSources.Contains(SourceID))
	{
		WarnedMissingInnerStateSources.Add(SourceID);
		UE_LOG(LogDungeonCurseComponent, Warning, TEXT("Room curse %s could not find Inner State support on %s."),
			*SourceID.ToString(),
			*GetNameSafe(Owner));
	}

	return bApplied;
}

bool UDungeonCurseComponent::ApplyNeedDrainMultiplier(const FName SourceID, const FName NeedName, const float Multiplier, const bool bEnabled)
{
	bool bApplied = false;
	if (UProjectSurvivalNeedsComponent* NeedsComponent = ResolveNeedsComponent())
	{
		if (NeedsComponent->HasNeed(NeedName))
		{
			if (bEnabled)
			{
				NeedsComponent->SetExternalNeedDecayMultiplier(SourceID, NeedName, Multiplier);
			}
			else
			{
				NeedsComponent->ClearExternalNeedDecayMultiplier(SourceID, NeedName);
			}
			bApplied = true;
		}
	}

	AActor* Owner = GetOwner();
	if (DungeonCurseComponentPrivate::HasTargetInterface(Owner))
	{
		IDungeonCurseTargetInterface::Execute_ApplyInnerStateDrainMultiplier(Owner, NeedName, Multiplier, bEnabled);
		bApplied = true;
	}

	if (!bApplied && !WarnedMissingInnerStateSources.Contains(SourceID))
	{
		WarnedMissingInnerStateSources.Add(SourceID);
		UE_LOG(LogDungeonCurseComponent, Warning, TEXT("Room curse %s could not apply %s drain multiplier on %s."),
			*SourceID.ToString(),
			*NeedName.ToString(),
			*GetNameSafe(Owner));
	}

	return bApplied;
}

void UDungeonCurseComponent::NotifyCurseApplied(const FRoomCurseDefinition& Curse) const
{
	AActor* Owner = GetOwner();
	if (DungeonCurseComponentPrivate::HasTargetInterface(Owner))
	{
		IDungeonCurseTargetInterface::Execute_OnRoomCurseApplied(Owner, ResolveCurseID(Curse));
	}
}

void UDungeonCurseComponent::NotifyCurseRemoved(const FRoomCurseDefinition& Curse) const
{
	AActor* Owner = GetOwner();
	if (DungeonCurseComponentPrivate::HasTargetInterface(Owner))
	{
		IDungeonCurseTargetInterface::Execute_OnRoomCurseRemoved(Owner, ResolveCurseID(Curse));
	}
}

FName UDungeonCurseComponent::ResolveCurseID(const FRoomCurseDefinition& Curse) const
{
	if (!Curse.CurseID.IsNone())
	{
		return Curse.CurseID;
	}

	return FName(*StaticEnum<ERoomCurseType>()->GetNameStringByValue(static_cast<int64>(Curse.CurseType)));
}
