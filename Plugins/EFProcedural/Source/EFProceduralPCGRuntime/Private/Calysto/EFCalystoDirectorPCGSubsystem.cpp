#include "Calysto/EFCalystoDirectorPCGSubsystem.h"
#include "Calysto/EFCalystoDirectorSubsystem.h"
#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDirectorPortal.h"
#include "Calysto/EFCalystoNativeAdapter.h"
#include "Calysto/EFCalystoStructuralNavigation.h"
#include "Calysto/EFCalystoBakedArchitecture.h"
#include "Calysto/EFCalystoArchitectureManifest.h"
#include "Calysto/EFCalystoContentAttemptProvider.h"
#include "Calysto/EFCalystoContentCollisionContract.h"
#include "Calysto/EFCalystoContentMaterializer.h"
#include "Calysto/EFCalystoDecalMaterializer.h"
#include "Calysto/EFCalystoDecalReservation.h"
#include "Calysto/EFCalystoSurfaceCandidates.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "PCGComponent.h"
#include "PCGDataAsset.h"
#include "PCGManagedResource.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	bool HasAuthoredContent(const FEFCalystoCompiledDirector& Configuration, const FGuid& StyleId)
	{
		const FEFCalystoStyle* Style = Configuration.FindStyle(StyleId);
		if (!Style) return false;
		if (!Style->Content.IsEmpty()) return true;
		for (const auto& Theme : Configuration.GetThemes())
			if (!Theme.Content.IsEmpty()) return true;
		return false;
	}

	IEFCalystoContentAttemptProvider* ResolveUniqueContentProvider(FString& Error)
	{
		Error.Reset();
		const TArray<IEFCalystoContentAttemptProvider*> Providers =
			IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoContentAttemptProvider>(
				IEFCalystoContentAttemptProvider::GetModularFeatureName());
		TArray<IEFCalystoContentAttemptProvider*> Valid;
		for (IEFCalystoContentAttemptProvider* Provider : Providers) if (Provider) Valid.Add(Provider);
		if (Valid.Num() != 1)
		{
			Error = FString::Printf(TEXT("V7 content requires exactly one registered project gameplay provider; found %d."), Valid.Num());
			return nullptr;
		}
		return Valid[0];
	}

	bool MatchNativeWallExtensionInstances(const TArray<FEFCalystoNativeWallExtension>& Extensions,
		TArray<FEFCalystoStructuralSource>& Sources, FString& Error)
	{
		Error.Reset();
		for (auto& Source : Sources) Source.NativeZeroHeightWallExtensionIndices.Reset();
		if (Extensions.Num() > 8192)
		{ Error = TEXT("Native upper-wall provenance exceeds its finite record capacity."); return false; }
		TBitArray<> Consumed(false, Extensions.Num());
		TArray<TSet<int32>> ProposedIndices;
		ProposedIndices.SetNum(Sources.Num());
		int64 Comparisons = 0;
		for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
		{
			const auto& Source = Sources[SourceIndex];
			if (Source.Role != EEFCalystoStructuralRole::Wall) continue;
			const auto* Instances = Cast<UInstancedStaticMeshComponent>(Source.Component.Get());
			if (!Instances || !Instances->GetStaticMesh()) continue;
			const FSoftObjectPath Mesh(Instances->GetStaticMesh());
			const int32 Count = Instances->GetInstanceCount();
			if (Count > 100000)
			{ Error = TEXT("Native wall observation exceeds its finite instance capacity."); return false; }
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FTransform Actual;
				if (!Instances->GetInstanceTransform(Index, Actual, true) || Actual.ContainsNaN())
				{ Error = TEXT("Native wall instance transform is unavailable."); return false; }
				const FVector Scale = Actual.GetScale3D();
				if (Scale.Z != 0.0) continue;
				if (!Instances->PerInstanceSMData.IsValidIndex(Index))
				{ Error = TEXT("Native wall instance storage is unavailable."); return false; }
				// A zero-height instance has a singular matrix. UE's public transform
				// getter reconstructs its quaternion as identity, losing wall orientation.
				// Match the actual stored basis instead of that lossy decomposition.
				const FMatrix ActualMatrix = Instances->PerInstanceSMData[Index].Transform * Instances->GetComponentTransform().ToMatrixWithScale();
				if (ActualMatrix.ContainsNaN())
				{ Error = TEXT("Native wall instance storage contains a nonfinite matrix."); return false; }
				int32 Match = INDEX_NONE;
				int32 Nearest = INDEX_NONE;
				double NearestDistance = TNumericLimits<double>::Max();
				for (int32 ExtensionIndex = 0; ExtensionIndex < Extensions.Num(); ++ExtensionIndex)
				{
					if (++Comparisons > 4000000)
					{ Error = TEXT("Native upper-wall provenance matching exceeded its bounded work capacity."); return false; }
					const auto& Expected = Extensions[ExtensionIndex];
					const FMatrix ExpectedMatrix = Expected.WorldTransform.ToMatrixWithScale();
					if (!Consumed[ExtensionIndex] && Expected.Mesh == Mesh)
					{
						const double Distance = FVector::DistSquared(Actual.GetLocation(), Expected.WorldTransform.GetLocation());
						if (Distance < NearestDistance) { Nearest = ExtensionIndex; NearestDistance = Distance; }
					}
					if (!Consumed[ExtensionIndex] && Expected.Mesh == Mesh
						&& ActualMatrix.GetOrigin().Equals(ExpectedMatrix.GetOrigin(), 0.01)
						&& ActualMatrix.GetScaledAxis(EAxis::X).Equals(ExpectedMatrix.GetScaledAxis(EAxis::X), 0.0001)
						&& ActualMatrix.GetScaledAxis(EAxis::Y).Equals(ExpectedMatrix.GetScaledAxis(EAxis::Y), 0.0001)
						&& ActualMatrix.GetScaledAxis(EAxis::Z).Equals(ExpectedMatrix.GetScaledAxis(EAxis::Z), 0.0001))
					{ Match = ExtensionIndex; break; }
				}
				if (Match == INDEX_NONE)
				{
					Error = FString::Printf(TEXT("Zero-height wall instance has no matching native upper-extension output: %s [%d]; proposals=%d; actual=%s; nearest=%s."),
						*Instances->GetPathName(), Index, Extensions.Num(), *Actual.ToString(),
						Nearest == INDEX_NONE ? TEXT("none") : *Extensions[Nearest].WorldTransform.ToString());
					return false;
				}
				Consumed[Match] = true;
				ProposedIndices[SourceIndex].Add(Index);
			}
		}
		if (Consumed.CountSetBits() != Extensions.Num())
		{ Error = TEXT("Native upper-wall output did not materialize as its exact owned instance multiset."); return false; }
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
			Sources[Index].NativeZeroHeightWallExtensionIndices = MoveTemp(ProposedIndices[Index]);
		return true;
	}

	struct FEFCalystoWallInstanceCandidate
	{
		int32 SourceIndex = INDEX_NONE;
		int32 InstanceIndex = INDEX_NONE;
		FSoftObjectPath Mesh;
		FMatrix WorldMatrix = FMatrix::Identity;
		bool bConsumed = false;
	};

	FString MakeWallLocationBucket(const FSoftObjectPath& Mesh, const int64 X, const int64 Y, const int64 Z)
	{
		return Mesh.ToString() + TEXT("|") + LexToString(X) + TEXT("|") + LexToString(Y) + TEXT("|") + LexToString(Z);
	}

	bool MatchNativeWallSurfaceInstances(const TArray<FEFCalystoNativeWallSurface>& Surfaces,
		TArray<FEFCalystoStructuralSource>& Sources, FString& Error)
	{
		Error.Reset();
		for (FEFCalystoStructuralSource& Source : Sources)
		{
			Source.NativeWallRoomIds.Reset();
			Source.NativeWallMaterials.Reset();
			Source.NativeDoorWallIndices.Reset();
			Source.NativeStyleOwnedWallIndices.Reset();
		}
		if (Surfaces.IsEmpty() || Surfaces.Num() > 65536)
		{
			Error = TEXT("Native final-wall provenance has an invalid bounded record count.");
			return false;
		}

		// The PCG point and stored ISM matrices are produced in the same world space.
		// Bucket only by a small location cell, then compare every matrix basis exactly
		// enough to preserve singular upper-wall rotations without nearest substitution.
		auto Cell = [](const double Value) { return FMath::RoundToInt64(Value / 0.05); };
		auto MatrixMatches = [](const FMatrix& Actual, const FMatrix& Expected)
		{
			return Actual.GetOrigin().Equals(Expected.GetOrigin(), 0.01)
				&& Actual.GetScaledAxis(EAxis::X).Equals(Expected.GetScaledAxis(EAxis::X), 0.0001)
				&& Actual.GetScaledAxis(EAxis::Y).Equals(Expected.GetScaledAxis(EAxis::Y), 0.0001)
				&& Actual.GetScaledAxis(EAxis::Z).Equals(Expected.GetScaledAxis(EAxis::Z), 0.0001);
		};

		TArray<FEFCalystoWallInstanceCandidate> Candidates;
		TMap<FString, TArray<int32>> ByLocation;
		int32 TotalInstances = 0;
		for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
		{
			FEFCalystoStructuralSource& Source = Sources[SourceIndex];
			if (Source.Role != EEFCalystoStructuralRole::Wall) continue;
			UStaticMeshComponent* Component = Source.Component.Get();
			if (!Component || !Component->GetStaticMesh())
			{
				Error = TEXT("A final-wall structural component disappeared before provenance binding.");
				return false;
			}
			const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component);
			const int32 Count = Instances ? Instances->GetInstanceCount() : 1;
			if (Count <= 0 || Count > 100000 - TotalInstances)
			{
				Error = TEXT("Final-wall structural observation exceeded its bounded instance contract.");
				return false;
			}
			const FSoftObjectPath Mesh(Component->GetStaticMesh());
			for (int32 InstanceIndex = 0; InstanceIndex < Count; ++InstanceIndex)
			{
				FMatrix WorldMatrix = Component->GetComponentTransform().ToMatrixWithScale();
				if (Instances)
				{
					if (!Instances->PerInstanceSMData.IsValidIndex(InstanceIndex))
					{
						Error = TEXT("Final-wall structural storage is unavailable for provenance binding.");
						return false;
					}
					WorldMatrix = Instances->PerInstanceSMData[InstanceIndex].Transform * WorldMatrix;
				}
				if (WorldMatrix.ContainsNaN())
				{
					Error = TEXT("Final-wall structural storage contains a nonfinite matrix.");
					return false;
				}
				const FVector Origin = WorldMatrix.GetOrigin();
				const int32 CandidateIndex = Candidates.Add({SourceIndex, InstanceIndex, Mesh, WorldMatrix});
				ByLocation.FindOrAdd(MakeWallLocationBucket(Mesh, Cell(Origin.X), Cell(Origin.Y), Cell(Origin.Z))).Add(CandidateIndex);
				++TotalInstances;
			}
		}
		if (Candidates.IsEmpty())
		{
			Error = TEXT("No generated structural wall instances are available for final-wall provenance.");
			return false;
		}

		int64 Comparisons = 0;
		for (const FEFCalystoNativeWallSurface& Surface : Surfaces)
		{
			if ((Surface.bStyleOwned ? Surface.RoomId != 0 : Surface.RoomId <= 0)
				|| Surface.Mesh.IsNull() || Surface.Material.IsNull() || Surface.WorldTransform.ContainsNaN())
			{
				Error = TEXT("Native final-wall provenance contains an invalid authority, mesh, material or transform.");
				return false;
			}
			const FMatrix ExpectedMatrix = Surface.WorldTransform.ToMatrixWithScale();
			if (ExpectedMatrix.ContainsNaN())
			{
				Error = TEXT("Native final-wall provenance contains a nonfinite matrix.");
				return false;
			}
			const FVector Origin = ExpectedMatrix.GetOrigin();
			int32 Match = INDEX_NONE;
			for (int64 X = Cell(Origin.X) - 1; X <= Cell(Origin.X) + 1 && Match == INDEX_NONE; ++X)
				for (int64 Y = Cell(Origin.Y) - 1; Y <= Cell(Origin.Y) + 1 && Match == INDEX_NONE; ++Y)
					for (int64 Z = Cell(Origin.Z) - 1; Z <= Cell(Origin.Z) + 1 && Match == INDEX_NONE; ++Z)
					{
						const TArray<int32>* Bucket = ByLocation.Find(MakeWallLocationBucket(Surface.Mesh, X, Y, Z));
						if (!Bucket) continue;
						for (const int32 CandidateIndex : *Bucket)
						{
							if (++Comparisons > 4000000)
							{
								Error = TEXT("Final-wall provenance matching exceeded its bounded comparison capacity.");
								return false;
							}
							FEFCalystoWallInstanceCandidate& Candidate = Candidates[CandidateIndex];
							if (!Candidate.bConsumed && Candidate.Mesh == Surface.Mesh && MatrixMatches(Candidate.WorldMatrix, ExpectedMatrix))
							{
								Match = CandidateIndex;
								break;
							}
						}
					}
			if (Match == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("A final native wall point has no exact generated structural instance: room=%lld mesh=%s transform=%s."),
					Surface.RoomId, *Surface.Mesh.ToString(), *Surface.WorldTransform.ToString());
				return false;
			}
			FEFCalystoWallInstanceCandidate& Candidate = Candidates[Match];
			Candidate.bConsumed = true;
			FEFCalystoStructuralSource& Source = Sources[Candidate.SourceIndex];
			if (Source.NativeWallRoomIds.Contains(Candidate.InstanceIndex) || Source.NativeWallMaterials.Contains(Candidate.InstanceIndex)
				|| Source.NativeDoorWallIndices.Contains(Candidate.InstanceIndex)
				|| Source.NativeStyleOwnedWallIndices.Contains(Candidate.InstanceIndex))
			{
				Error = TEXT("Two final native wall records tried to own one structural instance.");
				return false;
			}
			Source.NativeWallRoomIds.Add(Candidate.InstanceIndex, Surface.RoomId);
			Source.NativeWallMaterials.Add(Candidate.InstanceIndex, Surface.Material);
			if (Surface.bDoorway) Source.NativeDoorWallIndices.Add(Candidate.InstanceIndex);
			if (Surface.bStyleOwned) Source.NativeStyleOwnedWallIndices.Add(Candidate.InstanceIndex);
		}
		for (const FEFCalystoWallInstanceCandidate& Candidate : Candidates)
		{
			if (!Candidate.bConsumed)
			{
				Error = FString::Printf(TEXT("A generated structural wall has no exact final native provenance: component=%s instance=%d."),
					*Sources[Candidate.SourceIndex].Component->GetPathName(), Candidate.InstanceIndex);
				return false;
			}
		}
		return true;
	}

	bool BuildStructuralMeshRules(const FEFCalystoStyleArchitecture& Architecture,
		TArray<FEFCalystoStructuralMeshRule>& OutRules, FString& Error)
	{
		TArray<FEFCalystoStructuralMeshRule> AuthoredRules;
		auto AddRules = [&AuthoredRules](const TArray<FEFCalystoArchitectureEntry>& Entries, EEFCalystoStructuralRole Role)
		{ for (const auto& Entry : Entries) if (!Entry.Mesh.IsNull()) AuthoredRules.Add({Entry.Mesh.ToSoftObjectPath(), Role}); };
		AddRules(Architecture.Floor, EEFCalystoStructuralRole::Floor); AddRules(Architecture.Wall, EEFCalystoStructuralRole::Wall);
		AddRules(Architecture.Roof, EEFCalystoStructuralRole::Roof); AddRules(Architecture.RampTop, EEFCalystoStructuralRole::Ramp);
		AddRules(Architecture.RampBottom, EEFCalystoStructuralRole::Ramp);
		for (const auto& Entry : Architecture.Doorways) AuthoredRules.Add({Entry.WallMesh.ToSoftObjectPath(), EEFCalystoStructuralRole::Wall});
		// Frame geometry participates in exact doorway protection even when it is not a walkable surface.
		// Keep an existing structural classification when the authored mesh is deliberately shared.
		const auto AddFrame=[&](const FSoftObjectPath& Mesh)
		{
			if (!Mesh.IsNull() && !AuthoredRules.ContainsByPredicate([&](const auto& Rule) { return Rule.Mesh==Mesh; }))
				AuthoredRules.Add({Mesh,EEFCalystoStructuralRole::OtherStructure});
		};
		for (const auto& Entry:Architecture.DoorFrames) AddFrame(Entry.Mesh.ToSoftObjectPath());
		for (const auto& Entry:Architecture.Doorways) AddFrame(Entry.FrameMesh.ToSoftObjectPath());
		return FEFCalystoStructuralNavigation::NormalizeMeshRules(AuthoredRules, OutRules, Error);
	}

	bool ComputeDungeonLocalFootprint(const UStaticMeshComponent& Component, const FMatrix& WorldMatrix,
		const FTransform& DungeonTransform, FBox& OutFootprint, FString& Error)
	{
		if (!Component.GetStaticMesh() || WorldMatrix.ContainsNaN())
		{
			Error = TEXT("A structural material instance has unavailable native geometry.");
			return false;
		}
		const FBox MeshBounds = Component.GetStaticMesh()->GetBounds().GetBox();
		OutFootprint = FBox(ForceInit);
		for (int32 X = 0; X < 2; ++X) for (int32 Y = 0; Y < 2; ++Y) for (int32 Z = 0; Z < 2; ++Z)
		{
			const FVector Corner(X ? MeshBounds.Max.X : MeshBounds.Min.X, Y ? MeshBounds.Max.Y : MeshBounds.Min.Y, Z ? MeshBounds.Max.Z : MeshBounds.Min.Z);
			OutFootprint += DungeonTransform.InverseTransformPosition(WorldMatrix.TransformPosition(Corner));
		}
		if (!OutFootprint.IsValid)
		{
			Error = TEXT("A structural material footprint is invalid.");
			return false;
		}
		return true;
	}

	class FDirectorNativeRuntime final : public IEFCalystoDirectorAttemptRuntime
	{
	public:
		virtual TArray<FSoftObjectPath> GetSharedDependencies() const override
		{
			auto Paths = FEFCalystoNativeAdapter::GetSharedDependencies();
			Paths.Add(FSoftObjectPath(TEXT("/Script/EFProceduralACFURuntime.EFCalystoFloorDoor")));
			return Paths;
		}
		virtual bool Preflight(const FEFCalystoCompiledDirector& Configuration, const FGuid& Style, FString& Error) const override
		{
			if (!FEFCalystoNativeAdapter::ValidateCapabilities(Configuration, Style, Error)) return false;
			if (!FEFCalystoDecalReservationPlanner::ValidateConfiguration(Configuration, Style, Error)) return false;
			if (HasAuthoredContent(Configuration, Style) && !ResolveUniqueContentProvider(Error)) return false;
			TArray<FEFCalystoStructuralMeshRule> Rules;
			return BuildStructuralMeshRules(Configuration.FindStyle(Style)->Architecture, Rules, Error);
		}
		virtual bool GetBakedVisualDependencies(TConstArrayView<FSoftObjectPath> LoadedVisuals,
			TArray<FSoftObjectPath>& Dependencies, FString& Error) const override
		{
			Dependencies.Reset(); Error.Reset(); TSet<FSoftObjectPath> Required;
			if (LoadedVisuals.Num()>8192) { Error=TEXT("Visual inspection exceeds 8192 parent resources."); return false; }
			for (const auto& Path:LoadedVisuals)
			{
				UObject* Resource=Path.ResolveObject();
				if (!Resource) { Error=TEXT("A reachable visual resource is unavailable: ")+Path.ToString(); return false; }
				const auto* Baked=Cast<UPCGDataAsset>(Resource); if (!Baked) continue;
				TArray<FEFCalystoBakedMesh> Children; TArray<FSoftObjectPath> ChildPaths;
				if (!FEFCalystoBakedArchitecture::Decode(*Baked,Children,ChildPaths,Error)) return false;
				if (!Children.ContainsByPredicate([](const auto& Child)
					{ return Child.ChancePercent==100 && Child.Rotation==EEFCalystoBakedRotation::Preserve; }))
				{ Error=TEXT("The baked attachment contract requires a guaranteed child with preserved rotation before native generation."); return false; }
				for (const auto& Child:ChildPaths) Required.Add(Child);
				if (Required.Num()>8192) { Error=TEXT("Baked visual expansion exceeds 8192 exact resources."); return false; }
			}
			Dependencies=Required.Array(); Dependencies.Sort([](const auto& A,const auto& B) { return A.ToString()<B.ToString(); }); return true;
		}
		virtual bool BeginAttempt(const FEFCalystoDirectorAttemptRequest& InRequest, FString& Error) override
		{
			if (!IsInGameThread() || Adapter || DungeonActor.IsValid() || !OwnedActors.IsEmpty() || !OwnedComponents.IsEmpty()
				|| !InRequest.Configuration || !InRequest.World.IsValid()
				|| !InRequest.Configuration->FindStyle(InRequest.Random.StyleId))
			{ Error = TEXT("Native attempt ownership is unavailable or still occupied."); return false; }
			if (ContentSnapshot && ContentSnapshotRequest!=InRequest.Token.Request)
			{ Error=TEXT("A retained V7 gameplay snapshot belongs to another floor request."); return false; }
			if (InRequest.PreFloorGameplaySnapshot && ContentSnapshot
				&& ContentSnapshotRequest == InRequest.Token.Request
				&& ContentSnapshot != InRequest.PreFloorGameplaySnapshot)
			{ Error=TEXT("A retry supplied a different immutable V7 gameplay snapshot for the same request."); return false; }
			Request = InRequest; Observation = {}; Observation.Token = Request.Token; bReleasing = false;
			NativeStartedSeconds = FPlatformTime::Seconds(); NavigationStartedSeconds = NavigationReadySeconds = -1;
			Native = {}; NavigationInput = {}; StructuralSources.Reset(); MeshRules.Reset();
			bNativeCaptured = false; bNavigationInputCaptured = false;
			bArchitectureFrozen = false; ArchitectureManifest={}; SurfaceCandidates={}; ArchitectureDecisions.Reset();
			bContentFrozen = false; bGameplayProviderPrepared = false; ContentManifest.Reset(); ContentPlanReport={}; ContentProviderResult={};
			ContentProviderRequest={}; ContentProvider=nullptr; ContentMaterializer.Reset();
			bDecalFrozen = false; DecalManifest.Reset(); DecalPlanReport={}; DecalMaterializer.Reset();
			bAcceptedContent = bAcceptedDecals = false;
			if (Request.PreFloorGameplaySnapshot)
			{
				ContentSnapshot = Request.PreFloorGameplaySnapshot;
				ContentSnapshotRequest = Request.Token.Request;
				if (GameplayReconstructionRequest != Request.Token.Request)
				{
					bGameplaySnapshotReconstructed = false;
					GameplayReconstructionRequest.Invalidate();
				}
			}
			ArchitectureRealizationStartedSeconds=-1;
			ArchitectureBatch=MakeUnique<FEFCalystoArchitectureMeshBatch>();
			// Validate and freeze the schema before actor creation or the single root PCG request.
			if (!BuildStructuralMeshRules(Request.Configuration->FindStyle(Request.Random.StyleId)->Architecture, MeshRules, Error)) return false;
			UClass* DoorClass = Cast<UClass>(FSoftObjectPath(TEXT("/Script/EFProceduralACFURuntime.EFCalystoFloorDoor")).ResolveObject());
			if (!DoorClass || !DoorClass->ImplementsInterface(UEFCalystoDirectorPortal::StaticClass()))
			{ Error = TEXT("The travel-owned floor door contract is unavailable."); return false; }
			DungeonActor = FEFCalystoNativeAdapter::SpawnGenerator(Request.World.Get(), Error);
			if (!DungeonActor.IsValid()) return false;
			CaptureOwnership();
			Adapter = FEFCalystoNativeAdapter::Prepare(DungeonActor.Get(), Request.Configuration.ToSharedRef(),
				Request.Random, Request.TopologySeed, DoorClass, Error);
			if (!Adapter) return false;
			CaptureOwnership();
			Navigation = MakeUnique<FEFCalystoStructuralNavigation>();
			return Adapter->GenerateOnce(Error) != InvalidPCGTaskId;
		}

		virtual FEFCalystoDirectorAttemptObservation ObserveAttempt(const double Now) override
		{
			if (bReleasing || Observation.Stage == EEFCalystoObservation::Failed) return Observation;
			FString Error;
			if (!Adapter || !Navigation || !DungeonActor.IsValid() || !Request.World.IsValid()
				|| DungeonActor->GetWorld() != Request.World.Get())
				return Failure(TEXT("NativeOwnerUnavailable"), TEXT("The native generator is unavailable."), EEFCalystoAttemptFailure::Resource);
			Observation.Metrics.GenerateLocalCalls = Adapter->GetGenerateRequestCount();
			if (!bNativeCaptured)
			{
				Observation.Metrics.NativeOutputSeconds = FPlatformTime::Seconds() - NativeStartedSeconds;
				Observation.Metrics.ObservationCode = TEXT("NATIVE_GENERATION_PENDING");
				Observation.Metrics.ObservationMessage = TEXT("Waiting for the native graph and owned resources.");
				UPCGComponent* Component = Adapter->GetComponent();
				// Managed resources can remain locked briefly after graph tasks settle.
				// Unfinished registration is not a settled topology failure.
				if (Component && !Component->AreManagedResourcesAccessible()) return Observation;
				EEFCalystoNativeFailureKind NativeFailure = EEFCalystoNativeFailureKind::Configuration;
				const auto NativeStatus = Adapter->Observe(Native, Error, NativeFailure);
				CaptureOwnership();
				if (NativeStatus == EEFCalystoNativeStatus::Pending) return Observation;
				if (NativeStatus == EEFCalystoNativeStatus::Failed)
				{
					const EEFCalystoAttemptFailure FailureKind = NativeFailure == EEFCalystoNativeFailureKind::Spatial
						? EEFCalystoAttemptFailure::Spatial
						: NativeFailure == EEFCalystoNativeFailureKind::Resource
							? EEFCalystoAttemptFailure::Resource
							: EEFCalystoAttemptFailure::Configuration;
					return Failure(TEXT("NativeOutputInvalid"), Error, FailureKind);
				}
				Observation.Stage = EEFCalystoObservation::StructuralVerification;
				if (!FEFCalystoStructuralNavigation::GatherNativeSources(*DungeonActor.Get(), Component, MeshRules, StructuralSources, Error))
					return Failure(TEXT("StructuralSourcesInvalid"), Error, EEFCalystoAttemptFailure::Configuration);
				if (!MatchNativeWallSurfaceInstances(Native.NativeFinalWallSurfaces, StructuralSources, Error))
					return Failure(TEXT("NativeFinalWallProvenanceInvalid"), Error, EEFCalystoAttemptFailure::Configuration);
				// Native Calysto batches surface instances by mesh. A single ISM can therefore
				// contain both Style-owned and Theme-owned tiles. Partition only such mixed
				// batches into transient, attempt-owned ISMs; the native layout and mesh
				// transforms remain unchanged and no room MID is introduced.
				if (!PartitionNativeSurfaceMaterials(Error))
					return Failure(TEXT("MaterialPartitionInvalid"), Error, EEFCalystoAttemptFailure::Configuration);
				const bool bZeroHeightExtensionsSupported = Adapter->CanContainZeroHeightWallExtensions(Error);
				if (!Error.IsEmpty()) return Failure(TEXT("NativeWallExtensionContractInvalid"), Error, EEFCalystoAttemptFailure::Configuration);
				if (!bZeroHeightExtensionsSupported && !Native.NativeZeroHeightWallExtensions.IsEmpty())
					return Failure(TEXT("NativeWallExtensionHeightInvalid"), TEXT("Native upper-wall output disagrees with the prepared runtime heights."), EEFCalystoAttemptFailure::Spatial);
				if (!MatchNativeWallExtensionInstances(Native.NativeZeroHeightWallExtensions, StructuralSources, Error))
					return Failure(TEXT("NativeWallExtensionRealizationInvalid"), Error, EEFCalystoAttemptFailure::Spatial);
				bNativeCaptured = true;
			}
			if (!bNavigationInputCaptured)
			{
				if (!UGameplayStatics::GetPlayerPawn(Request.World.Get(), 0))
				{
					Observation.Metrics.ObservationCode = TEXT("PLAYER_CAPSULE_PENDING");
					Observation.Metrics.ObservationMessage = TEXT("Waiting for the protected player's actual capsule.");
					return Observation;
				}
				if (!MakeNavigationInput(Error)) return Failure(TEXT("ProgressionContractInvalid"), Error, EEFCalystoAttemptFailure::Spatial);
				bNavigationInputCaptured = true;
				NavigationStartedSeconds = FPlatformTime::Seconds();
			}
			// Continue bounded realization independently of tile registration. The whole
			// selected batch must finish before accepting a fresh route through its collision.
			if (bArchitectureFrozen && ArchitectureBatch->GetState()==EEFCalystoArchitectureMeshState::Preparing)
				if (!ArchitectureBatch->Prepare(Now,64,Error))
					return Failure(TEXT("ArchitectureRealizationFailed"),Error,EEFCalystoAttemptFailure::Spatial);
			const auto& NavigationResult = Navigation->Observe(NavigationInput, Now, true);
			auto& Metrics = Observation.Metrics;
			Metrics.ObservationCode = NavigationResult.Code; Metrics.ObservationMessage = NavigationResult.Message;
			Metrics.NavigationDataPath = NavigationResult.NavigationDataPath;
			Metrics.FloorInstances = NavigationResult.Structure.FloorInstanceCount;
			Metrics.WallInstances = NavigationResult.Structure.WallInstanceCount;
			Metrics.RoofInstances = NavigationResult.Structure.RoofInstanceCount;
			Metrics.ActiveNavigationTiles = NavigationResult.ActiveNavigationTiles;
			Metrics.NavigationAgentRadius = NavigationResult.NavigationAgentRadius;
			Metrics.NavigationAgentHeight = NavigationResult.NavigationAgentHeight;
			Metrics.NavigationDefaultCellSize = NavigationResult.NavigationDefaultCellSize;
			Metrics.bBoundsRegistered = NavigationResult.bBoundsRegistered;
			Metrics.bNavigationBuilding = NavigationResult.bNavigationBuilding;
			Metrics.bStartProjected = NavigationResult.bStartProjected;
			Metrics.bEndProjected = NavigationResult.bEndProjected;
			Metrics.RoutePoints = NavigationResult.CompleteRoute.Num();
			if (NavigationResult.State == EEFCalystoNavigationObservation::Ready && NavigationReadySeconds < 0)
				NavigationReadySeconds = FPlatformTime::Seconds();
			Metrics.NavigationObservationSeconds = (NavigationReadySeconds >= 0 ? NavigationReadySeconds : FPlatformTime::Seconds()) - NavigationStartedSeconds;
			if (NavigationResult.State == EEFCalystoNavigationObservation::Pending)
			{ Observation.Stage = bArchitectureFrozen ? EEFCalystoObservation::RealizationVerification : EEFCalystoObservation::StructuralVerification; return Observation; }
			if (NavigationResult.State != EEFCalystoNavigationObservation::Ready)
				return Failure(NavigationResult.Code, NavigationResult.Message,
					NavigationResult.State == EEFCalystoNavigationObservation::ConfigurationFailure ? EEFCalystoAttemptFailure::Configuration : EEFCalystoAttemptFailure::Spatial);
			Observation.Stage = EEFCalystoObservation::NavigationAndReservations;
			if (!VerifyMaterials(Error)) return Failure(TEXT("MaterialRealizationInvalid"), Error, EEFCalystoAttemptFailure::Spatial);
			if (!bArchitectureFrozen)
			{
				const double ReservationStarted=FPlatformTime::Seconds();
				EEFCalystoAttemptFailure ReservationFailure=EEFCalystoAttemptFailure::Configuration;
				if (!FreezeArchitecture(NavigationResult,ReservationFailure,Error))
				{
					// No decision has been committed while native navigation is still registering.
					if (SurfaceCandidates.bPending)
					{
						Metrics.ObservationCode=SurfaceCandidates.FailureCode; Metrics.ObservationMessage=Error;
						Observation.Stage=EEFCalystoObservation::NavigationAndReservations; return Observation;
					}
					return Failure(SurfaceCandidates.FailureCode.IsNone() ? FName(TEXT("ArchitectureReservationInvalid"))
						: SurfaceCandidates.FailureCode,Error,ReservationFailure);
				}
				Metrics.ArchitectureReservationSeconds=FPlatformTime::Seconds()-ReservationStarted;
				Metrics.ArchitectureOpportunities=SurfaceCandidates.ArchitectureOpportunities.Num();
				Metrics.DecalOpportunities=SurfaceCandidates.DecalOpportunities.Num();
				Metrics.SurfacePhysicsQueries=SurfaceCandidates.PhysicsQueries;
				Metrics.SurfaceGeometryRejections=SurfaceCandidates.RejectedGeometry;
				Metrics.SurfaceProtectionRejections=SurfaceCandidates.RejectedProtection;
				Metrics.FirstArchitectureRejection=SurfaceCandidates.FirstArchitectureRejection;
				Metrics.ReservedArchitectureParents=ArchitectureManifest.GetParents().Num();
				Metrics.ReservedArchitectureMeshes=ArchitectureManifest.GetMeshes().Num();
				Metrics.ReservedDecals=DecalManifest ? DecalManifest->GetElements().Num() : 0;
				ArchitectureRealizationStartedSeconds=FPlatformTime::Seconds();
				const FString ContentHash = ContentManifest ? ContentManifest->GetHash() : TEXT("NoContent");
				const FString DecalHash = DecalManifest ? DecalManifest->GetHash() : TEXT("NoDecals");
				Observation.ReservationHash = FMD5::HashAnsiString(*FString::Printf(TEXT("Native|%d|%d|%s|%s|%s"),
					Request.TopologySeed, Native.Rooms.Num(), *ArchitectureManifest.GetHash(), *ContentHash, *DecalHash));
				Observation.ReservedElements=ArchitectureManifest.GetReservedElements();
				if (ContentManifest)
					for (const FEFCalystoReservedContent& Element : ContentManifest->GetElements())
						Observation.ReservedElements.Add(Element.Id);
				if (DecalManifest)
					for (const FEFCalystoReservedDecal& Element : DecalManifest->GetElements())
						Observation.ReservedElements.Add(Element.Id);
				bArchitectureFrozen=true;
				if (ArchitectureBatch->GetState()==EEFCalystoArchitectureMeshState::Preparing)
				{
					Observation.Stage=EEFCalystoObservation::RealizationVerification;
					Metrics.ObservationCode=TEXT("ARCHITECTURE_REALIZATION_PENDING");
					Metrics.ObservationMessage=TEXT("Materializing the exact frozen architecture before final navigation acceptance.");
					return Observation;
				}
			}
			if (ArchitectureBatch->GetState()==EEFCalystoArchitectureMeshState::Preparing)
			{ Observation.Stage=EEFCalystoObservation::RealizationVerification; return Observation; }
			// Reservation can synchronously advance the platform clock.  The selected
			// materializers record their Begin clock, so every same-tick observation must
			// use one later monotonic sample instead of the stale entry-to-observation clock.
			const double RealizationNow=FPlatformTime::Seconds();
			if (DecalMaterializer)
			{
				const FEFCalystoDecalRealizationObservation& Decals=DecalMaterializer->Observe(RealizationNow);
				Metrics.ReservedDecals=DecalManifest ? DecalManifest->GetElements().Num() : 0;
				Metrics.VerifiedDecals=Decals.VerifiedElements.Num(); Metrics.DecalOwnedPoolActors=Decals.OwnedPoolActors;
				Metrics.DecalRetainedLeases=Decals.RetainedLeases;
				if (Decals.State==EEFCalystoDecalRealizationState::Releasing || Decals.State==EEFCalystoDecalRealizationState::Released)
					return Failure(Decals.FailureCode.IsNone()?FName(TEXT("DecalRealizationFailed")):Decals.FailureCode,
						Decals.Message.IsEmpty()?TEXT("Selected decals released before strict verification."):Decals.Message,Decals.Failure);
				if (Decals.State!=EEFCalystoDecalRealizationState::Prepared)
				{
					Metrics.ObservationCode=TEXT("DECAL_REALIZATION_PENDING");
					Metrics.ObservationMessage=TEXT("Loading and staging the exact reserved decals before player release.");
					Observation.Stage=EEFCalystoObservation::RealizationVerification; return Observation;
				}
			}
			if (ContentMaterializer)
			{
				const FEFCalystoContentRealizationObservation& Content = ContentMaterializer->Observe(RealizationNow, 16);
				Metrics.ReservedContentElements = ContentManifest ? ContentManifest->GetElements().Num() : 0;
				Metrics.VerifiedContentElements = Content.VerifiedElements.Num();
				Metrics.ContentOwnedActors = Content.OwnedActors;
				Metrics.ContentRetainedLeases = Content.RetainedLeases;
				if (Content.State == EEFCalystoContentRealizationState::Releasing
					|| Content.State == EEFCalystoContentRealizationState::Released)
					return Failure(Content.FailureCode.IsNone() ? FName(TEXT("ContentRealizationFailed")) : Content.FailureCode,
						Content.Message.IsEmpty() ? TEXT("Selected content released before strict verification.") : Content.Message, Content.Failure);
				if (Content.State != EEFCalystoContentRealizationState::Prepared)
				{
					Metrics.ObservationCode = TEXT("CONTENT_REALIZATION_PENDING");
					Metrics.ObservationMessage = TEXT("Materializing the exact frozen gameplay content before player release.");
					Observation.Stage=EEFCalystoObservation::RealizationVerification; return Observation;
				}
			}
			TSet<FGuid> ActualChildren,VerifiedArchitecture;
			if (!ArchitectureBatch->Verify(ActualChildren,Error)
				|| !ArchitectureManifest.VerifyRealizedChildren(ActualChildren,VerifiedArchitecture,Error))
				return Failure(TEXT("ArchitectureRealizationInvalid"),Error,EEFCalystoAttemptFailure::Spatial);
			Metrics.VerifiedArchitectureMeshes=ActualChildren.Num();
			if (Metrics.ArchitectureRealizationSeconds<0)
				Metrics.ArchitectureRealizationSeconds=FPlatformTime::Seconds()-ArchitectureRealizationStartedSeconds;
			auto& Evidence = Observation.Evidence;
			Evidence.Entry = NavigationResult.ValidatedEntryTransform;
			Evidence.ReservationHash = Observation.ReservationHash;
			const FString ContentRealization=ContentMaterializer ? ContentMaterializer->GetObservation().RealizationHash : TEXT("NoContent");
			const FString DecalRealization=DecalMaterializer ? DecalMaterializer->GetObservation().RealizationHash : TEXT("NoDecals");
			Evidence.RealizationHash=FMD5::HashAnsiString(*(ArchitectureManifest.GetHash()+TEXT("|")+ContentRealization+TEXT("|")+DecalRealization));
			Evidence.ReservedElements=Observation.ReservedElements; Evidence.VerifiedElements=MoveTemp(VerifiedArchitecture);
			if (ContentMaterializer)
				for (const FGuid& ElementId : ContentMaterializer->GetObservation().VerifiedElements)
					Evidence.VerifiedElements.Add(ElementId);
			if (DecalMaterializer)
				for (const FGuid& ElementId : DecalMaterializer->GetObservation().VerifiedElements)
					Evidence.VerifiedElements.Add(ElementId);
			Evidence.bUniqueOwnedStartAndEnd = Native.NativeStartMarkers.Num() == 1 && Native.NativeEndMarkers.Num() == 1;
			Evidence.bBlockingFloorAndCapsuleClearance = NavigationResult.bEntryTransformValid;
			Evidence.bCompleteRelevantNavigationRoute = NavigationResult.CompleteRoute.Num() > 1;
			Evidence.bMaterialsVerified = true;
			Evidence.bNativeParityVerified = Native.bGraphCompleted && Native.GenerateRequests == 1
				&& NavigationResult.Structure.FloorInstanceCount > 0 && NavigationResult.Structure.WallInstanceCount > 0
				&& NavigationResult.Structure.RoofInstanceCount > 0;
			int32 Themed = 0;
			for (const auto& Room : Native.Rooms)
			{
				if (Room.ThemeId.IsValid()) ++Themed;
				if (Room.ThemeId.IsValid() && Room.Protection != EEFCalystoProtectedRoom::None)
					return Failure(TEXT("ProtectedTheme"), TEXT("A protected native room received a Theme."), EEFCalystoAttemptFailure::Configuration);
			}
			Evidence.bRoomThemeContractVerified = Themed >= 1;
			const bool bGameplayHandoffRequired = Request.PreFloorGameplaySnapshot.IsValid();
			Evidence.bGameplayPreparedWithoutCommit = !bGameplayHandoffRequired
				|| (bGameplaySnapshotReconstructed && GameplayReconstructionRequest == Request.Token.Request
					&& bGameplayProviderPrepared && (!ContentMaterializer
						|| ContentMaterializer->GetObservation().State == EEFCalystoContentRealizationState::Prepared));
			if (!Evidence.IsComplete(Error)) return Failure(TEXT("NativeEvidenceIncomplete"), Error, EEFCalystoAttemptFailure::Spatial);
			Observation.Stage = EEFCalystoObservation::Verified; return Observation;
		}

		virtual bool PublishPrepared(const FEFCalystoAttemptToken& Token, const FEFCalystoCommitEvidence& Evidence,
			double Now, EEFCalystoAttemptFailure& FailureKind, FString& Error) override
		{
			FailureKind = EEFCalystoAttemptFailure::Configuration;
			if (!(Request.Token == Token) || bReleasing || !Request.World.IsValid()
				|| Observation.Stage != EEFCalystoObservation::Verified || !FMath::IsFinite(Now)
				|| Now >= Request.DeadlineSeconds || !Evidence.IsComplete(Error)
				|| Evidence.ReservationHash != Observation.Evidence.ReservationHash
				|| Evidence.RealizationHash != Observation.Evidence.RealizationHash
				|| Evidence.ReservedElements.Num()!=Observation.ReservedElements.Num())
			{
				if (Error.IsEmpty()) Error = TEXT("Native publication requires the exact verified manifest and current attempt.");
				return false;
			}
			for (const auto& Id:Evidence.ReservedElements) if (!Observation.ReservedElements.Contains(Id))
			{ Error=TEXT("Native publication changed a frozen reservation identity."); return false; }
			TSet<FGuid> Children,Verified;
			if (!ArchitectureBatch || !ArchitectureBatch->Verify(Children,Error)
				|| !ArchitectureManifest.VerifyRealizedChildren(Children,Verified,Error)) return false;
			if (DecalMaterializer && !DecalMaterializer->Commit(Token,Now,Error))
			{
				FailureKind=DecalMaterializer->GetObservation().Failure;
				return false;
			}
			if (ContentMaterializer && !ContentMaterializer->Commit(Token,Now,Error))
			{
				FailureKind=ContentMaterializer->GetObservation().Failure;
				return false;
			}
			Error.Reset(); return true;
		}
		virtual EEFCalystoActivationResult OnCommitted(const FEFCalystoAttemptToken& Token, FString& Error) override
		{
			if (!(Request.Token == Token) || bReleasing || Observation.Stage != EEFCalystoObservation::Verified)
			{ Error=TEXT("Accepted native activation lost its verified attempt."); return EEFCalystoActivationResult::InvariantFailure; }
			if (!ArchitectureBatch || !ArchitectureBatch->Activate(Token,Error)) return EEFCalystoActivationResult::InvariantFailure;
			if (DecalMaterializer)
			{
				const EEFCalystoActivationResult DecalActivation=DecalMaterializer->ActivateCommitted(Token,Error);
				if (DecalActivation!=EEFCalystoActivationResult::Activated) return DecalActivation;
				bAcceptedDecals=true;
			}
			if (ContentMaterializer)
			{
				const EEFCalystoActivationResult ContentActivation=ContentMaterializer->ActivateCommitted(Token,Error);
				if (ContentActivation!=EEFCalystoActivationResult::Activated) return ContentActivation;
				bAcceptedContent=true;
			}
			for (auto& Door : Native.NativeEndMarkers)
			{
				auto* Portal = Cast<IEFCalystoDirectorPortal>(Door.Get());
				if (!Portal) { Error=TEXT("Accepted progression door is unavailable."); return EEFCalystoActivationResult::InvariantFailure; }
				Portal->SetDirectorInteractionEnabled(true);
			}
			Error.Reset(); return EEFCalystoActivationResult::Activated;
		}
		virtual void ReleaseAttempt(const FEFCalystoAttemptToken& Token) override
		{
			if (!(Request.Token == Token) || bReleasing) return;
			bReleasing = true;
			CaptureOwnership();
			if (Navigation) Navigation->Cancel();
			if (DecalMaterializer) DecalMaterializer->Release(Token);
			if (ContentMaterializer) ContentMaterializer->Release(Token);
			if (ArchitectureBatch) ArchitectureBatch->Release(Token);
			for (auto& Door : Native.NativeEndMarkers)
				if (auto* Portal = Cast<IEFCalystoDirectorPortal>(Door.Get())) Portal->SetDirectorInteractionEnabled(false);
			if (Adapter) Adapter->BeginCleanup();
		}
		virtual FEFCalystoRollbackEvidence ObserveRelease(const FEFCalystoAttemptToken& Token) const override
		{
			FEFCalystoRollbackEvidence Result;
			if (!(Request.Token == Token) || !bReleasing) { Result.PendingCallbacks = 1; return Result; }
			if (DecalMaterializer)
			{
				DecalMaterializer->Observe(FPlatformTime::Seconds());
				const FEFCalystoRollbackEvidence Decals=DecalMaterializer->GetReleaseEvidence(Token);
				Result.Actors+=Decals.Actors; Result.Instances+=Decals.Instances; Result.Decals+=Decals.Decals;
				Result.Reservations+=Decals.Reservations; Result.PendingCallbacks+=Decals.PendingCallbacks;
				Result.TransientReferences+=Decals.TransientReferences; Result.AttemptLeases+=Decals.AttemptLeases;
				if (!Decals.IsComplete()) return Result;
			}
			if (ContentMaterializer)
			{
				ContentMaterializer->Observe(FPlatformTime::Seconds(),16);
				const FEFCalystoRollbackEvidence Content=ContentMaterializer->GetReleaseEvidence(Token);
				Result.Actors+=Content.Actors; Result.Instances+=Content.Instances; Result.Decals+=Content.Decals;
				Result.Reservations+=Content.Reservations; Result.PendingCallbacks+=Content.PendingCallbacks;
				Result.TransientReferences+=Content.TransientReferences; Result.AttemptLeases+=Content.AttemptLeases;
				if (!Content.IsComplete()) return Result;
			}
			if (ArchitectureBatch)
			{
				ArchitectureBatch->Release(Token); const FEFCalystoRollbackEvidence Architecture=ArchitectureBatch->GetReleaseEvidence();
				Result.Actors+=Architecture.Actors; Result.Instances+=Architecture.Instances; Result.Decals+=Architecture.Decals;
				Result.Reservations+=Architecture.Reservations; Result.NavigationRegistrations+=Architecture.NavigationRegistrations;
				Result.PendingCallbacks+=Architecture.PendingCallbacks; Result.TransientReferences+=Architecture.TransientReferences;
				Result.AttemptLeases+=Architecture.AttemptLeases;
				if (!Architecture.IsComplete()) return Result;
			}
			CaptureOwnership();
			Result.bPCGCleanupComplete = !Adapter || Adapter->IsCleanupComplete();
			Result.NavigationRegistrations = Navigation && !Navigation->IsReleased() ? 1 : 0;
			if (!Result.bPCGCleanupComplete || Result.NavigationRegistrations != 0)
			{ Result.TransientReferences = Adapter ? 1 : 0; Result.Actors = DungeonActor.IsValid() ? 1 : 0; return Result; }
			// Retain exact managed owners through quiescence. A rejected Destroy or a
			// surviving registered component is observable and cannot be hidden by Reset.
			bool bDestroyRejected = false;
			for (const TWeakObjectPtr<AActor>& Ref : OwnedActors)
			{
				AActor* Actor = Ref.Get();
				if (Actor && !Actor->IsActorBeingDestroyed() && !Actor->Destroy(false, false)) bDestroyRejected = true;
			}
			for (const TWeakObjectPtr<AActor>& Ref : OwnedActors)
				if (const AActor* Actor = Ref.Get(); Actor && !Actor->IsActorBeingDestroyed()) ++Result.Actors;
			for (const TWeakObjectPtr<UActorComponent>& Ref : OwnedComponents)
			{
				const UActorComponent* Component = Ref.Get(true);
				if (!Component || !Component->IsRegistered()) continue;
				// Registered components still own rendering, collision or callbacks,
				// even when their actor is already marked for destruction.
				++Result.PendingCallbacks;
				if (const auto* Instances = Cast<UInstancedStaticMeshComponent>(Component)) Result.Instances += Instances->GetInstanceCount();
			}
			if (bDestroyRejected || Result.Actors != 0 || Result.PendingCallbacks != 0 || Result.Instances != 0)
			{
				Result.TransientReferences = Adapter ? 1 : 0;
				Result.AttemptLeases = Request.Configuration ? 1 : 0;
				if (bDestroyRejected && Result.Actors == 0) Result.Actors = 1;
				return Result;
			}
			Adapter.Reset(); Navigation.Reset(); ArchitectureBatch.Reset(); ContentMaterializer.Reset(); DecalMaterializer.Reset(); ArchitectureManifest={};
			ContentManifest.Reset(); DecalManifest.Reset(); ContentPlanReport={}; DecalPlanReport={}; ContentProviderResult={}; ContentProviderRequest={}; ContentProvider=nullptr;
			SurfaceCandidates={}; ArchitectureDecisions.Reset();
			DungeonActor.Reset(); Native = {}; StructuralSources.Reset(); MeshRules.Reset(); NavigationInput = {};
			Request.Configuration.Reset(); Request.World.Reset(); OwnedActors.Reset(); OwnedComponents.Reset();
			return Result;
		}
		virtual bool FinishRequest(const FGuid& RequestId, const bool bAccepted) override
		{
			if (!RequestId.IsValid() || RequestId!=ContentSnapshotRequest || !ContentSnapshot) return true;
			FString Error;
			IEFCalystoContentAttemptProvider* Provider=ResolveUniqueContentProvider(Error);
			if (!Provider || !Provider->FinishRequest(RequestId,ContentSnapshot,bAccepted))
			{
				UE_LOG(LogTemp, Error, TEXT("V7 Calysto terminal gameplay lease could not be released: %s"),
					Error.IsEmpty() ? TEXT("the content provider retained the snapshot") : *Error);
				return false;
			}
			ContentSnapshot.Reset(); ContentSnapshotRequest.Invalidate();
			bGameplaySnapshotReconstructed = false; bGameplayProviderPrepared = false;
			GameplayReconstructionRequest.Invalidate();
			return true;
		}

	private:
		bool PartitionNativeSurfaceMaterials(FString& Error)
		{
			const FEFCalystoStyle* Style = Request.Configuration ? Request.Configuration->FindStyle(Request.Random.StyleId) : nullptr;
			if (!Style || !Adapter)
			{
				Error = TEXT("The selected Style or native room configuration is unavailable for material realization.");
				return false;
			}
			const FTransform& DungeonTransform = Adapter->GetRoomConfig().DungeonTransform;
			const auto IsSurfaceRole = [](const EEFCalystoStructuralRole Role)
			{
				return Role == EEFCalystoStructuralRole::Floor || Role == EEFCalystoStructuralRole::Wall || Role == EEFCalystoStructuralRole::Roof;
			};
			const auto StyleMaterial = [Style](const EEFCalystoStructuralRole Role)
			{
				return Role == EEFCalystoStructuralRole::Floor ? Style->Materials.Floor.ToSoftObjectPath()
					: Role == EEFCalystoStructuralRole::Wall ? Style->Materials.Wall.ToSoftObjectPath()
					: Style->Materials.Roof.ToSoftObjectPath();
			};
			const auto RoomMaterial = [](const FEFCalystoNativeRoom& Room, const EEFCalystoStructuralRole Role)
			{
				return Role == EEFCalystoStructuralRole::Floor ? Room.FloorMaterial
					: Role == EEFCalystoStructuralRole::Wall ? Room.WallMaterial : Room.RoofMaterial;
			};
			const auto ContainsXY = [](const FBox& Outer, const FBox& Inner)
			{
				return Inner.Min.X >= Outer.Min.X - 0.25 && Inner.Max.X <= Outer.Max.X + 0.25
					&& Inner.Min.Y >= Outer.Min.Y - 0.25 && Inner.Max.Y <= Outer.Max.Y + 0.25;
			};
			const auto ResolveTarget = [&](const FEFCalystoStructuralSource& Source, const int32 InstanceIndex,
				const EEFCalystoStructuralRole Role, const FBox& Footprint, FSoftObjectPath& OutMaterial)
			{
				const FSoftObjectPath BaseMaterial = StyleMaterial(Role);
				if (BaseMaterial.IsNull())
				{
					Error = TEXT("The selected Style has an empty required surface material.");
					return false;
				}
				if (Role == EEFCalystoStructuralRole::Wall)
				{
					const int64* RoomId = Source.NativeWallRoomIds.Find(InstanceIndex);
					const FSoftObjectPath* NativeMaterial = Source.NativeWallMaterials.Find(InstanceIndex);
					if (!RoomId || !NativeMaterial || NativeMaterial->IsNull())
					{
						Error = TEXT("A structural wall lacks its exact final native room/material provenance.");
						return false;
					}
					if (Source.NativeStyleOwnedWallIndices.Contains(InstanceIndex))
					{
						if (*RoomId != 0 || *NativeMaterial != BaseMaterial)
						{
							Error = TEXT("A Style-owned structural wall disagrees with its frozen selected Style material.");
							return false;
						}
						OutMaterial = BaseMaterial;
						return true;
					}
					if (Source.NativeDoorWallIndices.Contains(InstanceIndex))
					{
						const FEFCalystoNativeRoom* DoorRoom = Native.Rooms.FindByPredicate([RoomId](const FEFCalystoNativeRoom& Candidate)
						{
							return Candidate.RoomId == *RoomId;
						});
						if (!DoorRoom || *RoomId <= 0)
						{
							Error = TEXT("A final Wall - Door record has no exact native room identity.");
							return false;
						}
						// A shared doorway can validly carry a material distinct from either
						// adjacent room. The typed carrier on this exact final point was
						// already validated by the adapter, so never collapse it to RoomId.
						OutMaterial = *NativeMaterial;
						return true;
					}
					const FEFCalystoNativeRoom* Room = Native.Rooms.FindByPredicate([RoomId](const FEFCalystoNativeRoom& Candidate)
					{
						return Candidate.RoomId == *RoomId;
					});
					if (!Room || Room->WallMaterial.IsNull() || *NativeMaterial != Room->WallMaterial)
					{
						const FString ComponentPath = Source.Component.IsValid() ? Source.Component->GetPathName() : TEXT("<missing>");
						const FString ExpectedMaterial = Room ? Room->WallMaterial.ToString() : TEXT("<missing room>");
						const FString ThemeId = Room ? Room->ThemeId.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("<missing room>");
						Error = FString::Printf(TEXT("A final native wall record disagrees with its frozen room material authority: component=%s instance=%d room=%lld emitted=%s expected=%s theme=%s style=%s."),
							*ComponentPath, InstanceIndex, *RoomId, *NativeMaterial->ToString(), *ExpectedMaterial, *ThemeId, *BaseMaterial.ToString());
						return false;
					}
					OutMaterial = *NativeMaterial;
					return true;
				}
				TSet<FSoftObjectPath> ThemeOverrides;
				for (const FEFCalystoNativeRoom& Room : Native.Rooms)
				{
					// Floor and roof ownership remains whole-footprint based. Final walls use
					// the exact PCG record above because shared boundaries have no reliable AABB owner.
					if (!ContainsXY(Room.LocalBounds, Footprint)) continue;
					const FSoftObjectPath Material = RoomMaterial(Room, Role);
					if (Material.IsNull())
					{
						Error = TEXT("A generated room has an empty required effective surface material.");
						return false;
					}
					if (Material != BaseMaterial) ThemeOverrides.Add(Material);
				}
				if (ThemeOverrides.Num() > 1)
				{
					TArray<FString> Paths;
					for (const FSoftObjectPath& Path : ThemeOverrides) Paths.Add(Path.ToString());
					Paths.Sort();
					Error = FString::Printf(TEXT("A structural surface is wholly owned by incompatible Theme material overrides: %s."), *FString::Join(Paths, TEXT(", ")));
					return false;
				}
				OutMaterial = BaseMaterial;
				if (ThemeOverrides.Num() == 1) OutMaterial = *ThemeOverrides.CreateConstIterator();
				return true;
			};
			const auto ResolveMaterial = [&Error](const FSoftObjectPath& Path) -> UMaterialInterface*
			{
				UMaterialInterface* Material = Cast<UMaterialInterface>(Path.ResolveObject());
				if (!Material) Error = TEXT("A selected surface material lease disappeared during native realization: ") + Path.ToString();
				return Material;
			};
			const auto ApplyMaterial = [](UStaticMeshComponent& Component, UMaterialInterface& Material)
			{
				for (int32 Slot = 0; Slot < FMath::Max(1, Component.GetNumMaterials()); ++Slot) Component.SetMaterial(Slot, &Material);
			};

			TArray<FEFCalystoStructuralSource> Partitioned;
			Partitioned.Reserve(StructuralSources.Num() * 2);
			for (const FEFCalystoStructuralSource& Source : StructuralSources)
			{
				UStaticMeshComponent* Component = Source.Component.Get();
				if (!Component || !Component->GetStaticMesh())
				{
					Error = TEXT("A structural source disappeared before material realization.");
					return false;
				}
				if (!IsSurfaceRole(Source.Role))
				{
					Partitioned.Add(Source);
					continue;
				}
				const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component);
				const int32 Count = Instances ? Instances->GetInstanceCount() : 1;
				if (Count <= 0)
				{
					Error = TEXT("A required native surface has no instances for material realization.");
					return false;
				}
				TMap<FSoftObjectPath, TArray<int32>> Groups;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					FMatrix WorldMatrix = Component->GetComponentTransform().ToMatrixWithScale();
					if (Instances)
					{
						if (!Instances->PerInstanceSMData.IsValidIndex(Index))
						{
							Error = TEXT("Native surface instance storage is unavailable for material realization.");
							return false;
						}
						WorldMatrix = Instances->PerInstanceSMData[Index].Transform * WorldMatrix;
					}
					FBox LocalFootprint(ForceInit);
					if (!ComputeDungeonLocalFootprint(*Component, WorldMatrix, DungeonTransform, LocalFootprint, Error)) return false;
					FSoftObjectPath Target;
					if (!ResolveTarget(Source, Index, Source.Role, LocalFootprint, Target)) return false;
					Groups.FindOrAdd(Target).Add(Index);
				}
				if (Groups.Num() == 1)
				{
					const FSoftObjectPath Target = Groups.CreateConstIterator().Key();
					UMaterialInterface* Material = ResolveMaterial(Target);
					if (!Material) return false;
					ApplyMaterial(*Component, *Material);
					Partitioned.Add(Source);
					continue;
				}
				if (!Instances)
				{
					Error = TEXT("A non-instanced native surface produced incompatible material ownership.");
					return false;
				}
				AActor* Owner = Component->GetOwner();
				if (!Owner)
				{
					Error = TEXT("A mixed native surface has no owning actor for transactional material realization.");
					return false;
				}
				TArray<FSoftObjectPath> Targets;
				Groups.GetKeys(Targets);
				Targets.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
				for (const FSoftObjectPath& Target : Targets)
				{
					UMaterialInterface* Material = ResolveMaterial(Target);
					if (!Material) return false;
					const TArray<int32>* Indices = Groups.Find(Target);
					if (!Indices || Indices->IsEmpty())
					{
						Error = TEXT("A material partition lost its native instance group.");
						return false;
					}
					UInstancedStaticMeshComponent* Replacement = NewObject<UInstancedStaticMeshComponent>(Owner, Component->GetClass(),
						MakeUniqueObjectName(Owner, Component->GetClass(), FName(TEXT("EFCalystoDirectorMaterial"))), RF_Transient);
					if (!Replacement)
					{
						Error = TEXT("A transactional material partition component could not be created.");
						return false;
					}
					Owner->AddInstanceComponent(Replacement);
					// This component is an exact transient partition of native PCG
					// construction instances. Preserve the source provenance and mark
					// the replacement as generated before it becomes the active
					// blocking/navigation structure; it is never an unrelated overlay.
					Replacement->ComponentTags = Component->ComponentTags;
					Replacement->ComponentTags.AddUnique(FName(TEXT("PCG Generated Component")));
					Replacement->ComponentTags.AddUnique(FName(TEXT("EFCalystoDirectorMaterialPartition")));
					Replacement->SetMobility(Component->Mobility);
					Replacement->SetStaticMesh(Component->GetStaticMesh());
					Replacement->SetWorldTransform(Component->GetComponentTransform());
					Replacement->SetCollisionProfileName(Component->GetCollisionProfileName());
					Replacement->SetCollisionEnabled(Component->GetCollisionEnabled());
					Replacement->SetCollisionObjectType(Component->GetCollisionObjectType());
					Replacement->SetCollisionResponseToChannels(Component->GetCollisionResponseToChannels());
					Replacement->SetGenerateOverlapEvents(Component->GetGenerateOverlapEvents());
					Replacement->SetWalkableSlopeOverride(Component->GetWalkableSlopeOverride());
					Replacement->SetCanEverAffectNavigation(Component->CanEverAffectNavigation());
					Replacement->SetVisibility(Component->IsVisible(), false);
					Replacement->SetHiddenInGame(Component->bHiddenInGame, false);
					Replacement->SetCastShadow(Component->CastShadow);
					Replacement->InstancingRandomSeed = Instances->InstancingRandomSeed;
					Replacement->InstanceLODDistanceScale = Instances->InstanceLODDistanceScale;
					Replacement->InstanceMinDrawDistance = Instances->InstanceMinDrawDistance;
					Replacement->InstanceStartCullDistance = Instances->InstanceStartCullDistance;
					Replacement->InstanceEndCullDistance = Instances->InstanceEndCullDistance;
					Replacement->SetNumCustomDataFloats(Instances->NumCustomDataFloats);
					Replacement->PreAllocateInstancesMemory(Indices->Num());
					for (const int32 Index : *Indices)
					{
						if (!Instances->PerInstanceSMData.IsValidIndex(Index))
						{
							Error = TEXT("A material partition encountered an invalid native instance index.");
							return false;
						}
						Replacement->PerInstanceSMData.Add(Instances->PerInstanceSMData[Index]);
						if (Instances->NumCustomDataFloats > 0)
						{
							const int64 Offset = static_cast<int64>(Index) * Instances->NumCustomDataFloats;
							const int64 End = Offset + Instances->NumCustomDataFloats;
							if (Offset < 0 || End > Instances->PerInstanceSMCustomData.Num())
							{
								Error = TEXT("Native material partition custom data is incomplete.");
								return false;
							}
							Replacement->PerInstanceSMCustomData.Append(Instances->PerInstanceSMCustomData.GetData() + Offset, Instances->NumCustomDataFloats);
						}
					}
					ApplyMaterial(*Replacement, *Material);
					// Raw matrices are intentional: UE decomposes a zero-height wall matrix
					// through the public transform API and loses its authored rotation.
					Replacement->InvalidateInstanceDataTracking();
					Replacement->RegisterComponentWithWorld(Component->GetWorld());
					if (!Replacement->IsRegistered() || Replacement->GetWorld() != Component->GetWorld()
						|| Replacement->GetOwner() != Owner || Replacement->GetInstanceCount() != Indices->Num())
					{
						Error = TEXT("A transactional material partition component did not register with its exact native owner.");
						return false;
					}
					FEFCalystoStructuralSource ReplacementSource;
					ReplacementSource.Component = Replacement;
					ReplacementSource.Role = Source.Role;
					if (Source.Role == EEFCalystoStructuralRole::Wall)
					{
						for (int32 NewIndex = 0; NewIndex < Indices->Num(); ++NewIndex)
						{
							const int32 OriginalIndex = (*Indices)[NewIndex];
							const int64* RoomId = Source.NativeWallRoomIds.Find(OriginalIndex);
							const FSoftObjectPath* NativeMaterial = Source.NativeWallMaterials.Find(OriginalIndex);
							if (!RoomId || !NativeMaterial)
							{
								Error = TEXT("A material partition lost exact final-wall provenance.");
								return false;
							}
							ReplacementSource.NativeWallRoomIds.Add(NewIndex, *RoomId);
							ReplacementSource.NativeWallMaterials.Add(NewIndex, *NativeMaterial);
							if (Source.NativeDoorWallIndices.Contains(OriginalIndex))
							{
								ReplacementSource.NativeDoorWallIndices.Add(NewIndex);
							}
							if (Source.NativeStyleOwnedWallIndices.Contains(OriginalIndex))
							{
								ReplacementSource.NativeStyleOwnedWallIndices.Add(NewIndex);
							}
						}
					}
					Partitioned.Add(MoveTemp(ReplacementSource));
				}
				Component->ComponentTags.AddUnique(FName(TEXT("EFCalystoDirectorMaterialSuperseded")));
				Component->SetCanEverAffectNavigation(false);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Component->SetVisibility(false, true);
				Component->SetHiddenInGame(true, true);
			}
			Partitioned.Sort([](const FEFCalystoStructuralSource& A, const FEFCalystoStructuralSource& B)
			{
				return A.Component->GetPathName() < B.Component->GetPathName();
			});
			StructuralSources = MoveTemp(Partitioned);
			CaptureOwnership();
			return true;
		}

		bool PrepareGameplayHandoff(const bool bHasContent, EEFCalystoAttemptFailure& FailureKind, FString& Error)
		{
			if (!Request.PreFloorGameplaySnapshot)
			{
				if (bHasContent)
				{
					FailureKind = EEFCalystoAttemptFailure::Configuration;
					Error = TEXT("V7 selected gameplay content has no immutable pre-travel snapshot.");
					return false;
				}
				return true;
			}
			if (!ContentSnapshot || ContentSnapshotRequest != Request.Token.Request
				|| ContentSnapshot != Request.PreFloorGameplaySnapshot || ContentSnapshot->GetCanonicalHash().IsEmpty())
			{
				FailureKind = EEFCalystoAttemptFailure::Configuration;
				Error = TEXT("The immutable V7 pre-travel gameplay snapshot was not retained for this request.");
				return false;
			}
			if (!ContentProvider)
			{
				ContentProvider = ResolveUniqueContentProvider(Error);
				if (!ContentProvider)
				{
					FailureKind = EEFCalystoAttemptFailure::Configuration;
					return false;
				}
			}
			ContentProviderRequest = {};
			ContentProviderRequest.Token = Request.Token;
			ContentProviderRequest.World = Request.World;
			ContentProviderRequest.AttemptOwner = DungeonActor;
			ContentProviderRequest.Configuration = Request.Configuration;
			ContentProviderRequest.Random = Request.Random;
			ContentProviderRequest.FloorNumber = Request.Random.FloorNumber;
			ContentProviderRequest.DeadlineSeconds = Request.DeadlineSeconds;
			ContentProviderRequest.ExistingSnapshot = ContentSnapshot;
			if (!bGameplaySnapshotReconstructed)
			{
				if (GameplayReconstructionRequest.IsValid() && GameplayReconstructionRequest != Request.Token.Request)
				{
					FailureKind = EEFCalystoAttemptFailure::Configuration;
					Error = TEXT("A V7 gameplay reconstruction is still owned by another request.");
					return false;
				}
				if (!ContentProvider->ReconstructPreTravelSnapshot(Request.Token, Request.World.Get(), ContentSnapshot, Error))
				{
					// The provider reconstructs a frozen cross-world gameplay lease. A failure
					// here is a deterministic bridge/lease or resource contract defect; a new
					// topology seed cannot repair it.
					FailureKind = EEFCalystoAttemptFailure::Configuration;
					return false;
				}
				bGameplaySnapshotReconstructed = true;
				GameplayReconstructionRequest = Request.Token.Request;
			}
			if (bGameplayProviderPrepared) return true;
			if (!ContentProvider->PrepareAttempt(ContentProviderRequest, ContentProviderResult, Error))
			{
				FailureKind = EEFCalystoAttemptFailure::Configuration;
				return false;
			}
			if (!ContentProviderResult.PreFloorSnapshot || !ContentProviderResult.GameplayBridge
				|| ContentProviderResult.PreFloorSnapshot != ContentSnapshot
				|| ContentProviderResult.PreFloorSnapshot->GetCanonicalHash() != ContentSnapshot->GetCanonicalHash())
			{
				FailureKind = EEFCalystoAttemptFailure::Configuration;
				Error = TEXT("The registered V7 gameplay provider changed or omitted the immutable pre-travel snapshot.");
				return false;
			}
			bGameplayProviderPrepared = true;
			return true;
		}

		bool FreezeArchitecture(const FEFCalystoStructuralNavigationResult& ReadyNavigation,
			EEFCalystoAttemptFailure& FailureKind,FString& Error)
		{
			FailureKind=EEFCalystoAttemptFailure::Configuration;
			const auto* Style=Request.Configuration->FindStyle(Request.Random.StyleId);
			if (!Style) { Error=TEXT("The frozen selected Style disappeared before reservation."); return false; }
			bool bHasArchitecture=!Style->Architecture.Decoration.IsEmpty();
			for (const auto& Room:Native.Rooms)
				if (const auto* Theme=Request.Configuration->FindTheme(Room.ThemeId)) bHasArchitecture|=!Theme->Architecture.IsEmpty();
			const bool bHasContent=HasAuthoredContent(*Request.Configuration,Request.Random.StyleId);
			const bool bHasDecals=FEFCalystoDecalReservationPlanner::HasPotentiallyActiveDecals(*Request.Configuration,Request.Random.StyleId);
			if (bHasArchitecture || bHasContent || bHasDecals)
			{
				TArray<UObject*> Resources;
				if (bHasArchitecture)
				{
					TArray<FSoftObjectPath> Reachable,Children;
					if (!Request.Configuration->GetReachableVisualDependencies(Request.Random,Reachable,Error)
						|| !GetBakedVisualDependencies(Reachable,Children,Error)) return false;
					for (const auto& Path:Children) Reachable.AddUnique(Path);
					for (const auto& Path:Reachable)
					{
						UObject* Resource=Path.ResolveObject();
						if (!Resource) { FailureKind=EEFCalystoAttemptFailure::Resource; Error=TEXT("An attempt-leased architecture resource disappeared before reservation."); return false; }
						Resources.Add(Resource);
					}
				}
				FEFCalystoSurfaceCandidateRequest Surfaces;
				Surfaces.Configuration=Request.Configuration.Get(); Surfaces.RoomConfiguration=&Adapter->GetRoomConfig();
				Surfaces.Native=&Native; Surfaces.NavigationInput=&NavigationInput; Surfaces.Navigation=&ReadyNavigation;
				Surfaces.LoadedArchitectureResources=Resources;
				if (!FEFCalystoSurfaceCandidates::Build(Surfaces,SurfaceCandidates))
				{
					FailureKind=SurfaceCandidates.Failure;
					Error=SurfaceCandidates.Message; return false;
				}
				if (bHasArchitecture)
				{
					FEFCalystoArchitectureRequest Planning;
					Planning.Random=Request.Random; Planning.Rooms=SurfaceCandidates.Rooms;
					Planning.Opportunities=SurfaceCandidates.ArchitectureOpportunities;
					if (!FEFCalystoArchitecturePlanner::Build(*Request.Configuration,Planning,ArchitectureDecisions,Error)) return false;
				}
			}
			// The source-world snapshot is reconstructed only after the native entry and
			// route are valid.  It is deliberately before every gameplay reservation,
			// including an otherwise empty catalog, and survives spatial reseeds.
			if (!PrepareGameplayHandoff(bHasContent, FailureKind, Error)) return false;
			if (!FEFCalystoArchitectureManifest::Build(Request.Random,ArchitectureDecisions,ArchitectureManifest,Error)) return false;
			if (!ArchitectureBatch || !ArchitectureBatch->Begin(Request.Token,Request.World.Get(),DungeonActor.Get(),
				ArchitectureManifest.GetMeshes(),Request.DeadlineSeconds,Error)) return false;
			if (bHasDecals)
			{
				TSharedRef<FEFCalystoReservedDecalManifest> Planned=MakeShared<FEFCalystoReservedDecalManifest>();
				if (!FEFCalystoDecalReservationPlanner::Build(*Request.Configuration,Request.Random,SurfaceCandidates.DecalOpportunities,
					*Planned,DecalPlanReport))
				{
					FailureKind=DecalPlanReport.bSpatialFailure ? EEFCalystoAttemptFailure::Spatial : EEFCalystoAttemptFailure::Configuration;
					Error=DecalPlanReport.Message; return false;
				}
				DecalManifest=Planned; DecalMaterializer=MakeUnique<FEFCalystoDecalMaterializer>();
				if (!DecalMaterializer->Begin(Request.Token,Request.Configuration.ToSharedRef(),Adapter.ToSharedRef(),Request.World.Get(),
					Planned,Request.DeadlineSeconds,FPlatformTime::Seconds(),Error))
				{
					FailureKind=DecalMaterializer->GetObservation().FailureCode.IsNone()
						? EEFCalystoAttemptFailure::Configuration : DecalMaterializer->GetObservation().Failure;
					return false;
				}
				bDecalFrozen=true;
			}
			if (!bHasContent) return true;

			FEFCalystoContentReservationRequest Planning;
			Planning.Random=Request.Random; Planning.Rooms=SurfaceCandidates.Rooms; Planning.Surfaces=SurfaceCandidates.Surfaces;
			// Surface feasibility proves a full support envelope, but the deterministic
			// per-entry jitter is calculated by the reservation planner. Bind that exact
			// transform to UE's own deferred-spawn collision predicate before Chance.
			const TWeakObjectPtr<UWorld> PlacementWorld=Request.World;
			Planning.bRequireExactPlacementPreflight=true;
			Planning.ExactPlacementPreflight=[PlacementWorld](const FEFCalystoContentEntry& Entry,
				const FTransform& Transform, const FString& FrozenCollisionHash, FString& OutError)
			{
				OutError.Reset();
				UWorld* const World=PlacementWorld.Get();
				UClass* const ActorClass=Cast<UClass>(Entry.ActorClass.ToSoftObjectPath().ResolveObject());
				if (!IsInGameThread() || !IsValid(World) || !ActorClass || !ActorClass->IsChildOf(AActor::StaticClass())
					|| ActorClass->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated))
				{
					OutError=TEXT("The retained world or selected actor class disappeared before exact jitter collision preflight.");
					return EEFCalystoExactPlacementPreflight::Invalid;
				}
				FEFCalystoContentCollisionContract Contract;
				if (!FEFCalystoContentCollisionContracts::Build(ActorClass,Contract,OutError)
					|| FrozenCollisionHash.IsEmpty() || Contract.Hash!=FrozenCollisionHash)
				{
					if (OutError.IsEmpty()) OutError=TEXT("The selected actor CDO collision contract changed after surface feasibility.");
					return EEFCalystoExactPlacementPreflight::Invalid;
				}
				const AActor* const Template=ActorClass->GetDefaultObject<AActor>();
				const USceneComponent* const Root=Template?Template->GetRootComponent():nullptr;
				if (!IsValid(Template) || (Root && !IsValid(Root)) || Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized())
				{
					OutError=TEXT("The selected actor CDO has no valid root transform for exact jitter collision preflight.");
					return EEFCalystoExactPlacementPreflight::Invalid;
				}
				const FTransform FinalRootTransform=Root
					? FTransform(Root->GetRelativeRotation(),Root->GetRelativeLocation(),Root->GetRelativeScale3D())*Transform : Transform;
				return World->EncroachingBlockingGeometry(Template,FinalRootTransform.GetLocation(),FinalRootTransform.Rotator())
					? EEFCalystoExactPlacementPreflight::SpatiallyBlocked : EEFCalystoExactPlacementPreflight::Feasible;
			};
			Planning.InitialUsage=ContentProviderResult.InitialUsage; Planning.LastSelectedFloor=ContentProviderResult.LastSelectedFloor;
			Planning.BlockedEntryIds=ContentProviderResult.BlockedEntryIds;
			Planning.GraveyardEligibleEntryIds=ContentProviderResult.GraveyardEligibleEntryIds;
			Planning.ResourceBudget=ContentProviderResult.ResourceBudget; Planning.TraitSnapshot=ContentProviderResult.TraitSnapshot;
			// Project-owned Calysto chests expose exactly three immutable inventory slots.  Compatibility
			// is resolved from the authored Container Content identities, before container selection.
			for (const FEFCalystoContentRoom& Room : SurfaceCandidates.Rooms)
			{
				TArray<FEFCalystoResolvedContentGroup> Groups;
				if (!Request.Configuration->ResolveContent(Request.Random.StyleId,Room.ThemeId,Groups,Error)) return false;
				TSet<FGuid> Contents;
				for (const FEFCalystoResolvedContentGroup& Group : Groups)
					if (Group.Content.Role==EEFCalystoGameplayRole::ContainerContent)
						for (const FEFCalystoContentEntry& Entry : Group.Content.Entries) Contents.Add(Entry.Selection.Id);
				for (const FEFCalystoResolvedContentGroup& Group : Groups)
					if (Group.Content.Role==EEFCalystoGameplayRole::Container)
						for (const FEFCalystoContentEntry& Entry : Group.Content.Entries)
						{
							auto& Capacity=Planning.ContainerCapacities.FindOrAdd(Entry.Selection.Id);
							Capacity.Slots=3; Capacity.CompatibleEntryIds.Append(Contents);
						}
			}
			TSharedRef<FEFCalystoReservedContentManifest> Planned=MakeShared<FEFCalystoReservedContentManifest>();
			if (!FEFCalystoContentReservationPlanner::Build(*Request.Configuration,Planning,*Planned,ContentPlanReport))
			{
				FailureKind=ContentPlanReport.Status==EEFCalystoContentPlanningStatus::WorkLimitExceeded
					? EEFCalystoAttemptFailure::Spatial : EEFCalystoAttemptFailure::Configuration;
				Error=ContentPlanReport.Message; return false;
			}
			ContentManifest=Planned;
			FEFCalystoContentGameplayContext Context;
			if (!ContentProvider->BuildMaterializationContext(ContentProviderRequest,ContentProviderResult,Planned,Context,Error)) return false;
			ContentMaterializer=MakeUnique<FEFCalystoContentMaterializer>();
			if (!ContentMaterializer->Begin(Context,ContentProviderResult.GameplayBridge.ToSharedRef(),FPlatformTime::Seconds(),Error))
			{
				FailureKind=EEFCalystoAttemptFailure::Configuration; return false;
			}
			bContentFrozen=true;
			return true;
		}
		void CaptureOwnership() const
		{
			const auto AddActor = [this](AActor* Actor)
			{
				if (!IsValid(Actor)) return;
				OwnedActors.AddUnique(Actor);
				TInlineComponentArray<UActorComponent*> Components(Actor);
				for (UActorComponent* Component : Components) if (IsValid(Component)) OwnedComponents.AddUnique(Component);
			};
			AddActor(DungeonActor.Get());
			const UPCGComponent* Component = Adapter ? Adapter->GetComponent() : nullptr;
			if (!Component || !Component->AreManagedResourcesAccessible()) return;
			Component->ForEachConstManagedResource([&](const UPCGManagedResource* Resource)
			{
				if (!Resource) return;
				if (const auto* Actors = Cast<UPCGManagedActors>(Resource))
					for (const auto& Ref : Actors->GetConstGeneratedActors()) AddActor(Ref.Get());
				if (const auto* Single = Cast<UPCGManagedComponent>(Resource))
				{
					if (UActorComponent* Generated = Single->GeneratedComponent.Get()) OwnedComponents.AddUnique(Generated);
				}
				else if (const auto* List = Cast<UPCGManagedComponentList>(Resource))
					for (const auto& Ref : List->GeneratedComponents) if (UActorComponent* Generated = Ref.Get()) OwnedComponents.AddUnique(Generated);
			});
		}
		FEFCalystoDirectorAttemptObservation Failure(FName Code, const FString& Error, EEFCalystoAttemptFailure Kind)
		{
			Observation.Stage = EEFCalystoObservation::Failed; Observation.FailureCode = Code;
			Observation.Message = Error; Observation.Failure = Kind;
			Observation.Metrics.ObservationCode = Code; Observation.Metrics.ObservationMessage = Error;
			return Observation;
		}
		bool MakeNavigationInput(FString& Error)
		{
			NavigationInput.AttemptId = Request.Token.Attempt; NavigationInput.World = Request.World;
			NavigationInput.DungeonOwner = DungeonActor; NavigationInput.StructuralSources = StructuralSources;
			NavigationInput.StartMarkers = Native.NativeStartMarkers; NavigationInput.EndMarkers = Native.NativeEndMarkers;
			// NativeRoom.LocalBounds is expressed in dungeon coordinates, not the
			// individual room point's transform coordinates.
			const FTransform& BoundsToWorld = Adapter->GetRoomConfig().DungeonTransform;
			int32 Starts = 0, Ends = 0;
			for (const auto& Room : Native.Rooms)
			{
				if (EnumHasAnyFlags(Room.Protection, EEFCalystoProtectedRoom::Start))
				{ ++Starts; NavigationInput.StartRoom = {Room.RoomId, BoundsToWorld, Room.LocalBounds}; }
				if (EnumHasAnyFlags(Room.Protection, EEFCalystoProtectedRoom::End))
				{ ++Ends; NavigationInput.EndRoom = {Room.RoomId, BoundsToWorld, Room.LocalBounds}; }
			}
			if (Starts != 1 || Ends != 1 || Native.NativeEndMarkers.Num() != 1)
			{ Error = TEXT("Native output must identify one owned Start room and one owned End room."); return false; }
			auto* Portal = Cast<IEFCalystoDirectorPortal>(Native.NativeEndMarkers[0].Get());
			if (!Portal) { Error = TEXT("The native End marker does not implement the travel contract."); return false; }
			if (!Portal->SetDirectorAppearance(Request.Configuration->FindStyle(Request.Random.StyleId)->Architecture.ProgressionDoorMesh.Get()))
			{ Error = TEXT("The selected floor door appearance could not be applied."); return false; }
			Portal->SetDirectorInteractionEnabled(false); NavigationInput.EndApproachWorld = Portal->GetDirectorApproachWorld();
			auto* Character = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(Request.World.Get(), 0));
			if (!Character || !Character->GetCapsuleComponent()) { Error = TEXT("The protected player capsule is unavailable."); return false; }
			NavigationInput.ProtectedPlayer = Character;
			Character->GetCapsuleComponent()->GetScaledCapsuleSize(NavigationInput.CapsuleRadius, NavigationInput.CapsuleHalfHeight);
			NavigationInput.AgentProperties = Character->GetNavAgentPropertiesRef();
			if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
				NavigationInput.MaximumWalkableSlopeDegrees = Movement->GetWalkableFloorAngle();
			NavigationInput.RequestDeadlineSeconds = Request.DeadlineSeconds;
			return true;
		}
		bool VerifyMaterials(FString& Error) const
		{
			const auto* Style = Request.Configuration->FindStyle(Request.Random.StyleId);
			if (!Style)
			{
				Error = TEXT("The selected Style disappeared before material verification.");
				return false;
			}
			const FTransform& DungeonTransform = Adapter->GetRoomConfig().DungeonTransform;
			const auto RoleLabel = [](const EEFCalystoStructuralRole Role)
			{
				switch (Role)
				{
				case EEFCalystoStructuralRole::Floor: return TEXT("Floor");
				case EEFCalystoStructuralRole::Wall: return TEXT("Wall");
				case EEFCalystoStructuralRole::Roof: return TEXT("Roof");
				default: return TEXT("Other");
				}
			};
			const auto StyleMaterial = [&Style](const EEFCalystoStructuralRole Role)
			{
				return Role == EEFCalystoStructuralRole::Floor ? Style->Materials.Floor.ToSoftObjectPath()
					: Role == EEFCalystoStructuralRole::Wall ? Style->Materials.Wall.ToSoftObjectPath()
					: Style->Materials.Roof.ToSoftObjectPath();
			};
			const auto RoomMaterial = [](const FEFCalystoNativeRoom& Room, const EEFCalystoStructuralRole Role)
			{
				return Role == EEFCalystoStructuralRole::Floor ? Room.FloorMaterial
					: Role == EEFCalystoStructuralRole::Wall ? Room.WallMaterial : Room.RoofMaterial;
			};
			const auto WitnessKey = [&RoleLabel](const FEFCalystoNativeRoom& Room, const EEFCalystoStructuralRole Role)
			{
				return Room.ThemeId.ToString(EGuidFormats::Digits) + TEXT("|") + RoleLabel(Role);
			};
			const auto IntersectsXY = [](const FBox& A, const FBox& B)
			{
				return A.Min.X <= B.Max.X + 0.25 && A.Max.X >= B.Min.X - 0.25
					&& A.Min.Y <= B.Max.Y + 0.25 && A.Max.Y >= B.Min.Y - 0.25;
			};
			const auto ContainsXY = [](const FBox& Outer, const FBox& Inner)
			{
				return Inner.Min.X >= Outer.Min.X - 0.25 && Inner.Max.X <= Outer.Max.X + 0.25
					&& Inner.Min.Y >= Outer.Min.Y - 0.25 && Inner.Max.Y <= Outer.Max.Y + 0.25;
			};
			const auto FindRoom = [this](const int64 RoomId) -> const FEFCalystoNativeRoom*
			{
				return Native.Rooms.FindByPredicate([RoomId](const FEFCalystoNativeRoom& Room) { return Room.RoomId == RoomId; });
			};
			TMap<FString, FSoftObjectPath> RequiredMaterialWitnesses;
			for (const FEFCalystoNativeRoom& Room : Native.Rooms)
			{
				if (!Room.ThemeId.IsValid()) continue;
				for (const EEFCalystoStructuralRole Role : { EEFCalystoStructuralRole::Floor, EEFCalystoStructuralRole::Wall, EEFCalystoStructuralRole::Roof })
				{
					const FSoftObjectPath Expected = RoomMaterial(Room, Role);
					if (Expected != StyleMaterial(Role)) RequiredMaterialWitnesses.FindOrAdd(WitnessKey(Room, Role)) = Expected;
				}
			}
			TSet<FString> MaterialWitnesses;
			int32 Inspected = 0;
			for (const auto& Source : StructuralSources)
			{
				if (Source.Role != EEFCalystoStructuralRole::Floor && Source.Role != EEFCalystoStructuralRole::Wall && Source.Role != EEFCalystoStructuralRole::Roof) continue;
				const auto* Component = Source.Component.Get();
				if (!Component || !Component->GetStaticMesh()) { Error = TEXT("A required material surface disappeared."); return false; }
				const UMaterialInterface* ActualMaterial = Component->GetMaterial(0);
				if (!ActualMaterial) { Error = TEXT("A required material surface has no assigned material."); return false; }
				const auto* Instances = Cast<UInstancedStaticMeshComponent>(Component);
				const int32 Count = Instances ? Instances->GetInstanceCount() : 1;
				if (Source.Role == EEFCalystoStructuralRole::Wall
					&& (Source.NativeWallRoomIds.Num() != Count || Source.NativeWallMaterials.Num() != Count))
				{
					Error = TEXT("A final-wall material component lost its one-to-one native provenance.");
					return false;
				}
				for (const int32 StyleIndex : Source.NativeStyleOwnedWallIndices)
				{
					if (StyleIndex < 0 || StyleIndex >= Count)
					{
						Error = TEXT("A Style-owned final-wall index is outside its material component.");
						return false;
					}
				}
				for (const int32 DoorIndex : Source.NativeDoorWallIndices)
				{
					if (DoorIndex < 0 || DoorIndex >= Count)
					{
						Error = TEXT("A final Wall - Door index is outside its material component.");
						return false;
					}
				}
				for (int32 I = 0; I < Count; ++I)
				{
					const FSoftObjectPath ActualPath(ActualMaterial);
					if (Source.Role == EEFCalystoStructuralRole::Wall)
					{
						const int64* RoomId = Source.NativeWallRoomIds.Find(I);
						const FSoftObjectPath* Expected = Source.NativeWallMaterials.Find(I);
						if (Source.NativeStyleOwnedWallIndices.Contains(I))
						{
							const FSoftObjectPath ExpectedStyle = StyleMaterial(EEFCalystoStructuralRole::Wall);
							if (!RoomId || !Expected || *RoomId != 0 || Expected->IsNull() || *Expected != ExpectedStyle || ActualPath != ExpectedStyle)
							{
								Error = TEXT("A Style-owned final wall differs from the frozen selected Style wall material.");
								return false;
							}
							++Inspected;
							continue;
						}
						const bool bDoorway = Source.NativeDoorWallIndices.Contains(I);
						if (bDoorway)
						{
							const FEFCalystoNativeRoom* DoorRoom = RoomId ? FindRoom(*RoomId) : nullptr;
							if (!RoomId || !Expected || !DoorRoom || *RoomId <= 0 || Expected->IsNull())
							{
								Error = TEXT("A final Wall - Door material record lacks its exact native carrier provenance.");
								return false;
							}
							if (ActualPath != *Expected)
							{
								Error = FString::Printf(TEXT("Generated Wall - Door material differs from its exact native carrier provenance: component=%s instance=%d room=%lld actual=%s expected=%s."),
									*Component->GetPathName(), I, *RoomId, *ActualPath.ToString(), *Expected->ToString());
								return false;
							}
							if (*Expected == DoorRoom->WallMaterial)
							{
								const FString Key = WitnessKey(*DoorRoom, EEFCalystoStructuralRole::Wall);
								if (const FSoftObjectPath* Required = RequiredMaterialWitnesses.Find(Key); Required && *Required == ActualPath)
								{
									MaterialWitnesses.Add(Key);
								}
							}
							++Inspected;
							continue;
						}
						const FEFCalystoNativeRoom* Room = RoomId ? FindRoom(*RoomId) : nullptr;
						if (!RoomId || !Expected || !Room || Expected->IsNull() || *Expected != Room->WallMaterial)
						{
							Error = TEXT("A final-wall material record disagrees with its frozen room authority.");
							return false;
						}
						if (ActualPath != *Expected)
						{
							Error = FString::Printf(TEXT("Generated Wall material differs from its exact native provenance: component=%s instance=%d room=%lld actual=%s expected=%s."),
								*Component->GetPathName(), I, *RoomId, *ActualPath.ToString(), *Expected->ToString());
							return false;
						}
						const FString Key = WitnessKey(*Room, EEFCalystoStructuralRole::Wall);
						if (const FSoftObjectPath* Required = RequiredMaterialWitnesses.Find(Key); Required && *Required == ActualPath)
						{
							MaterialWitnesses.Add(Key);
						}
						++Inspected;
						continue;
					}
					FMatrix WorldMatrix = Component->GetComponentTransform().ToMatrixWithScale();
					if (Instances)
					{
						if (!Instances->PerInstanceSMData.IsValidIndex(I)) { Error = TEXT("A structural material instance has unavailable native storage."); return false; }
						WorldMatrix = Instances->PerInstanceSMData[I].Transform * WorldMatrix;
					}
					if (WorldMatrix.ContainsNaN()) { Error = TEXT("A structural material instance has a non-finite transform."); return false; }
					FBox LocalFootprint(ForceInit);
					if (!ComputeDungeonLocalFootprint(*Component, WorldMatrix, DungeonTransform, LocalFootprint, Error)) return false;
					TArray<int32> IntersectingRooms;
					TArray<int32> ContainingRooms;
					for (int32 RoomIndex = 0; RoomIndex < Native.Rooms.Num(); ++RoomIndex)
					{
						const FBox& RoomBounds = Native.Rooms[RoomIndex].LocalBounds;
						if (!IntersectsXY(LocalFootprint, RoomBounds)) continue;
						IntersectingRooms.Add(RoomIndex);
						if (ContainsXY(RoomBounds, LocalFootprint)) ContainingRooms.Add(RoomIndex);
					}
					TSet<FSoftObjectPath> Expected;
					for (const int32 RoomIndex : IntersectingRooms) Expected.Add(RoomMaterial(Native.Rooms[RoomIndex], Source.Role));
					if (IntersectingRooms.IsEmpty() || ContainingRooms.IsEmpty()) Expected.Add(StyleMaterial(Source.Role));
					if (Expected.IsEmpty()) { Error = TEXT("A structural material surface has no eligible owning material."); return false; }
					if (!Expected.Contains(ActualPath))
					{
						TArray<FString> ExpectedPaths, RoomIds;
						for (const FSoftObjectPath& Path : Expected) ExpectedPaths.Add(Path.ToString());
						for (const int32 RoomIndex : IntersectingRooms)
							RoomIds.Add(LexToString(Native.Rooms[RoomIndex].RoomId));
						ExpectedPaths.Sort(); RoomIds.Sort();
						Error = FString::Printf(TEXT("Generated %s material differs from its complete room footprint: component=%s instance=%d actual=%s expected=%s rooms=%s local_min=%s local_max=%s."),
							RoleLabel(Source.Role), *Component->GetPathName(), I, *ActualPath.ToString(), *FString::Join(ExpectedPaths, TEXT(",")),
							*FString::Join(RoomIds, TEXT(",")), *LocalFootprint.Min.ToString(), *LocalFootprint.Max.ToString());
						return false;
					}
					if (ContainingRooms.Num() == 1 && IntersectingRooms.Num() == 1)
					{
						const FEFCalystoNativeRoom& Room = Native.Rooms[ContainingRooms[0]];
						const FString Key = WitnessKey(Room, Source.Role);
						if (const FSoftObjectPath* Required = RequiredMaterialWitnesses.Find(Key); Required && *Required == ActualPath)
						{
							MaterialWitnesses.Add(Key);
						}
					}
					++Inspected;
				}
			}
			if (Inspected == 0) Error = TEXT("No structural material assignments were available.");
			if (Inspected == 0) return false;
			if (MaterialWitnesses.Num() != RequiredMaterialWitnesses.Num())
			{
				TArray<FString> Missing;
				for (const auto& Pair : RequiredMaterialWitnesses) if (!MaterialWitnesses.Contains(Pair.Key))
				{
					Missing.Add(Pair.Key + TEXT("=") + Pair.Value.ToString());
				}
				Missing.Sort();
				Error = FString::Printf(TEXT("Generated surfaces have no material witness for selected Theme overrides: %s."), *FString::Join(Missing, TEXT(", ")));
				return false;
			}
			return true;
		}
		mutable FEFCalystoDirectorAttemptRequest Request;
		FEFCalystoDirectorAttemptObservation Observation;
		mutable TSharedPtr<FEFCalystoNativeAdapter> Adapter;
		mutable TUniquePtr<FEFCalystoStructuralNavigation> Navigation;
		mutable TUniquePtr<FEFCalystoArchitectureMeshBatch> ArchitectureBatch;
		mutable TUniquePtr<FEFCalystoContentMaterializer> ContentMaterializer;
		mutable TUniquePtr<FEFCalystoDecalMaterializer> DecalMaterializer;
		mutable FEFCalystoArchitectureManifest ArchitectureManifest;
		mutable TSharedPtr<const FEFCalystoReservedContentManifest> ContentManifest;
		mutable TSharedPtr<const FEFCalystoReservedDecalManifest> DecalManifest;
		mutable FEFCalystoContentPlanningReport ContentPlanReport;
		mutable FEFCalystoDecalPlanningReport DecalPlanReport;
		mutable TSharedPtr<IEFCalystoGameplaySnapshot> ContentSnapshot;
		mutable FGuid ContentSnapshotRequest;
		mutable FEFCalystoContentAttemptProviderRequest ContentProviderRequest;
		mutable FEFCalystoContentAttemptProviderResult ContentProviderResult;
		mutable IEFCalystoContentAttemptProvider* ContentProvider = nullptr;
		mutable FEFCalystoSurfaceCandidateResult SurfaceCandidates;
		mutable TArray<FEFCalystoArchitectureDecision> ArchitectureDecisions;
		mutable TWeakObjectPtr<AActor> DungeonActor;
		mutable FEFCalystoNativeResult Native;
		mutable FEFCalystoStructuralNavigationInput NavigationInput;
		mutable TArray<FEFCalystoStructuralSource> StructuralSources;
		mutable TArray<FEFCalystoStructuralMeshRule> MeshRules;
		mutable TArray<TWeakObjectPtr<AActor>> OwnedActors;
		mutable TArray<TWeakObjectPtr<UActorComponent>> OwnedComponents;
		bool bReleasing = false;
		bool bNativeCaptured = false;
		bool bNavigationInputCaptured = false;
		bool bArchitectureFrozen = false;
		bool bContentFrozen = false;
		bool bDecalFrozen = false;
		bool bGameplaySnapshotReconstructed = false;
		bool bGameplayProviderPrepared = false;
		bool bAcceptedContent = false;
		bool bAcceptedDecals = false;
		FGuid GameplayReconstructionRequest;
		double NativeStartedSeconds = -1;
		double NavigationStartedSeconds = -1;
		double NavigationReadySeconds = -1;
		double ArchitectureRealizationStartedSeconds = -1;
	};
}

bool UEFCalystoDirectorPCGSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{ return Super::ShouldCreateSubsystem(Outer) && UEFCalystoDirectorSettings::IsEnabled(); }

void UEFCalystoDirectorPCGSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); Collection.InitializeDependency<UEFCalystoDirectorSubsystem>();
	if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>())
	{
		Runtime = MakeShared<FDirectorNativeRuntime>();
		if (!Director->RegisterRuntime(Runtime.ToSharedRef())) Runtime.Reset();
	}
}

void UEFCalystoDirectorPCGSubsystem::Deinitialize()
{
	if (GetGameInstance()) if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>()) Director->UnregisterRuntime(Runtime.Get());
	Runtime.Reset(); Super::Deinitialize();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoWallExtensionMatchingTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.WallExtensionInstanceProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoWallExtensionMatchingTest::RunTest(const FString&)
{
	auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Provenance fixture mesh exists"), Mesh)) return false;
	auto* Component = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Component->SetStaticMesh(Mesh);
	const FTransform Trim(FQuat::Identity, FVector(0, 0, 300), FVector(1.01, 1, 0));
	Component->AddInstance(Trim); Component->AddInstance(Trim);
	TArray<FEFCalystoStructuralSource> Sources{{Component, EEFCalystoStructuralRole::Wall}};
	TArray<FEFCalystoNativeWallExtension> Extensions{{FSoftObjectPath(Mesh), Trim}, {FSoftObjectPath(Mesh), Trim}};
	const FSoftObjectPath WallMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TArray<FEFCalystoNativeWallSurface> FinalWalls{{FSoftObjectPath(Mesh), Trim, 71, WallMaterial, true, false, true},
		{FSoftObjectPath(Mesh), Trim, 72, WallMaterial, true}};
	FString Error;
	TestTrue(TEXT("Duplicate final native wall points bind one-to-one"), MatchNativeWallSurfaceInstances(FinalWalls, Sources, Error));
	TestEqual(TEXT("Final wall room identities survive duplicate transforms"), Sources[0].NativeWallRoomIds.Num(), 2);
	TestEqual(TEXT("Final wall material identities survive duplicate transforms"), Sources[0].NativeWallMaterials.Num(), 2);
	TestTrue(TEXT("Final Wall - Door origin survives exact instance binding"), Sources[0].NativeDoorWallIndices.Contains(0));
	FinalWalls[1].RoomId = 0;
	FinalWalls[1].bStyleOwned = true;
	TestTrue(TEXT("An explicit Style-owned wall binds without inventing a room"), MatchNativeWallSurfaceInstances(FinalWalls, Sources, Error));
	TestTrue(TEXT("The exact Style-owned wall index survives provenance binding"), Sources[0].NativeStyleOwnedWallIndices.Contains(1));
	FinalWalls[1].bStyleOwned = false;
	TestFalse(TEXT("Final wall ownership cannot be inferred when the room identity is missing"), MatchNativeWallSurfaceInstances(FinalWalls, Sources, Error));
	FinalWalls[1].RoomId = 72;
	TestTrue(TEXT("Restored final wall provenance binds again"), MatchNativeWallSurfaceInstances(FinalWalls, Sources, Error));
	TestTrue(TEXT("Duplicate native outputs match duplicate owned instances one-to-one"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	TestEqual(TEXT("Both exact indices are proved"), Sources[0].NativeZeroHeightWallExtensionIndices.Num(), 2);
	Extensions.Pop();
	TestFalse(TEXT("One output cannot excuse two zero-height instances"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	TestTrue(TEXT("Failed provenance publishes no partial permission"), Sources[0].NativeZeroHeightWallExtensionIndices.IsEmpty());
	Extensions.Add({FSoftObjectPath(Mesh), Trim}); Extensions.Add({FSoftObjectPath(Mesh), Trim});
	TestFalse(TEXT("Unrealized native output rejects the multiset"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	Extensions.Pop(); Extensions[1].WorldTransform.AddToTranslation(FVector(10, 0, 0));
	TestFalse(TEXT("A nearby different transform is not a substitution"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	Extensions[1] = {FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")), Trim};
	TestFalse(TEXT("A different mesh cannot supply extension provenance"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	Component->ClearInstances();
	const FTransform RotatedTrim(FRotator(0, 90, 0), FVector(0, 0, 300), FVector(1.01, 1, 0));
	Component->AddInstance(RotatedTrim);
	Extensions = {{FSoftObjectPath(Mesh), RotatedTrim}};
	FTransform Decomposed;
	TestTrue(TEXT("Native public instance getter succeeds for a singular matrix"), Component->GetInstanceTransform(0, Decomposed));
	TestFalse(TEXT("Singular decomposition loses the authored rotation"), Decomposed.GetRotation().Equals(RotatedTrim.GetRotation()));
	TestTrue(TEXT("Exact stored matrix preserves rotated upper-wall provenance"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	Extensions[0].WorldTransform.SetRotation(FQuat::Identity);
	TestFalse(TEXT("Same position and scale with a different actual orientation is rejected"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	Component->SetWorldTransform(FTransform(FRotator(0, 30, 0), FVector(100, 200, 0)));
	Extensions[0].WorldTransform = RotatedTrim * Component->GetComponentTransform();
	TestTrue(TEXT("Stored local matrix is compared in its owning component world space"), MatchNativeWallExtensionInstances(Extensions, Sources, Error));
	return true;
}
#endif
