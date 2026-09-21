#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"

class AActor;
class ANavigationData;
class AEFCalystoNavigationBoundsVolume;
class UPCGComponent;
class UStaticMeshComponent;
class UNavigationSystemV1;
class UWorld;

enum class EEFCalystoStructuralRole : uint8 { Floor, Wall, Roof, Ramp, OtherStructure };
enum class EEFCalystoNavigationObservation : uint8
{
	Pending, Ready, RecoverableSpatialFailure, ConfigurationFailure, Cancelled
};

/** Construct from the selected native mesh schema; never classify by a mesh name or tag. */
struct FEFCalystoStructuralMeshRule
{
	FSoftObjectPath Mesh;
	EEFCalystoStructuralRole Role = EEFCalystoStructuralRole::OtherStructure;
};

struct FEFCalystoStructuralSource
{
	TWeakObjectPtr<UStaticMeshComponent> Component;
	EEFCalystoStructuralRole Role = EEFCalystoStructuralRole::OtherStructure;
	/** Exact final-wall provenance copied by index through any transient material partition. */
	TMap<int32, int64> NativeWallRoomIds;
	TMap<int32, FSoftObjectPath> NativeWallMaterials;
	/** Exact final Wall - Door records. Their carrier-restored per-piece material
	 * is authoritative; a shared doorway cannot be reduced to one room material. */
	TSet<int32> NativeDoorWallIndices;
	/** Exact final-wall indices from the audited native Difference branch.  They
	 * have no room owner and must use the frozen selected Style wall material. */
	TSet<int32> NativeStyleOwnedWallIndices;
	/** Exact indices matched to the adapter's validated native upper-wall output.
	 * A wall role or mesh reference alone never permits degenerate geometry. */
	TSet<int32> NativeZeroHeightWallExtensionIndices;
};

struct FEFCalystoStructuralComponentEvidence
{
	FString ComponentPath;
	FSoftObjectPath Mesh;
	EEFCalystoStructuralRole Role = EEFCalystoStructuralRole::OtherStructure;
	TArray<FTransform> WorldInstanceTransforms;
	TArray<int32> NativeZeroHeightWallExtensionIndices;
	FBox WorldBounds = FBox(ForceInit);
	bool bRegistered = false;
	bool bQueryCollision = false;
	bool bBlocksPawn = false;
	bool bPhysicsStateCreated = false;
	bool bNavigationRelevant = false;
};

struct FEFCalystoStructuralEvidence
{
	TArray<FEFCalystoStructuralComponentEvidence> Components;
	FBox StructuralBounds = FBox(ForceInit);
	FBox WalkableGeometryBounds = FBox(ForceInit);
	int32 FloorInstanceCount = 0;
	int32 WallInstanceCount = 0;
	int32 RoofInstanceCount = 0;
	int32 NativeZeroHeightWallExtensionCount = 0;
	int32 TotalInstanceCount = 0;
	uint32 ObservationFingerprint = 0;
	bool bWalkableComponentsReady = false;
};

/** Room ownership is supplied by the native adapter's frozen room output. */
struct FEFCalystoDesignatedRoom
{
	int64 StableRoomId = 0;
	FTransform RoomToWorld = FTransform::Identity;
	FBox LocalBounds = FBox(ForceInit);
	bool ContainsWorldPoint(const FVector& Point, double Tolerance = 2.0) const;
};

struct FEFCalystoStructuralNavigationInput
{
	FGuid AttemptId;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> DungeonOwner;
	TArray<FEFCalystoStructuralSource> StructuralSources;
	// These arrays must be the complete native owned marker sets, not prefiltered winners.
	TArray<TWeakObjectPtr<AActor>> StartMarkers;
	TArray<TWeakObjectPtr<AActor>> EndMarkers;
	FEFCalystoDesignatedRoom StartRoom;
	FEFCalystoDesignatedRoom EndRoom;
	// The native travel adapter owns the door's approach point. No room search occurs here.
	FVector EndApproachWorld = FVector::ZeroVector;
	TWeakObjectPtr<AActor> ProtectedPlayer;
	FNavAgentProperties AgentProperties;
	float CapsuleRadius = 42.0f;
	float CapsuleHalfHeight = 96.0f;
	float FloorClearance = 2.0f;
	float MaximumFloorProbeUp = 40.0f;
	float MaximumFloorProbeDown = 300.0f;
	float MaximumProjectionHorizontal = 50.0f;
	float MaximumProjectionVertical = 100.0f;
	float MaximumWalkableSlopeDegrees = 45.0f;
	FVector NavigationPadding = FVector(200.0, 200.0, 200.0);
	double RequestDeadlineSeconds = 0.0; // Absolute FPlatformTime::Seconds, shared by all attempts.
	double StructuralSettleSeconds = 0.25;
	double RegistrationGraceSeconds = 1.0;
	int32 MaximumComponents = 1024;
	int32 MaximumInstances = 100000;
};

struct FEFCalystoStructuralNavigationResult
{
	EEFCalystoNavigationObservation State = EEFCalystoNavigationObservation::Pending;
	FName Code;
	FString Message;
	FEFCalystoStructuralEvidence Structure;
	FBox RegisteredNavigationBounds = FBox(ForceInit);
	FString NavigationDataPath;
	FTransform ValidatedEntryTransform = FTransform::Identity;
	FVector ValidatedEndApproach = FVector::ZeroVector;
	TArray<FVector> CompleteRoute;
	uint64 StartNavigationNode = 0;
	uint64 EndNavigationNode = 0;
	bool bEntryTransformValid = false;
	int32 ActiveNavigationTiles = INDEX_NONE;
	float NavigationAgentRadius = -1;
	float NavigationAgentHeight = -1;
	float NavigationDefaultCellSize = -1;
	bool bBoundsRegistered = false;
	bool bNavigationBuilding = false;
	bool bStartProjected = false;
	bool bEndProjected = false;
};

/** One game-thread, attempt-owned asynchronous observation session. Does not generate geometry. */
class FEFCalystoStructuralNavigation final
{
public:
	FEFCalystoStructuralNavigation();
	~FEFCalystoStructuralNavigation();
	FEFCalystoStructuralNavigation(const FEFCalystoStructuralNavigation&) = delete;
	FEFCalystoStructuralNavigation& operator=(const FEFCalystoStructuralNavigation&) = delete;

	// Pure preflight: repeated mesh/role pairs collapse; one mesh with conflicting roles is invalid.
	static bool NormalizeMeshRules(TConstArrayView<FEFCalystoStructuralMeshRule> MeshRules,
		TArray<FEFCalystoStructuralMeshRule>& OutRules, FString& OutError);
	// Includes only the exact dungeon actor's components and the controlled PCG's managed resources.
	static bool GatherNativeSources(AActor& DungeonOwner, UPCGComponent* ControlledPCG,
		TConstArrayView<FEFCalystoStructuralMeshRule> MeshRules,
		TArray<FEFCalystoStructuralSource>& OutSources, FString& OutError);
	static bool CollectStructuralEvidence(TConstArrayView<FEFCalystoStructuralSource> Sources,
		int32 MaximumComponents, int32 MaximumInstances,
		FEFCalystoStructuralEvidence& OutEvidence, FString& OutError);
	static bool ValidateInput(const FEFCalystoStructuralNavigationInput& Input, FString& OutError);
	static FBox ComputeNavigationBounds(const FEFCalystoStructuralEvidence& Evidence, const FVector& Padding);

	const FEFCalystoStructuralNavigationResult& Observe(
		const FEFCalystoStructuralNavigationInput& Input, double NowSeconds, bool bNativeGenerationSettled);
	// Cancel before starting another attempt. The owner waits for IsReleased before regenerating.
	void Cancel();
	bool IsReleased() const;
	const FEFCalystoStructuralNavigationResult& GetResult() const { return Result; }

private:
	struct FAsyncState;
	TSharedPtr<FAsyncState> Async;
	FEFCalystoStructuralNavigationResult Result;
	FGuid AttemptId;
	TWeakObjectPtr<UNavigationSystemV1> NavigationSystem;
	TWeakObjectPtr<AEFCalystoNavigationBoundsVolume> BoundsVolume;
	TWeakObjectPtr<ANavigationData> NavigationData;
	FVector FrozenStart = FVector::ZeroVector;
	FVector FrozenEnd = FVector::ZeroVector;
	FVector FrozenStartMarkerLocation = FVector::ZeroVector;
	FVector FrozenEndMarkerLocation = FVector::ZeroVector;
	int64 FrozenStartRoomId = 0;
	int64 FrozenEndRoomId = 0;
	FTransform CandidateEntry = FTransform::Identity;
	double FirstSettledObservation = -1.0;
	double LastStructuralChange = -1.0;
	double BoundsRegisteredAt = -1.0;
	uint32 LastFingerprint = 0;
	uint32 BoundsRegistrationId = 0;
	bool bPrepared = false;
	bool bCancelled = false;

	void SetResult(EEFCalystoNavigationObservation State, FName Code, const FString& Message);
	bool VerifyEndpoint(const FEFCalystoStructuralNavigationInput& Input, const FVector& ProbeOrigin,
		const FEFCalystoDesignatedRoom& Room, FVector& OutFloor, FString& OutError) const;
	bool PrepareNavigation(const FEFCalystoStructuralNavigationInput& Input, double NowSeconds);
	bool ReleaseNavigation();
};
