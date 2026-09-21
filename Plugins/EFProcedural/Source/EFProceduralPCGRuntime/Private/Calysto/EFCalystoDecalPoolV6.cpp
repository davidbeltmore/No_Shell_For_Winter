#include "Calysto/EFCalystoDecalPoolV6.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EFCalystoDecalPoolV6)

namespace EFCalystoDecalPlacementPrivate
{
	struct FOrderedRoom
	{
		const FEFCalystoRoomContextV6* Room = nullptr;
		double Priority = 0.0;
	};

	static int32 SurfaceBit(const EEFCalystoPCGDecalSurfaceV6 Surface)
	{
		switch (Surface)
		{
		case EEFCalystoPCGDecalSurfaceV6::Floor:
			return static_cast<int32>(EEFCalystoDecalSurfaceV6::Floor);
		case EEFCalystoPCGDecalSurfaceV6::Wall:
			return static_cast<int32>(EEFCalystoDecalSurfaceV6::Wall);
		case EEFCalystoPCGDecalSurfaceV6::Roof:
			return static_cast<int32>(EEFCalystoDecalSurfaceV6::Roof);
		default:
			return 0;
		}
	}

	static int32 SurfaceLimit(
		const FEFCalystoDecalProfileV6& Global,
		const FEFCalystoDecalProfileV6& Effective,
		const EEFCalystoPCGDecalSurfaceV6 Surface)
	{
		switch (Surface)
		{
		case EEFCalystoPCGDecalSurfaceV6::Floor:
			return FMath::Min(Global.FloorLimit, Effective.FloorLimit);
		case EEFCalystoPCGDecalSurfaceV6::Wall:
			return FMath::Min(Global.WallLimit, Effective.WallLimit);
		case EEFCalystoPCGDecalSurfaceV6::Roof:
			return FMath::Min(Global.RoofLimit, Effective.RoofLimit);
		default:
			return 0;
		}
	}

	static int32& SurfaceCount(
		const EEFCalystoPCGDecalSurfaceV6 Surface,
		int32& FloorCount,
		int32& WallCount,
		int32& RoofCount)
	{
		return Surface == EEFCalystoPCGDecalSurfaceV6::Floor
			? FloorCount
			: Surface == EEFCalystoPCGDecalSurfaceV6::Wall
				? WallCount
				: RoofCount;
	}

	static const FEFCalystoResolvedThemeProfileV6* FindTheme(
		const FEFCalystoResolvedFloorPlanV6& Plan,
		const FName ThemeId)
	{
		return Plan.Themes.FindByPredicate([ThemeId](const FEFCalystoResolvedThemeProfileV6& Theme)
		{
			return Theme.ThemeId == ThemeId;
		});
	}

	static const FEFCalystoDecalVariantV6* SelectVariant(
		const FEFCalystoDecalProfileV6& Profile,
		const EEFCalystoPCGDecalSurfaceV6 Surface,
		const double Uniform)
	{
		TArray<const FEFCalystoDecalVariantV6*> Variants;
		double TotalWeight = 0.0;
		const int32 RequiredBit = SurfaceBit(Surface);
		for (const FEFCalystoDecalVariantV6& Variant : Profile.Catalog)
		{
			if ((Variant.AllowedSurfaces & RequiredBit) != 0
				&& !Variant.StableId.IsNone()
				&& !Variant.Material.IsNull()
				&& FMath::IsFinite(Variant.SelectionWeight)
				&& Variant.SelectionWeight > 0.0f)
			{
				Variants.Add(&Variant);
				TotalWeight += Variant.SelectionWeight;
			}
		}
		Variants.Sort([](const FEFCalystoDecalVariantV6& Left, const FEFCalystoDecalVariantV6& Right)
		{
			return Left.StableId.ToString().ToLower() < Right.StableId.ToString().ToLower();
		});
		if (Variants.IsEmpty() || !FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
		{
			return nullptr;
		}

		double Target = FMath::Clamp(Uniform, 0.0, 1.0 - UE_DOUBLE_SMALL_NUMBER) * TotalWeight;
		for (const FEFCalystoDecalVariantV6* Variant : Variants)
		{
			Target -= Variant->SelectionWeight;
			if (Target < 0.0)
			{
				return Variant;
			}
		}
		return Variants.Last();
	}
}

bool FEFCalystoDecalPlacementMathV6::IsPlacementProtectedRoom(const int32 RoomFlags)
{
	const int32 ProtectedMask = static_cast<int32>(EEFCalystoRoomFlagsV6::Start)
		| static_cast<int32>(EEFCalystoRoomFlagsV6::End)
		| static_cast<int32>(EEFCalystoRoomFlagsV6::Critical)
		| static_cast<int32>(EEFCalystoRoomFlagsV6::Progression)
		| static_cast<int32>(EEFCalystoRoomFlagsV6::MainPath)
		| static_cast<int32>(EEFCalystoRoomFlagsV6::DoorClearance);
	return (RoomFlags & ProtectedMask) != 0;
}

double FEFCalystoDecalPlacementMathV6::UniformLane(
	const int64 FloorSeed,
	const int64 StableRoomId,
	const FName StyleId,
	const FName ThemeId,
	const TCHAR* Lane)
{
	if (StableRoomId <= 0 || StyleId.IsNone() || Lane == nullptr || Lane[0] == TCHAR('\0'))
	{
		return 0.0;
	}
	const FString Canonical = FString::Printf(
		TEXT("EFCalystoDecalV6|%lld|%lld|%s|%s|%s"),
		FloorSeed,
		StableRoomId,
		*StyleId.ToString().ToLower(),
		*ThemeId.ToString().ToLower(),
		Lane);
	const FString Hash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
	const uint64 Bits = FCString::Strtoui64(*Hash.Left(16), nullptr, 16);
	return static_cast<double>(Bits >> 11) * (1.0 / 9007199254740992.0);
}

bool FEFCalystoDecalPlacementMathV6::BuildCandidates(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FEFCalystoRoomManifestV6& Manifest,
	TArray<FEFCalystoDecalPlacementCandidateV6>& OutCandidates,
	FString& OutError)
{
	using namespace EFCalystoDecalPlacementPrivate;
	OutCandidates.Reset();
	OutError.Reset();
	if (FloorPlan.FloorPlanHash.IsEmpty()
		|| Manifest.FloorPlanHash != FloorPlan.FloorPlanHash
		|| Manifest.FloorSeed != FloorPlan.FloorSeed
		|| Manifest.StyleId != FloorPlan.StyleId
		|| Manifest.ManifestHash.IsEmpty())
	{
		OutError = TEXT("Calysto V6 decal placement requires a manifest from the supplied frozen floor plan.");
		return false;
	}
	const FEFCalystoDecalProfileV6& Global = FloorPlan.StyleDecals;
	if (Global.Mode == EEFCalystoDecalResolutionModeV6::Block
		|| Global.MaximumActivePerFloor <= 0
		|| Global.MaximumPerRoom <= 0)
	{
		return true;
	}
	if (!FMath::IsFinite(Global.ChancePerEligibleRoom)
		|| Global.ChancePerEligibleRoom < 0.0f
		|| Global.ChancePerEligibleRoom > 1.0f)
	{
		OutError = TEXT("The frozen Style decal probability is invalid.");
		return false;
	}

	TArray<FOrderedRoom> OrderedRooms;
	OrderedRooms.Reserve(Manifest.Rooms.Num());
	for (const FEFCalystoRoomContextV6& Room : Manifest.Rooms)
	{
		if (Room.StableRoomId > 0 && !IsPlacementProtectedRoom(Room.RoomFlags))
		{
			FOrderedRoom& Ordered = OrderedRooms.AddDefaulted_GetRef();
			Ordered.Room = &Room;
			Ordered.Priority = UniformLane(
				FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("BudgetOrder"));
		}
	}
	OrderedRooms.Sort([](const FOrderedRoom& Left, const FOrderedRoom& Right)
	{
		return Left.Priority == Right.Priority
			? Left.Room->StableRoomId < Right.Room->StableRoomId
			: Left.Priority < Right.Priority;
	});

	const int32 ActiveLimit = FMath::Min3(
		Global.MaximumActivePerFloor,
		FloorPlan.DecalComponentPoolCapacity,
		UEFCalystoDecalPoolComponentV6::HardMaximumDecals);
	int32 FloorCount = 0;
	int32 WallCount = 0;
	int32 RoofCount = 0;
	for (const FOrderedRoom& Ordered : OrderedRooms)
	{
		if (OutCandidates.Num() >= ActiveLimit)
		{
			break;
		}
		const FEFCalystoRoomContextV6& Room = *Ordered.Room;
		const FEFCalystoDecalProfileV6* Effective = &Global;
		if (Room.bIsThemed)
		{
			const FEFCalystoResolvedThemeProfileV6* Theme = FindTheme(FloorPlan, Room.ThemeId);
			if (!Theme || Theme->DecalHash != Room.DecalHash)
			{
				OutError = FString::Printf(
					TEXT("Room %lld references an unavailable or drifted resolved Theme decal profile."),
					Room.StableRoomId);
				return false;
			}
			Effective = &Theme->Decals;
		}
		else if (Room.DecalHash != FloorPlan.StyleDecalHash)
		{
			OutError = FString::Printf(TEXT("Room %lld has a drifted Style decal hash."), Room.StableRoomId);
			return false;
		}
		if (Effective->Mode == EEFCalystoDecalResolutionModeV6::Block
			|| FMath::Min(Global.MaximumPerRoom, Effective->MaximumPerRoom) <= 0
			|| Effective->ChancePerEligibleRoom <= 0.0f)
		{
			continue;
		}
		if (!FMath::IsFinite(Effective->ChancePerEligibleRoom)
			|| Effective->ChancePerEligibleRoom > 1.0f
			|| !FMath::IsFinite(Effective->MinimumSizeCm)
			|| !FMath::IsFinite(Effective->MaximumSizeCm)
			|| Effective->MinimumSizeCm <= 0.0f
			|| Effective->MaximumSizeCm < Effective->MinimumSizeCm
			|| !FMath::IsFinite(Effective->FadeStartDistanceCm)
			|| !FMath::IsFinite(Effective->CullDistanceCm)
			|| Effective->FadeStartDistanceCm <= 0.0f
			|| Effective->CullDistanceCm <= Effective->FadeStartDistanceCm)
		{
			OutError = FString::Printf(TEXT("Room %lld resolved an invalid decal placement profile."), Room.StableRoomId);
			return false;
		}
		if (UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("Presence"))
			>= Effective->ChancePerEligibleRoom)
		{
			continue;
		}

		TArray<EEFCalystoPCGDecalSurfaceV6, TInlineAllocator<3>> Surfaces;
		for (const EEFCalystoPCGDecalSurfaceV6 Surface : {
			EEFCalystoPCGDecalSurfaceV6::Floor,
			EEFCalystoPCGDecalSurfaceV6::Wall,
			EEFCalystoPCGDecalSurfaceV6::Roof})
		{
			int32& Count = SurfaceCount(Surface, FloorCount, WallCount, RoofCount);
			const int32 Limit = SurfaceLimit(Global, *Effective, Surface);
			const int32 Bit = SurfaceBit(Surface);
			if (Count < Limit && (Global.AllowedSurfaces & Bit) != 0
				&& (Effective->AllowedSurfaces & Bit) != 0
				&& SelectVariant(*Effective, Surface, 0.0) != nullptr)
			{
				Surfaces.Add(Surface);
			}
		}
		if (Surfaces.IsEmpty())
		{
			continue;
		}
		const double SurfaceRoll = UniformLane(
			FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("Surface"));
		const int32 SurfaceIndex = FMath::Min(
			FMath::FloorToInt(SurfaceRoll * Surfaces.Num()), Surfaces.Num() - 1);
		const EEFCalystoPCGDecalSurfaceV6 Surface = Surfaces[SurfaceIndex];
		const FEFCalystoDecalVariantV6* Variant = SelectVariant(
			*Effective,
			Surface,
			UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("Variant")));
		if (!Variant)
		{
			OutError = FString::Printf(TEXT("Room %lld selected a decal surface without a valid variant."), Room.StableRoomId);
			return false;
		}

		const FVector SourceExtents = Room.Extents.GetAbs();
		if (Room.LocalCenter.ContainsNaN() || SourceExtents.ContainsNaN()
			|| SourceExtents.X <= 1.0 || SourceExtents.Y <= 1.0)
		{
			// Decals are optional decoration. A malformed footprint must not abort an
			// otherwise valid floor; the immutable manifest remains authoritative.
			continue;
		}
		// Calysto's room-context points are legitimate planar footprints and can
		// carry zero Z bounds. Give decal traces a conservative vertical search
		// volume without mutating the room identity or its deterministic hash.
		constexpr double MinimumTraceHalfHeightCm = 500.0;
		const FVector Extents(
			SourceExtents.X,
			SourceExtents.Y,
			FMath::Max(SourceExtents.Z, MinimumTraceHalfHeightCm));
		const double OffsetA = UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("TransformA")) * 1.2 - 0.6;
		const double OffsetB = UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("TransformB")) * 1.0 - 0.5;
		const float TraceMargin = 25.0f;
		FEFCalystoDecalPlacementCandidateV6& Candidate = OutCandidates.AddDefaulted_GetRef();
		Candidate.StableRoomId = Room.StableRoomId;
		Candidate.VariantId = Variant->StableId;
		Candidate.Surface = Surface;
		Candidate.Material = Variant->Material;
		Candidate.SizeCm = FMath::Lerp(
			Effective->MinimumSizeCm,
			Effective->MaximumSizeCm,
			static_cast<float>(UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("Size"))));
		Candidate.RollDegrees = static_cast<float>(
			UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("Rotation")) * 360.0 - 180.0);
		Candidate.FadeStartDistanceCm = Effective->FadeStartDistanceCm;
		Candidate.CullDistanceCm = Effective->CullDistanceCm;
		Candidate.StableDecalId = FName(*FString::Printf(
			TEXT("EF.Decal.%016llX.%s"),
			static_cast<unsigned long long>(Room.StableRoomId),
			*Variant->StableId.ToString()));

		if (Surface == EEFCalystoPCGDecalSurfaceV6::Floor
			|| Surface == EEFCalystoPCGDecalSurfaceV6::Roof)
		{
			const FVector XY(Extents.X * OffsetA, Extents.Y * OffsetB, 0.0);
			Candidate.LocalTraceStart = Room.LocalCenter + XY;
			Candidate.LocalTraceEnd = Candidate.LocalTraceStart;
			if (Surface == EEFCalystoPCGDecalSurfaceV6::Floor)
			{
				Candidate.LocalTraceStart.Z += FMath::Max(TraceMargin, Extents.Z * 0.25);
				Candidate.LocalTraceEnd.Z -= Extents.Z + TraceMargin;
			}
			else
			{
				Candidate.LocalTraceStart.Z -= FMath::Max(TraceMargin, Extents.Z * 0.25);
				Candidate.LocalTraceEnd.Z += Extents.Z + TraceMargin;
			}
		}
		else
		{
			Candidate.LocalTraceStart = Room.LocalCenter;
			Candidate.LocalTraceStart.Z += Extents.Z * OffsetB;
			Candidate.LocalTraceEnd = Candidate.LocalTraceStart;
			const int32 Side = FMath::Min(3, FMath::FloorToInt(
				UniformLane(FloorPlan.FloorSeed, Room.StableRoomId, Room.StyleId, Room.ThemeId, TEXT("WallSide")) * 4.0));
			if (Side < 2)
			{
				Candidate.LocalTraceStart.Y += Extents.Y * OffsetA;
				Candidate.LocalTraceEnd.X += (Side == 0 ? 1.0 : -1.0) * (Extents.X + TraceMargin);
			}
			else
			{
				Candidate.LocalTraceStart.X += Extents.X * OffsetA;
				Candidate.LocalTraceEnd.Y += (Side == 2 ? 1.0 : -1.0) * (Extents.Y + TraceMargin);
			}
		}
		++SurfaceCount(Surface, FloorCount, WallCount, RoofCount);
	}
	return true;
}

UEFCalystoDecalPoolComponentV6::UEFCalystoDecalPoolComponentV6()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetComponentTickEnabled(false);
	SetIsReplicatedByDefault(false);
}

void UEFCalystoDecalPoolComponentV6::OnRegister()
{
	Super::OnRegister();
	MaximumDecals = FMath::Clamp(MaximumDecals, 0, HardMaximumDecals);
	EnsurePoolSize(MaximumDecals);
}

void UEFCalystoDecalPoolComponentV6::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	for (UDecalComponent* Decal : DecalComponents)
	{
		if (IsValid(Decal))
		{
			Decal->DestroyComponent();
		}
	}
	DecalComponents.Reset();
	ActiveSlots.Reset();
	SlotGenerations.Reset();
	StableDecalIds.Reset();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UEFCalystoDecalPoolComponentV6::InitializePool(const int32 InMaximumDecals)
{
	if (!IsInGameThread() || InMaximumDecals < 0 || InMaximumDecals > HardMaximumDecals)
	{
		return false;
	}
	ReleaseAllDecals();
	MaximumDecals = InMaximumDecals;
	return EnsurePoolSize(MaximumDecals);
}

bool UEFCalystoDecalPoolComponentV6::EnsurePoolSize(const int32 TargetSize)
{
	if (!IsInGameThread() || TargetSize < 0 || TargetSize > HardMaximumDecals || !IsValid(GetOwner()))
	{
		return false;
	}

	while (DecalComponents.Num() > TargetSize)
	{
		const int32 LastIndex = DecalComponents.Num() - 1;
		if (IsValid(DecalComponents[LastIndex]))
		{
			DecalComponents[LastIndex]->DestroyComponent();
		}
		DecalComponents.RemoveAt(LastIndex, 1, EAllowShrinking::No);
		ActiveSlots.RemoveAt(LastIndex, 1, EAllowShrinking::No);
		SlotGenerations.RemoveAt(LastIndex, 1, EAllowShrinking::No);
		StableDecalIds.RemoveAt(LastIndex, 1, EAllowShrinking::No);
	}

	while (DecalComponents.Num() < TargetSize)
	{
		AActor* Owner = GetOwner();
		const int32 SlotIndex = DecalComponents.Num();
		const FName ComponentName = MakeUniqueObjectName(
			Owner,
			UDecalComponent::StaticClass(),
			FName(*FString::Printf(TEXT("EFCalystoPooledDecalV6_%02d"), SlotIndex)));
		UDecalComponent* Decal = NewObject<UDecalComponent>(Owner, ComponentName, RF_Transient);
		if (!IsValid(Decal))
		{
			return false;
		}
		Owner->AddInstanceComponent(Decal);
		Decal->SetupAttachment(this);
		Decal->SetMobility(EComponentMobility::Movable);
		Decal->SetCanEverAffectNavigation(false);
		Decal->PrimaryComponentTick.bCanEverTick = false;
		Decal->PrimaryComponentTick.bStartWithTickEnabled = false;
		Decal->SetVisibility(false, true);
		Decal->SetHiddenInGame(true, true);
		Decal->SetAutoActivate(false);
		if (IsRegistered())
		{
			Decal->RegisterComponent();
		}
		DecalComponents.Add(Decal);
		ActiveSlots.Add(0);
		SlotGenerations.Add(0);
		StableDecalIds.Add(NAME_None);
	}
	return DecalComponents.Num() == TargetSize;
}

bool UEFCalystoDecalPoolComponentV6::AcquireDecal(
	const FEFCalystoPooledDecalRequestV6& Request,
	FEFCalystoPooledDecalHandleV6& OutHandle)
{
	OutHandle.Reset();
	if (!IsInGameThread()
		|| Request.StableDecalId.IsNone()
		|| !IsValid(Request.Material.Get())
		|| Request.WorldTransform.ContainsNaN()
		|| Request.DecalSize.ContainsNaN()
		|| Request.DecalSize.GetMin() <= 0.0
		|| !FMath::IsFinite(Request.FadeStartDelay)
		|| !FMath::IsFinite(Request.FadeDuration)
		|| !FMath::IsFinite(Request.FadeStartDistanceCm)
		|| !FMath::IsFinite(Request.CullDistanceCm)
		|| Request.FadeStartDelay < 0.0f
		|| Request.FadeDuration < 0.0f
		|| Request.FadeStartDistanceCm <= 0.0f
		|| Request.CullDistanceCm <= Request.FadeStartDistanceCm)
	{
		return false;
	}
	if (!EnsurePoolSize(MaximumDecals))
	{
		return false;
	}

	// Stable IDs make repeated completion callbacks idempotent without allocating.
	for (int32 Index = 0; Index < ActiveSlots.Num(); ++Index)
	{
		if (ActiveSlots[Index] != 0 && StableDecalIds[Index] == Request.StableDecalId)
		{
			OutHandle.SlotIndex = Index;
			OutHandle.SlotGeneration = SlotGenerations[Index];
			return true;
		}
	}

	int32 FreeIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ActiveSlots.Num(); ++Index)
	{
		if (ActiveSlots[Index] == 0)
		{
			FreeIndex = Index;
			break;
		}
	}
	if (FreeIndex == INDEX_NONE || !DecalComponents.IsValidIndex(FreeIndex) || !IsValid(DecalComponents[FreeIndex]))
	{
		return false;
	}

	UDecalComponent* Decal = DecalComponents[FreeIndex];
	if (!Decal->IsRegistered() && IsRegistered())
	{
		Decal->RegisterComponent();
	}
	Decal->SetWorldTransform(Request.WorldTransform);
	Decal->DecalSize = Request.DecalSize;
	Decal->SetDecalMaterial(Request.Material.Get());
	// Deferred decals expose renderer-side screen fading rather than a primitive
	// max-draw-distance. Derive a non-zero threshold from the mandatory distance
	// contract so culling remains render-thread driven with no tick or MID.
	const float RadiusCm = FMath::Max(Request.DecalSize.Y, Request.DecalSize.Z);
	const float FadeStartRatio = RadiusCm / Request.FadeStartDistanceCm;
	const float CullRatio = RadiusCm / Request.CullDistanceCm;
	const float DistanceFadeScreenSize = FMath::Clamp(
		FMath::Lerp(CullRatio, FadeStartRatio, 0.5f),
		0.0001f,
		0.25f);
	Decal->SetFadeScreenSize(FMath::Max(FadeScreenSize, DistanceFadeScreenSize));
	Decal->SetSortOrder(SharedSortOrder);
	Decal->SetFadeOut(0.0f, 0.0f, false);
	Decal->SetHiddenInGame(false, true);
	Decal->SetVisibility(true, true);
	Decal->Activate(false);
	Decal->SetComponentTickEnabled(false);

	ActiveSlots[FreeIndex] = 1;
	StableDecalIds[FreeIndex] = Request.StableDecalId;
	int32& Generation = SlotGenerations[FreeIndex];
	Generation = Generation == MAX_int32 ? 1 : FMath::Max(1, Generation + 1);
	OutHandle.SlotIndex = FreeIndex;
	OutHandle.SlotGeneration = Generation;
	return true;
}

bool UEFCalystoDecalPoolComponentV6::ReleaseDecal(const FEFCalystoPooledDecalHandleV6& Handle)
{
	if (!IsInGameThread()
		|| !Handle.IsValid()
		|| !ActiveSlots.IsValidIndex(Handle.SlotIndex)
		|| ActiveSlots[Handle.SlotIndex] == 0
		|| SlotGenerations[Handle.SlotIndex] != Handle.SlotGeneration)
	{
		return false;
	}
	DeactivateSlot(Handle.SlotIndex);
	return true;
}

void UEFCalystoDecalPoolComponentV6::ReleaseAllDecals()
{
	if (!IsInGameThread())
	{
		return;
	}
	for (int32 Index = 0; Index < ActiveSlots.Num(); ++Index)
	{
		if (ActiveSlots[Index] != 0)
		{
			DeactivateSlot(Index);
		}
	}
}

void UEFCalystoDecalPoolComponentV6::DeactivateSlot(const int32 SlotIndex)
{
	if (!ActiveSlots.IsValidIndex(SlotIndex))
	{
		return;
	}
	if (DecalComponents.IsValidIndex(SlotIndex) && IsValid(DecalComponents[SlotIndex]))
	{
		UDecalComponent* Decal = DecalComponents[SlotIndex];
		Decal->SetVisibility(false, true);
		Decal->SetHiddenInGame(true, true);
		Decal->Deactivate();
		Decal->SetDecalMaterial(nullptr);
		Decal->SetComponentTickEnabled(false);
	}
	ActiveSlots[SlotIndex] = 0;
	StableDecalIds[SlotIndex] = NAME_None;
}

int32 UEFCalystoDecalPoolComponentV6::GetActiveDecalCount() const
{
	int32 Count = 0;
	for (const uint8 bActive : ActiveSlots)
	{
		Count += bActive != 0 ? 1 : 0;
	}
	return Count;
}

UDecalComponent* UEFCalystoDecalPoolComponentV6::GetDecalComponent(
	const FEFCalystoPooledDecalHandleV6& Handle) const
{
	return Handle.IsValid()
		&& ActiveSlots.IsValidIndex(Handle.SlotIndex)
		&& ActiveSlots[Handle.SlotIndex] != 0
		&& SlotGenerations[Handle.SlotIndex] == Handle.SlotGeneration
		&& DecalComponents.IsValidIndex(Handle.SlotIndex)
		? DecalComponents[Handle.SlotIndex].Get()
		: nullptr;
}

AEFCalystoDecalPoolOwnerV6::AEFCalystoDecalPoolOwnerV6()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	DecalPool = CreateDefaultSubobject<UEFCalystoDecalPoolComponentV6>(TEXT("CalystoDecalPoolV6"));
	SetRootComponent(DecalPool);
}
