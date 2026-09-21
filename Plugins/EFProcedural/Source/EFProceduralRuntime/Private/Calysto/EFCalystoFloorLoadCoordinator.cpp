#include "Calysto/EFCalystoFloorLoadCoordinator.h"

#include "Engine/AssetManager.h"

namespace EFCalystoFloorLoadCoordinatorPrivate
{
	static TArray<FSoftObjectPath> CanonicalizePaths(
		const TConstArrayView<FSoftObjectPath> AssetPaths)
	{
		TSet<FSoftObjectPath> UniquePaths;
		for (const FSoftObjectPath& Path : AssetPaths)
		{
			if (Path.IsValid())
			{
				UniquePaths.Add(Path);
			}
		}

		TArray<FSoftObjectPath> Result = UniquePaths.Array();
		Result.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right)
		{
			return Left.ToString().ToLower() < Right.ToString().ToLower();
		});
		return Result;
	}
}

FEFCalystoFloorLoadCoordinator::~FEFCalystoFloorLoadCoordinator()
{
	ResetAll();
}

bool FEFCalystoFloorLoadCoordinator::BeginPhase(
	const EEFCalystoLoadPhase Phase,
	const TConstArrayView<FSoftObjectPath> AssetPaths,
	FStreamableDelegate Completion,
	FString& OutError)
{
	OutError.Reset();
	ResetPhase(Phase);

	FPhaseLease& Lease = PhaseLeases.Add(Phase);
	Lease.Paths = EFCalystoFloorLoadCoordinatorPrivate::CanonicalizePaths(AssetPaths);
	if (Lease.Paths.IsEmpty())
	{
		Completion.ExecuteIfBound();
		return true;
	}

	Lease.Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Lease.Paths,
		MoveTemp(Completion),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("CalystoV6FloorLoad"));
	if (!Lease.Handle.IsValid())
	{
		OutError = TEXT("The asynchronous Calysto V6 phase could not acquire a streamable handle.");
		PhaseLeases.Remove(Phase);
		return false;
	}
	return true;
}

bool FEFCalystoFloorLoadCoordinator::IsPhaseReady(
	const EEFCalystoLoadPhase Phase,
	FString& OutError) const
{
	return GetPhaseState(Phase, OutError) == EEFCalystoLoadPhaseState::Ready;
}

EEFCalystoLoadPhaseState FEFCalystoFloorLoadCoordinator::GetPhaseState(
	const EEFCalystoLoadPhase Phase,
	FString& OutError) const
{
	OutError.Reset();
	const FPhaseLease* Lease = PhaseLeases.Find(Phase);
	if (!Lease)
	{
		OutError = TEXT("The requested Calysto V6 load phase has not started.");
		return EEFCalystoLoadPhaseState::NotStarted;
	}
	if (Lease->Handle.IsValid() && !Lease->Handle->HasLoadCompleted())
	{
		OutError = TEXT("The requested Calysto V6 load phase is still in progress.");
		return EEFCalystoLoadPhaseState::Loading;
	}
	for (const FSoftObjectPath& Path : Lease->Paths)
	{
		if (!Path.ResolveObject())
		{
			OutError = FString::Printf(
				TEXT("Calysto V6 load phase completed without resolving %s."),
				*Path.ToString());
			return EEFCalystoLoadPhaseState::Failed;
		}
	}
	return EEFCalystoLoadPhaseState::Ready;
}

TArray<FSoftObjectPath> FEFCalystoFloorLoadCoordinator::GetPhasePaths(
	const EEFCalystoLoadPhase Phase) const
{
	if (const FPhaseLease* Lease = PhaseLeases.Find(Phase))
	{
		return Lease->Paths;
	}
	return {};
}

bool FEFCalystoFloorLoadCoordinator::HasPhase(const EEFCalystoLoadPhase Phase) const
{
	return PhaseLeases.Contains(Phase);
}

void FEFCalystoFloorLoadCoordinator::ResetFloor()
{
	ResetPhase(EEFCalystoLoadPhase::FloorVisual);
	ResetPhase(EEFCalystoLoadPhase::PostTopologyContent);
	ResetPhase(EEFCalystoLoadPhase::OptionalDecals);
}

void FEFCalystoFloorLoadCoordinator::ResetAll()
{
	ResetFloor();
	ResetPhase(EEFCalystoLoadPhase::SessionCore);
}

void FEFCalystoFloorLoadCoordinator::ResetPhase(const EEFCalystoLoadPhase Phase)
{
	if (FPhaseLease* Lease = PhaseLeases.Find(Phase))
	{
		if (Lease->Handle.IsValid() && !Lease->Handle->HasLoadCompleted())
		{
			Lease->Handle->CancelHandle();
		}
	}
	PhaseLeases.Remove(Phase);
}
