#include "Calysto/EFCalystoPCGInternalClosureEditorLibrary.h"

#include "Calysto/EFCalystoPCGCookedCompatibility.h"
#include "PCGGraph.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoPCGInternalClosureEditorLibraryPrivate
{
	static constexpr TCHAR SourceSetDungeonMeshPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh");
	static constexpr TCHAR SourceAddRampsPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps");
	static constexpr TCHAR LegacySimplePath[] =
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple");
	static constexpr TCHAR CookedSimplePath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon");
	static constexpr TCHAR InternalSetDungeonMeshPath[] =
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe");
	static constexpr TCHAR InternalAddRampsPath[] =
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe");

	static UPCGGraph* LoadExactGraph(const TCHAR* Path, FString& OutError)
	{
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, Path);
		if (!IsValid(Graph) || Graph->GetPathName() != Path)
		{
			OutError = FString::Printf(
				TEXT("Unable to load exact PCG graph %s (resolved %s)."),
				Path,
				*GetPathNameSafe(Graph));
			return nullptr;
		}
		return Graph;
	}

	static bool LoadValidationClosure(
		UPCGGraph*& OutInternalSetDungeonMesh,
		UPCGGraph*& OutInternalAddRamps,
		FString& OutError)
	{
		// The runtime validator is deliberately find-only. This Editor bridge owns
		// all synchronous loads before it invokes that shared structural contract.
		for (const TCHAR* Path : {
			SourceSetDungeonMeshPath,
			SourceAddRampsPath,
			LegacySimplePath,
			CookedSimplePath })
		{
			if (!LoadExactGraph(Path, OutError))
			{
				return false;
			}
		}
		OutInternalSetDungeonMesh = LoadExactGraph(InternalSetDungeonMeshPath, OutError);
		OutInternalAddRamps = LoadExactGraph(InternalAddRampsPath, OutError);
		return IsValid(OutInternalSetDungeonMesh) && IsValid(OutInternalAddRamps);
	}
}

FString UEFCalystoPCGInternalClosureEditorLibrary::PrepareNewInternalCookedClosure()
{
	using namespace EFCalystoPCGInternalClosureEditorLibraryPrivate;
	FString Error;
	UPCGGraph* InternalSetDungeonMesh = nullptr;
	UPCGGraph* InternalAddRamps = nullptr;
	if (!LoadValidationClosure(InternalSetDungeonMesh, InternalAddRamps, Error)
		|| !FEFCalystoPCGCookedCompatibility::PrepareInternalClosureForEditor(
			InternalSetDungeonMesh,
			InternalAddRamps,
			Error))
	{
		return Error.IsEmpty()
			? TEXT("Calysto V6 internal PCG closure preparation failed without diagnostics.")
			: Error;
	}
	return FString();
}

FString UEFCalystoPCGInternalClosureEditorLibrary::ValidateInternalCookedClosure()
{
	using namespace EFCalystoPCGInternalClosureEditorLibraryPrivate;
	FString Error;
	UPCGGraph* InternalSetDungeonMesh = nullptr;
	UPCGGraph* InternalAddRamps = nullptr;
	if (!LoadValidationClosure(InternalSetDungeonMesh, InternalAddRamps, Error)
		|| !FEFCalystoPCGCookedCompatibility::ValidateResidentInternalClosure(Error))
	{
		return Error.IsEmpty()
			? TEXT("Calysto V6 internal PCG closure validation failed without diagnostics.")
			: Error;
	}
	return FString();
}
