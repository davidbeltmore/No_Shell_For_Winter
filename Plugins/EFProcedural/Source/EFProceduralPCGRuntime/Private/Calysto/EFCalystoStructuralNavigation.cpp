#include "Calysto/EFCalystoStructuralNavigation.h"

#include "Calysto/EFCalystoNavigationBoundsVolume.h"
#include "AI/Navigation/NavigationBounds.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"

namespace EFCalystoStructuralNavigationPrivate
{
	bool FiniteBox(const FBox& Box)
	{
		return Box.IsValid && !Box.Min.ContainsNaN() && !Box.Max.ContainsNaN()
			&& Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
	}

	bool IsFloor(const EEFCalystoStructuralRole Role)
	{
		return Role == EEFCalystoStructuralRole::Floor || Role == EEFCalystoStructuralRole::Ramp;
	}

	const TCHAR* RoleName(const EEFCalystoStructuralRole Role)
	{
		switch (Role)
		{
		case EEFCalystoStructuralRole::Floor: return TEXT("Floor");
		case EEFCalystoStructuralRole::Wall: return TEXT("Wall");
		case EEFCalystoStructuralRole::Roof: return TEXT("Roof");
		case EEFCalystoStructuralRole::Ramp: return TEXT("Ramp");
		case EEFCalystoStructuralRole::OtherStructure: return TEXT("OtherStructure");
		default: return nullptr;
		}
	}

	void FingerprintVector(uint32& Hash, const FVector& Vector)
	{
		const double Values[] = {Vector.X, Vector.Y, Vector.Z};
		Hash = FCrc::MemCrc32(Values, sizeof(Values), Hash);
	}
}

struct FEFCalystoStructuralNavigation::FAsyncState
{
	FGuid AttemptId;
	FNavPathSharedPtr Path;
	ENavigationQueryResult::Type QueryResult = ENavigationQueryResult::Invalid;
	uint32 QueryId = 0;
	uint64 NavigationRevision = 0;
	uint64 QueryNavigationRevision = 0;
	double LastNavigationEventSeconds = 0.0;
	bool bQueryPending = false;
	bool bQueryCompleted = false;
	bool bCancelled = false;
};

bool FEFCalystoDesignatedRoom::ContainsWorldPoint(const FVector& Point, const double Tolerance) const
{
	return StableRoomId != 0 && EFCalystoStructuralNavigationPrivate::FiniteBox(LocalBounds)
		&& !RoomToWorld.ContainsNaN() && !Point.ContainsNaN()
		&& FMath::IsFinite(Tolerance) && Tolerance >= 0.0
		&& LocalBounds.ExpandBy(Tolerance).IsInsideOrOn(RoomToWorld.InverseTransformPosition(Point));
}

FEFCalystoStructuralNavigation::FEFCalystoStructuralNavigation() : Async(MakeShared<FAsyncState>()) {}

FEFCalystoStructuralNavigation::~FEFCalystoStructuralNavigation()
{
	ReleaseNavigation();
}

bool FEFCalystoStructuralNavigation::NormalizeMeshRules(
	const TConstArrayView<FEFCalystoStructuralMeshRule> MeshRules,
	TArray<FEFCalystoStructuralMeshRule>& OutRules, FString& OutError)
{
	using namespace EFCalystoStructuralNavigationPrivate;
	OutError.Reset();
	TMap<FSoftObjectPath, EEFCalystoStructuralRole> Roles;
	for (const FEFCalystoStructuralMeshRule& Rule : MeshRules)
	{
		if (!Rule.Mesh.IsValid() || !RoleName(Rule.Role))
		{
			OutError = FString::Printf(TEXT("Structural mesh rules require explicit native mesh paths and supported roles: mesh=%s role=%d."),
				*Rule.Mesh.ToString(), int32(Rule.Role));
			OutRules.Reset();
			return false;
		}
		if (const EEFCalystoStructuralRole* Existing = Roles.Find(Rule.Mesh))
		{
			if (*Existing != Rule.Role)
			{
				OutError = FString::Printf(TEXT("Structural mesh %s has conflicting roles %s and %s."),
					*Rule.Mesh.ToString(), RoleName(*Existing), RoleName(Rule.Role));
				OutRules.Reset();
				return false;
			}
			continue;
		}
		Roles.Add(Rule.Mesh, Rule.Role);
	}
	OutRules.Reset();
	if (Roles.IsEmpty())
	{
		OutError = TEXT("Structural collection requires a nonempty native mesh schema.");
		return false;
	}
	for (const auto& Pair : Roles) OutRules.Add({Pair.Key, Pair.Value});
	OutRules.Sort([](const FEFCalystoStructuralMeshRule& A, const FEFCalystoStructuralMeshRule& B)
	{
		return A.Mesh.ToString() < B.Mesh.ToString();
	});
	return true;
}

bool FEFCalystoStructuralNavigation::GatherNativeSources(AActor& DungeonOwner, UPCGComponent* ControlledPCG,
	const TConstArrayView<FEFCalystoStructuralMeshRule> MeshRules,
	TArray<FEFCalystoStructuralSource>& OutSources, FString& OutError)
{
	check(IsInGameThread());
	OutSources.Reset();
	TArray<FEFCalystoStructuralMeshRule> Normalized;
	if (!NormalizeMeshRules(MeshRules, Normalized, OutError)) return false;
	TMap<FSoftObjectPath, EEFCalystoStructuralRole> Roles;
	for (const FEFCalystoStructuralMeshRule& Rule : Normalized) Roles.Add(Rule.Mesh, Rule.Role);
	if (ControlledPCG && ControlledPCG->GetOwner() != &DungeonOwner)
	{
		OutError = TEXT("Structural collection requires its exact controlled dungeon owner.");
		return false;
	}
	TSet<UStaticMeshComponent*> Seen;
	auto AddComponent = [&](UActorComponent* Component)
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
		if (!IsValid(MeshComponent) || !MeshComponent->GetStaticMesh()
			|| MeshComponent->GetWorld() != DungeonOwner.GetWorld() || Seen.Contains(MeshComponent)) return;
		if (const EEFCalystoStructuralRole* Role = Roles.Find(FSoftObjectPath(MeshComponent->GetStaticMesh())))
		{
			Seen.Add(MeshComponent);
			OutSources.Add({MeshComponent, *Role});
		}
	};
	auto AddActorComponents = [&](AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->GetWorld() != DungeonOwner.GetWorld()) return;
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		for (UStaticMeshComponent* Component : Components) AddComponent(Component);
	};
	AddActorComponents(&DungeonOwner);
	if (ControlledPCG)
	{
		if (!ControlledPCG->AreManagedResourcesAccessible())
		{
			OutError = TEXT("The controlled PCG resources are still being updated.");
			OutSources.Reset();
			return false;
		}
		ControlledPCG->ForEachConstManagedResource([&](const UPCGManagedResource* Resource)
		{
			if (!Resource || Resource->IsMarkedUnused()) return;
			if (const UPCGManagedComponent* Single = Cast<UPCGManagedComponent>(Resource))
				AddComponent(Single->GeneratedComponent.Get());
			else if (const UPCGManagedComponentList* List = Cast<UPCGManagedComponentList>(Resource))
				for (const TSoftObjectPtr<UActorComponent>& Component : List->GeneratedComponents) AddComponent(Component.Get());
			else if (const UPCGManagedActors* Actors = Cast<UPCGManagedActors>(Resource))
				for (const TSoftObjectPtr<AActor>& Actor : Actors->GetConstGeneratedActors()) AddActorComponents(Actor.Get());
		});
	}
	OutSources.Sort([](const FEFCalystoStructuralSource& A, const FEFCalystoStructuralSource& B)
	{
		return A.Component->GetPathName() < B.Component->GetPathName();
	});
	return true;
}

bool FEFCalystoStructuralNavigation::CollectStructuralEvidence(
	const TConstArrayView<FEFCalystoStructuralSource> Sources, const int32 MaximumComponents,
	const int32 MaximumInstances, FEFCalystoStructuralEvidence& OutEvidence, FString& OutError)
{
	check(IsInGameThread());
	using namespace EFCalystoStructuralNavigationPrivate;
	OutEvidence = {};
	OutError.Reset();
	if (MaximumComponents < 1 || MaximumInstances < 1 || Sources.Num() > MaximumComponents)
	{
		OutError = TEXT("Structural evidence exceeds its configured finite component capacity.");
		return false;
	}
	bool bAllFloorComponentsReady = true;
	TSet<UStaticMeshComponent*> Seen;
	for (const FEFCalystoStructuralSource& Source : Sources)
	{
		UStaticMeshComponent* Component = Source.Component.Get();
		if (!IsValid(Component) || !Component->GetStaticMesh() || Seen.Contains(Component))
		{
			OutError = TEXT("Structural sources contain a missing mesh component or duplicate binding.");
			return false;
		}
		Seen.Add(Component);
		FEFCalystoStructuralComponentEvidence& Evidence = OutEvidence.Components.AddDefaulted_GetRef();
		Evidence.ComponentPath = Component->GetPathName();
		Evidence.Mesh = FSoftObjectPath(Component->GetStaticMesh());
		Evidence.Role = Source.Role;
		Evidence.bRegistered = Component->IsRegistered();
		Evidence.bQueryCollision = Component->IsQueryCollisionEnabled();
		Evidence.bBlocksPawn = Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
		Evidence.bPhysicsStateCreated = Component->IsPhysicsStateCreated();
		Evidence.bNavigationRelevant = Component->CanEverAffectNavigation() && Component->IsNavigationRelevant();
		const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component);
		const int32 Count = Instances ? Instances->GetInstanceCount() : 1;
		if (Count < 0 || Count > MaximumInstances - OutEvidence.TotalInstanceCount)
		{
			OutError = TEXT("Structural evidence exceeds its configured finite instance capacity.");
			return false;
		}
		for (const int32 ExtensionIndex : Source.NativeZeroHeightWallExtensionIndices)
		{
			if (!Instances || Source.Role != EEFCalystoStructuralRole::Wall || ExtensionIndex < 0 || ExtensionIndex >= Count)
			{
				OutError = TEXT("Native zero-height wall extension evidence has an invalid component, role or instance index.");
				return false;
			}
		}
		const FBox MeshBounds = Component->GetStaticMesh()->GetBoundingBox();
		if (!FiniteBox(MeshBounds))
		{
			OutError = FString::Printf(TEXT("Structural mesh has invalid local bounds: %s."), *Evidence.Mesh.ToString());
			return false;
		}
		Evidence.WorldInstanceTransforms.Reserve(Count);
		int32 VolumeInstanceCount = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FTransform Transform = Component->GetComponentTransform();
			if ((Instances && !Instances->GetInstanceTransform(Index, Transform, true))
				|| Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized())
			{
				OutError = FString::Printf(TEXT("Structural component has an invalid instance transform: %s [%d]."), *Evidence.ComponentPath, Index);
				return false;
			}
			Evidence.WorldInstanceTransforms.Add(Transform);
			const FVector Scale = Transform.GetScale3D();
			if (Source.NativeZeroHeightWallExtensionIndices.Contains(Index))
			{
				if (Scale.Z != 0.0 || FMath::Abs(Scale.X) <= UE_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER)
				{
					OutError = TEXT("The frozen native upper-wall extension no longer has its proved zero-height transform.");
					return false;
				}
				Evidence.NativeZeroHeightWallExtensionIndices.Add(Index);
				++OutEvidence.NativeZeroHeightWallExtensionCount;
				continue;
			}
			if (Scale.GetAbsMin() <= UE_SMALL_NUMBER)
			{
				OutError = FString::Printf(TEXT("Structural component has an unproved degenerate instance transform: %s [%d]."), *Evidence.ComponentPath, Index);
				return false;
			}
			++VolumeInstanceCount;
			Evidence.WorldBounds += MeshBounds.TransformBy(Transform);
		}
		OutEvidence.TotalInstanceCount += Count;
		if (VolumeInstanceCount > 0)
		{
			OutEvidence.StructuralBounds += Evidence.WorldBounds;
			if (Source.Role == EEFCalystoStructuralRole::Floor)
			{
				OutEvidence.FloorInstanceCount += VolumeInstanceCount;
				bAllFloorComponentsReady &= Evidence.bRegistered && Evidence.bQueryCollision
					&& Evidence.bBlocksPawn && Evidence.bPhysicsStateCreated && Evidence.bNavigationRelevant;
			}
			else if (Source.Role == EEFCalystoStructuralRole::Wall) OutEvidence.WallInstanceCount += VolumeInstanceCount;
			else if (Source.Role == EEFCalystoStructuralRole::Roof) OutEvidence.RoofInstanceCount += VolumeInstanceCount;
			// Native ramps may include intentionally nonblocking trim. Record it, but only
			// collision/navigation-enabled Floor or Ramp geometry contributes walkable bounds.
			if (IsFloor(Source.Role) && Evidence.bQueryCollision && Evidence.bBlocksPawn && Evidence.bNavigationRelevant)
				OutEvidence.WalkableGeometryBounds += Evidence.WorldBounds;
		}
	}
	OutEvidence.Components.Sort([](const FEFCalystoStructuralComponentEvidence& A, const FEFCalystoStructuralComponentEvidence& B)
	{
		return A.ComponentPath < B.ComponentPath;
	});
	uint32 Fingerprint = 0;
	for (const FEFCalystoStructuralComponentEvidence& Evidence : OutEvidence.Components)
	{
		Fingerprint = FCrc::StrCrc32(*Evidence.ComponentPath, Fingerprint);
		Fingerprint = FCrc::StrCrc32(*Evidence.Mesh.ToString(), Fingerprint);
		const uint8 Flags[] = {static_cast<uint8>(Evidence.Role), Evidence.bRegistered,
			Evidence.bQueryCollision, Evidence.bBlocksPawn, Evidence.bPhysicsStateCreated, Evidence.bNavigationRelevant};
		Fingerprint = FCrc::MemCrc32(Flags, sizeof(Flags), Fingerprint);
		for (const FTransform& Transform : Evidence.WorldInstanceTransforms)
		{
			FingerprintVector(Fingerprint, Transform.GetLocation());
			FingerprintVector(Fingerprint, Transform.GetScale3D());
			const FQuat Rotation = Transform.GetRotation();
			const double Values[] = {Rotation.X, Rotation.Y, Rotation.Z, Rotation.W};
			Fingerprint = FCrc::MemCrc32(Values, sizeof(Values), Fingerprint);
		}
	}
	OutEvidence.ObservationFingerprint = Fingerprint;
	OutEvidence.bWalkableComponentsReady = bAllFloorComponentsReady && OutEvidence.FloorInstanceCount > 0;
	return true;
}

bool FEFCalystoStructuralNavigation::ValidateInput(const FEFCalystoStructuralNavigationInput& Input, FString& OutError)
{
	using namespace EFCalystoStructuralNavigationPrivate;
	OutError.Reset();
	if (!Input.AttemptId.IsValid() || !Input.World.IsValid() || !Input.DungeonOwner.IsValid()
		|| Input.DungeonOwner->GetWorld() != Input.World.Get())
	{
		OutError = TEXT("Navigation observation requires the current attempt, world and native dungeon owner.");
		return false;
	}
	if (!FMath::IsFinite(Input.RequestDeadlineSeconds) || Input.RequestDeadlineSeconds <= 0.0
		|| !FMath::IsFinite(Input.StructuralSettleSeconds) || Input.StructuralSettleSeconds < 0.0 || Input.StructuralSettleSeconds > 5.0
		|| !FMath::IsFinite(Input.RegistrationGraceSeconds) || Input.RegistrationGraceSeconds < Input.StructuralSettleSeconds || Input.RegistrationGraceSeconds > 5.0
		|| Input.MaximumComponents < 1 || Input.MaximumComponents > 4096
		|| Input.MaximumInstances < 1 || Input.MaximumInstances > 1000000)
	{
		OutError = TEXT("Navigation observation deadline, grace or finite work capacities are invalid.");
		return false;
	}
	const float PositiveValues[] = {Input.CapsuleRadius, Input.CapsuleHalfHeight,
		Input.MaximumFloorProbeUp, Input.MaximumFloorProbeDown,
		Input.MaximumProjectionHorizontal, Input.MaximumProjectionVertical};
	for (const float Value : PositiveValues)
		if (!FMath::IsFinite(Value) || Value <= 0.0f)
		{
			OutError = TEXT("Capsule and bounded endpoint probes require positive finite dimensions.");
			return false;
		}
	if (Input.CapsuleHalfHeight < Input.CapsuleRadius
		|| !FMath::IsFinite(Input.FloorClearance) || Input.FloorClearance < 0.0f || Input.FloorClearance > 10.0f
		|| !FMath::IsFinite(Input.MaximumWalkableSlopeDegrees) || Input.MaximumWalkableSlopeDegrees < 0.0f || Input.MaximumWalkableSlopeDegrees >= 90.0f
		|| Input.NavigationPadding.ContainsNaN() || Input.NavigationPadding.GetMin() <= 0.0
		|| !Input.AgentProperties.IsValid() || Input.AgentProperties.AgentRadius < Input.CapsuleRadius
		|| Input.AgentProperties.AgentHeight < 2.0f * Input.CapsuleHalfHeight
		|| !FMath::IsFinite(Input.AgentProperties.AgentRadius) || !FMath::IsFinite(Input.AgentProperties.AgentHeight))
	{
		OutError = TEXT("Navigation agent must cover the player capsule, with valid clearance, slope and bounds padding.");
		return false;
	}
	for (const FEFCalystoDesignatedRoom* Room : {&Input.StartRoom, &Input.EndRoom})
	{
		if (Room->StableRoomId == 0 || !FiniteBox(Room->LocalBounds) || Room->RoomToWorld.ContainsNaN()
			|| Room->RoomToWorld.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("Start and End require valid designated native room identities and bounds.");
			return false;
		}
	}
	if (Input.StartRoom.StableRoomId == Input.EndRoom.StableRoomId || Input.EndApproachWorld.ContainsNaN())
	{
		OutError = TEXT("Start and End must have distinct designated native rooms and an explicit finite door approach.");
		return false;
	}
	return true;
}

FBox FEFCalystoStructuralNavigation::ComputeNavigationBounds(const FEFCalystoStructuralEvidence& Evidence, const FVector& Padding)
{
	if (!EFCalystoStructuralNavigationPrivate::FiniteBox(Evidence.StructuralBounds)
		|| !EFCalystoStructuralNavigationPrivate::FiniteBox(Evidence.WalkableGeometryBounds)
		|| Padding.ContainsNaN() || Padding.GetMin() <= 0.0) return FBox(ForceInit);
	return (Evidence.StructuralBounds + Evidence.WalkableGeometryBounds).ExpandBy(Padding);
}

void FEFCalystoStructuralNavigation::SetResult(const EEFCalystoNavigationObservation State, const FName Code, const FString& Message)
{
	Result.State = State;
	Result.Code = Code;
	Result.Message = Message;
	if (State != EEFCalystoNavigationObservation::Ready) Result.bEntryTransformValid = false;
}

bool FEFCalystoStructuralNavigation::VerifyEndpoint(const FEFCalystoStructuralNavigationInput& Input,
	const FVector& ProbeOrigin, const FEFCalystoDesignatedRoom& Room, FVector& OutFloor, FString& OutError) const
{
	using namespace EFCalystoStructuralNavigationPrivate;
	if (!Room.ContainsWorldPoint(ProbeOrigin))
	{
		OutError = TEXT("The progression marker or door approach lies outside its designated native room.");
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(EFCalystoStructuralFloor), false);
	if (Input.ProtectedPlayer.IsValid()) Query.AddIgnoredActor(Input.ProtectedPlayer.Get());
	// Markers are ignored as collision targets; a structural floor must carry the capsule.
	for (const TWeakObjectPtr<AActor>& Marker : Input.StartMarkers) if (Marker.IsValid()) Query.AddIgnoredActor(Marker.Get());
	for (const TWeakObjectPtr<AActor>& Marker : Input.EndMarkers) if (Marker.IsValid()) Query.AddIgnoredActor(Marker.Get());
	FHitResult Hit;
	if (!Input.World->LineTraceSingleByChannel(Hit,
		ProbeOrigin + FVector(0.0, 0.0, Input.MaximumFloorProbeUp),
		ProbeOrigin - FVector(0.0, 0.0, Input.MaximumFloorProbeDown), ECC_Pawn, Query)
		|| !Hit.bBlockingHit || !Room.ContainsWorldPoint(Hit.ImpactPoint)
		|| Hit.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(Input.MaximumWalkableSlopeDegrees)))
	{
		OutError = TEXT("No blocking walkable floor exists at the designated progression point.");
		return false;
	}
	const bool bOwnedFloor = Input.StructuralSources.ContainsByPredicate([&](const FEFCalystoStructuralSource& Source)
	{
		return IsFloor(Source.Role) && Source.Component.Get() == Hit.GetComponent();
	});
	if (!bOwnedFloor)
	{
		OutError = TEXT("Progression point is supported by a component outside the generated structural floor set.");
		return false;
	}
	const FVector CapsuleCenter = Hit.ImpactPoint + FVector(0.0, 0.0, Input.CapsuleHalfHeight + Input.FloorClearance);
	// The player is ignored, but structural architecture and gameplay obstacles are not.
	FCollisionQueryParams ClearanceQuery(SCENE_QUERY_STAT(EFCalystoStructuralCapsule), false);
	if (Input.ProtectedPlayer.IsValid()) ClearanceQuery.AddIgnoredActor(Input.ProtectedPlayer.Get());
	if (Input.World->OverlapBlockingTestByChannel(CapsuleCenter, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Input.CapsuleRadius, Input.CapsuleHalfHeight), ClearanceQuery))
	{
		TArray<FOverlapResult> Overlaps;
		Input.World->OverlapMultiByChannel(Overlaps, CapsuleCenter, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Input.CapsuleRadius, Input.CapsuleHalfHeight), ClearanceQuery);
		FString BlockingComponent;
		for (const FOverlapResult& Overlap : Overlaps)
			if (Overlap.bBlockingHit && Overlap.GetComponent())
			{
				const FString Path = Overlap.GetComponent()->GetPathName();
				if (BlockingComponent.IsEmpty() || Path < BlockingComponent) BlockingComponent = Path;
			}
		OutError = FString::Printf(TEXT("Player capsule is blocked at %s: center=%s radius=%.3f half-height=%.3f; floor=%s at %s; blocker=%s."),
			Room.StableRoomId == Input.StartRoom.StableRoomId ? TEXT("Start") : TEXT("End approach"),
			*CapsuleCenter.ToString(), Input.CapsuleRadius, Input.CapsuleHalfHeight,
			*GetPathNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), *BlockingComponent);
		return false;
	}
	OutFloor = Hit.ImpactPoint;
	return true;
}

bool FEFCalystoStructuralNavigation::PrepareNavigation(const FEFCalystoStructuralNavigationInput& Input, const double NowSeconds)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Input.World.Get());
	if (!Nav || UNavigationSystemV1::IsNavigationSystemStatic())
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_INFRASTRUCTURE_UNAVAILABLE"),
			TEXT("The dungeon requires a dynamic navigation system."));
		return false;
	}
	NavigationSystem = Nav;
	const FBox Bounds = ComputeNavigationBounds(Result.Structure, Input.NavigationPadding);
	if (!Bounds.IsValid)
	{
		SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("STRUCTURAL_NAV_BOUNDS_INVALID"),
			TEXT("Generated walkable geometry did not produce valid navigation bounds."));
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.Owner = Input.DungeonOwner.Get();
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEFCalystoNavigationBoundsVolume* Volume = Input.World->SpawnActor<AEFCalystoNavigationBoundsVolume>(
		AEFCalystoNavigationBoundsVolume::StaticClass(), FTransform(FQuat::Identity, Bounds.GetCenter()), Spawn);
	BoundsVolume = Volume;
	// Spawn may already queue a bounds registration. Track its exact ID even if
	// applying the geometry fails, so cleanup still observes its removal.
	BoundsRegistrationId = Volume ? Volume->GetUniqueID() : 0;
	if (!Volume || !Volume->SetGeneratedGeometryBounds(Bounds))
	{
		const FBox Actual = Volume ? Volume->GetComponentsBoundingBox(true) : FBox(ForceInit);
		const FVector MinDelta = Actual.Min - Bounds.Min, MaxDelta = Actual.Max - Bounds.Max;
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_BOUNDS_REGISTRATION_FAILED"),
			FString::Printf(TEXT("The runtime navigation volume did not acquire geometry-derived bounds: volume=%s requested=%s actual=%s min_delta=(%.17g,%.17g,%.17g) max_delta=(%.17g,%.17g,%.17g)."),
				*GetPathNameSafe(Volume), *Bounds.ToString(), *Actual.ToString(),
				MinDelta.X, MinDelta.Y, MinDelta.Z, MaxDelta.X, MaxDelta.Y, MaxDelta.Z));
		return false;
	}
	// Subscribe before requesting the bounds update. Recast's OnNavMeshUpdate
	// observes replacement of its internal object, not completion of tile builds.
	Volume->ObserveNavigationCompletion(Nav, Nav->GetNavDataForProps(Input.AgentProperties, FrozenStart),
		&Input.AgentProperties, FrozenStart);
	Nav->OnNavigationBoundsUpdated(Volume);
	// A single owned invoker also supports projects using generation around invokers.
	// Its center is the geometry bounds center, so this radius covers the complete floor.
	const float Radius = static_cast<float>(Bounds.GetExtent().Size2D());
	UNavigationSystemV1::RegisterNavigationInvoker(*Volume, Radius, Radius + 200.0f);
	BoundsRegisteredAt = NowSeconds;
	bPrepared = true;
	return true;
}

const FEFCalystoStructuralNavigationResult& FEFCalystoStructuralNavigation::Observe(
	const FEFCalystoStructuralNavigationInput& Input, const double NowSeconds, const bool bNativeGenerationSettled)
{
	check(IsInGameThread());
	if (bCancelled) return Result;
	if (Result.State == EEFCalystoNavigationObservation::RecoverableSpatialFailure
		|| Result.State == EEFCalystoNavigationObservation::ConfigurationFailure) return Result;
	FString Error;
	if (!ValidateInput(Input, Error) || !FMath::IsFinite(NowSeconds))
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_INPUT_INVALID"), Error);
		return Result;
	}
	if (AttemptId.IsValid() && AttemptId != Input.AttemptId)
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_ATTEMPT_OWNER_CHANGED"),
			TEXT("A navigation session cannot be reused by another attempt before cleanup."));
		return Result;
	}
	AttemptId = Input.AttemptId;
	Async->AttemptId = AttemptId;
	if (NowSeconds >= Input.RequestDeadlineSeconds)
	{
		SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("REQUEST_DEADLINE_EXHAUSTED"),
			TEXT("The shared floor request deadline expired before structural navigation was verified."));
		return Result;
	}
	if (!bNativeGenerationSettled)
	{
		SetResult(EEFCalystoNavigationObservation::Pending, TEXT("NATIVE_GENERATION_PENDING"), TEXT("Native geometry is still being generated."));
		return Result;
	}
	if (FirstSettledObservation < 0.0) FirstSettledObservation = NowSeconds;
	if (!bPrepared)
	{
		if (!CollectStructuralEvidence(Input.StructuralSources, Input.MaximumComponents, Input.MaximumInstances, Result.Structure, Error))
		{
			SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("STRUCTURAL_EVIDENCE_INVALID"), Error);
			return Result;
		}
		if (LastStructuralChange < 0.0 || LastFingerprint != Result.Structure.ObservationFingerprint)
		{
			LastStructuralChange = NowSeconds;
			LastFingerprint = Result.Structure.ObservationFingerprint;
		}
		if (NowSeconds - LastStructuralChange < Input.StructuralSettleSeconds)
		{
			SetResult(EEFCalystoNavigationObservation::Pending, TEXT("STRUCTURAL_REGISTRATION_PENDING"), TEXT("Observing generated geometry and collision registration."));
			return Result;
		}
		if (!Result.Structure.bWalkableComponentsReady)
		{
			SetResult(NowSeconds - FirstSettledObservation < Input.RegistrationGraceSeconds
				? EEFCalystoNavigationObservation::Pending : EEFCalystoNavigationObservation::RecoverableSpatialFailure,
				TEXT("STRUCTURAL_FLOOR_UNUSABLE"), TEXT("Generated floors lack instances, query collision, physics registration or navigation relevance."));
			return Result;
		}
		if (Input.StartMarkers.Num() != 1 || Input.EndMarkers.Num() != 1
			|| !Input.StartMarkers[0].IsValid() || !Input.EndMarkers[0].IsValid())
		{
			SetResult(NowSeconds - FirstSettledObservation < Input.RegistrationGraceSeconds
				? EEFCalystoNavigationObservation::Pending : EEFCalystoNavigationObservation::RecoverableSpatialFailure,
				TEXT("PROGRESSION_MARKER_CARDINALITY"), TEXT("Exactly one native Start and End marker must exist for this attempt."));
			return Result;
		}
		if (Input.StartMarkers[0] == Input.EndMarkers[0]
			|| Input.StartMarkers[0]->GetWorld() != Input.World.Get() || Input.EndMarkers[0]->GetWorld() != Input.World.Get()
			|| !Input.StartRoom.ContainsWorldPoint(Input.StartMarkers[0]->GetActorLocation())
			|| !Input.EndRoom.ContainsWorldPoint(Input.EndMarkers[0]->GetActorLocation()))
		{
			SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("PROGRESSION_ROOM_OWNERSHIP_INVALID"),
				TEXT("Native progression markers do not belong to their designated Start and End rooms."));
			return Result;
		}
		if (!VerifyEndpoint(Input, Input.StartMarkers[0]->GetActorLocation(), Input.StartRoom, FrozenStart, Error)
			|| !VerifyEndpoint(Input, Input.EndApproachWorld, Input.EndRoom, FrozenEnd, Error))
		{
			SetResult(NowSeconds - FirstSettledObservation < Input.RegistrationGraceSeconds
				? EEFCalystoNavigationObservation::Pending : EEFCalystoNavigationObservation::RecoverableSpatialFailure,
				TEXT("PROGRESSION_FLOOR_OR_CLEARANCE_INVALID"), Error);
			return Result;
		}
		CandidateEntry = FTransform(Input.StartMarkers[0]->GetActorQuat(),
			FrozenStart + FVector(0.0, 0.0, Input.CapsuleHalfHeight + Input.FloorClearance));
		FrozenStartMarkerLocation = Input.StartMarkers[0]->GetActorLocation();
		FrozenEndMarkerLocation = Input.EndMarkers[0]->GetActorLocation();
		FrozenStartRoomId = Input.StartRoom.StableRoomId;
		FrozenEndRoomId = Input.EndRoom.StableRoomId;
		if (!PrepareNavigation(Input, NowSeconds)) return Result;
	}
	if (Input.StartMarkers.Num() != 1 || Input.EndMarkers.Num() != 1
		|| !Input.StartMarkers[0].IsValid() || !Input.EndMarkers[0].IsValid()
		|| Input.StartRoom.StableRoomId != FrozenStartRoomId || Input.EndRoom.StableRoomId != FrozenEndRoomId
		|| !Input.StartMarkers[0]->GetActorLocation().Equals(FrozenStartMarkerLocation, 0.01)
		|| !Input.EndMarkers[0]->GetActorLocation().Equals(FrozenEndMarkerLocation, 0.01))
	{
		SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("PROGRESSION_MARKERS_CHANGED"), TEXT("The frozen native progression markers or their room ownership changed during navigation preparation."));
		return Result;
	}
	UNavigationSystemV1* Nav = NavigationSystem.Get();
	AEFCalystoNavigationBoundsVolume* Volume = BoundsVolume.Get();
	if (!Nav || !Volume)
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_OWNER_LOST"), TEXT("The attempt lost its navigation registration owner."));
		return Result;
	}
	bool bRegisteredBoundsFound = false;
	const FBox RequiredBounds = ComputeNavigationBounds(Result.Structure, Input.NavigationPadding);
	for (const FNavigationBounds& Bounds : Nav->GetNavigationBounds())
	{
		if (Bounds.UniqueID == Volume->GetUniqueID())
		{
			Result.RegisteredNavigationBounds = Bounds.AreaBox;
			bRegisteredBoundsFound = Bounds.AreaBox.IsValid
				&& Bounds.AreaBox.IsInsideOrOn(RequiredBounds.Min) && Bounds.AreaBox.IsInsideOrOn(RequiredBounds.Max);
			break;
		}
	}
	Result.bBoundsRegistered = bRegisteredBoundsFound;
	if (!bRegisteredBoundsFound)
	{
		SetResult(EEFCalystoNavigationObservation::Pending, TEXT("NAVIGATION_BOUNDS_PENDING"), TEXT("Waiting for the geometry bounds to reach the navigation system."));
		return Result;
	}
	ANavigationData* NavData = Nav->GetNavDataForProps(Input.AgentProperties, FrozenStart);
	if (!NavData)
	{
		SetResult(EEFCalystoNavigationObservation::Pending, TEXT("NAVIGATION_DATA_PENDING"), TEXT("Waiting for relevant navigation data."));
		return Result;
	}
	Result.NavigationDataPath = NavData->GetPathName();
	Result.NavigationAgentRadius = NavData->GetConfig().AgentRadius;
	Result.NavigationAgentHeight = NavData->GetConfig().AgentHeight;
	if (NavData->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_RUNTIME_MODE_UNSUPPORTED"), TEXT("Procedural structural geometry requires Dynamic runtime navigation generation."));
		return Result;
	}
	if (NavData->GetConfig().AgentRadius + UE_KINDA_SMALL_NUMBER < Input.CapsuleRadius
		|| NavData->GetConfig().AgentHeight + UE_KINDA_SMALL_NUMBER < Input.CapsuleHalfHeight * 2.0f)
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_AGENT_TOO_SMALL"),
			FString::Printf(TEXT("Navigation data %s supports radius %.3f and height %.3f cm; the player requires radius %.3f and height %.3f cm."),
				*NavData->GetPathName(), NavData->GetConfig().AgentRadius, NavData->GetConfig().AgentHeight,
				Input.CapsuleRadius, Input.CapsuleHalfHeight * 2.0f));
		return Result;
	}
	ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData);
	if (!Recast || (NavigationData.IsValid() && NavigationData.Get() != NavData))
	{
		SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_DATA_CONTRACT_CHANGED"), TEXT("Native Calysto requires one consistent Recast navigation data owner during an attempt."));
		return Result;
	}
	NavigationData = NavData;
	Volume->ObserveNavigationCompletion(Nav, NavData, &Input.AgentProperties, FrozenStart);
	Async->NavigationRevision = Volume->GetNavigationCompletionRevision();
	Async->LastNavigationEventSeconds = Volume->GetLastNavigationCompletionSeconds();
	Result.ActiveNavigationTiles = Recast->GetNumActiveTiles();
	Result.NavigationDefaultCellSize = Recast->GetCellSize(ENavigationDataResolution::Default);
	const FVector ProjectionExtent(Input.MaximumProjectionHorizontal, Input.MaximumProjectionHorizontal, Input.MaximumProjectionVertical);
	FNavLocation StartNav, EndNav;
	Result.bStartProjected = Nav->ProjectPointToNavigation(FrozenStart, StartNav, ProjectionExtent, NavData) && StartNav.NodeRef != 0;
	Result.bEndProjected = Nav->ProjectPointToNavigation(FrozenEnd, EndNav, ProjectionExtent, NavData) && EndNav.NodeRef != 0;
	const bool bProjected = Result.bStartProjected && Result.bEndProjected
		&& StartNav.NodeRef != 0 && EndNav.NodeRef != 0
		&& Input.StartRoom.ContainsWorldPoint(StartNav.Location) && Input.EndRoom.ContainsWorldPoint(EndNav.Location)
		&& FVector::DistSquared2D(StartNav.Location, FrozenStart) <= FMath::Square(Input.MaximumProjectionHorizontal)
		&& FVector::DistSquared2D(EndNav.Location, FrozenEnd) <= FMath::Square(Input.MaximumProjectionHorizontal);
	const bool bNavigationBuilding = Nav->IsNavigationBuildInProgress();
	const bool bNavigationUnfinished = bNavigationBuilding || Nav->HasDirtyAreasQueued();
	Result.bNavigationBuilding = bNavigationBuilding;
	const bool bObservedSettledNavigation = Async->NavigationRevision > 0 && !bNavigationUnfinished
		&& NowSeconds - Async->LastNavigationEventSeconds >= Input.StructuralSettleSeconds
		&& NowSeconds - BoundsRegisteredAt >= Input.RegistrationGraceSeconds;
	if (!bProjected)
	{
		SetResult(bObservedSettledNavigation ? EEFCalystoNavigationObservation::RecoverableSpatialFailure : EEFCalystoNavigationObservation::Pending,
			TEXT("PROGRESSION_NAVIGATION_TILES_PENDING_OR_INVALID"), TEXT("The designated Start and End points do not yet have usable relevant navigation polygons."));
		return Result;
	}
	Result.StartNavigationNode = StartNav.NodeRef;
	Result.EndNavigationNode = EndNav.NodeRef;
	if (bNavigationUnfinished)
	{
		SetResult(EEFCalystoNavigationObservation::Pending, TEXT("NAVIGATION_GEOMETRY_UPDATE_PENDING"),
			TEXT("Navigation is incorporating registered geometry; a previous route cannot release the player."));
		return Result;
	}
	if (Async->bQueryCompleted && Async->QueryNavigationRevision != Async->NavigationRevision)
	{
		// Realized architecture can dirty tiles after the initial reservation route.
		// A valid path object from an older tile revision is not current acceptance proof.
		Async->bQueryCompleted = false; Async->Path.Reset();
	}
	if (Async->bQueryCompleted)
	{
		const bool bComplete = Async->QueryResult == ENavigationQueryResult::Success && Async->Path.IsValid()
			&& Async->Path->IsValid() && Async->Path->IsUpToDate() && !Async->Path->IsPartial() && !Async->Path->DidSearchReachedLimit()
			&& Async->Path->GetNavigationDataUsed() == NavData;
		if (bComplete)
		{
			const TArray<FNavPathPoint>& Points = Async->Path->GetPathPoints();
			if (!Input.StartRoom.ContainsWorldPoint(Points[0].Location) || !Input.EndRoom.ContainsWorldPoint(Points.Last().Location))
			{
				SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("ROUTE_ENDPOINT_OWNER_INVALID"), TEXT("Navigation returned a route outside the designated progression rooms."));
				return Result;
			}
			// Recheck contact and capsule immediately before publishing the only accepted entry.
			FVector EntryFloor, ExitFloor;
			if (!VerifyEndpoint(Input, FrozenStart, Input.StartRoom, EntryFloor, Error)
				|| !VerifyEndpoint(Input, FrozenEnd, Input.EndRoom, ExitFloor, Error)
				|| !EntryFloor.Equals(FrozenStart, 2.0) || !ExitFloor.Equals(FrozenEnd, 2.0))
			{
				SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("PROGRESSION_CONTACT_CHANGED"), Error);
				return Result;
			}
			Result.CompleteRoute.Reset(Points.Num());
			for (const FNavPathPoint& Point : Points) Result.CompleteRoute.Add(Point.Location);
			Result.ValidatedEntryTransform = CandidateEntry;
			Result.ValidatedEndApproach = ExitFloor;
			SetResult(EEFCalystoNavigationObservation::Ready, TEXT("STRUCTURAL_NAVIGATION_VERIFIED"), TEXT("Generated structure, entry capsule, navigation bounds and complete progression route verified."));
			Result.bEntryTransformValid = true;
			return Result;
		}
		if (bObservedSettledNavigation && Async->QueryNavigationRevision == Async->NavigationRevision)
		{
			SetResult(EEFCalystoNavigationObservation::RecoverableSpatialFailure, TEXT("PROGRESSION_ROUTE_INVALID"),
				FString::Printf(TEXT("Settled navigation has no complete progression route: result=%d valid=%d partial=%d search_limit=%d points=%d revision=%llu."),
					int32(Async->QueryResult), Async->Path.IsValid() && Async->Path->IsValid(),
					Async->Path.IsValid() && Async->Path->IsPartial(), Async->Path.IsValid() && Async->Path->DidSearchReachedLimit(),
					Async->Path.IsValid() ? Async->Path->GetPathPoints().Num() : 0, Async->NavigationRevision));
			return Result;
		}
		if (Async->QueryNavigationRevision == Async->NavigationRevision)
		{
			SetResult(EEFCalystoNavigationObservation::Pending, TEXT("NAVIGATION_UPDATE_PENDING"), TEXT("Waiting for navigation completion before reconsidering the route."));
			return Result;
		}
		Async->bQueryCompleted = false;
		Async->Path.Reset();
	}
	if (!Async->bQueryPending)
	{
		const TWeakPtr<FAsyncState> WeakAsync = Async;
		const FGuid QueryAttempt = AttemptId;
		FNavPathQueryDelegate Callback = FNavPathQueryDelegate::CreateLambda(
			[WeakAsync, QueryAttempt](uint32 QueryId, ENavigationQueryResult::Type QueryResult, FNavPathSharedPtr Path)
			{
				if (const TSharedPtr<FAsyncState> State = WeakAsync.Pin(); State && !State->bCancelled
					&& State->AttemptId == QueryAttempt && State->QueryId == QueryId && State->bQueryPending)
				{
					State->Path = MoveTemp(Path);
					State->QueryResult = QueryResult;
					State->bQueryPending = false;
					State->bQueryCompleted = true;
				}
			});
		FPathFindingQuery Query(Input.DungeonOwner.Get(), *NavData, StartNav.Location, EndNav.Location);
		Query.SetAllowPartialPaths(false).SetRequireNavigableEndLocation(true).SetNavAgentProperties(Input.AgentProperties);
		Async->bQueryPending = true;
		Async->QueryNavigationRevision = Async->NavigationRevision;
		Async->QueryId = Nav->FindPathAsync(Input.AgentProperties, Query, Callback);
		if (Async->QueryId == INVALID_NAVQUERYID)
		{
			Async->bQueryPending = false;
			SetResult(EEFCalystoNavigationObservation::ConfigurationFailure, TEXT("NAVIGATION_QUERY_REJECTED"), TEXT("The navigation system rejected the asynchronous route request."));
			return Result;
		}
	}
	SetResult(EEFCalystoNavigationObservation::Pending, TEXT("PROGRESSION_ROUTE_PENDING"), TEXT("Waiting for the bounded asynchronous progression route query."));
	return Result;
}

bool FEFCalystoStructuralNavigation::ReleaseNavigation()
{
	check(IsInGameThread());
	Async->bCancelled = true;
	if (AEFCalystoNavigationBoundsVolume* Volume = BoundsVolume.Get()) Volume->StopObservingNavigationCompletion();
	bool bDestroyAccepted = true;
	if (UNavigationSystemV1* Nav = NavigationSystem.Get())
	{
		if (Async->bQueryPending && Async->QueryId != INVALID_NAVQUERYID) Nav->AbortAsyncFindPathRequest(Async->QueryId);
		if (AEFCalystoNavigationBoundsVolume* Volume = BoundsVolume.Get())
		{
			UNavigationSystemV1::UnregisterNavigationInvoker(*Volume);
			// Destroy unregisters the volume and queues removal of its exact nav bounds ID.
			bDestroyAccepted = Volume->Destroy();
		}
	}
	else if (AEFCalystoNavigationBoundsVolume* Volume = BoundsVolume.Get()) bDestroyAccepted = Volume->Destroy();
	Async->bQueryPending = false;
	Async->Path.Reset();
	NavigationData.Reset();
	return bDestroyAccepted;
}

void FEFCalystoStructuralNavigation::Cancel()
{
	if (bCancelled) return;
	bCancelled = true;
	const bool bDestroyAccepted = ReleaseNavigation();
	Result.CompleteRoute.Reset();
	SetResult(EEFCalystoNavigationObservation::Cancelled,
		bDestroyAccepted ? TEXT("NAVIGATION_CANCELLED") : TEXT("NAVIGATION_BOUNDS_DESTRUCTION_REJECTED"),
		bDestroyAccepted ? TEXT("The attempt cancelled navigation; owned cleanup must still be observed.")
			: TEXT("Navigation bounds refused destruction; the attempt remains owned until cleanup completes."));
}

bool FEFCalystoStructuralNavigation::IsReleased() const
{
	if (!bCancelled || Async->bQueryPending || BoundsVolume.IsValid()) return false;
	// Removal is queued by UE; the transaction must observe removal before another attempt.
	if (const UNavigationSystemV1* Nav = NavigationSystem.Get())
	{
		if (BoundsRegistrationId != 0)
			for (const FNavigationBounds& Bounds : Nav->GetNavigationBounds())
				if (Bounds.UniqueID == BoundsRegistrationId) return false;
	}
	return true;
}
