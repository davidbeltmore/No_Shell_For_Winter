#include "Calysto/EFCalystoDirectorSubsystem.h"
#include "Calysto/EFCalystoAsyncLoadingBudget.h"
#include "Calysto/EFCalystoContentAttemptProvider.h"
#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDirectorTestFixture.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Features/IModularFeatures.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCalystoDirector, Log, All);

namespace
{
	FString DirectorConfigurationHash(const UEFCalystoDungeonDirectorAsset& Asset)
	{
		FString Text;
		for (TFieldIterator<FProperty> It(Asset.GetClass()); It; ++It)
		{
			if (It->GetOwnerStruct() == Asset.GetClass())
			{
				Text += It->GetName();
				It->ExportText_InContainer(0, Text, &Asset, nullptr, nullptr, PPF_None);
			}
		}
		return FMD5::HashAnsiString(*Text);
	}
	bool IsTerminal(const EEFCalystoDirectorState State)
	{
		return State == EEFCalystoDirectorState::Idle || State == EEFCalystoDirectorState::Ready
			|| State == EEFCalystoDirectorState::Failed || State == EEFCalystoDirectorState::Cancelled;
	}
	void CanonicalizePaths(TArray<FSoftObjectPath>& Paths)
	{
		Paths.RemoveAll([](const FSoftObjectPath& Path) { return Path.IsNull(); });
		Paths.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
		for (int32 Index = Paths.Num() - 1; Index > 0; --Index)
			if (Paths[Index] == Paths[Index - 1]) Paths.RemoveAt(Index);
	}
	IEFCalystoContentAttemptProvider* ResolveUniqueContentProvider(FString& Error)
	{
		Error.Reset();
		const TArray<IEFCalystoContentAttemptProvider*> Providers =
			IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoContentAttemptProvider>(
				IEFCalystoContentAttemptProvider::GetModularFeatureName());
		TArray<IEFCalystoContentAttemptProvider*> Valid;
		for (IEFCalystoContentAttemptProvider* Provider : Providers) if (Provider) Valid.Add(Provider);
		if (Valid.Num() != 1)
		{
			Error = Valid.IsEmpty()
				? TEXT("The required V7 project gameplay snapshot provider is unavailable.")
				: TEXT("More than one V7 project gameplay snapshot provider is registered.");
			return nullptr;
		}
		return Valid[0];
	}
}

bool UEFCalystoDirectorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{ return Super::ShouldCreateSubsystem(Outer) && UEFCalystoDirectorSettings::IsEnabled(); }

void UEFCalystoDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UEFProceduralRuntimeSubsystem>();
	GetGameInstance()->GetSubsystem<UEFProceduralRuntimeSubsystem>()->RegisterProvider(this);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickRequest), 0.05f);
}

void UEFCalystoDirectorSubsystem::Deinitialize()
{
	bDeinitializing = true;
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	InvalidatePendingLoads();
	if (AttemptRuntime && Transaction && bAttemptStarted && !bReleaseStarted)
		AttemptRuntime->ReleaseAttempt(Transaction->GetToken());
	// A source-world snapshot which never crossed into the runtime still owns an
	// ordinary same-world lease. Complete that lease before project subsystems
	// begin their own shutdown. A handed-off detached graph remains retained by
	// the attempt runtime until its token-scoped cleanup has completed.
	if (PreTravelGameplaySnapshot && !bPreTravelGameplayHandedToRuntime)
		FinishPreTravelGameplaySnapshot(PreTravelGameplayRequest, false);
	if (GetGameInstance())
		if (auto* Providers = GetGameInstance()->GetSubsystem<UEFProceduralRuntimeSubsystem>()) Providers->UnregisterProvider(this);
	ReleaseLoads(); ReleaseSharedDependencies(); AttemptRuntime.Reset(); Configuration.Reset(); Transaction.Reset();
	Super::Deinitialize();
}

bool UEFCalystoDirectorSubsystem::RegisterRuntime(TSharedRef<IEFCalystoDirectorAttemptRuntime> Runtime)
{
	if (bDeinitializing || bRuntimeUnregisterPending || bReleaseStarted) return false;
	if (AttemptRuntime && AttemptRuntime.Get() != &Runtime.Get()) return false;
	if (AttemptRuntime) return true;
	AttemptRuntime = Runtime;
	StartSharedDependencyLoading();
	return AttemptRuntime.Get() == &Runtime.Get();
}

void UEFCalystoDirectorSubsystem::UnregisterRuntime(const IEFCalystoDirectorAttemptRuntime* Runtime)
{
	if (AttemptRuntime.Get() != Runtime || bRuntimeUnregisterPending || bDeinitializing) return;
	if (bAttemptStarted || bReleaseStarted)
	{
		// Keep the actual owner alive until its token-scoped release evidence is complete.
		bRuntimeUnregisterPending = true;
		Fail(TEXT("RuntimeUnavailable"), TEXT("The dungeon runtime is unavailable."), EEFCalystoAttemptFailure::Resource);
		return;
	}
	ReleaseSharedDependencies(); AttemptRuntime.Reset();
	if (IsTravelRequestPending())
		Fail(TEXT("RuntimeUnavailable"), TEXT("The dungeon runtime is unavailable."), EEFCalystoAttemptFailure::Resource);
}

void UEFCalystoDirectorSubsystem::ReleaseSharedDependencies()
{
	SharedLoadGeneration.Invalidate(); bSharedLoadPending = false; bSharedLoadReady = false;
	if (SharedLease) { SharedLease->CancelHandle(); SharedLease.Reset(); }
	SharedPaths.Reset(); SharedLoadFailure.Reset();
	SharedLoadStartedSeconds = SharedLoadCompletedSeconds = -1;
	RefreshAsyncLoadingBudget();
}

void UEFCalystoDirectorSubsystem::RefreshAsyncLoadingBudget()
{
	const bool bLoading = bSharedLoadPending || bConfigurationLoadPending || (bVisualLoadPending && !bVisualDependenciesLoaded);
	EFCalystoAsyncLoadingBudget::SetOwnerActive(this, bLoading && !bDeinitializing && !bReleaseStarted
		&& !bRuntimeUnregisterPending && (Snapshot.State == EEFCalystoDirectorState::Idle || RoutingRequest.IsValid())
		&& !bCancelAfterRelease && !bReturnToHubAfterRelease && !bFailurePublished
		&& Snapshot.State != EEFCalystoDirectorState::Failed && Snapshot.State != EEFCalystoDirectorState::Cancelled);
}

void UEFCalystoDirectorSubsystem::StartSharedDependencyLoading()
{
	if (!AttemptRuntime || bDeinitializing || bRuntimeUnregisterPending || bReleaseStarted) return;
	ReleaseSharedDependencies();
	const auto Runtime = AttemptRuntime;
	const FGuid Generation = SharedLoadGeneration = FGuid::NewGuid();
	SharedLoadStartedSeconds = FPlatformTime::Seconds();
	TArray<FSoftObjectPath> Paths = Runtime->GetSharedDependencies();
	if (bDeinitializing || Generation != SharedLoadGeneration || AttemptRuntime != Runtime) return;
	// This is a finite compatibility dependency list, not an arbitrary content scan.
	if (Paths.Num() > 1024)
	{
		SharedLoadFailure = TEXT("The native integration declared too many shared dependencies.");
		SharedLoadCompletedSeconds = FPlatformTime::Seconds();
		return;
	}
	CanonicalizePaths(Paths); SharedPaths = MoveTemp(Paths); bSharedLoadPending = true;
	RefreshAsyncLoadingBudget();
	if (SharedPaths.IsEmpty()) { HandleSharedDependenciesLoaded(Generation); return; }
	// Set priority on the initial request: UE 5.8 SetPriority only affects JIT loads.
	auto RequestedLease = UAssetManager::GetStreamableManager().RequestAsyncLoad(SharedPaths,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleSharedDependenciesLoaded, Generation),
		FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("CalystoDirector.SharedDependencies"));
	if (bDeinitializing || Generation != SharedLoadGeneration || AttemptRuntime != Runtime)
	{ if (RequestedLease) RequestedLease->CancelHandle(); return; }
	SharedLease = MoveTemp(RequestedLease);
	if (!SharedLease)
	{
		bSharedLoadPending = false; bSharedLoadReady = false;
		SharedLoadFailure = TEXT("Shared dungeon resources could not be requested.");
		SharedLoadCompletedSeconds = FPlatformTime::Seconds();
		RefreshAsyncLoadingBudget();
		TryFinishDependencyLoading(RoutingRequest);
	}
}

double UEFCalystoDirectorSubsystem::GetSharedDependencyDeadlineSeconds() const
{
	// HUB preparation is bounded independently until a real request owns the wait.
	// Reuse its lease and generation; prewarming must not shorten the floor's 30s budget.
	const bool bServingRequest = RoutingRequest.IsValid() && IsTravelRequestPending()
		&& !bCancelAfterRelease && !bReturnToHubAfterRelease;
	return (bServingRequest ? RequestStartedSeconds : SharedLoadStartedSeconds)
		+ FEFCalystoFloorTransaction::RequestBudgetSeconds;
}

void UEFCalystoDirectorSubsystem::HandleSharedDependenciesLoaded(const FGuid Generation)
{
	if (bDeinitializing || !Generation.IsValid() || Generation != SharedLoadGeneration
		|| !AttemptRuntime || bRuntimeUnregisterPending || !bSharedLoadPending) return;
	bSharedLoadPending = false; SharedLoadCompletedSeconds = FPlatformTime::Seconds();
	if (SharedLoadCompletedSeconds >= GetSharedDependencyDeadlineSeconds())
		SharedLoadFailure = TEXT("Shared dungeon resources did not finish loading within 30 seconds.");
	for (const auto& Path : SharedPaths)
		if (!Path.ResolveObject())
		{ SharedLoadFailure = TEXT("A shared dungeon resource is unavailable: ") + Path.ToString(); break; }
	bSharedLoadReady = SharedLoadFailure.IsEmpty();
	RefreshAsyncLoadingBudget();
	// Session completion may join a current request, but never starts one.
	TryFinishDependencyLoading(RoutingRequest);
}

FEFCalystoDirectorSnapshot UEFCalystoDirectorSubsystem::GetSnapshot() const
{
	FEFCalystoDirectorSnapshot Result = Snapshot;
	if (IsTravelRequestPending()) Result.RequestElapsedSeconds = FPlatformTime::Seconds() - RequestStartedSeconds;
	Result.bCanRetry = Snapshot.State == EEFCalystoDirectorState::Failed && !bReleaseStarted;
	Result.bCanReturnToHub = HasActiveRun() && !bReturnToHubAfterRelease && GetWorld()
		&& !GetDefault<UEFCalystoDirectorSettings>()->HubMap.IsNull();
	if (Transaction)
	{
		Result.AttemptCount = Transaction->GetAttempts().Num();
		if (!Transaction->GetAttempts().IsEmpty()) Result.TopologySeed = Transaction->GetAttempts().Last().TopologySeed;
	}
	return Result;
}

FString UEFCalystoDirectorSubsystem::GetDiagnosticsJson() const
{
	// Do not call runtime ObserveAttempt/ObserveRelease here: even a const native
	// observer may advance cleanup. This exports retained transaction evidence only.
	if (!IsInGameThread()) return TEXT("{\"schema_version\":1,\"error\":\"GameThreadRequired\"}");
	const FEFCalystoDirectorSnapshot Current = GetSnapshot();
	const FEFCalystoAttemptToken Token = Transaction ? Transaction->GetToken() : FEFCalystoAttemptToken();
	const FEFCalystoCommitEvidence* Evidence = Transaction ? Transaction->GetVerifiedCommitEvidence() : nullptr;
	const FTransform* Entry = Evidence && !Evidence->Entry.ContainsNaN() ? &Evidence->Entry : nullptr;
	const auto JsonBool = [](const bool Value) { return Value ? TEXT("true") : TEXT("false"); };
	// Only bounded error codes need escaping. GUIDs and enum names below use their
	// canonical native representation; arbitrary asset names are not serialized.
	const auto JsonString = [](const FString& Value)
	{
		FString Escaped(TEXT("\""));
		for (const TCHAR C : Value.Left(256))
		{
			if (C == TCHAR('"') || C == TCHAR('\\')) { Escaped += TCHAR('\\'); Escaped += C; }
			else if (static_cast<uint32>(C) < 0x20u) Escaped.Appendf(TEXT("\\u%04x"), static_cast<uint32>(C));
			else Escaped += C;
		}
		return Escaped + TEXT("\"");
	};
	const TCHAR* PhaseNames[] = { TEXT("Idle"), TEXT("Preflight"), TEXT("NativeGeneration"),
		TEXT("StructuralVerification"), TEXT("NavigationAndReservations"), TEXT("RealizationVerification"),
		TEXT("Commit"), TEXT("PlayerRelease"), TEXT("Ready"), TEXT("RollingBack"), TEXT("RetryAvailable"),
		TEXT("Failed"), TEXT("Cancelled") };
	const int32 Phase = Transaction ? static_cast<int32>(Transaction->GetPhase()) : 0;
	FString Json;
	Json.Reserve(4096);
	// JSON numbers are formatted directly from int64. Passing run seeds through
	// a double-backed JSON DOM would lose reproducibility above 2^53.
	Json.Appendf(TEXT("{\"schema_version\":1,\"run_seed\":%lld,\"run_epoch\":%lld,\"floor_number\":%lld,"
		"\"last_committed_floor_number\":%lld,\"last_committed_run_epoch\":%lld,\"reroll_index\":%d,"
		"\"state\":\"%s\",\"style_id\":\"%s\",\"request_id\":\"%s\",\"attempt_id\":\"%s\","
		"\"routing_request_id\":\"%s\",\"transaction_phase\":\"%s\",\"topology_seed\":%d,"),
		Current.RunSeed, Current.RunEpoch, Current.FloorNumber, Current.LastCommittedFloorNumber,
		Current.LastCommittedRunEpoch, Current.RerollIndex,
		*StaticEnum<EEFCalystoDirectorState>()->GetNameStringByValue(static_cast<int64>(Current.State)),
		*Current.StyleId.ToString(EGuidFormats::DigitsWithHyphens), *Token.Request.ToString(EGuidFormats::DigitsWithHyphens),
		*Token.Attempt.ToString(EGuidFormats::DigitsWithHyphens), *RoutingRequest.ToString(EGuidFormats::DigitsWithHyphens),
		Phase >= 0 && Phase < UE_ARRAY_COUNT(PhaseNames) ? PhaseNames[Phase] : TEXT("Unknown"), Current.TopologySeed);
	Json.Appendf(TEXT("\"native_floor_verified\":%s,\"gameplay_verified\":%s,\"transaction_committed\":%s,"
		"\"has_validated_evidence\":%s,\"release_in_progress\":%s,\"cleanup_failed\":%s,"
		"\"terminal_gameplay_recovery_pending\":%s,\"terminal_gameplay_recovery_message\":%s,"
		"\"can_retry\":%s,\"can_return_to_hub\":%s,\"request_budget_seconds\":%.17g,\"maximum_attempts\":%d,"),
		JsonBool(Current.bNativeFloorVerified), JsonBool(Current.bGameplayVerified),
		JsonBool(Transaction && Transaction->HasCommittedGameplay()), JsonBool(Evidence != nullptr),
		JsonBool(bReleaseStarted), JsonBool(bCleanupFailed), JsonBool(bTerminalGameplayRecoveryPending),
		*JsonString(TerminalGameplayRecoveryMessage), JsonBool(Current.bCanRetry), JsonBool(Current.bCanReturnToHub),
		FEFCalystoFloorTransaction::RequestBudgetSeconds, FEFCalystoFloorTransaction::MaximumAttempts);
	Json += TEXT("\"request_elapsed_seconds\":");
	if (FMath::IsFinite(Current.RequestElapsedSeconds)) Json.Appendf(TEXT("%.17g"), Current.RequestElapsedSeconds);
	else Json += TEXT("null");
	Json += TEXT(",\"player_view\":");
	const UWorld* ObservedWorld = GetWorld();
	const APlayerController* ViewController = ObservedWorld ? ObservedWorld->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* ViewCamera = ViewController ? ViewController->PlayerCameraManager.Get() : nullptr;
	if (IsValid(ViewCamera) && FMath::IsFinite(ViewCamera->FadeAmount))
	{
		const UGameViewportClient* Viewport = ObservedWorld->GetGameViewport();
		Json.Appendf(TEXT("{\"fade_enabled\":%s,\"fade_amount\":%.17g,\"viewport_present\":%s,\"world_rendering_disabled\":%s}"),
			JsonBool(ViewCamera->bEnableFading), double(ViewCamera->FadeAmount), JsonBool(IsValid(Viewport)),
			JsonBool(Viewport && Viewport->bDisableWorldRendering));
	}
	else Json += TEXT("null");
	Json += TEXT(",\"remaining_deadline_seconds\":");
	if (IsTravelRequestPending() && FMath::IsFinite(RequestStartedSeconds))
		Json.Appendf(TEXT("%.17g"), FMath::Max(0.0, RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds - FPlatformTime::Seconds()));
	else Json += TEXT("null");
	Json += TEXT(",\"entry_location\":");
	if (Entry)
	{
		const FVector Location = Entry->GetLocation();
		Json.Appendf(TEXT("[%.17g,%.17g,%.17g]"), Location.X, Location.Y, Location.Z);
	}
	else Json += TEXT("null");
	Json.Appendf(TEXT(",\"unique_owned_start_and_end\":%s,\"blocking_floor_and_capsule_clearance\":%s,"
		"\"complete_relevant_navigation_route\":%s,\"room_theme_contract_verified\":%s,\"materials_verified\":%s,"
		"\"native_parity_verified\":%s,\"reserved_element_count\":%d,\"verified_element_count\":%d,"),
		JsonBool(Evidence && Evidence->bUniqueOwnedStartAndEnd), JsonBool(Evidence && Evidence->bBlockingFloorAndCapsuleClearance),
		JsonBool(Evidence && Evidence->bCompleteRelevantNavigationRoute), JsonBool(Evidence && Evidence->bRoomThemeContractVerified),
		JsonBool(Evidence && Evidence->bMaterialsVerified), JsonBool(Evidence && Evidence->bNativeParityVerified),
		Evidence ? Evidence->ReservedElements.Num() : 0, Evidence ? Evidence->VerifiedElements.Num() : 0);
	const int32 AttemptCount = Transaction ? Transaction->GetAttempts().Num() : 0;
	Json.Appendf(TEXT("\"attempt_count\":%d,\"attempts_truncated\":%s,\"attempts\":["), AttemptCount,
		JsonBool(AttemptCount > FEFCalystoFloorTransaction::MaximumAttempts));
	for (int32 Index = 0; Index < FMath::Min(AttemptCount, FEFCalystoFloorTransaction::MaximumAttempts); ++Index)
	{
		const FEFCalystoAttemptRecord& Attempt = Transaction->GetAttempts()[Index];
		if (Index != 0) Json += TEXT(",");
		const FEFCalystoAttemptMetrics* Metrics = AttemptMetrics.Find(Attempt.Token.Attempt);
		Json.Appendf(TEXT("{\"request_id\":\"%s\",\"attempt_id\":\"%s\",\"attempt_index\":%d,\"topology_seed\":%d,"
			"\"root_generation_count_source\":\"TransactionAuthorization\","
			"\"root_generation_requests\":%d,\"accepted\":%s,\"cleanup_verified\":%s,\"failure_code\":%s,"),
			*Attempt.Token.Request.ToString(EGuidFormats::DigitsWithHyphens), *Attempt.Token.Attempt.ToString(EGuidFormats::DigitsWithHyphens),
			Attempt.AttemptIndex, Attempt.TopologySeed, Attempt.RootGenerationRequests,
			JsonBool(Attempt.bAccepted), JsonBool(Attempt.bCleanupVerified),
			*JsonString(Attempt.FailureCode.IsNone() ? FString() : Attempt.FailureCode.ToString()));
		Json += TEXT("\"actual_root_generation_requests\":");
		if (Metrics && Metrics->GenerateLocalCalls >= 0) Json.Appendf(TEXT("%d"), Metrics->GenerateLocalCalls);
		else Json += TEXT("null");
		Json += TEXT(",\"native_observation\":");
		if (!Metrics) Json += TEXT("null");
		else
		{
			Json.Appendf(TEXT("{\"code\":%s,\"message\":%s,\"floor_instances\":%d,\"wall_instances\":%d,\"roof_instances\":%d,"
				"\"navigation_data\":%s,\"navigation_agent_radius_cm\":%.9g,\"navigation_agent_height_cm\":%.9g,\"navigation_default_cell_size_cm\":%.9g,"
				"\"active_navigation_tiles\":%d,\"bounds_registered\":%s,\"navigation_building\":%s,"
				"\"start_projected\":%s,\"end_projected\":%s,\"route_points\":%d,"),
				*JsonString(Metrics->ObservationCode.ToString()), *JsonString(Metrics->ObservationMessage),
				Metrics->FloorInstances, Metrics->WallInstances, Metrics->RoofInstances,
				*JsonString(Metrics->NavigationDataPath), Metrics->NavigationAgentRadius, Metrics->NavigationAgentHeight, Metrics->NavigationDefaultCellSize,
				Metrics->ActiveNavigationTiles, JsonBool(Metrics->bBoundsRegistered), JsonBool(Metrics->bNavigationBuilding),
				JsonBool(Metrics->bStartProjected), JsonBool(Metrics->bEndProjected), Metrics->RoutePoints);
			Json += TEXT("\"native_output_seconds\":");
			if (FMath::IsFinite(Metrics->NativeOutputSeconds) && Metrics->NativeOutputSeconds >= 0)
				Json.Appendf(TEXT("%.17g"), Metrics->NativeOutputSeconds);
			else Json += TEXT("null");
			Json += TEXT(",\"navigation_observation_seconds\":");
			if (FMath::IsFinite(Metrics->NavigationObservationSeconds) && Metrics->NavigationObservationSeconds >= 0)
				Json.Appendf(TEXT("%.17g"), Metrics->NavigationObservationSeconds);
			else Json += TEXT("null");
			Json.Appendf(TEXT(",\"architecture_opportunities\":%d,\"reserved_architecture_parents\":%d,"
				"\"reserved_architecture_meshes\":%d,\"verified_architecture_meshes\":%d,\"surface_physics_queries\":%d,"),
				Metrics->ArchitectureOpportunities,Metrics->ReservedArchitectureParents,Metrics->ReservedArchitectureMeshes,
				Metrics->VerifiedArchitectureMeshes,Metrics->SurfacePhysicsQueries);
			Json.Appendf(TEXT("\"surface_geometry_rejections\":%d,\"surface_protection_rejections\":%d,\"first_architecture_rejection\":%s,"),
				Metrics->SurfaceGeometryRejections, Metrics->SurfaceProtectionRejections, *JsonString(Metrics->FirstArchitectureRejection));
			Json+=TEXT("\"architecture_reservation_seconds\":");
			Json+=FMath::IsFinite(Metrics->ArchitectureReservationSeconds) && Metrics->ArchitectureReservationSeconds>=0
				? FString::Printf(TEXT("%.17g"),Metrics->ArchitectureReservationSeconds) : TEXT("null");
			Json+=TEXT(",\"architecture_realization_seconds\":");
			Json+=FMath::IsFinite(Metrics->ArchitectureRealizationSeconds) && Metrics->ArchitectureRealizationSeconds>=0
				? FString::Printf(TEXT("%.17g"),Metrics->ArchitectureRealizationSeconds) : TEXT("null");
			Json += TEXT("}");
		}
		Json += TEXT("}");
	}
	Json.Appendf(TEXT("],\"shared_dependencies\":{\"path_count\":%d,\"pending\":%s,\"ready\":%s,"
		"\"ready_at_request_start\":%s,\"failure\":%s,\"lease_priority\":"),
		SharedPaths.Num(), JsonBool(bSharedLoadPending), JsonBool(bSharedLoadReady), JsonBool(bSharedReadyAtRequestStart),
		*JsonString(SharedLoadFailure));
	Json += SharedLease ? FString::FromInt(SharedLease->GetPriority()) : TEXT("null");
	Json.Appendf(TEXT(",\"handle_completed\":%s,\"unresolved_paths\":["), JsonBool(SharedLease && SharedLease->HasLoadCompleted()));
	int32 UnresolvedPathCount = 0;
	for (const auto& Path : SharedPaths)
	{
		if (Path.ResolveObject()) continue;
		if (UnresolvedPathCount < 16)
		{
			if (UnresolvedPathCount > 0) Json += TEXT(",");
			Json += JsonString(Path.ToString());
		}
		++UnresolvedPathCount;
	}
	Json.Appendf(TEXT("],\"unresolved_path_count\":%d,\"unresolved_paths_truncated\":%s},"
		"\"request_visual_path_count\":%d,\"async_loading_budget_ms\":%.9g,\"phase_timings\":{"),
		UnresolvedPathCount, JsonBool(UnresolvedPathCount > 16), RequestVisualPathCount,
		EFCalystoAsyncLoadingBudget::GetEffectiveMilliseconds());
	const double ObservedSeconds = FPlatformTime::Seconds();
	const auto AppendDuration = [&Json](const TCHAR* Name, const double Start, const double End)
	{
		Json.Appendf(TEXT("\"%s\":"), Name);
		if (FMath::IsFinite(Start) && FMath::IsFinite(End) && Start >= 0.0 && End >= Start)
			Json.Appendf(TEXT("%.17g"), End - Start);
		else Json += TEXT("null");
	};
	AppendDuration(TEXT("shared_async_load_seconds"), SharedLoadStartedSeconds,
		bSharedLoadPending ? ObservedSeconds : SharedLoadCompletedSeconds);
	Json += TEXT(",");
	AppendDuration(TEXT("configuration_preparation_seconds"), ConfigurationLoadStartedSeconds, ConfigurationLoadCompletedSeconds);
	Json += TEXT(",");
	AppendDuration(TEXT("configuration_compile_seconds"), 0, ConfigurationCompileSeconds);
	Json += TEXT(",");
	AppendDuration(TEXT("request_visual_async_load_seconds"), VisualLoadStartedSeconds, VisualLoadCompletedSeconds);
	Json += TEXT(",");
	AppendDuration(TEXT("baked_visual_async_load_seconds"), BakedVisualLoadStartedSeconds, BakedVisualLoadCompletedSeconds);
	Json.Appendf(TEXT(",\"retained_request_visual_leases\":%d,"),int32(VisualLease.IsValid())+int32(BakedVisualLease.IsValid()));
	AppendDuration(TEXT("request_to_dependencies_ready_seconds"), RequestStartedSeconds, DependenciesReadySeconds);
	Json += TEXT("}}");
	return Json;
}

bool UEFCalystoDirectorSubsystem::IsTravelRequestPending() const
{ return !IsTerminal(Snapshot.State) || bSetupPending || (bReleaseStarted && !bCleanupFailed); }

bool UEFCalystoDirectorSubsystem::RequestStartNewRun()
{
#if !UE_BUILD_SHIPPING
	// Development replay still enters through the real door and normal request path.
	// This seed is never a callback identity or a replacement for the topology seed.
	FString RunSeedText;
	if ((FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorNativeParity"))
		|| FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorCandidate")))
		&& FParse::Value(FCommandLine::Get(), TEXT("CalystoDirectorRunSeed="), RunSeedText))
	{
		int64 RunSeed = 0;
		if (!LexTryParseString(RunSeed, *RunSeedText))
		{
			UE_LOG(LogCalystoDirector, Error, TEXT("CalystoDirectorRunSeed requires a signed 64-bit integer."));
			return false;
		}
		return RequestStartNewRunWithSeed(RunSeed);
	}
#endif
	const FGuid Seed = FGuid::NewGuid();
	return RequestStartNewRunWithSeed(static_cast<int64>((static_cast<uint64>(Seed.A) << 32) | Seed.B));
}

bool UEFCalystoDirectorSubsystem::RequestStartNewRunWithSeed(const int64 Seed)
{
	if (IsTravelRequestPending() || bReleaseStarted || bRuntimeUnregisterPending || bDeinitializing || Snapshot.RunEpoch == MAX_int64) return false;
	FString RecoveryError;
	if (!ResolveTerminalPreTravelGameplaySnapshot(RecoveryError))
	{
		Snapshot.FailureMessage = RecoveryError.IsEmpty()
			? TEXT("Persistent gameplay recovery is still pending. The player remains protected.")
			: RecoveryError;
		UE_LOG(LogCalystoDirector, Warning, TEXT("TerminalGameplayRecoveryPending: %s"), *Snapshot.FailureMessage);
		return false;
	}
	Snapshot.RunSeed = Seed; ++Snapshot.RunEpoch; Snapshot.StyleId.Invalidate();
	return BeginRequest(1, 0, false);
}

bool UEFCalystoDirectorSubsystem::RequestAdvanceFloor()
{ return Snapshot.State == EEFCalystoDirectorState::Ready && Snapshot.FloorNumber < MAX_int64 && BeginRequest(Snapshot.FloorNumber + 1, 0, false); }

bool UEFCalystoDirectorSubsystem::RequestReplayCurrentFloor()
{ return Snapshot.State == EEFCalystoDirectorState::Ready && BeginRequest(Snapshot.FloorNumber, Snapshot.RerollIndex, true); }

bool UEFCalystoDirectorSubsystem::RequestRerollCurrentFloor()
{ return Snapshot.State == EEFCalystoDirectorState::Ready && Snapshot.RerollIndex < MAX_int32 && BeginRequest(Snapshot.FloorNumber, Snapshot.RerollIndex + 1, true); }

bool UEFCalystoDirectorSubsystem::RequestRetry()
{ return GetSnapshot().bCanRetry && Snapshot.RerollIndex < MAX_int32 && BeginRequest(Snapshot.FloorNumber, Snapshot.RerollIndex + 1, true); }

bool UEFCalystoDirectorSubsystem::BeginRequest(const int64 Floor, const int32 Reroll, const bool PreserveStyle)
{
	if (IsTravelRequestPending() || bReleaseStarted || bRuntimeUnregisterPending || bDeinitializing || Floor < 1 || Reroll < 0) return false;
	FString RecoveryError;
	if (!ResolveTerminalPreTravelGameplaySnapshot(RecoveryError))
	{
		Snapshot.FailureMessage = RecoveryError.IsEmpty()
			? TEXT("Persistent gameplay recovery is still pending. The player remains protected.")
			: RecoveryError;
		UE_LOG(LogCalystoDirector, Warning, TEXT("TerminalGameplayRecoveryPending: %s"), *Snapshot.FailureMessage);
		return false;
	}
	InvalidatePendingLoads();
	RequestStartedSeconds = FPlatformTime::Seconds(); RoutingRequest = FGuid::NewGuid();
	ConfigurationLoadStartedSeconds = ConfigurationLoadCompletedSeconds = ConfigurationCompileSeconds = -1;
	VisualLoadStartedSeconds = VisualLoadCompletedSeconds = DependenciesReadySeconds = -1;
	BakedVisualLoadStartedSeconds=BakedVisualLoadCompletedSeconds=-1;
	RequestVisualPathCount = 0; bSharedReadyAtRequestStart = bSharedLoadReady;
	Snapshot.FloorNumber = Floor; Snapshot.RerollIndex = Reroll; Snapshot.State = EEFCalystoDirectorState::Loading;
	Snapshot.FailureMessage.Reset(); Snapshot.bGameplayVerified = false; Snapshot.bNativeFloorVerified = false; Snapshot.RequestElapsedSeconds = 0;
	if (!PreserveStyle) Snapshot.StyleId.Invalidate();
	bPreserveSelectedStyle = PreserveStyle; bTravelIssued = false; bFailurePublished = false;
	bReturnToHubAfterRelease = false; bCancelAfterRelease = false; bSetupPending = true;
	bCleanupFailed = false; bHubTravelIssued = false; DepartingWorld.Reset();
	if (!SharedLoadFailure.IsEmpty() && !bAttemptStarted) StartSharedDependencyLoading();
	RefreshAsyncLoadingBudget();
	BeforeTravel.Broadcast(Floor);
	if (Transaction && bAttemptStarted) BeginRollback();
	return true;
}

bool UEFCalystoDirectorSubsystem::CapturePreTravelGameplaySnapshot(FString& Error)
{
	Error.Reset();
	if (!RoutingRequest.IsValid() || !GetWorld())
	{
		Error = TEXT("V7 gameplay capture requires the active routing request and source world.");
		return false;
	}
	if (PreTravelGameplaySnapshot)
	{
		if (PreTravelGameplayRequest == RoutingRequest && PreTravelGameplayProvider
			&& !PreTravelGameplaySnapshot->GetCanonicalHash().IsEmpty()) return true;
		Error = TEXT("A previous V7 gameplay snapshot still owns persistent travel state.");
		return false;
	}
	IEFCalystoContentAttemptProvider* Provider = ResolveUniqueContentProvider(Error);
	if (!Provider) return false;
	TSharedPtr<IEFCalystoGameplaySnapshot> Captured;
	if (!Provider->CapturePreTravelSnapshot(RoutingRequest, GetWorld(), Captured, Error)
		|| !Captured || Captured->GetCanonicalHash().IsEmpty())
	{
		if (Error.IsEmpty()) Error = TEXT("The V7 pre-travel gameplay snapshot is unavailable or incomplete.");
		return false;
	}
	PreTravelGameplaySnapshot = MoveTemp(Captured);
	PreTravelGameplayProvider = Provider;
	PreTravelGameplayRequest = RoutingRequest;
	bPreTravelGameplayDetached = false;
	bPreTravelGameplayHandedToRuntime = false;
	bTerminalGameplayRecoveryPending = false;
	TerminalGameplayRecoveryMessage.Reset();
	return true;
}

bool UEFCalystoDirectorSubsystem::PrepareAndDetachPreTravelGameplaySnapshot(FString& Error)
{
	Error.Reset();
	if (!RoutingRequest.IsValid() || !PreTravelGameplaySnapshot || !PreTravelGameplayProvider
		|| PreTravelGameplayRequest != RoutingRequest)
	{
		Error = TEXT("V7 travel requires the exact pre-floor gameplay snapshot captured for this routing request.");
		return false;
	}
	if (bPreTravelGameplayDetached) return true;
	if (!PreTravelGameplayProvider->PreparePreTravelSnapshot(RoutingRequest, PreTravelGameplaySnapshot, Error)) return false;
	if (FPlatformTime::Seconds() >= RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds)
	{
		Error = TEXT("The shared floor request deadline expired during pre-travel gameplay preparation.");
		return false;
	}
	if (!PreTravelGameplayProvider->DetachPreTravelSnapshot(RoutingRequest, PreTravelGameplaySnapshot, Error)) return false;
	bPreTravelGameplayDetached = true;
	return true;
}

bool UEFCalystoDirectorSubsystem::ResolveTerminalPreTravelGameplaySnapshot(FString& Error)
{
	Error.Reset();
	const bool bNeedsRecovery = bTerminalGameplayRecoveryPending
		|| (PreTravelGameplaySnapshot && PreTravelGameplaySnapshot->IsDetachedForTravel());
	if (!bNeedsRecovery) return true;
	if (!PreTravelGameplaySnapshot || !PreTravelGameplayProvider || !PreTravelGameplayRequest.IsValid())
	{
		Error = TEXT("The protected V7 gameplay recovery lease is incomplete.");
		return false;
	}
	FEFCalystoAttemptToken RecoveryToken = ReleasingToken;
	if (!RecoveryToken.Request.IsValid() && Transaction) RecoveryToken = Transaction->GetToken();
	if (RecoveryToken.Request != PreTravelGameplayRequest || !RecoveryToken.Attempt.IsValid())
	{
		Error = TEXT("The protected V7 gameplay recovery lease no longer has its owned destination attempt.");
		return false;
	}
	UWorld* DestinationWorld = AttemptWorld.Get();
	if (!DestinationWorld) DestinationWorld = GetWorld();
	if (!IsValid(DestinationWorld) || !DestinationWorld->IsGameWorld())
	{
		Error = TEXT("The protected V7 gameplay recovery destination is unavailable. Retry after the destination is ready.");
		return false;
	}
	if (!PreTravelGameplayProvider->RecoverDetachedSnapshot(RecoveryToken, DestinationWorld,
		PreTravelGameplaySnapshot, Error))
	{
		if (Error.IsEmpty()) Error = TEXT("The protected V7 gameplay state could not be reconstructed in the destination.");
		return false;
	}
	if (PreTravelGameplaySnapshot)
	{
		Error = TEXT("The recovered V7 gameplay lease is still retained.");
		return false;
	}
	const FGuid ResolvedRequest = PreTravelGameplayRequest;
	bPreTravelGameplayDetached = false;
	bTerminalGameplayRecoveryPending = false;
	TerminalGameplayRecoveryMessage.Reset();
	// The provider has already completed the rejected lease. Clear the outer
	// bookkeeping immediately so a fresh capture cannot be mistaken for the
	// terminal request's detached graph.
	FinishPreTravelGameplaySnapshot(ResolvedRequest, false);
	if (!bAttemptStarted && !bReleaseStarted) AttemptWorld.Reset();
	return true;
}

void UEFCalystoDirectorSubsystem::FinishPreTravelGameplaySnapshot(const FGuid& RequestId, const bool bAccepted)
{
	if (!RequestId.IsValid() || PreTravelGameplayRequest != RequestId) return;
	if (PreTravelGameplaySnapshot && !bPreTravelGameplayHandedToRuntime && PreTravelGameplayProvider)
		PreTravelGameplayProvider->FinishRequest(RequestId, PreTravelGameplaySnapshot, bAccepted);
	// A detached graph remains retained until the explicit terminal recovery has
	// reconstructed it in the destination. Accepted requests must also never
	// silently discard an impossible detached graph.
	if (!PreTravelGameplaySnapshot || !PreTravelGameplaySnapshot->IsDetachedForTravel())
	{
		PreTravelGameplaySnapshot.Reset();
		PreTravelGameplayProvider = nullptr;
		PreTravelGameplayRequest.Invalidate();
		bPreTravelGameplayDetached = false;
		bPreTravelGameplayHandedToRuntime = false;
		bTerminalGameplayRecoveryPending = false;
		TerminalGameplayRecoveryMessage.Reset();
	}
}

bool UEFCalystoDirectorSubsystem::CanReceiveLoadCallback(const FGuid Request) const
{
	return !bDeinitializing && Request.IsValid() && Request == RoutingRequest
		&& Snapshot.State == EEFCalystoDirectorState::Loading && !bReleaseStarted
		&& !bSetupPending && !bCancelAfterRelease && !bReturnToHubAfterRelease;
}

void UEFCalystoDirectorSubsystem::HandleConfigurationLoaded(const FGuid Request)
{
	if (!CanReceiveLoadCallback(Request) || !bConfigurationLoadPending) return;
	bConfigurationLoadPending = false;
	ConfigurationLoadCompletedSeconds = FPlatformTime::Seconds();
	if (FPlatformTime::Seconds() >= RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds)
	{ Fail(TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), EEFCalystoAttemptFailure::Deadline); return; }
	const auto* Settings = GetDefault<UEFCalystoDirectorSettings>();
	const UEFCalystoDungeonDirectorAsset* Asset = NativeParityFixture ? NativeParityFixture.Get() : Settings->Director.Get();
	if (!Asset || !AttemptRuntime)
	{ Fail(TEXT("DirectorUnavailable"), TEXT("The dungeon Director or native integration is unavailable."), EEFCalystoAttemptFailure::Resource); return; }
	FEFCalystoCompiledDirector Compiled; TArray<FEFCalystoValidationIssue> Issues;
	const double CompileStarted = FPlatformTime::Seconds();
	const bool bCompiled = Asset->Compile(Compiled, Issues);
	ConfigurationCompileSeconds = FPlatformTime::Seconds() - CompileStarted;
	if (!bCompiled)
	{
		const FString Error = Issues.IsEmpty() ? TEXT("The Director configuration is invalid.") : Issues[0].Field + TEXT(": ") + Issues[0].Message;
		Fail(TEXT("AuthoringInvalid"), Error, EEFCalystoAttemptFailure::Configuration); return;
	}
	Configuration = MakeShared<const FEFCalystoCompiledDirector>(MoveTemp(Compiled));
	FEFCalystoRandomKey Key; Key.RunSeed = Snapshot.RunSeed; Key.FloorNumber = Snapshot.FloorNumber; Key.RerollIndex = Snapshot.RerollIndex;
	FString Error;
	if (!bPreserveSelectedStyle || !Snapshot.StyleId.IsValid())
		if (!Configuration->SelectStyle(Key, {}, Snapshot.StyleId, Error))
		{ Fail(TEXT("NoEligibleStyle"), Error, EEFCalystoAttemptFailure::Configuration); return; }
	if (!Configuration->FindStyle(Snapshot.StyleId))
	{ Fail(TEXT("FrozenStyleUnavailable"), TEXT("The selected dungeon Style is unavailable."), EEFCalystoAttemptFailure::Configuration); return; }
	if (!CapturePreTravelGameplaySnapshot(Error))
	{
		Fail(TEXT("PreFloorGameplayCaptureFailed"), Error, EEFCalystoAttemptFailure::Configuration);
		return;
	}
	FEFCalystoFloorRequestIdentity Identity;
	Identity.RunSeed = Snapshot.RunSeed; Identity.FloorNumber = Snapshot.FloorNumber; Identity.RerollIndex = Snapshot.RerollIndex;
	Identity.SelectedStyle = Snapshot.StyleId; Identity.CompiledConfigurationHash = DirectorConfigurationHash(*Asset);
	Identity.PreFloorGameplayHash = PreTravelGameplaySnapshot->GetCanonicalHash();
	Transaction = MakeUnique<FEFCalystoFloorTransaction>();
	AttemptMetrics.Reset();
	if (!Transaction->Begin(Identity, RoutingRequest, RequestStartedSeconds, Error))
	{ Fail(TEXT("RequestIdentityInvalid"), Error, EEFCalystoAttemptFailure::Configuration); return; }
	RequiredVisualPaths = SharedPaths;
	TArray<FSoftObjectPath> ReachableVisuals; Key.StyleId=Snapshot.StyleId;
	if (!Configuration->GetReachableVisualDependencies(Key,ReachableVisuals,Error))
	{ Fail(TEXT("VisualSelectionInvalid"),Error,EEFCalystoAttemptFailure::Configuration); return; }
	RequiredVisualPaths.Append(ReachableVisuals);
	CanonicalizePaths(RequiredVisualPaths);
	TSet<FSoftObjectPath> RetainedSharedPaths(SharedPaths);
	TArray<FSoftObjectPath> RequestPaths = RequiredVisualPaths;
	RequestPaths.RemoveAll([&RetainedSharedPaths](const FSoftObjectPath& Path) { return RetainedSharedPaths.Contains(Path); });
	RequestVisualPathCount = RequestPaths.Num();
	bVisualLoadPending = true; bVisualDependenciesLoaded = false; bBakedVisualsExpanded = false;
	RefreshAsyncLoadingBudget();
	VisualLoadStartedSeconds = FPlatformTime::Seconds();
	if (RequestPaths.IsEmpty()) { HandleVisualsLoaded(Request); return; }
	TSharedPtr<FStreamableHandle> RequestedLease = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestPaths,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleVisualsLoaded, Request),
		FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("CalystoDirector.VisualDependencies"));
	if (Request != RoutingRequest || bReleaseStarted)
	{ if (RequestedLease) RequestedLease->CancelHandle(); return; }
	VisualLease = MoveTemp(RequestedLease);
	if (!VisualLease) Fail(TEXT("VisualLoadUnavailable"), TEXT("Dungeon resources could not be requested."), EEFCalystoAttemptFailure::Resource);
}

void UEFCalystoDirectorSubsystem::HandleVisualsLoaded(const FGuid Request)
{
	if (!CanReceiveLoadCallback(Request) || !bVisualLoadPending || !Transaction || !Configuration
		|| Transaction->GetPhase() != EEFCalystoFloorPhase::Preflight) return;
	if (bVisualDependenciesLoaded || bBakedVisualsExpanded) return;
	bVisualDependenciesLoaded = true; VisualLoadCompletedSeconds = FPlatformTime::Seconds();
	RefreshAsyncLoadingBudget();
	TryFinishDependencyLoading(Request);
}

void UEFCalystoDirectorSubsystem::HandleBakedVisualsLoaded(const FGuid Request)
{
	if (!CanReceiveLoadCallback(Request) || !bVisualLoadPending || !bBakedVisualsExpanded
		|| bVisualDependenciesLoaded || !Transaction || Transaction->GetPhase()!=EEFCalystoFloorPhase::Preflight) return;
	bVisualDependenciesLoaded=true; BakedVisualLoadCompletedSeconds=VisualLoadCompletedSeconds=FPlatformTime::Seconds();
	RefreshAsyncLoadingBudget(); TryFinishDependencyLoading(Request);
}

void UEFCalystoDirectorSubsystem::TryFinishDependencyLoading(const FGuid Request)
{
	if (!CanReceiveLoadCallback(Request) || !bVisualLoadPending || !bVisualDependenciesLoaded
		|| !Transaction || !Configuration || Transaction->GetPhase() != EEFCalystoFloorPhase::Preflight) return;
	if (FPlatformTime::Seconds() >= RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds)
	{ Fail(TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), EEFCalystoAttemptFailure::Deadline); return; }
	if (bSharedLoadPending) return;
	if (!bSharedLoadReady)
	{
		Fail(TEXT("SharedResourcesUnavailable"), SharedLoadFailure.IsEmpty()
			? TEXT("Shared dungeon resources are unavailable.") : SharedLoadFailure, EEFCalystoAttemptFailure::Resource);
		return;
	}
	for (const auto& Path : RequiredVisualPaths)
		if (!Path.ResolveObject())
		{ Fail(TEXT("MissingResource"), TEXT("A required dungeon resource is unavailable: ") + Path.ToString(), EEFCalystoAttemptFailure::Resource); return; }
	FString Error;
	const TSharedPtr<IEFCalystoDirectorAttemptRuntime> PreflightRuntime = AttemptRuntime;
	const TSharedPtr<const FEFCalystoCompiledDirector> PreflightConfiguration = Configuration;
	if (!bBakedVisualsExpanded)
	{
		TArray<FSoftObjectPath> Parents=RequiredVisualPaths, BakedPaths;
		const TSet<FSoftObjectPath> SharedSet(SharedPaths);
		Parents.RemoveAll([&](const auto& Path) { return SharedSet.Contains(Path); });
		const bool bExpanded=PreflightRuntime && PreflightRuntime->GetBakedVisualDependencies(Parents,BakedPaths,Error);
		if (!CanReceiveLoadCallback(Request) || AttemptRuntime!=PreflightRuntime || Configuration!=PreflightConfiguration) return;
		if (!bExpanded || BakedPaths.Num()>8192)
		{ Fail(TEXT("BakedVisualContractInvalid"),Error.IsEmpty() ? TEXT("Baked visual expansion is unavailable or exceeds 8192 dependencies.") : Error,EEFCalystoAttemptFailure::Configuration); return; }
		for (const auto& Path:BakedPaths) if (!Path.IsValid() || !Path.GetSubPathString().IsEmpty())
		{ Fail(TEXT("BakedVisualContractInvalid"),TEXT("A baked dependency is not a supported asset reference."),EEFCalystoAttemptFailure::Configuration); return; }
		CanonicalizePaths(BakedPaths);
		const TSet<FSoftObjectPath> Retained(RequiredVisualPaths);
		BakedPaths.RemoveAll([&](const auto& Path) { return Retained.Contains(Path); });
		bBakedVisualsExpanded=true;
		if (!BakedPaths.IsEmpty())
		{
			RequiredVisualPaths.Append(BakedPaths); CanonicalizePaths(RequiredVisualPaths);
			RequestVisualPathCount+=BakedPaths.Num(); bVisualDependenciesLoaded=false;
			BakedVisualLoadStartedSeconds=FPlatformTime::Seconds();
			RefreshAsyncLoadingBudget();
			auto RequestedLease=UAssetManager::GetStreamableManager().RequestAsyncLoad(BakedPaths,
				FStreamableDelegate::CreateUObject(this,&ThisClass::HandleBakedVisualsLoaded,Request),
				FStreamableManager::AsyncLoadHighPriority,false,false,TEXT("CalystoDirector.BakedVisualDependencies"));
			if (Request!=RoutingRequest || bReleaseStarted || !bBakedVisualsExpanded)
			{ if (RequestedLease) RequestedLease->CancelHandle(); return; }
			BakedVisualLease=MoveTemp(RequestedLease);
			if (!BakedVisualLease) Fail(TEXT("BakedVisualLoadUnavailable"),TEXT("Baked dungeon resources could not be requested."),EEFCalystoAttemptFailure::Resource);
			return;
		}
	}
	bVisualLoadPending=false; DependenciesReadySeconds=FPlatformTime::Seconds();
	const bool bPreflightAccepted = PreflightRuntime && PreflightRuntime->Preflight(*PreflightConfiguration, Snapshot.StyleId, Error);
	if (!CanReceiveLoadCallback(Request) || AttemptRuntime != PreflightRuntime || Configuration != PreflightConfiguration) return;
	if (!bPreflightAccepted)
	{ Fail(TEXT("NativeContractInvalid"), Error, EEFCalystoAttemptFailure::Configuration); return; }
	if (FPlatformTime::Seconds() >= Transaction->GetDeadline())
	{ Fail(TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), EEFCalystoAttemptFailure::Deadline); return; }
	const auto* Settings = GetDefault<UEFCalystoDirectorSettings>();
	if (!GetWorld() || Settings->DungeonMap.IsNull())
	{ Fail(TEXT("DungeonMapUnavailable"), TEXT("The dungeon destination is unavailable."), EEFCalystoAttemptFailure::Configuration); return; }
	if (!PrepareAndDetachPreTravelGameplaySnapshot(Error))
	{
		Fail(TEXT("PreFloorGameplayTravelPreparationFailed"), Error,
			FPlatformTime::Seconds() >= Transaction->GetDeadline()
				? EEFCalystoAttemptFailure::Deadline : EEFCalystoAttemptFailure::Configuration);
		return;
	}
	// Detachment is the actual travel boundary. It is deliberately the final
	// fallible source-world step: once it succeeds, OpenLevel must follow
	// immediately so terminal recovery always has an owned destination rather
	// than trying to rebuild a portable graph back into its departing world.
	bTravelIssued = true; Snapshot.State = EEFCalystoDirectorState::Traveling;
	DepartingWorld = GetWorld();
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetWorld(), Settings->DungeonMap, true);
}

bool UEFCalystoDirectorSubsystem::IsDungeonWorld(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance()) return false;
	const FString Expected = GetDefault<UEFCalystoDirectorSettings>()->DungeonMap.ToSoftObjectPath().GetLongPackageName();
	return !Expected.IsEmpty() && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == Expected;
}

void UEFCalystoDirectorSubsystem::BeginNativeAttempt(UWorld* World)
{
	if (!Transaction || !AttemptRuntime || bAttemptStarted || bReleaseStarted || bRuntimeUnregisterPending
		|| !Configuration || !IsDungeonWorld(World) || !World->HasBegunPlay() || World == DepartingWorld.Get()) return;
	FString Error; const double Now = FPlatformTime::Seconds(); const auto Token = Transaction->GetToken();
	if (!PreTravelGameplaySnapshot || !PreTravelGameplayProvider || !bPreTravelGameplayDetached
		|| PreTravelGameplayRequest != Token.Request)
	{
		Fail(TEXT("PreFloorGameplayOwnershipLost"),
			TEXT("The exact detached pre-floor gameplay snapshot is unavailable for native destination reconstruction."),
			EEFCalystoAttemptFailure::Configuration);
		return;
	}
	if (!Transaction->FinishPreflight(Token, Now, Error) || !Transaction->ArmRootGeneration(Token, Now, Error))
	{ Fail(TEXT("GenerationGateRejected"), Error, EEFCalystoAttemptFailure::Configuration); return; }
	FEFCalystoDirectorAttemptRequest Request;
	Request.Token = Token; Request.Configuration = Configuration; Request.World = World;
	Request.DeadlineSeconds = Transaction->GetDeadline(); Request.TopologySeed = Transaction->GetAttempts().Last().TopologySeed;
	Request.Random.RunSeed = Snapshot.RunSeed; Request.Random.FloorNumber = Snapshot.FloorNumber;
	Request.Random.RerollIndex = Snapshot.RerollIndex; Request.Random.StyleId = Snapshot.StyleId;
	Request.Random.AttemptIndex = Transaction->GetAttempts().Last().AttemptIndex;
	Request.PreFloorGameplaySnapshot = PreTravelGameplaySnapshot;
	bAttemptStarted = true; AttemptWorld = World; Snapshot.State = EEFCalystoDirectorState::Generating;
	// The native owner retains this exact shared lease from BeginAttempt through
	// FinishRequest, including all spatial retries. Do not independently release it.
	bPreTravelGameplayHandedToRuntime = true;
	const TSharedPtr<IEFCalystoDirectorAttemptRuntime> StartedRuntime = AttemptRuntime;
	const bool bStarted = StartedRuntime->BeginAttempt(Request, Error);
	if (!bStarted && !bDeinitializing && !bReleaseStarted && AttemptRuntime == StartedRuntime)
		Fail(TEXT("NativeGenerationRejected"), Error, EEFCalystoAttemptFailure::Configuration);
}

void UEFCalystoDirectorSubsystem::ObserveNativeAttempt(const double)
{
	if (!AttemptRuntime || !Transaction || !bAttemptStarted || bReleaseStarted) return;
	const TSharedPtr<IEFCalystoDirectorAttemptRuntime> ObservedRuntime = AttemptRuntime;
	const auto Observation = ObservedRuntime->ObserveAttempt(FPlatformTime::Seconds());
	// Native work can be delayed or reenter cancellation. Re-check ownership and use
	// a fresh timestamp, not the tick's earlier timestamp preceding BeginAttempt.
	const double Now = FPlatformTime::Seconds();
	if (!Transaction || !bAttemptStarted || bReleaseStarted || AttemptRuntime != ObservedRuntime
		|| !Transaction->Owns(Observation.Token)) return;
	AttemptMetrics.Add(Observation.Token.Attempt, Observation.Metrics);
	if (Observation.Stage == EEFCalystoObservation::Failed)
	{ Fail(Observation.FailureCode, Observation.Message, Observation.Failure); return; }
	FString Error; const auto Token = Transaction->GetToken();
	if (Observation.Stage >= EEFCalystoObservation::StructuralVerification && Transaction->GetPhase() == EEFCalystoFloorPhase::NativeGeneration)
		if (!Transaction->NativeGenerationFinished(Token, Now, Error)) { Fail(TEXT("NativeEvidenceRejected"), Error, EEFCalystoAttemptFailure::Configuration); return; }
	if (Observation.Stage >= EEFCalystoObservation::NavigationAndReservations && Transaction->GetPhase() == EEFCalystoFloorPhase::StructuralVerification)
		if (!Transaction->StructuralVerificationFinished(Token, Now, Error)) { Fail(TEXT("StructuralEvidenceRejected"), Error, EEFCalystoAttemptFailure::Spatial); return; }
	if (Observation.Stage >= EEFCalystoObservation::RealizationVerification && Transaction->GetPhase() == EEFCalystoFloorPhase::NavigationAndReservations)
		if (!Transaction->FreezeReservations(Token, Observation.ReservationHash, Observation.ReservedElements, Now, Error))
		{ Fail(TEXT("ReservationRejected"), Error, EEFCalystoAttemptFailure::Spatial); return; }
	if (Observation.Stage == EEFCalystoObservation::Verified && Transaction->GetPhase() == EEFCalystoFloorPhase::RealizationVerification)
	{
		if (!Transaction->VerifyRealization(Token, Observation.Evidence, Now, Error))
		{ Fail(TEXT("RealizationRejected"), Error, EEFCalystoAttemptFailure::Spatial); return; }
		Snapshot.State = EEFCalystoDirectorState::AwaitingPlayerRelease; Snapshot.bNativeFloorVerified = true;
		// The empty candidate gameplay identity above is not a verified companion,
		// inventory or outcome commit. Their staging/commit bridge is still pending.
		Snapshot.bGameplayVerified = false;
		Snapshot.RequestElapsedSeconds = Now - RequestStartedSeconds;
	}
}

void UEFCalystoDirectorSubsystem::BeginRollback()
{
	if (bReleaseStarted || !Transaction) return;
	bReleaseStarted = true; CleanupStartedSeconds = FPlatformTime::Seconds();
	NextCleanupObservationSeconds = CleanupStartedSeconds; ReleasingToken = Transaction->GetToken();
	if (AttemptRuntime && bAttemptStarted) AttemptRuntime->ReleaseAttempt(ReleasingToken);
}

bool UEFCalystoDirectorSubsystem::FinishRuntimeRequest(const TSharedPtr<IEFCalystoDirectorAttemptRuntime>& Runtime,
	const FGuid& RequestId, const bool bAccepted)
{
	if (!RequestId.IsValid() || FinishedRuntimeRequest == RequestId) return true;
	if (Runtime && !Runtime->FinishRequest(RequestId, bAccepted))
	{
		// The runtime still owns an immutable gameplay lease. Keep the request
		// observable and protected; accepting a later floor would otherwise turn
		// a failed terminal release into a hidden competing lease.
		return false;
	}
	// The runtime's terminal release deliberately defers a detached rejected
	// graph. Native cleanup has completed at this call site, so restore the exact
	// destination state before clearing the outer request owner or enabling retry.
	if (!bAccepted && !ResolveTerminalPreTravelGameplaySnapshot(TerminalGameplayRecoveryMessage))
	{
		bTerminalGameplayRecoveryPending = true;
		Snapshot.FailureMessage = TerminalGameplayRecoveryMessage.IsEmpty()
			? TEXT("Persistent gameplay recovery is pending. The player remains protected.")
			: TerminalGameplayRecoveryMessage;
		UE_LOG(LogCalystoDirector, Warning, TEXT("TerminalGameplayRecoveryPending: %s"), *Snapshot.FailureMessage);
		return false;
	}
	// Set the guard only after terminal gameplay ownership is actually resolved.
	// A failed recovery intentionally leaves it clear so Retry/Return to HUB can
	// retry the recovery without creating a new topology attempt.
	FinishedRuntimeRequest = RequestId;
	FinishPreTravelGameplaySnapshot(RequestId, bAccepted);
	return true;
}

void UEFCalystoDirectorSubsystem::Fail(const FName Code, const FString& Message, const EEFCalystoAttemptFailure Kind)
{
	Snapshot.FailureMessage = Message.IsEmpty() ? TEXT("The dungeon could not be prepared.") : Message;
	Snapshot.bGameplayVerified = false; Snapshot.bNativeFloorVerified = false; bSetupPending = false;
	InvalidatePendingLoads();
	UE_LOG(LogCalystoDirector, Warning, TEXT("%s: %s"), *Code.ToString(), *Snapshot.FailureMessage);
	if (Transaction && (bAttemptStarted || Transaction->GetPhase() == EEFCalystoFloorPhase::RollingBack))
	{
		Transaction->Reject(Transaction->GetToken(), Kind, Code, Snapshot.FailureMessage, FPlatformTime::Seconds());
		BeginRollback();
		Snapshot.State = bCleanupFailed ? EEFCalystoDirectorState::Failed : EEFCalystoDirectorState::Recovering;
	}
	else
	{
		const FGuid TerminalRequest = Transaction ? Transaction->GetToken().Request : RoutingRequest;
		if (Transaction && Transaction->GetPhase() != EEFCalystoFloorPhase::Ready)
		{
			Transaction->Reject(Transaction->GetToken(), Kind, Code, Snapshot.FailureMessage, FPlatformTime::Seconds());
			FEFCalystoRollbackEvidence Empty; Empty.bPCGCleanupComplete = true; FString Error;
			if (Transaction->GetPhase() == EEFCalystoFloorPhase::RollingBack)
				Transaction->CompleteRollback(Transaction->GetToken(), Empty, FPlatformTime::Seconds(), Error);
		}
		FinishRuntimeRequest(AttemptRuntime, TerminalRequest,
			Transaction && Transaction->HasCommittedGameplay());
		ReleaseLoads(); Configuration.Reset();
		Snapshot.State = EEFCalystoDirectorState::Failed; PublishFailure();
	}
}

void UEFCalystoDirectorSubsystem::PublishFailure()
{
	if (bFailurePublished) return;
	bFailurePublished = true; Snapshot.RequestElapsedSeconds = FPlatformTime::Seconds() - RequestStartedSeconds;
	RequestFailed.Broadcast(GetSnapshot());
}

void UEFCalystoDirectorSubsystem::ReleaseLoads()
{
	bConfigurationLoadPending = false; bVisualLoadPending = false; bVisualDependenciesLoaded = false;
	if (ConfigurationLease) { ConfigurationLease->CancelHandle(); ConfigurationLease.Reset(); }
	if (VisualLease) { VisualLease->CancelHandle(); VisualLease.Reset(); }
	if (BakedVisualLease) { BakedVisualLease->CancelHandle(); BakedVisualLease.Reset(); }
	bBakedVisualsExpanded=false;
	RequiredVisualPaths.Reset();
	NativeParityFixture = nullptr;
	RefreshAsyncLoadingBudget();
}

void UEFCalystoDirectorSubsystem::InvalidatePendingLoads()
{
	EFCalystoAsyncLoadingBudget::SetOwnerActive(this, false);
	RoutingRequest.Invalidate(); bConfigurationLoadPending = false; bVisualLoadPending = false; bVisualDependenciesLoaded = false;
	// Completed leases keep the current attempt's referenced resources alive until
	// verified teardown. Only unfinished callbacks/loads are cancelled here.
	if (ConfigurationLease && !ConfigurationLease->HasLoadCompleted())
	{ ConfigurationLease->CancelHandle(); ConfigurationLease.Reset(); }
	if (VisualLease && !VisualLease->HasLoadCompleted())
	{ VisualLease->CancelHandle(); VisualLease.Reset(); }
	if (BakedVisualLease && !BakedVisualLease->HasLoadCompleted())
	{ BakedVisualLease->CancelHandle(); BakedVisualLease.Reset(); }
}

void UEFCalystoDirectorSubsystem::IssueRequestedHubTravel()
{
	if (!bReturnToHubAfterRelease || bHubTravelIssued || !GetWorld()) return;
	const auto* Settings = GetDefault<UEFCalystoDirectorSettings>();
	if (Settings->HubMap.IsNull()) return;
	bHubTravelIssued = true;
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetWorld(), Settings->HubMap, true);
}

bool UEFCalystoDirectorSubsystem::TickRequest(float)
{
	const double Now = FPlatformTime::Seconds(); FString Error;
	if (bDeinitializing) return false;
	const bool bSharedLoadExpired = bSharedLoadPending && Now >= GetSharedDependencyDeadlineSeconds();
	if (bSharedLoadExpired)
	{
		bSharedLoadPending = false; bSharedLoadReady = false; SharedLoadCompletedSeconds = Now;
		SharedLoadFailure = TEXT("Shared dungeon resources did not finish loading within 30 seconds.");
		if (SharedLease) { SharedLease->CancelHandle(); SharedLease.Reset(); }
		RefreshAsyncLoadingBudget();
	}
	if (!IsTerminal(Snapshot.State) && !bReturnToHubAfterRelease && !bCancelAfterRelease && !bReleaseStarted
		&& Now >= RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds)
	{ Fail(TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), EEFCalystoAttemptFailure::Deadline); return true; }
	if (bSharedLoadExpired) TryFinishDependencyLoading(RoutingRequest);
	if (bReleaseStarted)
	{
		if (Now < NextCleanupObservationSeconds) return true;
		NextCleanupObservationSeconds = Now + (bCleanupFailed ? 1.0 : 0.05);
		FEFCalystoRollbackEvidence Evidence;
		if (AttemptRuntime && bAttemptStarted)
		{
			const TSharedPtr<IEFCalystoDirectorAttemptRuntime> ReleasingRuntime = AttemptRuntime;
			const FEFCalystoAttemptToken ObservedToken = ReleasingToken;
			Evidence = ReleasingRuntime->ObserveRelease(ObservedToken);
			if (bDeinitializing || !bReleaseStarted || !(ObservedToken == ReleasingToken) || AttemptRuntime != ReleasingRuntime) return true;
		}
		else Evidence.bPCGCleanupComplete = !bAttemptStarted;
		if (!Evidence.IsComplete())
		{
			if (!bCleanupFailed && Now - CleanupStartedSeconds > 5.0)
			{
				bCleanupFailed = true; bSetupPending = false; InvalidatePendingLoads();
				Snapshot.State = EEFCalystoDirectorState::Failed;
				Snapshot.FailureMessage = TEXT("Dungeon cleanup is unfinished. The player remains protected.");
				PublishFailure();
			}
			if (bCleanupFailed) IssueRequestedHubTravel();
			return true;
		}
		bReleaseStarted = false; bAttemptStarted = false;
		if (Transaction && Transaction->HasCommittedGameplay())
			Transaction->CompleteAcceptedRelease(ReleasingToken, Evidence);
		if (Transaction && Transaction->GetPhase() == EEFCalystoFloorPhase::RollingBack)
			if (!Transaction->CompleteRollback(ReleasingToken, Evidence, FPlatformTime::Seconds(), Error))
			{
				bReleaseStarted = true; bCleanupFailed = true; bSetupPending = false;
				Snapshot.State = EEFCalystoDirectorState::Failed; Snapshot.FailureMessage = Error; PublishFailure(); return true;
			}
		const bool bCanRetry = !bSetupPending && !bCleanupFailed && AttemptRuntime
			&& Transaction && Transaction->GetPhase() == EEFCalystoFloorPhase::RetryAvailable
			&& Transaction->BeginRetry(FPlatformTime::Seconds(), Error);
		if (bCanRetry)
		{
			Snapshot.State = EEFCalystoDirectorState::Recovering; Snapshot.FailureMessage.Reset();
			BeginNativeAttempt(GetWorld()); return true;
		}
		const bool bAcceptedRequest = Transaction && Transaction->HasCommittedGameplay();
		FinishRuntimeRequest(AttemptRuntime, ReleasingToken.Request, bAcceptedRequest);
		// Keep the exact destination weak reference only while a failed detached
		// graph still needs explicit recovery. It is otherwise no longer an active
		// attempt owner after verified cleanup.
		if (!bTerminalGameplayRecoveryPending) AttemptWorld.Reset();
		if (bRuntimeUnregisterPending) { ReleaseSharedDependencies(); AttemptRuntime.Reset(); bRuntimeUnregisterPending = false; }
		if (bReturnToHubAfterRelease)
		{
			IssueRequestedHubTravel();
			bReturnToHubAfterRelease = false; InvalidatePendingLoads(); ReleaseLoads(); Transaction.Reset(); Configuration.Reset();
			Snapshot = {}; bCleanupFailed = false; return true;
		}
		if (bCancelAfterRelease)
		{
			bCancelAfterRelease = false; InvalidatePendingLoads(); ReleaseLoads(); Configuration.Reset();
			Snapshot.State = EEFCalystoDirectorState::Cancelled; bCleanupFailed = false; return true;
		}
		if (!bSetupPending && Transaction)
		{
			ReleaseLoads(); Configuration.Reset(); Snapshot.State = EEFCalystoDirectorState::Failed; PublishFailure();
			return true;
		}
	}
	if (bSetupPending)
	{
		if (Now >= RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds)
		{ Fail(TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), EEFCalystoAttemptFailure::Deadline); return true; }
		bSetupPending = false; Transaction.Reset(); Configuration.Reset(); ReleaseLoads();
		ConfigurationLoadStartedSeconds = FPlatformTime::Seconds();
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorNativeParity")))
		{
			NativeParityFixture = EFCalystoDirectorTestFixture::MakeTransientFixture(
				FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorNativeParityLighting")),
				FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorNativeParityArchitecture")));
			if (!NativeParityFixture)
			{ Fail(TEXT("ParityFixtureUnavailable"), TEXT("The explicit native parity fixture could not be created."), EEFCalystoAttemptFailure::Configuration); return true; }
			bConfigurationLoadPending = true;
			HandleConfigurationLoaded(RoutingRequest);
			return true;
		}
#endif
		const auto* Settings = GetDefault<UEFCalystoDirectorSettings>();
		if (Settings->Director.IsNull())
		{ Fail(TEXT("DirectorLoadUnavailable"), TEXT("The dungeon Director is not configured."), EEFCalystoAttemptFailure::Configuration); return true; }
		const FGuid Request = RoutingRequest; bConfigurationLoadPending = true;
		RefreshAsyncLoadingBudget();
		TSharedPtr<FStreamableHandle> RequestedLease = UAssetManager::GetStreamableManager().RequestAsyncLoad(Settings->Director.ToSoftObjectPath(),
			FStreamableDelegate::CreateUObject(this, &ThisClass::HandleConfigurationLoaded, Request),
			FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("CalystoDirector.Configuration"));
		if (Request != RoutingRequest || bReleaseStarted)
		{ if (RequestedLease) RequestedLease->CancelHandle(); return true; }
		ConfigurationLease = MoveTemp(RequestedLease);
		if (!ConfigurationLease) Fail(TEXT("DirectorLoadUnavailable"), TEXT("The dungeon Director could not be loaded."), EEFCalystoAttemptFailure::Resource);
		return true;
	}
	if (IsTerminal(Snapshot.State)) return true;
	if (Now >= RequestStartedSeconds + FEFCalystoFloorTransaction::RequestBudgetSeconds)
	{ Fail(TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), EEFCalystoAttemptFailure::Deadline); return true; }
	if (bTravelIssued && !bAttemptStarted && IsDungeonWorld(GetWorld()) && GetWorld()->HasBegunPlay()) BeginNativeAttempt(GetWorld());
	if (bAttemptStarted && !bReleaseStarted) ObserveNativeAttempt(FPlatformTime::Seconds());
	return true;
}

bool UEFCalystoDirectorSubsystem::RequestCancel()
{
	if (!IsTravelRequestPending() || bReturnToHubAfterRelease) return false;
	bSetupPending = false; bCancelAfterRelease = true; InvalidatePendingLoads();
	if (Transaction) { Transaction->Cancel(FPlatformTime::Seconds()); BeginRollback(); }
	else { ReleaseLoads(); Snapshot.State = EEFCalystoDirectorState::Cancelled; bCancelAfterRelease = false; }
	return true;
}

bool UEFCalystoDirectorSubsystem::RequestReturnToHub()
{
	if (!HasActiveRun() || bReturnToHubAfterRelease || !GetWorld() || GetDefault<UEFCalystoDirectorSettings>()->HubMap.IsNull()) return false;
	// A detached graph can only be reconstructed after ObserveRelease has proved
	// that this attempt owns no native actors, instances, reservations or callbacks.
	// Do not make the Return-to-HUB request race that cleanup by restoring its
	// gameplay projections into a still-live rejected dungeon.
	if (!bAttemptStarted && !bReleaseStarted)
	{
		FString RecoveryError;
		if (!ResolveTerminalPreTravelGameplaySnapshot(RecoveryError))
		{
			Snapshot.FailureMessage = RecoveryError.IsEmpty()
				? TEXT("Persistent gameplay recovery is still pending. The player remains protected.")
				: RecoveryError;
			UE_LOG(LogCalystoDirector, Warning, TEXT("TerminalGameplayRecoveryPending: %s"), *Snapshot.FailureMessage);
			return false;
		}
	}
	bSetupPending = false; bReturnToHubAfterRelease = true; InvalidatePendingLoads();
	if (Transaction) { Transaction->Cancel(FPlatformTime::Seconds()); BeginRollback(); }
	else
	{
		IssueRequestedHubTravel(); ReleaseLoads(); Configuration.Reset(); Snapshot = {}; bReturnToHubAfterRelease = false;
	}
	return true;
}

bool UEFCalystoDirectorSubsystem::IsLevelRuntimeReady(UWorld* World)
{
	return !IsDungeonWorld(World) || (AttemptWorld.Get() == World && !bReleaseStarted && Transaction
		&& Snapshot.bNativeFloorVerified
		&& ((Snapshot.State == EEFCalystoDirectorState::AwaitingPlayerRelease && Transaction->GetPhase() == EEFCalystoFloorPhase::Commit)
			|| (Snapshot.State == EEFCalystoDirectorState::Ready && Transaction->GetPhase() == EEFCalystoFloorPhase::Ready)));
}

bool UEFCalystoDirectorSubsystem::ResolvePlayerStartTransform(UWorld* World, FTransform& Transform) const
{
	if (!IsDungeonWorld(World) || World != AttemptWorld.Get() || !Transaction || bReleaseStarted
		|| !Snapshot.bNativeFloorVerified || (Snapshot.State != EEFCalystoDirectorState::Ready
			&& Snapshot.State != EEFCalystoDirectorState::AwaitingPlayerRelease)) return false;
	const FTransform* Entry = Transaction->GetValidatedEntry();
	if (!Entry) return false;
	Transform = *Entry; return true;
}

EEFCalystoPlayerReleaseResult UEFCalystoDirectorSubsystem::ConfirmPlayerRelease(UWorld* World, APawn* Player, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bDeinitializing || bReleaseStarted || bConfirmingPlayerRelease || !IsDungeonWorld(World)
		|| AttemptWorld.Get() != World || !Transaction || Snapshot.State != EEFCalystoDirectorState::AwaitingPlayerRelease
		|| Transaction->GetPhase() != EEFCalystoFloorPhase::Commit)
	{ Error = TEXT("Player release does not own the current verified floor."); return EEFCalystoPlayerReleaseResult::Rejected; }
	TGuardValue<bool> ReleaseGuard(bConfirmingPlayerRelease, true);
	APlayerController* Controller = UGameplayStatics::GetPlayerController(World, 0);
	const FTransform* Entry = Transaction->GetValidatedEntry();
	if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld() != World
		|| !IsValid(Controller) || Controller->GetPawn() != Player || Player->GetController() != Controller
		|| UGameplayStatics::GetPlayerPawn(World, 0) != Player || !Entry
		|| Player->GetActorTransform().ContainsNaN()
		|| FVector::DistSquared(Player->GetActorLocation(), Entry->GetLocation()) > FMath::Square(2.0))
	{ Error = TEXT("The protected player has not reached the exact validated entry."); return EEFCalystoPlayerReleaseResult::Rejected; }
	const FEFCalystoAttemptToken Token = Transaction->GetToken();
	const FEFCalystoCommitEvidence* VerifiedEvidence = Transaction->GetVerifiedCommitEvidence();
	if (!VerifiedEvidence) { Error = TEXT("The frozen release evidence is unavailable."); return EEFCalystoPlayerReleaseResult::Rejected; }
	const FEFCalystoCommitEvidence FrozenEvidence = *VerifiedEvidence;
	const TSharedPtr<IEFCalystoDirectorAttemptRuntime> ReleasingRuntime = AttemptRuntime;
	if (!ReleasingRuntime)
	{ Error = TEXT("The verified native floor owner is unavailable."); Fail(TEXT("PlayerReleaseOwnerUnavailable"), Error, EEFCalystoAttemptFailure::Resource); return EEFCalystoPlayerReleaseResult::Rejected; }
	const FEFCalystoDirectorAttemptObservation FinalObservation = ReleasingRuntime->ObserveAttempt(FPlatformTime::Seconds());
	if (bDeinitializing || bReleaseStarted || !Transaction || !Transaction->Owns(Token)
		|| AttemptRuntime != ReleasingRuntime || Snapshot.State != EEFCalystoDirectorState::AwaitingPlayerRelease
		|| Transaction->GetPhase() != EEFCalystoFloorPhase::Commit)
	{ Error = TEXT("Player release was cancelled while verifying the native floor."); return EEFCalystoPlayerReleaseResult::Rejected; }
	if (!(FinalObservation.Token == Token))
	{
		Error = TEXT("The final observation does not belong to the active floor.");
		Fail(TEXT("PlayerReleaseOwnerChanged"), Error, EEFCalystoAttemptFailure::Configuration);
		return EEFCalystoPlayerReleaseResult::Rejected;
	}
	AttemptMetrics.Add(Token.Attempt, FinalObservation.Metrics);
	if (FinalObservation.Stage == EEFCalystoObservation::Failed)
	{
		Error = FinalObservation.Message;
		Fail(FinalObservation.FailureCode, Error, FinalObservation.Failure);
		return EEFCalystoPlayerReleaseResult::Rejected;
	}
	if (FinalObservation.Stage != EEFCalystoObservation::Verified)
	{
		if (FPlatformTime::Seconds() >= Transaction->GetDeadline())
		{
			Error = TEXT("The shared floor request deadline expired before gameplay publication.");
			Fail(TEXT("PlayerReleaseRejected"), Error, EEFCalystoAttemptFailure::Deadline);
			return EEFCalystoPlayerReleaseResult::Rejected;
		}
		Error = FinalObservation.Metrics.ObservationMessage.IsEmpty()
			? TEXT("Waiting for the floor's final verification.") : FinalObservation.Metrics.ObservationMessage;
		return EEFCalystoPlayerReleaseResult::Pending;
	}
	if (!FinalObservation.Evidence.IsComplete(Error))
	{
		if (Error.IsEmpty()) Error = TEXT("The native floor changed before player release.");
		Fail(TEXT("PlayerReleaseEvidenceChanged"), Error, EEFCalystoAttemptFailure::Spatial);
		return EEFCalystoPlayerReleaseResult::Rejected;
	}
	bool bSameReservation = FinalObservation.Evidence.ReservationHash == FrozenEvidence.ReservationHash
		&& FinalObservation.Evidence.RealizationHash == FrozenEvidence.RealizationHash
		&& FinalObservation.Evidence.Entry.Equals(FrozenEvidence.Entry, 0.001)
		&& FinalObservation.Evidence.ReservedElements.Num() == FrozenEvidence.ReservedElements.Num();
	for (const FGuid& Element : FrozenEvidence.ReservedElements)
		bSameReservation &= FinalObservation.Evidence.ReservedElements.Contains(Element);
	// Provider observation may invoke project callbacks. Recheck the real pawn and
	// frozen entry after that boundary, before any gameplay can be committed.
	if (!IsValid(World) || AttemptWorld.Get() != World || !IsValid(Player) || Player->IsActorBeingDestroyed()
		|| Player->GetWorld() != World || !IsValid(Controller) || Controller->GetWorld() != World
		|| Controller->GetPawn() != Player || Player->GetController() != Controller
		|| UGameplayStatics::GetPlayerPawn(World, 0) != Player || Player->GetActorTransform().ContainsNaN()
		|| FVector::DistSquared(Player->GetActorLocation(), FrozenEvidence.Entry.GetLocation()) > FMath::Square(2.0)
		|| !FinalObservation.Evidence.Entry.Equals(FrozenEvidence.Entry, 0.01) || !bSameReservation)
	{
		Error = TEXT("The protected player or reserved floor changed during release verification.");
		Fail(TEXT("PlayerReleaseStateChanged"), Error, EEFCalystoAttemptFailure::Spatial);
		return EEFCalystoPlayerReleaseResult::Rejected;
	}
	const double BeforePublication = FPlatformTime::Seconds();
	if (BeforePublication >= Transaction->GetDeadline())
	{
		Error = TEXT("The shared floor request deadline expired before gameplay publication.");
		Fail(TEXT("PlayerReleaseRejected"), Error, EEFCalystoAttemptFailure::Deadline); return EEFCalystoPlayerReleaseResult::Rejected;
	}
	EEFCalystoAttemptFailure PublicationFailure = EEFCalystoAttemptFailure::Configuration;
	const bool bPublished = ReleasingRuntime->PublishPrepared(Token, FrozenEvidence, BeforePublication, PublicationFailure, Error);
	// Publication can call project code. Cancellation/unregistration owns rollback;
	// a nested release cannot publish twice or commit a stale request.
	if (bDeinitializing || bReleaseStarted || !Transaction || !Transaction->Owns(Token)
		|| AttemptRuntime != ReleasingRuntime || Snapshot.State != EEFCalystoDirectorState::AwaitingPlayerRelease
		|| Transaction->GetPhase() != EEFCalystoFloorPhase::Commit)
	{ Error = TEXT("Player release was cancelled during reversible gameplay publication."); return EEFCalystoPlayerReleaseResult::Rejected; }
	if (!bPublished)
	{
		if (Error.IsEmpty()) Error = TEXT("The prepared gameplay state could not be published.");
		Fail(TEXT("GameplayPublicationRejected"), Error, PublicationFailure); return EEFCalystoPlayerReleaseResult::Rejected;
	}
	if (!IsValid(World) || AttemptWorld.Get() != World || !IsValid(Player) || Player->IsActorBeingDestroyed()
		|| Player->GetWorld() != World || !IsValid(Controller) || Controller->GetWorld() != World
		|| Controller->GetPawn() != Player || Player->GetController() != Controller
		|| UGameplayStatics::GetPlayerPawn(World, 0) != Player || Player->GetActorTransform().ContainsNaN()
		|| FVector::DistSquared(Player->GetActorLocation(), FrozenEvidence.Entry.GetLocation()) > FMath::Square(2.0))
	{
		Error = TEXT("The protected player changed during reversible gameplay publication.");
		Fail(TEXT("GameplayPublicationPlayerChanged"), Error, EEFCalystoAttemptFailure::Spatial); return EEFCalystoPlayerReleaseResult::Rejected;
	}
	const double Now = FPlatformTime::Seconds();
	if (!Transaction->CommitAndReleasePlayer(Token, Now, Error))
	{
		Fail(TEXT("PlayerReleaseRejected"), Error, Now >= Transaction->GetDeadline()
			? EEFCalystoAttemptFailure::Deadline : EEFCalystoAttemptFailure::Spatial);
		return EEFCalystoPlayerReleaseResult::Rejected;
	}
	Snapshot.State = EEFCalystoDirectorState::Ready;
	Snapshot.LastCommittedFloorNumber = Snapshot.FloorNumber;
	Snapshot.LastCommittedRunEpoch = Snapshot.RunEpoch;
	Snapshot.RequestElapsedSeconds = Now - RequestStartedSeconds;
	const int32 AcceptedAttempts = Transaction->GetAttempts().Num();
	const EEFCalystoActivationResult Activation = ReleasingRuntime->OnCommitted(Token, Error);
	// Accepted gameplay callbacks may request another floor/HUB or deinitialize.
	// Preserve that request; never dereference a released transaction or announce
	// its replacement's Loading snapshot as the previous floor's Ready event.
	if (bDeinitializing || bReleaseStarted || !Transaction || !Transaction->Owns(Token)
		|| AttemptRuntime != ReleasingRuntime || AttemptWorld.Get() != World
		|| Snapshot.State != EEFCalystoDirectorState::Ready || Transaction->GetPhase() != EEFCalystoFloorPhase::Ready)
	{ Error = TEXT("The accepted floor is no longer the active player-release request."); return EEFCalystoPlayerReleaseResult::Rejected; }
	const bool bPlayerStillOwned = IsValid(World) && IsValid(Player) && !Player->IsActorBeingDestroyed()
		&& Player->GetWorld() == World && IsValid(Controller) && Controller->GetWorld() == World
		&& Controller->GetPawn() == Player && Player->GetController() == Controller
		&& UGameplayStatics::GetPlayerPawn(World, 0) == Player && !Player->GetActorTransform().ContainsNaN()
		&& FVector::DistSquared(Player->GetActorLocation(), FrozenEvidence.Entry.GetLocation()) <= FMath::Square(2.0);
	if (Activation != EEFCalystoActivationResult::Activated || !bPlayerStillOwned)
	{
		if (Error.IsEmpty()) Error = TEXT("The accepted dungeon could not activate safely. The player remains protected.");
		Transaction->FailAcceptedActivation(Token, Error, FPlatformTime::Seconds());
		Snapshot.State = EEFCalystoDirectorState::Failed; Snapshot.FailureMessage = Error;
		Snapshot.bGameplayVerified = false; Snapshot.bNativeFloorVerified = false; bSetupPending = false;
		InvalidatePendingLoads();
		UE_LOG(LogCalystoDirector, Warning, TEXT("AcceptedActivationInvariant: %s"), *Error);
		// The runtime was notified of acceptance before activation. Its release path must
		// preserve published outcomes; the accepted transaction cannot enter BeginRetry.
		BeginRollback(); PublishFailure(); return EEFCalystoPlayerReleaseResult::Rejected;
	}
	// The portable source graph is no longer rollback-capable once gameplay has
	// committed. Release its exact destination lease before FloorReady observers
	// update run/floor outcome state; postponing this until the next door leaves
	// a valid accepted floor with a competing snapshot lease and blocks Floor 2.
	if (!FinishRuntimeRequest(ReleasingRuntime, Token.Request, true))
	{
		Error = TEXT("The accepted dungeon gameplay lease could not be finalized. The player remains protected.");
		Transaction->FailAcceptedActivation(Token, Error, FPlatformTime::Seconds());
		Snapshot.State = EEFCalystoDirectorState::Failed; Snapshot.FailureMessage = Error;
		Snapshot.bGameplayVerified = false; Snapshot.bNativeFloorVerified = false; bSetupPending = false;
		InvalidatePendingLoads();
		UE_LOG(LogCalystoDirector, Error, TEXT("AcceptedGameplayLeaseReleaseFailed: %s"), *Error);
		BeginRollback(); PublishFailure(); return EEFCalystoPlayerReleaseResult::Rejected;
	}
	// Gameplay has been transactionally published, the transaction has committed,
	// activation completed, and the exact protected player remains owned. This is
	// the first point at which consumers may treat the floor as gameplay-verified.
	Snapshot.bGameplayVerified = true;
	const FEFCalystoDirectorSnapshot AcceptedSnapshot = GetSnapshot();
	UE_LOG(LogCalystoDirector, Display, TEXT("Floor %lld ready after %d attempt(s), %.3f seconds."),
		AcceptedSnapshot.FloorNumber, AcceptedAttempts, AcceptedSnapshot.RequestElapsedSeconds);
	FloorReady.Broadcast(AcceptedSnapshot);
	return EEFCalystoPlayerReleaseResult::Released;
}

bool UEFCalystoDirectorSubsystem::RejectPlayerRelease(UWorld* World, const FName Code, const FString& Message)
{
	if (!IsInGameThread() || bDeinitializing || bReleaseStarted || !IsDungeonWorld(World)
		|| AttemptWorld.Get() != World || !Transaction || Snapshot.State != EEFCalystoDirectorState::AwaitingPlayerRelease
		|| Transaction->GetPhase() != EEFCalystoFloorPhase::Commit) return false;
	Fail(Code, Message, EEFCalystoAttemptFailure::Spatial);
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	class FDirectorLifecycleRuntime : public IEFCalystoDirectorAttemptRuntime
	{
	public:
		FEFCalystoDirectorAttemptObservation Observation;
		int32 ReleaseCalls = 0;
		int32 BeginCalls = 0;
		mutable int32 DependencyRequests = 0;
		bool bReleaseComplete = false;
		virtual TArray<FSoftObjectPath> GetSharedDependencies() const override { ++DependencyRequests; return {}; }
		virtual bool Preflight(const FEFCalystoCompiledDirector&, const FGuid&, FString&) const override { return true; }
		virtual bool BeginAttempt(const FEFCalystoDirectorAttemptRequest&, FString&) override { ++BeginCalls; return true; }
		int32 ObserveCalls = 0;
		virtual FEFCalystoDirectorAttemptObservation ObserveAttempt(double) override { ++ObserveCalls; return Observation; }
		virtual bool PublishPrepared(const FEFCalystoAttemptToken&, const FEFCalystoCommitEvidence&,
			double, EEFCalystoAttemptFailure&, FString&) override { return true; }
		virtual void ReleaseAttempt(const FEFCalystoAttemptToken&) override { ++ReleaseCalls; }
		virtual FEFCalystoRollbackEvidence ObserveRelease(const FEFCalystoAttemptToken&) const override
		{
			FEFCalystoRollbackEvidence Result;
			Result.bPCGCleanupComplete = bReleaseComplete;
			Result.Actors = bReleaseComplete ? 0 : 1;
			return Result;
		}
	};

	class FDirectorPublicationRuntime final : public FDirectorLifecycleRuntime
	{
	public:
		TFunction<void()> PublicationAction;
		TFunction<void()> AcceptedAction;
		int32 PublicationCalls = 0;
		int32 AcceptedCalls = 0;
		int32 PreparedValue = 7;
		bool bRejectPublication = false;
		virtual bool PublishPrepared(const FEFCalystoAttemptToken&, const FEFCalystoCommitEvidence&,
			double, EEFCalystoAttemptFailure& Failure, FString& Error) override
		{
			++PublicationCalls;
			if (bRejectPublication)
			{ Failure = EEFCalystoAttemptFailure::Resource; Error = TEXT("Injected reversible publication failure."); return false; }
			PreparedValue = 8;
			if (PublicationAction) PublicationAction();
			return true;
		}
		EEFCalystoActivationResult ActivationResult = EEFCalystoActivationResult::Activated;
		virtual EEFCalystoActivationResult OnCommitted(const FEFCalystoAttemptToken&, FString& Error) override
		{ ++AcceptedCalls; if (AcceptedAction) AcceptedAction();
		  if (ActivationResult == EEFCalystoActivationResult::InvariantFailure) Error = TEXT("Injected accepted activation failure.");
		  return ActivationResult; }
		virtual void ReleaseAttempt(const FEFCalystoAttemptToken& Token) override
		{ if (AcceptedCalls == 0) PreparedValue = 7; FDirectorLifecycleRuntime::ReleaseAttempt(Token); }
	};

	class FDetachedGameplaySnapshot final : public IEFCalystoGameplaySnapshot
	{
	public:
		bool bDetached = true;
		virtual FString GetCanonicalHash() const override { return TEXT("DetachedGameplayFixture"); }
		virtual bool IsDetachedForTravel() const override { return bDetached; }
	};

	class FTerminalRecoveryProvider final : public IEFCalystoContentAttemptProvider
	{
	public:
		bool bAllowRecovery = true;
		int32 FinishCalls = 0;
		int32 RecoveryCalls = 0;
		virtual bool CapturePreTravelSnapshot(const FGuid&, UWorld*,
			TSharedPtr<IEFCalystoGameplaySnapshot>& OutSnapshot, FString& Error) override
		{ OutSnapshot.Reset(); Error = TEXT("Fixture capture is not used."); return false; }
		virtual bool CanHandleSnapshot(const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot) const override
		{
			return Snapshot && Snapshot->GetCanonicalHash() == TEXT("DetachedGameplayFixture");
		}
		virtual bool PreparePreTravelSnapshot(const FGuid&,
			const TSharedPtr<IEFCalystoGameplaySnapshot>&, FString& Error) override
		{ Error = TEXT("Fixture preparation is not used."); return false; }
		virtual bool DetachPreTravelSnapshot(const FGuid&,
			const TSharedPtr<IEFCalystoGameplaySnapshot>&, FString& Error) override
		{ Error = TEXT("Fixture detachment is not used."); return false; }
		virtual bool ReconstructPreTravelSnapshot(const FEFCalystoAttemptToken&, UWorld*,
			const TSharedPtr<IEFCalystoGameplaySnapshot>&, FString& Error) override
		{ Error = TEXT("Fixture reconstruction is routed through terminal recovery."); return false; }
		virtual bool RecoverDetachedSnapshot(const FEFCalystoAttemptToken& Token, UWorld* World,
			TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) override
		{
			++RecoveryCalls; Error.Reset();
			if (!bAllowRecovery || !Token.Request.IsValid() || !Token.Attempt.IsValid()
				|| !IsValid(World) || !World->IsGameWorld() || !Snapshot)
			{
				Error = TEXT("Injected terminal destination recovery failure.");
				return false;
			}
			const TSharedPtr<FDetachedGameplaySnapshot> Detached =
				StaticCastSharedPtr<FDetachedGameplaySnapshot>(Snapshot);
			Detached->bDetached = false;
			FinishRequest(Token.Request, Snapshot, false);
			return !Snapshot;
		}
		virtual bool PrepareAttempt(const FEFCalystoContentAttemptProviderRequest&,
			FEFCalystoContentAttemptProviderResult&, FString& Error) override
		{ Error = TEXT("Fixture content preparation is not used."); return false; }
		virtual bool BuildMaterializationContext(const FEFCalystoContentAttemptProviderRequest&,
			const FEFCalystoContentAttemptProviderResult&, TSharedRef<const FEFCalystoReservedContentManifest>,
			FEFCalystoContentGameplayContext&, FString& Error) override
		{ Error = TEXT("Fixture materialization is not used."); return false; }
		virtual bool FinishRequest(const FGuid&, TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot,
			const bool) override
		{
			++FinishCalls;
			if (Snapshot && !Snapshot->IsDetachedForTravel()) Snapshot.Reset();
			return !Snapshot;
		}
	};

	class FTerminalRecoveryRuntime final : public FDirectorLifecycleRuntime
	{
	public:
		IEFCalystoContentAttemptProvider* Provider = nullptr;
		TSharedPtr<IEFCalystoGameplaySnapshot> RetainedSnapshot;
		int32 FinishCalls = 0;
		virtual bool FinishRequest(const FGuid& RequestId, const bool bAccepted) override
		{
			++FinishCalls;
			if (Provider && !Provider->FinishRequest(RequestId, RetainedSnapshot, bAccepted)) return false;
			RetainedSnapshot.Reset();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorPublicationTests,
	"NoShellForWinter.CalystoDungeon.Director.Lifecycle.ReversiblePublicationBeforePlayerRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDirectorPublicationTests::RunTest(const FString&)
{
	if (!GEngine) return false;
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: GameplayPublicationRejected: Injected reversible publication failure."),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: GameplayPublicationPlayerChanged: The protected player changed during reversible gameplay publication."),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: PlayerReleaseRejected: The shared floor request deadline expired before gameplay publication."),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: AcceptedActivationInvariant: Injected accepted activation failure."),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: AcceptedActivationInvariant: The accepted dungeon could not activate safely. The player remains protected."),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 2);
	// Actual world, controller and possessed pawn exercise the production release boundary.
	// Prepared floor evidence and persistent value are controlled fixtures, not gameplay/traversal proof.
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/CalystoPublication_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CalystoPublication"), Package, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Release fixture world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); };
	UGameInstance* Instance = NewObject<UGameInstance>(); World->SetGameInstance(Instance);
	TGuardValue<TSoftObjectPtr<UWorld>> MapGuard(GetMutableDefault<UEFCalystoDirectorSettings>()->DungeonMap, TSoftObjectPtr<UWorld>(World));
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APawn* Player = World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Real controller exists"), Controller) || !TestNotNull(TEXT("Real pawn exists"), Player)) return false;
	USceneComponent* Root = NewObject<USceneComponent>(Player); Player->AddInstanceComponent(Root);
	Player->SetRootComponent(Root); Root->RegisterComponent();
	// This isolated world never initializes a gameplay level. Explicitly perform the
	// controller registration normally supplied by AController::PostInitializeComponents.
	World->AddController(Controller); Controller->Possess(Player);
	if (!TestEqual(TEXT("Controller really possesses the fixture pawn"),Controller->GetPawn().Get(),Player)) return false;
	if (!TestEqual(TEXT("Player zero resolves the possessed pawn"), UGameplayStatics::GetPlayerPawn(World, 0), Player)) return false;
	for (int32 Scenario = 0; Scenario < 11; ++Scenario)
	{
		Player->SetActorLocation(FVector::ZeroVector);
		auto* Owner = NewObject<UEFCalystoDirectorSubsystem>(Instance);
		const auto Runtime = MakeShared<FDirectorPublicationRuntime>();
		Owner->AttemptRuntime = Runtime; Owner->AttemptWorld = World; Owner->bAttemptStarted = true;
		Owner->Snapshot.State = EEFCalystoDirectorState::AwaitingPlayerRelease;
		Owner->Snapshot.FloorNumber = 1; Owner->Snapshot.RunEpoch = 1;
		Owner->Transaction = MakeUnique<FEFCalystoFloorTransaction>();
		const double Started = FPlatformTime::Seconds() - (Scenario == 5 ? 31.0 : 0.0);
		Owner->RequestStartedSeconds = Started;
		FEFCalystoFloorRequestIdentity Identity;
		Identity.SelectedStyle = FGuid(1, 2, 3, 4); Identity.CompiledConfigurationHash = TEXT("PublicationFixture");
		Identity.PreFloorGameplayHash = TEXT("PreparedValue7"); FString Error;
		if (!Owner->Transaction->Begin(Identity, Owner->RoutingRequest.IsValid() ? Owner->RoutingRequest : FGuid::NewGuid(), Started, Error)) { AddError(Error); return false; }
		const auto Token = Owner->Transaction->GetToken();
		auto& Observation = Runtime->Observation; Observation.Token = Token; Observation.Stage = EEFCalystoObservation::Verified;
		auto& Evidence = Observation.Evidence; Observation.ReservationHash = Evidence.ReservationHash = TEXT("ExactFixtureReservation");
		Evidence.RealizationHash = TEXT("ExactFixtureRealization");
		Evidence.bUniqueOwnedStartAndEnd = Evidence.bBlockingFloorAndCapsuleClearance = true;
		Evidence.bCompleteRelevantNavigationRoute = Evidence.bMaterialsVerified = true;
		Evidence.bNativeParityVerified = Evidence.bRoomThemeContractVerified = Evidence.bGameplayPreparedWithoutCommit = true;
		if (!Owner->Transaction->FinishPreflight(Token, Started, Error)
			|| !Owner->Transaction->ArmRootGeneration(Token, Started, Error)
			|| !Owner->Transaction->NativeGenerationFinished(Token, Started, Error)
			|| !Owner->Transaction->StructuralVerificationFinished(Token, Started, Error)
			|| !Owner->Transaction->FreezeReservations(Token, Evidence.ReservationHash, {}, Started, Error)
			|| !Owner->Transaction->VerifyRealization(Token, Evidence, Started, Error))
		{ AddError(Error); return false; }
		Runtime->bRejectPublication = Scenario == 1;
		if (Scenario == 8) Runtime->ActivationResult = EEFCalystoActivationResult::InvariantFailure;
		if (Scenario == 10) Runtime->ActivationResult = EEFCalystoActivationResult::AcceptedExitRequested;
		if (Scenario == 2) Runtime->PublicationAction = [Player]() { Player->SetActorLocation(FVector(100, 0, 0)); };
		if (Scenario == 3) Runtime->PublicationAction = [Owner]() { Owner->RequestCancel(); };
		if (Scenario == 4) Runtime->PublicationAction = [&, Owner]()
		{
			FString NestedError;
			TestFalse(TEXT("A nested release cannot publish twice"), (Owner->ConfirmPlayerRelease(World, Player, NestedError) == EEFCalystoPlayerReleaseResult::Released));
			TestFalse(TEXT("Nested rejection leaves the coordinator uncommitted"), Owner->Transaction->HasCommittedGameplay());
		};
		Runtime->AcceptedAction = [&, Owner, Scenario]()
		{
			TestTrue(TEXT("Activation follows actual coordinator commit"), Owner->Transaction->HasCommittedGameplay());
			TestEqual(TEXT("Activation observes Ready"), Owner->Snapshot.State, EEFCalystoDirectorState::Ready);
			if (Scenario==6) TestTrue(TEXT("Accepted callback may queue the next floor"),Owner->RequestAdvanceFloor());
			if (Scenario==7) Owner->Deinitialize();
			if (Scenario==9) Player->SetActorLocation(FVector(100,0,0));
		};
		int32 ReadyEvents=0;
		Owner->FloorReady.AddLambda([&](const auto& Ready)
		{ ++ReadyEvents; TestEqual(TEXT("Ready notification describes the accepted floor"),Ready.FloorNumber,int64(1)); });
		if (Scenario == 0)
		{
			// Actual final observation can become Pending when player placement or native
			// collision registration dirties navigation. No new seed or publication is authorized.
			for (auto Pending : {EEFCalystoObservation::Generating, EEFCalystoObservation::StructuralVerification,
				EEFCalystoObservation::NavigationAndReservations, EEFCalystoObservation::RealizationVerification})
			{
				Runtime->Observation.Stage = Pending;
				TestEqual(TEXT("Final unfinished observation remains explicitly Pending"), Owner->ConfirmPlayerRelease(World, Player, Error), EEFCalystoPlayerReleaseResult::Pending);
				TestTrue(TEXT("Pending retains the exact frozen token"), Owner->Transaction->Owns(Token));
				TestEqual(TEXT("Pending does not consume an attempt"), Owner->Transaction->GetAttempts().Num(), 1);
				TestEqual(TEXT("Pending performs no publication"), Runtime->PublicationCalls, 0);
				TestEqual(TEXT("Pending performs no rollback"), Runtime->ReleaseCalls, 0);
				TestEqual(TEXT("Pending emits no Ready"), ReadyEvents, 0);
				TestEqual(TEXT("Pending retains protected release state"), Owner->Snapshot.State, EEFCalystoDirectorState::AwaitingPlayerRelease);
			}
			Runtime->Observation.Stage = EEFCalystoObservation::Verified;
		}
		const bool ExpectedAccepted = Scenario == 0 || Scenario == 4 || Scenario >= 6;
		const bool ExpectedReleased = Scenario == 0 || Scenario == 4;
		TestEqual(FString::Printf(TEXT("Scenario %d releases only after valid publication"), Scenario),
			(Owner->ConfirmPlayerRelease(World, Player, Error) == EEFCalystoPlayerReleaseResult::Released), ExpectedReleased);
		TestEqual(TEXT("Publication occurs exactly once, never after an expired deadline"), Runtime->PublicationCalls, Scenario == 5 ? 0 : 1);
		TestEqual(TEXT("Rejected/cancelled publication never activates"), Runtime->AcceptedCalls, ExpectedAccepted ? 1 : 0);
		TestEqual(TEXT("Coordinator retains only owned accepted transactions"),
			Owner->Transaction && Owner->Transaction->HasCommittedGameplay(), ExpectedAccepted && Scenario!=7);
		TestEqual(TEXT("Only rejected publication restores staged value; accepted cleanup preserves it"), Runtime->PreparedValue, ExpectedAccepted ? 8 : 7);
		TestEqual(TEXT("Reentrant advance/deinitialize cannot emit stale Ready"),ReadyEvents,ExpectedReleased ? 1 : 0);
		if (Scenario==6)
		{ TestEqual(TEXT("Accepted callback's next request is preserved"),Owner->Snapshot.FloorNumber,int64(2));
		  TestEqual(TEXT("Next request remains Loading"),Owner->Snapshot.State,EEFCalystoDirectorState::Loading); }
		if (Scenario >= 8)
		{
			TestEqual(TEXT("Activation failure leaves a failed accepted transaction"),Owner->Transaction->GetPhase(),EEFCalystoFloorPhase::Failed);
			TestTrue(TEXT("Accepted record survives activation failure"),Owner->Transaction->GetAttempts().Last().bAccepted);
			TestFalse(TEXT("Accepted failure cannot become an automatic spatial retry"),Owner->Transaction->BeginRetry(FPlatformTime::Seconds(),Error));
			TestFalse(TEXT("Player readiness remains blocked"),Owner->IsLevelRuntimeReady(World));
			Runtime->bReleaseComplete=true; Owner->TickRequest(0);
			TestEqual(TEXT("Cleanup does not start another generation"),Runtime->BeginCalls,0);
			TestTrue(TEXT("Accepted cleanup is recorded"),Owner->Transaction->GetAttempts().Last().bCleanupVerified);
			TestEqual(TEXT("Accepted state survives verified cleanup"),Runtime->PreparedValue,8);
			TestEqual(TEXT("Explicit retry waits for complete cleanup"),Owner->GetSnapshot().bCanRetry,true);
		}
		if (ExpectedReleased)
		{
			TestFalse(TEXT("Repeated confirmation cannot double-commit"), (Owner->ConfirmPlayerRelease(World, Player, Error) == EEFCalystoPlayerReleaseResult::Released));
			TestEqual(TEXT("Repeat did not publish"), Runtime->PublicationCalls, 1);
		}
		else TestEqual(TEXT("Rejection starts one rollback"), Runtime->ReleaseCalls, 1);
		Owner->FloorReady.Clear();
	}
	return true;
}

namespace
{
	class FDirectorBakedLoadingRuntime final : public FDirectorLifecycleRuntime
	{
	public:
		mutable int32 ExpansionCalls=0;
		TFunction<void()> PreflightAction;
		virtual bool GetBakedVisualDependencies(TConstArrayView<FSoftObjectPath> Parents,
			TArray<FSoftObjectPath>& Paths,FString& Error) const override
		{
			++ExpansionCalls; Error.Reset(); Paths={FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))};
			// The expansion intentionally repeats a parent and child to exercise actual cross-phase deduplication.
			Paths.Add(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))); if (!Parents.IsEmpty()) Paths.Add(Parents[0]); return true;
		}
		virtual bool Preflight(const FEFCalystoCompiledDirector&,const FGuid&,FString&) const override
		{ if (PreflightAction) PreflightAction(); return true; }
	};
	class FCalystoLoadingLatentCheck final : public IAutomationLatentCommand
	{
	public:
		explicit FCalystoLoadingLatentCheck(TFunction<bool()> InCheck):Check(MoveTemp(InCheck)) {}
		virtual bool Update() override { return Check(); }
	private: TFunction<bool()> Check;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorBakedLoadingTests,
	"NoShellForWinter.CalystoDungeon.Director.Loading.BakedPhaseOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDirectorBakedLoadingTests::RunTest(const FString&)
{
	// Real streamable handles and native asset, isolated from travel/gameplay. Preflight cancels deliberately.
	auto* Owner=NewObject<UEFCalystoDirectorSubsystem>(NewObject<UGameInstance>()); Owner->AddToRoot();
	const auto Runtime=MakeShared<FDirectorBakedLoadingRuntime>(); Owner->RegisterRuntime(Runtime);
	const FSoftObjectPath Parent(TEXT("/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Table.PCGDA_Table"));
	Owner->VisualLease=UAssetManager::GetStreamableManager().RequestSyncLoad(Parent,false,TEXT("CalystoDirector.TestParentLease"));
	if (!TestTrue(TEXT("Native baked parent has a retained actual streamable lease"),Owner->VisualLease.IsValid() && Parent.ResolveObject()))
	{ Owner->ReleaseLoads(); Owner->RemoveFromRoot(); return false; }
	Owner->RequiredVisualPaths={Parent}; Owner->RequestVisualPathCount=1;
	Owner->Configuration=MakeShared<const FEFCalystoCompiledDirector>();
	Owner->Snapshot.State=EEFCalystoDirectorState::Loading; Owner->Snapshot.FloorNumber=1;
	Owner->Snapshot.StyleId=FGuid(47,48,49,50); Owner->RoutingRequest=FGuid::NewGuid();
	Owner->RequestStartedSeconds=FPlatformTime::Seconds(); Owner->bSetupPending=false;
	Owner->bVisualLoadPending=true; Owner->bVisualDependenciesLoaded=true;
	Owner->Transaction=MakeUnique<FEFCalystoFloorTransaction>();
	FEFCalystoFloorRequestIdentity Identity; Identity.SelectedStyle=Owner->Snapshot.StyleId;
	Identity.CompiledConfigurationHash=TEXT("LoadingOnlyFixture"); Identity.PreFloorGameplayHash=TEXT("NoGameplayFixture");
	FString Error;
	if (!Owner->Transaction->Begin(Identity,Owner->RoutingRequest.IsValid() ? Owner->RoutingRequest : FGuid::NewGuid(),Owner->RequestStartedSeconds,Error))
	{ AddError(Error); Owner->ReleaseLoads(); Owner->RemoveFromRoot(); return false; }
	const FGuid Request=Owner->RoutingRequest;
	const auto Reached=MakeShared<bool>(false);
	Runtime->PreflightAction=[this,Owner,Reached]()
	{
		*Reached=true;
		TestTrue(TEXT("Both real phase leases remain retained through preflight"),Owner->VisualLease.IsValid() && Owner->BakedVisualLease.IsValid());
		TestEqual(TEXT("Cross-phase duplicate requests are removed"),Owner->RequestVisualPathCount,2);
		TestEqual(TEXT("Required resources have exact unique identity"),Owner->RequiredVisualPaths.Num(),2);
		TestTrue(TEXT("Child phase really completed before preflight"),Owner->BakedVisualLease && Owner->BakedVisualLease->HasLoadCompleted());
		Owner->RequestCancel();
	};
	Owner->TryFinishDependencyLoading(Request);
	TestEqual(TEXT("Baked inspection runs once"),Runtime->ExpansionCalls,1);
	Owner->TryFinishDependencyLoading(Request);
	Owner->HandleVisualsLoaded(Request); // a delayed duplicate base completion cannot finish child loading
	TestEqual(TEXT("Repeated observation/base completion cannot expand or issue children twice"),Runtime->ExpansionCalls,1);
	const double Deadline=FPlatformTime::Seconds()+5;
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCalystoLoadingLatentCheck>(
		[this,Owner,Runtime,Reached,Request,Deadline]()
		{
			if (!*Reached && FPlatformTime::Seconds()<Deadline) return false;
			TestTrue(TEXT("Actual asynchronous child load reaches preflight within five seconds"),*Reached);
			if (!*Reached) Owner->RequestCancel();
			Owner->NextCleanupObservationSeconds=0; Owner->TickRequest(0);
			TestFalse(TEXT("Cancellation releases parent lease after cleanup"),Owner->VisualLease.IsValid());
			TestFalse(TEXT("Cancellation releases child lease after cleanup"),Owner->BakedVisualLease.IsValid());
			Owner->HandleBakedVisualsLoaded(Request); Owner->HandleVisualsLoaded(Request);
			TestEqual(TEXT("Stale phase callbacks cannot revive the cancelled request"),Owner->Snapshot.State,EEFCalystoDirectorState::Cancelled);
			TestEqual(TEXT("Loading fixture performs no root generation"),Runtime->BeginCalls,0);
			Runtime->PreflightAction=nullptr; Owner->UnregisterRuntime(&Runtime.Get()); Owner->RemoveFromRoot(); return true;
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorLifecycleTests,
	"NoShellForWinter.CalystoDungeon.Director.Lifecycle.CallbacksOwnershipAndDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDirectorLifecycleTests::RunTest(const FString&)
{
	// Each injected failure must publish exactly one matching diagnostic.
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: FixtureResourceFailure: Injected resource failure."), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: RuntimeUnavailable: The dungeon runtime is unavailable."), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: RequestDeadline: The dungeon could not be prepared within 30 seconds."), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 2);
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: FixtureSpatialFailure: Injected spatial failure."), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	// No Initialize, world, ticker, travel, asset load or native generation is used.
	// These fixtures exercise request ownership independently of gameplay proof.
	const auto NewOwner = []()
	{
		return NewObject<UEFCalystoDirectorSubsystem>(NewObject<UGameInstance>());
	};
	const auto PrepareNative = [&](UEFCalystoDirectorSubsystem& Owner, const TSharedRef<FDirectorLifecycleRuntime>& Runtime)
	{
		const double Now = FPlatformTime::Seconds();
		Owner.RequestStartedSeconds = Now;
		Owner.Snapshot.FloorNumber = 1;
		Owner.Snapshot.State = EEFCalystoDirectorState::Generating;
		Owner.Transaction = MakeUnique<FEFCalystoFloorTransaction>();
		FEFCalystoFloorRequestIdentity Identity;
		Identity.SelectedStyle = FGuid(1, 2, 3, 4);
		Identity.CompiledConfigurationHash = TEXT("LifecycleFixture");
		Identity.PreFloorGameplayHash = TEXT("FixtureOnly");
		FString Error;
		if (!Owner.Transaction->Begin(Identity, Owner.RoutingRequest.IsValid() ? Owner.RoutingRequest : FGuid::NewGuid(), Now, Error)
			|| !Owner.Transaction->FinishPreflight(Owner.Transaction->GetToken(), Now, Error)
			|| !Owner.Transaction->ArmRootGeneration(Owner.Transaction->GetToken(), Now, Error))
		{
			AddError(TEXT("The isolated lifecycle fixture could not begin: ") + Error);
			return false;
		}
		Owner.RegisterRuntime(Runtime);
		Owner.bAttemptStarted = true;
		Runtime->Observation.Token = Owner.Transaction->GetToken();
		return true;
	};

	{
		auto* Owner = NewOwner();
		const auto Runtime = MakeShared<FDirectorLifecycleRuntime>();
		const auto OtherRuntime = MakeShared<FDirectorLifecycleRuntime>();
		Owner->RegisterRuntime(Runtime);
		const FGuid OldGeneration = Owner->SharedLoadGeneration;
		TestTrue(TEXT("Empty shared dependencies complete without asynchronous work"), Owner->bSharedLoadReady);
		Owner->ReleaseLoads(); Owner->InvalidatePendingLoads(); Owner->RegisterRuntime(Runtime);
		TestTrue(TEXT("Per-request release retains session dependency ownership"), Owner->SharedLoadGeneration == OldGeneration && Owner->bSharedLoadReady);
		TestEqual(TEXT("Identical runtime registration does not request dependencies twice"), Runtime->DependencyRequests, 1);
		Owner->UnregisterRuntime(&Runtime.Get());
		TestFalse(TEXT("Unregister invalidates shared dependency callback ownership"), Owner->SharedLoadGeneration.IsValid());
		Owner->RegisterRuntime(OtherRuntime);
		Owner->bSharedLoadPending = true; Owner->bSharedLoadReady = false;
		Owner->HandleSharedDependenciesLoaded(OldGeneration);
		TestTrue(TEXT("A stale shared completion cannot finish its successor"), Owner->bSharedLoadPending);
		Owner->SharedLoadStartedSeconds = FPlatformTime::Seconds() - 31.0;
		Owner->TickRequest(0);
		TestFalse(TEXT("Shared preparation has a finite observation budget"), Owner->bSharedLoadPending);
		TestFalse(TEXT("Timed out preparation is not ready"), Owner->bSharedLoadReady);
		TestEqual(TEXT("A HUB prewarm failure does not create a travel request"), Owner->Snapshot.State, EEFCalystoDirectorState::Idle);
		TestTrue(TEXT("An explicit request can retry failed shared preparation"), Owner->RequestStartNewRunWithSeed(42));
		TestEqual(TEXT("The explicit retry does not change the authored seed"), Owner->Snapshot.RunSeed, int64(42));
		TestEqual(TEXT("Failed session preparation retries exactly once per new request"), OtherRuntime->DependencyRequests, 2);
		TestTrue(TEXT("Cancelling the request is supported during preflight setup"), Owner->RequestCancel());
		TestTrue(TEXT("Request cancellation retains completed session preparation"), Owner->bSharedLoadReady);
		Owner->UnregisterRuntime(&OtherRuntime.Get());
	}

	{
		auto* Owner = NewOwner();
		const auto Runtime = MakeShared<FDirectorLifecycleRuntime>();
		Owner->RegisterRuntime(Runtime);
		TestTrue(TEXT("A cold request can join pending HUB preparation"), Owner->RequestStartNewRunWithSeed(43));
		const FGuid SharedGeneration = Owner->SharedLoadGeneration;
		// Reproduce shared age 35s with floor age 25s without a wall-clock wait or asset load.
		const double Now = FPlatformTime::Seconds();
		Owner->SharedLoadStartedSeconds = Now - 35.0;
		Owner->RequestStartedSeconds = Now - 25.0;
		Owner->bSharedLoadPending = true; Owner->bSharedLoadReady = false; Owner->bSetupPending = false;
		Owner->TickRequest(0);
		TestTrue(TEXT("HUB prewarm age cannot cancel a floor with time remaining"), Owner->bSharedLoadPending);
		TestEqual(TEXT("Joining the floor does not request the shared resources again"), Runtime->DependencyRequests, 1);
		TestTrue(TEXT("Joining the floor retains the original shared callback generation"), Owner->SharedLoadGeneration == SharedGeneration);
		Owner->HandleSharedDependenciesLoaded(SharedGeneration);
		TestTrue(TEXT("Shared completion within the floor deadline remains eligible"), Owner->bSharedLoadReady);
		Owner->SharedPaths.Add(FSoftObjectPath(TEXT("/Engine/Transient.CalystoMissingDependency")));
		Owner->bSharedLoadPending = true;
		Owner->HandleSharedDependenciesLoaded(SharedGeneration);
		TestFalse(TEXT("The shared deadline correction does not accept an unresolved resource"), Owner->bSharedLoadReady);
		TestTrue(TEXT("Missing shared resource evidence names the actual path"), Owner->SharedLoadFailure.Contains(TEXT("/Engine/Transient.CalystoMissingDependency")));
		Owner->SharedPaths.Reset(); Owner->SharedLoadFailure.Reset(); Owner->bSharedLoadPending = true;
		Owner->RequestStartedSeconds = Now - 31.0;
		Owner->TickRequest(0);
		TestEqual(TEXT("The floor still fails at its original 30 second deadline"), Owner->Snapshot.State, EEFCalystoDirectorState::Failed);
		TestFalse(TEXT("Expired shared preparation does not remain pending"), Owner->bSharedLoadPending);
		Owner->HandleSharedDependenciesLoaded(SharedGeneration);
		TestFalse(TEXT("A late shared completion cannot revive an expired load"), Owner->bSharedLoadReady);
		Owner->UnregisterRuntime(&Runtime.Get());
	}
	{
		auto* Owner = NewOwner();
		const auto Runtime = MakeShared<FDirectorLifecycleRuntime>();
		Owner->RegisterRuntime(Runtime);
		Owner->RequestStartNewRunWithSeed(44);
		Owner->SharedLoadStartedSeconds = FPlatformTime::Seconds() - 31.0;
		Owner->bSharedLoadPending = true; Owner->bSharedLoadReady = false;
		Owner->RequestCancel(); Owner->TickRequest(0);
		TestFalse(TEXT("Cancellation restores the finite HUB preparation deadline"), Owner->bSharedLoadPending);
		TestEqual(TEXT("Shared expiry cannot restart a cancelled floor"), Owner->Snapshot.State, EEFCalystoDirectorState::Cancelled);
		Owner->UnregisterRuntime(&Runtime.Get());
	}

	{
		auto* Owner = NewOwner();
		const FGuid OldRequest = FGuid::NewGuid();
		Owner->RoutingRequest = OldRequest;
		Owner->Snapshot.State = EEFCalystoDirectorState::Loading;
		Owner->bConfigurationLoadPending = true;
		Owner->RequestStartedSeconds = FPlatformTime::Seconds();
		Owner->Fail(TEXT("FixtureResourceFailure"), TEXT("Injected resource failure."), EEFCalystoAttemptFailure::Resource);
		Owner->HandleConfigurationLoaded(OldRequest);
		Owner->HandleVisualsLoaded(OldRequest);
		TestFalse(TEXT("Failed request invalidates old callback routing"), Owner->RoutingRequest.IsValid());
		TestFalse(TEXT("Stale configuration callback cannot create a transaction"), Owner->Transaction.IsValid());
		TestEqual(TEXT("Stale callback cannot restore loading"), Owner->Snapshot.State, EEFCalystoDirectorState::Failed);
	}
	{
		auto* Owner = NewOwner();
		const auto Runtime = MakeShared<FDirectorLifecycleRuntime>();
		const auto OtherRuntime = MakeShared<FDirectorLifecycleRuntime>();
		TestTrue(TEXT("A runtime can register"), Owner->RegisterRuntime(Runtime));
		TestTrue(TEXT("Registration of the same owner is idempotent"), Owner->RegisterRuntime(Runtime));
		TestFalse(TEXT("A competing runtime cannot replace the owner"), Owner->RegisterRuntime(OtherRuntime));
		if (!PrepareNative(*Owner, Runtime)) return false;
		Owner->UnregisterRuntime(&Runtime.Get());
		TestEqual(TEXT("Unregister issues exactly one release"), Runtime->ReleaseCalls, 1);
		TestTrue(TEXT("Unregister retains the runtime while cleanup is unfinished"), Owner->AttemptRuntime.IsValid());
		Owner->TickRequest(0);
		TestTrue(TEXT("Unfinished cleanup retains ownership after observation"), Owner->bReleaseStarted);
		Runtime->bReleaseComplete = true;
		Owner->NextCleanupObservationSeconds = 0;
		Owner->TickRequest(0);
		TestFalse(TEXT("Completed cleanup releases the unregistering runtime"), Owner->AttemptRuntime.IsValid());
		TestEqual(TEXT("Runtime loss does not reseed generation"), Runtime->BeginCalls, 0);
		TestFalse(TEXT("The failed request no longer waits"), Owner->IsTravelRequestPending());
	}
	{
		auto* Owner = NewOwner();
		Owner->Snapshot.State = EEFCalystoDirectorState::Loading;
		Owner->Snapshot.FloorNumber = 1;
		Owner->RoutingRequest = FGuid::NewGuid();
		Owner->bSetupPending = true;
		Owner->RequestStartedSeconds = FPlatformTime::Seconds() - 31.0;
		Owner->TickRequest(0);
		TestEqual(TEXT("The door request deadline applies before loading starts"), Owner->Snapshot.State, EEFCalystoDirectorState::Failed);
		TestFalse(TEXT("Expired setup cannot request an asset lease"), Owner->ConfigurationLease.IsValid());
		TestFalse(TEXT("Expired setup cannot resume next tick"), Owner->bSetupPending);
	}
	{
		auto* Owner = NewOwner();
		const auto Runtime = MakeShared<FDirectorLifecycleRuntime>();
		if (!PrepareNative(*Owner, Runtime)) return false;
		Runtime->Observation.Stage = EEFCalystoObservation::StructuralVerification;
		Runtime->Observation.Metrics.GenerateLocalCalls = 1;
		Runtime->Observation.Metrics.ObservationCode = TEXT("NAVIGATION_DATA_PENDING");
		Runtime->Observation.Metrics.ActiveNavigationTiles = 0;
		Owner->ObserveNativeAttempt(Owner->RequestStartedSeconds - 1.0);
		TestEqual(TEXT("Observation uses a fresh timestamp after native work"), Owner->Transaction->GetPhase(), EEFCalystoFloorPhase::StructuralVerification);
		const int32 ObservationsBeforeInspection = Runtime->ObserveCalls;
		const FString Inspection = Owner->GetDiagnosticsJson();
		TestTrue(TEXT("Inspector distinguishes actual native calls from generation authorization"), Inspection.Contains(TEXT("\"actual_root_generation_requests\":1")));
		TestTrue(TEXT("Inspector retains the concrete pending navigation reason"), Inspection.Contains(TEXT("NAVIGATION_DATA_PENDING")));
		TestTrue(TEXT("Inspector reports actual empty tiles without claiming readiness"), Inspection.Contains(TEXT("\"active_navigation_tiles\":0")));
		Owner->GetDiagnosticsJson();
		TestEqual(TEXT("Inspector never advances the native owner"), Runtime->ObserveCalls, ObservationsBeforeInspection);
		const auto ActiveToken = Runtime->Observation.Token;
		Runtime->Observation.Token.Attempt = FGuid::NewGuid();
		Runtime->Observation.Metrics.GenerateLocalCalls = 7;
		Owner->ObserveNativeAttempt(FPlatformTime::Seconds());
		TestTrue(TEXT("Stale native metrics cannot overwrite the active attempt"), Owner->GetDiagnosticsJson().Contains(TEXT("\"actual_root_generation_requests\":1")));
		Runtime->Observation.Token = ActiveToken;
		Owner->Fail(TEXT("FixtureSpatialFailure"), TEXT("Injected spatial failure."), EEFCalystoAttemptFailure::Spatial);
		TestTrue(TEXT("Rejected attempt evidence survives rollback for inspection"), Owner->GetDiagnosticsJson().Contains(TEXT("NAVIGATION_DATA_PENDING")));
		Owner->bSetupPending = true; // A queued next request must not survive cleanup failure.
		Owner->CleanupStartedSeconds = FPlatformTime::Seconds() - 6.0;
		Owner->NextCleanupObservationSeconds = 0;
		Owner->TickRequest(0);
		TestEqual(TEXT("Cleanup timeout exposes a finite failed state"), Owner->Snapshot.State, EEFCalystoDirectorState::Failed);
		TestFalse(TEXT("Cleanup timeout cancels queued setup"), Owner->bSetupPending);
		TestFalse(TEXT("Cleanup timeout does not leave an endless loading state"), Owner->IsTravelRequestPending());
		TestFalse(TEXT("Another run cannot overlap unfinished release"), Owner->RequestStartNewRunWithSeed(42));
		TestFalse(TEXT("Cleanup timeout does not claim native verification"), Owner->Snapshot.bNativeFloorVerified);
		Runtime->bReleaseComplete = true;
		Owner->NextCleanupObservationSeconds = 0;
		Owner->TickRequest(0);
		TestEqual(TEXT("Late cleanup completion cannot start a hidden retry"), Runtime->BeginCalls, 0);
		TestTrue(TEXT("Explicit retry becomes available only after cleanup"), Owner->GetSnapshot().bCanRetry);
	}
	{
		auto* Owner = NewOwner();
		const auto Runtime = MakeShared<FDirectorLifecycleRuntime>();
		if (!PrepareNative(*Owner, Runtime)) return false;
		// The accepted-floor owner must remain retained during an advance request.
		Owner->Snapshot.RunEpoch = 3;
		Runtime->Observation.Stage = EEFCalystoObservation::Verified;
		Runtime->Observation.ReservationHash = TEXT("FixtureReservation");
		FEFCalystoCommitEvidence& Evidence = Runtime->Observation.Evidence;
		Evidence.ReservationHash = Runtime->Observation.ReservationHash;
		Evidence.RealizationHash = TEXT("FixtureRealization");
		Evidence.bUniqueOwnedStartAndEnd = Evidence.bBlockingFloorAndCapsuleClearance = true;
		Evidence.bCompleteRelevantNavigationRoute = Evidence.bMaterialsVerified = true;
		Evidence.bNativeParityVerified = Evidence.bRoomThemeContractVerified = true;
		Evidence.bGameplayPreparedWithoutCommit = true;
		Owner->ObserveNativeAttempt(FPlatformTime::Seconds());
		TestEqual(TEXT("Native evidence waits for actual player release"), Owner->Snapshot.State, EEFCalystoDirectorState::AwaitingPlayerRelease);
		TestEqual(TEXT("Native evidence stops at the commit gate"), Owner->Transaction->GetPhase(), EEFCalystoFloorPhase::Commit);
		TestFalse(TEXT("Native evidence does not commit gameplay"), Owner->Transaction->HasCommittedGameplay());
		TestFalse(TEXT("Native candidate evidence is not full gameplay verification"), Owner->Snapshot.bGameplayVerified);
		FString ReleaseError;
		TestFalse(TEXT("A missing world/player cannot confirm release"), (Owner->ConfirmPlayerRelease(nullptr, nullptr, ReleaseError) == EEFCalystoPlayerReleaseResult::Released));
		// Isolated advancement fixture only; this does not simulate or prove player release.
		Owner->Snapshot.State = EEFCalystoDirectorState::Ready;
		Owner->Snapshot.LastCommittedFloorNumber = 1;
		Owner->Snapshot.LastCommittedRunEpoch = 3;
		TestTrue(TEXT("A floor advance queues one request"), Owner->RequestAdvanceFloor());
		TestEqual(TEXT("Advance records the requested floor"), Owner->Snapshot.FloorNumber, int64(2));
		TestEqual(TEXT("Advance does not commit the requested floor"), Owner->Snapshot.LastCommittedFloorNumber, int64(1));
		TestEqual(TEXT("Advance releases the previous attempt once"), Runtime->ReleaseCalls, 1);
		Owner->TickRequest(0);
		TestFalse(TEXT("The next configuration cannot load before teardown"), Owner->ConfigurationLease.IsValid());
		TestEqual(TEXT("The next native generation cannot overlap teardown"), Runtime->BeginCalls, 0);
		TestTrue(TEXT("Cancelling pending advancement is supported"), Owner->RequestCancel());
		Runtime->bReleaseComplete = true;
		Owner->NextCleanupObservationSeconds = 0;
		Owner->TickRequest(0);
		TestEqual(TEXT("Cancellation settles after owner release"), Owner->Snapshot.State, EEFCalystoDirectorState::Cancelled);
		TestEqual(TEXT("Cancellation never double-releases native work"), Runtime->ReleaseCalls, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorDetachedSnapshotRecoveryTests,
	"NoShellForWinter.CalystoDungeon.Director.Lifecycle.DetachedSnapshotTerminalRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDirectorDetachedSnapshotRecoveryTests::RunTest(const FString&)
{
	if (!GEngine) return false;
	AddExpectedMessagePlain(TEXT("LogCalystoDirector: TerminalGameplayRecoveryPending: Injected terminal destination recovery failure."),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 1);
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/CalystoDetachedRecovery_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CalystoDetachedRecovery"), Package,
		true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Terminal recovery fixture world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); };
	UGameInstance* Instance = NewObject<UGameInstance>();
	World->SetGameInstance(Instance);
	const FGuid Request = FGuid::NewGuid();
	const FEFCalystoAttemptToken Token{ Request, FGuid::NewGuid() };

	const auto MakeOwner = [&]()
	{
		auto* Owner = NewObject<UEFCalystoDirectorSubsystem>(Instance);
		auto Snapshot = MakeShared<FDetachedGameplaySnapshot>();
		Owner->PreTravelGameplaySnapshot = Snapshot;
		Owner->PreTravelGameplayRequest = Request;
		Owner->bPreTravelGameplayDetached = true;
		Owner->bPreTravelGameplayHandedToRuntime = true;
		Owner->ReleasingToken = Token;
		Owner->AttemptWorld = World;
		Owner->Snapshot.State = EEFCalystoDirectorState::Failed;
		Owner->Snapshot.FloorNumber = 1;
		return Owner;
	};

	{
		FTerminalRecoveryProvider Provider;
		auto* Owner = MakeOwner();
		Owner->PreTravelGameplayProvider = &Provider;
		const auto Runtime = MakeShared<FTerminalRecoveryRuntime>();
		Runtime->Provider = &Provider;
		Runtime->RetainedSnapshot = Owner->PreTravelGameplaySnapshot;
		Owner->FinishRuntimeRequest(Runtime, Request, false);
		TestEqual(TEXT("Terminal release first defers the detached runtime lease"), Runtime->FinishCalls, 1);
		TestEqual(TEXT("Terminal recovery reconstructs exactly once after runtime cleanup"), Provider.RecoveryCalls, 1);
		TestEqual(TEXT("Provider releases only after the graph is reconstructed"), Provider.FinishCalls, 2);
		TestFalse(TEXT("Successful terminal recovery retains no old gameplay snapshot"), Owner->PreTravelGameplaySnapshot.IsValid());
		TestFalse(TEXT("Successful terminal recovery does not strand the recovery flag"), Owner->bTerminalGameplayRecoveryPending);
		TestFalse(TEXT("Successful terminal recovery clears the old routing owner"), Owner->PreTravelGameplayRequest.IsValid());
		TestEqual(TEXT("Successful terminal recovery closes the terminal request once"), Owner->FinishedRuntimeRequest, Request);
	}
	{
		FTerminalRecoveryProvider Provider;
		Provider.bAllowRecovery = false;
		auto* Owner = MakeOwner();
		Owner->PreTravelGameplayProvider = &Provider;
		const auto Runtime = MakeShared<FTerminalRecoveryRuntime>();
		Runtime->Provider = &Provider;
		Runtime->RetainedSnapshot = Owner->PreTravelGameplaySnapshot;
		Owner->FinishRuntimeRequest(Runtime, Request, false);
		TestTrue(TEXT("Failed terminal recovery retains the detached graph"), Owner->PreTravelGameplaySnapshot.IsValid()
			&& Owner->PreTravelGameplaySnapshot->IsDetachedForTravel());
		TestTrue(TEXT("Failed terminal recovery exposes explicit protected recovery"), Owner->bTerminalGameplayRecoveryPending);
		TestTrue(TEXT("Failed terminal recovery keeps Retry available without a reseed"), Owner->GetSnapshot().bCanRetry);
		TestFalse(TEXT("Failed terminal recovery does not finish the request"), Owner->FinishedRuntimeRequest == Request);
		Provider.bAllowRecovery = true;
		FString Error;
		TestTrue(TEXT("Explicit recovery can retry the same detached graph"), Owner->ResolveTerminalPreTravelGameplaySnapshot(Error));
		TestFalse(TEXT("Explicit recovery clears persistent ownership before a new request"), Owner->PreTravelGameplaySnapshot.IsValid());
		Owner->FinishRuntimeRequest(Runtime, Request, false);
		TestEqual(TEXT("Recovered terminal request can close without another topology attempt"), Owner->FinishedRuntimeRequest, Request);
	}
	return true;
}
#endif
