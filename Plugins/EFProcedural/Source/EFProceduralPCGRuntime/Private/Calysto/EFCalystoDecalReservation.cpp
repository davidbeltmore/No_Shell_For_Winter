#include "Calysto/EFCalystoDecalReservation.h"

#include "Misc/SecureHash.h"

namespace EFCalystoDecalReservationPrivate
{
	constexpr TCHAR FloorMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor.MI_CalystoBloodDecal_Floor");
	constexpr TCHAR WallMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall.MI_CalystoBloodDecal_Wall");
	constexpr TCHAR RoofMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling.MI_CalystoBloodDecal_Ceiling");
	constexpr TCHAR ColorTexture[] = TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.T_Splat_04");
	constexpr TCHAR NormalTexture[] = TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.T_Splat_N_04");

	bool GuidLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}
	bool Finite(const FVector& V)
	{ return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	bool SupportsSurface(const FEFCalystoDecalVariant& Variant, const EEFCalystoDecalSurface Surface)
	{
		return Surface == EEFCalystoDecalSurface::Floor ? Variant.bFloor
			: Surface == EEFCalystoDecalSurface::Wall ? Variant.bWall : Variant.bRoof;
	}
	int32 SurfaceLimit(const FEFCalystoDecals& Decals, const EEFCalystoDecalSurface Surface)
	{
		return Surface == EEFCalystoDecalSurface::Floor ? Decals.FloorLimit
			: Surface == EEFCalystoDecalSurface::Wall ? Decals.WallLimit : Decals.RoofLimit;
	}
	double MaximumSize(const FEFCalystoDecals& Decals)
	{ return Decals.SizeCm.Distribution == EEFCalystoDistribution::Fixed ? Decals.SizeCm.Value : Decals.SizeCm.Maximum; }
	FString GuidText(const FGuid& Id) { return Id.ToString(EGuidFormats::Digits); }
	FGuid DerivedId(const FEFCalystoRandomKey& Key, const FEFCalystoDecalOpportunity& Opportunity, const FGuid& Variant)
	{
		const uint64 A = FEFCalystoDirectorProbability::Hash(Key, EEFCalystoRandomDomain::Decal, Opportunity.Id, 17);
		const uint64 B = FEFCalystoDirectorProbability::Hash(Key, EEFCalystoRandomDomain::Decal, Variant, Opportunity.NativeOpportunityId);
		return FGuid(uint32(A >> 32), uint32(A), uint32(B >> 32), uint32(B));
	}
	bool AllowedPayload(const FEFCalystoDecalVariant& Variant, FString& Error)
	{
		const FString Material = Variant.Material.ToSoftObjectPath().ToString();
		const FString Color = Variant.ColorTexture.ToSoftObjectPath().ToString();
		const FString Normal = Variant.NormalTexture.ToSoftObjectPath().ToString();
		if (Color != ColorTexture || Normal != NormalTexture)
		{
			Error = TEXT("Selected decals may retain only the intended RealisticBlood Splat_04 color and normal textures.");
			return false;
		}
		const bool bFloor = Material == FloorMaterial;
		const bool bWall = Material == WallMaterial;
		const bool bRoof = Material == RoofMaterial;
		if (!bFloor && !bWall && !bRoof)
		{
			Error = TEXT("Selected decal material is outside the project-owned RealisticBlood dependency closure.");
			return false;
		}
		if ((bFloor && (!Variant.bFloor || Variant.bWall || Variant.bRoof))
			|| (bWall && (Variant.bFloor || !Variant.bWall || Variant.bRoof))
			|| (bRoof && (Variant.bFloor || Variant.bWall || !Variant.bRoof)))
		{
			Error = TEXT("Each retained blood decal material must declare exactly its matching supported surface.");
			return false;
		}
		return true;
	}
	bool ValidateDecals(const FEFCalystoDecals& Decals, const bool bStyle, const FString& Path, FString& Error)
	{
		if (uint8(Decals.Mode) > uint8(EEFCalystoDecalMode::Block)
			|| (bStyle && Decals.Mode == EEFCalystoDecalMode::Inherit))
		{ Error = Path + TEXT(".Mode has an unsupported inheritance mode."); return false; }
		if (Decals.Mode != EEFCalystoDecalMode::Replace) return true;
		if (!FMath::IsFinite(Decals.ChancePercent) || Decals.ChancePercent < 0 || Decals.ChancePercent > 100
			|| Decals.MaximumPerRoom < 0 || Decals.MaximumPerRoom > 1 || Decals.ActiveFloorBudget < 0
			|| Decals.ActiveFloorBudget > AEFCalystoDecalPoolOwner::PoolCapacity || Decals.FloorLimit < 0
			|| Decals.WallLimit < 0 || Decals.RoofLimit < 0 || !FEFCalystoDirectorProbability::IsValid(Decals.SizeCm)
			|| MaximumSize(Decals) <= 0 || !FMath::IsFinite(Decals.CullDistanceCm) || Decals.CullDistanceCm < 0
			|| !FMath::IsFinite(Decals.FadeScreenSize) || Decals.FadeScreenSize < 0 || Decals.Variants.Num() > 24)
		{ Error = Path + TEXT(" has unsupported bounded decal capacities, probability, size or culling values."); return false; }
		TSet<FGuid> Ids;
		for (int32 Index = 0; Index < Decals.Variants.Num(); ++Index)
		{
			const FEFCalystoDecalVariant& Variant = Decals.Variants[Index];
			if (!Variant.Selection.Id.IsValid() || Ids.Contains(Variant.Selection.Id)
				|| !FMath::IsFinite(Variant.Selection.Weight) || Variant.Selection.Weight < 0)
			{ Error = FString::Printf(TEXT("%s.Variants[%d].Selection lacks a valid stable identity or weight."), *Path, Index); return false; }
			Ids.Add(Variant.Selection.Id);
			if (!Variant.Selection.bEnabled || Variant.Selection.Weight == 0) continue;
			if (!AllowedPayload(Variant, Error)) { Error = FString::Printf(TEXT("%s.Variants[%d]: %s"), *Path, Index, *Error); return false; }
		}
		return true;
	}
	struct FRoomCandidate
	{
		const FEFCalystoDecalOpportunity* Opportunity = nullptr;
		FEFCalystoDecals Effective;
		TArray<FEFCalystoWeightedAlternative> Variants;
	};
	struct FRoomPlan
	{
		int64 RoomId = 0;
		TArray<FRoomCandidate> Candidates;
	};
}

bool FEFCalystoDecalReservationPlanner::ValidateConfiguration(const FEFCalystoCompiledDirector& Configuration,
	const FGuid& StyleId, FString& Error)
{
	using namespace EFCalystoDecalReservationPrivate;
	Error.Reset();
	const FEFCalystoStyle* Style = Configuration.FindStyle(StyleId);
	if (!Configuration.IsValid() || !Style || !Style->Selection.bEnabled || Style->Selection.Weight <= 0)
	{ Error = TEXT("Selected Style must be a valid enabled compiled Style before decals can be reserved."); return false; }
	if (!ValidateDecals(Style->Decals, true, TEXT("Style.Decals"), Error)) return false;
	for (const FEFCalystoTheme& Theme : Configuration.GetThemes())
		if (!ValidateDecals(Theme.Decals, false, TEXT("RoomTheme.Decals"), Error)) return false;
	return true;
}

bool FEFCalystoDecalReservationPlanner::HasPotentiallyActiveDecals(const FEFCalystoCompiledDirector& Configuration,
	const FGuid& StyleId)
{
	const FEFCalystoStyle* Style = Configuration.FindStyle(StyleId);
	if (!Configuration.IsValid() || !Style) return false;
	auto Active=[](const FEFCalystoDecals& D)
	{
		return D.Mode == EEFCalystoDecalMode::Replace && D.ChancePercent > 0 && D.MaximumPerRoom > 0
			&& D.ActiveFloorBudget > 0 && (D.FloorLimit > 0 || D.WallLimit > 0 || D.RoofLimit > 0);
	};
	if (Active(Style->Decals)) return true;
	for (const FEFCalystoTheme& Theme : Configuration.GetThemes()) if (Active(Theme.Decals)) return true;
	return false;
}

bool FEFCalystoDecalReservationPlanner::Build(const FEFCalystoCompiledDirector& Configuration,
	const FEFCalystoRandomKey& Random, TConstArrayView<FEFCalystoDecalOpportunity> Opportunities,
	FEFCalystoReservedDecalManifest& OutManifest, FEFCalystoDecalPlanningReport& OutReport)
{
	using namespace EFCalystoDecalReservationPrivate;
	OutManifest = {}; OutReport = {};
	FString Error;
	if (!ValidateConfiguration(Configuration, Random.StyleId, Error))
	{ OutReport.FailureCode = TEXT("DecalConfigurationInvalid"); OutReport.Message = Error; return false; }
	const FEFCalystoStyle* Style = Configuration.FindStyle(Random.StyleId);
	if (!Style || Random.FloorNumber < 1 || Random.AttemptIndex < 0 || Opportunities.Num() > 4096)
	{ OutReport.FailureCode = TEXT("DecalInputInvalid"); OutReport.Message = TEXT("Decal reservation requires a bounded valid floor identity and opportunities."); return false; }

	TMap<int64, FRoomPlan> Rooms;
	TSet<FGuid> OpportunityIds; TSet<int64> NativeOpportunityIds;
	for (const FEFCalystoDecalOpportunity& Opportunity : Opportunities)
	{
		FEFCalystoDecalOpportunityReport& Report = OutReport.Opportunities.AddDefaulted_GetRef();
		Report.OpportunityId = Opportunity.Id; Report.RoomId = Opportunity.RoomId; Report.Surface = Opportunity.Surface; Report.ThemeId = Opportunity.ThemeId;
		if (!Opportunity.Id.IsValid() || Opportunity.RoomId <= 0 || Opportunity.NativeOpportunityId <= 0
			|| OpportunityIds.Contains(Opportunity.Id) || NativeOpportunityIds.Contains(Opportunity.NativeOpportunityId)
			|| !Opportunity.Support.IsValid() || Opportunity.WorldTransform.ContainsNaN()
			|| !Opportunity.WorldTransform.GetRotation().IsNormalized() || !Finite(Opportunity.SurfaceNormal)
			|| !FMath::IsNearlyEqual(Opportunity.SurfaceNormal.SizeSquared(), 1.0, 1e-6)
			|| !FMath::IsFinite(Opportunity.MaximumSizeCm) || Opportunity.MaximumSizeCm <= 0
			|| !Opportunity.SupportTransform.IsValid() || Opportunity.CoverageSupports.IsEmpty()
			|| Opportunity.CoverageSupports.Num() > 64)
		{
			OutReport.FailureCode = TEXT("DecalOpportunityInvalid");
			OutReport.Message = TEXT("A physically verified decal opportunity lost its exact finite native support binding.");
			OutReport.bSpatialFailure = true; return false;
		}
		OpportunityIds.Add(Opportunity.Id); NativeOpportunityIds.Add(Opportunity.NativeOpportunityId);
		for (const FEFCalystoDecalCoverageSupport& Coverage : Opportunity.CoverageSupports)
			if (!Coverage.Support.IsValid() || Coverage.InstanceIndex < INDEX_NONE || !Coverage.SupportTransform.IsValid()
				|| !Finite(Coverage.Point) || !Finite(Coverage.Normal)
				|| !FMath::IsNearlyEqual(Coverage.Normal.SizeSquared(), 1.0, 1e-6)
				|| FVector::DotProduct(Coverage.Normal, Opportunity.SurfaceNormal) < 0.999)
			{
				OutReport.FailureCode = TEXT("DecalCoverageInvalid");
				OutReport.Message = TEXT("A selected decal footprint lacks a valid exact collision witness.");
				OutReport.bSpatialFailure = true; return false;
			}
		if (FVector::DotProduct(Opportunity.WorldTransform.GetUnitAxis(EAxis::X), Opportunity.SurfaceNormal) < 0.99)
		{
			OutReport.FailureCode = TEXT("DecalOrientationInvalid"); OutReport.Message = TEXT("A decal opportunity no longer projects its X axis along its verified surface normal.");
			OutReport.bSpatialFailure = true; return false;
		}
		FEFCalystoDecals Effective;
		if (!Configuration.ResolveDecals(Random.StyleId, Opportunity.ThemeId, Effective, Error))
		{ OutReport.FailureCode = TEXT("DecalResolutionInvalid"); OutReport.Message = Error; return false; }
		Report.RequestedChancePercent = Effective.Mode == EEFCalystoDecalMode::Replace ? Effective.ChancePercent : 0.0;
		if (Effective.Mode != EEFCalystoDecalMode::Replace || Effective.MaximumPerRoom == 0 || Effective.ActiveFloorBudget == 0
			|| SurfaceLimit(Effective, Opportunity.Surface) == 0 || MaximumSize(Effective) > Opportunity.MaximumSizeCm)
			continue;
		FRoomCandidate Candidate; Candidate.Opportunity = &Opportunity; Candidate.Effective = Effective;
		for (const FEFCalystoDecalVariant& Variant : Effective.Variants)
			if (Variant.Selection.bEnabled && Variant.Selection.Weight > 0
				&& FEFCalystoDirectorProbability::IsEligible(Variant.Selection, Random.FloorNumber, {})
				&& SupportsSurface(Variant, Opportunity.Surface)) Candidate.Variants.Add({Variant.Selection.Id, Variant.Selection.Weight});
		if (Candidate.Variants.IsEmpty()) { Report.Outcome = EEFCalystoDecalOpportunityOutcome::NoCompatibleVariant; continue; }
		Rooms.FindOrAdd(Opportunity.RoomId).RoomId = Opportunity.RoomId;
		Rooms[Opportunity.RoomId].Candidates.Add(MoveTemp(Candidate));
	}

	TArray<FRoomPlan*> OrderedRooms;
	for (auto& Pair : Rooms) if (!Pair.Value.Candidates.IsEmpty()) OrderedRooms.Add(&Pair.Value);
	OrderedRooms.Sort([&](const FRoomPlan& A, const FRoomPlan& B)
	{
		const FGuid AI = FEFCalystoDirectorProbability::RoomIdentity(A.RoomId), BI = FEFCalystoDirectorProbability::RoomIdentity(B.RoomId);
		const uint64 ARank = FEFCalystoDirectorProbability::Hash(Random, EEFCalystoRandomDomain::Decal, AI, 0);
		const uint64 BRank = FEFCalystoDirectorProbability::Hash(Random, EEFCalystoRandomDomain::Decal, BI, 0);
		return ARank == BRank ? A.RoomId < B.RoomId : ARank < BRank;
	});
	OutReport.FeasibleRooms = OrderedRooms.Num();
	int32 GlobalSurfaceCounts[3] = {}; int32 GlobalCount = 0;
	TMap<FGuid, int32> ThemeCounts; TMap<FGuid, TArray<int32>> ThemeSurfaceCounts;
	TArray<FRoomCandidate> CapacityReserved;
	for (FRoomPlan* Room : OrderedRooms)
	{
		Room->Candidates.Sort([&](const FRoomCandidate& A, const FRoomCandidate& B)
		{
			const uint64 ARank = FEFCalystoDirectorProbability::Hash(Random, EEFCalystoRandomDomain::Placement, A.Opportunity->Id, 0);
			const uint64 BRank = FEFCalystoDirectorProbability::Hash(Random, EEFCalystoRandomDomain::Placement, B.Opportunity->Id, 0);
			return ARank == BRank ? GuidLess(A.Opportunity->Id, B.Opportunity->Id) : ARank < BRank;
		});
		bool bReserved = false;
		for (const FRoomCandidate& Candidate : Room->Candidates)
		{
			const int32 Surface = int32(Candidate.Opportunity->Surface);
			auto& ScopedSurfaces = ThemeSurfaceCounts.FindOrAdd(Candidate.Opportunity->ThemeId); if (ScopedSurfaces.IsEmpty()) ScopedSurfaces.Init(0, 3);
			if (GlobalCount >= Style->Decals.ActiveFloorBudget || GlobalSurfaceCounts[Surface] >= SurfaceLimit(Style->Decals, Candidate.Opportunity->Surface)
				|| ThemeCounts.FindRef(Candidate.Opportunity->ThemeId) >= Candidate.Effective.ActiveFloorBudget
				|| ScopedSurfaces[Surface] >= SurfaceLimit(Candidate.Effective, Candidate.Opportunity->Surface)) continue;
			++GlobalCount; ++GlobalSurfaceCounts[Surface]; ++ThemeCounts.FindOrAdd(Candidate.Opportunity->ThemeId); ++ScopedSurfaces[Surface];
			CapacityReserved.Add(Candidate); bReserved = true; break;
		}
		if (!bReserved)
			for (FEFCalystoDecalOpportunityReport& Report : OutReport.Opportunities)
				if (Report.RoomId == Room->RoomId && Report.Outcome == EEFCalystoDecalOpportunityOutcome::Ineligible)
					Report.Outcome = EEFCalystoDecalOpportunityOutcome::CapacityExcluded;
	}
	OutReport.CapacityReservedRooms = CapacityReserved.Num();
	for (const FRoomCandidate& Candidate : CapacityReserved)
	{
		const FEFCalystoDecalOpportunity& Opportunity = *Candidate.Opportunity;
		FEFCalystoDecalOpportunityReport* Report = OutReport.Opportunities.FindByPredicate([&](const auto& Item) { return Item.OpportunityId == Opportunity.Id; });
		if (!Report) { OutReport.FailureCode = TEXT("DecalReportIdentityLost"); OutReport.Message = TEXT("Decal reservation lost a selected opportunity report."); return false; }
		Report->bChanceRolled = true;
		if (!FEFCalystoDirectorProbability::RollChance(Candidate.Effective.ChancePercent,
			FEFCalystoDirectorProbability::Unit(Random, EEFCalystoRandomDomain::Decal, FEFCalystoDirectorProbability::RoomIdentity(Opportunity.RoomId), 1)))
		{ Report->Outcome = EEFCalystoDecalOpportunityOutcome::ChanceAbsent; continue; }
		FGuid VariantId;
		if (!FEFCalystoDirectorProbability::SelectWeighted(Candidate.Variants,
			FEFCalystoDirectorProbability::Unit(Random, EEFCalystoRandomDomain::Decal, Opportunity.Id, 2), VariantId, Error))
		{ OutReport.FailureCode = TEXT("DecalVariantSelectionInvalid"); OutReport.Message = Error; return false; }
		const FEFCalystoDecalVariant* Variant = Candidate.Effective.Variants.FindByPredicate([&](const auto& Item) { return Item.Selection.Id == VariantId; });
		double SizeCm = 0;
		if (!Variant || !FEFCalystoDirectorProbability::SampleFloat(Candidate.Effective.SizeCm,
			FEFCalystoDirectorProbability::Unit(Random, EEFCalystoRandomDomain::Decal, Opportunity.Id, 3), SizeCm, Error)
			|| SizeCm <= 0 || SizeCm > Opportunity.MaximumSizeCm)
		{ OutReport.FailureCode = TEXT("DecalSizeReservationInvalid"); OutReport.Message = Error.IsEmpty() ? TEXT("Selected decal size exceeded its proven support footprint.") : Error; OutReport.bSpatialFailure = true; return false; }
		FEFCalystoReservedDecal& Reserved = OutManifest.Elements.AddDefaulted_GetRef();
		Reserved.Id = DerivedId(Random, Opportunity, VariantId); Reserved.RoomId = Opportunity.RoomId;
		Reserved.OpportunityId = Opportunity.NativeOpportunityId; Reserved.ThemeId = Opportunity.ThemeId; Reserved.VariantId = VariantId;
		Reserved.Surface = Opportunity.Surface; Reserved.WorldTransform = Opportunity.WorldTransform; Reserved.Size = FVector(8.0, SizeCm, SizeCm);
		Reserved.SurfaceNormal = Opportunity.SurfaceNormal; Reserved.Support = Opportunity.Support; Reserved.InstanceIndex = Opportunity.InstanceIndex; Reserved.SupportTransform = Opportunity.SupportTransform;
		Reserved.FootprintSupports = Opportunity.CoverageSupports;
		OutManifest.Dependencies.AddUnique(Variant->Material.ToSoftObjectPath());
		OutManifest.Dependencies.AddUnique(Variant->ColorTexture.ToSoftObjectPath());
		OutManifest.Dependencies.AddUnique(Variant->NormalTexture.ToSoftObjectPath());
		Report->Outcome = EEFCalystoDecalOpportunityOutcome::Reserved; Report->SelectedVariant = VariantId;
	}
	OutManifest.Elements.Sort([](const FEFCalystoReservedDecal& A, const FEFCalystoReservedDecal& B) { return GuidLess(A.Id, B.Id); });
	OutManifest.Dependencies.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
	TSet<FGuid> ReservedIds;
	TArray<FString> Lines;
	for (const FEFCalystoReservedDecal& Reserved : OutManifest.Elements)
	{
		if (!Reserved.Id.IsValid() || ReservedIds.Contains(Reserved.Id))
		{ OutManifest = {}; OutReport.FailureCode = TEXT("DecalReservationIdentityInvalid"); OutReport.Message = TEXT("A selected decal identity was invalid or duplicated."); return false; }
		ReservedIds.Add(Reserved.Id);
		Lines.Add(FString::Printf(TEXT("%s|%lld|%lld|%s|%s|%d|%s|%s|%s|%s|%d|%s"),
			*GuidText(Reserved.Id), Reserved.RoomId, Reserved.OpportunityId, *GuidText(Reserved.ThemeId), *GuidText(Reserved.VariantId), int32(Reserved.Surface),
			*Reserved.WorldTransform.ToHumanReadableString(), *Reserved.Size.ToString(), *Reserved.SurfaceNormal.ToString(),
			*GetPathNameSafe(Reserved.Support.Get()), Reserved.InstanceIndex, *Reserved.SupportTransform.ToHumanReadableString()));
		for (const FEFCalystoDecalCoverageSupport& Coverage : Reserved.FootprintSupports)
			Lines.Add(FString::Printf(TEXT("coverage|%s|%d|%s|%s|%s"), *GetPathNameSafe(Coverage.Support.Get()), Coverage.InstanceIndex,
				*Coverage.SupportTransform.ToHumanReadableString(), *Coverage.Point.ToString(), *Coverage.Normal.ToString()));
	}
	for (const FSoftObjectPath& Dependency : OutManifest.Dependencies) Lines.Add(Dependency.ToString());
	Lines.Sort(); OutManifest.Hash = FMD5::HashAnsiString(*FString::Join(Lines, TEXT("\n")));
	OutManifest.bValid = true; OutReport.SelectedDecals = OutManifest.Elements.Num();
	OutReport.Message = TEXT("All selected decal opportunities were spatially and capacity-reserved before Chance; chance misses were not backfilled.");
	return true;
}
