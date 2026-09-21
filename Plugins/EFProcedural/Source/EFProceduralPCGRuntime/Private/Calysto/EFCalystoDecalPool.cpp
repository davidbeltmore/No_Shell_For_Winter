#include "Calysto/EFCalystoDecalPool.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/SecureHash.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	bool Owned(UPrimitiveComponent* C, AActor* Owner, UPCGComponent* PCG)
	{
		if (!IsValid(C) || !IsValid(Owner) || !IsValid(PCG) || PCG->GetOwner() != Owner
			|| C->GetWorld() != Owner->GetWorld() || !PCG->AreManagedResourcesAccessible()) return false;
		if (C->GetOwner() == Owner && C->ComponentHasTag(TEXT("PCG Generated Component"))) return true;
		bool Found = false;
		PCG->ForEachConstManagedResource([&](const UPCGManagedResource* R)
		{
			if (!R || R->IsMarkedUnused()) return;
			if (const auto* Single = Cast<UPCGManagedComponent>(R)) Found |= Single->GeneratedComponent.Get() == C;
			else if (const auto* List = Cast<UPCGManagedComponentList>(R))
				for (const auto& Item : List->GeneratedComponents) Found |= Item.Get() == C;
			else if (const auto* Actors = Cast<UPCGManagedActors>(R))
				for (const auto& Actor : Actors->GetConstGeneratedActors()) Found |= Actor.Get() == C->GetOwner();
		});
		return Found;
	}
	bool SupportWitness(UWorld* World, AActor* Owner, UPCGComponent* PCG,
		UPrimitiveComponent* C, const int32 InstanceIndex, const FTransform& ExpectedTransform,
		const FVector& Point, const FVector& Normal)
	{
		if (!World || !Owned(C, Owner, PCG) || !C->IsRegistered() || !C->IsQueryCollisionEnabled()
			|| !C->IsPhysicsStateCreated() || C->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block) return false;
		FTransform Transform = C->GetComponentTransform(); FBox Bounds = C->Bounds.GetBox();
		if (const auto* ISM = Cast<UInstancedStaticMeshComponent>(C))
		{
			if (!ISM->GetStaticMesh() || InstanceIndex < 0 || InstanceIndex >= ISM->GetInstanceCount()
				|| !ISM->GetInstanceTransform(InstanceIndex, Transform, true)) return false;
			Bounds = ISM->GetStaticMesh()->GetBoundingBox().TransformBy(Transform);
		}
		else if (InstanceIndex != INDEX_NONE) return false;
		if (!Transform.Equals(ExpectedTransform, 1e-6) || !Bounds.IsValid || !Bounds.ExpandBy(1.0).IsInsideOrOn(Point)) return false;
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CalystoReservedDecalSupport), false);
		if (!World->LineTraceSingleByChannel(Hit, Point + Normal * 8.0, Point - Normal * 8.0,
			ECC_Visibility, Query) || Hit.GetComponent() != C || (InstanceIndex != INDEX_NONE && Hit.Item != InstanceIndex)
			|| FVector::DistSquared(Hit.ImpactPoint, Point) > 1.0
			|| FVector::DotProduct(Hit.ImpactNormal, Normal) < 0.99) return false;
		return true;
	}
	bool Support(UWorld* World, AActor* Owner, UPCGComponent* PCG, const FEFCalystoReservedDecal& P, FString& Error)
	{
		auto Fail = [&]() { Error = TEXT("Reserved decal support ownership, transform, bounds or exact physical footprint traces changed."); return false; };
		if (!SupportWitness(World, Owner, PCG, P.Support.Get(), P.InstanceIndex, P.SupportTransform,
			P.WorldTransform.GetLocation(), P.SurfaceNormal)) return Fail();
		if (P.FootprintSupports.IsEmpty() || P.FootprintSupports.Num() > 64) return Fail();
		for (const FEFCalystoDecalCoverageSupport& Coverage : P.FootprintSupports)
		{
			if (!Coverage.Support.IsValid() || Coverage.InstanceIndex < INDEX_NONE || Coverage.Point.ContainsNaN()
				|| Coverage.Normal.ContainsNaN() || !FMath::IsNearlyEqual(Coverage.Normal.SizeSquared(), 1.0, 1e-6)
				|| FVector::DotProduct(Coverage.Normal, P.SurfaceNormal) < 0.999
				|| !SupportWitness(World, Owner, PCG, Coverage.Support.Get(), Coverage.InstanceIndex,
					Coverage.SupportTransform, Coverage.Point, Coverage.Normal)) return Fail();
		}
		return true;
	}
	bool SurfaceMatches(EEFCalystoDecalSurface Surface, EEFCalystoPlacementZone Zone)
	{
		return Surface == EEFCalystoDecalSurface::Floor ? Zone == EEFCalystoPlacementZone::Floor
			: Surface == EEFCalystoDecalSurface::Roof ? Zone == EEFCalystoPlacementZone::Roof
			: Surface == EEFCalystoDecalSurface::Wall && (Zone == EEFCalystoPlacementZone::WallBottom
				|| Zone == EEFCalystoPlacementZone::WallMiddle || Zone == EEFCalystoPlacementZone::WallTop);
	}
	int32 SurfaceLimit(const FEFCalystoDecals& D, EEFCalystoDecalSurface Surface)
	{ return Surface == EEFCalystoDecalSurface::Floor ? D.FloorLimit : Surface == EEFCalystoDecalSurface::Wall ? D.WallLimit : D.RoofLimit; }
}

AEFCalystoDecalPoolOwner::AEFCalystoDecalPoolOwner()
{
	bReplicates = false; PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DecalPoolRoot")));
	for (int32 Index = 0; Index < PoolCapacity; ++Index)
	{
		auto* C = CreateDefaultSubobject<UDecalComponent>(*FString::Printf(TEXT("PooledDecal_%02d"), Index));
		C->SetupAttachment(RootComponent); C->SetMobility(EComponentMobility::Movable);
		C->PrimaryComponentTick.bCanEverTick = false; C->SetAutoActivate(false);
		C->SetHiddenInGame(true); C->SetVisibility(false); C->SetCanEverAffectNavigation(false);
		Slots.Add(C);
	}
}

bool AEFCalystoDecalPoolOwner::Begin(const FEFCalystoAttemptToken& Token, const FEFCalystoCompiledDirector& Configuration,
	TSharedRef<const FEFCalystoNativeAdapter> Native, TConstArrayView<FEFCalystoReservedDecal> Proposals,
	TConstArrayView<UObject*> LoadedResources, FString& Error)
{
	FEFCalystoNativeResult Observed;
	if (Native->Observe(Observed, Error) != EEFCalystoNativeStatus::Complete)
	{ if (Error.IsEmpty()) Error = TEXT("Decal staging requires completed native observation."); return false; }
	if (!BeginObserved(Token, Configuration, Native->GetRoomConfig(), Native->GetDungeonActor(), Native->GetComponent(),
		Observed, Proposals, LoadedResources, Error)) return false;
	NativeLease = Native;
	return true;
}

bool AEFCalystoDecalPoolOwner::BeginObserved(const FEFCalystoAttemptToken& Token, const FEFCalystoCompiledDirector& Configuration,
	const FEFCalystoNativeRoomConfig& RoomConfig, AActor* InNativeOwner, UPCGComponent* InControlledPCG, const FEFCalystoNativeResult& Native,
	TConstArrayView<FEFCalystoReservedDecal> Proposals, TConstArrayView<UObject*> LoadedResources, FString& Error)
{
	Error.Reset(); auto Fail = [&](const TCHAR* Message) { Error = Message; return false; };
	const auto* Style = Configuration.FindStyle(RoomConfig.StyleId);
	if (!IsInGameThread() || !GetWorld() || !GetWorld()->IsGameWorld() || !Token.Request.IsValid() || !Token.Attempt.IsValid()
		|| !Configuration.IsValid() || !Style || !Style->Selection.bEnabled || Style->Selection.Weight <= 0
		|| Configuration.GetAdvanced().DecalPoolCapacity != PoolCapacity || RoomConfig.FloorNumber < 1
		|| !IsValid(InNativeOwner) || InNativeOwner->GetWorld() != GetWorld() || !IsValid(InControlledPCG) || InControlledPCG->GetOwner() != InNativeOwner
		|| !InControlledPCG->IsRegistered() || InControlledPCG->IsGenerating() || InControlledPCG->IsCleaningUp() || !Native.bGraphCompleted
		|| Native.GenerateRequests != 1 || Native.Rooms.Num() > 2048 || Native.Surfaces.Num() > 100000
		|| Proposals.Num() > PoolCapacity || LoadedResources.Num() > PoolCapacity * 3 || Slots.Num() != PoolCapacity)
		return Fail(TEXT("Decal request ownership, native phase, configuration or bounded input is invalid."));
	for (const UDecalComponent* C : Slots) if (!IsValid(C) || !C->IsRegistered() || C->GetOwner() != this)
		return Fail(TEXT("All reusable decal components must exist and be registered."));
	TArray<FEFCalystoReservedDecal> Sorted; Sorted.Append(Proposals.GetData(), Proposals.Num());
	Sorted.Sort([](const auto& A, const auto& B) { return A.Id.ToString() < B.Id.ToString(); });
	TMap<FSoftObjectPath, UObject*> Resources;
	for (UObject* Object : LoadedResources)
	{
		if (!IsValid(Object) || Resources.Contains(FSoftObjectPath(Object))) return Fail(TEXT("Explicit loaded resource bindings must be unique and valid."));
		Resources.Add(FSoftObjectPath(Object), Object);
	}
	TSet<FSoftObjectPath> UsedResources; TSet<FGuid> Ids; TSet<int64> Rooms, Opportunities;
	TMap<FGuid, int32> ScopeCounts; TMap<FGuid, TArray<int32>> ScopeSurfaces;
	int32 SurfaceCounts[3] = {}; TArray<FBinding> Proposed;
	TArray<uint8> Bytes; FMemoryWriter Seal(Bytes); FGuid RequestId = Token.Request, AttemptId = Token.Attempt;
	Seal << RequestId << AttemptId; FString OwnerPath = InNativeOwner->GetPathName(), PCGPath = InControlledPCG->GetPathName(); Seal << OwnerPath << PCGPath;
	FGuid StyleId = RoomConfig.StyleId; int64 FloorNumber = RoomConfig.FloorNumber; Seal << StyleId << FloorNumber;
	if (Proposals.Num() > Style->Decals.ActiveFloorBudget) return Fail(TEXT("Reserved decals exceed the selected Style's shared floor capacity."));
	for (const auto& P : Sorted)
	{
		const auto* Room = Native.Rooms.FindByPredicate([&](const auto& R) { return R.RoomId == P.RoomId; });
		const auto* Opportunity = Native.Surfaces.FindByPredicate([&](const auto& S) { return S.RoomId == P.RoomId && S.OpportunityId == P.OpportunityId; });
		if (!P.Id.IsValid() || Ids.Contains(P.Id) || P.RoomId <= 0 || Rooms.Contains(P.RoomId) || Opportunities.Contains(P.OpportunityId)
			|| !Room || !Opportunity || Room->StyleId != RoomConfig.StyleId || Room->ThemeId != P.ThemeId
			|| Room->Protection != EEFCalystoProtectedRoom::None || Room->bMainPath || Room->bDoorClearance
			|| !SurfaceMatches(P.Surface, Opportunity->Zone) || !Room->LocalBounds.IsValid)
			return Fail(TEXT("Reserved decal lacks a unique unprotected native room/opportunity with matching typed identity."));
		if (P.WorldTransform.ContainsNaN() || !P.WorldTransform.GetRotation().IsNormalized()
			|| !P.WorldTransform.GetScale3D().Equals(FVector::OneVector, 1e-6) || P.Size.ContainsNaN() || P.Size.GetMin() <= 0
			|| P.SurfaceNormal.ContainsNaN() || !FMath::IsNearlyEqual(P.SurfaceNormal.SizeSquared(), 1.0, 1e-6)
			|| FMath::Abs(FVector::DotProduct(P.WorldTransform.GetUnitAxis(EAxis::X), P.SurfaceNormal)) < 0.99
			|| (P.Surface == EEFCalystoDecalSurface::Floor && P.SurfaceNormal.Z < 0.7)
			|| (P.Surface == EEFCalystoDecalSurface::Roof && P.SurfaceNormal.Z > -0.7)
			|| (P.Surface == EEFCalystoDecalSurface::Wall && FMath::Abs(P.SurfaceNormal.Z) > 0.3))
			return Fail(TEXT("Reserved decal transform, positive size or typed surface normal is invalid."));
		// Every footprint corner must remain in the assigned room's XY footprint. Planar room records are valid.
		for (int32 Y : {-1, 1}) for (int32 Z : {-1, 1})
		{
			const FVector Local = Room->Transform.InverseTransformPosition(P.WorldTransform.TransformPosition(FVector(0, Y * P.Size.Y, Z * P.Size.Z)));
			if (Local.X < Room->LocalBounds.Min.X || Local.X > Room->LocalBounds.Max.X
				|| Local.Y < Room->LocalBounds.Min.Y || Local.Y > Room->LocalBounds.Max.Y)
				return Fail(TEXT("Reserved decal footprint crosses its owned room boundary."));
		}
		if (!Support(GetWorld(), InNativeOwner, InControlledPCG, P, Error)) return false;
		FEFCalystoDecals Effective;
		if (!Configuration.ResolveDecals(RoomConfig.StyleId, P.ThemeId, Effective, Error)) return false;
		const auto* Variant = Effective.Variants.FindByPredicate([&](const auto& V) { return V.Selection.Id == P.VariantId; });
		const int32 Surface = int32(P.Surface);
		auto& Scoped = ScopeSurfaces.FindOrAdd(P.ThemeId); if (Scoped.IsEmpty()) Scoped.Init(0, 3);
		if (Effective.Mode != EEFCalystoDecalMode::Replace || Effective.MaximumPerRoom < 1 || Effective.MaximumPerRoom > 1
			|| ++ScopeCounts.FindOrAdd(P.ThemeId) > Effective.ActiveFloorBudget
			|| ++Scoped[Surface] > SurfaceLimit(Effective, P.Surface) || ++SurfaceCounts[Surface] > SurfaceLimit(Style->Decals, P.Surface)
			|| !Variant || !Variant->Selection.bEnabled || Variant->Selection.Weight <= 0
			|| RoomConfig.FloorNumber < Variant->Selection.FirstEligibleFloor
			|| (Variant->Selection.LastEligibleFloor > 0 && RoomConfig.FloorNumber > Variant->Selection.LastEligibleFloor)
			|| !(P.Surface == EEFCalystoDecalSurface::Floor ? Variant->bFloor : P.Surface == EEFCalystoDecalSurface::Wall ? Variant->bWall : Variant->bRoof))
			return Fail(TEXT("Selected decal violates resolved mode, variant eligibility or shared room/floor/surface capacities."));
		const double SizeMin = Effective.SizeCm.Distribution == EEFCalystoDistribution::Fixed ? Effective.SizeCm.Value : Effective.SizeCm.Minimum;
		const double SizeMax = Effective.SizeCm.Distribution == EEFCalystoDistribution::Fixed ? Effective.SizeCm.Value : Effective.SizeCm.Maximum;
		if (P.Size.Y != P.Size.Z || P.Size.Y < SizeMin || P.Size.Y > SizeMax || P.Size.X > P.Size.Y
			|| !FMath::IsFinite(Effective.CullDistanceCm * Effective.CullDistanceCm) || Effective.CullDistanceCm < 0
			|| !FMath::IsFinite(float(Effective.FadeScreenSize)) || Effective.FadeScreenSize < 0)
			return Fail(TEXT("Reserved size or culling/fading value is outside its exact supported configuration."));
		const FSoftObjectPath Paths[] = {Variant->Material.ToSoftObjectPath(), Variant->ColorTexture.ToSoftObjectPath(), Variant->NormalTexture.ToSoftObjectPath()};
		for (const auto& Path : Paths)
		{
			UObject* const* Resource = Resources.Find(Path);
			if (!Resource || Path.ResolveObject() != *Resource) return Fail(TEXT("A selected decal dependency lacks its exact already-loaded shared binding."));
			UsedResources.Add(Path);
		}
		auto* Material = Cast<UMaterialInterface>(Resources[Paths[0]]);
		if (!Material || Material->IsA<UMaterialInstanceDynamic>() || !Material->GetMaterial()
			|| Material->GetMaterial()->MaterialDomain != MD_DeferredDecal
			|| !Cast<UTexture2D>(Resources[Paths[1]]) || !Cast<UTexture2D>(Resources[Paths[2]]))
			return Fail(TEXT("Selected payload requires a shared deferred-decal material and its declared textures; no dynamic instance or fallback."));
		FBinding& B = Proposed.AddDefaulted_GetRef(); B.Proposal = P; B.Material = Material;
		B.CullDistance = Effective.CullDistanceCm; B.ScreenFade = float(Effective.FadeScreenSize);
		auto Copy = P; FString SupportPath = P.Support->GetPathName(); FString MaterialPath = Material->GetPathName();
		Seal << Copy.Id << Copy.RoomId << Copy.OpportunityId << Copy.ThemeId << Copy.VariantId << Copy.WorldTransform
			<< Copy.Size << Copy.SurfaceNormal << Copy.SupportTransform << Copy.InstanceIndex << SupportPath << MaterialPath;
		int32 CoverageCount = Copy.FootprintSupports.Num(); Seal << CoverageCount;
		for (const FEFCalystoDecalCoverageSupport& Coverage : Copy.FootprintSupports)
		{
			FString CoveragePath = Coverage.Support->GetPathName();
			int32 CoverageInstance = Coverage.InstanceIndex;
			FTransform CoverageTransform = Coverage.SupportTransform;
			FVector CoveragePoint = Coverage.Point, CoverageNormal = Coverage.Normal;
			Seal << CoverageInstance << CoverageTransform << CoveragePoint << CoverageNormal << CoveragePath;
		}
		uint8 SurfaceByte = uint8(P.Surface); Seal << SurfaceByte << B.CullDistance << B.ScreenFade;
		for (const auto& Path : Paths) { FString Name = Path.ToString(); Seal << Name; }
		Ids.Add(P.Id); Rooms.Add(P.RoomId); Opportunities.Add(P.OpportunityId);
	}
	if (UsedResources.Num() != Resources.Num()) return Fail(TEXT("Resource lease contains unselected dependencies."));
	FMD5 Hash; Hash.Update(Bytes.GetData(), Bytes.Num()); uint8 Digest[16]; Hash.Final(Digest); const FString NewHash = BytesToHex(Digest, 16);
	if (bLeased) return LeaseToken == Token && ProposalHash == NewHash ? Verify(Token, Error) : Fail(TEXT("An active decal lease cannot be replaced or changed before verified release."));
	if (ReleasedToken == Token) return Fail(TEXT("A released attempt token cannot reacquire pooled presentation."));
	NativeOwner = InNativeOwner; ControlledPCG = InControlledPCG; LeaseToken = Token; Bindings = MoveTemp(Proposed); ProposalHash = NewHash;
	for (const auto& Pair : Resources) ResourceLeases.Add(Pair.Value);
	bLeased = true; bCommitted = false; Failure.Reset();
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		auto* C = Slots[Index].Get(); const auto& B = Bindings[Index];
		C->SetWorldTransform(B.Proposal.WorldTransform); C->DecalSize = B.Proposal.Size; C->SetDecalMaterial(B.Material.Get());
		C->SetFadeScreenSize(B.ScreenFade); C->SetFadeOut(0, 0, false); C->SetFadeIn(0, 0); C->SetSortOrder(0);
		C->SetDecalColor(FLinearColor::White); C->SetVisibility(false); C->SetHiddenInGame(true); C->Deactivate();
	}
	if (!Verify(Token, Error)) { Release(Token); return false; }
	return true;
}

bool AEFCalystoDecalPoolOwner::OwnsSupport(UPrimitiveComponent* Component) const
{ return Owned(Component, NativeOwner.Get(), ControlledPCG.Get()); }
bool AEFCalystoDecalPoolOwner::VerifySupport(const FEFCalystoReservedDecal& Proposal, FString& Error) const
{ return Support(GetWorld(), NativeOwner.Get(), ControlledPCG.Get(), Proposal, Error); }

bool AEFCalystoDecalPoolOwner::Verify(const FEFCalystoAttemptToken& Token, FString& Error) const
{
	Error.Reset();
	if (!IsInGameThread() || !bLeased || !(Token == LeaseToken) || !Failure.IsEmpty() || !NativeOwner.IsValid()
		|| !ControlledPCG.IsValid() || ControlledPCG->IsGenerating() || ControlledPCG->IsCleaningUp() || Slots.Num() != PoolCapacity)
	{ Error = Failure.IsEmpty() ? TEXT("Decal lease token or native ownership is no longer valid.") : Failure; return false; }
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const auto* C = Slots[Index].Get();
		if (!IsValid(C) || C->GetOwner() != this || !C->IsRegistered() || C->CanEverAffectNavigation())
		{ Error = TEXT("A reusable decal component is missing or has changed ownership/registration/navigation."); return false; }
		if (Index >= Bindings.Num())
		{
			if (C->GetDecalMaterial() || C->IsVisible() || !C->bHiddenInGame)
			{ Error = TEXT("An unleased pool slot has unexpected material or presentation."); return false; }
			continue;
		}
		const auto& B = Bindings[Index];
		if (!B.Material.IsValid() || C->GetDecalMaterial() != B.Material.Get() || !C->GetComponentTransform().Equals(B.Proposal.WorldTransform, 1e-6)
			|| C->DecalSize != B.Proposal.Size || C->FadeScreenSize != B.ScreenFade || C->SortOrder != 0
			|| C->FadeDuration != 0 || C->FadeStartDelay != 0 || C->FadeInDuration != 0 || C->FadeInStartDelay != 0
			|| C->bDestroyOwnerAfterFade || C->DecalColor != FLinearColor::White
			|| C->IsVisible() != (bCommitted && B.bVisible) || bool(C->bHiddenInGame) == (bCommitted && B.bVisible))
		{ Error = TEXT("A realized decal no longer matches its exact sealed payload, transform, fade or visibility."); return false; }
		if (!VerifySupport(B.Proposal, Error)) return false;
	}
	for (const UObject* Resource : ResourceLeases) if (!IsValid(Resource))
	{ Error = TEXT("A selected shared decal resource lease was lost."); return false; }
	return true;
}

bool AEFCalystoDecalPoolOwner::ActivateCommitted(const FEFCalystoAttemptToken& Token, FString& Error)
{
	if (!Verify(Token, Error)) return false;
	bCommitted = true; SetActorTickEnabled(!Bindings.IsEmpty());
	// No camera sample has yet authorized presentation. Tick observes actual local views.
	return true;
}

bool AEFCalystoDecalPoolOwner::UpdateViewLocations(const FEFCalystoAttemptToken& Token, TConstArrayView<FVector> Views, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !bLeased || !(Token == LeaseToken) || !Failure.IsEmpty() || Views.Num() > 4)
	{ Error = TEXT("Decal view observation has a stale token, failed lease or exceeds four local views."); return false; }
	for (const auto& View : Views) if (View.ContainsNaN()) { Error = TEXT("Decal view locations must be finite."); return false; }
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		auto* C = Slots[Index].Get(); if (!IsValid(C)) { Error = TEXT("A leased decal component disappeared."); return false; }
		auto& B = Bindings[Index]; B.bVisible = false;
		if (bCommitted) for (const auto& View : Views)
			B.bVisible |= FVector::DistSquared(View, B.Proposal.WorldTransform.GetLocation()) <= B.CullDistance * B.CullDistance;
		C->SetVisibility(B.bVisible); C->SetHiddenInGame(!B.bVisible);
		if (B.bVisible) C->Activate(false); else C->Deactivate();
	}
	return true;
}

void AEFCalystoDecalPoolOwner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLeased || !bCommitted || !GetWorld()) return;
	TArray<FVector, TInlineAllocator<4>> Views;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const auto* PC = It->Get();
		if (IsValid(PC) && PC->IsLocalPlayerController() && IsValid(PC->PlayerCameraManager)) Views.Add(PC->PlayerCameraManager->GetCameraLocation());
		if (Views.Num() > 4) break;
	}
	FString Error;
	if (!UpdateViewLocations(LeaseToken, Views, Error))
	{
		Failure = Error; SetActorTickEnabled(false);
		for (UDecalComponent* C : Slots) if (IsValid(C)) { C->SetVisibility(false); C->SetHiddenInGame(true); C->Deactivate(); }
	}
}

void AEFCalystoDecalPoolOwner::ClearSlots()
{
	SetActorTickEnabled(false);
	for (UDecalComponent* C : Slots) if (IsValid(C))
	{
		C->SetVisibility(false); C->SetHiddenInGame(true); C->Deactivate(); C->SetDecalMaterial(nullptr);
		C->SetFadeOut(0, 0, false); C->SetFadeIn(0, 0); C->SetDecalColor(FLinearColor::White);
		C->SetFadeScreenSize(0); C->SetSortOrder(0);
	}
	Bindings.Reset(); ResourceLeases.Reset(); NativeLease.Reset(); NativeOwner.Reset(); ControlledPCG.Reset();
	ProposalHash.Reset(); Failure.Reset(); bLeased = bCommitted = false;
}

bool AEFCalystoDecalPoolOwner::Release(const FEFCalystoAttemptToken& Token)
{
	if (!IsInGameThread()) return false;
	if (!bLeased) return ReleasedToken == Token;
	if (!(LeaseToken == Token)) return false;
	ClearSlots(); ReleasedToken = Token; LeaseToken = {}; return true;
}

FEFCalystoDecalReleaseEvidence AEFCalystoDecalPoolOwner::GetReleaseEvidence(const FEFCalystoAttemptToken& Token) const
{
	FEFCalystoDecalReleaseEvidence Result;
	Result.AllocatedSlots = Slots.Num(); Result.LeasedSlots = Bindings.Num(); Result.RetainedResources = ResourceLeases.Num();
	Result.bAllSlotsHiddenAndCleared = true;
	for (const UDecalComponent* C : Slots) if (IsValid(C)) Result.bAllSlotsHiddenAndCleared &= !C->IsVisible() && C->bHiddenInGame && !C->GetDecalMaterial();
	Result.bReleased = ReleasedToken == Token && Token.Request.IsValid() && Token.Attempt.IsValid() && !bLeased
		&& !Result.LeasedSlots && !Result.RetainedResources && Result.bAllSlotsHiddenAndCleared;
	return Result;
}

void AEFCalystoDecalPoolOwner::EndPlay(const EEndPlayReason::Type Reason)
{ ClearSlots(); Super::EndPlay(Reason); }
