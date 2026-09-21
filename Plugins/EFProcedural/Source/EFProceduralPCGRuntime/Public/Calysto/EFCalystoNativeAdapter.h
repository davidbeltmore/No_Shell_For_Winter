#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoAssignRoomTheme.h"
#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "PCGCommon.h"

class AActor;
class UPCGComponent;
class UPCGGraph;
class UWorld;
struct FPCGDataCollection;

enum class EEFCalystoNativeStatus : uint8 { Pending, Complete, Failed };
/** Native failures are classified at their source so transactional recovery
 * never turns an invalid graph/schema/provenance contract into seed retries. */
enum class EEFCalystoNativeFailureKind : uint8 { Spatial, Configuration, Resource };

/** An opportunity, not a placement reservation or proof of collision/navigation feasibility. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeSurface
{
	int64 RoomId = 0;
	int64 OpportunityId = 0;
	EEFCalystoPlacementZone Zone = EEFCalystoPlacementZone::Floor;
	FTransform Transform;
	FVector Normal = FVector::UpVector;
	FBox Bounds = FBox(ForceInit);
};

/** Exact native upper-extension output, retained one-for-one even for identical transforms. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeWallExtension
{
	FSoftObjectPath Mesh;
	FTransform WorldTransform;
};

/**
 * One final native wall-spawner point.  This is provenance, rather than a
 * spatial ownership guess: the mesh, transform, emitting room and effective
 * material must all bind one-to-one to the generated structural instance.
 */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeWallSurface
{
	FSoftObjectPath Mesh;
	FTransform WorldTransform;
	int64 RoomId = 0;
	FSoftObjectPath Material;
	bool bZeroHeightExtension = false;
	/** Exact default/corridor Difference branch with no room owner; material is frozen Style authority. */
	bool bStyleOwned = false;
	/** Exact final Wall - Door branch. Its per-piece carrier remains the material authority because a shared doorway can validly span room materials. */
	bool bDoorway = false;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeResult
{
	TArray<FEFCalystoNativeRoom> Rooms;
	TArray<FEFCalystoNativeSurface> Surfaces;
	/** Complete final-wall stream from the transient native graph, including normal-height walls. */
	TArray<FEFCalystoNativeWallSurface> NativeFinalWallSurfaces;
	/** Provenance records must be consumed once per matching owned zero-height wall instance. */
	TArray<FEFCalystoNativeWallExtension> NativeZeroHeightWallExtensions;
	/** Exact PCG-managed actors matching the reflected native Start class and explicit travel End class. */
	TArray<TWeakObjectPtr<AActor>> NativeStartMarkers;
	TArray<TWeakObjectPtr<AActor>> NativeEndMarkers;
	/** Exact actor references produced by the native wall-light spawn branch. */
	TArray<TWeakObjectPtr<AActor>> NativeWallLights;
	int32 RequestedWallLightCount = 0;
	bool bLightingVerified = false;
	FBox StructuralBounds = FBox(ForceInit);
	int32 FloorInstances = 0;
	int32 WallInstances = 0;
	int32 RoofInstances = 0;
	int32 GenerateRequests = 0;
	/** Graph completion does not establish usable navigation or gameplay readiness. */
	bool bGraphCompleted = false;
};

/** Direct native Calysto boundary; it neither selects a Style nor accesses a Director subsystem. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeAdapter final
{
public:
	static TArray<FSoftObjectPath> GetSharedDependencies();
	static AActor* SpawnGenerator(UWorld* World, FString& Error);
	/** Read-only native component selection. Editor-only PCG auxiliaries must also be idle/on-demand. */
	static UPCGComponent* FindIdleRuntimeComponent(AActor* DungeonActor, FString& Error);
	static bool ValidateCapabilities(const FEFCalystoCompiledDirector& Compiled, const FGuid& StyleId, FString& Error);
	static UPCGGraph* ComposeGraph(UPCGGraph* Source, UObject* TransientOwner,
		const FEFCalystoNativeRoomConfig& Config, FString& Error);
	static TSharedPtr<FEFCalystoNativeAdapter> Prepare(AActor* DungeonActor,
		TSharedRef<const FEFCalystoCompiledDirector> Compiled, const FEFCalystoRandomKey& Key,
		int32 TopologySeed, UClass* ProgressionDoorClass, FString& Error);
	~FEFCalystoNativeAdapter();
	FEFCalystoNativeAdapter(const FEFCalystoNativeAdapter&) = delete;
	FEFCalystoNativeAdapter& operator=(const FEFCalystoNativeAdapter&) = delete;
	FPCGTaskId GenerateOnce(FString& Error);
	EEFCalystoNativeStatus Observe(FEFCalystoNativeResult& Result, FString& Error) const;
	EEFCalystoNativeStatus Observe(FEFCalystoNativeResult& Result, FString& Error,
		EEFCalystoNativeFailureKind& OutFailure) const;
	void BeginCleanup();
	bool IsCleanupComplete() const;
	UPCGComponent* GetComponent() const;
	AActor* GetDungeonActor() const;
	UPCGGraph* GetGraph() const;
	int32 GetGenerateRequestCount() const;
	/** Rechecks current native property readback and all controlled light-component intensities. */
	bool VerifyLighting(FString& Error) const;
	const FEFCalystoNativeRoomConfig& GetRoomConfig() const;
	/** Read-only capability for the audited native upper-wall extension stream.
	 * False with no error means valid unequal heights; this never authorizes floor/roof degenerates. */
	bool CanContainZeroHeightWallExtensions(FString& Error) const;
	static bool ValidateGraph(const UPCGGraph* Graph, FString& Error);
private:
	friend class FEFCalystoNativeWallExtensionContractTest;
	static bool ValidateZeroHeightWallExtensionContract(const UPCGGraph* MeshGraph,
		double WallHeight, double InitialWallHeight, FString& Error);
	static bool ReadFinalWallSurfaces(const FPCGDataCollection& Output, bool bZeroHeightCapabilityVerified,
		TArray<FEFCalystoNativeWallSurface>& Surfaces,
		TArray<FEFCalystoNativeWallExtension>& ZeroHeightExtensions, FString& Error);
	bool ObserveLighting(FEFCalystoNativeResult& Result, FString& Error) const;
	FEFCalystoNativeAdapter();
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
