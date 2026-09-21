#include "Calysto/EFCalystoPackagedSmokeSubsystem.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoFloorDoor.h"
#include "Components/ACFInteractionComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "HighResScreenshot.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoV6PackagedSmoke, Log, All);

namespace EFCalystoV6PackagedSmokePrivate
{
static const FName NaturalScenario(TEXT("Natural"));
static const FName NoThemeId(TEXT("NoTheme"));
static const FName PopulationActorTag(TEXT("EF.Calysto.Population"));
static const FName PopulationAnchorTag(TEXT("EF.Calysto.PopulationAnchor"));
static constexpr const TCHAR* ExpectedPolicyPath =
	TEXT("/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy");
static constexpr const TCHAR* ExpectedPolicyClass =
	TEXT("/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset");
static constexpr const TCHAR* EntranceDoorClass =
	TEXT("/Game/Procedural/DoorToLevel.DoorToLevel_C");

static bool ParseInt64Option(const TCHAR* Key, int64& OutValue)
{
	FString Text;
	return FParse::Value(FCommandLine::Get(), Key, Text) && LexTryParseString(OutValue, *Text);
}

static bool ParseInt32Option(const TCHAR* Key, int32& OutValue)
{
	FString Text;
	return FParse::Value(FCommandLine::Get(), Key, Text) && LexTryParseString(OutValue, *Text);
}

static bool ParseFloatOption(const TCHAR* Key, float& OutValue)
{
	FString Text;
	return FParse::Value(FCommandLine::Get(), Key, Text) && LexTryParseString(OutValue, *Text);
}

static bool HasOption(const TCHAR* Key)
{
	FString Ignored;
	return FParse::Value(FCommandLine::Get(), Key, Ignored);
}

static bool HasSha256(const FString& Value)
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

static bool IsSafeToken(const FString& Value, const int32 MaximumLength)
{
	if (Value.IsEmpty() || Value.Len() > MaximumLength || !FChar::IsAlnum(Value[0]))
	{
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

static FString MakeRunTag()
{
	return FString::Printf(
		TEXT("%s_%s"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
}

static FString JoinNames(const TArray<FName>& Names)
{
	TArray<FString> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(Name.ToString());
	}
	return FString::Join(Values, TEXT(","));
}

static bool ValidateReadinessTrace(const TArray<FName>& Trace, FString& OutError)
{
	static const FName ExpectedTrace[] = {
		TEXT("GenerateLocal"),
		TEXT("PCGComplete"),
		TEXT("ManifestReady"),
		TEXT("VisualsReady"),
		TEXT("NavigationPathReady"),
		TEXT("EnemyLevelsReady"),
		TEXT("PopulationRealized"),
		TEXT("CompanionRosterReady"),
		TEXT("DoorEnabled")};
	if (Trace.Num() != UE_ARRAY_COUNT(ExpectedTrace))
	{
		OutError = TEXT("READINESS_TRACE_CARDINALITY_INVALID");
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedTrace); ++Index)
	{
		if (Trace[Index] != ExpectedTrace[Index])
		{
			OutError = FString::Printf(
				TEXT("READINESS_TRACE_ORDER_INVALID:%d:%s:%s"),
				Index,
				*ExpectedTrace[Index].ToString(),
				*Trace[Index].ToString());
			return false;
		}
	}
	return true;
}
}

bool UEFCalystoPackagedSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_EDITOR
	(void)Outer;
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer) &&
		FPlatformProperties::RequiresCookedData() &&
		FApp::IsUnattended() &&
		FParse::Param(FCommandLine::Get(), TEXT("CalystoV6PackagedSmoke"));
#endif
}

void UEFCalystoPackagedSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UEFCalystoDungeonSubsystem::StaticClass());
	DungeonSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	StartedAtSeconds = FPlatformTime::Seconds();

	FString Error;
	if (!ConfigureFromCommandLine(Error) || !InitializeProjectTelemetry(Error))
	{
		Finish(false, Error);
		return;
	}
	if (!DungeonSubsystem)
	{
		Finish(false, TEXT("DUNGEON_SUBSYSTEM_MISSING"));
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (ForcedDungeonEdge > 0)
	{
		DungeonSubsystem->SetForcedDungeonEdgeForAutomation(ForcedDungeonEdge);
	}
	if (Scenario != EFCalystoV6PackagedSmokePrivate::NaturalScenario)
	{
		DungeonSubsystem->SetPopulationScenarioForAutomation(Scenario);
	}
#endif

	DungeonSubsystem->OnFloorReady().AddUObject(
		this, &UEFCalystoPackagedSmokeSubsystem::HandleFloorReady);
	DungeonSubsystem->OnFloorTravelFailed().AddUObject(
		this, &UEFCalystoPackagedSmokeSubsystem::HandleFloorTravelFailed);
	BootstrapTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoPackagedSmokeSubsystem::HandleBootstrapTick),
		0.25f);
	TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoPackagedSmokeSubsystem::HandleTimeoutTick),
		1.0f);
}

void UEFCalystoPackagedSmokeSubsystem::Deinitialize()
{
	CancelTicker(BootstrapTickerHandle);
	CancelTicker(DoorSelectionTickerHandle);
	CancelTicker(TimeoutTickerHandle);
	CancelTicker(ExitTickerHandle);
	if (DungeonSubsystem)
	{
		DungeonSubsystem->OnFloorReady().RemoveAll(this);
		DungeonSubsystem->OnFloorTravelFailed().RemoveAll(this);
#if WITH_DEV_AUTOMATION_TESTS
		DungeonSubsystem->ClearForcedDungeonEdgeForAutomation();
		DungeonSubsystem->ClearPopulationScenarioForAutomation();
#endif
	}
	DungeonSubsystem = nullptr;
	Super::Deinitialize();
}

bool UEFCalystoPackagedSmokeSubsystem::ConfigureFromCommandLine(FString& OutError)
{
	using namespace EFCalystoV6PackagedSmokePrivate;
	OutError.Reset();
#if UE_BUILD_SHIPPING
	ConfigurationName = TEXT("Shipping");
#else
	ConfigurationName = TEXT("Development");
#endif

	FString ParsedRunTag;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV6SmokeRunTag="), ParsedRunTag))
	{
		if (!IsSafeToken(ParsedRunTag, 96))
		{
			RunTag = MakeRunTag();
			OutError = TEXT("INVALID_RUN_TAG");
			return false;
		}
		RunTag = MoveTemp(ParsedRunTag);
	}
	else
	{
		RunTag = MakeRunTag();
	}

	if (HasOption(TEXT("CalystoV6SmokeSeed=")) &&
		(!ParseInt64Option(TEXT("CalystoV6SmokeSeed="), RunSeed) || RunSeed <= 0))
	{
		OutError = TEXT("INVALID_RUN_SEED");
		return false;
	}
	if (HasOption(TEXT("CalystoV6SmokeMaxFloor=")) &&
		(!ParseInt32Option(TEXT("CalystoV6SmokeMaxFloor="), MaximumFloor) ||
		 MaximumFloor < 1 || MaximumFloor > 100))
	{
		OutError = TEXT("INVALID_MAXIMUM_FLOOR");
		return false;
	}
	if (HasOption(TEXT("CalystoV6SmokeTimeout=")) &&
		(!ParseFloatOption(TEXT("CalystoV6SmokeTimeout="), TimeoutSeconds) ||
		 !FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds < 60.0f || TimeoutSeconds > 1800.0f))
	{
		OutError = TEXT("INVALID_TIMEOUT");
		return false;
	}
	if (HasOption(TEXT("CalystoV6SmokeForcedEdge=")) &&
		(!ParseInt32Option(TEXT("CalystoV6SmokeForcedEdge="), ForcedDungeonEdge) ||
		 (ForcedDungeonEdge != 0 &&
		  (ForcedDungeonEdge < EFCalystoDungeonRuntimeSchemaV6::MinimumDungeonEdge ||
		   ForcedDungeonEdge > EFCalystoDungeonRuntimeSchemaV6::MaximumDungeonEdge))))
	{
		OutError = TEXT("INVALID_FORCED_DUNGEON_EDGE");
		return false;
	}

	FString ScenarioText;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV6SmokeScenario="), ScenarioText))
	{
		if (!IsSafeToken(ScenarioText, 64))
		{
			OutError = TEXT("INVALID_POPULATION_SCENARIO");
			return false;
		}
		Scenario = FName(*ScenarioText);
	}

	const bool bHasDevelopmentOverride =
		ForcedDungeonEdge > 0 || Scenario != NaturalScenario;
#if UE_BUILD_SHIPPING
	if (bHasDevelopmentOverride)
	{
		OutError = TEXT("SHIPPING_DEVELOPMENT_OVERRIDE_REJECTED");
		return false;
	}
#elif !WITH_DEV_AUTOMATION_TESTS
	if (bHasDevelopmentOverride)
	{
		OutError = TEXT("DEVELOPMENT_AUTOMATION_HOOKS_NOT_COMPILED");
		return false;
	}
#endif

	bCaptureVisual = FParse::Param(FCommandLine::Get(), TEXT("CalystoV6SmokeCapture"));
	const FString EvidenceDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("CalystoDungeonDirectorV6"));
	IFileManager::Get().MakeDirectory(*EvidenceDirectory, true);
	const FString SafeScenario = Scenario.ToString();
	ReceiptPath = FPaths::Combine(
		EvidenceDirectory,
		FString::Printf(TEXT("PackagedSmokeReceipt_%s_%s_%s.json"),
			*ConfigurationName, *SafeScenario, *RunTag));
	ProjectTelemetryPath = FPaths::Combine(
		EvidenceDirectory,
		FString::Printf(TEXT("PackagedSmokeTelemetry_%s_%s_%s.log"),
			*ConfigurationName, *SafeScenario, *RunTag));
	if (bCaptureVisual)
	{
		ScreenshotPath = FPaths::Combine(
			EvidenceDirectory,
			FString::Printf(TEXT("PackagedSmokeVisual_%s_%s_%s.png"),
				*ConfigurationName, *SafeScenario, *RunTag));
	}
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::InitializeProjectTelemetry(FString& OutError)
{
	OutError.Reset();
	if (ProjectTelemetryPath.IsEmpty())
	{
		OutError = TEXT("PROJECT_TELEMETRY_PATH_MISSING");
		return false;
	}
	if (IFileManager::Get().FileSize(*ProjectTelemetryPath) >= 0)
	{
		OutError = TEXT("PROJECT_TELEMETRY_FILE_COLLISION");
		return false;
	}
	const FString Header = FString::Printf(
		TEXT("CALYSTO_V6_PROJECT_TELEMETRY schema=%d runTag=%s configuration=%s scenario=%s"),
		ProjectTelemetrySchemaVersion,
		*RunTag,
		*ConfigurationName,
		*Scenario.ToString());
	if (!FFileHelper::SaveStringToFile(
		Header + LINE_TERMINATOR,
		*ProjectTelemetryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = TEXT("PROJECT_TELEMETRY_CREATE_FAILED");
		return false;
	}
	bProjectTelemetryInitialized = true;
	bProjectTelemetryHealthy = true;
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::AppendProjectTelemetry(const FString& EventPayload)
{
	if (!bProjectTelemetryInitialized || !bProjectTelemetryHealthy || EventPayload.IsEmpty())
	{
		bProjectTelemetryHealthy = false;
		return false;
	}
	const uint64 NextSequence = ProjectTelemetrySequence + 1;
	const FString Line = FString::Printf(
		TEXT("CALYSTO_V6_PROJECT_TELEMETRY schema=%d sequence=%llu %s"),
		ProjectTelemetrySchemaVersion,
		static_cast<unsigned long long>(NextSequence),
		*EventPayload.ReplaceCharWithEscapedChar());
	if (!FFileHelper::SaveStringToFile(
		Line + LINE_TERMINATOR,
		*ProjectTelemetryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append))
	{
		bProjectTelemetryHealthy = false;
		return false;
	}
	ProjectTelemetrySequence = NextSequence;
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::AppendReadyFloorProjectTelemetry(
	const FReadyFloorRecord& Record,
	const FEFCalystoRoomManifestV6& RoomManifest)
{
	return AppendProjectTelemetry(FString::Printf(
		TEXT("event=FloorReady status=PASS floor=%lld serial=%lld styleId=%s "
			 "rooms=%d eligibleRooms=%d themedRooms=%d roomThemeIds=%s "
			 "actorDecisions=%d chestContentDecisions=%d actors=%d intent=%s "
			 "roomManifest=%s populationPlan=%s realizedManifest=%s"),
		static_cast<long long>(Record.FloorNumber),
		static_cast<long long>(Record.GenerationSerial),
		*Record.StyleId.ToString(),
		RoomManifest.Rooms.Num(),
		RoomManifest.EligibleRoomCount,
		RoomManifest.ThemedRoomCount,
		*EFCalystoV6PackagedSmokePrivate::JoinNames(Record.RoomThemeIds),
		Record.ActorDecisionCount,
		Record.ChestContentDecisionCount,
		Record.SpawnedActorCount,
		*Record.IntentHash,
		*Record.RoomManifestHash,
		*Record.PopulationPlanHash,
		*Record.RealizedManifestHash));
}

bool UEFCalystoPackagedSmokeSubsystem::RecordRuntimeReadinessTrace(
	UWorld* World,
	const int64 FloorNumber,
	const int64 GenerationSerial,
	const TArray<FName>& ReadinessTrace)
{
	if (bFinished || !IsValid(World) || !IsConfiguredDungeonWorld(World))
	{
		return false;
	}
	FString Error;
	if (!EFCalystoV6PackagedSmokePrivate::ValidateReadinessTrace(ReadinessTrace, Error))
	{
		AppendProjectTelemetry(FString::Printf(
			TEXT("event=ReadinessTraceRejected floor=%lld serial=%lld reason=%s"),
			static_cast<long long>(FloorNumber),
			static_cast<long long>(GenerationSerial),
			*Error));
		Finish(false, Error);
		return false;
	}

	const bool bEntryProbeTrace = !bSeededRunRequested;
	if (bEntryProbeTrace)
	{
		if (!bEntryDoorInteracted || FloorNumber != 1 || GenerationSerial != 0 ||
			EntryProbeReadinessTraceCount != 0)
		{
			Finish(false, TEXT("ENTRY_PROBE_READINESS_IDENTITY_INVALID"));
			return false;
		}
		++EntryProbeReadinessTraceCount;
	}
	else
	{
		if (FloorNumber != ExpectedFloor || GenerationSerial != FloorNumber - 1 ||
			ProjectTelemetryReadySequenceCount != CompletedFloorCount)
		{
			Finish(false, TEXT("SEEDED_READINESS_IDENTITY_INVALID"));
			return false;
		}
		++ProjectTelemetryReadySequenceCount;
	}

	const TCHAR* Phase = bEntryProbeTrace ? TEXT("EntryProbe") : TEXT("SeededRun");
	for (const FName Stage : ReadinessTrace)
	{
		if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=%s source=PCGRuntimeTrace phase=%s floor=%lld serial=%lld world=%s"),
			*Stage.ToString(),
			Phase,
			static_cast<long long>(FloorNumber),
			static_cast<long long>(GenerationSerial),
			*World->GetPathName())))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_READINESS_TRACE_WRITE_FAILED"));
			return false;
		}
	}
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::HandleBootstrapTick(float DeltaTime)
{
	(void)DeltaTime;
	if (bFinished)
	{
		BootstrapTickerHandle.Reset();
		return false;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World) || !World->IsGameWorld() || World->WorldType != EWorldType::Game ||
		!World->HasBegunPlay() || World->bIsTearingDown)
	{
		return true;
	}

	if (bEntryProbeAccepted && !bSeededRunRequested)
	{
		ExpectedFloor = 1;
		PreviousGenerationSerial = -1;
		bSeededRunRequested = true;
		if (!DungeonSubsystem || !DungeonSubsystem->RequestStartNewRunWithSeed(RunSeed))
		{
			bSeededRunRequested = false;
			Finish(false, TEXT("SEEDED_NEW_RUN_REJECTED"));
			BootstrapTickerHandle.Reset();
			return false;
		}
		if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=SeededRunRequested seed=%lld maxFloor=%d"),
			static_cast<long long>(RunSeed), MaximumFloor)))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_SEEDED_RUN_WRITE_FAILED"));
			BootstrapTickerHandle.Reset();
			return false;
		}
		BootstrapTickerHandle.Reset();
		return false;
	}

	if (bEntryDoorInteracted)
	{
		if (IsConfiguredDungeonWorld(World))
		{
			if (!bDungeonWorldObserved)
			{
				bDungeonWorldObserved = true;
				if (!AppendProjectTelemetry(FString::Printf(
					TEXT("event=DungeonWorldObserved source=DoorToLevel world=%s"),
					*World->GetPathName())))
				{
					Finish(false, TEXT("PROJECT_TELEMETRY_DUNGEON_WORLD_WRITE_FAILED"));
					return false;
				}
			}
			return true;
		}
		if (World->GetPathName() != EntranceWorldPath)
		{
			Finish(false, TEXT("DOOR_TO_LEVEL_OPENED_UNEXPECTED_WORLD"));
			return false;
		}
		if (FPlatformTime::Seconds() - EntranceDoorInteractionAtSeconds > 15.0)
		{
			Finish(false, TEXT("DOOR_TO_LEVEL_TRAVEL_TIMEOUT"));
			return false;
		}
		return true;
	}

	if (IsConfiguredDungeonWorld(World))
	{
		Finish(false, TEXT("DUNGEON_ENTRY_BYPASSED_DOOR_TO_LEVEL"));
		return false;
	}

	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem->GetSnapshot();
	if (!Snapshot.PolicyError.IsEmpty())
	{
		Finish(false, FString::Printf(TEXT("POLICY_INVALID:%s"), *Snapshot.PolicyError));
		return false;
	}
	if (!Snapshot.bPolicyValid)
	{
		return true;
	}

	const UEFCalystoDungeonHarnessSettings* Harness = UEFCalystoDungeonHarnessSettings::Get();
	const UEFCalystoDungeonDirectorPolicyV6Asset* Policy = Harness ? Harness->DirectorPolicy.Get() : nullptr;
	if (!Policy)
	{
		return true;
	}
	const FString GameplayHash = Policy->GetGameplayHash();
	const FString AuthoringHash = Policy->GetAuthoringHash();
	const FString MaterialHash = Policy->GetMaterialHash();
	const FString DecalHash = Policy->GetDecalHash();
	if (!Policy->ValidatePolicy() || Policy->GetPathName() != EFCalystoV6PackagedSmokePrivate::ExpectedPolicyPath ||
		Policy->GetClass()->GetPathName() != EFCalystoV6PackagedSmokePrivate::ExpectedPolicyClass ||
		!EFCalystoV6PackagedSmokePrivate::HasSha256(GameplayHash) ||
		!EFCalystoV6PackagedSmokePrivate::HasSha256(AuthoringHash) ||
		!EFCalystoV6PackagedSmokePrivate::HasSha256(MaterialHash) ||
		!EFCalystoV6PackagedSmokePrivate::HasSha256(DecalHash))
	{
		Finish(false, TEXT("V6_AUTHORITY_IDENTITY_INVALID"));
		return false;
	}

	if (!bAuthorityValidated)
	{
		if (DungeonSubsystem->HasActiveRun() || DungeonSubsystem->IsTravelRequestPending())
		{
			Finish(false, TEXT("ENTRANCE_REQUIRES_IDLE_DIRECTOR"));
			return false;
		}
		if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=Begin tag=%s configuration=%s scenario=%s forcedEdge=%d seed=%lld "
				 "maxFloor=%d timeout=%.1f world=%s"),
			*RunTag,
			*ConfigurationName,
			*Scenario.ToString(),
			ForcedDungeonEdge,
			static_cast<long long>(RunSeed),
			MaximumFloor,
			TimeoutSeconds,
			*World->GetPathName())) ||
			!AppendProjectTelemetry(FString::Printf(
				TEXT("event=Authority policyPath=%s policyClass=%s schema=6 generator=6 "
					 "gameplayHash=%s authoringHash=%s materialHash=%s decalHash=%s"),
				*Policy->GetPathName(),
				*Policy->GetClass()->GetPathName(),
				*GameplayHash,
				*AuthoringHash,
				*MaterialHash,
				*DecalHash)))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_BEGIN_WRITE_FAILED"));
			return false;
		}
		bAuthorityValidated = true;
	}

	FString DoorError;
	AActor* EntranceDoor = FindUniqueEntranceDoor(World, DoorError);
	if (!DoorError.IsEmpty())
	{
		Finish(false, DoorError);
		return false;
	}
	if (!EntranceDoor)
	{
		return true;
	}
	if (!TrySelectDoor(World, EntranceDoor, DoorError))
	{
		if (!DoorError.IsEmpty())
		{
			Finish(false, DoorError);
			return false;
		}
		return true;
	}

	if (!bEntryDoorSelected)
	{
		if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=DoorToLevelSelected class=%s world=%s"),
			*EntranceDoor->GetClass()->GetPathName(),
			*World->GetPathName())))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_ENTRY_DOOR_SELECTION_WRITE_FAILED"));
			return false;
		}
		bEntryDoorSelected = true;
		++ProjectTelemetryEntryDoorSelectedCount;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	UACFInteractionComponent* Interaction =
		PlayerPawn ? PlayerPawn->FindComponentByClass<UACFInteractionComponent>() : nullptr;
	if (!Interaction)
	{
		return true;
	}
	EntranceWorldPath = World->GetPathName();
	Interaction->Interact(TEXT("CalystoV6PackagedSmoke"));
	bEntryDoorInteracted = true;
	EntranceDoorInteractionAtSeconds = FPlatformTime::Seconds();
	++ProjectTelemetryEntryDoorInteractedCount;
	if (!AppendProjectTelemetry(FString::Printf(
		TEXT("event=DoorToLevelInteracted sourceWorld=%s"), *EntranceWorldPath)))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_ENTRY_DOOR_INTERACTION_WRITE_FAILED"));
		return false;
	}
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::IsConfiguredDungeonWorld(const UWorld* World) const
{
	const UEFCalystoDungeonHarnessSettings* Harness = UEFCalystoDungeonHarnessSettings::Get();
	if (!IsValid(World) || !Harness || Harness->DungeonMap.IsNull() || !World->GetPackage())
	{
		return false;
	}
	return World->GetPackage()->GetFName() ==
		FName(*Harness->DungeonMap.ToSoftObjectPath().GetLongPackageName());
}

AActor* UEFCalystoPackagedSmokeSubsystem::FindUniqueEntranceDoor(
	UWorld* World,
	FString& OutError) const
{
	OutError.Reset();
	AActor* Result = nullptr;
	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
			Actor->GetClass()->GetPathName() == EFCalystoV6PackagedSmokePrivate::EntranceDoorClass)
		{
			Result = Actor;
			++Count;
		}
	}
	if (Count > 1)
	{
		OutError = TEXT("DOOR_TO_LEVEL_CARDINALITY_INVALID");
		return nullptr;
	}
	return Result;
}

bool UEFCalystoPackagedSmokeSubsystem::TrySelectDoor(
	UWorld* World,
	AActor* Door,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(World) || !IsValid(Door))
	{
		OutError = TEXT("DOOR_SELECTION_INPUT_INVALID");
		return false;
	}
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	UACFInteractionComponent* Interaction =
		PlayerPawn ? PlayerPawn->FindComponentByClass<UACFInteractionComponent>() : nullptr;
	if (!PlayerPawn || !Interaction)
	{
		return false;
	}

	bool& bPositioned = Door->IsA<AEFCalystoFloorDoor>()
		? bFloorDoorPositioned
		: bEntryDoorPositioned;
	if (!bPositioned)
	{
		const FVector DoorLocation = Door->GetActorLocation();
		const FVector SelectionLocation(DoorLocation.X - 80.0, DoorLocation.Y - 80.0, DoorLocation.Z);
		PlayerPawn->SetActorLocation(
			SelectionLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Interaction->EnableDetection(false);
		Interaction->EnableDetection(true);
		bPositioned = true;
	}

	Interaction->RefreshInteractions();
	TArray<AActor*> OverlappingActors;
	Interaction->GetOverlappingActors(OverlappingActors);
	return Interaction->GetCurrentBestInteractableActor() == Door &&
		OverlappingActors.Contains(Door);
}

void UEFCalystoPackagedSmokeSubsystem::HandleFloorReady(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV6& Intent,
	const FEFCalystoRealizedFloorManifestV6& Manifest)
{
	if (bFinished)
	{
		return;
	}

	FString Error;
	if (!bSeededRunRequested)
	{
		// The generated floor can become ready before the quarter-second bootstrap
		// ticker observes the post-travel world. The ready callback itself is
		// authoritative evidence that DoorToLevel reached the configured map.
		UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		if (bEntryDoorInteracted && IsConfiguredDungeonWorld(World) && !bDungeonWorldObserved)
		{
			bDungeonWorldObserved = true;
			if (!AppendProjectTelemetry(FString::Printf(
				TEXT("event=DungeonWorldObserved source=DoorToLevel world=%s"),
				*World->GetPathName())))
			{
				Finish(false, TEXT("PROJECT_TELEMETRY_DUNGEON_WORLD_WRITE_FAILED"));
				return;
			}
		}
		if (!ValidateEntryProbe(FloorNumber, PCGSeed, Intent, Manifest, Error))
		{
			Finish(false, Error);
			return;
		}
		bEntryProbeAccepted = true;
		if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=DoorToLevelEntryProbeReady floor=%lld serial=%lld styleId=%s "
				 "intent=%s manifest=%s"),
			static_cast<long long>(FloorNumber),
			static_cast<long long>(Intent.GenerationContext.GenerationSerial),
			*Intent.StyleId.ToString(),
			*Intent.IntentHash,
			*Manifest.ManifestHash)))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_ENTRY_PROBE_WRITE_FAILED"));
		}
		return;
	}

	const FEFCalystoRoomManifestV6 RoomManifest = DungeonSubsystem->GetRoomManifestV6();
	const FEFCalystoPopulationPlanV6& PopulationPlan = DungeonSubsystem->GetPopulationPlanV6();
	if (!ValidateReadyFloor(
		FloorNumber, PCGSeed, Intent, Manifest, RoomManifest, PopulationPlan, Error))
	{
		Finish(false, Error);
		return;
	}

	FReadyFloorRecord& Record = ReadyFloorRecords.AddDefaulted_GetRef();
	Record.SchemaVersion = Intent.SchemaVersion;
	Record.GeneratorVersion = Intent.GeneratorVersion;
	Record.FloorNumber = FloorNumber;
	Record.GenerationSerial = Intent.GenerationContext.GenerationSerial;
	Record.PCGSeed = PCGSeed;
	Record.StyleId = Intent.StyleId;
	Record.DungeonSize = Intent.DungeonSize;
	Record.RoomCount = RoomManifest.Rooms.Num();
	Record.EligibleRoomCount = RoomManifest.EligibleRoomCount;
	Record.ThemedRoomCount = RoomManifest.ThemedRoomCount;
	TSet<FName> UniqueThemeIds;
	for (const FEFCalystoRoomContextV6& Room : RoomManifest.Rooms)
	{
		UniqueThemeIds.Add(Room.ThemeId);
	}
	for (const FName ThemeId : UniqueThemeIds)
	{
		Record.RoomThemeIds.Add(ThemeId);
	}
	Record.RoomThemeIds.Sort([](const FName A, const FName B)
	{
		return A.ToString() < B.ToString();
	});
	Record.CandidateAnchorCount = Manifest.CandidateAnchorCount;
	Record.ActorDecisionCount = PopulationPlan.ActorDecisionCount;
	Record.ChestContentDecisionCount = PopulationPlan.ChestContentDecisionCount;
	Record.EnemyCount = PopulationPlan.EnemyCount;
	Record.LooseFoodCount = PopulationPlan.LooseFoodCount;
	Record.ChestCount = PopulationPlan.ChestCount;
	Record.LootActorCount = PopulationPlan.LootActorCount;
	Record.SpecialEventCount = PopulationPlan.SpecialEventCount;
	Record.SpawnedActorCount = Manifest.SpawnedActorCount;
	Record.RealizedThreatCost = Manifest.RealizedThreatCost;
	Record.RealizedResourceCost = Manifest.RealizedResourceCost;
	Record.PolicyHash = Intent.GenerationContext.PolicyHash;
	Record.EcologyHash = Intent.EcologyHash;
	Record.IntentHash = Intent.IntentHash;
	Record.FloorPlanHash = Intent.FloorPlan.FloorPlanHash;
	Record.RoomManifestHash = RoomManifest.ManifestHash;
	Record.PopulationPlanHash = PopulationPlan.PopulationHash;
	Record.AnchorTopologyHash = Manifest.AnchorTopologyHash;
	Record.CompanionSnapshotHash = Manifest.CompanionSnapshotHash;
	Record.RealizedManifestHash = Manifest.ManifestHash;

	++CompletedFloorCount;
	PreviousGenerationSerial = Record.GenerationSerial;
	if (!AppendReadyFloorProjectTelemetry(Record, RoomManifest))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_FLOOR_READY_WRITE_FAILED"));
		return;
	}
	ScheduleFloorDoorInspection();
}

bool UEFCalystoPackagedSmokeSubsystem::ValidateEntryProbe(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV6& Intent,
	const FEFCalystoRealizedFloorManifestV6& Manifest,
	FString& OutError) const
{
	OutError.Reset();
	if (!bEntryDoorInteracted || !bDungeonWorldObserved || EntryProbeReadinessTraceCount != 1)
	{
		OutError = TEXT("DOOR_TO_LEVEL_ENTRY_EVIDENCE_INCOMPLETE");
		return false;
	}
	if (FloorNumber != 1 || Intent.GenerationContext.FloorNumber != 1 ||
		Intent.GenerationContext.GenerationSerial != 0 || Manifest.FloorNumber != 1 ||
		Manifest.GenerationSerial != 0 || PCGSeed != Intent.PCGSeed ||
		Intent.GenerationContext.RunSeed <= 0 ||
		Manifest.RunSeed != Intent.GenerationContext.RunSeed)
	{
		OutError = TEXT("DOOR_TO_LEVEL_ENTRY_IDENTITY_INVALID");
		return false;
	}
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(Intent, OutError) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(Manifest, OutError))
	{
		OutError = FString::Printf(TEXT("DOOR_TO_LEVEL_ENTRY_CONTRACT_INVALID:%s"), *OutError);
		return false;
	}
	if (Intent.IntentHash != FEFCalystoDungeonRuntimeMathV6::ComputeResolvedFloorIntentHash(Intent) ||
		Manifest.ManifestHash != FEFCalystoDungeonRuntimeMathV6::ComputeRealizedFloorManifestHash(Manifest) ||
		Intent.StyleId.IsNone() || Intent.StyleId != Manifest.StyleId ||
		Intent.IntentHash != Manifest.IntentHash ||
		Intent.FloorPlan.FloorPlanHash != Manifest.FloorPlanHash)
	{
		OutError = TEXT("DOOR_TO_LEVEL_ENTRY_MANIFEST_INVALID");
		return false;
	}
	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem->GetSnapshot();
	if (Snapshot.State != EEFCalystoDungeonTravelStateV6::Ready || !Snapshot.bDoorEnabled ||
		!Snapshot.bPCGComplete || !Snapshot.bRoomManifestReady || !Snapshot.bPopulationReady ||
		!Snapshot.bVisualsReady || !Snapshot.bNavigationPathReady ||
		Snapshot.RunSeed != Intent.GenerationContext.RunSeed || Snapshot.FloorNumber != 1 ||
		Snapshot.GenerationSerial != 0 || Snapshot.StyleId != Intent.StyleId ||
		Snapshot.FloorIntentHash != Intent.IntentHash ||
		Snapshot.PopulationManifestHash != Manifest.ManifestHash)
	{
		OutError = TEXT("DOOR_TO_LEVEL_ENTRY_NOT_READY");
		return false;
	}
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::ValidateReadyFloor(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV6& Intent,
	const FEFCalystoRealizedFloorManifestV6& Manifest,
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationPlanV6& PopulationPlan,
	FString& OutError) const
{
	using namespace EFCalystoV6PackagedSmokePrivate;
	OutError.Reset();
	if (ProjectTelemetryReadySequenceCount != CompletedFloorCount + 1)
	{
		OutError = TEXT("READY_CALLBACK_PRECEDED_BY_INVALID_TRACE_COUNT");
		return false;
	}
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(Intent, OutError) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(Manifest, OutError))
	{
		OutError = FString::Printf(TEXT("V6_RUNTIME_CONTRACT_INVALID:%s"), *OutError);
		return false;
	}
	if (!Intent.bIsValid || !Manifest.bIsValid ||
		Intent.SchemaVersion != EFCalystoDungeonRuntimeSchemaV6::SchemaVersion ||
		Intent.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion ||
		Manifest.SchemaVersion != EFCalystoDungeonRuntimeSchemaV6::SchemaVersion ||
		Manifest.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion)
	{
		OutError = TEXT("V6_SCHEMA_OR_VALIDITY_INVALID");
		return false;
	}
	const int64 Serial = Intent.GenerationContext.GenerationSerial;
	if (FloorNumber != ExpectedFloor || FloorNumber != Intent.GenerationContext.FloorNumber ||
		FloorNumber != Manifest.FloorNumber || Serial != FloorNumber - 1 ||
		Manifest.GenerationSerial != Serial || Serial <= PreviousGenerationSerial ||
		Intent.GenerationContext.RunSeed != RunSeed || Manifest.RunSeed != RunSeed ||
		PCGSeed != Intent.PCGSeed)
	{
		OutError = TEXT("SEEDED_FLOOR_IDENTITY_INVALID");
		return false;
	}
	const FName ExpectedScenario = Scenario == NaturalScenario ? NAME_None : Scenario;
	if (Intent.GenerationContext.DevelopmentForcedDungeonEdge != ForcedDungeonEdge ||
		Intent.GenerationContext.DevelopmentPopulationScenario != ExpectedScenario ||
		(ForcedDungeonEdge > 0 &&
		 (Intent.DungeonSize.X != ForcedDungeonEdge || Intent.DungeonSize.Y != ForcedDungeonEdge)))
	{
		OutError = TEXT("DEVELOPMENT_HOOK_IDENTITY_INVALID");
		return false;
	}

	const FName StyleId = Intent.StyleId;
	if (StyleId.IsNone() || Intent.FloorPlan.StyleId != StyleId ||
		RoomManifest.StyleId != StyleId || PopulationPlan.StyleId != StyleId ||
		Manifest.StyleId != StyleId)
	{
		OutError = TEXT("FLOOR_STYLE_ID_NOT_UNIQUE");
		return false;
	}
	if (Intent.FloorPlan.FloorSeed != RoomManifest.FloorSeed ||
		PopulationPlan.FloorSeed != RoomManifest.FloorSeed ||
		PopulationPlan.FloorNumber != FloorNumber ||
		Intent.FloorPlan.FloorPlanHash != RoomManifest.FloorPlanHash ||
		Intent.FloorPlan.FloorPlanHash != PopulationPlan.FloorPlanHash ||
		Intent.FloorPlan.FloorPlanHash != Manifest.FloorPlanHash ||
		RoomManifest.ManifestHash != PopulationPlan.RoomManifestHash ||
		RoomManifest.ManifestHash != Manifest.RoomManifestHash ||
		PopulationPlan.PopulationHash != Manifest.PopulationPlanHash ||
		Intent.IntentHash != Manifest.IntentHash ||
		Intent.CompanionRoster.SnapshotHash != Manifest.CompanionSnapshotHash)
	{
		OutError = TEXT("IMMUTABLE_MANIFEST_LINK_INVALID");
		return false;
	}
	if (Intent.IntentHash != FEFCalystoDungeonRuntimeMathV6::ComputeResolvedFloorIntentHash(Intent) ||
		Manifest.ManifestHash != FEFCalystoDungeonRuntimeMathV6::ComputeRealizedFloorManifestHash(Manifest))
	{
		OutError = TEXT("CANONICAL_RUNTIME_HASH_MISMATCH");
		return false;
	}
	const FString Hashes[] = {
		Intent.GenerationContext.PolicyHash,
		Intent.EcologyHash,
		Intent.IntentHash,
		Intent.FloorPlan.FloorPlanHash,
		RoomManifest.ManifestHash,
		PopulationPlan.PopulationHash,
		Manifest.AnchorTopologyHash,
		Manifest.CompanionSnapshotHash,
		Manifest.ManifestHash};
	for (const FString& Hash : Hashes)
	{
		if (!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Hash))
		{
			OutError = TEXT("NON_CANONICAL_RUNTIME_HASH");
			return false;
		}
	}

	if (Intent.FloorPlan.StyleMaterials.FloorMaterial.IsNull() ||
		Intent.FloorPlan.StyleMaterials.WallMaterial.IsNull() ||
		Intent.FloorPlan.StyleMaterials.RoofMaterial.IsNull() ||
		RoomManifest.Rooms.IsEmpty() || RoomManifest.Rooms.Num() > Intent.FloorPlan.MaximumRoomRecords)
	{
		OutError = TEXT("STYLE_MATERIAL_OR_ROOM_CARDINALITY_INVALID");
		return false;
	}
	TSet<FName> ReachableThemeIds;
	ReachableThemeIds.Add(NoThemeId);
	for (const FEFCalystoResolvedThemeProfileV6& Theme : Intent.FloorPlan.Themes)
	{
		if (Theme.ThemeId.IsNone() || Theme.ThemeId == NoThemeId || ReachableThemeIds.Contains(Theme.ThemeId))
		{
			OutError = TEXT("REACHABLE_THEME_ID_INVALID");
			return false;
		}
		ReachableThemeIds.Add(Theme.ThemeId);
	}

	TSet<int64> RoomIds;
	TMap<int64, FName> ThemeByRoom;
	int32 EligibleRoomCount = 0;
	int32 ThemedRoomCount = 0;
	for (const FEFCalystoRoomContextV6& Room : RoomManifest.Rooms)
	{
		const bool bProtected = FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room.RoomFlags);
		if (Room.StableRoomId == 0 || RoomIds.Contains(Room.StableRoomId) ||
			Room.StyleId != StyleId || !ReachableThemeIds.Contains(Room.ThemeId) ||
			!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Room.RoomContextHash) ||
			Room.EffectiveMaterials.FloorMaterial.IsNull() ||
			Room.EffectiveMaterials.WallMaterial.IsNull() ||
			Room.EffectiveMaterials.RoofMaterial.IsNull() ||
			(bProtected && Room.bIsThemed) ||
			(Room.bIsThemed && (Room.ThemeId.IsNone() || Room.ThemeId == NoThemeId)) ||
			(!Room.bIsThemed && Room.ThemeId != NoThemeId))
		{
			OutError = TEXT("ROOM_LOCAL_STYLE_THEME_OR_MATERIAL_INVALID");
			return false;
		}
		RoomIds.Add(Room.StableRoomId);
		ThemeByRoom.Add(Room.StableRoomId, Room.ThemeId);
		EligibleRoomCount += bProtected ? 0 : 1;
		ThemedRoomCount += Room.bIsThemed ? 1 : 0;
	}
	if (EligibleRoomCount != RoomManifest.EligibleRoomCount ||
		ThemedRoomCount != RoomManifest.ThemedRoomCount)
	{
		OutError = TEXT("ROOM_THEME_STATISTICS_INVALID");
		return false;
	}

	int32 ActorDecisionCount = 0;
	int32 ChestContentDecisionCount = 0;
	TSet<int64> PopulationRoomIds;
	for (const FEFCalystoRoomPopulationPlanV6& RoomPlan : PopulationPlan.Rooms)
	{
		const FName* RoomTheme = ThemeByRoom.Find(RoomPlan.StableRoomId);
		if (!RoomTheme || PopulationRoomIds.Contains(RoomPlan.StableRoomId) ||
			*RoomTheme != RoomPlan.ThemeId ||
			!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(RoomPlan.RoomPopulationHash) ||
			!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(RoomPlan.EffectiveCatalogHash))
		{
			OutError = TEXT("ROOM_POPULATION_PLAN_INVALID");
			return false;
		}
		PopulationRoomIds.Add(RoomPlan.StableRoomId);
		for (const FEFCalystoPopulationDecisionV6& Decision : RoomPlan.Decisions)
		{
			if (Decision.StableRoomId != RoomPlan.StableRoomId || Decision.StyleId != StyleId ||
				Decision.ThemeId != RoomPlan.ThemeId || Decision.CategoryId.IsNone() ||
				Decision.EntryId.IsNone() || Decision.ClassPath.IsNull() ||
				!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Decision.DecisionId))
			{
				OutError = TEXT("POPULATION_DECISION_INVALID");
				return false;
			}
			if (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor)
			{
				++ActorDecisionCount;
			}
			else
			{
				++ChestContentDecisionCount;
			}
		}
	}
	if (ActorDecisionCount != PopulationPlan.ActorDecisionCount ||
		ChestContentDecisionCount != PopulationPlan.ChestContentDecisionCount ||
		Manifest.SpawnedActorCount != PopulationPlan.ActorDecisionCount ||
		Manifest.Actors.Num() != Manifest.SpawnedActorCount)
	{
		OutError = TEXT("POPULATION_DECISION_OR_REALIZATION_COUNT_INVALID");
		return false;
	}

	TSet<FName> RealizedActorIds;
	for (const FEFCalystoRealizedPopulationActorRecordV6& Actor : Manifest.Actors)
	{
		if (Actor.StableActorId.IsNone() || RealizedActorIds.Contains(Actor.StableActorId) ||
			!RoomIds.Contains(Actor.StableRoomId) || Actor.CategoryId.IsNone() ||
			Actor.CatalogEntryId.IsNone() || Actor.ActorClass.IsNull())
		{
			OutError = TEXT("REALIZED_POPULATION_ACTOR_INVALID");
			return false;
		}
		RealizedActorIds.Add(Actor.StableActorId);
	}

	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem->GetSnapshot();
	if (Snapshot.State != EEFCalystoDungeonTravelStateV6::Ready || !Snapshot.bHasActiveRun ||
		!Snapshot.bPolicyValid || !Snapshot.PolicyError.IsEmpty() || !Snapshot.FailureReason.IsEmpty() ||
		!Snapshot.bPCGComplete || !Snapshot.bNavigationPathReady ||
		!Snapshot.bRoomManifestReady || !Snapshot.bPopulationReady ||
		!Snapshot.bVisualsReady || !Snapshot.bDoorEnabled ||
		Snapshot.RunSeed != RunSeed || Snapshot.FloorNumber != FloorNumber ||
		Snapshot.GenerationSerial != Serial || Snapshot.StyleId != StyleId ||
		Snapshot.PCGSeed != PCGSeed || Snapshot.FloorPlanHash != Intent.FloorPlan.FloorPlanHash ||
		Snapshot.FloorIntentHash != Intent.IntentHash ||
		Snapshot.RoomManifestHash != RoomManifest.ManifestHash ||
		Snapshot.PopulationManifestHash != Manifest.ManifestHash ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Snapshot.SnapshotHash))
	{
		OutError = TEXT("READY_SNAPSHOT_INVALID");
		return false;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsConfiguredDungeonWorld(World))
	{
		OutError = TEXT("READY_WORLD_NOT_CONFIGURED_DUNGEON");
		return false;
	}
	int32 LivePopulationActorCount = 0;
	int32 RemainingAnchorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		LivePopulationActorCount += Actor->ActorHasTag(PopulationActorTag) ? 1 : 0;
		RemainingAnchorCount += Actor->ActorHasTag(PopulationAnchorTag) ? 1 : 0;
	}
	if (LivePopulationActorCount != Manifest.SpawnedActorCount || RemainingAnchorCount != 0)
	{
		OutError = TEXT("LIVE_POPULATION_OR_ANCHOR_COUNT_INVALID");
		return false;
	}
	return true;
}

void UEFCalystoPackagedSmokeSubsystem::ScheduleFloorDoorInspection()
{
	CancelTicker(DoorSelectionTickerHandle);
	bFloorDoorPositioned = false;
	DoorSelectionStartedAtSeconds = FPlatformTime::Seconds();
	DoorInspectionNotBeforeSeconds = DoorSelectionStartedAtSeconds + 0.25;
	DoorSelectionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoPackagedSmokeSubsystem::HandleDoorSelectionTick),
		0.10f);
}

bool UEFCalystoPackagedSmokeSubsystem::HandleDoorSelectionTick(float DeltaTime)
{
	(void)DeltaTime;
	if (bFinished)
	{
		DoorSelectionTickerHandle.Reset();
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < DoorInspectionNotBeforeSeconds)
	{
		return true;
	}
	if (Now - DoorSelectionStartedAtSeconds > 15.0)
	{
		Finish(false, TEXT("FLOOR_DOOR_SELECTION_TIMEOUT"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}

	if (bCaptureVisual && ExpectedFloor >= MaximumFloor)
	{
		if (!bScreenshotRequested)
		{
			FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
			bScreenshotRequested = true;
			DoorInspectionNotBeforeSeconds = Now + 0.50;
			return true;
		}
		if (IFileManager::Get().FileSize(*ScreenshotPath) <= 1024)
		{
			return true;
		}
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsConfiguredDungeonWorld(World) || World->bIsTearingDown)
	{
		return true;
	}
	TArray<AEFCalystoFloorDoor*> Doors;
	for (TActorIterator<AEFCalystoFloorDoor> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			Doors.Add(*It);
		}
	}
	if (Doors.IsEmpty())
	{
		return true;
	}
	if (Doors.Num() != 1 || !Doors[0]->bIsEnabled)
	{
		Finish(false, Doors.Num() == 1
			? TEXT("FLOOR_DOOR_NOT_ENABLED")
			: TEXT("FLOOR_DOOR_CARDINALITY_INVALID"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}

	FString Error;
	if (!TrySelectDoor(World, Doors[0], Error))
	{
		if (!Error.IsEmpty())
		{
			Finish(false, Error);
			return false;
		}
		return true;
	}
	if (!AppendProjectTelemetry(FString::Printf(
		TEXT("event=FloorDoorSelected floor=%lld styleId=%s"),
		static_cast<long long>(ExpectedFloor),
		ReadyFloorRecords.IsEmpty() ? TEXT("NONE") : *ReadyFloorRecords.Last().StyleId.ToString())))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_FLOOR_DOOR_SELECTION_WRITE_FAILED"));
		return false;
	}
	++ProjectTelemetryFloorDoorSelectedCount;

	if (ExpectedFloor >= MaximumFloor)
	{
		Finish(true, TEXT("PASS"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	UACFInteractionComponent* Interaction =
		PlayerPawn ? PlayerPawn->FindComponentByClass<UACFInteractionComponent>() : nullptr;
	if (!Interaction)
	{
		Finish(false, TEXT("FLOOR_DOOR_INTERACTION_COMPONENT_MISSING"));
		return false;
	}
	const int64 SourceFloor = ExpectedFloor;
	ExpectedFloor = SourceFloor + 1;
	Interaction->Interact(TEXT("CalystoV6PackagedSmoke"));
	if (!DungeonSubsystem->IsTravelRequestPending())
	{
		ExpectedFloor = SourceFloor;
		Finish(false, TEXT("FLOOR_DOOR_DID_NOT_REQUEST_ADVANCE"));
		return false;
	}
	++FloorDoorInteractionCount;
	++ProjectTelemetryFloorDoorInteractedCount;
	if (!AppendProjectTelemetry(FString::Printf(
		TEXT("event=FloorDoorInteracted floor=%lld destination=%lld"),
		static_cast<long long>(SourceFloor),
		static_cast<long long>(ExpectedFloor))))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_FLOOR_DOOR_INTERACTION_WRITE_FAILED"));
		return false;
	}
	DoorSelectionTickerHandle.Reset();
	return false;
}

void UEFCalystoPackagedSmokeSubsystem::HandleFloorTravelFailed()
{
	if (bFinished)
	{
		return;
	}
	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem
		? DungeonSubsystem->GetSnapshot()
		: FEFCalystoDungeonSnapshotV6();
	const FString Detail = Snapshot.FailureReason.IsEmpty()
		? TEXT("UNKNOWN")
		: Snapshot.FailureReason;
	Finish(false, FString::Printf(TEXT("FLOOR_TRAVEL_FAILED:%s"), *Detail));
}

bool UEFCalystoPackagedSmokeSubsystem::HandleTimeoutTick(float DeltaTime)
{
	(void)DeltaTime;
	if (bFinished)
	{
		TimeoutTickerHandle.Reset();
		return false;
	}
	if (FPlatformTime::Seconds() - StartedAtSeconds >= TimeoutSeconds)
	{
		Finish(false, TEXT("PACKAGED_SMOKE_TIMEOUT"));
		TimeoutTickerHandle.Reset();
		return false;
	}
	return true;
}

void UEFCalystoPackagedSmokeSubsystem::Finish(const bool bSuccess, const FString& Reason)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	CancelTicker(BootstrapTickerHandle);
	CancelTicker(DoorSelectionTickerHandle);
	CancelTicker(TimeoutTickerHandle);
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartedAtSeconds;

	bool bContractSuccess = bSuccess && bAuthorityValidated && bEntryDoorSelected &&
		bEntryDoorInteracted && bDungeonWorldObserved && bEntryProbeAccepted &&
		bSeededRunRequested && EntryProbeReadinessTraceCount == 1 &&
		CompletedFloorCount == MaximumFloor && ReadyFloorRecords.Num() == MaximumFloor &&
		ProjectTelemetryReadySequenceCount == MaximumFloor &&
		FloorDoorInteractionCount == FMath::Max(0, MaximumFloor - 1) &&
		ProjectTelemetryEntryDoorSelectedCount == 1 &&
		ProjectTelemetryEntryDoorInteractedCount == 1 &&
		ProjectTelemetryFloorDoorSelectedCount == MaximumFloor &&
		ProjectTelemetryFloorDoorInteractedCount == FMath::Max(0, MaximumFloor - 1);
	FString EffectiveReason = Reason;
	if (bSuccess && !bContractSuccess)
	{
		EffectiveReason = TEXT("COMPLETION_INVARIANTS_INVALID");
	}
	if (!bContractSuccess)
	{
		if (AppendProjectTelemetry(FString::Printf(
			TEXT("event=Failure status=FAIL reason=%s floors=%d expectedFloor=%lld"),
			*EffectiveReason,
			CompletedFloorCount,
			static_cast<long long>(ExpectedFloor))))
		{
			++ProjectTelemetryFailureEventCount;
		}
	}
	const bool bCompletionWritten = AppendProjectTelemetry(FString::Printf(
		TEXT("event=Complete status=%s tag=%s seed=%lld floors=%d floorDoorInteractions=%d "
			 "elapsed=%.3f reason=%s"),
		bContractSuccess ? TEXT("PASS") : TEXT("FAIL"),
		*RunTag,
		static_cast<long long>(RunSeed),
		CompletedFloorCount,
		FloorDoorInteractionCount,
		ElapsedSeconds,
		*EffectiveReason));
	if (bCompletionWritten)
	{
		++ProjectTelemetryCompleteEventCount;
	}
	bFinalSuccess = bContractSuccess && bCompletionWritten && bProjectTelemetryHealthy;
	if (bContractSuccess && !bFinalSuccess)
	{
		EffectiveReason = TEXT("PROJECT_TELEMETRY_COMPLETION_WRITE_FAILED");
	}
	const bool bReceiptWritten = WriteReceipt(bFinalSuccess, EffectiveReason, ElapsedSeconds);
	const FString CompletionLine = FString::Printf(
		TEXT("CALYSTO_V6_PACKAGED_SMOKE_COMPLETE status=%s tag=%s seed=%lld floors=%d "
			 "entryDoor=true floorDoorInteractions=%d receipt=%s elapsed=%.3f reason=%s"),
		bFinalSuccess && bReceiptWritten ? TEXT("PASS") : TEXT("FAIL"),
		*RunTag,
		static_cast<long long>(RunSeed),
		CompletedFloorCount,
		FloorDoorInteractionCount,
		*ReceiptPath,
		ElapsedSeconds,
		*EffectiveReason);
	if (bFinalSuccess && bReceiptWritten)
	{
		UE_LOG(LogEFCalystoV6PackagedSmoke, Log, TEXT("%s"), *CompletionLine);
	}
	else
	{
		UE_LOG(LogEFCalystoV6PackagedSmoke, Error, TEXT("%s"), *CompletionLine);
	}
	ExitTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoPackagedSmokeSubsystem::HandleExitTick),
		0.75f);
}

bool UEFCalystoPackagedSmokeSubsystem::WriteReceipt(
	const bool bSuccess,
	const FString& Reason,
	const double ElapsedSeconds)
{
	using namespace EFCalystoV6PackagedSmokePrivate;
	if (ReceiptPath.IsEmpty())
	{
		if (!IsSafeToken(RunTag, 96))
		{
			RunTag = MakeRunTag();
		}
		const FString Directory = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("CalystoDungeonDirectorV6"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		ReceiptPath = FPaths::Combine(
			Directory,
			FString::Printf(TEXT("PackagedSmokeReceipt_Invalid_%s.json"), *RunTag));
	}

	const UEFCalystoDungeonHarnessSettings* Harness = UEFCalystoDungeonHarnessSettings::Get();
	const UEFCalystoDungeonDirectorPolicyV6Asset* Policy = Harness ? Harness->DirectorPolicy.Get() : nullptr;
	const FString GameplayHash = Policy ? Policy->GetGameplayHash() : FString();
	const FString AuthoringHash = Policy ? Policy->GetAuthoringHash() : FString();
	const FString MaterialHash = Policy ? Policy->GetMaterialHash() : FString();
	const FString DecalHash = Policy ? Policy->GetDecalHash() : FString();
	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem
		? DungeonSubsystem->GetSnapshot()
		: FEFCalystoDungeonSnapshotV6();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 6);
	Root->SetNumberField(TEXT("artifact_schema_version"), 1);
	Root->SetNumberField(TEXT("generator_version"), 6);
	Root->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("status"), bSuccess ? TEXT("PASS") : TEXT("FAIL"));
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetStringField(TEXT("configuration"), ConfigurationName);
	Root->SetStringField(TEXT("scenario"), Scenario.ToString());
	Root->SetNumberField(TEXT("forced_dungeon_edge"), ForcedDungeonEdge);
	Root->SetStringField(TEXT("run_tag"), RunTag);
	Root->SetStringField(TEXT("run_seed"), LexToString(RunSeed));
	Root->SetNumberField(TEXT("maximum_floor"), MaximumFloor);
	Root->SetNumberField(TEXT("completed_floor_count"), CompletedFloorCount);
	Root->SetNumberField(TEXT("floor_door_interaction_count"), FloorDoorInteractionCount);
	Root->SetNumberField(TEXT("elapsed_seconds"), ElapsedSeconds);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetBoolField(TEXT("unattended"), FApp::IsUnattended());
	Root->SetBoolField(TEXT("requires_cooked_data"), FPlatformProperties::RequiresCookedData());
	Root->SetStringField(TEXT("receipt_path"), ReceiptPath);
	Root->SetStringField(TEXT("project_telemetry_path"), ProjectTelemetryPath);
	Root->SetNumberField(TEXT("project_telemetry_schema_version"), ProjectTelemetrySchemaVersion);
	Root->SetStringField(TEXT("project_telemetry_sequence_count"), LexToString(ProjectTelemetrySequence));
	Root->SetStringField(TEXT("screenshot_path"), ScreenshotPath);
	Root->SetBoolField(TEXT("screenshot_requested"), bScreenshotRequested);

	Root->SetStringField(TEXT("policy_path"),
		Harness ? Harness->DirectorPolicy.ToSoftObjectPath().ToString() : FString());
	Root->SetStringField(TEXT("policy_class"),
		Policy ? Policy->GetClass()->GetPathName() : FString());
	Root->SetNumberField(TEXT("policy_schema_version"), Policy ? Policy->SchemaVersion : 0);
	Root->SetNumberField(TEXT("policy_generator_version"), Policy ? Policy->RuntimeGeneratorVersion : 0);
	Root->SetNumberField(TEXT("policy_hash_schema_version"),
		Policy ? UEFCalystoDungeonDirectorPolicyV6Asset::HashSchemaVersion : 0);
	Root->SetStringField(TEXT("policy_gameplay_hash"), GameplayHash);
	Root->SetStringField(TEXT("policy_authoring_hash"), AuthoringHash);
	Root->SetStringField(TEXT("policy_material_hash"), MaterialHash);
	Root->SetStringField(TEXT("policy_decal_hash"), DecalHash);

	TSharedRef<FJsonObject> Entrance = MakeShared<FJsonObject>();
	Entrance->SetStringField(TEXT("actor_class"), EntranceDoorClass);
	Entrance->SetStringField(TEXT("source_world"), EntranceWorldPath);
	Entrance->SetBoolField(TEXT("selected"), bEntryDoorSelected);
	Entrance->SetBoolField(TEXT("interacted"), bEntryDoorInteracted);
	Entrance->SetBoolField(TEXT("dungeon_world_observed"), bDungeonWorldObserved);
	Entrance->SetBoolField(TEXT("entry_probe_ready"), bEntryProbeAccepted);
	Entrance->SetNumberField(TEXT("entry_probe_readiness_trace_count"), EntryProbeReadinessTraceCount);
	Root->SetObjectField(TEXT("door_to_level"), Entrance);

	TArray<TSharedPtr<FJsonValue>> FloorValues;
	FloorValues.Reserve(ReadyFloorRecords.Num());
	for (const FReadyFloorRecord& Record : ReadyFloorRecords)
	{
		TSharedRef<FJsonObject> Floor = MakeShared<FJsonObject>();
		Floor->SetNumberField(TEXT("schema_version"), Record.SchemaVersion);
		Floor->SetNumberField(TEXT("generator_version"), Record.GeneratorVersion);
		Floor->SetStringField(TEXT("floor_number"), LexToString(Record.FloorNumber));
		Floor->SetStringField(TEXT("generation_serial"), LexToString(Record.GenerationSerial));
		Floor->SetNumberField(TEXT("pcg_seed"), Record.PCGSeed);
		Floor->SetStringField(TEXT("style_id"), Record.StyleId.ToString());
		Floor->SetNumberField(TEXT("size_x"), Record.DungeonSize.X);
		Floor->SetNumberField(TEXT("size_y"), Record.DungeonSize.Y);
		Floor->SetNumberField(TEXT("size_z"), Record.DungeonSize.Z);
		Floor->SetNumberField(TEXT("room_count"), Record.RoomCount);
		Floor->SetNumberField(TEXT("eligible_room_count"), Record.EligibleRoomCount);
		Floor->SetNumberField(TEXT("themed_room_count"), Record.ThemedRoomCount);
		TArray<TSharedPtr<FJsonValue>> ThemeValues;
		for (const FName ThemeId : Record.RoomThemeIds)
		{
			ThemeValues.Add(MakeShared<FJsonValueString>(ThemeId.ToString()));
		}
		Floor->SetArrayField(TEXT("room_theme_ids"), ThemeValues);
		Floor->SetNumberField(TEXT("candidate_anchor_count"), Record.CandidateAnchorCount);
		Floor->SetNumberField(TEXT("actor_decision_count"), Record.ActorDecisionCount);
		Floor->SetNumberField(TEXT("chest_content_decision_count"), Record.ChestContentDecisionCount);
		Floor->SetNumberField(TEXT("enemy_count"), Record.EnemyCount);
		Floor->SetNumberField(TEXT("loose_food_count"), Record.LooseFoodCount);
		Floor->SetNumberField(TEXT("chest_count"), Record.ChestCount);
		Floor->SetNumberField(TEXT("loot_actor_count"), Record.LootActorCount);
		Floor->SetNumberField(TEXT("special_event_count"), Record.SpecialEventCount);
		Floor->SetNumberField(TEXT("spawned_actor_count"), Record.SpawnedActorCount);
		Floor->SetNumberField(TEXT("realized_threat_cost"), Record.RealizedThreatCost);
		Floor->SetNumberField(TEXT("realized_resource_cost"), Record.RealizedResourceCost);
		Floor->SetStringField(TEXT("policy_hash"), Record.PolicyHash);
		Floor->SetStringField(TEXT("ecology_hash"), Record.EcologyHash);
		Floor->SetStringField(TEXT("intent_hash"), Record.IntentHash);
		Floor->SetStringField(TEXT("floor_plan_hash"), Record.FloorPlanHash);
		Floor->SetStringField(TEXT("room_manifest_hash"), Record.RoomManifestHash);
		Floor->SetStringField(TEXT("population_plan_hash"), Record.PopulationPlanHash);
		Floor->SetStringField(TEXT("anchor_topology_hash"), Record.AnchorTopologyHash);
		Floor->SetStringField(TEXT("companion_snapshot_hash"), Record.CompanionSnapshotHash);
		Floor->SetStringField(TEXT("realized_manifest_hash"), Record.RealizedManifestHash);
		FloorValues.Add(MakeShared<FJsonValueObject>(Floor));
	}
	Root->SetArrayField(TEXT("floors"), FloorValues);

	bool bAllFloorHashesCanonical = !ReadyFloorRecords.IsEmpty();
	bool bOneStylePerFloor = !ReadyFloorRecords.IsEmpty();
	bool bRoomLocalThemesValidated = !ReadyFloorRecords.IsEmpty();
	bool bActivePolicyMatches = !ReadyFloorRecords.IsEmpty();
	for (const FReadyFloorRecord& Record : ReadyFloorRecords)
	{
		bAllFloorHashesCanonical &= HasSha256(Record.PolicyHash) && HasSha256(Record.EcologyHash) &&
			HasSha256(Record.IntentHash) && HasSha256(Record.FloorPlanHash) &&
			HasSha256(Record.RoomManifestHash) && HasSha256(Record.PopulationPlanHash) &&
			HasSha256(Record.AnchorTopologyHash) && HasSha256(Record.CompanionSnapshotHash) &&
			HasSha256(Record.RealizedManifestHash);
		bOneStylePerFloor &= !Record.StyleId.IsNone();
		bRoomLocalThemesValidated &= !Record.RoomThemeIds.IsEmpty() &&
			Record.EligibleRoomCount >= Record.ThemedRoomCount;
		bActivePolicyMatches &= Record.PolicyHash == GameplayHash;
	}

	TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
	Checks->SetBoolField(TEXT("v6_authority_identity_exact"),
		Policy && Policy->ValidatePolicy() && Policy->GetPathName() == ExpectedPolicyPath &&
		Policy->GetClass()->GetPathName() == ExpectedPolicyClass &&
		HasSha256(GameplayHash) && HasSha256(AuthoringHash) &&
		HasSha256(MaterialHash) && HasSha256(DecalHash));
	Checks->SetBoolField(TEXT("active_policy_matches_authority"), bActivePolicyMatches);
	Checks->SetBoolField(TEXT("door_to_level_traversal_complete"),
		bEntryDoorSelected && bEntryDoorInteracted && bDungeonWorldObserved && bEntryProbeAccepted);
	Checks->SetBoolField(TEXT("all_requested_floors_ready"), CompletedFloorCount == MaximumFloor);
	Checks->SetBoolField(TEXT("one_style_id_per_floor"), bOneStylePerFloor);
	Checks->SetBoolField(TEXT("room_local_theme_ids_validated"), bRoomLocalThemesValidated);
	Checks->SetBoolField(TEXT("all_floor_hashes_canonical"), bAllFloorHashesCanonical);
	Checks->SetBoolField(TEXT("readiness_trace_exact"),
		EntryProbeReadinessTraceCount == 1 && ProjectTelemetryReadySequenceCount == MaximumFloor);
	Checks->SetBoolField(TEXT("floor_door_traversal_exact"),
		ProjectTelemetryFloorDoorSelectedCount == MaximumFloor &&
		ProjectTelemetryFloorDoorInteractedCount == FMath::Max(0, MaximumFloor - 1));
	Checks->SetBoolField(TEXT("project_telemetry_complete"),
		bProjectTelemetryInitialized && bProjectTelemetryHealthy &&
		ProjectTelemetryCompleteEventCount == 1 &&
		(bSuccess ? ProjectTelemetryFailureEventCount == 0 : ProjectTelemetryFailureEventCount == 1));
	Checks->SetBoolField(TEXT("final_snapshot_ready"),
		bSuccess && Snapshot.State == EEFCalystoDungeonTravelStateV6::Ready &&
		Snapshot.RunSeed == RunSeed && Snapshot.FloorNumber == MaximumFloor && Snapshot.bDoorEnabled);
	Checks->SetBoolField(TEXT("shipping_natural_only"),
		ConfigurationName != TEXT("Shipping") ||
		(Scenario == NaturalScenario && ForcedDungeonEdge == 0));
	Checks->SetBoolField(TEXT("visual_capture_complete"),
		!bCaptureVisual || (bScreenshotRequested && IFileManager::Get().FileSize(*ScreenshotPath) > 1024));
	Root->SetObjectField(TEXT("checks"), Checks);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	return FJsonSerializer::Serialize(Root, Writer) &&
		FFileHelper::SaveStringToFile(
			JsonText + LINE_TERMINATOR,
			*ReceiptPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UEFCalystoPackagedSmokeSubsystem::HandleExitTick(float DeltaTime)
{
	(void)DeltaTime;
	ExitTickerHandle.Reset();
	FPlatformMisc::RequestExit(false);
	return false;
}

void UEFCalystoPackagedSmokeSubsystem::CancelTicker(FTSTicker::FDelegateHandle& Handle)
{
	if (Handle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
		Handle.Reset();
	}
}
