#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoArchitecturePlanner.h"
#include "Calysto/EFCalystoArchitectureMeshBatch.h"
#include "Calysto/EFCalystoBakedArchitecture.h"

/** Frozen children include explicitly absent native baked opportunities for honest statistics. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoReservedArchitecture
{
	FGuid Id;
	FGuid EntryId;
	int64 RoomId=0;
	FBox Bounds=FBox(ForceInit);
	TArray<FEFCalystoBakedDecision> BakedChildren;
	TSet<FGuid> SelectedChildren;
};

/** One immutable publication boundary between native-slot planning and actual ISM realization.
 * Build expands only loaded, already reserved mesh/baked payloads. No loads, graph execution,
 * world searches or parent re-selection; all resource/envelope checks precede child trials. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoArchitectureManifest final
{
public:
	static bool Build(const FEFCalystoRandomKey& Random,TConstArrayView<FEFCalystoArchitectureDecision> Decisions,
		FEFCalystoArchitectureManifest& Manifest,FString& Error);
	bool IsValid() const { return bValid; }
	const TArray<FEFCalystoFrozenArchitectureMesh>& GetMeshes() const { return Meshes; }
	const TArray<FEFCalystoReservedArchitecture>& GetParents() const { return Parents; }
	const TSet<FGuid>& GetReservedElements() const { return ReservedElements; }
	const FString& GetHash() const { return Hash; }
	/** Every selected child must have verified actual realization; extra/untracked identities fail too. */
	bool VerifyRealizedChildren(const TSet<FGuid>& ActualChildren,TSet<FGuid>& Verified,FString& Error) const;
private:
	bool bValid=false;
	FString Hash;
	TArray<FEFCalystoFrozenArchitectureMesh> Meshes;
	TArray<FEFCalystoReservedArchitecture> Parents;
	TSet<FGuid> ReservedElements;
};
