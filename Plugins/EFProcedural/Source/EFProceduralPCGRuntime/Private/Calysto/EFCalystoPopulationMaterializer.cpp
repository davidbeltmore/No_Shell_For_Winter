#include "Calysto/EFCalystoPopulationMaterializer.h"

#include "Calysto/EFCalystoFloorDoor.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoPopulationAnchor.h"
#include "EFProceduralSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPopulation, Log, All);

namespace EFCalystoPopulationPrivate
{
	static constexpr int32 MaxEnemyCount = 25;
	static constexpr int32 MaxFoodCount = 8;
	static constexpr int32 MaxChestCount = 3;
	static constexpr int32 MaxLootCount = 4;
	static constexpr int32 MaxSpecialEventCount = 4;
	static constexpr int32 MaxSpawnedActorCount = 36;
	static constexpr int32 MaxCandidateCount = 1024;
	static constexpr float ProtectedPointRadius = 600.0f;
	static constexpr float CandidateDeduplicationRadius = 120.0f;

	struct FCandidate
	{
		FString StableId;
		FVector NavigationLocation = FVector::ZeroVector;
		float SourceYaw = 0.0f;
		bool bSynthetic = false;
	};

	struct FRawAnchorCandidate
	{
		FString StableId;
		FVector Location = FVector::ZeroVector;
		float Yaw = 0.0f;
	};

	struct FSpawnRequest
	{
		const FEFCalystoSpawnDirective* Directive = nullptr;
		int32 DirectiveIndex = INDEX_NONE;
		int32 Ordinal = 0;
		FString StableKey;
	};

	static FString HashCanonicalString(const FString& Canonical)
	{
		return UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Canonical);
	}

	static uint64 HashRank(const FString& Canonical)
	{
		const FString Hash = HashCanonicalString(Canonical);
		return Hash.Len() >= 16 ? FCString::Strtoui64(*Hash.Left(16), nullptr, 16) : MAX_uint64;
	}

	static uint64 PlacementDomain(const EEFCalystoSpawnCategory Category)
	{
		switch (Category)
		{
		case EEFCalystoSpawnCategory::Enemy: return 0xA41E5C6D7F812301ULL;
		case EEFCalystoSpawnCategory::Food: return 0xA41E5C6D7F812302ULL;
		case EEFCalystoSpawnCategory::Chest: return 0xA41E5C6D7F812303ULL;
		case EEFCalystoSpawnCategory::Loot: return 0xA41E5C6D7F812304ULL;
		case EEFCalystoSpawnCategory::SpecialEvent: return 0xA41E5C6D7F812305ULL;
		default: return 0xA41E5C6D7F8123FFULL;
		}
	}

	static uint64 DerivePlacementValue(
		const FEFCalystoResolvedFloorIntent& Intent,
		const FSpawnRequest& Request,
		const FString& CandidateStableId,
		const bool bYaw)
	{
		FEFCalystoDungeonGenerationContext Context;
		Context.RunSeed = Intent.RunSeed;
		Context.FloorNumber = Intent.FloorNumber;
		Context.GenerationSerial = Intent.GenerationSerial;
		Context.PCGSeed = Intent.PCGSeed;
		Context.PolicyHash = Intent.PolicyHash;
		const uint64 StableEntityId = HashRank(FString::Printf(
			TEXT("%s|%s"),
			*Request.StableKey,
			*CandidateStableId));
		const uint64 Domain = PlacementDomain(Request.Directive->Category)
			^ (bYaw ? 0x6A09E667F3BCC909ULL : 0ULL);
		return EFCalystoDungeonDeterminism::DeriveDomainValue(
			Context,
			Intent.GeneratorVersion,
			Intent.EcologyHash,
			Domain,
			StableEntityId,
			static_cast<uint32>(Request.Ordinal));
	}

	static FString MakeTransformStableId(const FVector& Location, const float Yaw, const TCHAR* Source)
	{
		constexpr double Quantization = 10.0;
		const int64 X = FMath::RoundToInt64(Location.X / Quantization);
		const int64 Y = FMath::RoundToInt64(Location.Y / Quantization);
		const int64 Z = FMath::RoundToInt64(Location.Z / Quantization);
		const int32 QuantizedYaw = FMath::RoundToInt(FRotator::ClampAxis(Yaw));
		return HashCanonicalString(FString::Printf(TEXT("%s|%lld|%lld|%lld|%d"), Source, X, Y, Z, QuantizedYaw));
	}

	static FString QuantizedTransformRecord(const FVector& Location, const float Yaw)
	{
		constexpr double Quantization = 10.0;
		return FString::Printf(
			TEXT("%lld|%lld|%lld|%d"),
			FMath::RoundToInt64(Location.X / Quantization),
			FMath::RoundToInt64(Location.Y / Quantization),
			FMath::RoundToInt64(Location.Z / Quantization),
			FMath::RoundToInt(FRotator::ClampAxis(Yaw)));
	}

	static int32 CategoryPriority(const EEFCalystoSpawnCategory Category)
	{
		switch (Category)
		{
		case EEFCalystoSpawnCategory::Enemy: return 0;
		case EEFCalystoSpawnCategory::Chest: return 1;
		case EEFCalystoSpawnCategory::SpecialEvent: return 2;
		case EEFCalystoSpawnCategory::Food: return 3;
		case EEFCalystoSpawnCategory::Loot: return 4;
		default: return 5;
		}
	}

	static const TCHAR* CategoryName(const EEFCalystoSpawnCategory Category)
	{
		switch (Category)
		{
		case EEFCalystoSpawnCategory::Enemy: return TEXT("Enemy");
		case EEFCalystoSpawnCategory::Food: return TEXT("Food");
		case EEFCalystoSpawnCategory::Chest: return TEXT("Chest");
		case EEFCalystoSpawnCategory::Loot: return TEXT("Loot");
		case EEFCalystoSpawnCategory::SpecialEvent: return TEXT("SpecialEvent");
		default: return TEXT("Unknown");
		}
	}

	static FName CategoryTag(const EEFCalystoSpawnCategory Category)
	{
		return FName(*FString::Printf(TEXT("EF.Calysto.%s"), CategoryName(Category)));
	}

	static bool IsProtectedLocation(const FVector& Location, const TArray<FVector>& ProtectedLocations)
	{
		for (const FVector& ProtectedLocation : ProtectedLocations)
		{
			if (FVector::DistSquared2D(Location, ProtectedLocation)
				< FMath::Square(ProtectedPointRadius))
			{
				return true;
			}
		}
		return false;
	}

	static bool HasNearbyCandidate(const FVector& Location, const TArray<FCandidate>& Candidates)
	{
		for (const FCandidate& Existing : Candidates)
		{
			if (FVector::DistSquared2D(Location, Existing.NavigationLocation)
				< FMath::Square(CandidateDeduplicationRadius))
			{
				return true;
			}
		}
		return false;
	}

	static bool TryAddCandidate(
		UWorld* World,
		UNavigationSystemV1* NavigationSystem,
		const FBox& DungeonBounds,
		const FVector& ReachabilityOrigin,
		const FVector& RawLocation,
		const float Yaw,
		const bool bSynthetic,
		const TArray<FVector>& ProtectedLocations,
		TArray<FCandidate>& Candidates)
	{
		if (!IsValid(World) || !NavigationSystem || !DungeonBounds.IsValid || Candidates.Num() >= MaxCandidateCount)
		{
			return false;
		}

		FNavLocation Projected;
		if (!NavigationSystem->ProjectPointToNavigation(
				RawLocation,
				Projected,
				FVector(250.0f, 250.0f, 900.0f)))
		{
			return false;
		}
		if (!DungeonBounds.IsInsideXY(Projected.Location))
		{
			return false;
		}
		UNavigationPath* ReachabilityPath = UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			ReachabilityOrigin,
			Projected.Location);
		if (!IsValid(ReachabilityPath)
			|| !ReachabilityPath->IsValid()
			|| ReachabilityPath->IsPartial()
			|| ReachabilityPath->PathPoints.Num() < 2)
		{
			return false;
		}

		if (IsProtectedLocation(Projected.Location, ProtectedLocations)
			|| HasNearbyCandidate(Projected.Location, Candidates))
		{
			return false;
		}

		FCandidate Candidate;
		Candidate.NavigationLocation = Projected.Location;
		Candidate.SourceYaw = FRotator::ClampAxis(Yaw);
		Candidate.bSynthetic = bSynthetic;
		Candidate.StableId = MakeTransformStableId(
			Candidate.NavigationLocation,
			Candidate.SourceYaw,
			bSynthetic ? TEXT("GRID") : TEXT("PCG"));
		if (Candidate.StableId.IsEmpty())
		{
			return false;
		}

		Candidates.Add(MoveTemp(Candidate));
		return true;
	}

	static void AddSyntheticCandidates(
		UWorld* World,
		UNavigationSystemV1* NavigationSystem,
		const FBox& DungeonBounds,
		const FVector& ReachabilityOrigin,
		const int32 DesiredCandidateCount,
		const TArray<FVector>& ProtectedLocations,
		TArray<FCandidate>& Candidates)
	{
		if (!IsValid(World) || !NavigationSystem || !DungeonBounds.IsValid || Candidates.Num() >= DesiredCandidateCount)
		{
			return;
		}

		const FVector Size = DungeonBounds.GetSize();
		if (Size.ContainsNaN() || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return;
		}

		const double Step = FMath::Clamp(FMath::Max(Size.X, Size.Y) / 48.0, 300.0, 750.0);
		const double StartX = FMath::GridSnap(DungeonBounds.Min.X, Step);
		const double StartY = FMath::GridSnap(DungeonBounds.Min.Y, Step);
		const double QueryZ = DungeonBounds.GetCenter().Z;
		for (double Y = StartY; Y <= DungeonBounds.Max.Y && Candidates.Num() < DesiredCandidateCount; Y += Step)
		{
			for (double X = StartX; X <= DungeonBounds.Max.X && Candidates.Num() < DesiredCandidateCount; X += Step)
			{
				TryAddCandidate(
					World,
					NavigationSystem,
					DungeonBounds,
					ReachabilityOrigin,
					FVector(X, Y, QueryZ),
					0.0f,
					true,
					ProtectedLocations,
					Candidates);
			}
		}
	}

	static void ResolveSpawnShape(
		const FEFCalystoSpawnDirective& Directive,
		UClass* ActorClass,
		float& OutRadius,
		float& OutHalfHeight,
		float& OutActorZOffset)
	{
		OutRadius = 30.0f;
		OutHalfHeight = 30.0f;
		OutActorZOffset = 2.0f;
		if (Directive.Category == EEFCalystoSpawnCategory::Enemy)
		{
			OutRadius = 42.0f;
			OutHalfHeight = 92.0f;
			if (const ACharacter* CharacterCDO = ActorClass ? ActorClass->GetDefaultObject<ACharacter>() : nullptr)
			{
				if (const UCapsuleComponent* Capsule = CharacterCDO->GetCapsuleComponent())
				{
					Capsule->GetUnscaledCapsuleSize(OutRadius, OutHalfHeight);
					OutRadius = FMath::Max(OutRadius, 34.0f);
					OutHalfHeight = FMath::Max(OutHalfHeight, 88.0f);
				}
			}
			OutActorZOffset = OutHalfHeight + 2.0f;
		}
		else if (Directive.Category == EEFCalystoSpawnCategory::Chest)
		{
			OutRadius = 65.0f;
			OutHalfHeight = 55.0f;
		}
	}

	static bool IsSpawnLocationFree(
		UWorld* World,
		const FVector& NavigationLocation,
		const float Radius,
		const float HalfHeight,
		const TArray<FVector>& OccupiedLocations)
	{
		const float MinimumSpacing = FMath::Max(Radius * 2.25f, CandidateDeduplicationRadius);
		for (const FVector& Occupied : OccupiedLocations)
		{
			if (FVector::DistSquared2D(NavigationLocation, Occupied) < FMath::Square(MinimumSpacing))
			{
				return false;
			}
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFCalystoPopulationOverlap), false);
		const FVector CollisionCenter = NavigationLocation + FVector(0.0f, 0.0f, HalfHeight + 3.0f);
		return !World->OverlapBlockingTestByChannel(
			CollisionCenter,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight),
			QueryParams);
	}

	static FString CanonicalDirectiveKey(const FEFCalystoSpawnDirective& Directive)
	{
		return FString::Printf(
			TEXT("%d|%s|%s"),
			static_cast<int32>(Directive.Category),
			*Directive.StableId.ToString(),
			*Directive.ActorClass.ToSoftObjectPath().ToString());
	}

	static bool ValidateIntent(
		const FEFCalystoResolvedFloorIntent& Intent,
		int32& OutDirectiveEnemyCount,
		int32& OutDirectiveFoodCount,
		int32& OutDirectiveChestCount,
		int32& OutDirectiveLootCount,
		int32& OutDirectiveSpecialEventCount,
		float& OutRealizedThreatCost,
		float& OutRealizedResourceCost,
		FString& OutError)
	{
		OutDirectiveEnemyCount = 0;
		OutDirectiveFoodCount = 0;
		OutDirectiveChestCount = 0;
		OutDirectiveLootCount = 0;
		OutDirectiveSpecialEventCount = 0;
		OutRealizedThreatCost = 0.0f;
		OutRealizedResourceCost = 0.0f;
		if (!Intent.bIsValid
			|| Intent.GeneratorVersion != 3
			|| Intent.RunSeed <= 0
			|| Intent.FloorNumber <= 0
			|| Intent.GenerationSerial <= 0
			|| Intent.PCGSeed <= 0
			|| Intent.PolicyHash.IsEmpty()
			|| Intent.EcologyHash.IsEmpty()
			|| Intent.IntentHash.IsEmpty())
		{
			OutError = TEXT("Resolved V3 floor intent identity or hashes are invalid.");
			return false;
		}

		TSet<FString> StableDirectiveKeys;
		for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
		{
			if (Directive.StableId.IsNone()
				|| Directive.Count < 0
				|| Directive.Count > MaxSpawnedActorCount
				|| !FMath::IsFinite(Directive.CostPerActor)
				|| Directive.CostPerActor < 0.0f
				|| Directive.RelativeWeight < 0)
			{
				OutError = TEXT("A V3 spawn directive has an invalid StableId, count, cost, or relative weight.");
				return false;
			}
			const FSoftObjectPath ClassPath = Directive.ActorClass.ToSoftObjectPath();
			if (!ClassPath.IsValid())
			{
				OutError = FString::Printf(TEXT("Directive %s has an invalid actor class path."), *Directive.StableId.ToString());
				return false;
			}
			const FString StableKey = CanonicalDirectiveKey(Directive);
			if (StableDirectiveKeys.Contains(StableKey))
			{
				OutError = FString::Printf(TEXT("Duplicate V3 spawn directive key %s."), *StableKey);
				return false;
			}
			StableDirectiveKeys.Add(StableKey);

			switch (Directive.Category)
			{
			case EEFCalystoSpawnCategory::Enemy:
				OutDirectiveEnemyCount += Directive.Count;
				OutRealizedThreatCost += static_cast<float>(Directive.Count) * Directive.CostPerActor;
				break;
			case EEFCalystoSpawnCategory::Food: OutDirectiveFoodCount += Directive.Count; break;
			case EEFCalystoSpawnCategory::Chest: OutDirectiveChestCount += Directive.Count; break;
			case EEFCalystoSpawnCategory::Loot: OutDirectiveLootCount += Directive.Count; break;
			case EEFCalystoSpawnCategory::SpecialEvent: OutDirectiveSpecialEventCount += Directive.Count; break;
			default:
				OutError = FString::Printf(TEXT("Directive %s has an unsupported category."), *Directive.StableId.ToString());
				return false;
			}
		}

		OutRealizedResourceCost = 0.0f;
		for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
		{
			if (Directive.Category != EEFCalystoSpawnCategory::Enemy)
			{
				OutRealizedResourceCost += static_cast<float>(Directive.Count) * Directive.CostPerActor;
			}
		}
		const int32 TotalCount = OutDirectiveEnemyCount
			+ OutDirectiveFoodCount
			+ OutDirectiveChestCount
			+ OutDirectiveLootCount
			+ OutDirectiveSpecialEventCount;
		if (!FMath::IsFinite(Intent.ThreatBudget)
			|| Intent.ThreatBudget < 0.0f
			|| !FMath::IsFinite(Intent.ResourceBudget)
			|| Intent.ResourceBudget < 0.0f
			|| !FMath::IsFinite(OutRealizedThreatCost)
			|| !FMath::IsFinite(OutRealizedResourceCost))
		{
			OutError = TEXT("V3 threat/resource budget or realized cost is non-finite or negative.");
			return false;
		}
		const float ThreatTolerance = FMath::Max(0.001f, Intent.ThreatBudget * 0.001f);
		if (OutRealizedThreatCost > Intent.ThreatBudget + ThreatTolerance)
		{
			OutError = FString::Printf(
				TEXT("V3 enemy directives cost %.3f but the frozen threat budget is %.3f."),
				OutRealizedThreatCost,
				Intent.ThreatBudget);
			return false;
		}
		const float ResourceTolerance = FMath::Max(0.001f, Intent.ResourceBudget * 0.001f);
		if (OutRealizedResourceCost > Intent.ResourceBudget + ResourceTolerance)
		{
			OutError = FString::Printf(
				TEXT("V3 resource directives cost %.3f but the frozen resource budget is %.3f."),
				OutRealizedResourceCost,
				Intent.ResourceBudget);
			return false;
		}
		if (OutDirectiveEnemyCount != Intent.EnemyCount
			|| OutDirectiveFoodCount != Intent.FoodCount
			|| OutDirectiveChestCount != Intent.ChestCount
			|| OutDirectiveLootCount != Intent.LootCount
			|| OutDirectiveSpecialEventCount != Intent.SpecialEventCount)
		{
			OutError = TEXT("V3 spawn-directive totals do not match the frozen floor intent totals.");
			return false;
		}
		if (OutDirectiveEnemyCount > MaxEnemyCount
			|| OutDirectiveFoodCount > MaxFoodCount
			|| OutDirectiveChestCount > MaxChestCount
			|| OutDirectiveLootCount > MaxLootCount
			|| OutDirectiveSpecialEventCount > MaxSpecialEventCount
			|| TotalCount > MaxSpawnedActorCount)
		{
			OutError = FString::Printf(
				TEXT("V3 population exceeds hard caps (enemy=%d food=%d chest=%d loot=%d special=%d total=%d)."),
				OutDirectiveEnemyCount,
				OutDirectiveFoodCount,
				OutDirectiveChestCount,
				OutDirectiveLootCount,
				OutDirectiveSpecialEventCount,
				TotalCount);
			return false;
		}
		return true;
	}
}

FEFCalystoPopulationMaterializationResult FEFCalystoPopulationMaterializer::Materialize(
	UWorld* World,
	AActor* DungeonActor,
	const FBox& DungeonBounds,
	const FEFCalystoResolvedFloorIntent& Intent)
{
	using namespace EFCalystoPopulationPrivate;

	FEFCalystoPopulationMaterializationResult Result;
	auto Fail = [&Result](FString&& Reason)
	{
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};
	if (!IsValid(World) || !World->IsGameWorld() || !IsValid(DungeonActor))
	{
		return Fail(TEXT("Cannot materialize V3 population without a valid game world and dungeon actor."));
	}

	int32 ExpectedEnemies = 0;
	int32 ExpectedFood = 0;
	int32 ExpectedChests = 0;
	int32 ExpectedLoot = 0;
	int32 ExpectedSpecialEvents = 0;
	float RealizedThreatCost = 0.0f;
	float RealizedResourceCost = 0.0f;
	FString ValidationError;
	if (!ValidateIntent(
		Intent,
		ExpectedEnemies,
		ExpectedFood,
		ExpectedChests,
		ExpectedLoot,
		ExpectedSpecialEvents,
		RealizedThreatCost,
		RealizedResourceCost,
		ValidationError))
	{
		return Fail(MoveTemp(ValidationError));
	}

	TArray<TWeakObjectPtr<AEFCalystoPopulationAnchor>> AnchorActors;
	for (TActorIterator<AEFCalystoPopulationAnchor> AnchorIt(World); AnchorIt; ++AnchorIt)
	{
		if (IsValid(*AnchorIt))
		{
			AnchorActors.Add(*AnchorIt);
		}
	}
	ON_SCOPE_EXIT
	{
		for (const TWeakObjectPtr<AEFCalystoPopulationAnchor>& AnchorPtr : AnchorActors)
		{
			if (AnchorPtr.IsValid())
			{
				AnchorPtr->Destroy();
			}
		}
	};

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return Fail(TEXT("Runtime navigation is unavailable while materializing V3 population."));
	}
	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	UClass* StartPointClass = Settings ? Settings->GetStartPointActorClassResolved().Get() : nullptr;
	TArray<AActor*> StartPoints;
	if (IsValid(StartPointClass))
	{
		UGameplayStatics::GetAllActorsOfClass(World, StartPointClass, StartPoints);
		StartPoints.RemoveAllSwap([](const AActor* StartPoint) { return !IsValid(StartPoint); });
	}
	if (!IsValid(StartPointClass) || StartPoints.Num() != 1)
	{
		return Fail(FString::Printf(
			TEXT("Population materialization requires exactly one configured start point; found %d (class=%s)."),
			StartPoints.Num(),
			*GetPathNameSafe(StartPointClass)));
	}
	FNavLocation ProjectedStart;
	if (!NavigationSystem->ProjectPointToNavigation(
			StartPoints[0]->GetActorLocation(),
			ProjectedStart,
			FVector(600.0f, 600.0f, 1400.0f)))
	{
		return Fail(TEXT("The configured start point could not be projected to runtime navigation."));
	}

	TArray<FVector> ProtectedLocations;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt))
		{
			ProtectedLocations.Add(DoorIt->GetActorLocation());
		}
	}
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor))
		{
			continue;
		}
		const FString ActorName = Actor->GetName();
		const FString ClassName = Actor->GetClass()->GetName();
		if (ActorName.Contains(TEXT("StartPoint"), ESearchCase::IgnoreCase)
			|| ClassName.Contains(TEXT("StartPoint"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("Door"), ESearchCase::IgnoreCase)
			|| ClassName.Contains(TEXT("Door"), ESearchCase::IgnoreCase))
		{
			ProtectedLocations.AddUnique(Actor->GetActorLocation());
		}
	}
	ProtectedLocations.Sort([](const FVector& Left, const FVector& Right)
	{
		const FString LeftKey = QuantizedTransformRecord(Left, 0.0f);
		const FString RightKey = QuantizedTransformRecord(Right, 0.0f);
		return LeftKey < RightKey;
	});

	TArray<FCandidate> Candidates;
	Candidates.Reserve(FMath::Min(MaxCandidateCount, FMath::Max(AnchorActors.Num(), 64)));
	TArray<FRawAnchorCandidate> RawCandidates;
	RawCandidates.Reserve(AnchorActors.Num());
	for (const TWeakObjectPtr<AEFCalystoPopulationAnchor>& AnchorPtr : AnchorActors)
	{
		if (const AEFCalystoPopulationAnchor* Anchor = AnchorPtr.Get())
		{
			FRawAnchorCandidate& RawCandidate = RawCandidates.AddDefaulted_GetRef();
			const FVector AnchorLocation = Anchor->GetActorLocation();
			RawCandidate.Location = FVector(
				FMath::GridSnap(AnchorLocation.X, 10.0),
				FMath::GridSnap(AnchorLocation.Y, 10.0),
				FMath::GridSnap(AnchorLocation.Z, 10.0));
			RawCandidate.Yaw = static_cast<float>(FMath::RoundToInt(FRotator::ClampAxis(Anchor->GetActorRotation().Yaw)));
			RawCandidate.StableId = MakeTransformStableId(RawCandidate.Location, RawCandidate.Yaw, TEXT("RAW"));
		}
	}
	RawCandidates.Sort([](const FRawAnchorCandidate& Left, const FRawAnchorCandidate& Right)
	{
		return Left.StableId == Right.StableId
			? QuantizedTransformRecord(Left.Location, Left.Yaw) < QuantizedTransformRecord(Right.Location, Right.Yaw)
			: Left.StableId < Right.StableId;
	});
	for (const FRawAnchorCandidate& RawCandidate : RawCandidates)
	{
		TryAddCandidate(
			World,
			NavigationSystem,
			DungeonBounds,
			ProjectedStart.Location,
			RawCandidate.Location,
			RawCandidate.Yaw,
			false,
			ProtectedLocations,
			Candidates);
	}

	const int32 TotalRequested = ExpectedEnemies + ExpectedFood + ExpectedChests + ExpectedLoot + ExpectedSpecialEvents;
	const int32 DesiredCandidates = FMath::Min(MaxCandidateCount, FMath::Max(TotalRequested * 4, TotalRequested));
	AddSyntheticCandidates(
		World,
		NavigationSystem,
		DungeonBounds,
		ProjectedStart.Location,
		DesiredCandidates,
		ProtectedLocations,
		Candidates);
	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
	{
		return Left.StableId < Right.StableId;
	});

	TArray<FString> AnchorTopologyRecords;
	AnchorTopologyRecords.Reserve(RawCandidates.Num() + Candidates.Num() + 2);
	for (const FRawAnchorCandidate& RawCandidate : RawCandidates)
	{
		AnchorTopologyRecords.Add(FString::Printf(TEXT("RAW|%s"), *RawCandidate.StableId));
	}
	for (const FCandidate& Candidate : Candidates)
	{
		AnchorTopologyRecords.Add(FString::Printf(
			TEXT("CANDIDATE|%s|%d"),
			*Candidate.StableId,
			Candidate.bSynthetic ? 1 : 0));
	}
	static const FName StartPointRepairTag(TEXT("EF.Calysto.StartPointRepair.V3"));
	static const FString StartPointRepairHashTagPrefix(TEXT("EF.Calysto.StartPointRepairHash."));
	const FString StartTransform = QuantizedTransformRecord(
		StartPoints[0]->GetActorLocation(),
		StartPoints[0]->GetActorRotation().Yaw);
	if (!StartPoints[0]->ActorHasTag(StartPointRepairTag))
	{
		AnchorTopologyRecords.Add(FString::Printf(
			TEXT("TOPOLOGY_START|NONE|%s"),
			*StartTransform));
	}
	else
	{
		TArray<FString> StartRepairHashes;
		for (const FName& Tag : StartPoints[0]->Tags)
		{
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(StartPointRepairHashTagPrefix))
			{
				StartRepairHashes.Add(TagString.RightChop(StartPointRepairHashTagPrefix.Len()));
			}
		}
		StartRepairHashes.Sort();
		if (StartRepairHashes.Num() != 1 || StartRepairHashes[0].IsEmpty())
		{
			return Fail(FString::Printf(
				TEXT("Topology-repaired start point %s must expose exactly one deterministic repair hash; found %d."),
				*StartPoints[0]->GetPathName(),
				StartRepairHashes.Num()));
		}
		AnchorTopologyRecords.Add(FString::Printf(
			TEXT("TOPOLOGY_START|REPAIRED|%s|%s"),
			*StartRepairHashes[0],
			*StartTransform));
	}
	static const FName TopologyRepairTag(TEXT("EF.Calysto.TopologyRepair.V3"));
	static const FString TopologyRepairHashTagPrefix(TEXT("EF.Calysto.TopologyRepairHash."));
	int32 CanonicalDoorCount = 0;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (!IsValid(*DoorIt))
		{
			continue;
		}
		++CanonicalDoorCount;
		const FString DoorTransform = QuantizedTransformRecord(
			DoorIt->GetActorLocation(),
			DoorIt->GetActorRotation().Yaw);
		if (!DoorIt->ActorHasTag(TopologyRepairTag))
		{
			AnchorTopologyRecords.Add(FString::Printf(
				TEXT("TOPOLOGY_DOOR|NONE|%s"),
				*DoorTransform));
			continue;
		}
		TArray<FString> RepairHashes;
		for (const FName& Tag : DoorIt->Tags)
		{
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(TopologyRepairHashTagPrefix))
			{
				RepairHashes.Add(TagString.RightChop(TopologyRepairHashTagPrefix.Len()));
			}
		}
		RepairHashes.Sort();
		if (RepairHashes.Num() != 1 || RepairHashes[0].IsEmpty())
		{
			return Fail(FString::Printf(
				TEXT("Topology-repaired door %s must expose exactly one deterministic repair hash; found %d."),
				*DoorIt->GetPathName(),
				RepairHashes.Num()));
		}
		AnchorTopologyRecords.Add(FString::Printf(
			TEXT("TOPOLOGY_DOOR|REPAIRED|%s|%s"),
			*RepairHashes[0],
			*DoorTransform));
	}
	if (CanonicalDoorCount != 1)
	{
		return Fail(FString::Printf(
			TEXT("Manifest topology requires exactly one AEFCalystoFloorDoor; found %d."),
			CanonicalDoorCount));
	}
	AnchorTopologyRecords.Sort();
	const FString AnchorTopologyHash = HashCanonicalString(FString::Join(AnchorTopologyRecords, TEXT("\n")));
	if (AnchorTopologyHash.IsEmpty())
	{
		return Fail(TEXT("Failed to hash the deterministic V3 anchor topology."));
	}

	TArray<FSpawnRequest> Requests;
	Requests.Reserve(TotalRequested);
	for (int32 DirectiveIndex = 0; DirectiveIndex < Intent.SpawnDirectives.Num(); ++DirectiveIndex)
	{
		const FEFCalystoSpawnDirective& Directive = Intent.SpawnDirectives[DirectiveIndex];
		const FString DirectiveKey = CanonicalDirectiveKey(Directive);
		for (int32 Ordinal = 0; Ordinal < Directive.Count; ++Ordinal)
		{
			FSpawnRequest& Request = Requests.AddDefaulted_GetRef();
			Request.Directive = &Directive;
			Request.DirectiveIndex = DirectiveIndex;
			Request.Ordinal = Ordinal;
			Request.StableKey = FString::Printf(TEXT("%s|%06d"), *DirectiveKey, Ordinal);
		}
	}
	Requests.Sort([](const FSpawnRequest& Left, const FSpawnRequest& Right)
	{
		const int32 LeftPriority = CategoryPriority(Left.Directive->Category);
		const int32 RightPriority = CategoryPriority(Right.Directive->Category);
		return LeftPriority == RightPriority
			? Left.StableKey < Right.StableKey
			: LeftPriority < RightPriority;
	});

	TArray<TWeakObjectPtr<AActor>> SpawnedActors;
	TArray<FVector> OccupiedLocations;
	TSet<int32> UsedCandidateIndices;
	TArray<int32> ActualDirectiveCounts;
	ActualDirectiveCounts.Init(0, Intent.SpawnDirectives.Num());
	TArray<FString> PopulationRecords;
	TArray<FString> ResourceRecords;
	auto DestroySpawnedActors = [&SpawnedActors]()
	{
		for (const TWeakObjectPtr<AActor>& SpawnedPtr : SpawnedActors)
		{
			if (SpawnedPtr.IsValid())
			{
				SpawnedPtr->Destroy();
			}
		}
	};

	for (const FSpawnRequest& Request : Requests)
	{
		const FEFCalystoSpawnDirective& Directive = *Request.Directive;
		UClass* ActorClass = Directive.ActorClass.Get();
		if (!IsValid(ActorClass)
			|| !ActorClass->IsChildOf(AActor::StaticClass())
			|| ActorClass->HasAnyClassFlags(CLASS_Abstract)
			|| (Directive.Category == EEFCalystoSpawnCategory::Enemy && !ActorClass->IsChildOf(APawn::StaticClass())))
		{
			DestroySpawnedActors();
			return Fail(FString::Printf(
				TEXT("Preloaded class %s is invalid for V3 %s directive %s."),
				*Directive.ActorClass.ToSoftObjectPath().ToString(),
				CategoryName(Directive.Category),
				*Directive.StableId.ToString()));
		}

		float CollisionRadius = 0.0f;
		float CollisionHalfHeight = 0.0f;
		float ActorZOffset = 0.0f;
		ResolveSpawnShape(Directive, ActorClass, CollisionRadius, CollisionHalfHeight, ActorZOffset);
		TArray<TPair<uint64, int32>> RankedCandidates;
		RankedCandidates.Reserve(Candidates.Num());
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			if (!UsedCandidateIndices.Contains(CandidateIndex))
			{
				RankedCandidates.Emplace(
					DerivePlacementValue(Intent, Request, Candidates[CandidateIndex].StableId, false),
					CandidateIndex);
			}
		}
		RankedCandidates.Sort([](const TPair<uint64, int32>& Left, const TPair<uint64, int32>& Right)
		{
			return Left.Key == Right.Key ? Left.Value < Right.Value : Left.Key < Right.Key;
		});

		AActor* SpawnedActor = nullptr;
		int32 SelectedCandidateIndex = INDEX_NONE;
		for (const TPair<uint64, int32>& RankedCandidate : RankedCandidates)
		{
			const int32 CandidateIndex = RankedCandidate.Value;
			const FCandidate& Candidate = Candidates[CandidateIndex];
			if (!IsSpawnLocationFree(
					World,
					Candidate.NavigationLocation,
					CollisionRadius,
					CollisionHalfHeight,
					OccupiedLocations))
			{
				continue;
			}

			const uint64 YawValue = DerivePlacementValue(Intent, Request, Candidate.StableId, true);
			const float Yaw = static_cast<float>(YawValue % 36000ULL) / 100.0f;
			const FTransform SpawnTransform(
				FRotator(0.0f, Yaw, 0.0f),
				Candidate.NavigationLocation + FVector(0.0f, 0.0f, ActorZOffset));
			SpawnedActor = World->SpawnActorDeferred<AActor>(
				ActorClass,
				SpawnTransform,
				DungeonActor,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!IsValid(SpawnedActor))
			{
				continue;
			}

			SpawnedActor->Tags.AddUnique(UEFProceduralSettings::Get()->GeneratedActorTag);
			SpawnedActor->Tags.AddUnique(FName(TEXT("EF.Calysto.Population.V3")));
			SpawnedActor->Tags.AddUnique(CategoryTag(Directive.Category));
			SpawnedActor->FinishSpawning(SpawnTransform);
			if (!IsValid(SpawnedActor) || SpawnedActor->IsActorBeingDestroyed())
			{
				SpawnedActor = nullptr;
				continue;
			}
			SelectedCandidateIndex = CandidateIndex;
			break;
		}

		if (!IsValid(SpawnedActor) || SelectedCandidateIndex == INDEX_NONE)
		{
			DestroySpawnedActors();
			return Fail(FString::Printf(
				TEXT("No safe deterministic candidate remained for %s directive %s ordinal %d (candidates=%d)."),
				CategoryName(Directive.Category),
				*Directive.StableId.ToString(),
				Request.Ordinal,
				Candidates.Num()));
		}

		UsedCandidateIndices.Add(SelectedCandidateIndex);
		OccupiedLocations.Add(Candidates[SelectedCandidateIndex].NavigationLocation);
		SpawnedActors.Add(SpawnedActor);
		++ActualDirectiveCounts[Request.DirectiveIndex];

		const FVector FinalLocation = SpawnedActor->GetActorLocation();
		const FString Record = FString::Printf(
			TEXT("%s|%s|%s|%s|%d|%s"),
			CategoryName(Directive.Category),
			*Directive.StableId.ToString(),
			*Directive.ActorClass.ToSoftObjectPath().ToString(),
			*Candidates[SelectedCandidateIndex].StableId,
			Request.Ordinal,
			*QuantizedTransformRecord(FinalLocation, SpawnedActor->GetActorRotation().Yaw));
		if (Directive.Category == EEFCalystoSpawnCategory::Enemy)
		{
			PopulationRecords.Add(Record);
		}
		else
		{
			ResourceRecords.Add(Record);
		}
	}

	int32 LiveSpawnedActorCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : SpawnedActors)
	{
		LiveSpawnedActorCount += ActorPtr.IsValid() && !ActorPtr->IsActorBeingDestroyed() ? 1 : 0;
	}
	if (LiveSpawnedActorCount != TotalRequested)
	{
		DestroySpawnedActors();
		return Fail(FString::Printf(
			TEXT("V3 materialization realized %d live actors but the frozen intent requires %d."),
			LiveSpawnedActorCount,
			TotalRequested));
	}
	int32 ActualEnemies = 0;
	int32 ActualFood = 0;
	int32 ActualChests = 0;
	int32 ActualLoot = 0;
	int32 ActualSpecialEvents = 0;
	for (int32 DirectiveIndex = 0; DirectiveIndex < Intent.SpawnDirectives.Num(); ++DirectiveIndex)
	{
		const int32 ActualCount = ActualDirectiveCounts[DirectiveIndex];
		switch (Intent.SpawnDirectives[DirectiveIndex].Category)
		{
		case EEFCalystoSpawnCategory::Enemy: ActualEnemies += ActualCount; break;
		case EEFCalystoSpawnCategory::Food: ActualFood += ActualCount; break;
		case EEFCalystoSpawnCategory::Chest: ActualChests += ActualCount; break;
		case EEFCalystoSpawnCategory::Loot: ActualLoot += ActualCount; break;
		case EEFCalystoSpawnCategory::SpecialEvent: ActualSpecialEvents += ActualCount; break;
		default: break;
		}
	}
	if (ActualEnemies != ExpectedEnemies
		|| ActualFood != ExpectedFood
		|| ActualChests != ExpectedChests
		|| ActualLoot != ExpectedLoot
		|| ActualSpecialEvents != ExpectedSpecialEvents)
	{
		DestroySpawnedActors();
		return Fail(FString::Printf(
			TEXT("V3 realized category counts differ from the frozen intent (enemy %d/%d, food %d/%d, chest %d/%d, loot %d/%d, special %d/%d)."),
			ActualEnemies,
			ExpectedEnemies,
			ActualFood,
			ExpectedFood,
			ActualChests,
			ExpectedChests,
			ActualLoot,
			ExpectedLoot,
			ActualSpecialEvents,
			ExpectedSpecialEvents));
	}

	PopulationRecords.Sort();
	ResourceRecords.Sort();
	const FString PopulationHash = HashCanonicalString(FString::Join(PopulationRecords, TEXT("\n")));
	const FString ResourceHash = HashCanonicalString(FString::Join(ResourceRecords, TEXT("\n")));
	if (PopulationHash.IsEmpty() || ResourceHash.IsEmpty())
	{
		DestroySpawnedActors();
		return Fail(TEXT("Failed to hash the realized V3 population or resources."));
	}

	FEFCalystoRealizedFloorManifest Manifest;
	Manifest.RunSeed = Intent.RunSeed;
	Manifest.FloorNumber = Intent.FloorNumber;
	Manifest.GenerationSerial = Intent.GenerationSerial;
	Manifest.PCGSeed = Intent.PCGSeed;
	Manifest.IntentHash = Intent.IntentHash;
	Manifest.AnchorTopologyHash = AnchorTopologyHash;
	Manifest.PopulationHash = PopulationHash;
	Manifest.ResourceHash = ResourceHash;
	Manifest.CandidateAnchorCount = Candidates.Num();
	Manifest.EnemyCount = ActualEnemies;
	Manifest.FoodCount = ActualFood;
	Manifest.ChestCount = ActualChests;
	Manifest.LootCount = ActualLoot;
	Manifest.SpecialEventCount = ActualSpecialEvents;
	Manifest.RealizedThreatCost = RealizedThreatCost;
	Manifest.RealizedResourceCost = RealizedResourceCost;
	Manifest.SpawnedActorCount = LiveSpawnedActorCount;
	for (int32 DirectiveIndex = 0; DirectiveIndex < Intent.SpawnDirectives.Num(); ++DirectiveIndex)
	{
		if (ActualDirectiveCounts[DirectiveIndex] > 0)
		{
			FEFCalystoSpawnDirective RealizedDirective = Intent.SpawnDirectives[DirectiveIndex];
			RealizedDirective.Count = ActualDirectiveCounts[DirectiveIndex];
			Manifest.SpawnDirectives.Add(MoveTemp(RealizedDirective));
		}
	}
	Manifest.SpawnDirectives.Sort([](const FEFCalystoSpawnDirective& Left, const FEFCalystoSpawnDirective& Right)
	{
		return CanonicalDirectiveKey(Left) < CanonicalDirectiveKey(Right);
	});

	Manifest.ManifestHash = UEFCalystoDungeonSubsystem::ComputeManifestHash(Manifest);
	Manifest.bIsValid = !Manifest.ManifestHash.IsEmpty();
	if (!Manifest.bIsValid)
	{
		DestroySpawnedActors();
		return Fail(TEXT("Failed to hash the realized V3 floor manifest."));
	}

	Result.bSucceeded = true;
	Result.Manifest = MoveTemp(Manifest);
	UE_LOG(
		LogEFCalystoPopulation,
		Log,
		TEXT("PopulationRealized floor=%lld serial=%lld anchors=%d enemies=%d threatCost=%.3f/%.3f food=%d chests=%d loot=%d special=%d resourceCost=%.3f/%.3f actors=%d anchorHash=%s populationHash=%s resourceHash=%s manifestHash=%s."),
		Intent.FloorNumber,
		Intent.GenerationSerial,
		Result.Manifest.CandidateAnchorCount,
		Result.Manifest.EnemyCount,
		RealizedThreatCost,
		Intent.ThreatBudget,
		Result.Manifest.FoodCount,
		Result.Manifest.ChestCount,
		Result.Manifest.LootCount,
		Result.Manifest.SpecialEventCount,
		Result.Manifest.RealizedResourceCost,
		Intent.ResourceBudget,
		Result.Manifest.SpawnedActorCount,
		*Result.Manifest.AnchorTopologyHash,
		*Result.Manifest.PopulationHash,
		*Result.Manifest.ResourceHash,
		*Result.Manifest.ManifestHash);
	return Result;
}
