#include "Calysto/ProjectCalystoContentAttemptProvider.h"

#include "Calysto/EFCalystoDirectorProbability.h"
#include "Calysto/ProjectCalystoContentGameplayBridge.h"
#include "Calysto/ProjectCalystoFloorOutcomeSubsystem.h"
#include "Calysto/ProjectCalystoGameplaySnapshot.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace ProjectCalystoContentProviderPrivate
{
	TSharedPtr<FProjectCalystoGameplaySnapshot> ProjectSnapshot(const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot,
		const TSet<const IEFCalystoGameplaySnapshot*>& CapturedSnapshots, FString& Error)
	{
		if (!Snapshot || !CapturedSnapshots.Contains(Snapshot.Get()))
		{
			Error = TEXT("The V7 gameplay snapshot was not captured by the selected project provider.");
			return {};
		}
		return StaticCastSharedPtr<FProjectCalystoGameplaySnapshot>(Snapshot);
	}

	FGuid StableIdentity(const FEFCalystoRandomKey& Key, const FGuid& Reservation)
	{
		const uint64 First=FEFCalystoDirectorProbability::Hash(Key,EEFCalystoRandomDomain::Entry,Reservation,71);
		const uint64 Second=FEFCalystoDirectorProbability::Hash(Key,EEFCalystoRandomDomain::Entry,Reservation,72);
		FGuid Result(uint32(First),uint32(First>>32),uint32(Second),uint32(Second>>32));
		if (!Result.IsValid()) Result=FGuid(1,0,0,1);
		return Result;
	}

	int32 ResolveLogicalLevel(const FEFCalystoRandomKey& Key,const FEFCalystoReservedContent& Reservation)
	{
		const int32 Minimum=Reservation.Entry.MinimumLevelOffset;
		const int32 Maximum=Reservation.Entry.MaximumLevelOffset;
		if (Minimum>Maximum) return 0;
		const int64 Span=int64(Maximum)-int64(Minimum)+1;
		if (Span<=0 || Span>200001) return 0;
		const double Unit=FEFCalystoDirectorProbability::Unit(Key,EEFCalystoRandomDomain::Entry,Reservation.Id,73);
		const int64 Offset=int64(Minimum)+FMath::Min<int64>(Span-1,FMath::FloorToInt64(Unit*double(Span)));
		return int32(FMath::Clamp<int64>(Key.FloorNumber+Offset,1,MAX_int32));
	}
}

bool FProjectCalystoContentAttemptProvider::CapturePreTravelSnapshot(const FGuid& RequestId, UWorld* SourceWorld,
	TSharedPtr<IEFCalystoGameplaySnapshot>& OutSnapshot, FString& Error)
{
	OutSnapshot.Reset(); Error.Reset();
	if (!RequestId.IsValid() || !IsValid(SourceWorld) || !SourceWorld->IsGameWorld())
	{
		Error = TEXT("V7 pre-travel capture requires one valid routing request and source game world.");
		return false;
	}
	UGameInstance* Instance = SourceWorld->GetGameInstance();
	APawn* Player = UGameplayStatics::GetPlayerPawn(SourceWorld, 0);
	auto* Roster = Instance ? Instance->GetSubsystem<UProjectRunCompanionSubsystem>() : nullptr;
	auto* Outcomes = Instance ? Instance->GetSubsystem<UProjectCalystoFloorOutcomeSubsystem>() : nullptr;
	TSharedPtr<FProjectCalystoGameplaySnapshot> Snapshot = FProjectCalystoGameplaySnapshot::Capture(
		RequestId, SourceWorld, Player, Roster, Outcomes, Error);
	if (!Snapshot) return false;
	OutSnapshot = Snapshot;
	CapturedSnapshots.Add(OutSnapshot.Get());
	return true;
}

bool FProjectCalystoContentAttemptProvider::CanHandleSnapshot(
	const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot) const
{
	return Snapshot && CapturedSnapshots.Contains(Snapshot.Get());
}

bool FProjectCalystoContentAttemptProvider::PreparePreTravelSnapshot(const FGuid& RequestId,
	const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error)
{
	const TSharedPtr<FProjectCalystoGameplaySnapshot> ProjectSnapshot =
		ProjectCalystoContentProviderPrivate::ProjectSnapshot(Snapshot, CapturedSnapshots, Error);
	if (!RequestId.IsValid() || !ProjectSnapshot)
	{
		if (Error.IsEmpty()) Error = TEXT("V7 pre-travel preparation requires the exact project gameplay snapshot.");
		return false;
	}
	return ProjectSnapshot->PrepareForTravel(RequestId, Error);
}

bool FProjectCalystoContentAttemptProvider::DetachPreTravelSnapshot(const FGuid& RequestId,
	const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error)
{
	const TSharedPtr<FProjectCalystoGameplaySnapshot> ProjectSnapshot =
		ProjectCalystoContentProviderPrivate::ProjectSnapshot(Snapshot, CapturedSnapshots, Error);
	if (!RequestId.IsValid() || !ProjectSnapshot)
	{
		if (Error.IsEmpty()) Error = TEXT("V7 source detachment requires the exact project gameplay snapshot.");
		return false;
	}
	return ProjectSnapshot->DetachForTravel(RequestId, Error);
}

bool FProjectCalystoContentAttemptProvider::ReconstructPreTravelSnapshot(const FEFCalystoAttemptToken& Token,
	UWorld* DestinationWorld, const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error)
{
	Error.Reset();
	const TSharedPtr<FProjectCalystoGameplaySnapshot> ProjectSnapshot =
		ProjectCalystoContentProviderPrivate::ProjectSnapshot(Snapshot, CapturedSnapshots, Error);
	if (!Token.Request.IsValid() || !Token.Attempt.IsValid() || !ProjectSnapshot || !IsValid(DestinationWorld)
		|| !DestinationWorld->IsGameWorld())
	{
		if (Error.IsEmpty()) Error = TEXT("V7 destination reconstruction requires the owned attempt, detached snapshot and destination game world.");
		return false;
	}
	// A cleaned spatial retry stays in the same destination world. The snapshot
	// is already reconstructed and must be reused verbatim, never copied again.
	FString ExistingError;
	if (ProjectSnapshot->VerifyUnchanged(Token.Request, ExistingError)) return true;
	if (!ProjectSnapshot->ValidateDestinationLease(Token.Request, ExistingError))
	{
		Error = ExistingError;
		return false;
	}
	TArray<FProjectCalystoInventoryDestinationRequirement> Requirements;
	if (!ProjectSnapshot->GetDestinationRequirements(Token.Request, Requirements, Error)) return false;
	UGameInstance* Instance = DestinationWorld->GetGameInstance();
	auto* Roster = Instance ? Instance->GetSubsystem<UProjectRunCompanionSubsystem>() : nullptr;
	if (!Roster)
	{
		Error = TEXT("The project companion owner is unavailable for V7 destination reconstruction.");
		return false;
	}
	TArray<FProjectCalystoInventoryDestination> Destinations;
	if (!Roster->BuildV7TravelInventoryDestinations(Token.Request, DestinationWorld, Requirements, Destinations, Error)) return false;
	if (!ProjectSnapshot->ReconstructInWorld(Token.Request, DestinationWorld, Destinations, Error))
	{
		Roster->RollbackV7TravelDestinationProjections(Token.Request, DestinationWorld);
		return false;
	}
	return true;
}

bool FProjectCalystoContentAttemptProvider::RecoverDetachedSnapshot(const FEFCalystoAttemptToken& Token,
	UWorld* DestinationWorld, TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error)
{
	Error.Reset();
	if (!Token.Request.IsValid() || !Token.Attempt.IsValid() || !Snapshot)
	{
		Error = TEXT("V7 terminal gameplay recovery requires the exact owned attempt and retained snapshot.");
		return false;
	}
	if (!ReconstructPreTravelSnapshot(Token, DestinationWorld, Snapshot, Error)) return false;
	if (!Snapshot || Snapshot->IsDetachedForTravel())
	{
		Error = TEXT("V7 terminal gameplay recovery did not restore the detached destination graph.");
		return false;
	}
	// The rejected lease may be released only after the exact graph exists again
	// in the destination. FinishRequest resets Snapshot on success and retains it
	// on failure, which keeps a later explicit recovery possible.
	FinishRequest(Token.Request, Snapshot, false);
	if (Snapshot)
	{
		Error = TEXT("V7 terminal gameplay recovery restored the destination graph but could not release its rejected lease.");
		return false;
	}
	return true;
}

bool FProjectCalystoContentAttemptProvider::PrepareAttempt(const FEFCalystoContentAttemptProviderRequest& Request,
	FEFCalystoContentAttemptProviderResult& OutResult, FString& Error)
{
	OutResult={}; Error.Reset();
	if (!Request.Token.Request.IsValid() || !Request.Token.Attempt.IsValid() || !Request.World.IsValid()
		|| !Request.AttemptOwner.IsValid() || !Request.Configuration || Request.FloorNumber<1
		|| !FMath::IsFinite(Request.DeadlineSeconds) || Request.DeadlineSeconds<=0)
	{ Error=TEXT("V7 gameplay preparation requires one owned request, live world and immutable configuration."); return false; }
	TSharedPtr<FProjectCalystoGameplaySnapshot> Snapshot;
	if (Request.ExistingSnapshot)
	{
		Snapshot=ProjectCalystoContentProviderPrivate::ProjectSnapshot(Request.ExistingSnapshot, CapturedSnapshots, Error);
		if (!Snapshot)
		{
			if (Error.IsEmpty()) Error=TEXT("The retained V7 gameplay snapshot has an unsupported project type.");
			return false;
		}
		FString ExistingError;
		if (!Snapshot->VerifyUnchanged(Request.Token.Request,ExistingError)
			&& !Snapshot->ValidateDestinationLease(Request.Token.Request,ExistingError))
		{
			Error=ExistingError;
			return false;
		}
	}
	else
	{
		Error=TEXT("V7 content preparation requires the exact pre-travel snapshot captured before OpenLevel.");
		return false;
	}
	OutResult.PreFloorSnapshot=Snapshot;
	OutResult.GameplayBridge=MakeShared<FProjectCalystoContentGameplayBridge>(Snapshot.ToSharedRef());
	if (!OutResult.GameplayBridge || OutResult.PreFloorSnapshot->GetCanonicalHash().IsEmpty())
	{ Error=TEXT("V7 gameplay snapshot or typed bridge could not be retained."); return false; }
	return true;
}

bool FProjectCalystoContentAttemptProvider::BuildMaterializationContext(const FEFCalystoContentAttemptProviderRequest& Request,
	const FEFCalystoContentAttemptProviderResult& Prepared,
	TSharedRef<const FEFCalystoReservedContentManifest> Manifest,
	FEFCalystoContentGameplayContext& OutContext, FString& Error)
{
	OutContext={}; Error.Reset();
	if (!Prepared.PreFloorSnapshot || !Prepared.GameplayBridge || !Manifest->IsValid()
		|| !Request.World.IsValid() || !Request.AttemptOwner.IsValid() || Request.FloorNumber<1)
	{ Error=TEXT("A V7 materialization context requires the exact retained snapshot, bridge and manifest."); return false; }
	OutContext.Token=Request.Token; OutContext.World=Request.World; OutContext.AttemptOwner=Request.AttemptOwner;
	OutContext.Manifest=Manifest; OutContext.PreFloorSnapshot=Prepared.PreFloorSnapshot;
	OutContext.FloorNumber=Request.FloorNumber; OutContext.DeadlineSeconds=Request.DeadlineSeconds;
	for (const FEFCalystoReservedContent& Reservation : Manifest->GetElements())
	{
		if (Reservation.InventorySlot!=INDEX_NONE) continue;
		const int32 Logical=ProjectCalystoContentProviderPrivate::ResolveLogicalLevel(Request.Random,Reservation);
		const int32 Physical=UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(Logical);
		if (Logical<=0 || Physical<=0)
		{ Error=TEXT("A selected V7 actor has an invalid deterministic logical or physical level."); return false; }
		FEFCalystoFrozenActorGameplay& Actor=OutContext.Actors.Add(Reservation.Id);
		Actor.ReservationId=Reservation.Id; Actor.LogicalLevel=Logical; Actor.PhysicalLevel=Physical;
		if (Reservation.Role==EEFCalystoGameplayRole::SupportNPC)
			Actor.CompanionIdentity=ProjectCalystoContentProviderPrivate::StableIdentity(Request.Random,Reservation.Id);
	}
	return true;
}

bool FProjectCalystoContentAttemptProvider::FinishRequest(const FGuid& RequestId,
	TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, const bool bAccepted)
{
	if (!RequestId.IsValid() || !Snapshot) return false;
	FString Error;
	if (!CanHandleSnapshot(Snapshot))
	{
		UE_LOG(LogTemp, Error, TEXT("V7 Calysto gameplay snapshot release rejected: the selected project provider does not own this snapshot."));
		return false;
	}
	TSharedPtr<FProjectCalystoGameplaySnapshot> ProjectSnapshot =
		ProjectCalystoContentProviderPrivate::ProjectSnapshot(Snapshot, CapturedSnapshots, Error);
	if (!ProjectSnapshot)
	{
		UE_LOG(LogTemp, Error, TEXT("V7 Calysto gameplay snapshot release rejected: %s"), *Error);
		return false;
	}
	{
		if (!bAccepted && ProjectSnapshot->IsDetachedForTravel())
		{
			// Source objects no longer exist. The Director will reconstruct the
			// exact graph in its still-owned destination after native cleanup.
			// Retain the lease rather than treating that expected ordering as a
			// release error or discarding inventory/companions.
			UE_LOG(LogTemp, Verbose, TEXT("V7 Calysto gameplay snapshot release deferred until terminal destination recovery."));
			return false;
		}
		const bool bReleased=bAccepted
			? ProjectSnapshot->ReleaseAccepted(RequestId,Error)
			: ProjectSnapshot->Release(RequestId,Error);
		if (!bReleased)
		{
			UE_LOG(LogTemp, Error, TEXT("V7 Calysto gameplay snapshot release failed: %s"),*Error);
			// A detached graph cannot be discarded: source actors no longer exist,
			// so it remains retained for a protected recovery/reconstruction path.
			return false;
		}
	}
	CapturedSnapshots.Remove(Snapshot.Get());
	Snapshot.Reset();
	return true;
}
