#include "Calysto/EFCalystoPopulationMaterializerV4.h"

#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDungeonDirectorMathV4.h"
#include "Calysto/EFCalystoFloorDoor.h"
#include "Calysto/EFCalystoPopulationAnchor.h"
#include "Calysto/EFCalystoPopulationBridgeV4.h"
#include "EFProceduralSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPopulationV4, Log, All);

namespace EFCalystoPopulationV4Private
{
	static constexpr int32 MaxEnemyCount = 25;
	static constexpr int32 MaxNPCCount = 4;
	static constexpr int32 MaxFoodCount = 30;
	static constexpr int32 MaxChestCount = 10;
	static constexpr int32 MaxLooseLootCount = 4;
	static constexpr int32 MaxClothingCount = 10;
	static constexpr int32 MaxSpecialEventCount = 6;
	static constexpr int32 MaxSpawnedActorCount = 89;
	static constexpr int32 MaxChestContentAttempts = 3;
	static constexpr int32 MaxCandidateCount = 1024;
	static constexpr int32 MaxSyntheticGridAttempts = 16384;
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
		const FEFCalystoSpawnInstanceDirectiveV4* Directive = nullptr;
		FString StableKey;
	};

	struct FRealizedActor
	{
		TWeakObjectPtr<AActor> Actor;
		const FEFCalystoSpawnInstanceDirectiveV4* Directive = nullptr;
		IEFCalystoPopulationBridgeV4* Bridge = nullptr;
	};

	struct FCategoryCounts
	{
		int32 Enemies = 0;
		int32 NPCs = 0;
		int32 Food = 0;
		int32 Chests = 0;
		int32 LooseLoot = 0;
		int32 Clothing = 0;
		int32 SpecialEvents = 0;

		int32 Total() const
		{
			return Enemies + NPCs + Food + Chests + LooseLoot + Clothing + SpecialEvents;
		}

		int32 Get(const EEFCalystoContentCategoryV4 Category) const
		{
			switch (Category)
			{
			case EEFCalystoContentCategoryV4::Enemy: return Enemies;
			case EEFCalystoContentCategoryV4::NPC: return NPCs;
			case EEFCalystoContentCategoryV4::Food: return Food;
			case EEFCalystoContentCategoryV4::Chest: return Chests;
			case EEFCalystoContentCategoryV4::LooseLoot: return LooseLoot;
			case EEFCalystoContentCategoryV4::Clothing: return Clothing;
			case EEFCalystoContentCategoryV4::SpecialEvent: return SpecialEvents;
			default: return 0;
			}
		}

		bool Increment(const EEFCalystoContentCategoryV4 Category)
		{
			switch (Category)
			{
			case EEFCalystoContentCategoryV4::Enemy: ++Enemies; return true;
			case EEFCalystoContentCategoryV4::NPC: ++NPCs; return true;
			case EEFCalystoContentCategoryV4::Food: ++Food; return true;
			case EEFCalystoContentCategoryV4::Chest: ++Chests; return true;
			case EEFCalystoContentCategoryV4::LooseLoot: ++LooseLoot; return true;
			case EEFCalystoContentCategoryV4::Clothing: ++Clothing; return true;
			case EEFCalystoContentCategoryV4::SpecialEvent: ++SpecialEvents; return true;
			default: return false;
			}
		}
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

	static FString FloatBits(const float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return FString::Printf(TEXT("%08X"), Bits);
	}

	static const TCHAR* CategoryName(const EEFCalystoContentCategoryV4 Category)
	{
		switch (Category)
		{
		case EEFCalystoContentCategoryV4::Enemy: return TEXT("Enemy");
		case EEFCalystoContentCategoryV4::NPC: return TEXT("NPC");
		case EEFCalystoContentCategoryV4::Food: return TEXT("Food");
		case EEFCalystoContentCategoryV4::Chest: return TEXT("Chest");
		case EEFCalystoContentCategoryV4::LooseLoot: return TEXT("LooseLoot");
		case EEFCalystoContentCategoryV4::Clothing: return TEXT("Clothing");
		case EEFCalystoContentCategoryV4::SpecialEvent: return TEXT("SpecialEvent");
		case EEFCalystoContentCategoryV4::Decoration: return TEXT("Decoration");
		case EEFCalystoContentCategoryV4::Lighting: return TEXT("Lighting");
		default: return TEXT("Unknown");
		}
	}

	static const TCHAR* TierName(const EEFCalystoRarityTierV4 Tier)
	{
		switch (Tier)
		{
		case EEFCalystoRarityTierV4::Common: return TEXT("Common");
		case EEFCalystoRarityTierV4::Uncommon: return TEXT("Uncommon");
		case EEFCalystoRarityTierV4::Rare: return TEXT("Rare");
		case EEFCalystoRarityTierV4::Epic: return TEXT("Epic");
		case EEFCalystoRarityTierV4::Winter: return TEXT("Winter");
		default: return TEXT("Unknown");
		}
	}

	static const TCHAR* GenderName(const EEFCalystoGenderV4 Gender)
	{
		switch (Gender)
		{
		case EEFCalystoGenderV4::Female: return TEXT("Female");
		case EEFCalystoGenderV4::Male: return TEXT("Male");
		default: return TEXT("Any");
		}
	}

	static const TCHAR* LifecycleName(const EEFCalystoLifecycleV4 Lifecycle)
	{
		switch (Lifecycle)
		{
		case EEFCalystoLifecycleV4::Recruitable: return TEXT("Recruitable");
		default: return TEXT("FloorLocal");
		}
	}

	static int32 CategoryPriority(const EEFCalystoContentCategoryV4 Category)
	{
		switch (Category)
		{
		case EEFCalystoContentCategoryV4::Chest: return 0;
		case EEFCalystoContentCategoryV4::NPC: return 1;
		case EEFCalystoContentCategoryV4::Enemy: return 2;
		case EEFCalystoContentCategoryV4::SpecialEvent: return 3;
		case EEFCalystoContentCategoryV4::Food: return 4;
		case EEFCalystoContentCategoryV4::LooseLoot: return 5;
		case EEFCalystoContentCategoryV4::Clothing: return 6;
		default: return 7;
		}
	}

	static int32 HardCapForCategory(const EEFCalystoContentCategoryV4 Category)
	{
		switch (Category)
		{
		case EEFCalystoContentCategoryV4::Enemy: return MaxEnemyCount;
		case EEFCalystoContentCategoryV4::NPC: return MaxNPCCount;
		case EEFCalystoContentCategoryV4::Food: return MaxFoodCount;
		case EEFCalystoContentCategoryV4::Chest: return MaxChestCount;
		case EEFCalystoContentCategoryV4::LooseLoot: return MaxLooseLootCount;
		case EEFCalystoContentCategoryV4::Clothing: return MaxClothingCount;
		case EEFCalystoContentCategoryV4::SpecialEvent: return MaxSpecialEventCount;
		default: return 0;
		}
	}

	static bool RequiresProjectBridge(const EEFCalystoContentCategoryV4 Category)
	{
		return Category == EEFCalystoContentCategoryV4::Enemy
			|| Category == EEFCalystoContentCategoryV4::NPC
			|| Category == EEFCalystoContentCategoryV4::Chest;
	}

	static bool IsPopulationCategory(const EEFCalystoContentCategoryV4 Category)
	{
		return Category == EEFCalystoContentCategoryV4::Enemy
			|| Category == EEFCalystoContentCategoryV4::NPC;
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

	static FString MakeTransformStableId(const FVector& Location, const float Yaw, const TCHAR* Source)
	{
		return HashCanonicalString(FString::Printf(
			TEXT("EFCalystoAnchorV4|%s|%s"),
			Source,
			*QuantizedTransformRecord(Location, Yaw)));
	}

	static uint64 DerivePlacementValue(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FSpawnRequest& Request,
		const FString& CandidateStableId,
		const bool bYaw)
	{
		return HashRank(FString::Printf(
			TEXT("EFCalystoPlacementV4|%lld|%lld|%lld|%d|%s|%s|%s|%s|%s|%d"),
			Intent.RunSeed,
			Intent.FloorNumber,
			Intent.GenerationSerial,
			Intent.GeneratorVersion,
			*Intent.PolicyHash,
			*Intent.EcologyHash,
			bYaw ? TEXT("Anchors.Yaw") : CategoryName(Request.Directive->Category),
			*Request.StableKey,
			*CandidateStableId,
			Request.Directive->CategorySlotIndex));
	}

	static IEFCalystoPopulationBridgeV4* ResolveUniqueBridge(
		const EEFCalystoContentCategoryV4 Category,
		FString& OutError)
	{
		TArray<IEFCalystoPopulationBridgeV4*> Matching;
		const TArray<IEFCalystoPopulationBridgeV4*> Implementations =
			IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoPopulationBridgeV4>(
				IEFCalystoPopulationBridgeV4::GetModularFeatureName());
		for (IEFCalystoPopulationBridgeV4* Implementation : Implementations)
		{
			if (Implementation && Implementation->HandlesCategory(Category))
			{
				Matching.Add(Implementation);
			}
		}
		if (Matching.Num() != 1)
		{
			OutError = FString::Printf(
				TEXT("V4 category %s requires exactly one project population bridge; found %d."),
				CategoryName(Category),
				Matching.Num());
			return nullptr;
		}
		return Matching[0];
	}

	static bool IsProtectedLocation(const FVector& Location, const TArray<FVector>& ProtectedLocations)
	{
		for (const FVector& ProtectedLocation : ProtectedLocations)
		{
			if (FVector::DistSquared2D(Location, ProtectedLocation) < FMath::Square(ProtectedPointRadius))
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
		if (!IsValid(World) || !NavigationSystem || !DungeonBounds.IsValid
			|| Candidates.Num() >= MaxCandidateCount)
		{
			return false;
		}

		FNavLocation Projected;
		if (!NavigationSystem->ProjectPointToNavigation(
				RawLocation,
				Projected,
				FVector(250.0f, 250.0f, 900.0f))
			|| !DungeonBounds.IsInsideXY(Projected.Location))
		{
			return false;
		}
		UNavigationPath* ReachabilityPath = UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			ReachabilityOrigin,
			Projected.Location);
		if (!IsValid(ReachabilityPath) || !ReachabilityPath->IsValid()
			|| ReachabilityPath->IsPartial() || ReachabilityPath->PathPoints.Num() < 2
			|| IsProtectedLocation(Projected.Location, ProtectedLocations)
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
		if (!IsValid(World) || !NavigationSystem || !DungeonBounds.IsValid
			|| Candidates.Num() >= DesiredCandidateCount)
		{
			return;
		}
		const FVector Size = DungeonBounds.GetSize();
		if (Size.ContainsNaN() || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return;
		}

		const double BaseStep = FMath::Clamp(FMath::Max(Size.X, Size.Y) / 48.0, 300.0, 750.0);
		const double QueryZ = DungeonBounds.GetCenter().Z;
		const double StepScales[] = { 1.0, 0.67, 0.50, 0.34 };
		const double PhaseFractions[] = { 0.0, 0.5 };
		int32 GridAttempts = 0;
		for (const double StepScale : StepScales)
		{
			const double Step = FMath::Max(
				static_cast<double>(CandidateDeduplicationRadius) * 1.5,
				BaseStep * StepScale);
			for (const double PhaseFraction : PhaseFractions)
			{
				const double Offset = Step * PhaseFraction;
				const double StartX = FMath::CeilToDouble((DungeonBounds.Min.X - Offset) / Step) * Step + Offset;
				const double StartY = FMath::CeilToDouble((DungeonBounds.Min.Y - Offset) / Step) * Step + Offset;
				for (double Y = StartY;
					Y <= DungeonBounds.Max.Y
						&& Candidates.Num() < DesiredCandidateCount
						&& GridAttempts < MaxSyntheticGridAttempts;
					Y += Step)
				{
					for (double X = StartX;
						X <= DungeonBounds.Max.X
							&& Candidates.Num() < DesiredCandidateCount
							&& GridAttempts < MaxSyntheticGridAttempts;
						X += Step)
					{
						++GridAttempts;
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
				if (Candidates.Num() >= DesiredCandidateCount || GridAttempts >= MaxSyntheticGridAttempts)
				{
					break;
				}
			}
			if (Candidates.Num() >= DesiredCandidateCount || GridAttempts >= MaxSyntheticGridAttempts)
			{
				break;
			}
		}
		UE_LOG(
			LogEFCalystoPopulationV4,
			Verbose,
			TEXT("Deterministic V4 NavMesh grid completed with %d/%d candidates after %d bounded attempts."),
			Candidates.Num(),
			DesiredCandidateCount,
			GridAttempts);
	}

	static void ResolveSpawnShape(
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		UClass* ActorClass,
		float& OutRadius,
		float& OutHalfHeight,
		float& OutActorZOffset)
	{
		OutRadius = 30.0f;
		OutHalfHeight = 30.0f;
		OutActorZOffset = 2.0f;
		if (Directive.Category == EEFCalystoContentCategoryV4::Enemy
			|| Directive.Category == EEFCalystoContentCategoryV4::NPC)
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
		else if (Directive.Category == EEFCalystoContentCategoryV4::Chest)
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
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFCalystoPopulationV4Overlap), false);
		const FVector CollisionCenter = NavigationLocation + FVector(0.0f, 0.0f, HalfHeight + 3.0f);
		return !World->OverlapBlockingTestByChannel(
			CollisionCenter,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight),
			QueryParams);
	}

	static void AddDirectiveTags(
		AActor* Actor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive)
	{
		if (!IsValid(Actor))
		{
			return;
		}
		Actor->Tags.AddUnique(UEFProceduralSettings::Get()->GeneratedActorTag);
		Actor->Tags.AddUnique(FName(TEXT("EF.Calysto.Population.V4")));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Category.%s"), CategoryName(Directive.Category))));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Instance.%s"), *Directive.StableInstanceId.ToString())));
		if (Directive.StableCompanionId.IsValid())
		{
			Actor->Tags.AddUnique(FName(*FString::Printf(
				TEXT("EF.Calysto.V4.Companion.%s"),
				*Directive.StableCompanionId.ToString(EGuidFormats::Digits))));
		}
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Catalog.%s"), *Directive.CatalogId.ToString())));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Variant.%s"), *Directive.VariantId.ToString())));
		if (!Directive.Archetype.IsNone())
		{
			Actor->Tags.AddUnique(FName(*FString::Printf(
				TEXT("EF.Calysto.V4.Archetype.%s"), *Directive.Archetype.ToString())));
		}
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Gender.%s"), GenderName(Directive.Gender))));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Lifecycle.%s"), LifecycleName(Directive.Lifecycle))));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.V4.Tier.%s"), TierName(Directive.Tier))));
		if (Directive.Category == EEFCalystoContentCategoryV4::Enemy
			|| Directive.Category == EEFCalystoContentCategoryV4::NPC)
		{
			Actor->Tags.AddUnique(FName(TEXT("EF.Calysto.V4.DirectorAssignedLevel")));
			Actor->Tags.AddUnique(FName(*FString::Printf(
				TEXT("EF.Calysto.V4.LogicalLevel.%d"), Directive.LogicalLevel)));
		}
	}

	static void GatherChestContent(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FName ContainerInstanceId,
		TArray<FEFCalystoChestContentDirectiveV4>& OutContent)
	{
		OutContent.Reset();
		for (const FEFCalystoChestContentDirectiveV4& Content : Intent.ChestContentDirectives)
		{
			if (Content.ContainerInstanceId == ContainerInstanceId)
			{
				OutContent.Add(Content);
			}
		}
		OutContent.Sort([](const FEFCalystoChestContentDirectiveV4& Left, const FEFCalystoChestContentDirectiveV4& Right)
		{
			if (Left.StableAttemptId != Right.StableAttemptId)
			{
				return Left.StableAttemptId.LexicalLess(Right.StableAttemptId);
			}
			if (Left.ContentCatalogId != Right.ContentCatalogId)
			{
				return Left.ContentCatalogId.LexicalLess(Right.ContentCatalogId);
			}
			return Left.ContentClass.ToSoftObjectPath().ToString()
				< Right.ContentClass.ToSoftObjectPath().ToString();
		});
	}

	static FString CanonicalChestContentRecord(const FEFCalystoChestContentDirectiveV4& Content)
	{
		return FString::Printf(
			TEXT("%s|%s|%s|%s|%d|%d"),
			*Content.ContainerInstanceId.ToString(),
			*Content.StableAttemptId.ToString(),
			*Content.ContentCatalogId.ToString(),
			*Content.ContentClass.ToSoftObjectPath().ToString(),
			static_cast<int32>(Content.Tier),
			Content.CooldownFloors);
	}

	static bool EqualChestContentMultisets(
		const TArray<FEFCalystoChestContentDirectiveV4>& Left,
		const TArray<FEFCalystoChestContentDirectiveV4>& Right)
	{
		TArray<FString> LeftRecords;
		TArray<FString> RightRecords;
		LeftRecords.Reserve(Left.Num());
		RightRecords.Reserve(Right.Num());
		for (const FEFCalystoChestContentDirectiveV4& Content : Left)
		{
			LeftRecords.Add(CanonicalChestContentRecord(Content));
		}
		for (const FEFCalystoChestContentDirectiveV4& Content : Right)
		{
			RightRecords.Add(CanonicalChestContentRecord(Content));
		}
		LeftRecords.Sort();
		RightRecords.Sort();
		return LeftRecords == RightRecords;
	}

	static bool ValidateIntent(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		FCategoryCounts& OutCounts,
		FString& OutError)
	{
		OutCounts = FCategoryCounts();
		if (!Intent.bIsValid || Intent.GeneratorVersion != 4 || Intent.RunSeed <= 0
			|| Intent.FloorNumber <= 0 || Intent.GenerationSerial <= 0
			|| Intent.PolicyHash.IsEmpty() || Intent.EcologyHash.IsEmpty()
			|| Intent.CompanionSnapshotHash.IsEmpty() || Intent.IntentHash.IsEmpty()
			|| Intent.DungeonSize.X < 18 || Intent.DungeonSize.X > 30
			|| Intent.DungeonSize.Y < 18 || Intent.DungeonSize.Y > 30
			|| Intent.DungeonSize.Z != 1
			|| !FMath::IsFinite(Intent.CandidateAnchorDensity)
			|| Intent.CandidateAnchorDensity < 0.20f || Intent.CandidateAnchorDensity > 0.50f
			|| !FMath::IsFinite(Intent.SidePathChance)
			|| Intent.SidePathChance < 0.30f || Intent.SidePathChance > 0.70f
			|| !FMath::IsFinite(Intent.ThreatBudget) || Intent.ThreatBudget < 0.0f
			|| !FMath::IsFinite(Intent.PlannedThreatCost) || Intent.PlannedThreatCost < 0.0f
			|| !FMath::IsFinite(Intent.ResourceBudget) || Intent.ResourceBudget < 0.0f
			|| !FMath::IsFinite(Intent.PlannedResourceCost) || Intent.PlannedResourceCost < 0.0f)
		{
			OutError = TEXT("Resolved V4 floor intent identity, hashes, layout or budgets are invalid.");
			return false;
		}
		const FString ComputedCompanionSnapshotHash =
			FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Intent.CompanionSnapshot);
		if (!ComputedCompanionSnapshotHash.Equals(Intent.CompanionSnapshotHash, ESearchCase::IgnoreCase))
		{
			OutError = TEXT("Resolved V4 companion snapshot hash does not match its frozen payload.");
			return false;
		}

		TSet<FName> StableInstanceIds;
		TSet<FString> CategorySlots;
		TMap<FGuid, const FEFCalystoResolvedCompanionLevelV4*> FrozenCompanionLevels;
		for (const FEFCalystoResolvedCompanionLevelV4& Level : Intent.ResolvedCompanionLevels)
		{
			if (!Level.StableCompanionId.IsValid()
				|| FrozenCompanionLevels.Contains(Level.StableCompanionId)
				|| Level.LogicalLevel < 1
				|| Level.PhysicalACFLevel != FMath::Clamp(Level.LogicalLevel, 1, 100))
			{
				OutError = TEXT("Frozen V4 companion levels contain an invalid or duplicated record.");
				return false;
			}
			FrozenCompanionLevels.Add(Level.StableCompanionId, &Level);
		}
		if (FrozenCompanionLevels.Num() != Intent.CompanionSnapshot.Records.Num())
		{
			OutError = TEXT("Frozen V4 companion levels do not cover the exact roster snapshot.");
			return false;
		}
		TMap<FGuid, const FEFCalystoCompanionRecordV4*> ExpectedActiveParty;
		for (const FEFCalystoCompanionRecordV4& Companion : Intent.CompanionSnapshot.Records)
		{
			const FEFCalystoResolvedCompanionLevelV4* const* FrozenLevel =
				FrozenCompanionLevels.Find(Companion.StableCompanionId);
			if (!FrozenLevel || !*FrozenLevel || (*FrozenLevel)->Grade != Companion.Grade)
			{
				OutError = TEXT("Frozen V4 companion level does not match its roster record and difficulty grade.");
				return false;
			}
			if (Companion.State == EEFCalystoCompanionRosterStateV4::ActiveParty)
			{
				if (!Companion.StableCompanionId.IsValid()
					|| ExpectedActiveParty.Contains(Companion.StableCompanionId))
				{
					OutError = TEXT("Frozen V4 companion snapshot contains an invalid or duplicated active-party ID.");
					return false;
				}
				ExpectedActiveParty.Add(Companion.StableCompanionId, &Companion);
			}
		}
		if (ExpectedActiveParty.Num() > 2)
		{
			OutError = TEXT("Frozen V4 active party exceeds the hard cap of two companions.");
			return false;
		}
		TSet<FGuid> ProjectedActiveParty;
		float PlannedEnemyCost = 0.0f;
		for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
		{
			const FSoftObjectPath ClassPath = Directive.ActorClass.ToSoftObjectPath();
			if (Directive.StableInstanceId.IsNone() || Directive.CatalogId.IsNone()
				|| Directive.VariantId.IsNone()
				|| Directive.CategorySlotIndex < 0 || !ClassPath.IsValid()
				|| Directive.CooldownFloors < 0 || Directive.CooldownFloors > 100
				|| !FMath::IsFinite(Directive.EffectiveThreatCost)
				|| Directive.EffectiveThreatCost < 0.0f
				|| !OutCounts.Increment(Directive.Category))
			{
				OutError = FString::Printf(
					TEXT("V4 directive %s has an invalid identity, category, class, slot or cost."),
					*Directive.StableInstanceId.ToString());
				return false;
			}
			if (StableInstanceIds.Contains(Directive.StableInstanceId))
			{
				OutError = FString::Printf(TEXT("Duplicate V4 instance ID %s."), *Directive.StableInstanceId.ToString());
				return false;
			}
			StableInstanceIds.Add(Directive.StableInstanceId);
			const FString SlotKey = FString::Printf(
				TEXT("%d|%d"), static_cast<int32>(Directive.Category), Directive.CategorySlotIndex);
			if (CategorySlots.Contains(SlotKey))
			{
				OutError = FString::Printf(TEXT("Duplicate V4 category slot %s."), *SlotKey);
				return false;
			}
			CategorySlots.Add(SlotKey);

			const bool bCharacter = Directive.Category == EEFCalystoContentCategoryV4::Enemy
				|| Directive.Category == EEFCalystoContentCategoryV4::NPC;
			if (Directive.Category != EEFCalystoContentCategoryV4::NPC
				&& Directive.StableCompanionId.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Non-NPC V4 directive %s carries a StableCompanionId."),
					*Directive.StableInstanceId.ToString());
				return false;
			}
			if (Directive.Category == EEFCalystoContentCategoryV4::NPC
				&& Directive.StableCompanionId.IsValid())
			{
				const FEFCalystoCompanionRecordV4* const* FrozenRecord =
					ExpectedActiveParty.Find(Directive.StableCompanionId);
				const FEFCalystoResolvedCompanionLevelV4* const* FrozenLevel =
					FrozenCompanionLevels.Find(Directive.StableCompanionId);
				if (!FrozenRecord || !*FrozenRecord
					|| !FrozenLevel || !*FrozenLevel
					|| ProjectedActiveParty.Contains(Directive.StableCompanionId)
					|| (*FrozenRecord)->SourceCatalogId != Directive.CatalogId
					|| (*FrozenRecord)->ActorClass.ToSoftObjectPath() != Directive.ActorClass.ToSoftObjectPath()
					|| (*FrozenRecord)->Archetype != Directive.Archetype
					|| (*FrozenRecord)->Gender != Directive.Gender
					|| (*FrozenRecord)->Grade != Directive.Tier
					|| (*FrozenLevel)->LogicalLevel != Directive.LogicalLevel
					|| (*FrozenLevel)->PhysicalACFLevel != Directive.PhysicalACFLevel
					|| Directive.Lifecycle != EEFCalystoLifecycleV4::Recruitable)
				{
					OutError = FString::Printf(
						TEXT("NPC directive %s does not exactly project one frozen active-party companion."),
						*Directive.StableInstanceId.ToString());
					return false;
				}
				ProjectedActiveParty.Add(Directive.StableCompanionId);
			}
			if ((bCharacter && (Directive.Archetype.IsNone()
					|| Directive.Gender == EEFCalystoGenderV4::Any
					|| Directive.LogicalLevel < 1 || Directive.PhysicalACFLevel < 1
					|| Directive.PhysicalACFLevel > 100
					|| Directive.PhysicalACFLevel != FMath::Clamp(Directive.LogicalLevel, 1, 100)))
				|| (!bCharacter && (Directive.LogicalLevel != 0 || Directive.PhysicalACFLevel != 0))
				|| (Directive.Category == EEFCalystoContentCategoryV4::Chest
					&& (Directive.ChestContentAttemptCount < 0
						|| Directive.ChestContentAttemptCount > MaxChestContentAttempts))
				|| (Directive.Category != EEFCalystoContentCategoryV4::Chest
					&& Directive.ChestContentAttemptCount != 0))
			{
				OutError = FString::Printf(
					TEXT("V4 directive %s has invalid logical/physical level or chest-attempt metadata."),
					*Directive.StableInstanceId.ToString());
				return false;
			}
			if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
			{
				PlannedEnemyCost += Directive.EffectiveThreatCost;
			}
		}
		if (ProjectedActiveParty.Num() != ExpectedActiveParty.Num())
		{
			OutError = FString::Printf(
				TEXT("V4 intent projects %d of %d frozen active-party companions."),
				ProjectedActiveParty.Num(),
				ExpectedActiveParty.Num());
			return false;
		}

		if (OutCounts.Enemies > MaxEnemyCount || OutCounts.NPCs > MaxNPCCount
			|| OutCounts.Food > MaxFoodCount || OutCounts.Chests > MaxChestCount
			|| OutCounts.LooseLoot > MaxLooseLootCount || OutCounts.Clothing > MaxClothingCount
			|| OutCounts.SpecialEvents > MaxSpecialEventCount
			|| OutCounts.Total() > MaxSpawnedActorCount)
		{
			OutError = FString::Printf(
				TEXT("V4 directives exceed hard caps (enemy=%d npc=%d food=%d chest=%d loot=%d clothing=%d event=%d total=%d)."),
				OutCounts.Enemies, OutCounts.NPCs, OutCounts.Food, OutCounts.Chests,
				OutCounts.LooseLoot, OutCounts.Clothing, OutCounts.SpecialEvents, OutCounts.Total());
			return false;
		}

		const float ThreatTolerance = FMath::Max(0.001f, Intent.ThreatBudget * 0.001f);
		if (!FMath::IsFinite(PlannedEnemyCost)
			|| !FMath::IsNearlyEqual(PlannedEnemyCost, Intent.PlannedThreatCost, ThreatTolerance)
			|| PlannedEnemyCost > Intent.ThreatBudget + ThreatTolerance)
		{
			OutError = FString::Printf(
				TEXT("V4 enemy directives cost %.3f, planned %.3f, budget %.3f."),
				PlannedEnemyCost, Intent.PlannedThreatCost, Intent.ThreatBudget);
			return false;
		}
		const float RecomputedResourceCost = static_cast<float>(
			OutCounts.Food + OutCounts.Chests + OutCounts.LooseLoot
			+ OutCounts.Clothing + OutCounts.SpecialEvents);
		const float ResourceTolerance = FMath::Max(0.001f, Intent.ResourceBudget * 0.001f);
		if (!FMath::IsNearlyEqual(RecomputedResourceCost, Intent.PlannedResourceCost, ResourceTolerance)
			|| Intent.PlannedResourceCost > Intent.ResourceBudget + ResourceTolerance)
		{
			OutError = FString::Printf(
				TEXT("V4 resource directives cost %.3f, planned %.3f, budget %.3f."),
				RecomputedResourceCost,
				Intent.PlannedResourceCost,
				Intent.ResourceBudget);
			return false;
		}

		TSet<EEFCalystoContentCategoryV4> SeenCategories;
		for (const FEFCalystoResolvedCategoryV4& Category : Intent.Categories)
		{
			const FEFCalystoTierMixV4& Tiers = Category.ResolvedTiers;
			const float TierMass = Tiers.Common + Tiers.Uncommon + Tiers.Rare + Tiers.Epic;
			const bool bProbabilitiesValid = FMath::IsFinite(Category.StyleThemeBlend)
				&& Category.StyleThemeBlend >= 0.0f && Category.StyleThemeBlend <= 1.0f
				&& FMath::IsFinite(Category.OpportunityChance)
				&& Category.OpportunityChance >= 0.0f && Category.OpportunityChance <= 0.90f
				&& FMath::IsFinite(Category.SelectableTierMass)
				&& Category.SelectableTierMass >= 0.0f && Category.SelectableTierMass <= 0.90f + KINDA_SMALL_NUMBER
				&& FMath::IsFinite(Category.WinterChance)
				&& Category.WinterChance >= 0.0f && Category.WinterChance <= 0.90f
				&& FMath::IsFinite(Category.EffectiveChance)
				&& Category.EffectiveChance >= 0.0f && Category.EffectiveChance <= 0.90f
				&& FMath::IsFinite(Tiers.Common) && Tiers.Common >= 0.0f
				&& FMath::IsFinite(Tiers.Uncommon) && Tiers.Uncommon >= 0.0f
				&& FMath::IsFinite(Tiers.Rare) && Tiers.Rare >= 0.0f
				&& FMath::IsFinite(Tiers.Epic) && Tiers.Epic >= 0.0f
				&& FMath::IsFinite(Tiers.Nothing) && Tiers.Nothing >= 0.10f && Tiers.Nothing <= 1.0f
				&& FMath::IsFinite(TierMass) && TierMass <= 0.90f + KINDA_SMALL_NUMBER
				&& FMath::IsNearlyEqual(Category.SelectableTierMass, TierMass, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Tiers.GetCalculatedNothing(), 1.0f - TierMass, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Tiers.Nothing, Tiers.GetCalculatedNothing(), KINDA_SMALL_NUMBER);
			const bool bAttemptContractValid = Category.AttemptCount >= 0
				&& (Category.Category == EEFCalystoContentCategoryV4::NPC
					? Category.DirectiveCount <= Category.AttemptCount + ExpectedActiveParty.Num()
					: Category.DirectiveCount <= Category.AttemptCount);
			if (SeenCategories.Contains(Category.Category)
				|| !bProbabilitiesValid || !bAttemptContractValid
				|| Category.MinimumWhenPresent < 0 || Category.MaximumPerFloor < Category.MinimumWhenPresent
				|| Category.TargetCount < 0 || Category.DirectiveCount < 0
				|| Category.TargetCount != Category.DirectiveCount
				|| Category.TargetCount != OutCounts.Get(Category.Category)
				|| Category.TargetCount > Category.MaximumPerFloor
				|| Category.TargetCount > HardCapForCategory(Category.Category))
			{
				OutError = FString::Printf(
					TEXT("V4 resolved category %s disagrees with its individual directives."),
					CategoryName(Category.Category));
				return false;
			}
			SeenCategories.Add(Category.Category);
		}
		const EEFCalystoContentCategoryV4 RequiredCategories[] =
		{
			EEFCalystoContentCategoryV4::Enemy,
			EEFCalystoContentCategoryV4::NPC,
			EEFCalystoContentCategoryV4::Food,
			EEFCalystoContentCategoryV4::Chest,
			EEFCalystoContentCategoryV4::LooseLoot,
			EEFCalystoContentCategoryV4::Clothing,
			EEFCalystoContentCategoryV4::SpecialEvent,
			EEFCalystoContentCategoryV4::Decoration,
			EEFCalystoContentCategoryV4::Lighting
		};
		for (const EEFCalystoContentCategoryV4 Category : RequiredCategories)
		{
			if (!SeenCategories.Contains(Category))
			{
				OutError = FString::Printf(TEXT("V4 intent omits resolved category %s."), CategoryName(Category));
				return false;
			}
		}

		TMap<FName, int32> ContentCountByContainer;
		TSet<FName> StableContentAttemptIds;
		for (const FEFCalystoChestContentDirectiveV4& Content : Intent.ChestContentDirectives)
		{
			const FEFCalystoSpawnInstanceDirectiveV4* Container = Intent.SpawnDirectives.FindByPredicate(
				[&Content](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
				{
					return Directive.StableInstanceId == Content.ContainerInstanceId;
				});
			if (Content.ContainerInstanceId.IsNone() || Content.StableAttemptId.IsNone()
				|| StableContentAttemptIds.Contains(Content.StableAttemptId)
				|| Content.ContentCatalogId.IsNone()
				|| Content.CooldownFloors < 0 || Content.CooldownFloors > 100
				|| !Content.ContentClass.ToSoftObjectPath().IsValid() || !Container
				|| Container->Category != EEFCalystoContentCategoryV4::Chest)
			{
				OutError = TEXT("A frozen V4 chest-content directive is invalid or has no matching chest container.");
				return false;
			}
			StableContentAttemptIds.Add(Content.StableAttemptId);
			int32& ContentCount = ContentCountByContainer.FindOrAdd(Content.ContainerInstanceId);
			++ContentCount;
			if (ContentCount > MaxChestContentAttempts
				|| ContentCount > Container->ChestContentAttemptCount)
			{
				OutError = FString::Printf(
					TEXT("Chest %s contains more frozen items than its allowed attempts."),
					*Content.ContainerInstanceId.ToString());
				return false;
			}
		}
		return true;
	}

	static FString CanonicalInstanceRecord(
		const FEFCalystoRealizedInstanceV4& Instance,
		const FString& CandidateStableId)
	{
		TArray<FName> ContentIds = Instance.VerifiedChestContentIds;
		ContentIds.Sort([](const FName A, const FName B) { return A.LexicalLess(B); });
		FString Content;
		for (const FName ContentId : ContentIds)
		{
			Content += ContentId.ToString() + TEXT(",");
		}
		TArray<FEFCalystoChestContentDirectiveV4> VerifiedContents = Instance.VerifiedChestContents;
		VerifiedContents.Sort([](const FEFCalystoChestContentDirectiveV4& Left, const FEFCalystoChestContentDirectiveV4& Right)
		{
			return Left.StableAttemptId.LexicalLess(Right.StableAttemptId);
		});
		for (const FEFCalystoChestContentDirectiveV4& Verified : VerifiedContents)
		{
			Content += FString::Printf(
				TEXT("%s:%s:%s:%d:%d,"),
				*Verified.StableAttemptId.ToString(),
				*Verified.ContentCatalogId.ToString(),
				*Verified.ContentClass.ToSoftObjectPath().ToString(),
				static_cast<int32>(Verified.Tier),
				Verified.CooldownFloors);
		}
		return FString::Printf(
			TEXT("%s|%s|%s|%s|%s|%s|%s|%d|%s|%d|%d|%s|%d|%s|%s"),
			CategoryName(Instance.Category),
			*Instance.StableInstanceId.ToString(),
			*Instance.StableCompanionId.ToString(EGuidFormats::Digits),
			*Instance.CatalogId.ToString(),
			*Instance.VariantId.ToString(),
			*Instance.Archetype.ToString(),
			GenderName(Instance.Gender),
			static_cast<int32>(Instance.Lifecycle),
			*Instance.ActorClass.ToSoftObjectPath().ToString(),
			static_cast<int32>(Instance.Tier),
			Instance.LogicalLevel,
			*FloatBits(Instance.EffectiveThreatCost),
			Instance.CooldownFloors,
			*CandidateStableId,
			*QuantizedTransformRecord(Instance.Transform.GetLocation(), Instance.Transform.Rotator().Yaw)
		) + TEXT("|") + Content;
	}

	static bool ValidateRealizedInstancesAgainstIntent(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const TArray<FEFCalystoRealizedInstanceV4>& Instances,
		FString& OutError)
	{
		if (Instances.Num() != Intent.SpawnDirectives.Num())
		{
			OutError = TEXT("V4 realized-instance count differs from the frozen directive count.");
			return false;
		}
		TMap<FName, const FEFCalystoRealizedInstanceV4*> ByStableId;
		for (const FEFCalystoRealizedInstanceV4& Instance : Instances)
		{
			if (Instance.StableInstanceId.IsNone() || ByStableId.Contains(Instance.StableInstanceId))
			{
				OutError = TEXT("V4 manifest contains an empty or duplicated realized StableInstanceId.");
				return false;
			}
			ByStableId.Add(Instance.StableInstanceId, &Instance);
		}

		for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
		{
			const FEFCalystoRealizedInstanceV4* const* Found = ByStableId.Find(Directive.StableInstanceId);
			if (!Found || !*Found)
			{
				OutError = FString::Printf(
					TEXT("V4 manifest omits frozen directive %s."),
					*Directive.StableInstanceId.ToString());
				return false;
			}
			const FEFCalystoRealizedInstanceV4& Instance = **Found;
			if (Instance.StableCompanionId != Directive.StableCompanionId
				|| Instance.CatalogId != Directive.CatalogId
				|| Instance.VariantId != Directive.VariantId
				|| Instance.Archetype != Directive.Archetype
				|| Instance.Gender != Directive.Gender
				|| Instance.Lifecycle != Directive.Lifecycle
				|| Instance.Category != Directive.Category
				|| Instance.ActorClass.ToSoftObjectPath() != Directive.ActorClass.ToSoftObjectPath()
				|| Instance.Tier != Directive.Tier
				|| Instance.LogicalLevel != Directive.LogicalLevel
				|| FloatBits(Instance.EffectiveThreatCost) != FloatBits(Directive.EffectiveThreatCost)
				|| Instance.CooldownFloors != Directive.CooldownFloors
				|| Instance.Transform.ContainsNaN()
				|| !Instance.Transform.GetRotation().IsNormalized())
			{
				OutError = FString::Printf(
					TEXT("V4 realized instance %s differs from its frozen identity, class, tier, level or cost."),
					*Directive.StableInstanceId.ToString());
				return false;
			}

			TArray<FEFCalystoChestContentDirectiveV4> ExpectedContents;
			GatherChestContent(Intent, Directive.StableInstanceId, ExpectedContents);
			if (!EqualChestContentMultisets(ExpectedContents, Instance.VerifiedChestContents))
			{
				OutError = FString::Printf(
					TEXT("V4 realized chest payload differs from frozen per-attempt directives for %s."),
					*Directive.StableInstanceId.ToString());
				return false;
			}
			TArray<FName> ExpectedIds;
			TArray<FName> VerifiedIds = Instance.VerifiedChestContentIds;
			for (const FEFCalystoChestContentDirectiveV4& Expected : ExpectedContents)
			{
				ExpectedIds.Add(Expected.ContentCatalogId);
			}
			const auto NameLess = [](const FName Left, const FName Right)
			{
				return Left.LexicalLess(Right);
			};
			ExpectedIds.Sort(NameLess);
			VerifiedIds.Sort(NameLess);
			if (ExpectedIds != VerifiedIds)
			{
				OutError = FString::Printf(
					TEXT("V4 realized chest content ID mirror differs for %s."),
					*Directive.StableInstanceId.ToString());
				return false;
			}
		}
		return true;
	}
}

FEFCalystoPopulationMaterializationResultV4 FEFCalystoPopulationMaterializerV4::Materialize(
	UWorld* World,
	AActor* DungeonActor,
	const FBox& DungeonBounds,
	const FEFCalystoResolvedFloorIntentV4& Intent)
{
	using namespace EFCalystoPopulationV4Private;

	FEFCalystoPopulationMaterializationResultV4 Result;
	auto Fail = [&Result](FString&& Reason)
	{
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};
	if (!IsValid(World) || !World->IsGameWorld() || !IsValid(DungeonActor) || !DungeonBounds.IsValid)
	{
		return Fail(TEXT("Cannot materialize V4 population without a valid game world, dungeon actor and bounds."));
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

	FCategoryCounts ExpectedCounts;
	FString ValidationError;
	if (!ValidateIntent(Intent, ExpectedCounts, ValidationError))
	{
		return Fail(MoveTemp(ValidationError));
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return Fail(TEXT("Runtime navigation is unavailable while materializing V4 population."));
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
			TEXT("V4 materialization requires exactly one configured start point; found %d (class=%s)."),
			StartPoints.Num(),
			*GetPathNameSafe(StartPointClass)));
	}
	FNavLocation ProjectedStart;
	if (!NavigationSystem->ProjectPointToNavigation(
			StartPoints[0]->GetActorLocation(),
			ProjectedStart,
			FVector(600.0f, 600.0f, 1400.0f)))
	{
		return Fail(TEXT("The configured V4 start point could not be projected to runtime navigation."));
	}

	TArray<FVector> ProtectedLocations;
	int32 DoorCount = 0;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt))
		{
			++DoorCount;
			ProtectedLocations.Add(DoorIt->GetActorLocation());
		}
	}
	if (DoorCount != 1)
	{
		return Fail(FString::Printf(TEXT("V4 topology requires exactly one floor door; found %d."), DoorCount));
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
		return QuantizedTransformRecord(Left, 0.0f) < QuantizedTransformRecord(Right, 0.0f);
	});

	TArray<FRawAnchorCandidate> RawCandidates;
	RawCandidates.Reserve(AnchorActors.Num());
	for (const TWeakObjectPtr<AEFCalystoPopulationAnchor>& AnchorPtr : AnchorActors)
	{
		if (const AEFCalystoPopulationAnchor* Anchor = AnchorPtr.Get())
		{
			FRawAnchorCandidate& Raw = RawCandidates.AddDefaulted_GetRef();
			const FVector Location = Anchor->GetActorLocation();
			Raw.Location = FVector(
				FMath::GridSnap(Location.X, 10.0),
				FMath::GridSnap(Location.Y, 10.0),
				FMath::GridSnap(Location.Z, 10.0));
			Raw.Yaw = static_cast<float>(FMath::RoundToInt(FRotator::ClampAxis(Anchor->GetActorRotation().Yaw)));
			Raw.StableId = MakeTransformStableId(Raw.Location, Raw.Yaw, TEXT("RAW"));
		}
	}
	RawCandidates.Sort([](const FRawAnchorCandidate& Left, const FRawAnchorCandidate& Right)
	{
		return Left.StableId == Right.StableId
			? QuantizedTransformRecord(Left.Location, Left.Yaw) < QuantizedTransformRecord(Right.Location, Right.Yaw)
			: Left.StableId < Right.StableId;
	});

	TArray<FCandidate> Candidates;
	Candidates.Reserve(FMath::Min(MaxCandidateCount, FMath::Max(AnchorActors.Num(), 128)));
	for (const FRawAnchorCandidate& Raw : RawCandidates)
	{
		TryAddCandidate(
			World,
			NavigationSystem,
			DungeonBounds,
			ProjectedStart.Location,
			Raw.Location,
			Raw.Yaw,
			false,
			ProtectedLocations,
			Candidates);
	}
	const int32 DesiredCandidateCount = FMath::Min(
		MaxCandidateCount,
		FMath::Max(ExpectedCounts.Total(), ExpectedCounts.Total() * 4));
	AddSyntheticCandidates(
		World,
		NavigationSystem,
		DungeonBounds,
		ProjectedStart.Location,
		DesiredCandidateCount,
		ProtectedLocations,
		Candidates);
	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
	{
		return Left.StableId < Right.StableId;
	});
	if ((ExpectedCounts.Total() > 0 && Candidates.IsEmpty()) || Candidates.Num() > MaxCandidateCount
		|| Candidates.Num() < ExpectedCounts.Total())
	{
		return Fail(FString::Printf(
			TEXT("V4 anchor/grid preparation produced %d candidates for %d actor directives."),
			Candidates.Num(),
			ExpectedCounts.Total()));
	}

	TArray<FString> TopologyRecords;
	TopologyRecords.Reserve(RawCandidates.Num() + Candidates.Num() + 2);
	TopologyRecords.Add(FString::Printf(
		TEXT("START|%s"),
		*QuantizedTransformRecord(StartPoints[0]->GetActorLocation(), StartPoints[0]->GetActorRotation().Yaw)));
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt))
		{
			TopologyRecords.Add(FString::Printf(
				TEXT("DOOR|%s"),
				*QuantizedTransformRecord(DoorIt->GetActorLocation(), DoorIt->GetActorRotation().Yaw)));
		}
	}
	for (const FRawAnchorCandidate& Raw : RawCandidates)
	{
		TopologyRecords.Add(FString::Printf(TEXT("RAW|%s"), *Raw.StableId));
	}
	for (const FCandidate& Candidate : Candidates)
	{
		TopologyRecords.Add(FString::Printf(
			TEXT("CANDIDATE|%s|%d"),
			*Candidate.StableId,
			Candidate.bSynthetic ? 1 : 0));
	}
	TopologyRecords.Sort();
	const FString AnchorTopologyHash = HashCanonicalString(
		TEXT("EFCalystoAnchorTopologyV4\n") + FString::Join(TopologyRecords, TEXT("\n")));
	if (AnchorTopologyHash.IsEmpty())
	{
		return Fail(TEXT("Failed to hash deterministic V4 anchor topology."));
	}

	TArray<FSpawnRequest> Requests;
	Requests.Reserve(Intent.SpawnDirectives.Num());
	for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
	{
		FSpawnRequest& Request = Requests.AddDefaulted_GetRef();
		Request.Directive = &Directive;
		Request.StableKey = FString::Printf(
			TEXT("%d|%s|%s|%s|%s|%s|%s|%d|%s|%06d|%03d"),
			static_cast<int32>(Directive.Category),
			*Directive.StableInstanceId.ToString(),
			*Directive.StableCompanionId.ToString(EGuidFormats::Digits),
			*Directive.CatalogId.ToString(),
			*Directive.VariantId.ToString(),
			*Directive.Archetype.ToString(),
			GenderName(Directive.Gender),
			static_cast<int32>(Directive.Lifecycle),
			*Directive.ActorClass.ToSoftObjectPath().ToString(),
			Directive.CategorySlotIndex,
			Directive.CooldownFloors);
	}
	Requests.Sort([](const FSpawnRequest& Left, const FSpawnRequest& Right)
	{
		const int32 LeftPriority = CategoryPriority(Left.Directive->Category);
		const int32 RightPriority = CategoryPriority(Right.Directive->Category);
		return LeftPriority == RightPriority
			? Left.StableKey < Right.StableKey
			: LeftPriority < RightPriority;
	});

	TArray<FRealizedActor> RealizedActors;
	TArray<FVector> OccupiedLocations;
	TSet<int32> UsedCandidateIndices;
	TArray<FString> PopulationRecords;
	TArray<FString> ResourceRecords;
	TArray<FEFCalystoRealizedInstanceV4> RealizedInstances;
	auto RollbackAll = [&RealizedActors, World]()
	{
		for (int32 Index = RealizedActors.Num() - 1; Index >= 0; --Index)
		{
			FRealizedActor& Realized = RealizedActors[Index];
			AActor* Actor = Realized.Actor.Get();
			if (!IsValid(Actor))
			{
				continue;
			}
			if (Realized.Bridge && Realized.Directive)
			{
				Realized.Bridge->RollbackSpawnedActor(World, Actor, *Realized.Directive);
			}
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				Actor->Destroy();
			}
		}
		RealizedActors.Reset();
	};

	for (const FSpawnRequest& Request : Requests)
	{
		const FEFCalystoSpawnInstanceDirectiveV4& Directive = *Request.Directive;
		UClass* ActorClass = Directive.ActorClass.Get();
		const bool bRequiresPawn = Directive.Category == EEFCalystoContentCategoryV4::Enemy
			|| Directive.Category == EEFCalystoContentCategoryV4::NPC;
		if (!IsValid(ActorClass) || !ActorClass->IsChildOf(AActor::StaticClass())
			|| ActorClass->HasAnyClassFlags(CLASS_Abstract)
			|| (bRequiresPawn && !ActorClass->IsChildOf(APawn::StaticClass())))
		{
			RollbackAll();
			return Fail(FString::Printf(
				TEXT("Preloaded class %s is invalid for V4 %s directive %s."),
				*Directive.ActorClass.ToSoftObjectPath().ToString(),
				CategoryName(Directive.Category),
				*Directive.StableInstanceId.ToString()));
		}

		TArray<FEFCalystoChestContentDirectiveV4> ChestContent;
		GatherChestContent(Intent, Directive.StableInstanceId, ChestContent);
		for (const FEFCalystoChestContentDirectiveV4& Content : ChestContent)
		{
			if (!IsValid(Content.ContentClass.Get()))
			{
				RollbackAll();
				return Fail(FString::Printf(
					TEXT("Preloaded chest content %s is unavailable for container %s."),
					*Content.ContentClass.ToSoftObjectPath().ToString(),
					*Directive.StableInstanceId.ToString()));
			}
		}

		IEFCalystoPopulationBridgeV4* Bridge = nullptr;
		if (RequiresProjectBridge(Directive.Category))
		{
			FString BridgeError;
			Bridge = ResolveUniqueBridge(Directive.Category, BridgeError);
			if (!Bridge)
			{
				RollbackAll();
				return Fail(MoveTemp(BridgeError));
			}
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
		TArray<FEFCalystoChestContentDirectiveV4> VerifiedChestContents;
		for (const TPair<uint64, int32>& Ranked : RankedCandidates)
		{
			const int32 CandidateIndex = Ranked.Value;
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
			AddDirectiveTags(SpawnedActor, Directive);
			if (Bridge)
			{
				FString BridgeError;
				if (!Bridge->PrepareDeferredActor(
						World,
						SpawnedActor,
						Directive,
						MakeArrayView(ChestContent),
						BridgeError))
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Directive);
					if (IsValid(SpawnedActor) && !SpawnedActor->IsActorBeingDestroyed())
					{
						SpawnedActor->Destroy();
					}
					RollbackAll();
					return Fail(FString::Printf(
						TEXT("V4 bridge preparation failed for %s: %s"),
						*Directive.StableInstanceId.ToString(),
						*BridgeError));
				}
			}

			SpawnedActor->FinishSpawning(SpawnTransform);
			if (!IsValid(SpawnedActor) || SpawnedActor->IsActorBeingDestroyed())
			{
				if (Bridge && SpawnedActor)
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Directive);
				}
				SpawnedActor = nullptr;
				continue;
			}
			if (Bridge)
			{
				FString BridgeError;
				if (!Bridge->FinalizeSpawnedActor(
						World,
						SpawnedActor,
						Directive,
						MakeArrayView(ChestContent),
						VerifiedChestContents,
						BridgeError))
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Directive);
					if (IsValid(SpawnedActor) && !SpawnedActor->IsActorBeingDestroyed())
					{
						SpawnedActor->Destroy();
					}
					RollbackAll();
					return Fail(FString::Printf(
						TEXT("V4 bridge finalization failed for %s: %s"),
						*Directive.StableInstanceId.ToString(),
						*BridgeError));
				}
			}

			if ((Directive.Category == EEFCalystoContentCategoryV4::Chest
					&& !EqualChestContentMultisets(ChestContent, VerifiedChestContents))
				|| (Directive.Category != EEFCalystoContentCategoryV4::Chest
					&& !VerifiedChestContents.IsEmpty()))
			{
				if (Bridge)
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Directive);
				}
				if (IsValid(SpawnedActor) && !SpawnedActor->IsActorBeingDestroyed())
				{
					SpawnedActor->Destroy();
				}
				RollbackAll();
				return Fail(FString::Printf(
					TEXT("V4 chest content verification differs from frozen intent for %s."),
					*Directive.StableInstanceId.ToString()));
			}

			SelectedCandidateIndex = CandidateIndex;
			break;
		}

		if (!IsValid(SpawnedActor) || SelectedCandidateIndex == INDEX_NONE)
		{
			RollbackAll();
			return Fail(FString::Printf(
				TEXT("No safe deterministic candidate remained for V4 %s directive %s (candidates=%d)."),
				CategoryName(Directive.Category),
				*Directive.StableInstanceId.ToString(),
				Candidates.Num()));
		}

		UsedCandidateIndices.Add(SelectedCandidateIndex);
		OccupiedLocations.Add(Candidates[SelectedCandidateIndex].NavigationLocation);
		FRealizedActor& RealizedActor = RealizedActors.AddDefaulted_GetRef();
		RealizedActor.Actor = SpawnedActor;
		RealizedActor.Directive = &Directive;
		RealizedActor.Bridge = Bridge;

		FEFCalystoRealizedInstanceV4& Instance = RealizedInstances.AddDefaulted_GetRef();
		Instance.StableInstanceId = Directive.StableInstanceId;
		Instance.StableCompanionId = Directive.StableCompanionId;
		Instance.CatalogId = Directive.CatalogId;
		Instance.VariantId = Directive.VariantId;
		Instance.Archetype = Directive.Archetype;
		Instance.Gender = Directive.Gender;
		Instance.Lifecycle = Directive.Lifecycle;
		Instance.Category = Directive.Category;
		Instance.ActorClass = Directive.ActorClass;
		Instance.Transform = SpawnedActor->GetActorTransform();
		Instance.Tier = Directive.Tier;
		Instance.LogicalLevel = Directive.LogicalLevel;
		Instance.EffectiveThreatCost = Directive.EffectiveThreatCost;
		Instance.CooldownFloors = Directive.CooldownFloors;
		Instance.VerifiedChestContents = MoveTemp(VerifiedChestContents);
		for (const FEFCalystoChestContentDirectiveV4& Verified : Instance.VerifiedChestContents)
		{
			Instance.VerifiedChestContentIds.Add(Verified.ContentCatalogId);
		}
		const FString Record = CanonicalInstanceRecord(Instance, Candidates[SelectedCandidateIndex].StableId);
		(IsPopulationCategory(Directive.Category) ? PopulationRecords : ResourceRecords).Add(Record);
	}

	int32 LiveActorCount = 0;
	for (const FRealizedActor& Realized : RealizedActors)
	{
		LiveActorCount += Realized.Actor.IsValid() && !Realized.Actor->IsActorBeingDestroyed() ? 1 : 0;
	}
	if (LiveActorCount != ExpectedCounts.Total() || RealizedInstances.Num() != ExpectedCounts.Total())
	{
		RollbackAll();
		return Fail(FString::Printf(
			TEXT("V4 materialization realized %d live actors/%d instances but intent requires %d."),
			LiveActorCount,
			RealizedInstances.Num(),
			ExpectedCounts.Total()));
	}
	FString RealizedValidationError;
	if (!ValidateRealizedInstancesAgainstIntent(Intent, RealizedInstances, RealizedValidationError))
	{
		RollbackAll();
		return Fail(MoveTemp(RealizedValidationError));
	}

	PopulationRecords.Sort();
	ResourceRecords.Sort();
	const FString PopulationHash = HashCanonicalString(
		TEXT("EFCalystoPopulationV4\n")
		+ (PopulationRecords.IsEmpty() ? FString(TEXT("EMPTY")) : FString::Join(PopulationRecords, TEXT("\n"))));
	const FString ResourceHash = HashCanonicalString(
		TEXT("EFCalystoResourcesV4\n")
		+ (ResourceRecords.IsEmpty() ? FString(TEXT("EMPTY")) : FString::Join(ResourceRecords, TEXT("\n"))));
	if (PopulationHash.IsEmpty() || ResourceHash.IsEmpty())
	{
		RollbackAll();
		return Fail(TEXT("Failed to hash realized V4 population or resources."));
	}

	RealizedInstances.Sort([](const FEFCalystoRealizedInstanceV4& Left, const FEFCalystoRealizedInstanceV4& Right)
	{
		return Left.StableInstanceId.LexicalLess(Right.StableInstanceId);
	});
	FEFCalystoRealizedFloorManifestV4 Manifest;
	Manifest.RunSeed = Intent.RunSeed;
	Manifest.FloorNumber = Intent.FloorNumber;
	Manifest.GenerationSerial = Intent.GenerationSerial;
	Manifest.IntentHash = Intent.IntentHash;
	Manifest.AnchorTopologyHash = AnchorTopologyHash;
	Manifest.PopulationHash = PopulationHash;
	Manifest.ResourceHash = ResourceHash;
	Manifest.CompanionSnapshotHash = Intent.CompanionSnapshotHash;
	Manifest.CandidateAnchorCount = Candidates.Num();
	Manifest.EnemyCount = ExpectedCounts.Enemies;
	Manifest.NPCCount = ExpectedCounts.NPCs;
	Manifest.FoodCount = ExpectedCounts.Food;
	Manifest.ChestCount = ExpectedCounts.Chests;
	Manifest.LooseLootCount = ExpectedCounts.LooseLoot;
	Manifest.ClothingCount = ExpectedCounts.Clothing;
	Manifest.SpecialEventCount = ExpectedCounts.SpecialEvents;
	Manifest.SpawnedActorCount = ExpectedCounts.Total();
	Manifest.RealizedThreatCost = Intent.PlannedThreatCost;
	Manifest.RealizedResourceCost = Intent.PlannedResourceCost;
	Manifest.Instances = MoveTemp(RealizedInstances);
	Manifest.ManifestHash = UEFCalystoDungeonSubsystem::ComputeManifestHashV4(Manifest);
	Manifest.bIsValid = !Manifest.ManifestHash.IsEmpty();
	if (!Manifest.bIsValid)
	{
		RollbackAll();
		return Fail(TEXT("Failed to hash realized V4 floor manifest."));
	}

	Result.bSucceeded = true;
	Result.Manifest = MoveTemp(Manifest);
	UE_LOG(
		LogEFCalystoPopulationV4,
		Log,
		TEXT("PopulationRealizedV4 floor=%lld serial=%lld candidates=%d enemies=%d npc=%d food=%d chests=%d loot=%d clothing=%d events=%d actors=%d anchorHash=%s populationHash=%s resourceHash=%s companionHash=%s manifestHash=%s."),
		Intent.FloorNumber,
		Intent.GenerationSerial,
		Candidates.Num(),
		Result.Manifest.EnemyCount,
		Result.Manifest.NPCCount,
		Result.Manifest.FoodCount,
		Result.Manifest.ChestCount,
		Result.Manifest.LooseLootCount,
		Result.Manifest.ClothingCount,
		Result.Manifest.SpecialEventCount,
		Result.Manifest.SpawnedActorCount,
		*Result.Manifest.AnchorTopologyHash,
		*Result.Manifest.PopulationHash,
		*Result.Manifest.ResourceHash,
		*Result.Manifest.CompanionSnapshotHash,
		*Result.Manifest.ManifestHash);
	return Result;
}

bool FEFCalystoPopulationMaterializerV4::ValidateCompanionRosterReady(
	UWorld* World,
	const FString& ExpectedCompanionSnapshotHash,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(World) || !World->IsGameWorld() || ExpectedCompanionSnapshotHash.IsEmpty())
	{
		OutError = TEXT("V4 companion-roster readiness received an invalid world or snapshot hash.");
		return false;
	}

	TArray<IEFCalystoPopulationBridgeV4*> Providers;
	const TArray<IEFCalystoPopulationBridgeV4*> Implementations =
		IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoPopulationBridgeV4>(
			IEFCalystoPopulationBridgeV4::GetModularFeatureName());
	for (IEFCalystoPopulationBridgeV4* Implementation : Implementations)
	{
		if (Implementation && Implementation->ProvidesCompanionRosterReadiness())
		{
			Providers.Add(Implementation);
		}
	}
	if (Providers.Num() != 1)
	{
		OutError = FString::Printf(
			TEXT("V4 requires exactly one companion-roster readiness provider; found %d."),
			Providers.Num());
		return false;
	}
	return Providers[0]->IsCompanionRosterReady(World, ExpectedCompanionSnapshotHash, OutError);
}

void FEFCalystoPopulationMaterializerV4::RollbackMaterializedPopulation(
	UWorld* World,
	const FEFCalystoResolvedFloorIntentV4& Intent)
{
	using namespace EFCalystoPopulationV4Private;
	if (!IsValid(World))
	{
		return;
	}

	TArray<const FEFCalystoSpawnInstanceDirectiveV4*> Directives;
	Directives.Reserve(Intent.SpawnDirectives.Num());
	for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
	{
		Directives.Add(&Directive);
	}
	Directives.Sort([](
		const FEFCalystoSpawnInstanceDirectiveV4& Left,
		const FEFCalystoSpawnInstanceDirectiveV4& Right)
	{
		// Reverse canonical order unwinds registrations in the opposite order from realization.
		return Right.StableInstanceId.LexicalLess(Left.StableInstanceId);
	});

	TSet<TObjectKey<AActor>> RolledBackActors;
	for (const FEFCalystoSpawnInstanceDirectiveV4* Directive : Directives)
	{
		if (!Directive)
		{
			continue;
		}
		const FName InstanceTag(*FString::Printf(
			TEXT("EF.Calysto.V4.Instance.%s"),
			*Directive->StableInstanceId.ToString()));
		FString BridgeError;
		IEFCalystoPopulationBridgeV4* Bridge = RequiresProjectBridge(Directive->Category)
			? ResolveUniqueBridge(Directive->Category, BridgeError)
			: nullptr;
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			AActor* Actor = *ActorIt;
			if (!IsValid(Actor) || RolledBackActors.Contains(TObjectKey<AActor>(Actor))
				|| !Actor->ActorHasTag(FName(TEXT("EF.Calysto.Population.V4")))
				|| !Actor->ActorHasTag(InstanceTag))
			{
				continue;
			}
			Actor->Tags.AddUnique(FName(TEXT("EF.Calysto.V4.Rollback")));
			if (Bridge)
			{
				Bridge->RollbackSpawnedActor(World, Actor, *Directive);
			}
			else if (RequiresProjectBridge(Directive->Category))
			{
				UE_LOG(
					LogEFCalystoPopulationV4,
					Error,
					TEXT("V4 rollback could not resolve the required %s bridge for %s: %s"),
					CategoryName(Directive->Category),
					*Directive->StableInstanceId.ToString(),
					*BridgeError);
			}
			RolledBackActors.Add(TObjectKey<AActor>(Actor));
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				Actor->Destroy();
			}
		}
	}

	// Corrupt/duplicate instance tags must not leave a Director actor behind.
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (IsValid(Actor) && Actor->ActorHasTag(FName(TEXT("EF.Calysto.Population.V4"))))
		{
			Actor->Tags.AddUnique(FName(TEXT("EF.Calysto.V4.Rollback")));
			Actor->Destroy();
		}
	}
}
