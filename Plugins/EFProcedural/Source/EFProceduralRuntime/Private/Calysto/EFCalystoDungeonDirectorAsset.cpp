#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "Calysto/EFCalystoLightingResolver.h"

#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace EFCalystoDirectorAssetPrivate
{
bool GuidLess(const FGuid& A, const FGuid& B)
{
	if (A.A != B.A) return A.A < B.A;
	if (A.B != B.B) return A.B < B.B;
	if (A.C != B.C) return A.C < B.C;
	return A.D < B.D;
}

FString FieldAt(const FString& Field, const int32 Index)
{
	return FString::Printf(TEXT("%s[%d]"), *Field, Index);
}

struct FValidator
{
	TArray<FEFCalystoValidationIssue>& Issues;
	void Require(const bool bCondition, const FString& Field, const TCHAR* Message)
	{
		if (!bCondition) Issues.Add({Field, Message});
	}
	void Selection(const FEFCalystoSelection& S, const FString& Path, TSet<FGuid>& Seen)
	{
		Require(S.Id.IsValid() && !Seen.Contains(S.Id), Path + TEXT(".Id"), TEXT("Editor identity is missing or duplicated in this list."));
		Seen.Add(S.Id);
		Require(FMath::IsFinite(S.Weight) && S.Weight >= 0.0, Path + TEXT(".Weight"), TEXT("Weight must be finite and nonnegative."));
		Require(S.FirstEligibleFloor >= 1, Path + TEXT(".FirstEligibleFloor"), TEXT("First eligible floor must be positive."));
		Require(S.LastEligibleFloor == 0 || S.LastEligibleFloor >= S.FirstEligibleFloor, Path + TEXT(".LastEligibleFloor"), TEXT("Last eligible floor must be zero or at least the first floor."));
		Require(S.CooldownFloors >= 0, Path + TEXT(".CooldownFloors"), TEXT("Cooldown cannot be negative."));
	}
	void Traits(const FEFCalystoTraits& T, const FString& Path)
	{
		const double Values[] = {T.Mystery, T.Danger, T.Safe, T.Abundance, T.ClothingInfluence};
		const TCHAR* Names[] = {TEXT("Mystery"), TEXT("Danger"), TEXT("Safe"), TEXT("Abundance"), TEXT("ClothingInfluence")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
			Require(FMath::IsFinite(Values[Index]) && Values[Index] >= 0 && Values[Index] <= 1,
				Path + TEXT(".") + Names[Index], TEXT("A normalized trait must be finite within [0,1]; no implicit target or clamping."));
	}
	void Distribution(const FEFCalystoFloatDistribution& D, const double Minimum, const double Maximum, const FString& Path)
	{
		Require(FEFCalystoDirectorProbability::IsValid(D), Path, TEXT("Distribution must have finite ordered bounds and a supported peak."));
		const double Low = D.Distribution == EEFCalystoDistribution::Fixed ? D.Value : D.Minimum;
		const double High = D.Distribution == EEFCalystoDistribution::Fixed ? D.Value : D.Maximum;
		Require(Low >= Minimum && High <= Maximum, Path, TEXT("Distribution exceeds the supported value range."));
	}
	void Placement(const FEFCalystoPlacement& P, const FString& Path)
	{
		Require(static_cast<uint8>(P.Zone) <= static_cast<uint8>(EEFCalystoPlacementZone::Roof), Path + TEXT(".Zone"), TEXT("Unsupported placement zone."));
		Require(!P.FootprintHalfExtent.ContainsNaN() && P.FootprintHalfExtent.GetMin() > 0.0, Path + TEXT(".FootprintHalfExtent"), TEXT("Footprint extents must be finite and positive."));
		Require(FMath::IsFinite(P.Spacing) && P.Spacing >= 0.0, Path + TEXT(".Spacing"), TEXT("Spacing must be finite and nonnegative."));
		Require(FMath::IsFinite(P.Clearance) && P.Clearance >= 0.0, Path + TEXT(".Clearance"), TEXT("Clearance must be finite and nonnegative."));
		Require(FMath::IsFinite(P.PositionVariationCm) && P.PositionVariationCm >= 0.0, Path + TEXT(".PositionVariationCm"), TEXT("Position variation must be finite and nonnegative."));
	}
	void Transform(const FEFCalystoArchitectureTransform& T, const FString& Path)
	{
		Require(!T.LocationOffset.ContainsNaN(), Path + TEXT(".LocationOffset"), TEXT("Location must be finite."));
		Require(!T.RotationOffset.ContainsNaN(), Path + TEXT(".RotationOffset"), TEXT("Rotation must be finite."));
		Require(!T.Scale.ContainsNaN() && T.Scale.GetMin() > 0.0, Path + TEXT(".Scale"), TEXT("Scale must be finite and positive."));
	}
	void Variation(const FEFCalystoArchitectureVariation& V, const FString& Path)
	{
		const auto Ordered = [](const FVector& A, const FVector& B)
		{
			return !A.ContainsNaN() && !B.ContainsNaN() && A.X <= B.X && A.Y <= B.Y && A.Z <= B.Z;
		};
		Require(Ordered(V.LocationMinimum, V.LocationMaximum), Path + TEXT(".LocationMinimum"), TEXT("Location variation bounds must be finite and ordered."));
		Require(!V.RotationMinimum.ContainsNaN() && !V.RotationMaximum.ContainsNaN() &&
			V.RotationMinimum.Pitch <= V.RotationMaximum.Pitch && V.RotationMinimum.Yaw <= V.RotationMaximum.Yaw &&
			V.RotationMinimum.Roll <= V.RotationMaximum.Roll, Path + TEXT(".RotationMinimum"), TEXT("Rotation variation bounds must be finite and ordered."));
		Require(Ordered(V.ScaleMinimum, V.ScaleMaximum) && V.ScaleMinimum.GetMin() > 0.0,
			Path + TEXT(".ScaleMinimum"), TEXT("Scale variation bounds must be finite, ordered and positive."));
	}
	void Architecture(const TArray<FEFCalystoArchitectureEntry>& Entries, const FString& Path, const bool bRequired)
	{
		TSet<FGuid> Seen;
		bool bPositive = false;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FEFCalystoArchitectureEntry& E = Entries[Index];
			const FString At = FieldAt(Path, Index);
			Selection(E.Selection, At + TEXT(".Selection"), Seen);
			if (!E.Selection.bEnabled || E.Selection.Weight <= 0.0) continue;
			bPositive = true;
			Require(!bRequired || E.Payload != EEFCalystoArchitecturePayload::Empty, At + TEXT(".Payload"), TEXT("Required construction cannot select Empty."));
			switch (E.Payload)
			{
			case EEFCalystoArchitecturePayload::Mesh: Require(!E.Mesh.IsNull(), At + TEXT(".Mesh"), TEXT("Mesh payload requires a mesh.")); break;
			case EEFCalystoArchitecturePayload::Actor: Require(!E.ActorClass.IsNull(), At + TEXT(".ActorClass"), TEXT("Actor payload requires a class.")); break;
			case EEFCalystoArchitecturePayload::BakedPCG: Require(!E.BakedPCG.IsNull(), At + TEXT(".BakedPCG"), TEXT("Baked PCG payload requires a data asset.")); break;
			case EEFCalystoArchitecturePayload::Empty: break;
			default: Require(false, At + TEXT(".Payload"), TEXT("Unsupported architecture payload.")); break;
			}
			Transform(E.Transform, At + TEXT(".Transform"));
			Variation(E.Variation, At + TEXT(".Variation"));
			Require(static_cast<uint8>(E.Rotation) <= static_cast<uint8>(EEFCalystoArchitectureRotation::Full360),
				At + TEXT(".Rotation"), TEXT("Architecture rotation must be None, 45 Degrees, 90 Degrees or Full 360 Degrees."));
		}
		Require(!bRequired || bPositive, Path, TEXT("Required construction needs a positive eligible alternative."));
	}
	void Decoration(const TArray<FEFCalystoSurfaceDecoration>& Entries, const FString& Path)
	{
		TSet<uint8> Seen;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FEFCalystoSurfaceDecoration& D = Entries[Index];
			const FString At = FieldAt(Path, Index);
			const uint8 Zone = static_cast<uint8>(D.Zone);
			Require(Zone <= static_cast<uint8>(EEFCalystoPlacementZone::Roof) && !Seen.Contains(Zone), At + TEXT(".Zone"), TEXT("Decoration zones must be supported and unique in this scope."));
			Seen.Add(Zone);
			Require(FEFCalystoDirectorProbability::IsValid(D.Chance), At + TEXT(".Chance"), TEXT("Chance needs valid percent values and exact depth endpoints."));
			Architecture(D.Alternatives, At + TEXT(".Alternatives"), false);
		}
	}
	void Rarity(const FEFCalystoRarityCurve& C, const FString& Path)
	{
		Require(C.FirstFloor >= 1 && C.LastFloor >= C.FirstFloor, Path + TEXT(".LastFloor"), TEXT("Rarity depth endpoints must be ordered."));
		const double A[] = { C.FirstWeights.Common, C.FirstWeights.Uncommon, C.FirstWeights.Rare, C.FirstWeights.Epic, C.FirstWeights.Winter };
		const double B[] = { C.LastWeights.Common, C.LastWeights.Uncommon, C.LastWeights.Rare, C.LastWeights.Epic, C.LastWeights.Winter };
		const TCHAR* Names[] = { TEXT("Common"), TEXT("Uncommon"), TEXT("Rare"), TEXT("Epic"), TEXT("Winter") };
		for (int32 I = 0; I < 5; ++I)
		{
			Require(FMath::IsFinite(A[I]) && A[I] >= 0.0, Path + TEXT(".FirstWeights.") + Names[I], TEXT("Rarity weights must be finite and nonnegative."));
			Require(FMath::IsFinite(B[I]) && B[I] >= 0.0, Path + TEXT(".LastWeights.") + Names[I], TEXT("Rarity weights must be finite and nonnegative."));
			Require(C.FirstFloor != C.LastFloor || A[I] == B[I], Path + TEXT(".LastWeights.") + Names[I], TEXT("Coincident depth endpoints must have equal values."));
		}
	}
	void Content(const TArray<FEFCalystoContentGroup>& Groups, const FString& Path, const bool bStyle)
	{
		TSet<FGuid> GroupIds;
		TSet<uint8> Roles;
		for (int32 Index = 0; Index < Groups.Num(); ++Index)
		{
			const FEFCalystoContentGroup& G = Groups[Index];
			const FString At = FieldAt(Path, Index);
			Require(G.Id.IsValid() && !GroupIds.Contains(G.Id), At + TEXT(".Id"), TEXT("Content group identity is missing or duplicated."));
			GroupIds.Add(G.Id);
			const uint8 Role = static_cast<uint8>(G.Role);
			Require(Role <= static_cast<uint8>(EEFCalystoGameplayRole::SpecialEvent) && !Roles.Contains(Role), At + TEXT(".Role"), TEXT("A scope needs unique supported content roles."));
			Roles.Add(Role);
			Require(static_cast<uint8>(G.Budget) <= static_cast<uint8>(EEFCalystoBudgetMembership::SpecialEvent), At + TEXT(".Budget"), TEXT("Unsupported budget membership."));
			Require(static_cast<uint8>(G.Mode) <= static_cast<uint8>(EEFCalystoContentMode::Block), At + TEXT(".Mode"), TEXT("Unsupported content mode."));
			Require(!bStyle || G.Mode == EEFCalystoContentMode::Replace || G.Mode == EEFCalystoContentMode::Block, At + TEXT(".Mode"), TEXT("A Style category must Replace or Block; it has no parent Style."));
			if (G.Mode == EEFCalystoContentMode::Block || G.Mode == EEFCalystoContentMode::Inherit) continue;
			if (G.Mode == EEFCalystoContentMode::Replace || G.bOverrideChance)
				Require(FEFCalystoDirectorProbability::IsValid(G.Chance), At + TEXT(".Chance"), TEXT("Chance needs valid percent values and exact depth endpoints."));
			if (G.Mode == EEFCalystoContentMode::Replace || G.bOverrideAmount)
				Require(FEFCalystoDirectorProbability::IsValid(G.Amount), At + TEXT(".Amount"), TEXT("Amount must be a supported positive fixed, uniform or triangular count."));
			if (G.Mode == EEFCalystoContentMode::Replace || G.bOverrideRarity) Rarity(G.Rarity, At + TEXT(".Rarity"));
			Require(G.MaximumPerFloor >= 0, At + TEXT(".MaximumPerFloor"), TEXT("Capacity cannot be negative."));
			TSet<FGuid> Entries;
			for (int32 EntryIndex = 0; EntryIndex < G.Entries.Num(); ++EntryIndex)
			{
				const FEFCalystoContentEntry& E = G.Entries[EntryIndex];
				const FString EntryAt = FieldAt(At + TEXT(".Entries"), EntryIndex);
				Selection(E.Selection, EntryAt + TEXT(".Selection"), Entries);
				if (!E.Selection.bEnabled || E.Selection.Weight <= 0.0) continue;
				const bool bInventory = G.Role == EEFCalystoGameplayRole::ContainerContent;
				Require(bInventory ? !E.InventoryClass.IsNull() : !E.ActorClass.IsNull(), EntryAt + (bInventory ? TEXT(".InventoryClass") : TEXT(".ActorClass")), TEXT("Eligible content requires its typed payload resource."));
				Placement(E.Placement, EntryAt + TEXT(".Placement"));
				Require(static_cast<uint8>(E.Rarity) <= static_cast<uint8>(EEFCalystoRarity::Winter), EntryAt + TEXT(".Rarity"), TEXT("Unsupported rarity."));
				Require(static_cast<uint8>(E.Gender) <= static_cast<uint8>(EEFCalystoGender::Male), EntryAt + TEXT(".Gender"), TEXT("Unsupported gender."));
				Require(static_cast<uint8>(E.Lifecycle) <= static_cast<uint8>(EEFCalystoLifecycle::Recruitable), EntryAt + TEXT(".Lifecycle"), TEXT("Unsupported lifecycle."));
				Require(FMath::IsFinite(E.ThreatCost) && E.ThreatCost >= 0.0, EntryAt + TEXT(".ThreatCost"), TEXT("Threat cost must be finite and nonnegative."));
				Require(FMath::IsFinite(E.ResourceCost) && E.ResourceCost >= 0.0, EntryAt + TEXT(".ResourceCost"), TEXT("Resource cost must be finite and nonnegative."));
				Require(E.MaximumPerFloor >= 0, EntryAt + TEXT(".MaximumPerFloor"), TEXT("Entry capacity cannot be negative."));
				Require(E.MinimumLevelOffset <= E.MaximumLevelOffset, EntryAt + TEXT(".MaximumLevelOffset"), TEXT("Level offsets must be ordered."));
			}
		}
	}
	void Decals(const FEFCalystoDecals& D, const FString& Path, const bool bStyle)
	{
		Require(static_cast<uint8>(D.Mode) <= static_cast<uint8>(EEFCalystoDecalMode::Block), Path + TEXT(".Mode"), TEXT("Unsupported decal mode."));
		Require(!bStyle || D.Mode != EEFCalystoDecalMode::Inherit, Path + TEXT(".Mode"), TEXT("Style decals cannot inherit another Style."));
		if (D.Mode != EEFCalystoDecalMode::Replace) return;
		Require(FMath::IsFinite(D.ChancePercent) && D.ChancePercent >= 0.0 && D.ChancePercent <= 100.0, Path + TEXT(".ChancePercent"), TEXT("Decal chance must be a percentage from 0 to 100."));
		Require(D.MaximumPerRoom >= 0 && D.MaximumPerRoom <= 1, Path + TEXT(".MaximumPerRoom"), TEXT("The supported decal room capacity is zero or one."));
		Require(D.ActiveFloorBudget >= 0 && D.ActiveFloorBudget <= 24, Path + TEXT(".ActiveFloorBudget"), TEXT("Decal floor budget exceeds the pool capacity."));
		Require(D.FloorLimit >= 0, Path + TEXT(".FloorLimit"), TEXT("Surface capacity cannot be negative."));
		Require(D.WallLimit >= 0, Path + TEXT(".WallLimit"), TEXT("Surface capacity cannot be negative."));
		Require(D.RoofLimit >= 0, Path + TEXT(".RoofLimit"), TEXT("Surface capacity cannot be negative."));
		Distribution(D.SizeCm, 0.01, 10000.0, Path + TEXT(".SizeCm"));
		Require(FMath::IsFinite(D.CullDistanceCm) && D.CullDistanceCm >= 0.0, Path + TEXT(".CullDistanceCm"), TEXT("Cull distance must be finite and nonnegative."));
		Require(FMath::IsFinite(D.FadeScreenSize) && D.FadeScreenSize >= 0.0, Path + TEXT(".FadeScreenSize"), TEXT("Screen-size fading must be finite and nonnegative."));
		TSet<FGuid> Seen;
		for (int32 Index = 0; Index < D.Variants.Num(); ++Index)
		{
			const FEFCalystoDecalVariant& V = D.Variants[Index];
			const FString At = FieldAt(Path + TEXT(".Variants"), Index);
			Selection(V.Selection, At + TEXT(".Selection"), Seen);
			if (!V.Selection.bEnabled || V.Selection.Weight <= 0.0) continue;
			Require(!V.Material.IsNull(), At + TEXT(".Material"), TEXT("Decal variant requires a material."));
			Require(!V.ColorTexture.IsNull(), At + TEXT(".ColorTexture"), TEXT("Decal variant requires its declared color texture."));
			Require(!V.NormalTexture.IsNull(), At + TEXT(".NormalTexture"), TEXT("Decal variant requires its declared normal texture."));
			Require(V.bFloor || V.bWall || V.bRoof, At, TEXT("Decal variant must support at least one surface."));
		}
	}
};

void AddPath(const FSoftObjectPath& Path, TSet<FSoftObjectPath>& Paths)
{
	if (!Path.IsNull()) Paths.Add(Path);
}

void GatherArchitecture(const TArray<FEFCalystoArchitectureEntry>& Entries, TSet<FSoftObjectPath>& Paths, int64 Floor=0)
{
	for (const FEFCalystoArchitectureEntry& E : Entries)
	{
		if (!E.Selection.bEnabled || E.Selection.Weight <= 0.0) continue;
		if (Floor>0 && !FEFCalystoDirectorProbability::IsEligible(E.Selection,Floor,{})) continue;
		if (E.Payload == EEFCalystoArchitecturePayload::Mesh) AddPath(E.Mesh.ToSoftObjectPath(), Paths);
		else if (E.Payload == EEFCalystoArchitecturePayload::Actor) AddPath(E.ActorClass.ToSoftObjectPath(), Paths);
		else if (E.Payload == EEFCalystoArchitecturePayload::BakedPCG) AddPath(E.BakedPCG.ToSoftObjectPath(), Paths);
	}
}

void GatherDecoration(const TArray<FEFCalystoSurfaceDecoration>& Decoration, TSet<FSoftObjectPath>& Paths)
{
	for (const FEFCalystoSurfaceDecoration& D : Decoration)
		// This is the complete inspection inventory; an inherited Style chance can enable a zero local curve.
		GatherArchitecture(D.Alternatives, Paths);
}

/** CDO collision descriptors are eligibility inputs, so every potentially selectable
 * world actor must be resident before candidate feasibility and random decisions. */
void GatherContentCollision(const TArray<FEFCalystoContentGroup>& Groups, TSet<FSoftObjectPath>& Paths, const int64 Floor)
{
	for (const FEFCalystoContentGroup& Group : Groups)
	{
		if (Group.Mode == EEFCalystoContentMode::Block || Group.Mode == EEFCalystoContentMode::Inherit
			|| Group.Role == EEFCalystoGameplayRole::ContainerContent) continue;
		for (const FEFCalystoContentEntry& Entry : Group.Entries)
			if (Entry.Selection.bEnabled && Entry.Selection.Weight > 0.0
				&& FEFCalystoDirectorProbability::IsEligible(Entry.Selection, Floor, {}))
				AddPath(Entry.ActorClass.ToSoftObjectPath(), Paths);
	}
}

void CanonicalContent(TArray<FEFCalystoContentGroup>& Groups)
{
	Groups.Sort([](const FEFCalystoContentGroup& A, const FEFCalystoContentGroup& B) { return static_cast<uint8>(A.Role) < static_cast<uint8>(B.Role); });
	for (FEFCalystoContentGroup& G : Groups)
	{
		G.Entries.Sort([](const FEFCalystoContentEntry& A, const FEFCalystoContentEntry& B) { return GuidLess(A.Selection.Id, B.Selection.Id); });
		G.RemovedEntryIds.Sort([](const FGuid& A, const FGuid& B) { return GuidLess(A, B); });
	}
}
}

FPrimaryAssetId UEFCalystoDungeonDirectorAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("EFCalystoDungeonDirector"), GetFName());
}

bool UEFCalystoDungeonDirectorAsset::Compile(FEFCalystoCompiledDirector& OutConfiguration,
	TArray<FEFCalystoValidationIssue>& OutIssues) const
{
	using namespace EFCalystoDirectorAssetPrivate;
	OutConfiguration = FEFCalystoCompiledDirector();
	OutIssues.Reset();
	FValidator V{OutIssues};
	V.Require(SchemaVersion == CurrentSchemaVersion, TEXT("SchemaVersion"), TEXT("Unsupported internal Director schema."));
	V.Require(Dungeon.GuaranteedThemedRooms == 1, TEXT("Dungeon.GuaranteedThemedRooms"), TEXT("Exactly one independently ranked room is guaranteed."));
	V.Require(FMath::IsFinite(Dungeon.AdditionalRoomThemeChancePercent) && Dungeon.AdditionalRoomThemeChancePercent >= 0.0 && Dungeon.AdditionalRoomThemeChancePercent <= 100.0,
		TEXT("Dungeon.AdditionalRoomThemeChancePercent"), TEXT("Additional Room Theme Chance must be a percentage from 0 to 100."));
	V.Require(Advanced.MaximumAttempts == 4 && Advanced.RequestDeadlineSeconds == 30.0,
		TEXT("Advanced.RequestDeadlineSeconds"), TEXT("The supported initial transaction contract is four attempts in one 30-second request."));
	V.Require(Advanced.DecalPoolCapacity == 24, TEXT("Advanced.DecalPoolCapacity"), TEXT("The supported decal pool capacity is 24."));
	V.Require(Advanced.MaximumRoomRecords > 0 && Advanced.MaximumRoomRecords <= 2048, TEXT("Advanced.MaximumRoomRecords"), TEXT("Room records must remain bounded to 2048."));
	V.Require(FMath::IsFinite(Advanced.RoomIdentityQuantizationCm) && Advanced.RoomIdentityQuantizationCm > 0.0, TEXT("Advanced.RoomIdentityQuantizationCm"), TEXT("Room identity quantization must be finite and positive."));
	V.Require(FMath::IsFinite(Advanced.Adaptation.MaximumEffectPercent) && Advanced.Adaptation.MaximumEffectPercent >= 0.0 && Advanced.Adaptation.MaximumEffectPercent <= 100.0,
		TEXT("Advanced.Adaptation.MaximumEffectPercent"), TEXT("Adaptation effect must be bounded from 0 to 100 percent."));
	FString BindingField, BindingError;
	if (!FEFCalystoDirectorProbability::ValidateTraitBindings(Advanced.Adaptation, BindingField, BindingError))
		OutIssues.Add({TEXT("Advanced.Adaptation.") + BindingField, BindingError});
	for (int32 Index = 0; Index < FMath::Min(Advanced.Adaptation.Bindings.Num(), 64); ++Index)
	{
		const auto& Binding = Advanced.Adaptation.Bindings[Index];
		bool Found = false;
		const auto FindTarget = [&](const auto& Profiles)
		{
			for (const auto& Profile : Profiles) for (const auto& Group : Profile.Content)
				if (Group.Role == Binding.Role)
					Found |= Binding.Control == EEFCalystoTraitControl::Chance || Group.Entries.ContainsByPredicate(
						[&](const auto& Entry) { return Entry.Selection.Id == Binding.EntryId; });
		};
		FindTarget(Styles); FindTarget(RoomThemes);
		V.Require(Found, FieldAt(TEXT("Advanced.Adaptation.Bindings"), Index)
			+ (Binding.Control == EEFCalystoTraitControl::Chance ? TEXT(".Role") : TEXT(".EntryId")),
			TEXT("An explicit modifier must target an authored content role and, for Weight, an existing entry in that role."));
	}
	const FEFCalystoSafetyCeilings& H = Advanced.HardCeilings;
	const int32 Ceilings[] = {H.Enemies, H.LooseFood, H.Chests, H.LootActors, H.SpecialEvents, H.TotalActors};
	const TCHAR* CapacityNames[] = {TEXT("Enemies"), TEXT("LooseFood"), TEXT("Chests"), TEXT("LootActors"), TEXT("SpecialEvents"), TEXT("TotalActors")};
	for (int32 Capacity = 0; Capacity < UE_ARRAY_COUNT(Ceilings); ++Capacity)
		V.Require(Ceilings[Capacity] >= 0, FString(TEXT("Advanced.HardCeilings.")) + CapacityNames[Capacity], TEXT("A global safety ceiling must be nonnegative."));
	TSet<FGuid> StyleIds;
	bool bEnabledStyle = false;
	for (int32 Index = 0; Index < Styles.Num(); ++Index)
	{
		const FEFCalystoStyle& S = Styles[Index];
		const FString Path = FieldAt(TEXT("Styles"), Index);
		V.Selection(S.Selection, Path + TEXT(".Selection"), StyleIds);
		V.Traits(S.Traits, Path + TEXT(".Traits"));
		if (!S.Selection.bEnabled || S.Selection.Weight <= 0.0) continue;
		bEnabledStyle = true;
		V.Distribution(S.Layout.DungeonSize, 18.0, 30.0, Path + TEXT(".Layout.DungeonSize"));
		V.Distribution(S.Layout.CandidateDensity, 0.20, 0.50, Path + TEXT(".Layout.CandidateDensity"));
		V.Distribution(S.Layout.SidePathPercent, 30.0, 70.0, Path + TEXT(".Layout.SidePathPercent"));
		V.Require(S.Layout.MinimumRoomSize >= 4 && S.Layout.MaximumRoomSize <= 8 && S.Layout.MinimumRoomSize <= S.Layout.MaximumRoomSize,
			Path + TEXT(".Layout.MaximumRoomSize"), TEXT("Native room size must remain ordered within 4 to 8."));
		V.Require(!S.Materials.Floor.IsNull(), Path + TEXT(".Materials.Floor"), TEXT("Style Floor material is required; native fallback is forbidden."));
		V.Require(!S.Materials.Wall.IsNull(), Path + TEXT(".Materials.Wall"), TEXT("Style Wall material is required; native fallback is forbidden."));
		V.Require(!S.Materials.Roof.IsNull(), Path + TEXT(".Materials.Roof"), TEXT("Style Roof material is required; native fallback is forbidden."));
		const FEFCalystoStyleArchitecture& A = S.Architecture;
		V.Require(A.MaximumDecorationsPerRoom >= 0 && A.MaximumDecorationsPerRoom <= 256,
			Path + TEXT(".Architecture.MaximumDecorationsPerRoom"), TEXT("Optional decoration room capacity must be between 0 and 256 across all zones."));
		V.Require(FEFCalystoDirectorProbability::IsValid(A.DecorationChance),
			Path + TEXT(".Architecture.DecorationChance"), TEXT("Default optional-decoration Chance requires an ordered percentage curve."));
		V.Architecture(A.Floor, Path + TEXT(".Architecture.Floor"), true);
		V.Architecture(A.Wall, Path + TEXT(".Architecture.Wall"), true);
		V.Architecture(A.Roof, Path + TEXT(".Architecture.Roof"), true);
		V.Architecture(A.DoorFrames, Path + TEXT(".Architecture.DoorFrames"), !A.DoorFrames.IsEmpty());
		V.Architecture(A.Doors, Path + TEXT(".Architecture.Doors"), !A.Doors.IsEmpty());
		V.Architecture(A.RampTop, Path + TEXT(".Architecture.RampTop"), !A.RampTop.IsEmpty());
		V.Architecture(A.RampBottom, Path + TEXT(".Architecture.RampBottom"), !A.RampBottom.IsEmpty());
		TSet<FGuid> DoorIds;
		for (int32 DoorIndex = 0; DoorIndex < A.Doorways.Num(); ++DoorIndex)
		{
			const FEFCalystoDoorway& D = A.Doorways[DoorIndex];
			const FString At = FieldAt(Path + TEXT(".Architecture.Doorways"), DoorIndex);
			V.Selection(D.Selection, At + TEXT(".Selection"), DoorIds);
			if (!D.Selection.bEnabled || D.Selection.Weight <= 0.0) continue;
			V.Require(!D.WallMesh.IsNull(), At + TEXT(".WallMesh"), TEXT("Doorway requires its wall mesh."));
			V.Require(!D.FrameMesh.IsNull(), At + TEXT(".FrameMesh"), TEXT("Doorway requires its frame mesh."));
			V.Require(!D.DoorClass.IsNull(), At + TEXT(".DoorClass"), TEXT("Doorway requires its native door class."));
			V.Transform(D.WallTransform, At + TEXT(".WallTransform"));
			V.Transform(D.FrameTransform, At + TEXT(".FrameTransform"));
			V.Transform(D.DoorTransform, At + TEXT(".DoorTransform"));
		}
		V.Decoration(A.Decoration, Path + TEXT(".Architecture.Decoration"));
		FString LightingField, LightingError;
		if (!FEFCalystoLightingResolver::Validate(S.Lighting, LightingField, LightingError))
			OutIssues.Add({Path + TEXT(".Lighting.") + LightingField, LightingError});
		V.Placement(S.Lighting.Placement, Path + TEXT(".Lighting.Placement"));
		V.Architecture(S.Lighting.WallLights, Path + TEXT(".Lighting.WallLights"), false);
		V.Content(S.Content, Path + TEXT(".Content"), true);
		V.Decals(S.Decals, Path + TEXT(".Decals"), true);
		const FEFCalystoFloorBudgets& B = S.FloorBudgets;
		const int32 Capacities[] = {B.Enemies, B.LooseFood, B.Chests, B.LootActors, B.SpecialEvents, B.TotalActors};
		for (int32 Capacity = 0; Capacity < UE_ARRAY_COUNT(Capacities); ++Capacity)
			V.Require(Capacities[Capacity] >= 0 && Capacities[Capacity] <= Ceilings[Capacity],
				Path + TEXT(".FloorBudgets.") + CapacityNames[Capacity], TEXT("Capacity must be nonnegative and no greater than the matching Advanced.HardCeilings value."));
		V.Require(FEFCalystoDirectorProbability::IsValid(B.Threat) && B.Threat.FirstValue >= 0.0 && B.Threat.LastValue >= 0.0,
			Path + TEXT(".FloorBudgets.Threat"), TEXT("Threat needs a nonnegative exact linear depth curve."));
	}
	V.Require(bEnabledStyle, TEXT("Styles"), TEXT("At least one enabled positive-weight Style is required."));
	TSet<FGuid> ThemeIds;
	bool bEnabledTheme = false;
	for (int32 Index = 0; Index < RoomThemes.Num(); ++Index)
	{
		const FEFCalystoTheme& T = RoomThemes[Index];
		const FString Path = FieldAt(TEXT("RoomThemes"), Index);
		V.Selection(T.Selection, Path + TEXT(".Selection"), ThemeIds);
		V.Traits(T.Traits, Path + TEXT(".Traits"));
		if (!T.Selection.bEnabled || T.Selection.Weight <= 0.0) continue;
		bEnabledTheme = true;
		const FEFCalystoMaterialOverride* Surfaces[] = { &T.Materials.Floor, &T.Materials.Wall, &T.Materials.Roof };
		const TCHAR* Names[] = { TEXT("Floor"), TEXT("Wall"), TEXT("Roof") };
		for (int32 Surface = 0; Surface < 3; ++Surface)
		{
			const FString At = Path + TEXT(".Materials.") + Names[Surface];
			V.Require(Surfaces[Surface]->Mode == EEFCalystoMaterialMode::Inherit || Surfaces[Surface]->Mode == EEFCalystoMaterialMode::Override,
				At + TEXT(".Mode"), TEXT("Unsupported material resolution mode."));
			V.Require(Surfaces[Surface]->Mode != EEFCalystoMaterialMode::Override || !Surfaces[Surface]->Material.IsNull(),
				At + TEXT(".Material"), TEXT("An explicit material override cannot be empty."));
		}
		V.Decoration(T.Architecture, Path + TEXT(".Architecture"));
		V.Content(T.Content, Path + TEXT(".Content"), false);
		V.Decals(T.Decals, Path + TEXT(".Decals"), false);
	}
	V.Require(bEnabledTheme, TEXT("RoomThemes"), TEXT("At least one enabled positive-weight Theme is required for the guarantee."));
	if (!OutIssues.IsEmpty()) return false;

	FEFCalystoCompiledDirector Compiled;
	Compiled.Dungeon = Dungeon;
	Compiled.Advanced = Advanced;
	Compiled.Styles = Styles;
	Compiled.Themes = RoomThemes;
	Compiled.Styles.Sort([](const FEFCalystoStyle& A, const FEFCalystoStyle& B) { return GuidLess(A.Selection.Id, B.Selection.Id); });
	Compiled.Themes.Sort([](const FEFCalystoTheme& A, const FEFCalystoTheme& B) { return GuidLess(A.Selection.Id, B.Selection.Id); });
	TSet<FSoftObjectPath> Visuals;
	for (FEFCalystoStyle& S : Compiled.Styles)
	{
		CanonicalContent(S.Content);
		if (!S.Selection.bEnabled || S.Selection.Weight <= 0.0) continue;
		AddPath(S.Materials.Floor.ToSoftObjectPath(), Visuals);
		AddPath(S.Materials.Wall.ToSoftObjectPath(), Visuals);
		AddPath(S.Materials.Roof.ToSoftObjectPath(), Visuals);
		GatherArchitecture(S.Architecture.Floor, Visuals);
		GatherArchitecture(S.Architecture.Wall, Visuals);
		GatherArchitecture(S.Architecture.Roof, Visuals);
		GatherArchitecture(S.Architecture.DoorFrames, Visuals);
		GatherArchitecture(S.Architecture.Doors, Visuals);
		GatherArchitecture(S.Architecture.RampTop, Visuals);
		GatherArchitecture(S.Architecture.RampBottom, Visuals);
		GatherArchitecture(S.Lighting.WallLights, Visuals);
		GatherDecoration(S.Architecture.Decoration, Visuals);
		AddPath(S.Architecture.ProgressionDoorMesh.ToSoftObjectPath(), Visuals);
		for (const FEFCalystoDoorway& D : S.Architecture.Doorways)
		{
			if (!D.Selection.bEnabled || D.Selection.Weight <= 0.0) continue;
			AddPath(D.WallMesh.ToSoftObjectPath(), Visuals);
			AddPath(D.FrameMesh.ToSoftObjectPath(), Visuals);
			AddPath(D.DoorClass.ToSoftObjectPath(), Visuals);
		}
	}
	for (FEFCalystoTheme& T : Compiled.Themes)
	{
		CanonicalContent(T.Content);
		if (!T.Selection.bEnabled || T.Selection.Weight <= 0.0) continue;
		if (T.Materials.Floor.Mode == EEFCalystoMaterialMode::Override) AddPath(T.Materials.Floor.Material.ToSoftObjectPath(), Visuals);
		if (T.Materials.Wall.Mode == EEFCalystoMaterialMode::Override) AddPath(T.Materials.Wall.Material.ToSoftObjectPath(), Visuals);
		if (T.Materials.Roof.Mode == EEFCalystoMaterialMode::Override) AddPath(T.Materials.Roof.Material.ToSoftObjectPath(), Visuals);
		GatherDecoration(T.Architecture, Visuals);
	}
	Compiled.VisualDependencies = Visuals.Array();
	Compiled.VisualDependencies.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
	Compiled.bValid = true;
	// Resolve every reachable combination at compile time so unsupported inheritance cannot fail after a draw.
	for (const FEFCalystoStyle& S : Compiled.Styles)
	{
		if (!S.Selection.bEnabled || S.Selection.Weight <= 0.0) continue;
		for (const FEFCalystoTheme& T : Compiled.Themes)
		{
			if (!T.Selection.bEnabled || T.Selection.Weight <= 0.0) continue;
			TArray<FEFCalystoResolvedContentGroup> Effective;
			FString Error;
			if (!Compiled.ResolveContent(S.Selection.Id, T.Selection.Id, Effective, Error))
				OutIssues.Add({TEXT("RoomThemes.Content"), Error});
		}
	}
	if (!OutIssues.IsEmpty()) return false;
	OutConfiguration = MoveTemp(Compiled);
	return true;
}

bool UEFCalystoDungeonDirectorAsset::ValidateAuthoring(TArray<FString>& OutErrors) const
{
	FEFCalystoCompiledDirector Compiled;
	TArray<FEFCalystoValidationIssue> Issues;
	const bool bValid = Compile(Compiled, Issues);
	OutErrors.Reset();
	for (const FEFCalystoValidationIssue& Issue : Issues) OutErrors.Add(Issue.Field + TEXT(": ") + Issue.Message);
	return bValid;
}

TArray<FString> UEFCalystoDungeonDirectorAsset::GetAuthoringErrors() const
{
	TArray<FString> Errors;
	ValidateAuthoring(Errors);
	return Errors;
}

bool FEFCalystoCompiledDirector::GetReachableVisualDependencies(const FEFCalystoRandomKey& Key,
	TArray<FSoftObjectPath>& Paths, FString& Error) const
{
	using namespace EFCalystoDirectorAssetPrivate;
	Paths.Reset(); Error.Reset(); const auto* S=FindStyle(Key.StyleId);
	if (!bValid || !S || Key.FloorNumber<1 || !FEFCalystoDirectorProbability::IsEligible(S->Selection,Key.FloorNumber,{}))
	{ Error=TEXT("Reachable visuals require the selected eligible Style and a valid floor."); return false; }
	TSet<FSoftObjectPath> Visuals;
	const auto Architecture=[&](const TArray<FEFCalystoArchitectureEntry>& Entries) { GatherArchitecture(Entries,Visuals,Key.FloorNumber); };
	const auto Decoration=[&](const TArray<FEFCalystoSurfaceDecoration>& Zones)
	{
		if (S->Architecture.MaximumDecorationsPerRoom==0) return;
		for (const auto& Zone:Zones)
			if (FEFCalystoDirectorProbability::EvaluateLinearCurve(Zone.bOverrideDefaultChance
				? Zone.Chance : S->Architecture.DecorationChance,Key.FloorNumber)>0) Architecture(Zone.Alternatives);
	};
	AddPath(S->Materials.Floor.ToSoftObjectPath(),Visuals); AddPath(S->Materials.Wall.ToSoftObjectPath(),Visuals);
	AddPath(S->Materials.Roof.ToSoftObjectPath(),Visuals);
	Architecture(S->Architecture.Floor); Architecture(S->Architecture.Wall); Architecture(S->Architecture.Roof);
	Architecture(S->Architecture.DoorFrames); Architecture(S->Architecture.Doors);
	Architecture(S->Architecture.RampTop); Architecture(S->Architecture.RampBottom); Architecture(S->Lighting.WallLights);
	Decoration(S->Architecture.Decoration); AddPath(S->Architecture.ProgressionDoorMesh.ToSoftObjectPath(),Visuals);
	GatherContentCollision(S->Content, Visuals, Key.FloorNumber);
	for (const auto& Door:S->Architecture.Doorways)
		if (FEFCalystoDirectorProbability::IsEligible(Door.Selection,Key.FloorNumber,{}))
		{
			AddPath(Door.WallMesh.ToSoftObjectPath(),Visuals); AddPath(Door.FrameMesh.ToSoftObjectPath(),Visuals);
			AddPath(Door.DoorClass.ToSoftObjectPath(),Visuals);
		}
	for (const auto& T:Themes)
	{
		if (!FEFCalystoDirectorProbability::IsEligible(T.Selection,Key.FloorNumber,{})) continue;
		if (T.Materials.Floor.Mode==EEFCalystoMaterialMode::Override) AddPath(T.Materials.Floor.Material.ToSoftObjectPath(),Visuals);
		if (T.Materials.Wall.Mode==EEFCalystoMaterialMode::Override) AddPath(T.Materials.Wall.Material.ToSoftObjectPath(),Visuals);
		if (T.Materials.Roof.Mode==EEFCalystoMaterialMode::Override) AddPath(T.Materials.Roof.Material.ToSoftObjectPath(),Visuals);
		Decoration(T.Architecture);
		GatherContentCollision(T.Content, Visuals, Key.FloorNumber);
	}
	Paths=Visuals.Array(); Paths.Sort([](const auto& A,const auto& B) { return A.ToString()<B.ToString(); }); return true;
}

const FEFCalystoStyle* FEFCalystoCompiledDirector::FindStyle(const FGuid& Id) const
{
	return bValid ? Styles.FindByPredicate([&Id](const FEFCalystoStyle& Style) { return Style.Selection.Id == Id; }) : nullptr;
}

const FEFCalystoTheme* FEFCalystoCompiledDirector::FindTheme(const FGuid& Id) const
{
	return bValid ? Themes.FindByPredicate([&Id](const FEFCalystoTheme& Theme) { return Theme.Selection.Id == Id; }) : nullptr;
}

bool FEFCalystoCompiledDirector::SelectStyle(const FEFCalystoRandomKey& Key, const TConstArrayView<FGuid> CoolingDownIds,
	FGuid& OutStyleId, FString& OutError) const
{
	if (!bValid) { OutError = TEXT("Director configuration has not compiled."); return false; }
	TArray<FEFCalystoWeightedAlternative> Eligible;
	for (const FEFCalystoStyle& S : Styles)
		if (FEFCalystoDirectorProbability::IsEligible(S.Selection, Key.FloorNumber, CoolingDownIds)) Eligible.Add({ S.Selection.Id, S.Selection.Weight });
	return FEFCalystoDirectorProbability::SelectWeighted(Eligible,
		FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Style), OutStyleId, OutError);
}

bool FEFCalystoCompiledDirector::ResolveMaterials(const FGuid& StyleId, const FGuid& ThemeId,
	FEFCalystoSurfaceMaterials& OutMaterials, FString& OutError) const
{
	OutError.Reset();
	const FEFCalystoStyle* S = FindStyle(StyleId);
	const FEFCalystoTheme* T = ThemeId.IsValid() ? FindTheme(ThemeId) : nullptr;
	if (!S || (ThemeId.IsValid() && !T)) { OutError = TEXT("Material resolution requires a compiled Style and any selected Theme."); return false; }
	OutMaterials = S->Materials;
	if (T)
	{
		if (T->Materials.Floor.Mode == EEFCalystoMaterialMode::Override) OutMaterials.Floor = T->Materials.Floor.Material;
		if (T->Materials.Wall.Mode == EEFCalystoMaterialMode::Override) OutMaterials.Wall = T->Materials.Wall.Material;
		if (T->Materials.Roof.Mode == EEFCalystoMaterialMode::Override) OutMaterials.Roof = T->Materials.Roof.Material;
	}
	return true;
}

bool FEFCalystoCompiledDirector::ResolveContent(const FGuid& StyleId, const FGuid& ThemeId,
	TArray<FEFCalystoResolvedContentGroup>& OutContent, FString& OutError) const
{
	using namespace EFCalystoDirectorAssetPrivate;
	OutContent.Reset();
	OutError.Reset();
	const FEFCalystoStyle* S = FindStyle(StyleId);
	const FEFCalystoTheme* T = ThemeId.IsValid() ? FindTheme(ThemeId) : nullptr;
	if (!S || (ThemeId.IsValid() && !T)) { OutError = TEXT("Content resolution requires a compiled Style and any selected Theme."); return false; }
	TArray<FEFCalystoResolvedContentGroup> Result;
	for (const FEFCalystoContentGroup& G : S->Content)
		if (G.Mode != EEFCalystoContentMode::Block) Result.Add({G, G.MaximumPerFloor});
	if (T)
	{
		for (const FEFCalystoContentGroup& Overlay : T->Content)
		{
			const int32 Index = Result.IndexOfByPredicate([&Overlay](const FEFCalystoResolvedContentGroup& Group) { return Group.Content.Role == Overlay.Role; });
			if (Overlay.Mode == EEFCalystoContentMode::Block) { if (Index != INDEX_NONE) Result.RemoveAt(Index); continue; }
			if (Overlay.Mode == EEFCalystoContentMode::Inherit) continue;
			const FEFCalystoContentGroup* StyleGroup = S->Content.FindByPredicate([&Overlay](const FEFCalystoContentGroup& G) { return G.Role == Overlay.Role; });
			if (Overlay.Mode == EEFCalystoContentMode::Replace)
			{
				// The floor's typed actor budgets also apply when a Theme introduces a new category.
				const int32 FloorLimit = StyleGroup ? (StyleGroup->Mode == EEFCalystoContentMode::Block ? 0 : StyleGroup->MaximumPerFloor) : Overlay.MaximumPerFloor;
				if (Index == INDEX_NONE) Result.Add({Overlay, FloorLimit});
				else Result[Index] = {Overlay, FloorLimit};
				continue;
			}
			if (Index == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Theme '%s' Extend role %d has no active Style category in '%s'."),
					*T->Selection.DisplayName, static_cast<int32>(Overlay.Role), *S->Selection.DisplayName);
				return false;
			}
			FEFCalystoContentGroup& Effective = Result[Index].Content;
			if (Overlay.bOverrideChance) Effective.Chance = Overlay.Chance;
			if (Overlay.bOverrideAmount) Effective.Amount = Overlay.Amount;
			if (Overlay.bOverrideRarity) Effective.Rarity = Overlay.Rarity;
			for (const FGuid& Removed : Overlay.RemovedEntryIds)
				Effective.Entries.RemoveAll([&Removed](const FEFCalystoContentEntry& E) { return E.Selection.Id == Removed; });
			for (const FEFCalystoContentEntry& E : Overlay.Entries)
			{
				const int32 Existing = Effective.Entries.IndexOfByPredicate([&E](const FEFCalystoContentEntry& Candidate) { return Candidate.Selection.Id == E.Selection.Id; });
				if (Existing == INDEX_NONE) Effective.Entries.Add(E);
				else Effective.Entries[Existing] = E;
			}
			Effective.Entries.Sort([](const FEFCalystoContentEntry& A, const FEFCalystoContentEntry& B) { return GuidLess(A.Selection.Id, B.Selection.Id); });
		}
	}
	Result.Sort([](const FEFCalystoResolvedContentGroup& A, const FEFCalystoResolvedContentGroup& B) { return static_cast<uint8>(A.Content.Role) < static_cast<uint8>(B.Content.Role); });
	OutContent = MoveTemp(Result);
	return true;
}

bool FEFCalystoCompiledDirector::ResolveDecals(const FGuid& StyleId, const FGuid& ThemeId,
	FEFCalystoDecals& OutDecals, FString& OutError) const
{
	OutError.Reset();
	const FEFCalystoStyle* S = FindStyle(StyleId);
	const FEFCalystoTheme* T = ThemeId.IsValid() ? FindTheme(ThemeId) : nullptr;
	if (!S || (ThemeId.IsValid() && !T)) { OutError = TEXT("Decal resolution requires a compiled Style and any selected Theme."); return false; }
	OutDecals = T && T->Decals.Mode != EEFCalystoDecalMode::Inherit ? T->Decals : S->Decals;
	return true;
}

#if WITH_EDITOR
void UEFCalystoDungeonDirectorAsset::EnsureEditorIdentities()
{
	const auto Ensure = [](FGuid& Id, TSet<FGuid>& Seen)
	{
		if (!Id.IsValid() || Seen.Contains(Id)) Id = FGuid::NewGuid();
		Seen.Add(Id);
	};
	const auto Architecture = [&Ensure](TArray<FEFCalystoArchitectureEntry>& Entries)
	{
		TSet<FGuid> Seen;
		for (FEFCalystoArchitectureEntry& E : Entries) Ensure(E.Selection.Id, Seen);
	};
	const auto Decoration = [&Architecture](TArray<FEFCalystoSurfaceDecoration>& Entries)
	{
		for (FEFCalystoSurfaceDecoration& D : Entries) Architecture(D.Alternatives);
	};
	const auto Content = [&Ensure](TArray<FEFCalystoContentGroup>& Groups)
	{
		TSet<FGuid> GroupIds;
		for (FEFCalystoContentGroup& G : Groups)
		{
			Ensure(G.Id, GroupIds);
			TSet<FGuid> EntryIds;
			for (FEFCalystoContentEntry& E : G.Entries) Ensure(E.Selection.Id, EntryIds);
		}
	};
	const auto Decals = [&Ensure](FEFCalystoDecals& D)
	{
		TSet<FGuid> Seen;
		for (FEFCalystoDecalVariant& V : D.Variants) Ensure(V.Selection.Id, Seen);
	};
	TSet<FGuid> StyleIds;
	for (FEFCalystoStyle& S : Styles)
	{
		Ensure(S.Selection.Id, StyleIds);
		Architecture(S.Architecture.Floor); Architecture(S.Architecture.Wall); Architecture(S.Architecture.Roof);
		Architecture(S.Architecture.DoorFrames); Architecture(S.Architecture.Doors);
		Architecture(S.Architecture.RampTop); Architecture(S.Architecture.RampBottom);
		TSet<FGuid> DoorIds;
		for (FEFCalystoDoorway& D : S.Architecture.Doorways) Ensure(D.Selection.Id, DoorIds);
		Decoration(S.Architecture.Decoration); Architecture(S.Lighting.WallLights);
		Content(S.Content); Decals(S.Decals);
	}
	TSet<FGuid> ThemeIds;
	for (FEFCalystoTheme& T : RoomThemes)
	{
		Ensure(T.Selection.Id, ThemeIds);
		Decoration(T.Architecture); Content(T.Content); Decals(T.Decals);
	}
}

void UEFCalystoDungeonDirectorAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	EnsureEditorIdentities();
	Super::PostEditChangeProperty(Event);
}

void UEFCalystoDungeonDirectorAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& Event)
{
	EnsureEditorIdentities();
	Super::PostEditChangeChainProperty(Event);
}

EDataValidationResult UEFCalystoDungeonDirectorAsset::IsDataValid(FDataValidationContext& Context) const
{
	TArray<FString> Errors;
	if (!ValidateAuthoring(Errors))
	{
		for (const FString& Error : Errors) Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
