#include "Calysto/EFCalystoPackagedSmokeSubsystem.h"

#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"
#include "Calysto/EFCalystoFloorDoor.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Components/ACFInteractionComponent.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
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
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPackagedSmoke, Log, All);

namespace EFCalystoPackagedSmokePrivate
{
	static const FName NaturalScenario(TEXT("Natural"));
	static const FName ZeroScenario(TEXT("Zero"));
	static const FName EnemyCap25Scenario(TEXT("EnemyCap25"));
	static const FName ResourceMaxScenario(TEXT("ResourceMax"));
	static const FName ResourceMinScenario(TEXT("ResourceMin"));
	static const FName NPCTotal4Scenario(TEXT("NPCTotal4"));
	static const FName SpecialEvents6Scenario(TEXT("SpecialEvents6"));
	static const FName PopulationTag(TEXT("EF.Calysto.Population.V4"));
	static constexpr int32 EnemyHardCap = 25;
	static constexpr int32 NPCHardCap = 4;
	static constexpr int32 FoodHardCap = 30;
	static constexpr int32 ChestHardCap = 10;
	static constexpr int32 LooseLootHardCap = 4;
	static constexpr int32 ClothingHardCap = 10;
	static constexpr int32 SpecialEventHardCap = 6;
	static constexpr int32 InitialActorHardCap = 89;

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

	static bool IsSafeRunTag(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 96 || !FChar::IsAlnum(Value[0]))
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

	static const TCHAR* DescribeStyle(const EEFCalystoStyleV4 Style)
	{
		switch (Style)
		{
		case EEFCalystoStyleV4::Standard: return TEXT("Standard");
		case EEFCalystoStyleV4::Compact: return TEXT("Compact");
		case EEFCalystoStyleV4::Branching: return TEXT("Branching");
		default: return TEXT("Invalid");
		}
	}

	static const TCHAR* DescribeTheme(const EEFCalystoThemeV4 Theme)
	{
		switch (Theme)
		{
		case EEFCalystoThemeV4::Default: return TEXT("Default");
		case EEFCalystoThemeV4::Forge: return TEXT("Forge");
		case EEFCalystoThemeV4::Shrine: return TEXT("Shrine");
		default: return TEXT("Invalid");
		}
	}
}

bool UEFCalystoPackagedSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_EDITOR
	(void)Outer;
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer)
		&& FPlatformProperties::RequiresCookedData()
		&& FApp::IsUnattended()
		&& FParse::Param(FCommandLine::Get(), TEXT("CalystoV4PackagedSmoke"));
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

	FString ConfigurationError;
	if (!ConfigureFromCommandLine(ConfigurationError))
	{
		Finish(false, ConfigurationError);
		return;
	}
	if (!InitializeProjectTelemetry(ConfigurationError))
	{
		Finish(false, ConfigurationError);
		return;
	}
	if (!DungeonSubsystem)
	{
		Finish(false, TEXT("DUNGEON_SUBSYSTEM_MISSING"));
		return;
	}

	DungeonSubsystem->OnFloorReady().AddUObject(this, &UEFCalystoPackagedSmokeSubsystem::HandleFloorReady);
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
	}
	DungeonSubsystem = nullptr;
	Super::Deinitialize();
}

bool UEFCalystoPackagedSmokeSubsystem::ConfigureFromCommandLine(FString& OutError)
{
	using namespace EFCalystoPackagedSmokePrivate;
	OutError.Reset();
#if UE_BUILD_SHIPPING
	ConfigurationName = TEXT("Shipping");
#else
	ConfigurationName = TEXT("Development");
#endif
	FString ParsedRunTag;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV4SmokeRunTag="), ParsedRunTag))
	{
		if (!IsSafeRunTag(ParsedRunTag))
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

	int64 ParsedSeed = RunSeed;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV4SmokeSeed="), OutError))
	{
		if (!ParseInt64Option(TEXT("CalystoV4SmokeSeed="), ParsedSeed) || ParsedSeed <= 0)
		{
			OutError = TEXT("INVALID_RUN_SEED");
			return false;
		}
		RunSeed = ParsedSeed;
	}

	int32 ParsedMaximumFloor = MaximumFloor;
	FString IgnoredMaximumFloor;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV4SmokeMaxFloor="), IgnoredMaximumFloor))
	{
		if (!ParseInt32Option(TEXT("CalystoV4SmokeMaxFloor="), ParsedMaximumFloor)
			|| ParsedMaximumFloor < 1 || ParsedMaximumFloor > 100)
		{
			OutError = TEXT("INVALID_MAXIMUM_FLOOR");
			return false;
		}
		MaximumFloor = ParsedMaximumFloor;
	}

	float ParsedTimeout = TimeoutSeconds;
	FString IgnoredTimeout;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV4SmokeTimeout="), IgnoredTimeout))
	{
		if (!ParseFloatOption(TEXT("CalystoV4SmokeTimeout="), ParsedTimeout)
			|| !FMath::IsFinite(ParsedTimeout) || ParsedTimeout < 60.0f || ParsedTimeout > 1800.0f)
		{
			OutError = TEXT("INVALID_TIMEOUT");
			return false;
		}
		TimeoutSeconds = ParsedTimeout;
	}

	FString ScenarioText;
	if (FParse::Value(FCommandLine::Get(), TEXT("CalystoV4SmokeScenario="), ScenarioText))
	{
		if (ScenarioText.Equals(TEXT("Natural"), ESearchCase::IgnoreCase))
		{
			Scenario = NaturalScenario;
		}
		else if (ScenarioText.Equals(TEXT("Zero"), ESearchCase::IgnoreCase))
		{
			Scenario = ZeroScenario;
		}
		else if (ScenarioText.Equals(TEXT("EnemyCap25"), ESearchCase::IgnoreCase))
		{
			Scenario = EnemyCap25Scenario;
		}
		else if (ScenarioText.Equals(TEXT("ResourceMax"), ESearchCase::IgnoreCase))
		{
			Scenario = ResourceMaxScenario;
		}
		else if (ScenarioText.Equals(TEXT("ResourceMin"), ESearchCase::IgnoreCase))
		{
			Scenario = ResourceMinScenario;
		}
		else if (ScenarioText.Equals(TEXT("NPCTotal4"), ESearchCase::IgnoreCase))
		{
			Scenario = NPCTotal4Scenario;
		}
		else if (ScenarioText.Equals(TEXT("SpecialEvents6"), ESearchCase::IgnoreCase))
		{
			Scenario = SpecialEvents6Scenario;
		}
		else
		{
			OutError = TEXT("UNKNOWN_POPULATION_SCENARIO");
			return false;
		}
	}

	bOutcomeTelemetryDisabled = FParse::Param(
		FCommandLine::Get(),
		TEXT("CalystoV4DisableOutcomeTelemetry"));
#if UE_BUILD_SHIPPING
	if (bOutcomeTelemetryDisabled)
	{
		OutError = TEXT("NEUTRAL_OUTCOME_FIXTURE_NOT_AVAILABLE_IN_SHIPPING");
		return false;
	}
#else
	if (bOutcomeTelemetryDisabled && Scenario != NaturalScenario)
	{
		OutError = TEXT("NEUTRAL_OUTCOME_FIXTURE_REQUIRES_NATURAL_SCENARIO");
		return false;
	}
#endif

	bCaptureVisual = FParse::Param(FCommandLine::Get(), TEXT("CalystoV4SmokeCapture"));
	if (bCaptureVisual)
	{
		const FString SafeScenario = Scenario.ToString().Replace(TEXT("/"), TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CalystoDungeonDirectorV4"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		ScreenshotPath = FPaths::Combine(
			Directory,
			FString::Printf(
				TEXT("PackagedSmokeVisual_%s_%s_%s.png"),
				*ConfigurationName,
				*SafeScenario,
				*RunTag));
	}
	const FString SafeScenario = Scenario.ToString().Replace(TEXT("/"), TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
	const FString EvidenceDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CalystoDungeonDirectorV4"));
	IFileManager::Get().MakeDirectory(*EvidenceDirectory, true);
	ReceiptPath = FPaths::Combine(
		EvidenceDirectory,
		FString::Printf(
			TEXT("PackagedSmokeReceipt_%s_%s_%s.json"),
			*ConfigurationName,
			*SafeScenario,
			*RunTag));
	ProjectTelemetryPath = FPaths::Combine(
		EvidenceDirectory,
		FString::Printf(
			TEXT("PackagedSmokeTelemetry_%s_%s_%s.log"),
			*ConfigurationName,
			*SafeScenario,
			*RunTag));
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
		TEXT("CALYSTO_V4_PROJECT_TELEMETRY schema=%d runTag=%s configuration=%s scenario=%s"),
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
	if (!bProjectTelemetryInitialized || !bProjectTelemetryHealthy
		|| ProjectTelemetryPath.IsEmpty() || EventPayload.IsEmpty())
	{
		bProjectTelemetryHealthy = false;
		return false;
	}
	const uint64 NextSequence = ProjectTelemetrySequence + 1;
	const FString Line = FString::Printf(
		TEXT("CALYSTO_V4_PROJECT_TELEMETRY schema=%d sequence=%llu %s"),
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
	const int64 FloorNumber,
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FEFCalystoRealizedFloorManifestV4& Manifest)
{
	const FString Identity = FString::Printf(
		TEXT("floor=%lld serial=%lld intent=%s manifest=%s"),
		static_cast<long long>(FloorNumber),
		static_cast<long long>(Intent.GenerationSerial),
		*Intent.IntentHash,
		*Manifest.ManifestHash);
	return AppendProjectTelemetry(FString::Printf(
			TEXT("event=FloorReady status=PASS tag=%s %s enemies=%d npcs=%d food=%d chests=%d loose=%d clothing=%d special=%d actors=%d"),
			*RunTag,
			*Identity,
			Manifest.EnemyCount,
			Manifest.NPCCount,
			Manifest.FoodCount,
			Manifest.ChestCount,
			Manifest.LooseLootCount,
			Manifest.ClothingCount,
			Manifest.SpecialEventCount,
			Manifest.SpawnedActorCount));
}

bool UEFCalystoPackagedSmokeSubsystem::RecordRuntimeReadinessTrace(
	UWorld* World,
	const int64 FloorNumber,
	const int64 GenerationSerial,
	const TArray<FName>& ReadinessTrace)
{
	static const FName ExpectedTrace[] =
	{
		TEXT("GenerateLocal"),
		TEXT("PCGComplete"),
		TEXT("NavigationPathReady"),
		TEXT("EnemyLevelsReady"),
		TEXT("PopulationRealized"),
		TEXT("CompanionRosterReady"),
		TEXT("DoorEnabled")
	};
	if (bFinished || !IsValid(World) || FloorNumber != ExpectedFloor
		|| GenerationSerial != FloorNumber || ReadinessTrace.Num() != UE_ARRAY_COUNT(ExpectedTrace))
	{
		AppendProjectTelemetry(FString::Printf(
			TEXT("event=ReadinessTraceRejected floor=%lld serial=%lld entries=%d reason=identity_or_cardinality"),
			static_cast<long long>(FloorNumber),
			static_cast<long long>(GenerationSerial),
			ReadinessTrace.Num()));
		Finish(false, TEXT("PROJECT_TELEMETRY_READINESS_TRACE_INVALID"));
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedTrace); ++Index)
	{
		if (ReadinessTrace[Index] != ExpectedTrace[Index])
		{
			AppendProjectTelemetry(FString::Printf(
				TEXT("event=ReadinessTraceRejected floor=%lld serial=%lld index=%d expected=%s observed=%s reason=order"),
				static_cast<long long>(FloorNumber),
				static_cast<long long>(GenerationSerial),
				Index,
				*ExpectedTrace[Index].ToString(),
				*ReadinessTrace[Index].ToString()));
			Finish(false, TEXT("PROJECT_TELEMETRY_READINESS_TRACE_ORDER_INVALID"));
			return false;
		}
	}
	for (const FName& Stage : ReadinessTrace)
	{
		if (!AppendProjectTelemetry(FString::Printf(
				TEXT("event=%s source=PCGRuntimeTrace floor=%lld serial=%lld world=%s"),
				*Stage.ToString(),
				static_cast<long long>(FloorNumber),
				static_cast<long long>(GenerationSerial),
				*World->GetName())))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_READINESS_TRACE_WRITE_FAILED"));
			return false;
		}
	}
	++ProjectTelemetryReadySequenceCount;
	return true;
}

bool UEFCalystoPackagedSmokeSubsystem::HandleBootstrapTick(float DeltaTime)
{
	(void)DeltaTime;
	if (bFinished || bBootstrapDispatched)
	{
		BootstrapTickerHandle.Reset();
		return false;
	}
	if (FPlatformTime::Seconds() - StartedAtSeconds < 1.0)
	{
		return true;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World) || !World->IsGameWorld() || World->WorldType != EWorldType::Game
		|| !World->HasBegunPlay())
	{
		return true;
	}
	// ACF's cooked Shipping frontend can normalize the process' explicit startup
	// URL back to MenuMap and possess DefaultPawn.  The production Director
	// intentionally refuses travel without the authoritative Player equipment
	// component, so this explicit smoke-only driver materializes the same project
	// Player class before requesting the run.  Normal Shipping launches never
	// create this subsystem because -CalystoV4PackagedSmoke is absent.
	APawn* BootstrapPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const FString BootstrapPawnClassPath = BootstrapPawn
		? BootstrapPawn->GetClass()->GetPathName()
		: FString();
	const bool bHasProjectGameplayPawn =
		BootstrapPawnClassPath.StartsWith(TEXT("/Game/FullSample/Player.Player_C"));
	if (!bHasProjectGameplayPawn)
	{
		if (!bBootstrapGameplayPawnRequested)
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			UClass* PlayerClass = StaticLoadClass(
				APawn::StaticClass(),
				nullptr,
				TEXT("/Game/FullSample/Player.Player_C"));
			if (!Controller || !PlayerClass)
			{
				Finish(false, TEXT("PACKAGED_SMOKE_PLAYER_BOOTSTRAP_UNAVAILABLE"));
				BootstrapTickerHandle.Reset();
				return false;
			}
			const FTransform SpawnTransform = BootstrapPawn
				? BootstrapPawn->GetActorTransform()
				: FTransform::Identity;
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = Controller;
			SpawnParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APawn* ProjectPawn = World->SpawnActor<APawn>(
				PlayerClass,
				SpawnTransform,
				SpawnParameters);
			if (!ProjectPawn)
			{
				Finish(false, TEXT("PACKAGED_SMOKE_PLAYER_BOOTSTRAP_SPAWN_FAILED"));
				BootstrapTickerHandle.Reset();
				return false;
			}
			Controller->Possess(ProjectPawn);
			if (BootstrapPawn && BootstrapPawn != ProjectPawn)
			{
				BootstrapPawn->Destroy();
			}
			bBootstrapGameplayPawnRequested = true;
			BootstrapGameplayPawnReadyNotBeforeSeconds = FPlatformTime::Seconds() + 1.0;
		}
		return true;
	}
	if (bBootstrapGameplayPawnRequested
		&& FPlatformTime::Seconds() < BootstrapGameplayPawnReadyNotBeforeSeconds)
	{
		return true;
	}
	const FEFCalystoDungeonSnapshotV4 Snapshot = DungeonSubsystem->GetSnapshot();
	if (!Snapshot.bPolicyValid || !Snapshot.PolicyError.IsEmpty())
	{
		Finish(false, FString::Printf(TEXT("POLICY_INVALID:%s"), *Snapshot.PolicyError));
		BootstrapTickerHandle.Reset();
		return false;
	}
	const UEFCalystoDungeonHarnessSettings* HarnessSettings = UEFCalystoDungeonHarnessSettings::Get();
	const UEFCalystoDungeonDirectorPolicyV4* AuthorityPolicy = HarnessSettings
		? HarnessSettings->DirectorPolicy.Get()
		: nullptr;
	static const FString ExpectedPolicyPath =
		TEXT("/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy");
	static const FString ExpectedPolicyClass =
		TEXT("/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4");
	if (!AuthorityPolicy || AuthorityPolicy->SchemaVersion != 4 || AuthorityPolicy->GeneratorVersion != 4
		|| AuthorityPolicy->GetPathName() != ExpectedPolicyPath
		|| AuthorityPolicy->GetClass()->GetPathName() != ExpectedPolicyClass
		|| !EFCalystoPackagedSmokePrivate::HasSha256(Snapshot.PolicyHash))
	{
		Finish(false, TEXT("V4_AUTHORITY_IDENTITY_INVALID"));
		BootstrapTickerHandle.Reset();
		return false;
	}
	FString EffectivePolicyHash = Snapshot.PolicyHash;
	if (DungeonSubsystem->HasActiveRun() || DungeonSubsystem->IsTravelRequestPending())
	{
		Finish(false, TEXT("BOOTSTRAP_REQUIRES_IDLE_GAME_INSTANCE"));
		BootstrapTickerHandle.Reset();
		return false;
	}
	if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=Begin tag=%s configuration=%s scenario=%s seed=%lld maxFloor=%d timeout=%.1f outcomeMode=%s world=%s pawnClass=%s"),
			*RunTag,
			*ConfigurationName,
			*Scenario.ToString(),
			static_cast<long long>(RunSeed),
			MaximumFloor,
			TimeoutSeconds,
			bOutcomeTelemetryDisabled ? TEXT("neutral_missing_telemetry") : TEXT("live"),
			*World->GetPathName(),
			BootstrapPawn ? *BootstrapPawn->GetClass()->GetPathName() : TEXT("NONE"))))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_BEGIN_WRITE_FAILED"));
		BootstrapTickerHandle.Reset();
		return false;
	}

#if UE_BUILD_SHIPPING
	const bool bRejectShippingOverride =
		Scenario != EFCalystoPackagedSmokePrivate::NaturalScenario;
#else
	const bool bRejectShippingOverride = false;
	if (Scenario != EFCalystoPackagedSmokePrivate::NaturalScenario)
	{
		FString ScenarioPolicyHash;
		FString ScenarioError;
		if (!DungeonSubsystem->SetDevelopmentPopulationScenarioForAutomation(
				Scenario, ScenarioPolicyHash, ScenarioError, true)
			|| !DungeonSubsystem->SetDevelopmentForcedDungeonEdgeForAutomation(30, true))
		{
			Finish(false, FString::Printf(TEXT("DEVELOPMENT_SCENARIO_REJECTED:%s"), *ScenarioError));
			BootstrapTickerHandle.Reset();
			return false;
		}
		UE_LOG(
			LogEFCalystoPackagedSmoke,
			Log,
			TEXT("CALYSTO_PACKAGED_SMOKE_SCENARIO scenario=%s edge=30 transient=true policy=%s"),
			*Scenario.ToString(),
			*ScenarioPolicyHash);
		EffectivePolicyHash = ScenarioPolicyHash;
		if (!AppendProjectTelemetry(FString::Printf(
				TEXT("event=DevelopmentScenarioConfigured scenario=%s edge=30 policy=%s"),
				*Scenario.ToString(),
				*ScenarioPolicyHash)))
		{
			Finish(false, TEXT("PROJECT_TELEMETRY_SCENARIO_WRITE_FAILED"));
			BootstrapTickerHandle.Reset();
			return false;
		}
	}
#endif
	if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=Authority policyPath=%s policyClass=%s schema=4 generator=4 policyHash=%s legacyAuthorityLoaded=false"),
			*ExpectedPolicyPath,
			*ExpectedPolicyClass,
			*EffectivePolicyHash)))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_AUTHORITY_WRITE_FAILED"));
		BootstrapTickerHandle.Reset();
		return false;
	}
	if (bRejectShippingOverride)
	{
		Finish(false, TEXT("SHIPPING_OVERRIDE_REJECTED"));
		BootstrapTickerHandle.Reset();
		return false;
	}

	bBootstrapDispatched = true;
	UE_LOG(
		LogEFCalystoPackagedSmoke,
		Log,
		TEXT("CALYSTO_PACKAGED_SMOKE_BEGIN tag=%s configuration=%s scenario=%s seed=%lld maxFloor=%d timeout=%.1f outcomeMode=%s"),
		*RunTag,
		*ConfigurationName,
		*Scenario.ToString(),
		static_cast<long long>(RunSeed),
		MaximumFloor,
		TimeoutSeconds,
		bOutcomeTelemetryDisabled ? TEXT("neutral_missing_telemetry") : TEXT("live"));
	if (!DungeonSubsystem->RequestStartNewRunWithSeed(RunSeed))
	{
		Finish(false, TEXT("NEW_RUN_REJECTED"));
	}
	BootstrapTickerHandle.Reset();
	return false;
}

void UEFCalystoPackagedSmokeSubsystem::HandleFloorReady(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FEFCalystoRealizedFloorManifestV4& Manifest)
{
	if (bFinished)
	{
		return;
	}
	FString ValidationError;
	if (!ValidateReadyFloor(FloorNumber, PCGSeed, Intent, Manifest, ValidationError))
	{
		Finish(false, ValidationError);
		return;
	}

	++CompletedFloorCount;
	PreviousGenerationSerial = Intent.GenerationSerial;
	FReadyFloorRecord& FloorRecord = ReadyFloorRecords.AddDefaulted_GetRef();
	FloorRecord.GeneratorVersion = Intent.GeneratorVersion;
	FloorRecord.FloorNumber = FloorNumber;
	FloorRecord.GenerationSerial = Intent.GenerationSerial;
	FloorRecord.PCGSeed = PCGSeed;
	FloorRecord.Style = Intent.Style;
	FloorRecord.Theme = Intent.Theme;
	FloorRecord.DungeonSize = Intent.DungeonSize;
	FloorRecord.CandidateAnchorCount = Manifest.CandidateAnchorCount;
	FloorRecord.EnemyCount = Manifest.EnemyCount;
	FloorRecord.NPCCount = Manifest.NPCCount;
	FloorRecord.FoodCount = Manifest.FoodCount;
	FloorRecord.ChestCount = Manifest.ChestCount;
	FloorRecord.LooseLootCount = Manifest.LooseLootCount;
	FloorRecord.ClothingCount = Manifest.ClothingCount;
	FloorRecord.SpecialEventCount = Manifest.SpecialEventCount;
	FloorRecord.SpawnedActorCount = Manifest.SpawnedActorCount;
	FloorRecord.RealizedThreatCost = Manifest.RealizedThreatCost;
	FloorRecord.RealizedResourceCost = Manifest.RealizedResourceCost;
	FloorRecord.PolicyHash = Intent.PolicyHash;
	FloorRecord.EcologyHash = Intent.EcologyHash;
	FloorRecord.OutcomeHash = Intent.OutcomeHash;
	FloorRecord.bHasFrozenOutcome = Intent.bHasFrozenOutcome;
	FloorRecord.FrozenOutcome = Intent.FrozenOutcome;
	FloorRecord.IntentHash = Intent.IntentHash;
	FloorRecord.AnchorTopologyHash = Manifest.AnchorTopologyHash;
	FloorRecord.PopulationHash = Manifest.PopulationHash;
	FloorRecord.ResourceHash = Manifest.ResourceHash;
	FloorRecord.CompanionSnapshotHash = Manifest.CompanionSnapshotHash;
	FloorRecord.ManifestHash = Manifest.ManifestHash;
	if (!AppendReadyFloorProjectTelemetry(FloorNumber, Intent, Manifest))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_READY_SEQUENCE_WRITE_FAILED"));
		return;
	}
	UE_LOG(
		LogEFCalystoPackagedSmoke,
		Log,
		TEXT("CALYSTO_PACKAGED_SMOKE_FLOOR tag=%s status=PASS configuration=%s scenario=%s generator=%d floor=%lld serial=%lld pcgSeed=%d style=%s theme=%s size=%dx%dx%d anchors=%d enemies=%d npcs=%d food=%d chests=%d loose=%d clothing=%d special=%d actors=%d threat=%.3f resources=%.3f intent=%s companion=%s manifest=%s"),
		*RunTag,
		*ConfigurationName,
		*Scenario.ToString(),
		Intent.GeneratorVersion,
		static_cast<long long>(FloorNumber),
		static_cast<long long>(Intent.GenerationSerial),
		PCGSeed,
		EFCalystoPackagedSmokePrivate::DescribeStyle(Intent.Style),
		EFCalystoPackagedSmokePrivate::DescribeTheme(Intent.Theme),
		Intent.DungeonSize.X,
		Intent.DungeonSize.Y,
		Intent.DungeonSize.Z,
		Manifest.CandidateAnchorCount,
		Manifest.EnemyCount,
		Manifest.NPCCount,
		Manifest.FoodCount,
		Manifest.ChestCount,
		Manifest.LooseLootCount,
		Manifest.ClothingCount,
		Manifest.SpecialEventCount,
		Manifest.SpawnedActorCount,
		Manifest.RealizedThreatCost,
		Manifest.RealizedResourceCost,
		*Intent.IntentHash,
		*Manifest.CompanionSnapshotHash,
		*Manifest.ManifestHash);

	DoorSelectionStartedAtSeconds = FPlatformTime::Seconds();
	// Floor 1 can still be covered by the startup movie. Capture the final
	// realized floor only after a rendered settle window, then wait for the PNG
	// before allowing the packaged process to finish.
	DoorInspectionNotBeforeSeconds = DoorSelectionStartedAtSeconds
		+ (bCaptureVisual && FloorNumber == MaximumFloor ? 3.0 : 0.25);
	bDoorPositioned = false;
	CancelTicker(DoorSelectionTickerHandle);
	DoorSelectionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoPackagedSmokeSubsystem::HandleDoorSelectionTick),
		0.10f);
}

bool UEFCalystoPackagedSmokeSubsystem::ValidateReadyFloor(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FEFCalystoRealizedFloorManifestV4& Manifest,
	FString& OutError) const
{
	using namespace EFCalystoPackagedSmokePrivate;
	OutError.Reset();
	if (FloorNumber != ExpectedFloor || FloorNumber < 1 || FloorNumber > MaximumFloor
		|| PCGSeed <= 0 || !Intent.bIsValid || !Manifest.bIsValid
		|| Intent.GeneratorVersion != 4
		|| Intent.RunSeed != RunSeed || Manifest.RunSeed != RunSeed
		|| Intent.FloorNumber != FloorNumber || Manifest.FloorNumber != FloorNumber
		|| Intent.GenerationSerial != Manifest.GenerationSerial || Intent.PCGSeed != PCGSeed
		|| Intent.IntentHash != Manifest.IntentHash)
	{
		OutError = TEXT("FLOOR_IDENTITY_MISMATCH");
		return false;
	}
	if (Intent.CompanionSnapshotHash != Manifest.CompanionSnapshotHash)
	{
		OutError = TEXT("COMPANION_SNAPSHOT_HASH_MISMATCH");
		return false;
	}
	if ((Intent.Style != EEFCalystoStyleV4::Standard
			&& Intent.Style != EEFCalystoStyleV4::Compact
			&& Intent.Style != EEFCalystoStyleV4::Branching)
		|| (Intent.Theme != EEFCalystoThemeV4::Default
			&& Intent.Theme != EEFCalystoThemeV4::Forge
			&& Intent.Theme != EEFCalystoThemeV4::Shrine))
	{
		OutError = TEXT("STYLE_OR_THEME_INVALID");
		return false;
	}
	if ((FloorNumber == 1 && Intent.GenerationSerial != 1)
		|| (FloorNumber > 1 && Intent.GenerationSerial != PreviousGenerationSerial + 1))
	{
		OutError = TEXT("GENERATION_SERIAL_SEQUENCE_MISMATCH");
		return false;
	}
	if (Intent.DungeonSize.X < 26 || Intent.DungeonSize.X > 30
		|| Intent.DungeonSize.Y != Intent.DungeonSize.X || Intent.DungeonSize.Z != 1)
	{
		OutError = TEXT("DUNGEON_SIZE_OUTSIDE_CERTIFIED_SET");
		return false;
	}
#if !UE_BUILD_SHIPPING
	if (Scenario != NaturalScenario
		&& (Intent.DevelopmentPopulationScenario != Scenario
			|| Intent.DevelopmentForcedDungeonEdge != 30
			|| Intent.DungeonSize.X != 30))
	{
		OutError = TEXT("DEVELOPMENT_SCENARIO_INTENT_MISMATCH");
		return false;
	}
#endif
	if (Manifest.EnemyCount < 0 || Manifest.EnemyCount > EnemyHardCap
		|| Manifest.NPCCount < 0 || Manifest.NPCCount > NPCHardCap
		|| Manifest.FoodCount < 0 || Manifest.FoodCount > FoodHardCap
		|| Manifest.ChestCount < 0 || Manifest.ChestCount > ChestHardCap
		|| Manifest.LooseLootCount < 0 || Manifest.LooseLootCount > LooseLootHardCap
		|| Manifest.ClothingCount < 0 || Manifest.ClothingCount > ClothingHardCap
		|| Manifest.SpecialEventCount < 0 || Manifest.SpecialEventCount > SpecialEventHardCap
		|| Manifest.SpawnedActorCount < 0 || Manifest.SpawnedActorCount > InitialActorHardCap
		|| Manifest.SpawnedActorCount != Manifest.EnemyCount + Manifest.NPCCount
			+ Manifest.FoodCount + Manifest.ChestCount + Manifest.LooseLootCount
			+ Manifest.ClothingCount + Manifest.SpecialEventCount)
	{
		OutError = TEXT("REALIZED_COUNT_CAP_OR_SUM_MISMATCH");
		return false;
	}
	const FEFCalystoDungeonSnapshotV4 Snapshot = DungeonSubsystem->GetSnapshot();
	if (Snapshot.State != EEFCalystoDungeonRunStateV4::Ready
		|| !Snapshot.bHasActiveRun || !Snapshot.bDoorReady || !Snapshot.bCompanionReady
		|| Snapshot.FloorNumber != FloorNumber
		|| Snapshot.GenerationSerial != Intent.GenerationSerial
		|| Snapshot.IntentHash != Intent.IntentHash
		|| Snapshot.ManifestHash != Manifest.ManifestHash
		|| Snapshot.CompanionSnapshotHash != Manifest.CompanionSnapshotHash)
	{
		OutError = TEXT("PUBLIC_V4_READY_SNAPSHOT_MISMATCH");
		return false;
	}
	if (Manifest.Instances.Num() != Manifest.SpawnedActorCount
		|| Intent.SpawnDirectives.Num() != Manifest.SpawnedActorCount)
	{
		OutError = TEXT("DIRECTIVE_OR_INSTANCE_CARDINALITY_MISMATCH");
		return false;
	}
	int32 InstanceEnemyCount = 0;
	int32 InstanceNPCCount = 0;
	int32 InstanceFoodCount = 0;
	int32 InstanceChestCount = 0;
	int32 InstanceLooseLootCount = 0;
	int32 InstanceClothingCount = 0;
	int32 InstanceSpecialEventCount = 0;
	for (const FEFCalystoRealizedInstanceV4& Instance : Manifest.Instances)
	{
		if (Instance.StableInstanceId.IsNone() || Instance.CatalogId.IsNone()
			|| Instance.ActorClass.IsNull())
		{
			OutError = TEXT("REALIZED_INSTANCE_IDENTITY_INVALID");
			return false;
		}
		switch (Instance.Category)
		{
		case EEFCalystoContentCategoryV4::Enemy: ++InstanceEnemyCount; break;
		case EEFCalystoContentCategoryV4::NPC: ++InstanceNPCCount; break;
		case EEFCalystoContentCategoryV4::Food: ++InstanceFoodCount; break;
		case EEFCalystoContentCategoryV4::Chest: ++InstanceChestCount; break;
		case EEFCalystoContentCategoryV4::LooseLoot: ++InstanceLooseLootCount; break;
		case EEFCalystoContentCategoryV4::Clothing: ++InstanceClothingCount; break;
		case EEFCalystoContentCategoryV4::SpecialEvent: ++InstanceSpecialEventCount; break;
		default:
			OutError = TEXT("DIRECTOR_MATERIALIZED_NON_GAMEPLAY_CATEGORY");
			return false;
		}
	}
	if (InstanceEnemyCount != Manifest.EnemyCount
		|| InstanceNPCCount != Manifest.NPCCount
		|| InstanceFoodCount != Manifest.FoodCount
		|| InstanceChestCount != Manifest.ChestCount
		|| InstanceLooseLootCount != Manifest.LooseLootCount
		|| InstanceClothingCount != Manifest.ClothingCount
		|| InstanceSpecialEventCount != Manifest.SpecialEventCount)
	{
		OutError = TEXT("REALIZED_INSTANCE_CATEGORY_COUNT_MISMATCH");
		return false;
	}
	if (!FMath::IsFinite(Manifest.RealizedThreatCost) || Manifest.RealizedThreatCost < 0.0f
		|| !FMath::IsFinite(Manifest.RealizedResourceCost) || Manifest.RealizedResourceCost < 0.0f)
	{
		OutError = TEXT("REALIZED_COST_INVALID");
		return false;
	}
	if (!HasSha256(Intent.PolicyHash) || !HasSha256(Intent.EcologyHash)
		|| !HasSha256(Intent.CompanionSnapshotHash) || !HasSha256(Intent.OutcomeHash)
		|| !HasSha256(Intent.IntentHash) || !HasSha256(Manifest.AnchorTopologyHash)
		|| !HasSha256(Manifest.PopulationHash) || !HasSha256(Manifest.ResourceHash)
		|| !HasSha256(Manifest.CompanionSnapshotHash)
		|| !HasSha256(Manifest.ManifestHash))
	{
		OutError = TEXT("REALIZED_HASH_INVALID");
		return false;
	}
	if (bOutcomeTelemetryDisabled
		&& (!FMath::IsNearlyEqual(Intent.FrozenOutcome.Combat, 0.5f)
			|| !FMath::IsNearlyEqual(Intent.FrozenOutcome.Survival, 0.5f)
			|| !FMath::IsNearlyEqual(Intent.FrozenOutcome.Resources, 0.5f)
			|| !FMath::IsNearlyEqual(Intent.FrozenOutcome.Pace, 0.5f)
			|| !FMath::IsNearlyZero(Intent.FrozenOutcome.DeathsAndFailures)))
	{
		OutError = TEXT("NEUTRAL_OUTCOME_FIXTURE_DRIFT");
		return false;
	}
	if ((Scenario == ZeroScenario || Scenario == ResourceMinScenario)
		&& (Manifest.EnemyCount != 0 || Manifest.NPCCount != 0 || Manifest.FoodCount != 0
			|| Manifest.ChestCount != 0 || Manifest.LooseLootCount != 0
			|| Manifest.ClothingCount != 0 || Manifest.SpecialEventCount != 0
			|| Manifest.SpawnedActorCount != 0))
	{
		OutError = TEXT("ZERO_SCENARIO_NOT_EXACT");
		return false;
	}
	if (Scenario == EnemyCap25Scenario
		&& (Manifest.EnemyCount != 25 || Manifest.NPCCount != 0 || Manifest.FoodCount != 0
			|| Manifest.ChestCount != 0 || Manifest.LooseLootCount != 0
			|| Manifest.ClothingCount != 0 || Manifest.SpecialEventCount != 0
			|| Manifest.SpawnedActorCount != 25))
	{
		OutError = TEXT("ENEMY_CAP_25_SCENARIO_NOT_EXACT");
		return false;
	}
	if (Scenario == ResourceMaxScenario
		&& (Manifest.EnemyCount != 0 || Manifest.NPCCount != 0
			|| Manifest.FoodCount != 30 || Manifest.ChestCount != 10
			|| Manifest.LooseLootCount != 0 || Manifest.ClothingCount != 0
			|| Manifest.SpecialEventCount != 0 || Manifest.SpawnedActorCount != 40))
	{
		OutError = TEXT("RESOURCE_MAX_SCENARIO_NOT_EXACT");
		return false;
	}
	if (Scenario == NPCTotal4Scenario
		&& (Manifest.EnemyCount != 0 || Manifest.NPCCount != 4
			|| Manifest.FoodCount != 0 || Manifest.ChestCount != 0
			|| Manifest.LooseLootCount != 0 || Manifest.ClothingCount != 0
			|| Manifest.SpecialEventCount != 0 || Manifest.SpawnedActorCount != 4))
	{
		OutError = TEXT("NPC_TOTAL_4_SCENARIO_NOT_EXACT");
		return false;
	}
	if (Scenario == SpecialEvents6Scenario
		&& (Manifest.EnemyCount != 0 || Manifest.NPCCount != 0
			|| Manifest.FoodCount != 0 || Manifest.ChestCount != 0
			|| Manifest.LooseLootCount != 0 || Manifest.ClothingCount != 0
			|| Manifest.SpecialEventCount != 6 || Manifest.SpawnedActorCount != 6))
	{
		OutError = TEXT("SPECIAL_EVENTS_6_SCENARIO_NOT_EXACT");
		return false;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		OutError = TEXT("READY_WORLD_MISSING");
		return false;
	}
	int32 LivePopulationActors = 0;
	int32 RemainingAnchorActors = 0;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		const AActor* Actor = *ActorIt;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		LivePopulationActors += Actor->ActorHasTag(PopulationTag) ? 1 : 0;
		RemainingAnchorActors += Actor->GetClass()->GetName().Contains(TEXT("EFCalystoPopulationAnchor")) ? 1 : 0;
	}
	if (LivePopulationActors != Manifest.SpawnedActorCount || RemainingAnchorActors != 0)
	{
		OutError = TEXT("LIVE_POPULATION_OR_ANCHOR_COUNT_MISMATCH");
		return false;
	}
	return true;
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
	if (Now - DoorSelectionStartedAtSeconds > 10.0)
	{
		Finish(false, TEXT("ACF_DOOR_SELECTION_TIMEOUT"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}
	if (bCaptureVisual && ExpectedFloor >= MaximumFloor)
	{
		if (!bScreenshotRequested)
		{
			// Capture the realized world render target, not EFLevelFlow's temporary
			// Slate/UMG loading overlay. The overlay remains part of normal gameplay;
			// only acceptance evidence omits UI.
			FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
			bScreenshotRequested = true;
			DoorInspectionNotBeforeSeconds = Now + 0.50;
			UE_LOG(
				LogEFCalystoPackagedSmoke,
				Log,
				TEXT("CALYSTO_PACKAGED_SMOKE_SCREENSHOT requested=true floor=%lld path=%s"),
				static_cast<long long>(ExpectedFloor),
				*ScreenshotPath);
			return true;
		}
		if (IFileManager::Get().FileSize(*ScreenshotPath) <= 1024)
		{
			return true;
		}
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World) || World->WorldType != EWorldType::Game || World->bIsTearingDown)
	{
		return true;
	}
	TArray<AEFCalystoFloorDoor*> Doors;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt) && !DoorIt->IsActorBeingDestroyed())
		{
			Doors.Add(*DoorIt);
		}
	}
	if (Doors.Num() == 0)
	{
		return true;
	}
	if (Doors.Num() != 1 || !Doors[0]->bIsEnabled)
	{
		Finish(false, Doors.Num() == 1 ? TEXT("FLOOR_DOOR_NOT_ENABLED") : TEXT("FLOOR_DOOR_CARDINALITY"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	UACFInteractionComponent* InteractionComponent = PlayerPawn
		? PlayerPawn->FindComponentByClass<UACFInteractionComponent>()
		: nullptr;
	if (!PlayerPawn || !InteractionComponent)
	{
		return true;
	}
	if (!bDoorPositioned)
	{
		const FVector DoorLocation = Doors[0]->GetActorLocation();
		const FVector SelectionLocation(DoorLocation.X - 80.0, DoorLocation.Y - 80.0, DoorLocation.Z);
		PlayerPawn->SetActorLocation(SelectionLocation, false, nullptr, ETeleportType::TeleportPhysics);
		InteractionComponent->EnableDetection(false);
		InteractionComponent->EnableDetection(true);
		bDoorPositioned = true;
	}
	InteractionComponent->RefreshInteractions();
	TArray<AActor*> OverlappingActors;
	InteractionComponent->GetOverlappingActors(OverlappingActors);
	if (InteractionComponent->GetCurrentBestInteractableActor() != Doors[0]
		|| !OverlappingActors.Contains(Doors[0]))
	{
		return true;
	}

	UE_LOG(
		LogEFCalystoPackagedSmoke,
		Log,
		TEXT("CALYSTO_PACKAGED_SMOKE_DOOR status=SELECTED floor=%lld label=%s"),
		static_cast<long long>(ExpectedFloor),
		*Doors[0]->GetInteractableName_Implementation().ToString());
	if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=DoorSelected floor=%lld"),
			static_cast<long long>(ExpectedFloor))))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_DOOR_SELECTION_WRITE_FAILED"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}
	++ProjectTelemetryDoorSelectedCount;
	if (ExpectedFloor >= MaximumFloor)
	{
		Finish(true, TEXT("PASS"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}

	const int64 SourceFloor = ExpectedFloor;
	ExpectedFloor = SourceFloor + 1;
	InteractionComponent->Interact(TEXT("CalystoV4PackagedSmoke"));
	if (!DungeonSubsystem->IsTravelRequestPending())
	{
		ExpectedFloor = SourceFloor;
		Finish(false, TEXT("ACF_DOOR_INTERACTION_DID_NOT_REQUEST_TRAVEL"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}
	++DoorInteractionCount;
	if (!AppendProjectTelemetry(FString::Printf(
			TEXT("event=DoorInteracted floor=%lld destination=%lld"),
			static_cast<long long>(SourceFloor),
			static_cast<long long>(ExpectedFloor))))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_DOOR_INTERACTION_WRITE_FAILED"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}
	++ProjectTelemetryDoorInteractedCount;
	if (bOutcomeTelemetryDisabled
		&& !AppendProjectTelemetry(FString::Printf(
			TEXT("event=OutcomeTelemetrySuppressed floor=%lld destination=%lld"),
			static_cast<long long>(SourceFloor),
			static_cast<long long>(ExpectedFloor))))
	{
		Finish(false, TEXT("PROJECT_TELEMETRY_OUTCOME_WRITE_FAILED"));
		DoorSelectionTickerHandle.Reset();
		return false;
	}
	UE_LOG(LogEFCalystoPackagedSmoke, Log,
		TEXT("CALYSTO_PACKAGED_SMOKE_DOOR status=INTERACTED floor=%lld destination=%lld"),
		static_cast<long long>(SourceFloor),
		static_cast<long long>(ExpectedFloor));
	DoorSelectionTickerHandle.Reset();
	return false;
}

void UEFCalystoPackagedSmokeSubsystem::HandleFloorTravelFailed()
{
	if (!bFinished)
	{
		const FEFCalystoDungeonSnapshotV4 Snapshot = DungeonSubsystem
			? DungeonSubsystem->GetSnapshot()
			: FEFCalystoDungeonSnapshotV4();
		const FString FailureCode = Snapshot.FailureCode.IsNone()
			? TEXT("UNKNOWN")
			: Snapshot.FailureCode.ToString();
		const FString FailureMessageHash = Snapshot.FailureMessage.IsEmpty()
			? TEXT("NONE")
			: UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Snapshot.FailureMessage);
		FString FailureDetail = Snapshot.FailureMessage.IsEmpty()
			? TEXT("NONE")
			: Snapshot.FailureMessage;
		FailureDetail.ReplaceInline(TEXT(" "), TEXT("_"));
		FailureDetail.ReplaceInline(TEXT("\t"), TEXT("_"));
		FailureDetail.ReplaceInline(TEXT("\r"), TEXT("_"));
		FailureDetail.ReplaceInline(TEXT("\n"), TEXT("_"));
		FailureDetail.ReplaceInline(TEXT("="), TEXT("-"));
		AppendProjectTelemetry(FString::Printf(
			TEXT("event=DirectorTravelFailure code=%s detail=%s messageHash=%s"),
			*FailureCode,
			*FailureDetail,
			*FailureMessageHash));
		Finish(false, FString::Printf(
			TEXT("FLOOR_TRAVEL_FAILED:%s"),
			*FailureCode));
	}
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
	const bool bTraceAndDoorTotalsValid = ProjectTelemetryReadySequenceCount == CompletedFloorCount
		&& ProjectTelemetryDoorSelectedCount == CompletedFloorCount
		&& ProjectTelemetryDoorInteractedCount == FMath::Max(0, CompletedFloorCount - 1);
	const bool bPreliminarySuccess = bSuccess && bProjectTelemetryHealthy
		&& bTraceAndDoorTotalsValid;
	FString EffectiveReason = Reason;
	if (bSuccess && !bPreliminarySuccess)
	{
		EffectiveReason = bProjectTelemetryHealthy
			? TEXT("PROJECT_TELEMETRY_EVENT_TOTALS_INVALID")
			: TEXT("PROJECT_TELEMETRY_WRITE_FAILED");
	}
	if (!bPreliminarySuccess
		&& AppendProjectTelemetry(FString::Printf(
			TEXT("event=Failure code=PACKAGED_SMOKE_FAILURE status=FAIL reason=%s"),
			*EffectiveReason.ReplaceCharWithEscapedChar())))
	{
		++ProjectTelemetryFailureEventCount;
	}
	const bool bCompletionTelemetryWritten = AppendProjectTelemetry(FString::Printf(
		TEXT("event=Complete status=%s tag=%s configuration=%s scenario=%s seed=%lld floors=%d interactions=%d maxFloor=%d elapsed=%.3f reason=%s"),
		bPreliminarySuccess ? TEXT("PASS") : TEXT("FAIL"),
		*RunTag,
		*ConfigurationName,
		*Scenario.ToString(),
		static_cast<long long>(RunSeed),
		CompletedFloorCount,
		DoorInteractionCount,
		MaximumFloor,
		ElapsedSeconds,
		*EffectiveReason.ReplaceCharWithEscapedChar()));
	if (bCompletionTelemetryWritten)
	{
		++ProjectTelemetryCompleteEventCount;
	}
	const bool bEffectiveSuccess = bPreliminarySuccess && bCompletionTelemetryWritten
		&& bProjectTelemetryHealthy;
	if (bPreliminarySuccess && !bEffectiveSuccess)
	{
		EffectiveReason = TEXT("PROJECT_TELEMETRY_COMPLETION_WRITE_FAILED");
	}
	const FString CompletionLine = FString::Printf(
		TEXT("CALYSTO_PACKAGED_SMOKE_COMPLETE tag=%s status=%s configuration=%s scenario=%s seed=%lld floors=%d interactions=%d maxFloor=%d receipt=%s screenshot=%s elapsed=%.3f reason=%s"),
		*RunTag,
		bEffectiveSuccess ? TEXT("PASS") : TEXT("FAIL"),
		*ConfigurationName,
		*Scenario.ToString(),
		static_cast<long long>(RunSeed),
		CompletedFloorCount,
		DoorInteractionCount,
		MaximumFloor,
		ReceiptPath.IsEmpty() ? TEXT("none") : *ReceiptPath,
		ScreenshotPath.IsEmpty() ? TEXT("none") : *ScreenshotPath,
		ElapsedSeconds,
		*EffectiveReason.ReplaceCharWithEscapedChar());
	const bool bReceiptWritten = WriteReceipt(bEffectiveSuccess, EffectiveReason, ElapsedSeconds);
	if (bEffectiveSuccess && bReceiptWritten)
	{
		UE_LOG(LogEFCalystoPackagedSmoke, Log, TEXT("%s"), *CompletionLine);
	}
	else
	{
		UE_LOG(LogEFCalystoPackagedSmoke, Error, TEXT("%s receiptWritten=%s"),
			*CompletionLine, bReceiptWritten ? TEXT("true") : TEXT("false"));
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
	if (ReceiptPath.IsEmpty())
	{
		const FString SafeConfiguration = ConfigurationName.IsEmpty() ? TEXT("Unknown") : ConfigurationName;
		if (!EFCalystoPackagedSmokePrivate::IsSafeRunTag(RunTag))
		{
			RunTag = EFCalystoPackagedSmokePrivate::MakeRunTag();
		}
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CalystoDungeonDirectorV4"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		ReceiptPath = FPaths::Combine(Directory,
			FString::Printf(
				TEXT("PackagedSmokeReceipt_%s_Invalid_%s.json"),
				*SafeConfiguration,
				*RunTag));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 4);
	Root->SetNumberField(TEXT("artifact_schema_version"), 3);
	Root->SetNumberField(TEXT("generator_version"), 4);
	Root->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("status"), bSuccess ? TEXT("PASS") : TEXT("FAIL"));
	Root->SetStringField(TEXT("configuration"), ConfigurationName);
	Root->SetStringField(TEXT("scenario"), Scenario.ToString());
	Root->SetStringField(TEXT("run_tag"), RunTag);
	Root->SetStringField(
		TEXT("outcome_mode"),
		bOutcomeTelemetryDisabled ? TEXT("neutral_missing_telemetry") : TEXT("live"));
	Root->SetBoolField(TEXT("outcome_telemetry_disabled"), bOutcomeTelemetryDisabled);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	const UEFCalystoDungeonHarnessSettings* HarnessSettings = UEFCalystoDungeonHarnessSettings::Get();
	const UEFCalystoDungeonDirectorPolicyV4* AuthorityPolicy = HarnessSettings
		? HarnessSettings->DirectorPolicy.Get()
		: nullptr;
	Root->SetStringField(
		TEXT("policy_path"),
		HarnessSettings ? HarnessSettings->DirectorPolicy.ToSoftObjectPath().ToString() : FString());
	Root->SetStringField(
		TEXT("policy_class"),
		AuthorityPolicy ? AuthorityPolicy->GetClass()->GetPathName() : FString());
	Root->SetNumberField(TEXT("policy_schema_version"), AuthorityPolicy ? AuthorityPolicy->SchemaVersion : 0);
	Root->SetNumberField(TEXT("policy_generator_version"), AuthorityPolicy ? AuthorityPolicy->GeneratorVersion : 0);
	Root->SetBoolField(TEXT("legacy_authority_loaded"), false);
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetStringField(TEXT("receipt_path"), ReceiptPath);
	Root->SetNumberField(TEXT("project_telemetry_schema_version"), ProjectTelemetrySchemaVersion);
	Root->SetStringField(TEXT("project_telemetry_path"), ProjectTelemetryPath);
	Root->SetStringField(TEXT("project_telemetry_sequence_count"), LexToString(ProjectTelemetrySequence));
	Root->SetNumberField(TEXT("project_telemetry_ready_sequence_count"), ProjectTelemetryReadySequenceCount);
	Root->SetNumberField(TEXT("project_telemetry_door_selected_count"), ProjectTelemetryDoorSelectedCount);
	Root->SetNumberField(TEXT("project_telemetry_door_interacted_count"), ProjectTelemetryDoorInteractedCount);
	Root->SetNumberField(TEXT("project_telemetry_failure_event_count"), ProjectTelemetryFailureEventCount);
	Root->SetNumberField(TEXT("project_telemetry_complete_event_count"), ProjectTelemetryCompleteEventCount);
	Root->SetBoolField(TEXT("project_telemetry_initialized"), bProjectTelemetryInitialized);
	Root->SetBoolField(TEXT("project_telemetry_healthy"), bProjectTelemetryHealthy);
	Root->SetStringField(TEXT("screenshot_path"), ScreenshotPath);
	Root->SetNumberField(TEXT("screenshot_floor"), bScreenshotRequested ? MaximumFloor : 0);
	Root->SetStringField(TEXT("visual_capture_mode"), TEXT("SceneRenderTargetNoSlate"));
	Root->SetBoolField(TEXT("screenshot_show_ui"), false);
	Root->SetStringField(TEXT("run_seed"), LexToString(RunSeed));
	Root->SetNumberField(TEXT("maximum_floor"), MaximumFloor);
	Root->SetNumberField(TEXT("completed_floor_count"), CompletedFloorCount);
	Root->SetNumberField(TEXT("door_interaction_count"), DoorInteractionCount);
	Root->SetNumberField(TEXT("elapsed_seconds"), ElapsedSeconds);
	Root->SetBoolField(TEXT("unattended"), FApp::IsUnattended());
	Root->SetBoolField(TEXT("requires_cooked_data"), FPlatformProperties::RequiresCookedData());
#if UE_BUILD_SHIPPING
	Root->SetBoolField(TEXT("shipping_build"), true);
	Root->SetBoolField(TEXT("exact_population_controls_compiled"), false);
#else
	Root->SetBoolField(TEXT("shipping_build"), false);
	Root->SetBoolField(TEXT("exact_population_controls_compiled"), true);
#endif
	if (DungeonSubsystem)
	{
		const FEFCalystoDungeonSnapshotV4 Snapshot = DungeonSubsystem->GetSnapshot();
		Root->SetStringField(TEXT("policy_hash"), Snapshot.PolicyHash);
		Root->SetStringField(TEXT("ecology_hash"), Snapshot.EcologyHash);
	}

	TArray<TSharedPtr<FJsonValue>> FloorValues;
	FloorValues.Reserve(ReadyFloorRecords.Num());
	for (const FReadyFloorRecord& Record : ReadyFloorRecords)
	{
		TSharedRef<FJsonObject> FloorObject = MakeShared<FJsonObject>();
		FloorObject->SetNumberField(TEXT("generator_version"), Record.GeneratorVersion);
		FloorObject->SetStringField(TEXT("floor_number"), LexToString(Record.FloorNumber));
		FloorObject->SetStringField(TEXT("generation_serial"), LexToString(Record.GenerationSerial));
		FloorObject->SetNumberField(TEXT("pcg_seed"), Record.PCGSeed);
		FloorObject->SetStringField(TEXT("style"), EFCalystoPackagedSmokePrivate::DescribeStyle(Record.Style));
		FloorObject->SetStringField(TEXT("theme"), EFCalystoPackagedSmokePrivate::DescribeTheme(Record.Theme));
		FloorObject->SetNumberField(TEXT("size_x"), Record.DungeonSize.X);
		FloorObject->SetNumberField(TEXT("size_y"), Record.DungeonSize.Y);
		FloorObject->SetNumberField(TEXT("size_z"), Record.DungeonSize.Z);
		FloorObject->SetNumberField(TEXT("candidate_anchor_count"), Record.CandidateAnchorCount);
		FloorObject->SetNumberField(TEXT("enemy_count"), Record.EnemyCount);
		FloorObject->SetNumberField(TEXT("npc_count"), Record.NPCCount);
		FloorObject->SetNumberField(TEXT("food_count"), Record.FoodCount);
		FloorObject->SetNumberField(TEXT("chest_count"), Record.ChestCount);
		FloorObject->SetNumberField(TEXT("loose_loot_count"), Record.LooseLootCount);
		FloorObject->SetNumberField(TEXT("clothing_count"), Record.ClothingCount);
		FloorObject->SetNumberField(TEXT("special_event_count"), Record.SpecialEventCount);
		FloorObject->SetNumberField(TEXT("spawned_actor_count"), Record.SpawnedActorCount);
		FloorObject->SetNumberField(TEXT("realized_threat_cost"), Record.RealizedThreatCost);
		FloorObject->SetNumberField(TEXT("realized_resource_cost"), Record.RealizedResourceCost);
		FloorObject->SetStringField(TEXT("policy_hash"), Record.PolicyHash);
		FloorObject->SetStringField(TEXT("ecology_hash"), Record.EcologyHash);
		FloorObject->SetStringField(TEXT("outcome_hash"), Record.OutcomeHash);
		TSharedRef<FJsonObject> FrozenOutcomeObject = MakeShared<FJsonObject>();
		FrozenOutcomeObject->SetBoolField(TEXT("is_frozen"), Record.bHasFrozenOutcome);
		FrozenOutcomeObject->SetNumberField(TEXT("combat"), Record.FrozenOutcome.Combat);
		FrozenOutcomeObject->SetNumberField(TEXT("survival"), Record.FrozenOutcome.Survival);
		FrozenOutcomeObject->SetNumberField(TEXT("resources"), Record.FrozenOutcome.Resources);
		FrozenOutcomeObject->SetNumberField(TEXT("pace"), Record.FrozenOutcome.Pace);
		FrozenOutcomeObject->SetNumberField(
			TEXT("deaths_and_failures"), Record.FrozenOutcome.DeathsAndFailures);
		FloorObject->SetObjectField(TEXT("frozen_outcome"), FrozenOutcomeObject);
		FloorObject->SetStringField(TEXT("intent_hash"), Record.IntentHash);
		FloorObject->SetStringField(TEXT("anchor_topology_hash"), Record.AnchorTopologyHash);
		FloorObject->SetStringField(TEXT("population_hash"), Record.PopulationHash);
		FloorObject->SetStringField(TEXT("resource_hash"), Record.ResourceHash);
		FloorObject->SetStringField(TEXT("companion_snapshot_hash"), Record.CompanionSnapshotHash);
		FloorObject->SetStringField(TEXT("manifest_hash"), Record.ManifestHash);
		FloorValues.Add(MakeShared<FJsonValueObject>(FloorObject));
	}
	Root->SetArrayField(TEXT("floors"), FloorValues);

	TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
	Checks->SetBoolField(TEXT("all_requested_floors_ready"), CompletedFloorCount == MaximumFloor);
	Checks->SetBoolField(TEXT("real_acf_door_interaction_count"),
		DoorInteractionCount == FMath::Max(0, MaximumFloor - 1));
	Checks->SetBoolField(TEXT("floor_records_complete"), ReadyFloorRecords.Num() == CompletedFloorCount);
	Checks->SetBoolField(TEXT("project_owned_telemetry_available"),
		bProjectTelemetryInitialized && bProjectTelemetryHealthy
			&& IFileManager::Get().FileSize(*ProjectTelemetryPath) > 0);
	Checks->SetBoolField(TEXT("pcg_runtime_trace_matches_ready_floors"),
		ProjectTelemetryReadySequenceCount == CompletedFloorCount);
	Checks->SetBoolField(TEXT("project_telemetry_door_totals_match"),
		ProjectTelemetryDoorSelectedCount == CompletedFloorCount
			&& ProjectTelemetryDoorInteractedCount == FMath::Max(0, CompletedFloorCount - 1));
	Checks->SetBoolField(TEXT("project_telemetry_complete_event_exact"),
		ProjectTelemetryCompleteEventCount == 1);
	Checks->SetBoolField(TEXT("project_telemetry_failure_events_match_status"),
		bSuccess ? ProjectTelemetryFailureEventCount == 0 : ProjectTelemetryFailureEventCount == 1);
	Checks->SetBoolField(TEXT("v4_authority_identity_exact"),
		AuthorityPolicy && AuthorityPolicy->SchemaVersion == 4 && AuthorityPolicy->GeneratorVersion == 4
			&& AuthorityPolicy->GetPathName()
				== TEXT("/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy")
			&& AuthorityPolicy->GetClass()->GetPathName()
				== TEXT("/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"));
	Checks->SetBoolField(TEXT("legacy_authority_absent"), true);
	Checks->SetBoolField(TEXT("shipping_natural_only"),
		ConfigurationName != TEXT("Shipping") || Scenario == EFCalystoPackagedSmokePrivate::NaturalScenario);
	Checks->SetBoolField(TEXT("neutral_fixture_scope_valid"),
		!bOutcomeTelemetryDisabled
			|| (ConfigurationName == TEXT("Development")
				&& Scenario == EFCalystoPackagedSmokePrivate::NaturalScenario
				&& FApp::IsUnattended()));
	Checks->SetBoolField(TEXT("visual_capture_matches_final_floor"),
		!bCaptureVisual
			|| (bScreenshotRequested && IFileManager::Get().FileSize(*ScreenshotPath) > 1024));
	Root->SetObjectField(TEXT("checks"), Checks);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(
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
