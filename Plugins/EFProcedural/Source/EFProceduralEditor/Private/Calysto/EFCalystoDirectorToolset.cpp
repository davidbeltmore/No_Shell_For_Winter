#include "Calysto/EFCalystoDirectorToolset.h"

#include "Calysto/EFCalystoDirectorSubsystem.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCalystoDirectorTools, Log, All);

namespace EFCalystoDirectorTools
{
	const FString Target = TEXT("D:/Projects UE5/NoShellForWinter");
	FTSTicker::FDelegateHandle ShutdownTicker;
	bool bShutdownPending = false;
	struct FTraversalPerformanceScope
	{
		bool bActive = false, bPreviousThrottle = false, bSawPIE = false, bStopRequested = false;
		double Started = -1, Finished = -1;
		uint64 FirstFrame = 0, LastFrame = 0;
		FString Receipt, RestoreReason;
		TWeakObjectPtr<UEditorEngine> Editor;
		TWeakObjectPtr<UEditorPerformanceSettings> Settings;
		FDelegateHandle ThrottleDelegate, EndPIEDelegate;
		FTSTicker::FDelegateHandle Ticker;
	} Performance;
	enum class EPerformanceAction { Observe, StopPIE, Restore };
	EPerformanceAction PerformanceAction(bool bTerminalReceipt, double Elapsed, bool bSawPIE, bool bPIE)
	{
		if (bTerminalReceipt || (bSawPIE && !bPIE) || (!bSawPIE && !bPIE && Elapsed >= 15.0) || Elapsed >= 150.0)
			return EPerformanceAction::Restore;
		return bPIE && Elapsed >= 145.0 ? EPerformanceAction::StopPIE : EPerformanceAction::Observe;
	}
	FString Normalize(FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeDirectoryName(Path);
		return Path;
	}
	bool TargetReady()
	{
		return IsInGameThread() && GEditor && GEngine && !IsRunningCommandlet()
			&& Normalize(FPaths::ProjectDir()).Equals(Target, ESearchCase::IgnoreCase)
			&& Normalize(FPaths::GetProjectFilePath()).Equals(Target / TEXT("NoShellForWinter.uproject"), ESearchCase::IgnoreCase)
			&& FEngineVersion::Current().GetMajor() == 5 && FEngineVersion::Current().GetMinor() == 8;
	}
	TSharedRef<FJsonObject> Result(const FString& Status, const FString& Message)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("status"), Status); Json->SetStringField(TEXT("message"), Message);
		return Json;
	}
	FString Encode(const TSharedRef<FJsonObject>& Json)
	{
		FString Text; FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)); return Text;
	}
	FString Fail(const FString& Message) { return Encode(Result(TEXT("FAIL"), Message)); }
	TArray<TSharedPtr<FJsonValue>> PackageNames(const TArray<UPackage*>& Packages)
	{
		TArray<FString> Names; for (const UPackage* P : Packages) if (P) Names.AddUnique(P->GetName());
		Names.Sort(); TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Name : Names) Values.Add(MakeShared<FJsonValueString>(Name));
		return Values;
	}
	bool HasDirtyPackages()
	{
		TArray<UPackage*> Content, Maps;
		UEditorLoadingAndSavingUtils::GetDirtyContentPackages(Content);
		UEditorLoadingAndSavingUtils::GetDirtyMapPackages(Maps);
		return !Content.IsEmpty() || !Maps.IsEmpty();
	}
	void RestorePerformance(const FString& Reason)
	{
		if (!Performance.bActive) return;
		Performance.bActive = false; Performance.Finished = FPlatformTime::Seconds(); Performance.LastFrame = GFrameCounter;
		Performance.RestoreReason = Reason;
		if (Performance.Ticker.IsValid()) FTSTicker::RemoveTicker(Performance.Ticker);
		Performance.Ticker.Reset();
		FEditorDelegates::EndPIE.Remove(Performance.EndPIEDelegate); Performance.EndPIEDelegate.Reset();
		if (UEditorEngine* Editor = Performance.Editor.Get())
			Editor->ShouldDisableCPUThrottlingDelegates.RemoveAll([](const UEditorEngine::FShouldDisableCPUThrottling& Delegate)
			{ return Delegate.GetHandle() == Performance.ThrottleDelegate; });
		Performance.ThrottleDelegate.Reset();
		UEditorPerformanceSettings* Settings = Performance.Settings.Get();
		if (Settings) Settings->bThrottleCPUWhenNotForeground = Performance.bPreviousThrottle;
		UE_LOG(LogCalystoDirectorTools, Display, TEXT("Traversal performance scope ended: reason=%s previous_throttle=%d restored=%d elapsed=%.3f frames=%llu"),
			*Reason, Performance.bPreviousThrottle, Settings != nullptr, Performance.Finished - Performance.Started,
			static_cast<unsigned long long>(Performance.LastFrame - Performance.FirstFrame));
	}
	void BeginPerformance(const FString& Receipt, double ArmedElapsed)
	{
		Performance = {}; Performance.bActive = true; Performance.Receipt = Receipt;
		Performance.Started = FPlatformTime::Seconds() - ArmedElapsed; Performance.FirstFrame = GFrameCounter;
		Performance.Editor = GEditor; Performance.Settings = GetMutableDefault<UEditorPerformanceSettings>();
		Performance.bPreviousThrottle = Performance.Settings->bThrottleCPUWhenNotForeground != 0;
		// CPU delegates also cover minimized windows. Rendering separately reads this CDO bit.
		// Change only this in-memory value; do not call SaveConfig or property-edit notifications.
		Performance.Settings->bThrottleCPUWhenNotForeground = false;
		auto Delegate = UEditorEngine::FShouldDisableCPUThrottling::CreateLambda([] { return Performance.bActive; });
		Performance.ThrottleDelegate = Delegate.GetHandle(); GEditor->ShouldDisableCPUThrottlingDelegates.Add(MoveTemp(Delegate));
		Performance.EndPIEDelegate = FEditorDelegates::EndPIE.AddLambda([](bool) { RestorePerformance(TEXT("EndPIE")); });
		Performance.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			const double Elapsed = FPlatformTime::Seconds() - Performance.Started;
			UEditorEngine* Editor = Performance.Editor.Get();
			const bool bPIE = Editor && Editor->IsPlaySessionInProgress(); Performance.bSawPIE |= bPIE;
			FString Body, Status; TSharedPtr<FJsonObject> ReceiptJson;
			const int64 Bytes = IFileManager::Get().FileSize(*Performance.Receipt);
			if (Bytes > 0 && Bytes <= 1048576 && FFileHelper::LoadFileToString(Body, *Performance.Receipt)
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), ReceiptJson) && ReceiptJson.IsValid())
				ReceiptJson->TryGetStringField(TEXT("status"), Status);
			const bool bTerminal = Status == TEXT("FAIL") || Status == TEXT("PASS")
				|| Status == TEXT("NATIVE_TRAVERSAL_ONLY") || Status == TEXT("TRAVERSAL_OBSERVED");
			const EPerformanceAction Action = PerformanceAction(bTerminal, Elapsed, Performance.bSawPIE, bPIE);
			if (bPIE && !Performance.bStopRequested && (Action == EPerformanceAction::StopPIE || Elapsed >= 150.0))
			{ Performance.bStopRequested = true; Editor->RequestEndPlayMap(); }
			if (!TargetReady() || Action == EPerformanceAction::Restore)
			{
				Performance.Ticker.Reset();
				RestorePerformance(bTerminal ? TEXT("Receipt:") + Status : TEXT("ScopeEndedOrDeadline"));
				return false;
			}
			return true;
		}), 0.5f);
		UE_LOG(LogCalystoDirectorTools, Display, TEXT("Traversal performance scope armed: previous_throttle=%d focus=%d receipt=%s"),
			Performance.bPreviousThrottle, FApp::HasFocus(), *Receipt);
	}
	void AppendPerformanceContext(const TSharedRef<FJsonObject>& Json)
	{
		Json->SetBoolField(TEXT("application_has_focus"), FApp::HasFocus());
		Json->SetBoolField(TEXT("throttle_cpu_when_not_foreground"), GetDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground != 0);
		Json->SetBoolField(TEXT("should_throttle_cpu"), GEditor && GEditor->ShouldThrottleCPUUsage());
		Json->SetNumberField(TEXT("editor_frame_delta_seconds"), FApp::GetDeltaTime());
		Json->SetBoolField(TEXT("traversal_performance_scope_active"), Performance.bActive);
		Json->SetStringField(TEXT("traversal_performance_restore_reason"), Performance.RestoreReason);
		if (Performance.Started >= 0)
		{
			const double Elapsed = (Performance.bActive ? FPlatformTime::Seconds() : Performance.Finished) - Performance.Started;
			const uint64 Frames = (Performance.bActive ? GFrameCounter : Performance.LastFrame) - Performance.FirstFrame;
			Json->SetBoolField(TEXT("traversal_previous_throttle"), Performance.bPreviousThrottle);
			Json->SetNumberField(TEXT("traversal_mean_editor_fps"), Elapsed > 0 ? static_cast<double>(Frames) / Elapsed : 0.0);
		}
	}
}

FString UEFCalystoDirectorToolset::ReadEditorContext()
{
	using namespace EFCalystoDirectorTools;
	if (!IsInGameThread() || !GEditor || !GEngine) return Fail(TEXT("A live Editor game-thread context is required."));
	auto Json = Result(TargetReady() ? TEXT("OK") : TEXT("WRONG_CONTEXT"), TEXT("Read-only Editor observation; no gameplay acceptance implied."));
	Json->SetStringField(TEXT("project_directory"), Normalize(FPaths::ProjectDir()));
	Json->SetStringField(TEXT("project_file"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	Json->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	Json->SetStringField(TEXT("editor_world"), World ? World->GetPathName() : TEXT(""));
	Json->SetBoolField(TEXT("pie_active_or_queued"), GEditor->IsPlaySessionInProgress());
	TArray<TSharedPtr<FJsonValue>> Worlds;
	for (const FWorldContext& C : GEngine->GetWorldContexts())
		if (C.WorldType == EWorldType::PIE && C.World()) Worlds.Add(MakeShared<FJsonValueString>(C.World()->GetPathName()));
	Json->SetArrayField(TEXT("pie_worlds"), Worlds);
	TArray<UPackage*> Content, Maps;
	UEditorLoadingAndSavingUtils::GetDirtyContentPackages(Content);
	UEditorLoadingAndSavingUtils::GetDirtyMapPackages(Maps);
	Json->SetArrayField(TEXT("dirty_content_packages"), PackageNames(Content));
	Json->SetArrayField(TEXT("dirty_map_packages"), PackageNames(Maps));
	const IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	Json->SetBoolField(TEXT("python_available"), Python && Python->IsPythonAvailable());
	Json->SetBoolField(TEXT("python_initialized"), Python && Python->IsPythonInitialized());
	Json->SetBoolField(TEXT("shutdown_pending"), bShutdownPending);
	AppendPerformanceContext(Json);
	return Encode(Json);
}

FString UEFCalystoDirectorToolset::ArmTraversal(const FString& EvidenceName, const FString& Mode)
{
	using namespace EFCalystoDirectorTools;
	if (!TargetReady()) return Fail(TEXT("Only the live NoShellForWinter UE 5.8 Editor is supported."));
	if (bShutdownPending || Performance.bActive || GEditor->IsPlaySessionInProgress()) return Fail(TEXT("PIE and the previous traversal must be stopped with no play or shutdown request queued."));
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || World->GetPathName() != TEXT("/Game/_Game/Hub/HUB.HUB")) return Fail(TEXT("HUB must already be loaded; no map is changed by this tool."));
	if (EvidenceName.IsEmpty() || EvidenceName.Len() > 80) return Fail(TEXT("EvidenceName requires 1..80 ASCII letters/digits/_/-."));
	for (const TCHAR C : EvidenceName)
		if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == '-'))
			return Fail(TEXT("EvidenceName contains an unsupported character."));
	if (Mode != TEXT("native_parity") && Mode != TEXT("full")) return Fail(TEXT("Mode must be native_parity or full."));
	if (FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorNativeParity")) != (Mode == TEXT("native_parity")))
		return Fail(TEXT("Mode does not match the Editor's native-parity launch flag."));
	const FString Output = Target / TEXT("Saved/Migration/CalystoDungeonDirectorV7") / EvidenceName;
	const FString Script = Target / TEXT("Tools/Migration/Validate-CalystoDirectorTraversal58.py");
	const FString Receipt = Output / TEXT("traversal.json");
	if (IFileManager::Get().DirectoryExists(*Output) || IFileManager::Get().FileExists(*Output)) return Fail(TEXT("Evidence output already exists; choose a fresh name."));
	if (!IFileManager::Get().FileExists(*Script)) return Fail(TEXT("The fixed project traversal script is missing."));
	IPythonScriptPlugin* Python = FModuleManager::LoadModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
	if (!Python || !Python->IsPythonAvailable()) return Fail(TEXT("Editor Python is unavailable."));
	if (!Python->IsPythonInitialized()) Python->ForceEnablePythonAtRuntime();
	if (!Python->IsPythonInitialized()) return Fail(TEXT("Editor Python could not initialize."));
	FPythonCommandEx Command;
	Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	Command.FileExecutionScope = EPythonFileExecutionScope::Private;
	// All interpolated values are fixed project paths or strictly allowlisted tokens.
	Command.Command = FString::Printf(TEXT("import builtins, runpy\nbuiltins.CALYSTO_DIRECTOR_TRAVERSAL_OPTIONS = {'output_dir': r'%s', 'mode': '%s', 'mcp_arm': True}\nrunpy.run_path(r'%s')\n"), *Output, *Mode, *Script);
	if (!Python->ExecPythonCommandEx(Command)) return Fail(TEXT("Traversal arming failed: ") + Command.CommandResult.Left(4096));
	const int64 Bytes = IFileManager::Get().FileSize(*Receipt);
	FString Body; TSharedPtr<FJsonObject> Armed;
	if (Bytes <= 0 || Bytes > 1048576 || !FFileHelper::LoadFileToString(Body, *Receipt)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Armed) || !Armed.IsValid())
		return Fail(TEXT("Python returned without a valid bounded traversal receipt. Do not start PIE."));
	FString Status, RecordedMode;
	if (!Armed->TryGetStringField(TEXT("status"), Status) || Status != TEXT("ARMED")
		|| !Armed->TryGetStringField(TEXT("mode"), RecordedMode) || RecordedMode != Mode)
		return Fail(TEXT("The fresh receipt is not ARMED for the requested mode. Do not start PIE."));
	double ArmedElapsed = 0;
	if (!Armed->TryGetNumberField(TEXT("elapsed_seconds"), ArmedElapsed) || !FMath::IsFinite(ArmedElapsed) || ArmedElapsed < 0 || ArmedElapsed >= 30.0)
		return Fail(TEXT("The ARMED receipt has no remaining bounded PIE startup window. Do not start PIE."));
	BeginPerformance(Receipt, ArmedElapsed);
	auto Json = Result(TEXT("ARMED"), TEXT("Fresh ARMED receipt verified. Start PIE separately before the harness's 30-second startup limit."));
	Json->SetStringField(TEXT("receipt"), Receipt); Json->SetObjectField(TEXT("arming_receipt"), Armed);
	AppendPerformanceContext(Json);
	return Encode(Json);
}

FString UEFCalystoDirectorToolset::ReadDirectorDiagnostics()
{
	using namespace EFCalystoDirectorTools;
	if (!TargetReady()) return Fail(TEXT("Only the live NoShellForWinter UE 5.8 Editor is supported."));
	UWorld* World = nullptr;
	for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::PIE && C.World())
	{
		if (World) return Fail(TEXT("Multiple PIE worlds are ambiguous; no Director was selected."));
		World = C.World();
	}
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const auto* Director = Instance ? Instance->GetSubsystem<UEFCalystoDirectorSubsystem>() : nullptr;
	if (!Director) return Fail(TEXT("No existing unversioned Director on the actual PIE GameInstance."));
	const FString Body = Director->GetDiagnosticsJson(); TSharedPtr<FJsonObject> Diagnostics;
	if (Body.Len() > 1048576 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Diagnostics) || !Diagnostics.IsValid())
		return Fail(TEXT("Director diagnostics are missing, invalid or exceed the bounded response size."));
	auto Json = Result(TEXT("OK"), TEXT("Read-only diagnostics from the existing PIE Director."));
	Json->SetStringField(TEXT("pie_world"), World->GetPathName());
	Json->SetStringField(TEXT("game_instance"), Instance->GetPathName());
	// FJsonObject stores numbers as doubles. Keep the original JSON so int64 run
	// seeds survive transport exactly; consumers parse this string without a float roundtrip.
	Json->SetStringField(TEXT("diagnostics_json"), Body); AppendPerformanceContext(Json); return Encode(Json);
}

FString UEFCalystoDirectorToolset::MigrateAuthoring(const FString& EvidenceName, bool SaveMaster)
{
	using namespace EFCalystoDirectorTools;
	if (!TargetReady() || bShutdownPending || Performance.bActive || GEditor->IsPlaySessionInProgress() || HasDirtyPackages())
		return Fail(TEXT("Migration requires the target UE 5.8 Editor, stopped PIE and no dirty packages or pending shutdown."));
	if (EvidenceName.IsEmpty() || EvidenceName.Len() > 80) return Fail(TEXT("EvidenceName requires 1..80 ASCII letters/digits/_/-."));
	for (const TCHAR C : EvidenceName)
		if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == '-'))
			return Fail(TEXT("EvidenceName contains an unsupported character."));
	const FString Root = Target / TEXT("Saved/Migration/CalystoDungeonDirectorV7");
	const FString Output = Root / EvidenceName;
	const FString Script = Target / TEXT("Tools/Migration/Import-CalystoDungeonDirector58.py");
	if (IFileManager::Get().DirectoryExists(*Output) || IFileManager::Get().FileExists(*Output)
		|| !IFileManager::Get().FileExists(*Script)) return Fail(TEXT("Migration needs a fresh evidence name and its fixed project importer."));
	if (!IFileManager::Get().MakeDirectory(*Output, true)) return Fail(TEXT("Migration evidence directory could not be created."));
	for (const TCHAR* Receipt : {TEXT("ImportDirectorStaged.json"), TEXT("ImportDirector.json")})
		if (IFileManager::Get().FileExists(*(Root / Receipt))
			&& IFileManager::Get().Copy(*(Output / (FString(TEXT("Before_")) + Receipt)), *(Root / Receipt)) != COPY_OK)
			return Fail(TEXT("Previous migration evidence could not be archived."));
	IPythonScriptPlugin* Python = FModuleManager::LoadModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
	if (!Python || !Python->IsPythonAvailable()) return Fail(TEXT("Editor Python is unavailable."));
	if (!Python->IsPythonInitialized()) Python->ForceEnablePythonAtRuntime();
	if (!Python->IsPythonInitialized()) return Fail(TEXT("Editor Python could not initialize."));
	const auto Run = [&](bool StageOnly, TSharedPtr<FJsonObject>& Report)
	{
		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Command.FileExecutionScope = EPythonFileExecutionScope::Private;
		Command.Command = FString::Printf(TEXT("import runpy\nrunpy.run_path(r'%s')['main'](stage_only=%s)\n"), *Script, StageOnly ? TEXT("True") : TEXT("False"));
		const bool Executed = Python->ExecPythonCommandEx(Command);
		const FString Name = StageOnly ? TEXT("ImportDirectorStaged.json") : TEXT("ImportDirector.json");
		const FString Path = Root / Name;
		const int64 Size = IFileManager::Get().FileSize(*Path);
		FString Body;
		if (Size <= 0 || Size > 4194304 || !FFileHelper::LoadFileToString(Body, *Path)
			|| IFileManager::Get().Copy(*(Output / Name), *Path) != COPY_OK
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Report) || !Report) return false;
		FString Status;
		return Executed && Report->TryGetStringField(TEXT("status"), Status) && Status != TEXT("FAIL");
	};
	TSharedPtr<FJsonObject> Report;
	if (!Run(true, Report)) return Fail(TEXT("Native authoring staging failed; inspect the archived importer receipt in ") + Output);
	bool Complete = false;
	Report->TryGetBoolField(TEXT("complete_migration"), Complete);
	if (SaveMaster && Complete && !Run(false, Report)) return Fail(TEXT("Exact master import failed; inspect the archived receipt and dirty state in ") + Output);
	auto Json = Result(SaveMaster && Complete ? TEXT("AUTHORING_IMPORTED") : TEXT("AUTHORING_STAGED"),
		SaveMaster && !Complete ? TEXT("Master save refused because source mappings remain pending. No runtime authority was changed.") : TEXT("Authoring operation completed. Gameplay, visual and release acceptance remain separate."));
	Json->SetStringField(TEXT("evidence_directory"), Output);
	Json->SetBoolField(TEXT("complete_mapping"), Complete);
	Json->SetBoolField(TEXT("save_requested"), SaveMaster);
	Json->SetBoolField(TEXT("save_permitted"), SaveMaster && Complete);
	Json->SetObjectField(TEXT("report"), Report);
	return Encode(Json);
}

FToolsetImage UEFCalystoDirectorToolset::CaptureAuthoringDetails()
{
	using namespace EFCalystoDirectorTools;
	FToolsetImage Image;
	if (!TargetReady() || GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued() || !FSlateApplication::IsInitialized())
	{ UKismetSystemLibrary::RaiseScriptError(TEXT("Authoring capture requires the target Editor with stopped PIE.")); return Image; }
	UObject* Master=FindObject<UObject>(nullptr,TEXT("/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector.DA_CalystoDungeonDirector"));
	auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	auto* Editor=Master && Editors ? Editors->FindEditorForAsset(Master,false) : nullptr;
	const auto Tabs=Editor ? Editor->GetAssociatedTabManager() : nullptr;
	const auto Tab=Tabs ? Tabs->GetOwnerTab() : nullptr;
	const auto Window=Tab ? Tab->GetParentWindow() : nullptr;
	if (!Window || !Window->IsVisible())
	{ UKismetSystemLibrary::RaiseScriptError(TEXT("Open the exact master asset editor before capturing its native Details.")); return Image; }
	const FVector2D Size=Window->GetSizeInScreen();
	if (Size.X<=0 || Size.Y<=0 || Size.X*Size.Y>16777216)
	{ UKismetSystemLibrary::RaiseScriptError(TEXT("Authoring window exceeds the bounded 16-megapixel capture.")); return Image; }
	TArray<FColor> Pixels; FIntVector Dimensions;
	if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Dimensions)
		|| !Image.SetFromBitmap(Pixels,FIntPoint(Dimensions.X,Dimensions.Y)))
		UKismetSystemLibrary::RaiseScriptError(TEXT("The native asset editor window could not be captured."));
	return Image;
}

FString UEFCalystoDirectorToolset::RequestEditorShutdown()
{
	using namespace EFCalystoDirectorTools;
	if (!TargetReady()) return Fail(TEXT("Only the live NoShellForWinter UE 5.8 Editor is supported."));
	if (GEditor->IsPlaySessionInProgress() || HasDirtyPackages()) return Fail(TEXT("Shutdown requires no active/queued PIE and no dirty content or map packages."));
	if (bShutdownPending) return Encode(Result(TEXT("SHUTDOWN_PENDING"), TEXT("A graceful shutdown request is already queued.")));
	bShutdownPending = true;
	const double RequestedAt=FPlatformTime::Seconds();
	ShutdownTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([RequestedAt,bEditorsClosed=false,ExitAfterFrame=uint64(0)](float) mutable
	{
		const auto Cancel=[](){ bShutdownPending=false; ShutdownTicker.Reset(); return false; };
		if (!TargetReady() || GEditor->IsPlaySessionInProgress() || HasDirtyPackages()) return Cancel();
		const double Elapsed=FPlatformTime::Seconds()-RequestedAt;
		if (Elapsed<2.0) return true;
		auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!Editors) return Cancel();
		if (!bEditorsClosed)
		{
			// Native Details/toolbars must close while their Editor/world services are alive.
			// EngineExit first destroys those services, leaving floating asset windows to
			// close later during Slate shutdown. Observe the deferred window close first.
			if (!Editors->CloseAllAssetEditors()) return Cancel();
			bEditorsClosed=true; ExitAfterFrame=GFrameCounter+2; return true;
		}
		if (GFrameCounter<ExitAfterFrame || !Editors->GetAllOpenEditors().IsEmpty())
		{
			if (Elapsed>=10.0) { UE_LOG(LogCalystoDirectorTools,Warning,TEXT("Editor shutdown cancelled: asset windows did not finish closing within 10 seconds.")); return Cancel(); }
			return true;
		}
		bShutdownPending=false; ShutdownTicker.Reset();
		RestorePerformance(TEXT("EditorShutdown")); GEditor->CloseEditor();
		return false;
	}), 0.25f);
	return Encode(Result(TEXT("SHUTDOWN_PENDING"), TEXT("Graceful closure queued: close clean asset windows first, then exit. Dirty packages, PIE or a 10-second window-close timeout cancel it. Verify actual process exit separately.")));
}

void UEFCalystoDirectorToolset::CancelPendingShutdown()
{
	using namespace EFCalystoDirectorTools;
	if (ShutdownTicker.IsValid()) FTSTicker::RemoveTicker(ShutdownTicker);
	ShutdownTicker.Reset(); bShutdownPending = false;
	RestorePerformance(TEXT("ModuleShutdown"));
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTraversalPerformanceDeadlineTest,
	"NoShellForWinter.CalystoDungeon.Director.EditorAutomation.PerformanceScopeDeadlines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTraversalPerformanceDeadlineTest::RunTest(const FString&)
{
	using namespace EFCalystoDirectorTools;
	TestEqual(TEXT("Armed preparation retains its scope before startup expires"), PerformanceAction(false, 14.0, false, false), EPerformanceAction::Observe);
	TestEqual(TEXT("An unstarted harness cannot leave throttling disabled"), PerformanceAction(false, 15.0, false, false), EPerformanceAction::Restore);
	TestEqual(TEXT("A running test keeps full cadence through its work budget"), PerformanceAction(false, 144.0, true, true), EPerformanceAction::Observe);
	TestEqual(TEXT("The existing final five seconds are reserved for PIE cleanup"), PerformanceAction(false, 145.0, true, true), EPerformanceAction::StopPIE);
	TestEqual(TEXT("Even failed cleanup cannot extend the scope past 150 seconds"), PerformanceAction(false, 150.0, true, true), EPerformanceAction::Restore);
	TestEqual(TEXT("A terminal receipt restores without waiting for the deadline"), PerformanceAction(true, 20.0, true, true), EPerformanceAction::Restore);
	TestEqual(TEXT("An externally ended owned PIE restores immediately"), PerformanceAction(false, 20.0, true, false), EPerformanceAction::Restore);
	return true;
}
#endif
