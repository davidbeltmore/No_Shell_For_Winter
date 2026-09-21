#include "Calysto/EFCalystoDecalMaterializer.h"

#include "Components/ActorComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"

namespace EFCalystoDecalMaterializerPrivate
{
	constexpr TCHAR FloorMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor.MI_CalystoBloodDecal_Floor");
	constexpr TCHAR WallMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall.MI_CalystoBloodDecal_Wall");
	constexpr TCHAR RoofMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling.MI_CalystoBloodDecal_Ceiling");
	constexpr TCHAR ColorTexture[] = TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.T_Splat_04");
	constexpr TCHAR NormalTexture[] = TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.T_Splat_N_04");
	bool ValidToken(const FEFCalystoAttemptToken& Value) { return Value.Request.IsValid() && Value.Attempt.IsValid(); }
	bool InvalidClock(const double Now, const double LastNow)
	{ return !FMath::IsFinite(Now) || Now < 0 || Now < LastNow; }
	bool AllowedPath(const FSoftObjectPath& Path)
	{
		const FString Name = Path.ToString();
		return Name == FloorMaterial || Name == WallMaterial || Name == RoofMaterial || Name == ColorTexture || Name == NormalTexture;
	}
	bool ValidResource(const FSoftObjectPath& Path, UObject* Object)
	{
		if (!IsValid(Object) || FSoftObjectPath(Object) != Path || !AllowedPath(Path)) return false;
		const FString Name = Path.ToString();
		return (Name == ColorTexture || Name == NormalTexture) ? Cast<UTexture2D>(Object) != nullptr : Cast<UMaterialInterface>(Object) != nullptr;
	}
}

FEFCalystoDecalMaterializer::~FEFCalystoDecalMaterializer()
{
	ensureMsgf(Observation.State == EEFCalystoDecalRealizationState::Idle || Observation.State == EEFCalystoDecalRealizationState::Released,
		TEXT("Decal materializer was destroyed before exact token release."));
	if (IsInGameThread() && Observation.State != EEFCalystoDecalRealizationState::Idle
		&& Observation.State != EEFCalystoDecalRealizationState::Released)
	{
		Release(Token);
		for (int32 Index = 0; Index < 8 && Observation.State == EEFCalystoDecalRealizationState::Releasing; ++Index) StepRelease();
	}
}

void FEFCalystoDecalMaterializer::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Pool);
	for (TObjectPtr<UObject>& Resource : Resources) Collector.AddReferencedObject(Resource);
}

bool FEFCalystoDecalMaterializer::AcceptClock(const double Now, FString& Error)
{
	if (EFCalystoDecalMaterializerPrivate::InvalidClock(Now,LastNow))
	{ Error = TEXT("Decal materialization requires a finite monotonic request clock."); return false; }
	LastNow = Now;
	if (Now >= DeadlineSeconds)
	{ Error = TEXT("The shared floor request deadline expired during selected decal preparation."); return false; }
	return true;
}

bool FEFCalystoDecalMaterializer::Begin(const FEFCalystoAttemptToken& InToken,
	TSharedRef<const FEFCalystoCompiledDirector> InConfiguration, TSharedRef<FEFCalystoNativeAdapter> InNative,
	UWorld* InWorld, TSharedRef<const FEFCalystoReservedDecalManifest> InManifest,
	const double InDeadlineSeconds, const double Now, FString& Error)
{
	using namespace EFCalystoDecalMaterializerPrivate;
	check(IsInGameThread()); Error.Reset();
	if (Observation.State != EEFCalystoDecalRealizationState::Idle && Observation.State != EEFCalystoDecalRealizationState::Released)
	{ Error = TEXT("An active decal materializer cannot be replaced before verified release."); return false; }
	if (!ValidToken(InToken) || !InConfiguration->IsValid() || !InNative->GetDungeonActor() || !InWorld || !InWorld->IsGameWorld()
		|| InNative->GetDungeonActor()->GetWorld() != InWorld || !InManifest->IsValid() || InManifest->GetElements().Num() > AEFCalystoDecalPoolOwner::PoolCapacity
		|| InManifest->GetSelectedDependencies().Num() > AEFCalystoDecalPoolOwner::PoolCapacity * 3
		|| !FMath::IsFinite(InDeadlineSeconds) || !FMath::IsFinite(Now) || Now < 0 || Now >= InDeadlineSeconds)
	{ Error = TEXT("Selected decals require a bounded frozen manifest, same-attempt native owner and remaining shared deadline."); return false; }
	if (!FEFCalystoDecalReservationPlanner::ValidateConfiguration(*InConfiguration, InNative->GetRoomConfig().StyleId, Error)) return false;
	Token = InToken; Configuration = InConfiguration; Native = InNative; World = InWorld; Manifest = InManifest;
	DeadlineSeconds = InDeadlineSeconds; LastNow = Now; bActivated = false;
	Observation = {}; Observation.Token = Token; Observation.ManifestHash = Manifest->GetHash();
	Dependencies = Manifest->GetSelectedDependencies(); Resources.Reset(); Pool = nullptr;
	for (const FSoftObjectPath& Path : Dependencies)
		if (!Path.IsValid() || !AllowedPath(Path))
		{ Error = TEXT("Selected decal dependencies are outside the exact intended closure."); return Fail(EEFCalystoAttemptFailure::Configuration, TEXT("DecalClosureInvalid"), Error); }
	if (Manifest->GetElements().IsEmpty())
	{
		if (!Dependencies.IsEmpty()) { Error = TEXT("An empty decal reservation cannot retain payload dependencies."); return Fail(EEFCalystoAttemptFailure::Configuration, TEXT("DecalEmptyLeaseInvalid"), Error); }
		Observation.RealizationHash = FMD5::HashAnsiString(*(Manifest->GetHash() + TEXT("|NoSelectedDecals")));
		Observation.State = EEFCalystoDecalRealizationState::Prepared; UpdateCounts(); return true;
	}
	Observation.State = EEFCalystoDecalRealizationState::Loading;
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Dependencies, FStreamableDelegate(),
		FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("CalystoSelectedDecals"));
	if (!LoadHandle)
	{ Error = TEXT("Selected decal dependencies could not acquire their retained async lease."); return Fail(EEFCalystoAttemptFailure::Resource, TEXT("DecalLoadUnavailable"), Error); }
	UpdateCounts(); return true;
}

bool FEFCalystoDecalMaterializer::Fail(const EEFCalystoAttemptFailure Failure, const FName Code, const FString& Message)
{
	if (Observation.FailureCode.IsNone()) { Observation.Failure = Failure; Observation.FailureCode = Code; Observation.Message = Message; }
	Release(Token); return false;
}

bool FEFCalystoDecalMaterializer::Stage(FString& Error)
{
	using namespace EFCalystoDecalMaterializerPrivate;
	if (!World.IsValid() || !Native.IsValid() || !Configuration.IsValid() || !Manifest.IsValid())
	{ Error = TEXT("Selected decal owner disappeared while its retained resources were loading."); return false; }
	Resources.Reset();
	for (const FSoftObjectPath& Path : Dependencies)
	{
		UObject* Resource = Path.ResolveObject();
		if (!ValidResource(Path, Resource))
		{ Error = TEXT("A selected decal material or intended RealisticBlood texture is missing or changed."); return false; }
		Resources.Add(Resource);
	}
	FActorSpawnParameters Parameters; Parameters.Owner = Native->GetDungeonActor(); Parameters.ObjectFlags = RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Pool = World->SpawnActor<AEFCalystoDecalPoolOwner>(AEFCalystoDecalPoolOwner::StaticClass(), FTransform::Identity, Parameters);
	if (!Pool || Pool->GetWorld() != World.Get()) { Error = TEXT("The single transient decal pool owner could not be created."); return false; }
	TArray<UObject*> RawResources; RawResources.Reserve(Resources.Num()); for (UObject* Resource : Resources) RawResources.Add(Resource);
	if (!Pool->Begin(Token, *Configuration, Native.ToSharedRef(), Manifest->GetElements(), RawResources, Error)) return false;
	if (!Pool->Verify(Token, Error)) return false;
	Observation.VerifiedElements.Reset(); for (const FEFCalystoReservedDecal& Element : Manifest->GetElements()) Observation.VerifiedElements.Add(Element.Id);
	if (Observation.VerifiedElements.Num() != Manifest->GetElements().Num()) { Error = TEXT("Selected decal identity ledger became non-unique during staging."); return false; }
	Observation.RealizationHash = FMD5::HashAnsiString(*(Manifest->GetHash() + TEXT("|") + Pool->GetProposalHash()));
	Observation.State = EEFCalystoDecalRealizationState::Prepared; UpdateCounts(); return true;
}

FEFCalystoDecalRealizationObservation FEFCalystoDecalMaterializer::Observe(const double Now)
{
	check(IsInGameThread()); FString Error;
	if (Observation.State == EEFCalystoDecalRealizationState::Releasing)
	{ StepRelease(); UpdateCounts(); return Observation; }
	if (Observation.State == EEFCalystoDecalRealizationState::Idle || Observation.State == EEFCalystoDecalRealizationState::Released
		|| Observation.State == EEFCalystoDecalRealizationState::Prepared || Observation.State == EEFCalystoDecalRealizationState::Published
		|| Observation.State == EEFCalystoDecalRealizationState::Committed) return Observation;
	const bool bClockInvalid=EFCalystoDecalMaterializerPrivate::InvalidClock(Now,LastNow);
	if (!AcceptClock(Now, Error))
	{
		Fail(bClockInvalid?EEFCalystoAttemptFailure::Configuration:EEFCalystoAttemptFailure::Deadline,
			bClockInvalid?FName(TEXT("DecalRequestClockInvalid")):FName(TEXT("DecalPreparationDeadline")),Error);
		return Observation;
	}
	if (Observation.State != EEFCalystoDecalRealizationState::Loading || !LoadHandle)
	{ Fail(EEFCalystoAttemptFailure::Configuration, TEXT("DecalStateInvalid"), TEXT("Selected decal loading lost its retained handle.")); return Observation; }
	if (!LoadHandle->HasLoadCompleted()) return Observation;
	if (LoadHandle->WasCanceled()) { Fail(EEFCalystoAttemptFailure::Resource, TEXT("DecalLoadCancelled"), TEXT("Selected decal resource loading was cancelled.")); return Observation; }
	Observation.State = EEFCalystoDecalRealizationState::Staging;
	if (!Stage(Error)) Fail(EEFCalystoAttemptFailure::Resource, TEXT("DecalStagingFailed"), Error.IsEmpty() ? TEXT("Selected decal staging failed without a diagnostic.") : Error);
	return Observation;
}

bool FEFCalystoDecalMaterializer::Commit(const FEFCalystoAttemptToken& InToken, const double Now, FString& Error)
{
	check(IsInGameThread()); Error.Reset();
	if (!(InToken == Token) || Observation.State != EEFCalystoDecalRealizationState::Prepared)
	{ Error = TEXT("Only the exact hidden, fully verified decal token may publish."); return false; }
	const bool bClockInvalid=EFCalystoDecalMaterializerPrivate::InvalidClock(Now,LastNow);
	if (!AcceptClock(Now, Error)) return Fail(bClockInvalid?EEFCalystoAttemptFailure::Configuration:EEFCalystoAttemptFailure::Deadline,
		bClockInvalid?FName(TEXT("DecalCommitClockInvalid")):FName(TEXT("DecalCommitDeadline")),Error);
	if (Pool && !Pool->Verify(Token, Error)) return Fail(EEFCalystoAttemptFailure::Spatial, TEXT("DecalChangedBeforeCommit"), Error);
	if (Observation.VerifiedElements.Num() != (Manifest ? Manifest->GetElements().Num() : -1))
	{ Error = TEXT("The exact selected decal ledger changed before publication."); return Fail(EEFCalystoAttemptFailure::Spatial, TEXT("DecalLedgerChanged"), Error); }
	Observation.State = EEFCalystoDecalRealizationState::Published; return true;
}

EEFCalystoActivationResult FEFCalystoDecalMaterializer::ActivateCommitted(const FEFCalystoAttemptToken& InToken, FString& Error)
{
	check(IsInGameThread()); Error.Reset();
	if (!(InToken == Token) || (Observation.State != EEFCalystoDecalRealizationState::Published
		&& Observation.State != EEFCalystoDecalRealizationState::Committed))
	{ Error = TEXT("Accepted decal activation lost its exact published token."); return EEFCalystoActivationResult::InvariantFailure; }
	if (bActivated && Observation.State == EEFCalystoDecalRealizationState::Committed) return EEFCalystoActivationResult::Activated;
	if (Pool && !Pool->ActivateCommitted(Token, Error))
	{ Fail(EEFCalystoAttemptFailure::Configuration, TEXT("DecalActivationInvariant"), Error); return EEFCalystoActivationResult::InvariantFailure; }
	bActivated = true; Observation.State = EEFCalystoDecalRealizationState::Committed; return EEFCalystoActivationResult::Activated;
}

void FEFCalystoDecalMaterializer::Release(const FEFCalystoAttemptToken& InToken)
{
	check(IsInGameThread());
	if (!(InToken == Token) || Observation.State == EEFCalystoDecalRealizationState::Idle
		|| Observation.State == EEFCalystoDecalRealizationState::Released || Observation.State == EEFCalystoDecalRealizationState::Releasing) return;
	Observation.State = EEFCalystoDecalRealizationState::Releasing; Observation.VerifiedElements.Reset(); Observation.RealizationHash.Reset();
	if (LoadHandle && !LoadHandle->HasLoadCompleted()) LoadHandle->CancelHandle();
	if (Pool) Pool->Release(Token);
}

void FEFCalystoDecalMaterializer::StepRelease()
{
	if (Observation.State != EEFCalystoDecalRealizationState::Releasing) return;
	if (Pool)
	{
		Pool->Release(Token);
		if (!Pool->IsActorBeingDestroyed() && !Pool->Destroy(false, false)) return;
		const ULevel* Level = Pool->GetLevel(); TArray<UActorComponent*> Components; Pool->GetComponents(Components);
		bool bRegistered = false; for (const UActorComponent* Component : Components) bRegistered |= IsValid(Component) && Component->IsRegistered();
		if ((Level && Level->Actors.Contains(Pool)) || bRegistered) return;
		Pool = nullptr;
	}
	if (LoadHandle) { LoadHandle->ReleaseHandle(); LoadHandle.Reset(); }
	Resources.Reset(); Dependencies.Reset(); Manifest.Reset(); Native.Reset(); Configuration.Reset(); World.Reset();
	Observation.State = EEFCalystoDecalRealizationState::Released;
}

void FEFCalystoDecalMaterializer::UpdateCounts()
{
	Observation.OwnedPoolActors = Pool ? 1 : 0;
	Observation.RetainedLeases = LoadHandle.IsValid() ? 1 : 0;
}

FEFCalystoRollbackEvidence FEFCalystoDecalMaterializer::GetReleaseEvidence(const FEFCalystoAttemptToken& InToken) const
{
	FEFCalystoRollbackEvidence Evidence;
	if (!(InToken == Token)) { Evidence.PendingCallbacks = 1; return Evidence; }
	Evidence.Actors = Pool ? 1 : 0;
	Evidence.Decals = Pool ? AEFCalystoDecalPoolOwner::PoolCapacity : 0;
	Evidence.Reservations = Manifest ? Manifest->GetElements().Num() : 0;
	Evidence.TransientReferences = (Pool ? 1 : 0) + (Native.IsValid() ? 1 : 0);
	Evidence.AttemptLeases = LoadHandle.IsValid() ? 1 : 0;
	Evidence.bPCGCleanupComplete = Observation.State == EEFCalystoDecalRealizationState::Released;
	return Evidence;
}
