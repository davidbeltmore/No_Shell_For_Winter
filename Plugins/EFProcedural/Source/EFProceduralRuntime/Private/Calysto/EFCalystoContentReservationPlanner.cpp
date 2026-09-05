#include "Calysto/EFCalystoContentReservationPlanner.h"
#include "Misc/SecureHash.h"

namespace EFCalystoContentPlannerPrivate
{
	bool GuidLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}

	FGuid Join(const FGuid& A, const FGuid& B)
	{
		FGuid Result;
		FGuid::ParseExact(FMD5::HashAnsiString(*(A.ToString(EGuidFormats::Digits) + B.ToString(EGuidFormats::Digits))),
			EGuidFormats::Digits, Result);
		return Result;
	}

	FGuid Decision(const FEFCalystoRandomKey& Key, const FGuid& Opportunity, const FGuid& Pair)
	{
		const FGuid Input = Join(Opportunity, Pair);
		const uint64 A = FEFCalystoDirectorProbability::Hash(Key, EEFCalystoRandomDomain::Entry, Input);
		const uint64 B = FEFCalystoDirectorProbability::Hash(Key, EEFCalystoRandomDomain::Placement, Input);
		return FGuid(uint32(A >> 32), uint32(A), uint32(B >> 32), uint32(B));
	}

	bool Finite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	bool FiniteBox(const FBox& B) { return B.IsValid && Finite(B.Min) && Finite(B.Max) && B.Min.X <= B.Max.X && B.Min.Y <= B.Max.Y && B.Min.Z <= B.Max.Z; }
	double RarityWeight(const FEFCalystoRarityWeights& W, EEFCalystoRarity Tier)
	{
		switch (Tier)
		{
		case EEFCalystoRarity::Common: return W.Common;
		case EEFCalystoRarity::Uncommon: return W.Uncommon;
		case EEFCalystoRarity::Rare: return W.Rare;
		case EEFCalystoRarity::Epic: return W.Epic;
		case EEFCalystoRarity::Winter: return W.Winter;
		default: return 0.0;
		}
	}

	int32 BucketLimit(const FEFCalystoFloorBudgets& B, EEFCalystoBudgetMembership Bucket)
	{
		switch (Bucket)
		{
		case EEFCalystoBudgetMembership::Enemy: return B.Enemies;
		case EEFCalystoBudgetMembership::LooseFood: return B.LooseFood;
		case EEFCalystoBudgetMembership::Chest: return B.Chests;
		case EEFCalystoBudgetMembership::LootActor: return B.LootActors;
		case EEFCalystoBudgetMembership::SpecialEvent: return B.SpecialEvents;
		default: return MAX_int32;
		}
	}

	double BoundsGapSquared(const FBox& A, const FBox& B)
	{
		const FVector Gap(FMath::Max3(0.0, A.Min.X - B.Max.X, B.Min.X - A.Max.X),
			FMath::Max3(0.0, A.Min.Y - B.Max.Y, B.Min.Y - A.Max.Y),
			FMath::Max3(0.0, A.Min.Z - B.Max.Z, B.Min.Z - A.Max.Z));
		return Gap.SizeSquared();
	}

	struct FOpportunity
	{
		const FEFCalystoContentRoom* Room = nullptr;
		const FEFCalystoResolvedContentGroup* Group = nullptr;
		FGuid Id;
		FGuid Scope;
		FGuid ContainerId;
		const FEFCalystoContainerCapacity* Container = nullptr;
	};

	struct FPair
	{
		int32 EntryIndex = INDEX_NONE;
		FEFCalystoReservedContent Reservation;
	};

	struct FPlanner
	{
		const FEFCalystoCompiledDirector& Config;
		const FEFCalystoContentReservationRequest& Input;
		FEFCalystoContentPlanningReport& Report;
		const FEFCalystoStyle* Style = nullptr;
		FEFCalystoContentBudgetUsage Usage;
		TArray<FEFCalystoReservedContent> Elements;
		TArray<FEFCalystoReservedSpace> Space;
		TSet<FGuid> UsedCandidates;
		TMap<FGuid, TArray<FEFCalystoResolvedContentGroup>> ThemeGroups;
		TMap<FGuid, int32> StyleEntryLimits;
		double ThreatBudget = 0;
		bool bFailed = false;

		bool Fail(FName Code, const FString& Message, bool bWorkLimit = false)
		{
			if (!bFailed)
			{
				Report.Status = bWorkLimit ? EEFCalystoContentPlanningStatus::WorkLimitExceeded : EEFCalystoContentPlanningStatus::InvalidConfiguration;
				Report.FailureCode = Code; Report.Message = Message;
			}
			bFailed = true; return false;
		}
		bool Work(int64 Units = 1)
		{
			if (bFailed) return false;
			Report.WorkUnits += Units;
			if (Report.WorkUnits > Input.Limits.MaximumWorkUnits)
				return Fail(TEXT("ReservationWorkBound"), TEXT("Finite reservation proof exceeded its work bound. No manifest was accepted and no unknown feasibility was treated as impossible."), true);
			return true;
		}
		bool Validate()
		{
			const auto& L = Input.Limits;
			if (!Config.IsValid() || !(Style = Config.FindStyle(Input.Random.StyleId)) || Input.Random.FloorNumber < 1
				|| Input.Random.AttemptIndex < 0 || Input.Random.RerollIndex < 0)
				return Fail(TEXT("ReservationIdentityInvalid"), TEXT("A valid compiled Style and immutable floor identity are required."));
			if (L.MaximumRooms < 1 || L.MaximumRooms > 2048 || L.MaximumSurfaces < 1 || L.MaximumSurfaces > 65536
				|| L.MaximumEntriesPerGroup < 1 || L.MaximumEntriesPerGroup > 1024 || L.MaximumCompatiblePairs < 1 || L.MaximumCompatiblePairs > 65536
				|| L.MaximumAmount < 1 || L.MaximumAmount > 128 || L.MaximumReservations < 1 || L.MaximumReservations > 4096
				|| L.MaximumContainers < 1 || L.MaximumContainers > 1024 || L.MaximumWorkUnits < 1 || L.MaximumWorkUnits > 2000000)
				return Fail(TEXT("ReservationBoundsInvalid"), TEXT("Planner limits exceed the supported finite work contract."));
			if (Input.Rooms.Num() > L.MaximumRooms || Input.Surfaces.Num() > L.MaximumSurfaces
				|| Input.ExistingContainers.Num() > L.MaximumContainers || Input.ExistingSpace.Num() > L.MaximumReservations)
				return Fail(TEXT("ReservationInputBound"), TEXT("Room, surface, container or existing reservation counts exceed the finite input contract."));
			// Bound every caller-owned collection before copying or walking its contents.
			int64 MetadataItems = int64(Input.ContainerCapacities.Num()) + Input.BlockedEntryIds.Num()
				+ Input.GraveyardEligibleEntryIds.Num() + Input.LastSelectedFloor.Num() + Input.InitialUsage.Buckets.Num()
				+ Input.InitialUsage.Categories.Num() + Input.InitialUsage.Entries.Num()
				+ Input.InitialUsage.ScopedCategories.Num() + Input.InitialUsage.ScopedEntries.Num();
			if (MetadataItems > L.MaximumCompatiblePairs)
				return Fail(TEXT("ReservationMetadataBound"), TEXT("Eligibility and existing usage exceed the finite metadata bound."));
			for (const auto& Room : Input.Rooms) { if (!Work()) return false; MetadataItems += Room.AllowedRoles.Num(); }
			for (const auto& Surface : Input.Surfaces)
			{ if (!Work()) return false; MetadataItems += Surface.AllowedRoles.Num() + Surface.CompatibleEntryIds.Num(); }
			for (const auto& Container : Input.ExistingContainers)
			{ if (!Work()) return false; MetadataItems += Container.Capacity.CompatibleEntryIds.Num(); }
			for (const auto& Capacity : Input.ContainerCapacities)
			{ if (!Work()) return false; MetadataItems += Capacity.Value.CompatibleEntryIds.Num(); }
			if (MetadataItems > L.MaximumCompatiblePairs)
				return Fail(TEXT("ReservationMetadataBound"), TEXT("Explicit room/surface/container compatibility exceeds the finite metadata bound."));
			if (!Work(MetadataItems)) return false;
			const auto IdsValid = [](const TSet<FGuid>& Ids)
			{ for (const FGuid& Id : Ids) if (!Id.IsValid()) return false; return true; };
			const auto RolesValid = [](const TSet<EEFCalystoGameplayRole>& Roles)
			{ for (const auto Role : Roles) if (uint8(Role) > uint8(EEFCalystoGameplayRole::SpecialEvent)) return false; return true; };
			if (!IdsValid(Input.BlockedEntryIds) || !IdsValid(Input.GraveyardEligibleEntryIds))
				return Fail(TEXT("EligibilityIdentityInvalid"), TEXT("Eligibility sets require valid persisted entry identities."));
			if (Config.GetAdvanced().Adaptation.bEnabled
				&& (!FMath::IsFinite(Input.NormalizedAdaptationInput) || FMath::Abs(Input.NormalizedAdaptationInput) > 1.0))
				return Fail(TEXT("AdaptationInputInvalid"), TEXT("Enabled adaptation requires an explicit finite input in [-1,1]."));
			ThreatBudget = FEFCalystoDirectorProbability::EvaluateLinearCurve(Style->FloorBudgets.Threat, Input.Random.FloorNumber);
			Usage = Input.InitialUsage;
			if (Usage.Actors < 0 || Usage.Actors > Style->FloorBudgets.TotalActors || !FMath::IsFinite(Usage.Threat) || Usage.Threat < 0 || Usage.Threat > ThreatBudget
				|| !FMath::IsFinite(Usage.Resources) || Usage.Resources < 0
				|| (Usage.Resources > 0 && !Input.ResourceBudget.IsSet())
				|| (Input.ResourceBudget.IsSet() && (!FMath::IsFinite(Input.ResourceBudget.GetValue()) || Input.ResourceBudget.GetValue() < Usage.Resources)))
				return Fail(TEXT("ReservationUsageInvalid"), TEXT("Existing usage and explicit resource capacity must be finite, nonnegative and within floor limits."));
			const auto CountsValid = [](const auto& Counts)
			{ for (const auto& Item : Counts) if (Item.Value < 0 || Item.Value > 1000000) return false; return true; };
			if (!CountsValid(Usage.Buckets) || !CountsValid(Usage.Categories) || !CountsValid(Usage.Entries)
				|| !CountsValid(Usage.ScopedCategories) || !CountsValid(Usage.ScopedEntries))
				return Fail(TEXT("ReservationUsageInvalid"), TEXT("Existing capacity counters must be nonnegative and bounded."));
			for (const auto& Count : Usage.Buckets)
				if (uint8(Count.Key) > uint8(EEFCalystoBudgetMembership::SpecialEvent)
					|| (Count.Key == EEFCalystoBudgetMembership::None && Count.Value != 0)
					|| Count.Value > BucketLimit(Style->FloorBudgets, Count.Key))
					return Fail(TEXT("ReservationUsageInvalid"), TEXT("Existing typed bucket usage exceeds the selected Style capacity."));
			for (const auto& Selection : Input.LastSelectedFloor)
				if (!Selection.Key.IsValid() || Selection.Value < 1 || Selection.Value >= Input.Random.FloorNumber)
					return Fail(TEXT("CooldownContextInvalid"), TEXT("Cooldown history must contain stable identities and accepted prior floors only."));
			TSet<int64> Rooms;
			TMap<EEFCalystoGameplayRole, int32> CategoryLimits;
			TMap<FGuid, int32> KnownEntryLimits, ScopedCategoryLimits, ScopedEntryLimits;
			for (const auto& Group : Style->Content)
			{
				if (!Work()) return false;
				CategoryLimits.Add(Group.Role, Group.Mode == EEFCalystoContentMode::Block ? 0 : Group.MaximumPerFloor);
				for (const auto& Entry : Group.Entries)
				{
					if (!Work()) return false;
					int32* Limit = StyleEntryLimits.Find(Entry.Selection.Id);
					if (Limit) *Limit = FMath::Min(*Limit, Entry.MaximumPerFloor);
					else StyleEntryLimits.Add(Entry.Selection.Id, Entry.MaximumPerFloor);
				}
			}
			KnownEntryLimits = StyleEntryLimits;
			int32 ContextEntries = 0;
			for (const auto& Room : Input.Rooms)
			{
				if (!Work()) return false;
				if (Room.RoomId <= 0 || Rooms.Contains(Room.RoomId) || (Room.ThemeId.IsValid() && !Config.FindTheme(Room.ThemeId))
					|| uint8(Room.Protection) > 15 || !RolesValid(Room.AllowedRoles))
					return Fail(TEXT("ReservationRoomInvalid"), TEXT("Rooms require unique positive identities and a known optional Theme."));
				Rooms.Add(Room.RoomId);
				if (ThemeGroups.Contains(Room.ThemeId)) continue;
				TArray<FEFCalystoResolvedContentGroup> Groups; FString Error;
				if (!Config.ResolveContent(Input.Random.StyleId, Room.ThemeId, Groups, Error))
					return Fail(TEXT("ContentResolutionInvalid"), Error);
				for (const auto& Group : Groups)
				{
					const auto& Content = Group.Content;
					ContextEntries += Content.Entries.Num();
					if (ContextEntries > L.MaximumCompatiblePairs)
						return Fail(TEXT("ResolvedContextBound"), TEXT("Distinct resolved room catalogs exceed the finite context entry bound."));
					for (int32 Index = 0; Index < Content.Entries.Num(); ++Index) if (!Work()) return false;
					const int32 Highest = Content.Amount.Distribution == EEFCalystoDistribution::Fixed ? Content.Amount.Amount : Content.Amount.Maximum;
					if (Content.Entries.Num() > L.MaximumEntriesPerGroup || Highest > L.MaximumAmount)
						return Fail(TEXT("ContentAmountUnsupported"), TEXT("A category exceeds the declared entry/count reservation capability; it was not truncated."));
					const FGuid Scope = FEFCalystoContentReservationPlanner::ScopeIdentity(Input.Random.StyleId, Room.ThemeId, Content.Id);
					ScopedCategoryLimits.Add(Scope, Content.MaximumPerFloor);
					int32& FloorLimit = CategoryLimits.FindOrAdd(Content.Role);
					FloorLimit = FMath::Max(FloorLimit, Group.FloorMaximum);
					for (const auto& Entry : Content.Entries)
					{
						int32& Limit = KnownEntryLimits.FindOrAdd(Entry.Selection.Id);
						if (const int32* StyleLimit = StyleEntryLimits.Find(Entry.Selection.Id)) Limit = *StyleLimit;
						else Limit = FMath::Max(Limit, Entry.MaximumPerFloor);
						ScopedEntryLimits.Add(FEFCalystoContentReservationPlanner::ScopedEntryIdentity(Scope, Entry.Selection.Id), Entry.MaximumPerFloor);
					}
				}
				ThemeGroups.Add(Room.ThemeId, MoveTemp(Groups));
			}
			const auto WithinCaps = [&](const auto& Counts, const auto& Caps)
			{
				for (const auto& Count : Counts)
				{
					if (!Work()) return false;
					const int32* Cap = Caps.Find(Count.Key);
					if (!Cap || Count.Value > *Cap) return false;
				}
				return true;
			};
			if (!WithinCaps(Usage.Categories, CategoryLimits) || !WithinCaps(Usage.Entries, KnownEntryLimits)
				|| !WithinCaps(Usage.ScopedCategories, ScopedCategoryLimits) || !WithinCaps(Usage.ScopedEntries, ScopedEntryLimits))
				return Fail(TEXT("ReservationUsageInvalid"), TEXT("Every existing category/entry counter must identify a current authored scope and remain within its exact capacity."));
			for (const auto& Surface : Input.Surfaces)
			{
				if (!Work()) return false;
				if (!Surface.Id.IsValid() || UsedCandidates.Contains(Surface.Id) || !Rooms.Contains(Surface.RoomId)
					|| Surface.Transform.ContainsNaN() || !Surface.Transform.GetRotation().IsNormalized() || Surface.Transform.GetScale3D().GetMin() <= UE_SMALL_NUMBER
					|| uint8(Surface.Zone) > uint8(EEFCalystoPlacementZone::Roof) || !RolesValid(Surface.AllowedRoles) || !IdsValid(Surface.CompatibleEntryIds)
					|| !Finite(Surface.Normal) || !FMath::IsNearlyEqual(Surface.Normal.SizeSquared(), 1.0, 0.01)
					|| !Finite(Surface.AvailableHalfExtent) || Surface.AvailableHalfExtent.GetMin() < 0
					|| !FMath::IsFinite(Surface.AvailableClearanceCm) || Surface.AvailableClearanceCm < 0)
					return Fail(TEXT("ReservationSurfaceInvalid"), TEXT("Surface identities, room ownership, geometry and validated clearance must be finite and explicit."));
				UsedCandidates.Add(Surface.Id);
			}
			UsedCandidates.Reset();
			for (const auto& Existing : Input.ExistingSpace)
			{
				if (!Work()) return false;
				if (!Existing.CandidateId.IsValid() || UsedCandidates.Contains(Existing.CandidateId) || !FiniteBox(Existing.WorldBounds)
					|| !FMath::IsFinite(Existing.SpacingCm) || Existing.SpacingCm < 0)
					return Fail(TEXT("ExistingReservationInvalid"), TEXT("Existing spatial reservations must have unique identities and finite bounds/spacing."));
				UsedCandidates.Add(Existing.CandidateId); Space.Add(Existing);
			}
			const auto CapacityValid = [&](const FEFCalystoContainerCapacity& Capacity)
			{ return Capacity.Slots >= 0 && Capacity.Slots <= L.MaximumAmount && IdsValid(Capacity.CompatibleEntryIds); };
			TSet<FGuid> Containers;
			for (const auto& Container : Input.ExistingContainers)
			{
				if (!Container.ContainerId.IsValid() || Containers.Contains(Container.ContainerId) || !Rooms.Contains(Container.RoomId) || !CapacityValid(Container.Capacity))
					return Fail(TEXT("ContainerContractInvalid"), TEXT("Existing containers require unique identity, owned room and bounded validated slots."));
				Containers.Add(Container.ContainerId);
			}
			for (const auto& Capacity : Input.ContainerCapacities)
				if (!Capacity.Key.IsValid() || !CapacityValid(Capacity.Value))
					return Fail(TEXT("ContainerContractInvalid"), TEXT("Container schemas require entry identities and bounded validated slots."));
			return true;
		}

		bool EntryEligible(const FEFCalystoContentEntry& Entry)
		{
			const auto& S = Entry.Selection;
			if (!FEFCalystoDirectorProbability::IsEligible(S, Input.Random.FloorNumber, {})
				|| Input.BlockedEntryIds.Contains(S.Id) || (Entry.bRequiresGraveyardEligibility && !Input.GraveyardEligibleEntryIds.Contains(S.Id)))
				return false;
			if (const int64* Last = Input.LastSelectedFloor.Find(S.Id))
				if (Input.Random.FloorNumber - *Last <= S.CooldownFloors) return false;
			if (Entry.ResourceCost > 0 && !Input.ResourceBudget.IsSet())
				return Fail(TEXT("ResourceBudgetUnavailable"), TEXT("A nonzero authored resource cost requires an explicit resource budget before Chance."));
			return true;
		}
		FPair MakePair(const FOpportunity& O, int32 EntryIndex, FGuid Candidate, const FTransform& Transform, const FBox& Bounds, int32 Slot)
		{
			FPair Pair; Pair.EntryIndex = EntryIndex;
			auto& R = Pair.Reservation;
			R.Id = Decision(Input.Random, O.Id, Join(Candidate, O.Group->Content.Entries[EntryIndex].Selection.Id));
			R.OpportunityId = O.Id; R.CandidateId = Candidate; R.GroupId = O.Group->Content.Id; R.ScopeId = O.Scope;
			R.ParentContainerId = O.ContainerId; R.InventorySlot = Slot; R.RoomId = O.Room->RoomId; R.ThemeId = O.Room->ThemeId;
			R.Role = O.Group->Content.Role; R.Budget = O.Group->Content.Budget;
			R.Entry = O.Group->Content.Entries[EntryIndex]; R.Transform = Transform; R.ReservedBounds = Bounds;
			return Pair;
		}
		bool BuildPairs(const FOpportunity& O, TArray<FPair>& Pairs)
		{
			const auto Rarity = FEFCalystoDirectorProbability::EvaluateRarity(O.Group->Content.Rarity, Input.Random.FloorNumber);
			for (int32 E = 0; E < O.Group->Content.Entries.Num(); ++E)
			{
				const auto& Entry = O.Group->Content.Entries[E];
				if (!Work()) return false;
				if (!EntryEligible(Entry) || RarityWeight(Rarity, Entry.Rarity) <= 0) { if (bFailed) return false; continue; }
				if (O.Group->Content.Role == EEFCalystoGameplayRole::Container && !Input.ContainerCapacities.Contains(Entry.Selection.Id))
					return Fail(TEXT("ContainerContractUnavailable"), TEXT("An eligible container entry requires an explicit inventory slot/compatibility schema before selection."));
				if (O.ContainerId.IsValid())
				{
					if (!O.Container->CompatibleEntryIds.Contains(Entry.Selection.Id)) continue;
					for (int32 Slot = 0; Slot < O.Container->Slots; ++Slot)
					{
						if (!Work()) return false;
						const FGuid SlotId = Join(O.ContainerId, FGuid(0x534C4F54, 0, 0, uint32(Slot + 1)));
						if (Pairs.Num() >= Input.Limits.MaximumCompatiblePairs)
							return Fail(TEXT("CompatibilityPairBound"), TEXT("Finite entry/candidate compatibility exceeds the supported pair limit."));
						Pairs.Add(MakePair(O, E, SlotId, FTransform::Identity, FBox(ForceInit), Slot));
					}
				}
				else for (const auto& Surface : Input.Surfaces)
				{
					if (!Work()) return false;
					if (Surface.RoomId != O.Room->RoomId || Surface.Zone != Entry.Placement.Zone || !Surface.bCollisionValidated
						|| Surface.bProtectsDoorway || Surface.bProtectsMainRoute || !Surface.AllowedRoles.Contains(O.Group->Content.Role)
						|| !Surface.CompatibleEntryIds.Contains(Entry.Selection.Id) || UsedCandidates.Contains(Surface.Id)
						|| (Entry.Placement.bRequiresNavigation && !Surface.bNavigationValidated)
						|| Surface.AvailableClearanceCm < Entry.Placement.Clearance) continue;
					FVector TangentA, TangentB; Surface.Normal.FindBestAxisVectors(TangentA, TangentB);
					const FGuid PlacementId = Join(O.Id, Join(Surface.Id, Entry.Selection.Id));
					const double A = (2.0 * FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Placement, PlacementId, 0) - 1.0) * Entry.Placement.PositionVariationCm;
					const double B = (2.0 * FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Placement, PlacementId, 1) - 1.0) * Entry.Placement.PositionVariationCm;
					FTransform Transform = Surface.Transform; Transform.AddToTranslation(A * TangentA + B * TangentB);
					const FVector Extent = Entry.Placement.FootprintHalfExtent;
					// Clearance and jitter are world centimetres, even for scaled/rotated payloads.
					const FBox Bounds = FBox(-Extent, Extent).TransformBy(Transform).ExpandBy(Entry.Placement.Clearance);
					if (Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized() || !FiniteBox(Bounds))
						return Fail(TEXT("PlacementTransformInvalid"), TEXT("A proposed deterministic placement produced nonfinite geometry."));
					FBox LocalBounds(ForceInit);
					if (!Work(8)) return false;
					for (int32 Corner = 0; Corner < 8; ++Corner)
						LocalBounds += Surface.Transform.InverseTransformPosition(FVector(
							(Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
							(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z));
					if (!FiniteBox(LocalBounds))
						return Fail(TEXT("PlacementTransformInvalid"), TEXT("The proposed placement has no finite surface-space bound."));
					if (LocalBounds.Min.X < -Surface.AvailableHalfExtent.X || LocalBounds.Max.X > Surface.AvailableHalfExtent.X
						|| LocalBounds.Min.Y < -Surface.AvailableHalfExtent.Y || LocalBounds.Max.Y > Surface.AvailableHalfExtent.Y
						|| LocalBounds.Min.Z < -Surface.AvailableHalfExtent.Z || LocalBounds.Max.Z > Surface.AvailableHalfExtent.Z) continue;
					if (Pairs.Num() >= Input.Limits.MaximumCompatiblePairs)
						return Fail(TEXT("CompatibilityPairBound"), TEXT("Finite entry/candidate compatibility exceeds the supported pair limit."));
					Pairs.Add(MakePair(O, E, Surface.Id, Transform, Bounds, INDEX_NONE));
				}
				if (Pairs.Num() > Input.Limits.MaximumCompatiblePairs)
					return Fail(TEXT("CompatibilityPairBound"), TEXT("Finite entry/candidate compatibility exceeds the supported pair limit."));
			}
			Pairs.Sort([](const FPair& A, const FPair& B)
			{
				return A.Reservation.CandidateId != B.Reservation.CandidateId
					? GuidLess(A.Reservation.CandidateId, B.Reservation.CandidateId)
					: GuidLess(A.Reservation.Entry.Selection.Id, B.Reservation.Entry.Selection.Id);
			});
			return true;
		}

		bool SpaceFits(const FEFCalystoReservedContent& A, FGuid Candidate, const FBox& Bounds, double Spacing)
		{
			if (!Work()) return false;
			if (A.CandidateId == Candidate) return false;
			if (A.InventorySlot != INDEX_NONE) return true;
			if (A.ReservedBounds.Intersect(Bounds)) return false;
			const double Gap = FMath::Max(A.Entry.Placement.Spacing, Spacing);
			return BoundsGapSquared(A.ReservedBounds, Bounds) + 1e-8 >= Gap * Gap;
		}

		bool Fits(const FOpportunity& O, const TArray<FPair>& Pairs, int32 PairIndex, const TArray<int32>& Selected)
		{
			if (!Work()) return false;
			const auto& R = Pairs[PairIndex].Reservation; const bool Inventory = R.InventorySlot != INDEX_NONE;
			if (UsedCandidates.Contains(R.CandidateId)) return false;
			const int32 Count = Selected.Num() + 1;
			if (Usage.Categories.FindRef(R.Role) + Count > O.Group->FloorMaximum
				|| Usage.ScopedCategories.FindRef(O.Scope) + Count > O.Group->Content.MaximumPerFloor
				|| (!Inventory && Usage.Actors + Count > Style->FloorBudgets.TotalActors)
				|| (R.Budget != EEFCalystoBudgetMembership::None && Usage.Buckets.FindRef(R.Budget) + Count > BucketLimit(Style->FloorBudgets, R.Budget))) return false;
			int32 EntryCount = 1;
			double Threat = R.Budget == EEFCalystoBudgetMembership::Enemy ? R.Entry.ThreatCost : 0.0;
			double Resources = R.Entry.ResourceCost;
			for (int32 PriorIndex : Selected)
			{
				const auto& Prior = Pairs[PriorIndex].Reservation;
				if (!SpaceFits(R, Prior.CandidateId, Prior.ReservedBounds, Prior.Entry.Placement.Spacing)) return false;
				if (Prior.Entry.Selection.Id == R.Entry.Selection.Id) ++EntryCount;
				Threat += Prior.Budget == EEFCalystoBudgetMembership::Enemy ? Prior.Entry.ThreatCost : 0.0;
				Resources += Prior.Entry.ResourceCost;
			}
			const int32* GlobalEntryLimit = StyleEntryLimits.Find(R.Entry.Selection.Id);
			if (Usage.Entries.FindRef(R.Entry.Selection.Id) + EntryCount > (GlobalEntryLimit ? *GlobalEntryLimit : R.Entry.MaximumPerFloor)
				|| Usage.ScopedEntries.FindRef(FEFCalystoContentReservationPlanner::ScopedEntryIdentity(O.Scope, R.Entry.Selection.Id)) + EntryCount > R.Entry.MaximumPerFloor
				|| Usage.Threat + Threat > ThreatBudget + 1e-8
				|| (Input.ResourceBudget.IsSet() && Usage.Resources + Resources > Input.ResourceBudget.GetValue() + 1e-8)) return false;
			if (!Inventory)
				for (const auto& Existing : Space)
					if (!SpaceFits(R, Existing.CandidateId, Existing.WorldBounds, Existing.SpacingCm)) return false;
			return true;
		}

		bool Completion(const FOpportunity& O, const TArray<FPair>& Pairs, int32 Remaining, int32 Begin, TArray<int32>& Selected)
		{
			if (!Work()) return false;
			if (Remaining == 0) return true;
			for (int32 Index = Begin; Index < Pairs.Num(); ++Index)
			{
				if (!Fits(O, Pairs, Index, Selected)) { if (bFailed) return false; continue; }
				Selected.Add(Index);
				const bool Found = Completion(O, Pairs, Remaining - 1, Index + 1, Selected);
				Selected.Pop(EAllowShrinking::No);
				if (Found || bFailed) return Found;
			}
			return false;
		}

		int32 CapacityUpperBound(const FOpportunity& O, const TArray<FPair>& Pairs)
		{
			TSet<FGuid> Candidates, Entries;
			int64 EntryCapacity = 0;
			for (const auto& Pair : Pairs)
			{
				if (!Work()) return 0;
				const auto& R = Pair.Reservation;
				if (!UsedCandidates.Contains(R.CandidateId)) Candidates.Add(R.CandidateId);
				if (Entries.Contains(R.Entry.Selection.Id)) continue;
				Entries.Add(R.Entry.Selection.Id);
				const int32* GlobalLimit = StyleEntryLimits.Find(R.Entry.Selection.Id);
				EntryCapacity += FMath::Max(0, FMath::Min(
					(GlobalLimit ? *GlobalLimit : R.Entry.MaximumPerFloor) - Usage.Entries.FindRef(R.Entry.Selection.Id),
					R.Entry.MaximumPerFloor - Usage.ScopedEntries.FindRef(FEFCalystoContentReservationPlanner::ScopedEntryIdentity(O.Scope, R.Entry.Selection.Id))));
			}
			int32 Capacity = FMath::Min(Candidates.Num(), int32(FMath::Min<int64>(EntryCapacity, MAX_int32)));
			Capacity = FMath::Min(Capacity, O.Group->FloorMaximum - Usage.Categories.FindRef(O.Group->Content.Role));
			Capacity = FMath::Min(Capacity, O.Group->Content.MaximumPerFloor - Usage.ScopedCategories.FindRef(O.Scope));
			if (!O.ContainerId.IsValid()) Capacity = FMath::Min(Capacity, Style->FloorBudgets.TotalActors - Usage.Actors);
			if (O.Group->Content.Budget != EEFCalystoBudgetMembership::None)
				Capacity = FMath::Min(Capacity, BucketLimit(Style->FloorBudgets, O.Group->Content.Budget) - Usage.Buckets.FindRef(O.Group->Content.Budget));
			return FMath::Max(0, Capacity);
		}

		void Commit(const FEFCalystoReservedContent& R)
		{
			++Usage.Categories.FindOrAdd(R.Role); ++Usage.ScopedCategories.FindOrAdd(R.ScopeId);
			++Usage.Entries.FindOrAdd(R.Entry.Selection.Id);
			++Usage.ScopedEntries.FindOrAdd(FEFCalystoContentReservationPlanner::ScopedEntryIdentity(R.ScopeId, R.Entry.Selection.Id));
			if (R.Budget != EEFCalystoBudgetMembership::None) ++Usage.Buckets.FindOrAdd(R.Budget);
			if (R.Budget == EEFCalystoBudgetMembership::Enemy) Usage.Threat += R.Entry.ThreatCost;
			Usage.Resources += R.Entry.ResourceCost;
			if (R.InventorySlot == INDEX_NONE)
			{
				++Usage.Actors;
				Space.Add({R.CandidateId, R.ReservedBounds, R.Entry.Placement.Spacing});
			}
			UsedCandidates.Add(R.CandidateId); Elements.Add(R);
		}

		bool PlanOpportunity(const FOpportunity& O)
		{
			FEFCalystoContentOpportunityReport& Detail = Report.Opportunities.AddDefaulted_GetRef();
			Detail.OpportunityId = O.Id; Detail.RoomId = O.Room->RoomId; Detail.GroupId = O.Group->Content.Id;
			Detail.Role = O.Group->Content.Role; Detail.ContainerId = O.ContainerId;
			Detail.RequestedChancePercent = FEFCalystoDirectorProbability::EvaluateLinearCurve(O.Group->Content.Chance, Input.Random.FloorNumber);
			Detail.EffectiveChancePercent = FMath::Clamp(Detail.RequestedChancePercent
				* FEFCalystoDirectorProbability::AdaptationMultiplier(Config.GetAdvanced().Adaptation, Input.NormalizedAdaptationInput), 0.0, 100.0);
			if (O.Room->Protection != EEFCalystoProtectedRoom::None || !O.Room->AllowedRoles.Contains(Detail.Role))
			{ Detail.Outcome = EEFCalystoContentOpportunityOutcome::IneligibleRoom; Detail.Reason = TEXT("ProtectedOrIneligibleRoom"); return true; }
			TArray<FPair> Pairs;
			if (!BuildPairs(O, Pairs)) return false;
			if (Pairs.IsEmpty())
			{ Detail.Outcome = EEFCalystoContentOpportunityOutcome::NoCompatibleEntries; Detail.Reason = TEXT("NoCompatibleEntrySurface"); return true; }
			const auto& Amount = O.Group->Content.Amount;
			const int32 Lowest = Amount.Distribution == EEFCalystoDistribution::Fixed ? Amount.Amount : Amount.Minimum;
			const int32 Highest = Amount.Distribution == EEFCalystoDistribution::Fixed ? Amount.Amount : Amount.Maximum;
			// Positive costs, capacity and spacing constraints are downward closed.
			// Exhaustion returns failure; it is never interpreted as a feasibility result.
			TArray<int32> Prefix;
			const int32 UpperBound = CapacityUpperBound(O, Pairs);
			if (bFailed) return false;
			for (int32 Count = Lowest; Count <= FMath::Min(Highest, UpperBound); ++Count)
			{
				if (FEFCalystoDirectorProbability::AmountProbability(Amount, Count) <= 0) continue;
				if (!Completion(O, Pairs, Count, 0, Prefix)) { if (bFailed) return false; break; }
				if (Elements.Num() + Count > Input.Limits.MaximumReservations)
					return Fail(TEXT("ReservationOutputBound"), TEXT("A feasible authored amount exceeds the declared output capacity. It was not excluded or truncated as a probability outcome."));
				Detail.FeasibleAmounts.Add(Count);
				Detail.FeasibleAmountProbabilityMass += FEFCalystoDirectorProbability::AmountProbability(Amount, Count);
			}
			if (Detail.FeasibleAmounts.IsEmpty())
			{ Detail.Outcome = EEFCalystoContentOpportunityOutcome::NoFeasibleAmount; Detail.Reason = TEXT("NoSupportedAmountBeforeChance"); return true; }
			Detail.bChanceRolled = true;
			if (!FEFCalystoDirectorProbability::RollChance(Detail.EffectiveChancePercent,
				FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Chance, O.Id)))
			{ Detail.Outcome = EEFCalystoContentOpportunityOutcome::ChanceAbsent; Detail.Reason = TEXT("ChanceNotSelected"); return true; }
			FString Error; double Mass = 0;
			if (!FEFCalystoDirectorProbability::SampleFeasibleAmount(Amount, Detail.FeasibleAmounts,
				FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Amount, O.Id),
				Detail.SelectedAmount, Mass, Error)) return Fail(TEXT("AmountDecisionInvalid"), Error);
			const auto Rarity = FEFCalystoDirectorProbability::EvaluateRarity(O.Group->Content.Rarity, Input.Random.FloorNumber);
			for (int32 Slot = 0; Slot < Detail.SelectedAmount; ++Slot)
			{
				TArray<int32> ViablePairs;
				TArray<FEFCalystoWeightedAlternative> Entries;
				TSet<FGuid> SeenEntries;
				TArray<EEFCalystoRarity> Tiers;
				for (int32 Index = 0; Index < Pairs.Num(); ++Index)
				{
					if (!Fits(O, Pairs, Index, Prefix)) { if (bFailed) return false; continue; }
					Prefix.Add(Index);
					const bool bCompletable = Completion(O, Pairs, Detail.SelectedAmount - Slot - 1, 0, Prefix);
					Prefix.Pop(EAllowShrinking::No);
					if (bFailed) return false;
					if (!bCompletable) continue;
					ViablePairs.Add(Index);
					const auto& Entry = Pairs[Index].Reservation.Entry;
					if (!SeenEntries.Contains(Entry.Selection.Id))
					{ SeenEntries.Add(Entry.Selection.Id); Entries.Add({Entry.Selection.Id, Entry.Selection.Weight}); Tiers.AddUnique(Entry.Rarity); }
				}
				EEFCalystoRarity Tier;
				if (!FEFCalystoDirectorProbability::SelectPopulatedRarity(Rarity, Tiers,
					FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Rarity, O.Id, Slot), Tier, Error))
					return Fail(TEXT("RarityReservationInvariant"), Error);
				Entries.RemoveAll([&](const FEFCalystoWeightedAlternative& E)
				{ return O.Group->Content.Entries.FindByPredicate([&](const FEFCalystoContentEntry& V) { return V.Selection.Id == E.Id; })->Rarity != Tier; });
				FGuid EntryId;
				if (!FEFCalystoDirectorProbability::SelectWeighted(Entries,
					FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Entry, O.Id, Slot), EntryId, Error))
					return Fail(TEXT("EntryReservationInvariant"), Error);
				TArray<FEFCalystoWeightedAlternative> Candidates;
				for (int32 Index : ViablePairs)
					if (Pairs[Index].Reservation.Entry.Selection.Id == EntryId) Candidates.Add({Pairs[Index].Reservation.CandidateId, 1.0});
				FGuid Candidate;
				if (!FEFCalystoDirectorProbability::SelectWeighted(Candidates,
					FEFCalystoDirectorProbability::Unit(Input.Random, EEFCalystoRandomDomain::Placement, O.Id, Slot), Candidate, Error))
					return Fail(TEXT("PlacementReservationInvariant"), Error);
				const int32* Selected = ViablePairs.FindByPredicate([&](int32 Index)
				{ return Pairs[Index].Reservation.CandidateId == Candidate && Pairs[Index].Reservation.Entry.Selection.Id == EntryId; });
				if (!Selected) return Fail(TEXT("ReservationSelectionMissing"), TEXT("A selected pair disappeared from its proved finite compatibility set."));
				Prefix.Add(*Selected);
			}
			if (Prefix.Num() != Detail.SelectedAmount) return Fail(TEXT("ReservationCountMismatch"), TEXT("The frozen amount was not reserved exactly."));
			for (int32 Index : Prefix) Commit(Pairs[Index].Reservation);
			Detail.ReservedAmount = Prefix.Num(); Detail.Outcome = EEFCalystoContentOpportunityOutcome::Reserved; Detail.Reason = TEXT("ExactAmountReserved");
			return true;
		}

		FOpportunity Opportunity(const FEFCalystoContentRoom& Room, const FEFCalystoResolvedContentGroup& Group,
			const FEFCalystoExistingContainer* Container = nullptr)
		{
			FOpportunity O; O.Room = &Room; O.Group = &Group;
			O.Scope = FEFCalystoContentReservationPlanner::ScopeIdentity(Input.Random.StyleId, Room.ThemeId, Group.Content.Id);
			O.Id = Join(FEFCalystoDirectorProbability::RoomIdentity(Room.RoomId), Group.Content.Id);
			if (Container) { O.ContainerId = Container->ContainerId; O.Container = &Container->Capacity; O.Id = Join(O.Id, O.ContainerId); }
			return O;
		}
		void Rank(TArray<FOpportunity>& Opportunities)
		{
			Opportunities.Sort([&](const FOpportunity& A, const FOpportunity& B)
			{
				const uint64 ARank = FEFCalystoDirectorProbability::Hash(Input.Random, EEFCalystoRandomDomain::Placement, A.Id, -1);
				const uint64 BRank = FEFCalystoDirectorProbability::Hash(Input.Random, EEFCalystoRandomDomain::Placement, B.Id, -1);
				return ARank != BRank ? ARank < BRank : GuidLess(A.Id, B.Id);
			});
		}
		bool Run()
		{
			if (!Validate()) return false;
			TArray<FOpportunity> WorldOpportunities;
			for (const auto& Room : Input.Rooms)
				for (const auto& Group : ThemeGroups.FindChecked(Room.ThemeId))
				{
					if (!Work()) return false;
					if (Group.Content.Role != EEFCalystoGameplayRole::ContainerContent) WorldOpportunities.Add(Opportunity(Room, Group));
				}
			Rank(WorldOpportunities);
			for (const auto& O : WorldOpportunities) if (!PlanOpportunity(O)) return false;
			TArray<FEFCalystoExistingContainer> Containers = Input.ExistingContainers;
			for (const auto& R : Elements)
				if (R.Role == EEFCalystoGameplayRole::Container)
				{
					const auto& Capacity = Input.ContainerCapacities.FindChecked(R.Entry.Selection.Id);
					if (!Work(1 + Capacity.CompatibleEntryIds.Num())) return false;
					if (Containers.Num() >= Input.Limits.MaximumContainers)
						return Fail(TEXT("ContainerOpportunityBound"), TEXT("Selected and existing containers exceed the declared content-opportunity limit."));
					Containers.Add({R.Id, R.RoomId, Capacity});
				}
			if (Containers.Num() > Input.Limits.MaximumContainers)
				return Fail(TEXT("ContainerOpportunityBound"), TEXT("Selected and existing containers exceed the declared content-opportunity limit."));
			TArray<FOpportunity> InventoryOpportunities;
			for (const auto& Container : Containers)
			{
				if (!Work(Input.Rooms.Num())) return false;
				const auto* Room = Input.Rooms.FindByPredicate([&](const FEFCalystoContentRoom& R) { return R.RoomId == Container.RoomId; });
				for (const auto& Group : ThemeGroups.FindChecked(Room->ThemeId))
				{
					if (!Work()) return false;
					if (Group.Content.Role == EEFCalystoGameplayRole::ContainerContent) InventoryOpportunities.Add(Opportunity(*Room, Group, &Container));
				}
			}
			Rank(InventoryOpportunities);
			for (const auto& O : InventoryOpportunities) if (!PlanOpportunity(O)) return false;
			return true;
		}
	};

	FString ManifestHash(const TArray<FEFCalystoReservedContent>& Elements)
	{
		FString Canonical;
		for (const auto& R : Elements)
		{
			Canonical += FString::Printf(TEXT("%s|%s|%s|%s|%s|%lld|%d|%d|%d|%d|%d|"),
				*R.Id.ToString(EGuidFormats::Digits), *R.CandidateId.ToString(EGuidFormats::Digits),
				*R.Entry.Selection.Id.ToString(EGuidFormats::Digits), *R.ParentContainerId.ToString(EGuidFormats::Digits),
				*R.ScopeId.ToString(EGuidFormats::Digits), R.RoomId, R.InventorySlot, int32(R.Role), int32(R.Budget), int32(R.Entry.Rarity), int32(R.Entry.Gender));
			Canonical += R.Entry.ActorClass.ToSoftObjectPath().ToString() + TEXT("|") + R.Entry.InventoryClass.ToSoftObjectPath().ToString() + TEXT("|");
			Canonical += R.Entry.Archetype.ToString().ToLower() + FString::Printf(TEXT("|%d|%d|%d|%d|%.17g|%.17g|%.17g|"),
				int32(R.Entry.Lifecycle), R.Entry.MinimumLevelOffset, R.Entry.MaximumLevelOffset, int32(R.Entry.bRequiresGraveyardEligibility),
				R.Entry.ThreatCost, R.Entry.ResourceCost, R.Entry.Placement.Spacing);
			Canonical += FString::Printf(TEXT("%d|%d|%.17g|%.17g|%.17g,%.17g,%.17g|"),
				int32(R.Entry.Placement.Zone), int32(R.Entry.Placement.bRequiresNavigation), R.Entry.Placement.Clearance,
				R.Entry.Placement.PositionVariationCm, R.Entry.Placement.FootprintHalfExtent.X,
				R.Entry.Placement.FootprintHalfExtent.Y, R.Entry.Placement.FootprintHalfExtent.Z);
			const FVector P = R.Transform.GetLocation(), S = R.Transform.GetScale3D(); const FQuat Q = R.Transform.GetRotation();
			Canonical += FString::Printf(TEXT("%.17g,%.17g,%.17g|%.17g,%.17g,%.17g,%.17g|%.17g,%.17g,%.17g|"),
				P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
			if (R.ReservedBounds.IsValid)
				Canonical += FString::Printf(TEXT("%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
					R.ReservedBounds.Min.X,R.ReservedBounds.Min.Y,R.ReservedBounds.Min.Z,R.ReservedBounds.Max.X,R.ReservedBounds.Max.Y,R.ReservedBounds.Max.Z);
			Canonical += TEXT("\n");
		}
		return FMD5::HashAnsiString(*Canonical);
	}
}

FGuid FEFCalystoContentReservationPlanner::ScopeIdentity(FGuid StyleId, FGuid ThemeId, FGuid GroupId)
{ return EFCalystoContentPlannerPrivate::Join(EFCalystoContentPlannerPrivate::Join(StyleId, ThemeId), GroupId); }
FGuid FEFCalystoContentReservationPlanner::ScopedEntryIdentity(FGuid ScopeId, FGuid EntryId)
{ return EFCalystoContentPlannerPrivate::Join(ScopeId, EntryId); }

bool FEFCalystoContentReservationPlanner::Build(const FEFCalystoCompiledDirector& Configuration,
	const FEFCalystoContentReservationRequest& Request, FEFCalystoReservedContentManifest& OutManifest,
	FEFCalystoContentPlanningReport& OutReport)
{
	OutManifest = {}; OutReport = {};
	EFCalystoContentPlannerPrivate::FPlanner Planner{Configuration, Request, OutReport};
	if (!Planner.Run()) return false;
	Planner.Elements.Sort([](const FEFCalystoReservedContent& A, const FEFCalystoReservedContent& B)
	{ return EFCalystoContentPlannerPrivate::GuidLess(A.Id, B.Id); });
	FEFCalystoReservedContentManifest Accepted;
	TSet<FGuid> Ids;
	for (const auto& Element : Planner.Elements)
	{
		if (!Element.Id.IsValid() || Ids.Contains(Element.Id))
			return Planner.Fail(TEXT("ReservationIdentityCollision"), TEXT("Frozen reservation identities must be valid and unique."));
		Ids.Add(Element.Id);
		const FSoftObjectPath Path = Element.InventorySlot == INDEX_NONE
			? Element.Entry.ActorClass.ToSoftObjectPath() : Element.Entry.InventoryClass.ToSoftObjectPath();
		if (Path.IsNull()) return Planner.Fail(TEXT("SelectedPayloadUnavailable"), TEXT("A selected typed payload path is missing; no manifest was accepted."));
		Accepted.Dependencies.AddUnique(Path);
	}
	Accepted.Dependencies.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
	Accepted.Elements = MoveTemp(Planner.Elements); Accepted.FinalUsage = MoveTemp(Planner.Usage);
	Accepted.Hash = EFCalystoContentPlannerPrivate::ManifestHash(Accepted.Elements); Accepted.bValid = true;
	OutManifest = MoveTemp(Accepted);
	OutReport.Status = EEFCalystoContentPlanningStatus::Complete; OutReport.Message = TEXT("Every selected amount has an exact immutable reservation; realization remains unverified.");
	return true;
}
