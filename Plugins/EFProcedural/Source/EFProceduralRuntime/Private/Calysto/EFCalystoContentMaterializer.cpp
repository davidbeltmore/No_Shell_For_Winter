#include "Calysto/EFCalystoContentMaterializer.h"
#include "Calysto/EFCalystoContentCollisionContract.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"

namespace EFCalystoContentRealizationPrivate
{
	bool ValidToken(const FEFCalystoAttemptToken& T) { return T.Request.IsValid() && T.Attempt.IsValid(); }
	bool InvalidClock(const double Now, const double LastNow)
	{ return !FMath::IsFinite(Now) || Now < 0 || Now < LastNow; }
	FString Guid(const FGuid& Id) { return Id.ToString(EGuidFormats::Digits); }
	bool SameIds(const TSet<FGuid>& A, const TSet<FGuid>& B)
	{ if (A.Num()!=B.Num()) return false; for (const FGuid& Id:A) if (!B.Contains(Id)) return false; return true; }
	FString HashLines(TArray<FString> Lines)
	{ Lines.Sort(); return FMD5::HashAnsiString(*FString::Join(Lines,TEXT("\n"))); }
	bool ExactPose(const FTransform& A, const FTransform& B)
	{
		return !A.ContainsNaN() && A.GetRotation().IsNormalized()
			&& A.GetLocation().Equals(B.GetLocation(),0.1)
			&& FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation()))<=0.01
			&& A.GetScale3D().Equals(B.GetScale3D(),0.0001);
	}
	bool NativeActorOwner(const AActor* Actor,const FEFCalystoContentGameplayContext& Context)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->GetWorld()!=Context.World.Get()) return false;
		if (Actor->GetOwner()==Context.AttemptOwner.Get()) return true;
		const APawn* Pawn=Cast<APawn>(Actor);
		const AController* Controller=Pawn?Pawn->GetController():nullptr;
		return IsValid(Controller) && Controller->GetWorld()==Context.World.Get()
			&& Controller->GetPawn()==Pawn && Actor->GetOwner()==Controller;
	}
}

bool FEFCalystoGameplayElementEvidence::Matches(const FEFCalystoContentGameplayContext& C,
	const FEFCalystoReservedContent& R, FString& Error) const
{
	Error.Reset();
	const FSoftObjectPath Expected=R.InventorySlot==INDEX_NONE?R.Entry.ActorClass.ToSoftObjectPath():R.Entry.InventoryClass.ToSoftObjectPath();
	if (!(Token==C.Token) || ReservationId!=R.Id || EntryId!=R.Entry.Selection.Id || ParentContainerId!=R.ParentContainerId
		|| InventorySlot!=R.InventorySlot || Payload!=Expected || !C.PreFloorSnapshot
		|| SnapshotHash!=C.PreFloorSnapshot->GetCanonicalHash() || NativeStateHash.IsEmpty() || State!=EEFCalystoGameplayObservation::Verified)
	{ Error=TEXT("Gameplay evidence does not match the exact reserved identity, payload, slot and pre-floor snapshot."); return false; }
	return true;
}

bool FEFCalystoGameplayCommitReceipt::Matches(const FEFCalystoContentGameplayContext& C, FString& Error) const
{
	Error.Reset(); TSet<FGuid> Expected;
	if (C.Manifest) for (const auto& R:C.Manifest->GetElements()) Expected.Add(R.Id);
	if (!(Token==C.Token) || !C.Manifest || !C.Manifest->IsValid() || !C.PreFloorSnapshot
		|| ManifestHash!=C.Manifest->GetHash() || PreFloorSnapshotHash!=C.PreFloorSnapshot->GetCanonicalHash()
		|| PreparedStateHash.IsEmpty() || !EFCalystoContentRealizationPrivate::SameIds(Expected,PreparedElements))
	{ Error=TEXT("The prepared gameplay receipt must bind every exact reservation and the immutable pre-floor snapshot."); return false; }
	return true;
}

bool FEFCalystoGameplayReleaseEvidence::IsComplete() const
{ return bSafeToDestroyActors && bPersistentStateVerified && PendingCallbacks==0 && OwnedObjects==0 && SnapshotLeases==0; }

FString FEFCalystoContentMaterializer::Seal(const FEFCalystoContentGameplayContext& R)
{
	using namespace EFCalystoContentRealizationPrivate;
	TArray<FString> Lines;
	Lines.Add(Guid(R.Token.Request)+Guid(R.Token.Attempt));
	Lines.Add(FString::Printf(TEXT("%lld|%.17g|%s|%s"),R.FloorNumber,R.DeadlineSeconds,
		R.Manifest?*R.Manifest->GetHash():TEXT(""),R.PreFloorSnapshot?*R.PreFloorSnapshot->GetCanonicalHash():TEXT("")));
	for (const auto& Pair:R.Actors)
		Lines.Add(Guid(Pair.Key)+TEXT("|")+Guid(Pair.Value.ReservationId)+FString::Printf(TEXT("|%d|%d|"),Pair.Value.LogicalLevel,Pair.Value.PhysicalLevel)+Guid(Pair.Value.CompanionIdentity));
	for (const auto& M:R.Materials)
		Lines.Add(Guid(M.ReservationId)+TEXT("|")+M.ComponentName.ToString()+FString::Printf(TEXT("|%d|"),M.Slot)+M.Material.ToString());
	for (const auto& Pair:R.ExistingContainers)
		Lines.Add(Guid(Pair.Key)+FString::Printf(TEXT("|%u"),GetTypeHash(Pair.Value)));
	return HashLines(MoveTemp(Lines));
}

FEFCalystoContentMaterializer::~FEFCalystoContentMaterializer()
{
	// The coordinator must retain this owner until asynchronous teardown is independently verified.
	ensureMsgf(Observation.State==EEFCalystoContentRealizationState::Idle || Observation.State==EEFCalystoContentRealizationState::Released,
		TEXT("Content materializer destroyed before its token finished releasing."));
	if (IsInGameThread() && Observation.State!=EEFCalystoContentRealizationState::Idle && Observation.State!=EEFCalystoContentRealizationState::Released)
	{ Release(Context.Token); StepRelease(4096); }
}

void FEFCalystoContentMaterializer::AddReferencedObjects(FReferenceCollector& Collector)
{ for (auto& Record:Owned) Collector.AddReferencedObject(Record.Actor); }

bool FEFCalystoContentMaterializer::AcceptClock(double Now, FString& Error)
{
	if (EFCalystoContentRealizationPrivate::InvalidClock(Now,LastNow)) { Error=TEXT("Materialization requires a finite monotonic request clock."); return false; }
	LastNow=Now;
	if (Now>=Context.DeadlineSeconds) { Error=TEXT("The shared floor request deadline expired during content preparation."); return false; }
	return true;
}

bool FEFCalystoContentMaterializer::Begin(const FEFCalystoContentGameplayContext& Request,
	TSharedRef<IEFCalystoContentGameplayBridge> GameplayBridge, double Now, FString& Error)
{
	check(IsInGameThread()); Error.Reset();
	using namespace EFCalystoContentRealizationPrivate;
	if (bOperationActive) { Error=TEXT("Reentrant content initialization is unsupported."); return false; }
	TGuardValue<bool> Operation(bOperationActive,true);
	if (Observation.State!=EEFCalystoContentRealizationState::Idle && Observation.State!=EEFCalystoContentRealizationState::Released)
	{
		if (Observation.State!=EEFCalystoContentRealizationState::Releasing && Request.Token==Context.Token
			&& Request.World==Context.World && Request.AttemptOwner==Context.AttemptOwner && &GameplayBridge.Get()==Bridge.Get()
			&& RequestSeal==Seal(Request)) return AcceptClock(Now,Error);
		Error=TEXT("Another or changed content request cannot replace retained attempt ownership."); return false;
	}
	if ((OwningRequest.IsValid() && OwningRequest!=Request.Token.Request) || AttemptHistory.Contains(Request.Token.Attempt)
		|| AttemptHistory.Num()>=FEFCalystoFloorTransaction::MaximumAttempts || (LastNow>=0 && Now<LastNow))
	{ Error=TEXT("A released owner requires a fresh attempt in the same bounded floor request and a monotonic clock; retired tokens cannot reopen."); return false; }
	if (!ValidToken(Request.Token) || !Request.World.IsValid() || !Request.AttemptOwner.IsValid()
		|| Request.AttemptOwner->GetWorld()!=Request.World.Get() || !Request.Manifest || !Request.Manifest->IsValid()
		|| !Request.PreFloorSnapshot || Request.PreFloorSnapshot->GetCanonicalHash().IsEmpty() || Request.FloorNumber<1
		|| !FMath::IsFinite(Now) || Now<0 || !FMath::IsFinite(Request.DeadlineSeconds) || Now>=Request.DeadlineSeconds
		|| Request.Manifest->GetElements().Num()>4096 || Request.Actors.Num()>4096 || Request.Materials.Num()>16384
		|| Request.ExistingContainers.Num()>1024)
	{ Error=TEXT("Content requires a bounded frozen manifest, actual snapshot, owned world and remaining shared deadline."); return false; }
	TSet<FGuid> WorldIds; TSet<FString> MaterialKeys;
	for (const auto& R:Request.Manifest->GetElements())
	{
		if (R.InventorySlot!=INDEX_NONE) continue;
		WorldIds.Add(R.Id);
		const auto* Spec=Request.Actors.Find(R.Id);
		if (!Spec || Spec->ReservationId!=R.Id || Spec->LogicalLevel<1 || Spec->PhysicalLevel<1
			|| (R.Entry.Lifecycle==EEFCalystoLifecycle::Recruitable && !Spec->CompanionIdentity.IsValid()))
		{ Error=TEXT("Every actor requires exact frozen level data and any recruitable companion identity before loading."); return false; }
	}
	if (Request.Actors.Num()!=WorldIds.Num()) { Error=TEXT("Frozen gameplay actor keys differ from the reserved actors."); return false; }
	for (const auto& M:Request.Materials)
	{
		const FString Key=Guid(M.ReservationId)+M.ComponentName.ToString()+FString::FromInt(M.Slot);
		if (!WorldIds.Contains(M.ReservationId) || M.ComponentName.IsNone() || M.Slot<0 || !M.Material.IsValid() || MaterialKeys.Contains(Key))
		{ Error=TEXT("Selected material slots require unique owned actor/component/slot identities and explicit resources."); return false; }
		MaterialKeys.Add(Key);
	}
	for (const auto& Pair:Request.ExistingContainers)
		if (!Pair.Key.IsValid() || WorldIds.Contains(Pair.Key) || !Pair.Value.IsValid() || Pair.Value->GetWorld()!=Request.World.Get())
		{ Error=TEXT("Existing container bindings must identify exact actors in the attempt world."); return false; }
	TArray<FSoftObjectPath> Extra;
	if (!GameplayBridge->Preflight(Request,Extra,Error)) return false;
	if (Extra.Num()>32768) { Error=TEXT("Selected gameplay dependency closure exceeds the bounded loading contract."); return false; }
	Context=Request; Bridge=GameplayBridge; RequestSeal=Seal(Context); LastNow=Now;
	OwningRequest=Context.Token.Request; AttemptHistory.Add(Context.Token.Attempt);
	Observation={}; Observation.Token=Context.Token; Observation.ManifestHash=Context.Manifest->GetHash();
	Observation.State=EEFCalystoContentRealizationState::Loading;
	SpawnIndex=ContainerIndex=VerifyIndex=0; bActivated=false; bBridgeReleaseBegun=false; ReleaseIntent=EEFCalystoContentReleaseIntent::RejectedAttempt;
	bCoordinatorAccepted=bAcceptanceNotified=bActivationFailed=false;
	ActorIndices.Reset(); VerificationIndices.Reset(); ExistingContainerIds.Reset(); Contents.Reset(); MaterialsByActor.Reset(); NativeStateHashes.Reset(); CommitReceipt={}; GameplayRelease={};
	Dependencies=Context.Manifest->GetSelectedDependencies();
	for (const auto& Path:Extra)
	{
		if (!Path.IsValid()) { Error=TEXT("The selected bridge dependency closure contains an invalid path."); return Fail(EEFCalystoAttemptFailure::Resource,TEXT("SelectedDependencyInvalid"),Error); }
		Dependencies.AddUnique(Path);
	}
	for (const auto& M:Context.Materials) { Dependencies.AddUnique(M.Material); MaterialsByActor.FindOrAdd(M.ReservationId).Add(M); }
	Dependencies.Sort([](const auto& A,const auto& B){ return A.ToString()<B.ToString(); });
	for (int32 Index=0; Index<Context.Manifest->GetElements().Num(); ++Index)
	{
		const auto& R=Context.Manifest->GetElements()[Index];
		if (R.InventorySlot==INDEX_NONE) ActorIndices.Add(Index);
		else
		{
			if (!WorldIds.Contains(R.ParentContainerId) && !Context.ExistingContainers.Contains(R.ParentContainerId))
			{ Error=TEXT("A selected inventory item has no exact owned container binding."); return Fail(EEFCalystoAttemptFailure::Configuration,TEXT("ContainerBindingMissing"),Error); }
			Contents.FindOrAdd(R.ParentContainerId).Add(R);
		}
	}
	// Canonical manifest order is identity based and may put an item before its parent.
	// Finalize all actor storage first; inventory observation must not depend on GUID ordering.
	VerificationIndices=ActorIndices;
	for (int32 Index=0;Index<Context.Manifest->GetElements().Num();++Index)
		if (Context.Manifest->GetElements()[Index].InventorySlot!=INDEX_NONE) VerificationIndices.Add(Index);
	for (const auto& Pair:Context.ExistingContainers) if (Contents.Contains(Pair.Key)) ExistingContainerIds.Add(Pair.Key);
	ExistingContainerIds.Sort([](const FGuid& A,const FGuid& B){ return A.ToString()<B.ToString(); });
	Observation.State=EEFCalystoContentRealizationState::Loading;
	if (!Dependencies.IsEmpty())
	{
		// StreamableManager shares in-flight/resident resources across phase handles. This exact
		// selected-content handle independently retains them until token cleanup. No user callback exists.
		LoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Dependencies,FStreamableDelegate(),
			FStreamableManager::AsyncLoadHighPriority,false,false,TEXT("CalystoSelectedContent"));
		if (!LoadHandle) { Error=TEXT("Selected content could not acquire its retained async lease."); return Fail(EEFCalystoAttemptFailure::Resource,TEXT("SelectedLoadUnavailable"),Error); }
	}
	UpdateCounts(); return true;
}

bool FEFCalystoContentMaterializer::Fail(EEFCalystoAttemptFailure Failure,FName Code,const FString& Message)
{
	if (Observation.FailureCode.IsNone()) { Observation.Failure=Failure; Observation.FailureCode=Code; Observation.Message=Message; }
	Release(Context.Token); return false;
}

AActor* FEFCalystoContentMaterializer::FindContainer(FGuid Id) const
{
	if (const auto* Existing=Context.ExistingContainers.Find(Id)) return Existing->Get();
	if (const auto* Record=Owned.FindByPredicate([&](const FOwnedActor& A){ return A.ReservationId==Id; })) return Record->Actor;
	return nullptr;
}

bool FEFCalystoContentMaterializer::SpawnOne(const FEFCalystoReservedContent& R,FString& Error)
{
	Observation.Failure=EEFCalystoAttemptFailure::Resource;
	const auto ReservationContext=[&R]()
	{
		const FVector Location=R.Transform.GetLocation(), Scale=R.Transform.GetScale3D();
		const FQuat Rotation=R.Transform.GetRotation();
		return FString::Printf(TEXT("reservation=%s entry=%s role=%d payload=%s location=(%.3f,%.3f,%.3f) rotation=(%.6f,%.6f,%.6f,%.6f) scale=(%.3f,%.3f,%.3f) footprint_half_extent=(%.3f,%.3f,%.3f) clearance_cm=%.3f collision_contract=%s"),
			*R.Id.ToString(EGuidFormats::Digits), *R.Entry.Selection.Id.ToString(EGuidFormats::Digits), int32(R.Role),
			*R.Entry.ActorClass.ToSoftObjectPath().ToString(), Location.X, Location.Y, Location.Z,
			Rotation.X, Rotation.Y, Rotation.Z, Rotation.W, Scale.X, Scale.Y, Scale.Z,
			R.Entry.Placement.FootprintHalfExtent.X, R.Entry.Placement.FootprintHalfExtent.Y, R.Entry.Placement.FootprintHalfExtent.Z,
			R.Entry.Placement.Clearance, *R.CollisionContractHash);
	};
	UWorld* World=Context.World.Get(); AActor* Owner=Context.AttemptOwner.Get();
	UClass* Class=Cast<UClass>(R.Entry.ActorClass.ToSoftObjectPath().ResolveObject());
	if (!World || !Owner || !Class || !Class->IsChildOf(AActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated))
	{ Error=TEXT("The exact selected actor class or its owned world is unavailable. ")+ReservationContext(); return false; }
	FEFCalystoContentCollisionContract CollisionContract;
	FString CollisionError;
	Observation.Failure=EEFCalystoAttemptFailure::Configuration;
	if (R.CollisionContractHash.IsEmpty() || !FEFCalystoContentCollisionContracts::Build(Class,CollisionContract,CollisionError))
	{
		Error=TEXT("The frozen actor reservation has no valid CDO collision contract: ")+CollisionError+TEXT(". ")+ReservationContext();
		return false;
	}
	if (CollisionContract.Hash != R.CollisionContractHash)
	{
		Error=TEXT("The loaded actor CDO collision contract changed after reservation. frozen=")+R.CollisionContractHash
			+TEXT(" current=")+CollisionContract.Hash+TEXT(". ")+ReservationContext();
		return false;
	}
	AActor* Actor=World->SpawnActorDeferred<AActor>(Class,R.Transform,Owner,nullptr,
		ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding,ESpawnActorScaleMethod::OverrideRootScale);
	if (!Actor)
	{
		Observation.Failure=EEFCalystoAttemptFailure::Spatial;
		Error=TEXT("The exact reserved actor could not spawn without changing its reserved transform. ")+ReservationContext();
		return false;
	}
	FOwnedActor Record; Record.ReservationId=R.Id; Record.Actor=Actor;
	Record.bHiddenOnRelease=Actor->IsHidden(); Record.bCollisionOnRelease=Actor->GetActorEnableCollision(); Record.bTickOnRelease=Actor->IsActorTickEnabled();
	Owned.Add(Record);
	Actor->SetActorHiddenInGame(true); Actor->SetActorEnableCollision(false); Actor->SetActorTickEnabled(false);
	if (Observation.State==EEFCalystoContentRealizationState::Releasing) { Error=TEXT("The token was cancelled during deferred actor creation."); return false; }
	const TArray<FEFCalystoReservedContent> Empty;
	const auto* Children=Contents.Find(R.Id);
	Observation.Failure=EEFCalystoAttemptFailure::Configuration;
	if (!Bridge->PrepareDeferredActor(Context,R,Actor,Children?*Children:Empty,Error)) return false;
	if (Observation.State==EEFCalystoContentRealizationState::Releasing || !IsValid(Actor)) { Error=TEXT("The token or staged actor was released during native preparation."); return false; }
	Actor->FinishSpawning(R.Transform,false,nullptr,ESpawnActorScaleMethod::OverrideRootScale);
	Observation.Failure=EEFCalystoAttemptFailure::Spatial;
	if (Observation.State==EEFCalystoContentRealizationState::Releasing || !IsValid(Actor)
		|| Actor->GetWorld()!=World || !EFCalystoContentRealizationPrivate::NativeActorOwner(Actor,Context) || Actor->GetClass()!=Class)
	{ Error=TEXT("Construction invalidated the selected actor or its exact attempt ownership."); return false; }
	if (!Actor->IsHidden() || Actor->GetActorEnableCollision() || Actor->IsActorTickEnabled())
	{ Observation.Failure=EEFCalystoAttemptFailure::Configuration; Error=TEXT("A selected class activated itself before the gameplay commit gate."); return false; }
	return true;
}

EEFCalystoGameplayObservation FEFCalystoContentMaterializer::VerifyOne(const FEFCalystoReservedContent& R,FString& Error)
{
	using namespace EFCalystoContentRealizationPrivate;
	Observation.Failure=EEFCalystoAttemptFailure::Spatial;
	const bool Inventory=R.InventorySlot!=INDEX_NONE;
	AActor* Actor=FindContainer(Inventory?R.ParentContainerId:R.Id);
	if (!IsValid(Actor) || Actor->GetWorld()!=Context.World.Get())
	{ Error=TEXT("A selected actor/container disappeared from the attempt world."); return EEFCalystoGameplayObservation::Failed; }
	const auto InspectActor = [&]() -> EEFCalystoGameplayObservation
	{
		if (!NativeActorOwner(Actor,Context) || Actor->GetClass()!=R.Entry.ActorClass.ToSoftObjectPath().ResolveObject()
			|| !ExactPose(Actor->GetActorTransform(),R.Transform) || !Actor->IsHidden() || Actor->GetActorEnableCollision() || Actor->IsActorTickEnabled())
		{ Error=TEXT("A selected actor changed class, exact reserved pose, ownership or preparation quarantine."); return EEFCalystoGameplayObservation::Failed; }
		TArray<UPrimitiveComponent*> Components; Actor->GetComponents(Components);
		if (Components.Num()>256) { Error=TEXT("A selected actor exceeds the supported component verification bound."); return EEFCalystoGameplayObservation::Failed; }
		if (Components.IsEmpty()) return EEFCalystoGameplayObservation::Pending;
		for (const auto* Component:Components) if (!IsValid(Component) || !Component->IsRegistered()) return EEFCalystoGameplayObservation::Pending;
		const FBox Actual=Actor->GetComponentsBoundingBox(true,true);
		if (!Actual.IsValid || Actual.Min.ContainsNaN() || Actual.Max.ContainsNaN()) return EEFCalystoGameplayObservation::Pending;
		if (!R.ReservedBounds.IsValid || !R.ReservedBounds.IsInsideOrOn(Actual.Min) || !R.ReservedBounds.IsInsideOrOn(Actual.Max))
		{
			Error=FString::Printf(TEXT("Actual generated component bounds exceed the frozen placement reservation. reservation=%s entry=%s role=%d reserved=[(%.3f,%.3f,%.3f),(%.3f,%.3f,%.3f)] actual=[(%.3f,%.3f,%.3f),(%.3f,%.3f,%.3f)]"),
				*R.Id.ToString(EGuidFormats::Digits),*R.Entry.Selection.Id.ToString(EGuidFormats::Digits),int32(R.Role),
				R.ReservedBounds.Min.X,R.ReservedBounds.Min.Y,R.ReservedBounds.Min.Z,R.ReservedBounds.Max.X,R.ReservedBounds.Max.Y,R.ReservedBounds.Max.Z,
				Actual.Min.X,Actual.Min.Y,Actual.Min.Z,Actual.Max.X,Actual.Max.Y,Actual.Max.Z);
			return EEFCalystoGameplayObservation::Failed;
		}
		const TArray<FEFCalystoContentMaterialExpectation> EmptyMaterials;
		const auto* SelectedMaterials=MaterialsByActor.Find(R.Id);
		for (const auto& Expected:SelectedMaterials?*SelectedMaterials:EmptyMaterials)
		{
			UPrimitiveComponent* Found=nullptr;
			for (UPrimitiveComponent* Component:Components) if (Component->GetFName()==Expected.ComponentName)
			{ if (Found) { Error=TEXT("A selected material component identity is ambiguous."); return EEFCalystoGameplayObservation::Failed; } Found=Component; }
			if (!Found || Expected.Slot>=Found->GetNumMaterials() || !Cast<UMaterialInterface>(Expected.Material.ResolveObject())
				|| Found->GetMaterial(Expected.Slot)!=Expected.Material.ResolveObject())
			{ Error=TEXT("An actual selected content material slot differs from its frozen material reference."); return EEFCalystoGameplayObservation::Failed; }
		}
		return EEFCalystoGameplayObservation::Verified;
	};
	if (!Inventory) { const auto State=InspectActor(); if (State!=EEFCalystoGameplayObservation::Verified) return State; }
	const auto Evidence=Inventory?Bridge->ObserveInventoryItem(Context,R,Actor):Bridge->ObserveActor(Context,R,Actor);
	if (Observation.State==EEFCalystoContentRealizationState::Releasing || !IsValid(Actor) || Actor->GetWorld()!=Context.World.Get())
	{ Error=TEXT("A gameplay observation invalidated its token or actor ownership."); return EEFCalystoGameplayObservation::Failed; }
	if (Evidence.State==EEFCalystoGameplayObservation::Pending) return Evidence.State;
	if (Evidence.State==EEFCalystoGameplayObservation::Failed) { Error=Evidence.Message; Observation.Failure=Evidence.Failure; return Evidence.State; }
	if (!Evidence.Matches(Context,R,Error)) return EEFCalystoGameplayObservation::Failed;
	if (!Inventory) { const auto State=InspectActor(); if (State!=EEFCalystoGameplayObservation::Verified) return State; }
	NativeStateHashes.Add(R.Id,Evidence.NativeStateHash); Observation.VerifiedElements.Add(R.Id);
	return EEFCalystoGameplayObservation::Verified;
}

FEFCalystoContentRealizationObservation FEFCalystoContentMaterializer::Observe(double Now,int32 Operations)
{
	check(IsInGameThread());
	if (bOperationActive) return Observation;
	TGuardValue<bool> Operation(bOperationActive,true); Operations=FMath::Clamp(Operations,1,32);
	if (Observation.State==EEFCalystoContentRealizationState::Releasing)
	{ if (FMath::IsFinite(Now) && Now>=LastNow) LastNow=Now; StepRelease(Operations); UpdateCounts(); return Observation; }
	if (Observation.State==EEFCalystoContentRealizationState::Idle || Observation.State==EEFCalystoContentRealizationState::Released
		|| Observation.State==EEFCalystoContentRealizationState::Committed) return Observation;
	FString Error;
	const bool bClockInvalid=EFCalystoContentRealizationPrivate::InvalidClock(Now,LastNow);
	if (!AcceptClock(Now,Error))
	{
		Fail(bClockInvalid?EEFCalystoAttemptFailure::Configuration:EEFCalystoAttemptFailure::Deadline,
			bClockInvalid?FName(TEXT("ContentRequestClockInvalid")):FName(TEXT("ContentRequestDeadline")),Error);
		return Observation;
	}
	if (!Context.World.IsValid() || !Context.AttemptOwner.IsValid() || RequestSeal!=Seal(Context))
	{ Fail(EEFCalystoAttemptFailure::Configuration,TEXT("ContentRequestChanged"),TEXT("The immutable content request or owned world changed during realization.")); return Observation; }
	if (Observation.State==EEFCalystoContentRealizationState::Prepared || Observation.State==EEFCalystoContentRealizationState::GameplayPublished) return Observation;
	if (Observation.State==EEFCalystoContentRealizationState::Loading)
	{
		if (LoadHandle && !LoadHandle->HasLoadCompleted()) return Observation;
		for (const auto& Path:Dependencies) if (!Path.ResolveObject())
		{ Fail(EEFCalystoAttemptFailure::Resource,TEXT("SelectedLoadUnresolved"),TEXT("A completed selected-content phase has an unresolved exact resource: ")+Path.ToString()); return Observation; }
		Observation.State=EEFCalystoContentRealizationState::Spawning;
	}
	while (Operations-->0)
	{
		if (Observation.State==EEFCalystoContentRealizationState::Spawning)
		{
			if (SpawnIndex<ActorIndices.Num())
			{
				if (!SpawnOne(Context.Manifest->GetElements()[ActorIndices[SpawnIndex++]],Error))
				{ Fail(Observation.Failure,TEXT("SelectedActorUnrealized"),Error); break; }
				continue;
			}
			if (ContainerIndex<ExistingContainerIds.Num())
			{
				const FGuid Id=ExistingContainerIds[ContainerIndex++];
				if (!Bridge->StageExistingContainer(Context,Id,FindContainer(Id),Contents.FindChecked(Id),Error))
				{ Fail(EEFCalystoAttemptFailure::Configuration,TEXT("SelectedInventoryUnrealized"),Error); break; }
				if (Observation.State==EEFCalystoContentRealizationState::Releasing) break;
				continue;
			}
			Observation.State=EEFCalystoContentRealizationState::Verifying;
		}
		if (Observation.State!=EEFCalystoContentRealizationState::Verifying) break;
		if (VerifyIndex<VerificationIndices.Num())
		{
			const auto& Reservation=Context.Manifest->GetElements()[VerificationIndices[VerifyIndex]];
			if (Reservation.InventorySlot==INDEX_NONE)
			{
				const auto Finalized=Bridge->FinalizeSpawnedActor(Context,Reservation,FindContainer(Reservation.Id),Error);
				if (Observation.State==EEFCalystoContentRealizationState::Releasing || Finalized==EEFCalystoGameplayObservation::Pending) break;
				if (Finalized==EEFCalystoGameplayObservation::Failed) { Fail(EEFCalystoAttemptFailure::Configuration,TEXT("NativeActorInitializationFailed"),Error); break; }
			}
			const auto State=VerifyOne(Reservation,Error);
			if (State==EEFCalystoGameplayObservation::Pending) break;
			if (State==EEFCalystoGameplayObservation::Failed) { Fail(Observation.Failure,TEXT("SelectedElementUnverified"),Error); break; }
			++VerifyIndex; continue;
		}
		const auto Ready=Bridge->PrepareCommit(Context,CommitReceipt,Error);
		if (Observation.State==EEFCalystoContentRealizationState::Releasing) break;
		if (Ready==EEFCalystoGameplayObservation::Pending) break;
		if (Ready==EEFCalystoGameplayObservation::Failed || !CommitReceipt.Matches(Context,Error))
		{ Fail(EEFCalystoAttemptFailure::Configuration,TEXT("GameplayPreparationUnverified"),Error); break; }
		TArray<FString> Hashes;
		for (const auto& Pair:NativeStateHashes) Hashes.Add(Pair.Key.ToString()+TEXT("|")+Pair.Value);
		Hashes.Add(RequestSeal); Hashes.Add(CommitReceipt.PreparedStateHash);
		Observation.RealizationHash=EFCalystoContentRealizationPrivate::HashLines(MoveTemp(Hashes));
		Observation.State=EEFCalystoContentRealizationState::Prepared; break;
	}
	UpdateCounts(); return Observation;
}

bool FEFCalystoContentMaterializer::Commit(const FEFCalystoAttemptToken& Token,double Now,FString& Error)
{
	check(IsInGameThread()); Error.Reset();
	if (bOperationActive || !(Token==Context.Token) || Observation.State!=EEFCalystoContentRealizationState::Prepared)
	{ Error=TEXT("Only the current fully prepared content token may commit."); return false; }
	TGuardValue<bool> Operation(bOperationActive,true);
	const bool bClockInvalid=EFCalystoContentRealizationPrivate::InvalidClock(Now,LastNow);
	if (!AcceptClock(Now,Error)) return Fail(bClockInvalid?EEFCalystoAttemptFailure::Configuration:EEFCalystoAttemptFailure::Deadline,
		bClockInvalid?FName(TEXT("ContentCommitClockInvalid")):FName(TEXT("ContentCommitDeadline")),Error);
	if (!Context.World.IsValid() || !Context.AttemptOwner.IsValid() || RequestSeal!=Seal(Context) || !CommitReceipt.Matches(Context,Error))
		return Fail(EEFCalystoAttemptFailure::Configuration,TEXT("ContentCommitBindingChanged"),Error);
	const auto ExpectedHashes=NativeStateHashes;
	for (const auto& R:Context.Manifest->GetElements())
	{
		if (VerifyOne(R,Error)!=EEFCalystoGameplayObservation::Verified || NativeStateHashes.FindRef(R.Id)!=ExpectedHashes.FindRef(R.Id))
			return Fail(EEFCalystoAttemptFailure::Spatial,TEXT("ContentChangedBeforeCommit"),Error.IsEmpty()?TEXT("Selected native state changed after preparation."):Error);
	}
	if (!Bridge->CommitPrepared(Context,CommitReceipt,Error))
		return Fail(EEFCalystoAttemptFailure::Configuration,TEXT("GameplayCommitRejected"),Error);
	if (Observation.State==EEFCalystoContentRealizationState::Releasing)
	{ Error=TEXT("Gameplay commit reentered release; bridge must preserve the pre-floor state on failure."); return false; }
	// Publication is still reversible. A bridge callback must not replace or activate any
	// selected actor/controller, or change its frozen native state before acceptance.
	if (!Context.World.IsValid() || !Context.AttemptOwner.IsValid() || RequestSeal!=Seal(Context) || !CommitReceipt.Matches(Context,Error))
		return Fail(EEFCalystoAttemptFailure::Configuration,TEXT("ContentPublicationBindingChanged"),Error);
	for (const auto& R:Context.Manifest->GetElements())
	{
		if (VerifyOne(R,Error)!=EEFCalystoGameplayObservation::Verified || NativeStateHashes.FindRef(R.Id)!=ExpectedHashes.FindRef(R.Id))
			return Fail(EEFCalystoAttemptFailure::Configuration,TEXT("ContentChangedDuringPublication"),
				Error.IsEmpty()?TEXT("Gameplay publication changed the frozen activation or quarantine ledger."):Error);
	}
	Observation.State=EEFCalystoContentRealizationState::GameplayPublished; return true;
}

EEFCalystoActivationResult FEFCalystoContentMaterializer::ActivateCommitted(const FEFCalystoAttemptToken& Token,FString& Error)
{
	check(IsInGameThread()); Error.Reset();
	using Result=EEFCalystoActivationResult;
	if (bOperationActive || !(Token==Context.Token))
	{ Error=TEXT("Activation requires the exact accepted token and a non-reentrant owner."); return Result::InvariantFailure; }
	if (bCoordinatorAccepted && bActivationFailed) { Error=Observation.Message; return Result::InvariantFailure; }
	if (bCoordinatorAccepted && (Observation.State==EEFCalystoContentRealizationState::Releasing
		|| Observation.State==EEFCalystoContentRealizationState::Released)) return Result::AcceptedExitRequested;
	if (bCoordinatorAccepted && bActivated && Observation.State==EEFCalystoContentRealizationState::Committed) return Result::Activated;
	if (Observation.State!=EEFCalystoContentRealizationState::GameplayPublished
		&& !(bCoordinatorAccepted && Observation.State==EEFCalystoContentRealizationState::Committed))
	{ Error=TEXT("Only published content can cross the coordinator acceptance boundary."); return Result::InvariantFailure; }
	// The caller already committed. Even the first ownership check may fail; cleanup
	// must preserve accepted persistent state in that case, before any notification.
	bCoordinatorAccepted=true;
	Observation.State=EEFCalystoContentRealizationState::Committed;
	TGuardValue<bool> Operation(bOperationActive,true);
	const auto InvariantFailure = [&](const TCHAR* Message)
	{
		bActivationFailed=true; Error=Message;
		Fail(EEFCalystoAttemptFailure::Configuration,TEXT("ContentActivationInvariant"),Error);
		return Result::InvariantFailure;
	};
	const auto StillAccepted = [&]()
	{ return Observation.State==EEFCalystoContentRealizationState::Committed && Token==Context.Token; };
	const auto WorldOwned = [&]()
	{ return Context.World.IsValid() && Context.AttemptOwner.IsValid() && !Context.AttemptOwner->IsActorBeingDestroyed()
		&& Context.AttemptOwner->GetWorld()==Context.World.Get() && Bridge.IsValid(); };
	if (!WorldOwned()) return InvariantFailure(TEXT("Accepted world or gameplay owner disappeared before activation."));
	for (const auto& R:Owned)
		if (!EFCalystoContentRealizationPrivate::NativeActorOwner(R.Actor,Context)
			|| !R.Actor->IsHidden() || R.Actor->GetActorEnableCollision() || R.Actor->IsActorTickEnabled())
			return InvariantFailure(TEXT("Accepted actor ownership or preparation quarantine changed before activation."));
	if (!bAcceptanceNotified)
	{
		bAcceptanceNotified=true; // Set before callbacks: interrupted activation cannot publish acceptance twice.
		FString BridgeError;
		const Result BridgeResult=Bridge->ConfirmAccepted(Context,CommitReceipt,BridgeError);
		// A native failure cannot be concealed by an exit requested from the same callback.
		// The accepted latch is already set, so Fail() always preserves accepted outcomes.
		if (BridgeResult==Result::InvariantFailure)
			return InvariantFailure(BridgeError.IsEmpty()?TEXT("Accepted gameplay bridge activation failed without a diagnostic."):*BridgeError);
		if (BridgeResult==Result::AcceptedExitRequested)
		{
			if (Token==Context.Token && (Observation.State==EEFCalystoContentRealizationState::Releasing
				|| Observation.State==EEFCalystoContentRealizationState::Released))
			{ Error=BridgeError; return Result::AcceptedExitRequested; }
			return InvariantFailure(TEXT("Gameplay bridge reported accepted exit without an actual exact-token release request."));
		}
		if (BridgeResult!=Result::Activated)
			return InvariantFailure(TEXT("Accepted gameplay bridge returned an invalid activation result."));
	}
	if (!StillAccepted()) return Result::AcceptedExitRequested;
	if (!WorldOwned()) return InvariantFailure(TEXT("Accepted world or gameplay owner disappeared during confirmation."));
	for (auto& R:Owned)
	{
		if (!StillAccepted()) return Result::AcceptedExitRequested;
		if (!EFCalystoContentRealizationPrivate::NativeActorOwner(R.Actor,Context))
			return InvariantFailure(TEXT("Accepted actor ownership changed during confirmation."));
		R.Actor->SetActorHiddenInGame(R.bHiddenOnRelease);
		if (!StillAccepted()) return Result::AcceptedExitRequested;
		if (!WorldOwned() || !EFCalystoContentRealizationPrivate::NativeActorOwner(R.Actor,Context) || R.Actor->IsHidden()!=R.bHiddenOnRelease)
			return InvariantFailure(TEXT("Accepted actor visibility could not be restored exactly."));
		// Collision restoration invokes component hooks and overlap callbacks in UE5.8.
		R.Actor->SetActorEnableCollision(R.bCollisionOnRelease);
		if (!StillAccepted()) return Result::AcceptedExitRequested;
		if (!WorldOwned() || !EFCalystoContentRealizationPrivate::NativeActorOwner(R.Actor,Context) || R.Actor->GetActorEnableCollision()!=R.bCollisionOnRelease)
			return InvariantFailure(TEXT("Accepted actor collision could not be restored exactly."));
		R.Actor->SetActorTickEnabled(R.bTickOnRelease);
		if (!StillAccepted()) return Result::AcceptedExitRequested;
		if (!EFCalystoContentRealizationPrivate::NativeActorOwner(R.Actor,Context) || R.Actor->IsActorTickEnabled()!=R.bTickOnRelease)
			return InvariantFailure(TEXT("Accepted actor tick state could not be restored exactly."));
	}
	bActivated=true; return Result::Activated;
}

void FEFCalystoContentMaterializer::Release(const FEFCalystoAttemptToken& Token)
{
	check(IsInGameThread());
	if (!(Token==Context.Token) || Observation.State==EEFCalystoContentRealizationState::Idle
		|| Observation.State==EEFCalystoContentRealizationState::Released || Observation.State==EEFCalystoContentRealizationState::Releasing) return;
	TGuardValue<bool> Operation(bOperationActive,true);
	ReleaseIntent=bCoordinatorAccepted
		?EEFCalystoContentReleaseIntent::AcceptedFloorExit:EEFCalystoContentReleaseIntent::RejectedAttempt;
	Observation.State=EEFCalystoContentRealizationState::Releasing;
	Observation.VerifiedElements.Reset(); Observation.RealizationHash.Reset();
	if (LoadHandle && !LoadHandle->HasLoadCompleted()) LoadHandle->CancelHandle();
	for (auto& R:Owned) if (IsValid(R.Actor))
	{ R.bQuarantinedForRelease=true; R.Actor->SetActorHiddenInGame(true); R.Actor->SetActorEnableCollision(false); R.Actor->SetActorTickEnabled(false); }
	// BeginRelease may call project code; defer it until the current bridge callback has unwound.
}

void FEFCalystoContentMaterializer::StepRelease(int32 Operations)
{
	if (Observation.State!=EEFCalystoContentRealizationState::Releasing || !Bridge) return;
	TGuardValue<bool> Operation(bOperationActive,true);
	if (!bBridgeReleaseBegun) { bBridgeReleaseBegun=true; Bridge->BeginRelease(Context,ReleaseIntent); }
	GameplayRelease=Bridge->ObserveRelease(Context,ReleaseIntent);
	if (!(GameplayRelease.Token==Context.Token) || !GameplayRelease.bSafeToDestroyActors) return;
	if (GameplayRelease.TransferredActors.Num()>Owned.Num()
		|| (ReleaseIntent==EEFCalystoContentReleaseIntent::RejectedAttempt && !GameplayRelease.TransferredActors.IsEmpty()))
	{ Observation.Message=TEXT("Cleanup received unsupported actor ownership transfers."); return; }
	for (const auto& Transfer:GameplayRelease.TransferredActors)
		if (!Owned.ContainsByPredicate([&](const FOwnedActor& R){ return R.ReservationId==Transfer.Key; }))
		{ Observation.Message=TEXT("Cleanup received a transfer outside this owner's exact actor ledger."); return; }
	for (auto& R:Owned)
	{
		if (!R.Actor) continue;
		if (Operations--<=0) break;
		if (const auto* Transfer=GameplayRelease.TransferredActors.Find(R.ReservationId))
		{
			if (ReleaseIntent!=EEFCalystoContentReleaseIntent::AcceptedFloorExit || !GameplayRelease.bPersistentStateVerified
				|| Transfer->Get()!=R.Actor || !IsValid(R.Actor))
			{ Observation.Message=TEXT("Unverified actor ownership transfer prevents attempt cleanup."); continue; }
			if (R.bQuarantinedForRelease)
			{
				R.Actor->SetActorHiddenInGame(R.bHiddenOnRelease);
				if (!IsValid(R.Actor)) continue;
				R.Actor->SetActorEnableCollision(R.bCollisionOnRelease);
				if (!IsValid(R.Actor)) continue;
				R.Actor->SetActorTickEnabled(R.bTickOnRelease);
				if (!IsValid(R.Actor)) continue;
			}
			R.Actor=nullptr; continue;
		}
		if (IsValid(R.Actor) && !R.Actor->IsActorBeingDestroyed() && !R.Actor->Destroy())
		{ Observation.Message=TEXT("An owned actor rejected destruction; cleanup remains pending."); continue; }
		// Actor::Destroy is latent until the end of the tick. Keep the strong reference until
		// world actor membership is gone, not merely until IsActorBeingDestroyed becomes true.
		const ULevel* Level=R.Actor->GetLevel();
		TArray<UActorComponent*> Components; R.Actor->GetComponents(Components);
		bool Registered=false; for (const auto* Component:Components) Registered|=IsValid(Component) && Component->IsRegistered();
		if ((!Level || !Level->Actors.Contains(R.Actor)) && !Registered) R.Actor=nullptr;
	}
	bool Retained=false; for (const auto& R:Owned) Retained|=R.Actor!=nullptr;
	if (Retained || !GameplayRelease.IsComplete()) return;
	Owned.Reset(); Contents.Reset(); MaterialsByActor.Reset(); ActorIndices.Reset(); VerificationIndices.Reset(); ExistingContainerIds.Reset(); NativeStateHashes.Reset();
	if (LoadHandle) { LoadHandle->ReleaseHandle(); LoadHandle.Reset(); }
	Dependencies.Reset(); CommitReceipt={}; Context.Manifest.Reset(); Context.PreFloorSnapshot.Reset(); Context.Actors.Reset();
	Context.Materials.Reset(); Context.ExistingContainers.Reset(); Bridge.Reset();
	Observation.State=EEFCalystoContentRealizationState::Released;
}

void FEFCalystoContentMaterializer::UpdateCounts()
{
	Observation.OwnedActors=0; for (const auto& R:Owned) if (R.Actor) ++Observation.OwnedActors;
	Observation.RetainedLeases=LoadHandle.IsValid()?1:0;
}

FEFCalystoRollbackEvidence FEFCalystoContentMaterializer::GetReleaseEvidence(const FEFCalystoAttemptToken& Token) const
{
	FEFCalystoRollbackEvidence E;
	if (!(Token==Context.Token)) { E.PendingCallbacks=1; return E; }
	for (const auto& R:Owned) if (R.Actor) ++E.Actors;
	E.Reservations=Context.Manifest?Context.Manifest->GetElements().Num():0;
	E.PendingCallbacks=GameplayRelease.PendingCallbacks;
	E.TransientReferences=GameplayRelease.OwnedObjects+(Context.PreFloorSnapshot.IsValid()?1:0);
	E.AttemptLeases=(LoadHandle.IsValid()?1:0)+GameplayRelease.SnapshotLeases;
	// Content does not own PCG. True means this owner completed its own release; native
	// PCG/nav/decal owners must independently contribute their evidence to the coordinator.
	E.bPCGCleanupComplete=Observation.State==EEFCalystoContentRealizationState::Released;
	return E;
}
