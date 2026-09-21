#include "Calysto/EFCalystoPopulationMaterializerV6.h"

#include "Calysto/EFCalystoPopulationAnchor.h"
#include "Calysto/EFCalystoPopulationBridgeV6.h"
#include "Calysto/EFCalystoPCGRuntimeGraphV6.h"
#include "Algo/Count.h"
#include "Data/PCGBasePointData.h"
#include "PCGData.h"
#include "PCGPoint.h"
#include "EFProceduralSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPopulationV6, Log, All);

namespace EFCalystoPopulationMaterializerV6Private
{
	static const FName PopulationActorTag(TEXT("EF.Calysto.Population"));
	static const FName ApplyingTag(TEXT("EF.Calysto.PopulationV6.Applying"));
	static const FString AppliedPlanTagPrefix(TEXT("EF.Calysto.PopulationV6.Plan."));
	static const FString MaterializationTagPrefix(TEXT("EF.Calysto.PopulationV6.Materialization."));
	static const FString DecisionTagPrefix(TEXT("EF.Calysto.Population.Decision."));
	static const FString RoomTagPrefix(TEXT("EF.Calysto.Population.Room."));

	struct FAnchorCandidate
	{
		TWeakObjectPtr<AEFCalystoPopulationAnchor> Source;
		int64 StableRoomId = 0;
		EEFCalystoPlacementZoneV6 Zone = EEFCalystoPlacementZoneV6::Floor;
		FString AnchorId;
		FVector NavigationLocation = FVector::ZeroVector;
		float SourceYaw = 0.0f;
		FTransform NativeTransform = FTransform::Identity;
	};

	struct FSpawnRequest
	{
		const FEFCalystoPopulationDecisionV6* Decision = nullptr;
		TArray<FEFCalystoPopulationDecisionV6> ChestContents;
	};

	struct FRealizedInternal
	{
		TWeakObjectPtr<AActor> Actor;
		const FEFCalystoPopulationDecisionV6* Decision = nullptr;
		IEFCalystoPopulationBridgeV6* Bridge = nullptr;
	};

	bool IsSha256(const FString& Value)
	{
		if (Value.Len() != 64)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (!FChar::IsHexDigit(Character))
			{
				return false;
			}
		}
		return true;
	}

	FString CanonicalToken(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ToLowerInline();
		return Value;
	}

	FString CanonicalPath(const FSoftObjectPath& Path)
	{
		return CanonicalToken(Path.ToString());
	}

	FString QuantizedTransformRecord(const FVector& Location, const float Yaw)
	{
		constexpr double QuantizationCm = 10.0;
		return FString::Printf(
			TEXT("%lld|%lld|%lld|%d"),
			FMath::RoundToInt64(Location.X / QuantizationCm),
			FMath::RoundToInt64(Location.Y / QuantizationCm),
			FMath::RoundToInt64(Location.Z / QuantizationCm),
			FMath::RoundToInt(FRotator::ClampAxis(Yaw)));
	}

	uint64 HashRank(const FString& Canonical)
	{
		const FString Hash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
		return Hash.Len() >= 16
			? FCString::Strtoui64(*Hash.Left(16), nullptr, 16)
			: MAX_uint64;
	}

	float SignedPlacementDraw(const FString& DecisionId, const FString& CandidateId, const TCHAR* Lane)
	{
		const uint64 Value = HashRank(FString::Printf(
			TEXT("EFCalystoPopulationJitterV6|%s|%s|%s"),
			*DecisionId, *CandidateId, Lane));
		return (static_cast<float>(Value % 20001ULL) / 10000.0f) - 1.0f;
	}

	FVector ResolveDeterministicJitter(
		const FEFCalystoPopulationDecisionV6& Decision,
		const FAnchorCandidate& Candidate,
		const FVector& SurfaceNormal)
	{
		const float Radius = FMath::Clamp(Decision.PositionJitterCm, 0.0f, 25.0f);
		if (Radius <= UE_SMALL_NUMBER)
		{
			return FVector::ZeroVector;
		}
		const float A = SignedPlacementDraw(Decision.DecisionId, Candidate.AnchorId, TEXT("A"));
		const float B = SignedPlacementDraw(Decision.DecisionId, Candidate.AnchorId, TEXT("B"));
		const FQuat Rotation = Candidate.NativeTransform.GetRotation().GetNormalized();
		if (Decision.PlacementZone == EEFCalystoPlacementZoneV6::Floor
			|| Decision.PlacementZone == EEFCalystoPlacementZoneV6::Roof)
		{
			return (Rotation.GetAxisX() * A + Rotation.GetAxisY() * B) * Radius;
		}
		const FVector Tangent = FVector::CrossProduct(FVector::UpVector, SurfaceNormal).GetSafeNormal();
		return (Tangent * A + FVector::UpVector * B) * Radius;
	}

	bool IsNonFloorPlacementFree(
		UWorld* World,
		const FVector& Location,
		const float Radius,
		const float MinimumSpacing,
		const TArray<FVector>& OccupiedLocations)
	{
		for (const FVector& Occupied : OccupiedLocations)
		{
			if (FVector::DistSquared(Location, Occupied)
				< FMath::Square(FMath::Max(20.0f, MinimumSpacing)))
			{
				return false;
			}
		}
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFCalystoPopulationV6SurfaceOverlap), false);
		return !World->OverlapAnyTestByObjectType(
			Location,
			FQuat::Identity,
			Objects,
			FCollisionShape::MakeSphere(FMath::Clamp(Radius * 0.25f, 5.0f, 20.0f)),
			QueryParams);
	}

	bool HasTagWithPrefix(const AActor* Actor, const FString& Prefix, FString* OutSuffix = nullptr)
	{
		if (!IsValid(Actor))
		{
			return false;
		}
		for (const FName Tag : Actor->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				if (OutSuffix)
				{
					*OutSuffix = Text.Mid(Prefix.Len());
				}
				return true;
			}
		}
		return false;
	}

	void RemoveTagsWithPrefix(AActor* Actor, const FString& Prefix)
	{
		if (!IsValid(Actor))
		{
			return;
		}
		Actor->Tags.RemoveAll([&Prefix](const FName Tag)
		{
			return Tag.ToString().StartsWith(Prefix, ESearchCase::CaseSensitive);
		});
	}

	int32 CategoryPriority(const FName CategoryId)
	{
		if (CategoryId.IsEqual(TEXT("Chest"), ENameCase::IgnoreCase)) return 0;
		if (CategoryId.IsEqual(TEXT("NPC"), ENameCase::IgnoreCase)) return 1;
		if (CategoryId.IsEqual(TEXT("Enemy"), ENameCase::IgnoreCase)) return 2;
		if (CategoryId.IsEqual(TEXT("SpecialEvent"), ENameCase::IgnoreCase)) return 3;
		if (CategoryId.IsEqual(TEXT("Food"), ENameCase::IgnoreCase)) return 4;
		if (CategoryId.IsEqual(TEXT("LooseLoot"), ENameCase::IgnoreCase)) return 5;
		if (CategoryId.IsEqual(TEXT("Clothing"), ENameCase::IgnoreCase)) return 6;
		return 7;
	}

	bool IsPawnCategory(const FName CategoryId)
	{
		return CategoryId.IsEqual(TEXT("Enemy"), ENameCase::IgnoreCase)
			|| CategoryId.IsEqual(TEXT("NPC"), ENameCase::IgnoreCase);
	}

	bool IsChestCategory(const FName CategoryId)
	{
		return CategoryId.IsEqual(TEXT("Chest"), ENameCase::IgnoreCase);
	}

	void ResolveSpawnShape(
		const FName CategoryId,
		UClass* ActorClass,
		float& OutRadius,
		float& OutHalfHeight,
		float& OutActorZOffset)
	{
		OutRadius = 30.0f;
		OutHalfHeight = 30.0f;
		OutActorZOffset = 2.0f;
		if (IsPawnCategory(CategoryId))
		{
			OutRadius = 42.0f;
			OutHalfHeight = 92.0f;
			if (const ACharacter* CharacterCDO = ActorClass
				? ActorClass->GetDefaultObject<ACharacter>()
				: nullptr)
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
		else if (IsChestCategory(CategoryId))
		{
			OutRadius = 65.0f;
			OutHalfHeight = 55.0f;
		}
	}

	bool IsSpawnLocationFree(
		UWorld* World,
		const FVector& NavigationLocation,
		const float Radius,
		const float HalfHeight,
		const float MinimumAnchorSpacing,
		const TArray<FVector>& OccupiedLocations)
	{
		const float MinimumSpacing = FMath::Max(Radius * 2.25f, MinimumAnchorSpacing);
		for (const FVector& Occupied : OccupiedLocations)
		{
			if (FVector::DistSquared2D(NavigationLocation, Occupied) < FMath::Square(MinimumSpacing))
			{
				return false;
			}
		}
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFCalystoPopulationV6Overlap), false);
		const FVector CollisionCenter = NavigationLocation + FVector(0.0f, 0.0f, HalfHeight + 3.0f);
		return !World->OverlapBlockingTestByChannel(
			CollisionCenter,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight),
			QueryParams);
	}

	const FEFCalystoRoomContextV6* FindManifestRoom(
		const FEFCalystoRoomManifestV6& Manifest,
		const int64 StableRoomId)
	{
		return Manifest.Rooms.FindByPredicate([StableRoomId](const FEFCalystoRoomContextV6& Room)
		{
			return Room.StableRoomId == StableRoomId;
		});
	}

	const FEFCalystoRoomPopulationPlanV6* FindPlanRoom(
		const FEFCalystoPopulationPlanV6& Plan,
		const int64 StableRoomId)
	{
		return Plan.Rooms.FindByPredicate([StableRoomId](const FEFCalystoRoomPopulationPlanV6& Room)
		{
			return Room.StableRoomId == StableRoomId;
		});
	}

	const FEFCalystoRoomContextV6* MatchRoomForLocalPoint(
		const FEFCalystoRoomManifestV6& Manifest,
		const TSet<int64>& RoomsNeedingAnchors,
		const FVector& LocalPoint,
		const float Tolerance)
	{
		const FEFCalystoRoomContextV6* BestRoom = nullptr;
		double BestScore = TNumericLimits<double>::Max();
		for (const FEFCalystoRoomContextV6& Room : Manifest.Rooms)
		{
			if (!RoomsNeedingAnchors.Contains(Room.StableRoomId)
				|| Room.LocalCenter.ContainsNaN() || Room.Extents.ContainsNaN())
			{
				continue;
			}
			const FVector Extents = Room.Extents.GetAbs();
			const FVector Delta = (LocalPoint - Room.LocalCenter).GetAbs();
			if (Delta.X > Extents.X + Tolerance
				|| Delta.Y > Extents.Y + Tolerance
				|| Delta.Z > Extents.Z + Tolerance)
			{
				continue;
			}
			const double Score =
				FMath::Square(Delta.X / FMath::Max(1.0, Extents.X))
				+ FMath::Square(Delta.Y / FMath::Max(1.0, Extents.Y))
				+ 0.25 * FMath::Square(Delta.Z / FMath::Max(1.0, Extents.Z));
			if (!BestRoom || Score < BestScore - UE_DOUBLE_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Score, BestScore, UE_DOUBLE_SMALL_NUMBER)
					&& Room.StableRoomId < BestRoom->StableRoomId))
			{
				BestRoom = &Room;
				BestScore = Score;
			}
		}
		return BestRoom;
	}

	void AddPopulationTags(
		AActor* Actor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& Decision)
	{
		if (!IsValid(Actor))
		{
			return;
		}
		if (const UEFProceduralSettings* Settings = UEFProceduralSettings::Get())
		{
			Actor->Tags.AddUnique(Settings->GeneratedActorTag);
		}
		Actor->Tags.AddUnique(PopulationActorTag);
		Actor->Tags.AddUnique(FName(*(AppliedPlanTagPrefix + Plan.PopulationHash)));
		Actor->Tags.AddUnique(FName(*(DecisionTagPrefix + Decision.DecisionId)));
		Actor->Tags.AddUnique(FName(*(RoomTagPrefix + FString::Printf(TEXT("%lld"), Decision.StableRoomId))));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.Population.Category.%s"), *Decision.CategoryId.ToString())));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.Population.Theme.%s"), *Decision.ThemeId.ToString())));
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("EF.Calysto.Population.Placement.%d"), static_cast<int32>(Decision.PlacementZone))));
	}

	bool EqualCanonicalIds(TArray<FString> Left, TArray<FString> Right)
	{
		for (FString& Item : Left) Item = CanonicalToken(MoveTemp(Item));
		for (FString& Item : Right) Item = CanonicalToken(MoveTemp(Item));
		Left.Sort();
		Right.Sort();
		return Left == Right;
	}

	void DestroyAnchors(TArray<TWeakObjectPtr<AEFCalystoPopulationAnchor>>& Anchors)
	{
		for (const TWeakObjectPtr<AEFCalystoPopulationAnchor>& Anchor : Anchors)
		{
			if (Anchor.IsValid() && !Anchor->IsActorBeingDestroyed())
			{
				Anchor->Destroy();
			}
		}
		Anchors.Reset();
	}

	void RollbackActors(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FRealizedInternal>& Realized)
	{
		for (int32 Index = Realized.Num() - 1; Index >= 0; --Index)
		{
			FRealizedInternal& Item = Realized[Index];
			AActor* Actor = Item.Actor.Get();
			if (!IsValid(Actor))
			{
				continue;
			}
			if (Item.Bridge && Item.Decision)
			{
				Item.Bridge->RollbackSpawnedActor(World, Actor, Plan, *Item.Decision);
			}
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				Actor->Destroy();
			}
		}
		Realized.Reset();
	}
}

bool FEFCalystoPopulationMaterializerV6::GatherRequiredPreloadPaths(
	const FEFCalystoPopulationPlanV6& Plan,
	TArray<FSoftObjectPath>& OutPaths,
	FString& OutError)
{
	using namespace EFCalystoPopulationMaterializerV6Private;
	OutPaths.Reset();
	OutError.Reset();
	if (!IsSha256(Plan.PopulationHash))
	{
		OutError = TEXT("Calysto V6 cannot gather a preload closure for an invalid population plan.");
		return false;
	}
	for (const FSoftObjectPath& Path : Plan.PreloadClassPaths)
	{
		if (!Path.IsValid())
		{
			OutError = TEXT("The Calysto V6 population plan contains an invalid selected class path.");
			return false;
		}
		OutPaths.AddUnique(Path);
	}

	TArray<FSoftObjectPath> AdditionalPaths;
	if (!IEFCalystoPopulationBridgeV6::GatherRegisteredAdditionalPreloadPaths(
			Plan, AdditionalPaths, OutError))
	{
		return false;
	}
	for (const FSoftObjectPath& Path : AdditionalPaths)
	{
		if (!Path.IsValid())
		{
			OutError = TEXT("A registered Calysto V6 gameplay bridge returned an invalid preload path.");
			OutPaths.Reset();
			return false;
		}
		OutPaths.AddUnique(Path);
	}
	OutPaths.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right)
	{
		return CanonicalPath(Left) < CanonicalPath(Right);
	});
	return true;
}

bool FEFCalystoPopulationMaterializerV6::ValidatePlan(
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationPlanV6& Plan,
	FString& OutError)
{
	using namespace EFCalystoPopulationMaterializerV6Private;
	OutError.Reset();
	if (Plan.FloorNumber < 1 || Plan.StyleId.IsNone()
		|| !IsSha256(Plan.FloorPlanHash)
		|| !IsSha256(Plan.RoomManifestHash)
		|| !IsSha256(Plan.PopulationHash))
	{
		OutError = TEXT("The Calysto V6 population plan has an incomplete frozen identity.");
		return false;
	}
	if (RoomManifest.FloorSeed != Plan.FloorSeed
		|| !RoomManifest.StyleId.IsEqual(Plan.StyleId, ENameCase::IgnoreCase)
		|| RoomManifest.FloorPlanHash != Plan.FloorPlanHash
		|| RoomManifest.ManifestHash != Plan.RoomManifestHash)
	{
		OutError = TEXT("The Calysto V6 population plan does not belong to the supplied room manifest.");
		return false;
	}
	if (Plan.Rooms.Num() != RoomManifest.Rooms.Num())
	{
		OutError = TEXT("The Calysto V6 population plan does not contain exactly one record per frozen room.");
		return false;
	}

	TMap<int64, const FEFCalystoRoomContextV6*> ManifestRooms;
	for (const FEFCalystoRoomContextV6& Room : RoomManifest.Rooms)
	{
		if (Room.StableRoomId == 0 || ManifestRooms.Contains(Room.StableRoomId))
		{
			OutError = TEXT("The Calysto V6 room manifest contains a zero or duplicate Stable Room ID.");
			return false;
		}
		ManifestRooms.Add(Room.StableRoomId, &Room);
	}

	TMap<FString, const FEFCalystoPopulationDecisionV6*> ActorByDecisionId;
	TSet<FString> AllDecisionIds;
	TSet<FString> SelectedClassPaths;
	int32 ActorCount = 0;
	int32 ContentCount = 0;
	int64 PreviousRoomId = MIN_int64;
	for (const FEFCalystoRoomPopulationPlanV6& RoomPlan : Plan.Rooms)
	{
		const FEFCalystoRoomContextV6* const* ManifestRoomPtr = ManifestRooms.Find(RoomPlan.StableRoomId);
		const FEFCalystoRoomContextV6* ManifestRoom = ManifestRoomPtr ? *ManifestRoomPtr : nullptr;
		if (!ManifestRoom || RoomPlan.StableRoomId <= PreviousRoomId
			|| RoomPlan.ThemeId != ManifestRoom->ThemeId
			|| RoomPlan.EffectiveCatalogHash != ManifestRoom->CatalogHash
			|| !IsSha256(RoomPlan.RoomPopulationHash))
		{
			OutError = FString::Printf(
				TEXT("Calysto V6 room population record %lld is missing, unordered, or differs from the frozen manifest."),
				RoomPlan.StableRoomId);
			return false;
		}
		PreviousRoomId = RoomPlan.StableRoomId;
		if (FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(ManifestRoom->RoomFlags)
			&& !RoomPlan.Decisions.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Protected Calysto room %lld contains a population decision."),
				RoomPlan.StableRoomId);
			return false;
		}

		for (const FEFCalystoPopulationDecisionV6& Decision : RoomPlan.Decisions)
		{
			const int32 PlacementValue = static_cast<int32>(Decision.PlacementZone);
			if (!IsSha256(Decision.DecisionId)
				|| AllDecisionIds.Contains(Decision.DecisionId)
				|| Decision.StableRoomId != RoomPlan.StableRoomId
				|| !Decision.StyleId.IsEqual(Plan.StyleId, ENameCase::IgnoreCase)
				|| Decision.ThemeId != RoomPlan.ThemeId
				|| Decision.CategoryId.IsNone()
				|| Decision.EntryId.IsNone()
				|| !Decision.ClassPath.IsValid()
				|| PlacementValue < static_cast<int32>(EEFCalystoPlacementZoneV6::Floor)
				|| PlacementValue > static_cast<int32>(EEFCalystoPlacementZoneV6::Roof)
				|| !FMath::IsFinite(Decision.PositionJitterCm)
				|| Decision.PositionJitterCm < 0.0f
				|| Decision.PositionJitterCm > 25.0f)
			{
				OutError = FString::Printf(
					TEXT("Calysto V6 population decision '%s' is incomplete, duplicated, or belongs to another room."),
					*Decision.DecisionId);
				return false;
			}
			AllDecisionIds.Add(Decision.DecisionId);
			SelectedClassPaths.Add(CanonicalPath(Decision.ClassPath));
			if (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor)
			{
				if (!Decision.ParentDecisionId.IsEmpty())
				{
					OutError = TEXT("A Calysto V6 actor decision unexpectedly has a parent decision.");
					return false;
				}
				ActorByDecisionId.Add(Decision.DecisionId, &Decision);
				++ActorCount;
			}
			else
			{
				if (!IsSha256(Decision.ParentDecisionId)
					|| Decision.PlacementZone != EEFCalystoPlacementZoneV6::Floor
					|| !FMath::IsNearlyZero(Decision.PositionJitterCm))
				{
					OutError = TEXT("A Calysto V6 chest-content decision lacks a valid parent or has world-placement state.");
					return false;
				}
				++ContentCount;
			}
		}
	}

	for (const FEFCalystoRoomPopulationPlanV6& RoomPlan : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : RoomPlan.Decisions)
		{
			if (Decision.Kind != EEFCalystoPopulationDecisionKindV6::ChestContent)
			{
				continue;
			}
			const FEFCalystoPopulationDecisionV6* const* ParentPtr =
				ActorByDecisionId.Find(Decision.ParentDecisionId);
			const FEFCalystoPopulationDecisionV6* Parent = ParentPtr ? *ParentPtr : nullptr;
			if (!Parent || Parent->StableRoomId != Decision.StableRoomId
				|| !IsChestCategory(Parent->CategoryId))
			{
				OutError = FString::Printf(
					TEXT("Calysto V6 chest content '%s' does not attach to a chest actor in the same Stable Room."),
					*Decision.DecisionId);
				return false;
			}
		}
	}

	if (ActorCount != Plan.ActorDecisionCount
		|| ContentCount != Plan.ChestContentDecisionCount)
	{
		OutError = FString::Printf(
			TEXT("Calysto V6 population counts drifted (actors=%d/%d contents=%d/%d)."),
			ActorCount, Plan.ActorDecisionCount, ContentCount, Plan.ChestContentDecisionCount);
		return false;
	}

	TSet<FString> PreloadPaths;
	for (const FSoftObjectPath& Path : Plan.PreloadClassPaths)
	{
		if (!Path.IsValid())
		{
			OutError = TEXT("The Calysto V6 selected class closure contains an invalid path.");
			return false;
		}
		PreloadPaths.Add(CanonicalPath(Path));
	}
	for (const FString& SelectedPath : SelectedClassPaths)
	{
		if (!PreloadPaths.Contains(SelectedPath))
		{
			OutError = TEXT("The Calysto V6 selected class closure omits one or more frozen decisions.");
			return false;
		}
	}
	return true;
}

bool FEFCalystoPopulationMaterializerV6::ValidatePreloadClosureResident(
	const FEFCalystoPopulationPlanV6& Plan,
	FString& OutError)
{
	using namespace EFCalystoPopulationMaterializerV6Private;
	OutError.Reset();
	TArray<FSoftObjectPath> RequiredPaths;
	if (!GatherRequiredPreloadPaths(Plan, RequiredPaths, OutError))
	{
		return false;
	}
	for (const FSoftObjectPath& Path : RequiredPaths)
	{
		if (!IsValid(Path.ResolveObject()))
		{
			OutError = FString::Printf(
				TEXT("Calysto V6 required asset is not resident after asynchronous preload: %s"),
				*Path.ToString());
			return false;
		}
	}
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			UClass* ResolvedClass = Cast<UClass>(Decision.ClassPath.ResolveObject());
			if (!IsValid(ResolvedClass)
				|| (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor
					&& (!ResolvedClass->IsChildOf(AActor::StaticClass())
						|| ResolvedClass->HasAnyClassFlags(CLASS_Abstract)))
				|| (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor
					&& IsPawnCategory(Decision.CategoryId)
					&& !ResolvedClass->IsChildOf(APawn::StaticClass())))
			{
				OutError = FString::Printf(
					TEXT("Calysto V6 decision class is unavailable or incompatible after preload: %s"),
					*Decision.ClassPath.ToString());
				return false;
			}
		}
	}
	return true;
}

bool FEFCalystoPopulationMaterializerV6::GatherNativePlacementCandidates(
	const FPCGDataCollection& Output, const FTransform& DungeonTransform,
	const FEFCalystoRoomManifestV6& Manifest, const FEFCalystoPopulationPlanV6& Plan,
	TArray<FEFCalystoPlacementCandidateV6>& OutCandidates, FString& OutError)
{
	using namespace EFCalystoPopulationMaterializerV6Private;
	OutCandidates.Reset();
	OutError.Reset();
	TMap<EEFCalystoPlacementZoneV6, TSet<int64>> Requested;
	TSet<int64> AllRooms;
	for (const FEFCalystoRoomContextV6& Room : Manifest.Rooms) AllRooms.Add(Room.StableRoomId);
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor)
				Requested.FindOrAdd(Decision.PlacementZone).Add(Decision.StableRoomId);
		}
	}
	if (Requested.IsEmpty()) return true;
	const TMap<FName, EEFCalystoPlacementZoneV6> Zones = {
		{FEFCalystoPlacementCandidatePinsV6::Floor, EEFCalystoPlacementZoneV6::Floor},
		{FEFCalystoPlacementCandidatePinsV6::WallBottom, EEFCalystoPlacementZoneV6::WallBottom},
		{FEFCalystoPlacementCandidatePinsV6::WallMiddle, EEFCalystoPlacementZoneV6::WallMiddle},
		{FEFCalystoPlacementCandidatePinsV6::WallTop, EEFCalystoPlacementZoneV6::WallTop},
		{FEFCalystoPlacementCandidatePinsV6::CornerBottom, EEFCalystoPlacementZoneV6::CornerBottom},
		{FEFCalystoPlacementCandidatePinsV6::CornerMiddle, EEFCalystoPlacementZoneV6::CornerMiddle},
		{FEFCalystoPlacementCandidatePinsV6::CornerTop, EEFCalystoPlacementZoneV6::CornerTop},
		{FEFCalystoPlacementCandidatePinsV6::Roof, EEFCalystoPlacementZoneV6::Roof}};
	TSet<FString> Seen;
	for (const FPCGTaggedData& Tagged : Output.TaggedData)
	{
		const EEFCalystoPlacementZoneV6* Zone = Zones.Find(Tagged.Pin);
		if (!Zone || !Requested.Contains(*Zone)) continue;
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
		if (!Points) { OutError = TEXT("A native placement output is not point data."); return false; }
		const FConstPCGPointValueRanges Ranges(Points);
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FPCGPoint& Point = Ranges.GetPoint(Index);
			if (!Point.Transform.IsValid() || Point.Transform.ContainsNaN())
			{ OutError = TEXT("A native placement point has an invalid transform."); return false; }
			const FVector Local = DungeonTransform.InverseTransformPosition(Point.Transform.GetLocation());
			// Match against every room first: filtering before ownership would leak
			// neighboring protected/unthemed points into the requesting Theme.
			const FEFCalystoRoomContextV6* Room = MatchRoomForLocalPoint(Manifest, AllRooms, Local, 75.0f);
			if (!Room || !Requested.FindChecked(*Zone).Contains(Room->StableRoomId)) continue;
			FEFCalystoPlacementCandidateV6 Candidate;
			Candidate.StableRoomId = Room->StableRoomId;
			Candidate.Zone = *Zone;
			Candidate.Transform = Point.Transform;
			Candidate.Transform.SetScale3D(FVector::OneVector);
			Candidate.CandidateId = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(FString::Printf(
				TEXT("EFNativePlacementV6|%s|%lld|%d|%s|%s"), *Plan.FloorPlanHash,
				Room->StableRoomId, static_cast<int32>(*Zone),
				*QuantizedTransformRecord(Local, Point.Transform.Rotator().Yaw),
				*Point.Transform.GetRotation().ToString()));
			if (Seen.Contains(Candidate.CandidateId)) continue;
			Seen.Add(Candidate.CandidateId);
			OutCandidates.Add(MoveTemp(Candidate));
			if (OutCandidates.Num() > 4096)
			{ OutError = TEXT("Native placement candidates exceed the 4096-record safety ceiling."); OutCandidates.Reset(); return false; }
		}
	}
	OutCandidates.Sort([](const FEFCalystoPlacementCandidateV6& A, const FEFCalystoPlacementCandidateV6& B)
	{ return A.CandidateId < B.CandidateId; });
	return true;
}

FEFCalystoPopulationMaterializationResultV6 FEFCalystoPopulationMaterializerV6::Materialize(
	UWorld* World,
	AActor* DungeonActor,
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationPlanV6& Plan,
	const FEFCalystoPopulationMaterializationOptionsV6& Options)
{
	return Materialize(
		World,
		DungeonActor,
		RoomManifest,
		Plan,
		TConstArrayView<FEFCalystoPlacementCandidateV6>(),
		Options);
}

FEFCalystoPopulationMaterializationResultV6 FEFCalystoPopulationMaterializerV6::Materialize(
	UWorld* World,
	AActor* DungeonActor,
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationPlanV6& Plan,
	TConstArrayView<FEFCalystoPlacementCandidateV6> PlacementCandidates,
	const FEFCalystoPopulationMaterializationOptionsV6& Options)
{
	using namespace EFCalystoPopulationMaterializerV6Private;
	FEFCalystoPopulationMaterializationResultV6 Result;
	Result.PopulationHash = Plan.PopulationHash;
	auto Fail = [&Result](FString Reason)
	{
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};

	if (!IsValid(World) || !World->IsGameWorld() || !IsValid(DungeonActor)
		|| DungeonActor->GetWorld() != World)
	{
		return Fail(TEXT("Calysto V6 population requires one valid game world and dungeon actor."));
	}
	if (!FMath::IsFinite(Options.RoomBoundsTolerance) || Options.RoomBoundsTolerance < 0.0f
		|| !FMath::IsFinite(Options.MinimumAnchorSpacing) || Options.MinimumAnchorSpacing < 0.0f
		|| Options.NavigationProjectionExtent.ContainsNaN()
		|| Options.NavigationProjectionExtent.GetMin() <= 0.0f
		|| Options.MaximumAnchorRecords < 1)
	{
		return Fail(TEXT("Calysto V6 population materialization options are invalid."));
	}

	FString ValidationError;
	if (!ValidatePlan(RoomManifest, Plan, ValidationError))
	{
		return Fail(MoveTemp(ValidationError));
	}
	if (!ValidatePreloadClosureResident(Plan, ValidationError))
	{
		return Fail(MoveTemp(ValidationError));
	}

	const FString AppliedPlanTag = AppliedPlanTagPrefix + Plan.PopulationHash;
	if (DungeonActor->ActorHasTag(FName(*AppliedPlanTag)))
	{
		return Fail(
			TEXT("Calysto V6 rejected duplicate population materialization because immutable realized actor evidence cannot be reconstructed from summary tags."));
	}
	FString ExistingPlanHash;
	if (DungeonActor->ActorHasTag(ApplyingTag)
		|| HasTagWithPrefix(DungeonActor, AppliedPlanTagPrefix, &ExistingPlanHash))
	{
		return Fail(FString::Printf(
			TEXT("Calysto V6 population was already started or applied with another plan (%s)."),
			*ExistingPlanHash));
	}
	DungeonActor->Tags.AddUnique(ApplyingTag);

	TArray<TWeakObjectPtr<AEFCalystoPopulationAnchor>> AnchorActors;
	for (TActorIterator<AEFCalystoPopulationAnchor> AnchorIt(World); AnchorIt; ++AnchorIt)
	{
		if (IsValid(*AnchorIt))
		{
			AnchorActors.Add(*AnchorIt);
		}
	}
	Result.CandidateAnchorCount = AnchorActors.Num() + PlacementCandidates.Num();
	if (Result.CandidateAnchorCount > Options.MaximumAnchorRecords)
	{
		DungeonActor->Tags.Remove(ApplyingTag);
		DestroyAnchors(AnchorActors);
		return Fail(FString::Printf(
			TEXT("Calysto V6 received %d placement candidates, exceeding the %d-record ceiling."),
			Result.CandidateAnchorCount, Options.MaximumAnchorRecords));
	}

	auto FailAfterStart = [&Result, World, DungeonActor, &Plan, &AnchorActors](
		FString Reason,
		TArray<FRealizedInternal>& Realized,
		TArray<IEFCalystoPopulationBridgeV6*>& CommittedBridges)
	{
		for (int32 Index = CommittedBridges.Num() - 1; Index >= 0; --Index)
		{
			CommittedBridges[Index]->RollbackPopulationPlan(World, Plan);
		}
		RollbackActors(World, Plan, Realized);
		DungeonActor->Tags.Remove(ApplyingTag);
		DestroyAnchors(AnchorActors);
		Result.RealizedActors.Reset();
		Result.CandidateAnchorCount = 0;
		Result.SpawnedActorCount = 0;
		Result.VerifiedChestContentCount = 0;
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};

	TArray<FRealizedInternal> RealizedInternal;
	TArray<IEFCalystoPopulationBridgeV6*> CommittedBridges;
	TSet<int64> RoomsNeedingAnchors;
	TArray<FSpawnRequest> Requests;
	TMap<FString, TArray<FEFCalystoPopulationDecisionV6>> ContentsByParent;
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind == EEFCalystoPopulationDecisionKindV6::ChestContent)
			{
				ContentsByParent.FindOrAdd(Decision.ParentDecisionId).Add(Decision);
			}
		}
	}
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind != EEFCalystoPopulationDecisionKindV6::Actor)
			{
				continue;
			}
			FSpawnRequest& Request = Requests.AddDefaulted_GetRef();
			Request.Decision = &Decision;
			if (const TArray<FEFCalystoPopulationDecisionV6>* Contents =
				ContentsByParent.Find(Decision.DecisionId))
			{
				Request.ChestContents = *Contents;
				Request.ChestContents.Sort([](
					const FEFCalystoPopulationDecisionV6& Left,
					const FEFCalystoPopulationDecisionV6& Right)
				{
					return Left.DecisionId < Right.DecisionId;
				});
			}
			RoomsNeedingAnchors.Add(Decision.StableRoomId);
		}
	}
	Requests.Sort([](const FSpawnRequest& Left, const FSpawnRequest& Right)
	{
		if (Left.Decision->StableRoomId != Right.Decision->StableRoomId)
		{
			return Left.Decision->StableRoomId < Right.Decision->StableRoomId;
		}
		const int32 LeftPriority = CategoryPriority(Left.Decision->CategoryId);
		const int32 RightPriority = CategoryPriority(Right.Decision->CategoryId);
		return LeftPriority == RightPriority
			? Left.Decision->DecisionId < Right.Decision->DecisionId
			: LeftPriority < RightPriority;
	});

	const bool bRequiresFloorNavigation = Requests.ContainsByPredicate([](const FSpawnRequest& Request)
	{
		return Request.Decision
			&& Request.Decision->PlacementZone == EEFCalystoPlacementZoneV6::Floor;
	});
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem && bRequiresFloorNavigation)
	{
		return FailAfterStart(
			TEXT("Runtime navigation is unavailable for Calysto V6 population materialization."),
			RealizedInternal, CommittedBridges);
	}

	TMap<int64, TArray<FAnchorCandidate>> CandidatesByRoom;
	TSet<FString> AnchorIds;
	TSet<int64> AllRoomIds;
	for (const FEFCalystoRoomContextV6& Room : RoomManifest.Rooms)
	{
		AllRoomIds.Add(Room.StableRoomId);
	}
	const FTransform DungeonTransform = DungeonActor->GetActorTransform();
	for (const TWeakObjectPtr<AEFCalystoPopulationAnchor>& AnchorPtr : AnchorActors)
	{
		const AEFCalystoPopulationAnchor* Anchor = AnchorPtr.Get();
		if (!Anchor || !NavigationSystem || !bRequiresFloorNavigation)
		{
			continue;
		}
		const FVector LocalPoint = DungeonTransform.InverseTransformPosition(Anchor->GetActorLocation());
		const FEFCalystoRoomContextV6* Room = MatchRoomForLocalPoint(
			RoomManifest, RoomsNeedingAnchors, LocalPoint, Options.RoomBoundsTolerance);
		if (!Room)
		{
			continue;
		}
		FNavLocation Projected;
		if (!NavigationSystem->ProjectPointToNavigation(
				Anchor->GetActorLocation(), Projected, Options.NavigationProjectionExtent))
		{
			continue;
		}
		const FVector ProjectedLocal = DungeonTransform.InverseTransformPosition(Projected.Location);
		const FEFCalystoRoomContextV6* ProjectedRoom = MatchRoomForLocalPoint(
			RoomManifest, RoomsNeedingAnchors, ProjectedLocal, Options.RoomBoundsTolerance * 2.0f);
		if (!ProjectedRoom || ProjectedRoom->StableRoomId != Room->StableRoomId)
		{
			continue;
		}

		FAnchorCandidate Candidate;
		Candidate.Source = AnchorPtr;
		Candidate.StableRoomId = Room->StableRoomId;
		Candidate.Zone = EEFCalystoPlacementZoneV6::Floor;
		Candidate.NavigationLocation = Projected.Location;
		Candidate.SourceYaw = FRotator::ClampAxis(Anchor->GetActorRotation().Yaw);
		Candidate.NativeTransform = FTransform(
			FRotator(0.0f, Candidate.SourceYaw, 0.0f), Candidate.NavigationLocation);
		Candidate.AnchorId = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(FString::Printf(
			TEXT("EFCalystoPopulationAnchorV6|%s|%lld|%s"),
			*Plan.FloorPlanHash,
			Candidate.StableRoomId,
			*QuantizedTransformRecord(Candidate.NavigationLocation, Candidate.SourceYaw)));
		if (!IsSha256(Candidate.AnchorId) || AnchorIds.Contains(Candidate.AnchorId))
		{
			return FailAfterStart(
				TEXT("Calysto V6 produced an invalid or duplicate room-matched population anchor."),
				RealizedInternal, CommittedBridges);
		}
		AnchorIds.Add(Candidate.AnchorId);
		CandidatesByRoom.FindOrAdd(Candidate.StableRoomId).Add(MoveTemp(Candidate));
	}
	for (const FEFCalystoPlacementCandidateV6& SourceCandidate : PlacementCandidates)
	{
		// Native Floor points supplement the sparse PCG anchor actors. A room
		// can have valid walkable floor without receiving any sampled anchor.
		if (!RoomsNeedingAnchors.Contains(SourceCandidate.StableRoomId))
		{
			continue;
		}
		const int32 ZoneValue = static_cast<int32>(SourceCandidate.Zone);
		if (ZoneValue < static_cast<int32>(EEFCalystoPlacementZoneV6::Floor)
			|| ZoneValue > static_cast<int32>(EEFCalystoPlacementZoneV6::Roof)
			|| SourceCandidate.StableRoomId <= 0
			|| !RoomsNeedingAnchors.Contains(SourceCandidate.StableRoomId)
			|| !SourceCandidate.Transform.IsValid()
			|| SourceCandidate.Transform.ContainsNaN()
			|| !IsSha256(SourceCandidate.CandidateId)
			|| AnchorIds.Contains(SourceCandidate.CandidateId))
		{
			return FailAfterStart(
				TEXT("Calysto V6 received an invalid, duplicate, or unowned native placement candidate."),
				RealizedInternal, CommittedBridges);
		}
		FAnchorCandidate Candidate;
		Candidate.StableRoomId = SourceCandidate.StableRoomId;
		Candidate.Zone = SourceCandidate.Zone;
		Candidate.AnchorId = SourceCandidate.CandidateId;
		Candidate.NavigationLocation = SourceCandidate.Transform.GetLocation();
		Candidate.SourceYaw = FRotator::ClampAxis(SourceCandidate.Transform.Rotator().Yaw);
		Candidate.NativeTransform = SourceCandidate.Transform;
		if (Candidate.Zone == EEFCalystoPlacementZoneV6::Floor)
		{
			FNavLocation Projected;
			if (!NavigationSystem || !NavigationSystem->ProjectPointToNavigation(
				Candidate.NavigationLocation, Projected, Options.NavigationProjectionExtent))
			{
				continue;
			}
			const FEFCalystoRoomContextV6* ProjectedRoom = MatchRoomForLocalPoint(
				RoomManifest, AllRoomIds,
				DungeonTransform.InverseTransformPosition(Projected.Location), Options.RoomBoundsTolerance);
			if (!ProjectedRoom || ProjectedRoom->StableRoomId != Candidate.StableRoomId)
			{
				continue;
			}
			Candidate.NavigationLocation = Projected.Location;
			Candidate.NativeTransform.SetLocation(Projected.Location);
		}
		AnchorIds.Add(Candidate.AnchorId);
		CandidatesByRoom.FindOrAdd(Candidate.StableRoomId).Add(MoveTemp(Candidate));
	}
	for (TPair<int64, TArray<FAnchorCandidate>>& Pair : CandidatesByRoom)
	{
		Pair.Value.Sort([](const FAnchorCandidate& Left, const FAnchorCandidate& Right)
		{
			return Left.AnchorId < Right.AnchorId;
		});
	}
	for (const int64 RoomId : RoomsNeedingAnchors)
	{
		for (int32 ZoneValue = static_cast<int32>(EEFCalystoPlacementZoneV6::Floor);
			ZoneValue <= static_cast<int32>(EEFCalystoPlacementZoneV6::Roof); ++ZoneValue)
		{
			const EEFCalystoPlacementZoneV6 Zone = static_cast<EEFCalystoPlacementZoneV6>(ZoneValue);
			const int32 Required = Algo::CountIf(Requests, [RoomId, Zone](const FSpawnRequest& Request)
			{
				return Request.Decision && Request.Decision->StableRoomId == RoomId
					&& Request.Decision->PlacementZone == Zone;
			});
			const int32 Available = Algo::CountIf(CandidatesByRoom.FindRef(RoomId), [Zone](const FAnchorCandidate& Candidate)
			{
				return Candidate.Zone == Zone;
			});
			if (Available < Required)
			{
				return FailAfterStart(FString::Printf(
					TEXT("Stable Room %lld has %d candidates in placement zone %d for %d frozen actor decisions."),
					RoomId, Available, ZoneValue, Required), RealizedInternal, CommittedBridges);
			}
		}
	}

	TSet<FString> UsedAnchorIds;
	TArray<FVector> OccupiedLocations;
	TArray<FString> MaterializationRecords;
	TArray<IEFCalystoPopulationBridgeV6*> UsedBridges;
	for (const FSpawnRequest& Request : Requests)
	{
		const FEFCalystoPopulationDecisionV6& Decision = *Request.Decision;
		UClass* ActorClass = Cast<UClass>(Decision.ClassPath.ResolveObject());
		if (!IsValid(ActorClass))
		{
			return FailAfterStart(FString::Printf(
				TEXT("The asynchronously preloaded actor class became unavailable: %s"),
				*Decision.ClassPath.ToString()), RealizedInternal, CommittedBridges);
		}
		if (ActorClass->IsChildOf(APawn::StaticClass()) && Decision.PlacementZone != EEFCalystoPlacementZoneV6::Floor)
		{
			return FailAfterStart(TEXT("Pawn catalog entries require Floor placement for navigation safety."), RealizedInternal, CommittedBridges);
		}

		IEFCalystoPopulationBridgeV6* Bridge = nullptr;
		if (IEFCalystoPopulationBridgeV6::CategoryRequiresBridge(Decision.CategoryId))
		{
			FString BridgeError;
			Bridge = IEFCalystoPopulationBridgeV6::ResolveUniqueBridge(
				Decision.CategoryId, BridgeError);
			if (!Bridge)
			{
				return FailAfterStart(MoveTemp(BridgeError), RealizedInternal, CommittedBridges);
			}
			UsedBridges.AddUnique(Bridge);
		}

		float CollisionRadius = 0.0f;
		float CollisionHalfHeight = 0.0f;
		float ActorZOffset = 0.0f;
		ResolveSpawnShape(
			Decision.CategoryId, ActorClass,
			CollisionRadius, CollisionHalfHeight, ActorZOffset);
		TArray<TPair<uint64, const FAnchorCandidate*>> RankedCandidates;
		for (const FAnchorCandidate& Candidate : CandidatesByRoom.FindChecked(Decision.StableRoomId))
		{
			if (Candidate.Zone == Decision.PlacementZone
				&& !UsedAnchorIds.Contains(Candidate.AnchorId))
			{
				RankedCandidates.Emplace(HashRank(FString::Printf(
					TEXT("EFCalystoPopulationPlacementV6|%s|%s|%s"),
					*Plan.PopulationHash, *Decision.DecisionId, *Candidate.AnchorId)), &Candidate);
			}
		}
		RankedCandidates.Sort([](
			const TPair<uint64, const FAnchorCandidate*>& Left,
			const TPair<uint64, const FAnchorCandidate*>& Right)
		{
			return Left.Key == Right.Key
				? Left.Value->AnchorId < Right.Value->AnchorId
				: Left.Key < Right.Key;
		});

		AActor* SpawnedActor = nullptr;
		const FAnchorCandidate* SelectedCandidate = nullptr;
		TArray<FString> VerifiedContents;
		for (const TPair<uint64, const FAnchorCandidate*>& Ranked : RankedCandidates)
		{
			const FAnchorCandidate& Candidate = *Ranked.Value;
			const bool bFloorPlacement = Decision.PlacementZone == EEFCalystoPlacementZoneV6::Floor;
			FVector SurfaceNormal = FVector::UpVector;
			FVector SurfaceLocation = Candidate.NavigationLocation;
			if (!bFloorPlacement)
			{
				const FEFCalystoRoomContextV6* Room = RoomManifest.Rooms.FindByPredicate(
					[&Decision](const FEFCalystoRoomContextV6& Value) { return Value.StableRoomId == Decision.StableRoomId; });
				if (!Room) continue;
				FVector LocalNormal = -FVector::UpVector;
				if (Decision.PlacementZone != EEFCalystoPlacementZoneV6::Roof)
				{
					const FVector Delta = DungeonTransform.InverseTransformPosition(SurfaceLocation) - Room->LocalCenter;
					const FVector EdgeDistance = Room->Extents.GetAbs() - Delta.GetAbs();
					LocalNormal = EdgeDistance.X < EdgeDistance.Y
						? FVector(Delta.X > 0.0 ? -1.0 : 1.0, 0.0, 0.0)
						: FVector(0.0, Delta.Y > 0.0 ? -1.0 : 1.0, 0.0);
				}
				SurfaceNormal = DungeonTransform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(EFCalystoNativePlacementSurface), false);
				FCollisionObjectQueryParams Objects;
				Objects.AddObjectTypesToQuery(ECC_WorldStatic);
				if (!World->LineTraceSingleByObjectType(Hit, SurfaceLocation + SurfaceNormal * 100.0,
					SurfaceLocation - SurfaceNormal * 150.0, Objects, Params)
					|| FVector::DotProduct(Hit.ImpactNormal, SurfaceNormal) < 0.7) continue;
				SurfaceNormal = Hit.ImpactNormal;
				SurfaceLocation = Hit.ImpactPoint + SurfaceNormal * 2.0;
			}
			const FVector Jitter = ResolveDeterministicJitter(Decision, Candidate, SurfaceNormal);
			const FVector PlacementLocation = SurfaceLocation + Jitter;
			const bool bPlacementFree = bFloorPlacement
				? IsSpawnLocationFree(
					World,
					PlacementLocation,
					CollisionRadius,
					CollisionHalfHeight,
					Options.MinimumAnchorSpacing,
					OccupiedLocations)
				: IsNonFloorPlacementFree(
					World,
					PlacementLocation,
					CollisionRadius,
					Options.MinimumAnchorSpacing,
					OccupiedLocations);
			if (!bPlacementFree)
			{
				continue;
			}
			FTransform SpawnTransform = Candidate.NativeTransform;
			if (bFloorPlacement)
			{
				const uint64 YawHash = HashRank(FString::Printf(
					TEXT("EFCalystoPopulationYawV6|%s|%s|%s"),
					*Plan.PopulationHash, *Decision.DecisionId, *Candidate.AnchorId));
				const float Yaw = static_cast<float>(YawHash % 36000ULL) / 100.0f;
				SpawnTransform = FTransform(
					FRotator(0.0f, Yaw, 0.0f),
					PlacementLocation + FVector(0.0f, 0.0f, ActorZOffset));
			}
			else
			{
				SpawnTransform.SetLocation(PlacementLocation);
			}
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
			SpawnedActor->SetFlags(RF_Transient);
			AddPopulationTags(SpawnedActor, Plan, Decision);
			if (Bridge)
			{
				FString BridgeError;
				if (!Bridge->PrepareDeferredActor(
						World, SpawnedActor, Plan, Decision,
						MakeArrayView(Request.ChestContents), BridgeError))
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Plan, Decision);
					if (IsValid(SpawnedActor) && !SpawnedActor->IsActorBeingDestroyed())
					{
						SpawnedActor->Destroy();
					}
					return FailAfterStart(FString::Printf(
						TEXT("Calysto V6 bridge preparation failed for %s: %s"),
						*Decision.DecisionId, *BridgeError), RealizedInternal, CommittedBridges);
				}
			}

			SpawnedActor->FinishSpawning(SpawnTransform);
			if (!IsValid(SpawnedActor) || SpawnedActor->IsActorBeingDestroyed())
			{
				if (Bridge && SpawnedActor)
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Plan, Decision);
				}
				SpawnedActor = nullptr;
				continue;
			}
			if (Bridge)
			{
				FString BridgeError;
				if (!Bridge->FinalizeSpawnedActor(
						World, SpawnedActor, Plan, Decision,
						MakeArrayView(Request.ChestContents), VerifiedContents, BridgeError))
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Plan, Decision);
					if (IsValid(SpawnedActor) && !SpawnedActor->IsActorBeingDestroyed())
					{
						SpawnedActor->Destroy();
					}
					return FailAfterStart(FString::Printf(
						TEXT("Calysto V6 bridge finalization failed for %s: %s"),
						*Decision.DecisionId, *BridgeError), RealizedInternal, CommittedBridges);
				}
			}

			TArray<FString> ExpectedContentIds;
			for (const FEFCalystoPopulationDecisionV6& Content : Request.ChestContents)
			{
				ExpectedContentIds.Add(Content.DecisionId);
			}
			if (!EqualCanonicalIds(ExpectedContentIds, VerifiedContents))
			{
				if (Bridge)
				{
					Bridge->RollbackSpawnedActor(World, SpawnedActor, Plan, Decision);
				}
				if (IsValid(SpawnedActor) && !SpawnedActor->IsActorBeingDestroyed())
				{
					SpawnedActor->Destroy();
				}
				return FailAfterStart(FString::Printf(
					TEXT("Verified chest-content decisions differ from frozen parent %s."),
					*Decision.DecisionId), RealizedInternal, CommittedBridges);
			}

			SelectedCandidate = &Candidate;
			FEFCalystoRealizedPopulationActorV6& PublicRecord =
				Result.RealizedActors.AddDefaulted_GetRef();
			PublicRecord.DecisionId = Decision.DecisionId;
			PublicRecord.StableRoomId = Decision.StableRoomId;
			PublicRecord.CategoryId = Decision.CategoryId;
			PublicRecord.AnchorId = Candidate.AnchorId;
			PublicRecord.Transform = SpawnedActor->GetActorTransform();
			PublicRecord.Actor = SpawnedActor;
			PublicRecord.VerifiedChestContentDecisionIds = VerifiedContents;
			FRealizedInternal& InternalRecord = RealizedInternal.AddDefaulted_GetRef();
			InternalRecord.Actor = SpawnedActor;
			InternalRecord.Decision = &Decision;
			InternalRecord.Bridge = Bridge;
			break;
		}

		if (!IsValid(SpawnedActor) || !SelectedCandidate)
		{
			return FailAfterStart(FString::Printf(
				TEXT("No collision-free room-matched anchor remained for decision %s in Stable Room %lld."),
				*Decision.DecisionId, Decision.StableRoomId), RealizedInternal, CommittedBridges);
		}
		UsedAnchorIds.Add(SelectedCandidate->AnchorId);
		OccupiedLocations.Add(SpawnedActor->GetActorLocation());
		Result.VerifiedChestContentCount += VerifiedContents.Num();
		MaterializationRecords.Add(FString::Printf(
			TEXT("%s|%lld|%s|%s|%s|%s"),
			*Decision.DecisionId,
			Decision.StableRoomId,
			*SelectedCandidate->AnchorId,
			*CanonicalPath(Decision.ClassPath),
			*QuantizedTransformRecord(SpawnedActor->GetActorLocation(), SpawnedActor->GetActorRotation().Yaw),
			*FString::Join(VerifiedContents, TEXT(","))));
	}

	for (IEFCalystoPopulationBridgeV6* Bridge : UsedBridges)
	{
		FString CommitError;
		if (!Bridge->CommitPopulationPlan(World, Plan, CommitError))
		{
			return FailAfterStart(FString::Printf(
				TEXT("Calysto V6 gameplay bridge rejected the global plan commit: %s"),
				*CommitError), RealizedInternal, CommittedBridges);
		}
		CommittedBridges.Add(Bridge);
	}

	if (RealizedInternal.Num() != Plan.ActorDecisionCount
		|| Result.VerifiedChestContentCount != Plan.ChestContentDecisionCount)
	{
		return FailAfterStart(TEXT("Calysto V6 did not realize every frozen actor and chest-content decision."),
			RealizedInternal, CommittedBridges);
	}
	MaterializationRecords.Sort();
	Result.MaterializationHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
		TEXT("EFCalystoPopulationMaterializationV6\n")
		+ Plan.PopulationHash + TEXT("\n")
		+ (MaterializationRecords.IsEmpty()
			? FString(TEXT("EMPTY"))
			: FString::Join(MaterializationRecords, TEXT("\n"))));
	if (!IsSha256(Result.MaterializationHash))
	{
		return FailAfterStart(TEXT("Calysto V6 could not hash the verified materialization."),
			RealizedInternal, CommittedBridges);
	}

	DungeonActor->Tags.Remove(ApplyingTag);
	DungeonActor->Tags.AddUnique(FName(*AppliedPlanTag));
	DungeonActor->Tags.AddUnique(FName(*(MaterializationTagPrefix + Result.MaterializationHash)));
	DestroyAnchors(AnchorActors);
	Result.bSucceeded = true;
	Result.SpawnedActorCount = RealizedInternal.Num();
	UE_LOG(
		LogEFCalystoPopulationV6,
		Log,
		TEXT("CALYSTO_V6_POPULATION_REALIZED plan=%s materialization=%s actors=%d chest_contents=%d rooms=%d."),
		*Plan.PopulationHash,
		*Result.MaterializationHash,
		Result.SpawnedActorCount,
		Result.VerifiedChestContentCount,
		Plan.Rooms.Num());
	return Result;
}

void FEFCalystoPopulationMaterializerV6::RollbackMaterializedPopulation(
	UWorld* World,
	AActor* DungeonActor,
	const FEFCalystoPopulationPlanV6& Plan)
{
	using namespace EFCalystoPopulationMaterializerV6Private;
	if (!IsValid(World) || !IsValid(DungeonActor))
	{
		return;
	}

	TMap<FString, const FEFCalystoPopulationDecisionV6*> Decisions;
	TArray<IEFCalystoPopulationBridgeV6*> UsedBridges;
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor)
			{
				Decisions.Add(Decision.DecisionId, &Decision);
				if (IEFCalystoPopulationBridgeV6::CategoryRequiresBridge(Decision.CategoryId))
				{
					FString ResolveError;
					if (IEFCalystoPopulationBridgeV6* Bridge =
						IEFCalystoPopulationBridgeV6::ResolveUniqueBridge(Decision.CategoryId, ResolveError))
					{
						UsedBridges.AddUnique(Bridge);
					}
				}
			}
		}
	}

	TArray<TWeakObjectPtr<AActor>> ActorsToRollback;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (IsValid(*ActorIt)
			&& ActorIt->ActorHasTag(PopulationActorTag)
			&& ActorIt->ActorHasTag(FName(*(AppliedPlanTagPrefix + Plan.PopulationHash))))
		{
			ActorsToRollback.Add(*ActorIt);
		}
	}
	ActorsToRollback.Sort([](const TWeakObjectPtr<AActor>& Left, const TWeakObjectPtr<AActor>& Right)
	{
		FString LeftId;
		FString RightId;
		HasTagWithPrefix(Left.Get(), DecisionTagPrefix, &LeftId);
		HasTagWithPrefix(Right.Get(), DecisionTagPrefix, &RightId);
		return LeftId > RightId;
	});
	for (const TWeakObjectPtr<AActor>& ActorPtr : ActorsToRollback)
	{
		AActor* Actor = ActorPtr.Get();
		FString DecisionId;
		const FEFCalystoPopulationDecisionV6* const* DecisionPtr =
			HasTagWithPrefix(Actor, DecisionTagPrefix, &DecisionId)
				? Decisions.Find(DecisionId)
				: nullptr;
		const FEFCalystoPopulationDecisionV6* Decision = DecisionPtr ? *DecisionPtr : nullptr;
		if (Decision && IEFCalystoPopulationBridgeV6::CategoryRequiresBridge(Decision->CategoryId))
		{
			FString ResolveError;
			if (IEFCalystoPopulationBridgeV6* Bridge =
				IEFCalystoPopulationBridgeV6::ResolveUniqueBridge(Decision->CategoryId, ResolveError))
			{
				Bridge->RollbackSpawnedActor(World, Actor, Plan, *Decision);
			}
		}
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}
	for (int32 Index = UsedBridges.Num() - 1; Index >= 0; --Index)
	{
		UsedBridges[Index]->RollbackPopulationPlan(World, Plan);
	}
	DungeonActor->Tags.Remove(ApplyingTag);
	RemoveTagsWithPrefix(DungeonActor, AppliedPlanTagPrefix);
	RemoveTagsWithPrefix(DungeonActor, MaterializationTagPrefix);
}
