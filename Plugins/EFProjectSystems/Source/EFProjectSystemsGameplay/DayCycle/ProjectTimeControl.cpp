#include "DayCycle/ProjectDayCycleSubsystem.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "DayCycle/ProjectDayCycleStateActor.h"
#include "ACFTeamManagerSubsystem.h"
#include "ACFAIController.h"
#include "Interfaces/ACFEntityInterface.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "EFCharacterCreationSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Widgets/SViewport.h"
#include "Slate/SGameLayerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Lockpicking/ProjectLockpickingSubsystem.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectInnerStateComponent.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "HAL/IConsoleManager.h"
#include "ACFTrainingComponent.h"
#include "UI/ProjectActivityFeedSubsystem.h"
#include "DaySequenceSubsystem.h"
#include "DayCycle/ProjectWaitWidget.h"
#include "ANSUIPlayerSubsystem.h"
#include "CommonActivatableWidget.h"

#define LOCTEXT_NAMESPACE "ProjectTimeControl"

namespace
{
	class FProjectTimeInput final : public IInputProcessor
	{
	public:
		explicit FProjectTimeInput(UProjectDayCycleSubsystem* InOwner) : Owner(InOwner) {}
		virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			return Owner.IsValid() && Owner->RouteTimeKey(Event);
		}
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override
		{
			return Owner.IsValid() && Owner->ShouldBlockPausedGameplayInput();
		}
	private:
		TWeakObjectPtr<UProjectDayCycleSubsystem> Owner;
	};
}

bool UProjectDayCycleSubsystem::IsSupportedSpeed(float Speed)
{
	return Speed == 1.f || Speed == 2.f || Speed == 4.f;
}

double UProjectDayCycleSubsystem::SimulationSecondsToCalendarHours(double Seconds, double DaySeconds)
{
	return FMath::IsFinite(Seconds) && FMath::IsFinite(DaySeconds) && DaySeconds > 0.0
		? FMath::Max(0.0, Seconds) * 24.0 / DaySeconds : 0.0;
}

double UProjectDayCycleSubsystem::GetCalendarHours() const
{
	return StateActor ? StateActor->GetElapsedCalendarDays() * 24.0 : 0.0;
}

bool UProjectDayCycleSubsystem::CanFastForward(FText& OutReason) const
{
	return CanFastForwardInternal(OutReason, false);
}

bool UProjectDayCycleSubsystem::CanFastForwardInternal(FText& OutReason, bool bIgnorePlayerPause) const
{
	const UWorld* World = GetWorld();
	const auto* Settings = UProjectDayCycleSettings::Get();
	const APawn* Player = World && World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	auto Deny = [&OutReason](const FText& Reason) { OutReason = Reason; return false; };
	if (!World || World->GetNetMode() != NM_Standalone || !Player || !Settings)
		return Deny(LOCTEXT("NoPlayer", "Time acceleration requires a ready single-player character."));
	FString Package = World->GetPackage()->GetName();
	if (!World->StreamingLevelsPrefix.IsEmpty()) Package.ReplaceInline(*World->StreamingLevelsPrefix, TEXT(""));
	if (!Settings->FastForwardMaps.ContainsByPredicate([&Package](const FSoftObjectPath& Map) { return Map.GetLongPackageName() == Package; }))
		return Deny(LOCTEXT("UnsafeMap", "Time acceleration is available in HUB and StorySelection."));
	if (UGameplayStatics::IsGamePaused(World) && !(bIgnorePlayerPause && PauseReasons.Num() == 1 && PauseReasons.Contains(TEXT("Player")) && !IsTimeMenuOpen()))
		return Deny(LOCTEXT("Paused", "Resume time before changing its speed."));
	if (World->GetTimeSeconds() < DamageBlockUntil)
		return Deny(LOCTEXT("Damage", "Time acceleration stopped after taking damage."));
	if (const UGameInstance* GI = World->GetGameInstance())
	{
		if (const auto* Save = GI->GetSubsystem<UALSLoadAndSaveSubsystem>(); Save && Save->GetSystemState() != ELoadingState::EIdle)
			return Deny(LOCTEXT("Loading", "Wait for saving or loading to finish."));
		if (const auto* Creator = GI->GetSubsystem<UEFCharacterCreationSubsystem>(); Creator && Creator->IsCharacterCreationActive())
			return Deny(LOCTEXT("Creation", "Finish character creation first."));
	}
	if (const auto* Locks = World->GetSubsystem<UProjectLockpickingSubsystem>(); Locks && Locks->IsLockpickingOpen())
		return Deny(LOCTEXT("Precision", "Manual lockpicking uses normal time."));
	if (const auto* Training = Player->FindComponentByClass<UACFTrainingComponent>(); Training && Training->IsTimingMinigameSessionActive())
		return Deny(LOCTEXT("TrainingPrecision", "Timing exercises use normal time."));
	if (Player->Implements<UACFEntityInterface>() && !IACFEntityInterface::Execute_IsEntityAlive(Player))
		return Deny(LOCTEXT("Defeated", "Time acceleration requires a living character."));
	if (const auto* Needs = Player->FindComponentByClass<UProjectSurvivalNeedsComponent>())
	{
		const auto* Inner = Player->FindComponentByClass<UProjectInnerStateComponent>();
		if (Inner && (Inner->State.bUnconscious || Inner->State.bCriticalSickness || Inner->State.Air<99.9 || Inner->State.Awareness<50 || Inner->GetValue(TEXT("Poison"))>0))
			return Deny(LOCTEXT("InnerStateCritical", "Resolve oxygen loss, poison or impaired consciousness before accelerating time."));
		for (const FName Name : { FName(TEXT("Hunger")), FName(TEXT("Thirst")), FName(TEXT("Sleep")) })
			if (!(Name==TEXT("Sleep") && Inner && Inner->IsSleeping()) && Needs->HasNeed(Name) && Needs->GetNeedNormalizedValue(Name) <= Settings->CriticalNeedFraction)
				return Deny(LOCTEXT("Critical", "A need is critically low. Time returned to normal."));
	}
	const auto* Teams = World->GetSubsystem<UACFTeamManagerSubsystem>();
	if (!Teams || !Teams->GetActorTeam(Player).IsValid())
		return Deny(LOCTEXT("UnknownTeam", "Waiting for the character's combat team."));
	for (TActorIterator<APawn> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const APawn* Other = *It;
		if (Other == Player || Other->IsActorBeingDestroyed()) continue;
		if (Other->Implements<UACFEntityInterface>() && !IACFEntityInterface::Execute_IsEntityAlive(Other)) continue;
		const auto* AI = Cast<AACFAIController>(Other->GetController());
		if (AI && (AI->GetTarget() == Player || AI->GetTargetActorBK() == Player))
			return Deny(LOCTEXT("Pursuit", "An enemy is targeting you."));
		if (FVector::DistSquared(Player->GetActorLocation(), Other->GetActorLocation()) <= FMath::Square(Settings->ThreatRadius)
			&& (Teams->AreActorsHostile(Other, Player) || Teams->AreActorsHostile(Player, Other)))
			return Deny(LOCTEXT("Nearby", "An enemy is nearby."));
	}
	OutReason = FText::GetEmpty();
	return true;
}

bool UProjectDayCycleSubsystem::RequestTimeSpeed(float Speed)
{
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Standalone || !IsSupportedSpeed(Speed)) return false;
	FText Reason;
	if (Speed > 1.f && !CanFastForward(Reason))
	{
		TimeControlMessage = Reason;
		if (auto* Feed = GetWorld()->GetSubsystem<UProjectActivityFeedSubsystem>()) Feed->AddSystemEntry(Reason);
		RefreshHud();
		return false;
	}
	RequestedSpeed = Speed;
	LastAcceleratedHealth = -1.f;
	if (Speed > 1.f && GetWorld()->GetFirstPlayerController() && GetWorld()->GetFirstPlayerController()->GetPawn())
	{
		if (const auto* Snapshot = GetWorld()->GetFirstPlayerController()->GetPawn()->FindComponentByClass<UProjectRealtimeSnapshotComponent>())
		{
			float Maximum = 0.f;
			Snapshot->TryReadOwnerResource(TEXT("Health"), LastAcceleratedHealth, Maximum);
		}
	}
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Speed);
	TimeControlMessage = FText::GetEmpty();
	RefreshHud();
	return FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(GetWorld()), Speed);
}

bool UProjectDayCycleSubsystem::SetPauseReason(FName Reason, bool bPause)
{
	if (Reason.IsNone() || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone) return false;
	if (bPause)
	{
		PauseReasons.Add(Reason);
		RequestTimeSpeed(1.f);
		if (!UGameplayStatics::IsGamePaused(GetWorld()))
		{
			bOwnsEnginePause = UGameplayStatics::SetGamePaused(GetWorld(), true);
			if (!bOwnsEnginePause) { PauseReasons.Remove(Reason); return false; }
		}
	}
	else
	{
		PauseReasons.Remove(Reason);
		if (PauseReasons.IsEmpty() && bOwnsEnginePause)
		{
			if (!UGameplayStatics::SetGamePaused(GetWorld(), false)) return false;
			bOwnsEnginePause = false;
		}
	}
	RefreshHud();
	return true;
}

bool UProjectDayCycleSubsystem::TogglePlayerPause()
{
	return SetPauseReason(TEXT("Player"), !PauseReasons.Contains(TEXT("Player")));
}

bool UProjectDayCycleSubsystem::HasTimeInputFocus() const
{
	if (!LocalPlayerController || !LocalPlayerController->IsLocalController() || !FSlateApplication::IsInitialized()
		|| !LocalPlayerController->GetLocalPlayer() || !GetWorld()->GetGameViewport()) return false;
	const auto ViewportWidget = GetWorld()->GetGameViewport()->GetGameViewportWidget();
	const auto Layers = GetWorld()->GetGameViewport()->GetGameLayerManager();
	return (ViewportWidget.IsValid() && ViewportWidget->HasAnyUserFocusOrFocusedDescendants())
		|| (Layers.IsValid() && Layers->AsWidget()->HasAnyUserFocusOrFocusedDescendants());
}

bool UProjectDayCycleSubsystem::ShouldBlockPausedGameplayInput() const
{
	return PauseReasons.Num() == 1 && PauseReasons.Contains(TEXT("Player"))
		&& !IsTimeMenuOpen() && UGameplayStatics::IsGamePaused(GetWorld()) && HasTimeInputFocus();
}

bool UProjectDayCycleSubsystem::RouteTimeKey(const FKeyEvent& Event)
{
	if (!HasTimeInputFocus() || Event.IsControlDown() || Event.IsAltDown() || Event.IsCommandDown()) return false;
	const auto Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (Focused.IsValid() && Focused->GetTypeAsString().Contains(TEXT("EditableText"))) return false;
	const FKey Key = Event.GetKey();
	// P belongs to the native ACF menu. Unshifted top-row -/= own time;
	// numpad +/- and shifted chords retain their previous interactions.
	const bool bTimeStep = !Event.IsShiftDown() && (Key == EKeys::Hyphen || Key == EKeys::Equals);
	const bool bWait = !Event.IsShiftDown() && Key == EKeys::Apostrophe;
	if (Key != EKeys::P && !bTimeStep && !bWait)
		return ShouldBlockPausedGameplayInput();
	if (Event.IsRepeat()) return true;
	if (Key == EKeys::P) ToggleNativePauseMenu();
	else if (bWait) ToggleWaitMenu();
	else StepTimeSpeed(Key == EKeys::Equals ? 1 : -1);
	return true;
}

void UProjectDayCycleSubsystem::UpdateTimeControl()
{
	if (GetWorld()->GetNetMode() != NM_Standalone) return;
	if (IsWaiting() && (GetCalendarHours() >= WaitUntilCalendarHours || RequestedSpeed != 4.f)) CancelWait();
	// Legacy UI owners may release the native pause directly. They must not
	// release an outstanding player/project pause with it.
	if (!PauseReasons.IsEmpty() && !UGameplayStatics::IsGamePaused(GetWorld()))
		bOwnsEnginePause = UGameplayStatics::SetGamePaused(GetWorld(), true);
	if (!TimeInputProcessor && LocalPlayerController && FSlateApplication::IsInitialized())
	{
		TimeInputProcessor = MakeShared<FProjectTimeInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(TimeInputProcessor, 0);
	}
	APawn* Pawn = LocalPlayerController ? LocalPlayerController->GetPawn() : nullptr;
	if (Pawn != TimePlayerPawn.Get())
	{
		if (TimePlayerPawn.IsValid()) TimePlayerPawn->OnTakeAnyDamage.RemoveDynamic(this, &ThisClass::HandleTimeOwnerDamaged);
		TimePlayerPawn = Pawn;
		if (Pawn) Pawn->OnTakeAnyDamage.AddUniqueDynamic(this, &ThisClass::HandleTimeOwnerDamaged);
		RequestTimeSpeed(1.f);
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (RequestedSpeed > 1.f && Now - LastSafetyCheck >= 0.1)
	{
		LastSafetyCheck = Now;
		if (Pawn)
		{
			if (const auto* Snapshot = Pawn->FindComponentByClass<UProjectRealtimeSnapshotComponent>())
			{
				float Health = 0.f, Maximum = 0.f;
				if (Snapshot->TryReadOwnerResource(TEXT("Health"), Health, Maximum))
				{
					if (LastAcceleratedHealth >= 0.f && Health < LastAcceleratedHealth - KINDA_SMALL_NUMBER)
					{
						HandleTimeOwnerDamaged(Pawn, LastAcceleratedHealth - Health, nullptr, nullptr, nullptr);
						return;
					}
					LastAcceleratedHealth = Health;
				}
			}
		}
		FText Reason;
		if (!CanFastForward(Reason))
		{
			RequestTimeSpeed(1.f);
			TimeControlMessage = Reason;
			if (auto* Feed = GetWorld()->GetSubsystem<UProjectActivityFeedSubsystem>()) Feed->AddSystemEntry(Reason);
		}
	}
}

void UProjectDayCycleSubsystem::HandleTimeOwnerDamaged(AActor*, float Damage, const UDamageType*, AController*, AActor*)
{
	if (Damage <= 0.f) return;
	DamageBlockUntil = GetWorld()->GetTimeSeconds() + 5.f;
	RequestTimeSpeed(1.f);
	TimeControlMessage = LOCTEXT("Damage", "Time acceleration stopped after taking damage.");
}

void UProjectDayCycleSubsystem::ShutdownTimeControl()
{
	WaitUntilCalendarHours = -1.0;
	CloseWaitMenu();
	NativePauseMenu = nullptr;
	if (TimeInputProcessor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(TimeInputProcessor);
	TimeInputProcessor.Reset();
	if (TimePlayerPawn.IsValid()) TimePlayerPawn->OnTakeAnyDamage.RemoveDynamic(this, &ThisClass::HandleTimeOwnerDamaged);
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
}

#undef LOCTEXT_NAMESPACE

static FAutoConsoleCommandWithWorldAndArgs ProjectTimeCommand(
	TEXT("Project.Time.Speed"), TEXT("Request 1, 2 or 4; pause toggles the player pause. Uses normal safety checks."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() != 1) return;
		if (auto* Clock = World->GetSubsystem<UProjectDayCycleSubsystem>())
		{
			if (Args[0].Equals(TEXT("pause"), ESearchCase::IgnoreCase)) Clock->TogglePlayerPause();
			else Clock->RequestTimeSpeed(FCString::Atof(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorld ProjectTimeReport(
	TEXT("Project.Time.Report"), TEXT("Report calendar, dilation and eligibility without changing time."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World) return;
		if (const auto* Clock = World->GetSubsystem<UProjectDayCycleSubsystem>())
		{
			FText Reason;
			const bool Allowed = Clock->CanFastForward(Reason);
			UE_LOG(LogTemp, Display, TEXT("[ProjectTime] hours=%.9f speed=%.1f paused=%d allowed=%d reason=%s"),
				Clock->GetCalendarHours(), UGameplayStatics::GetGlobalTimeDilation(World),
				UGameplayStatics::IsGamePaused(World), Allowed, *Reason.ToString());
			const auto* Native = World->GetSubsystem<UDaySequenceSubsystem>();
			const auto* Actor = Native ? Native->GetDaySequenceActor() : nullptr;
			UE_LOG(LogTemp, Display, TEXT("[ACFTime] shared=%d native=%.9f apparent=%.9f calendar=%.9f"),
				Actor && Actor == Clock->GetDayCycleStateActor(), Actor ? Actor->GetTimeOfDay() : -1.f,
				Actor ? Actor->GetApparentTimeOfDay() : -1.f, FMath::Fmod(Clock->GetCalendarHours(), 24.0));
			UE_LOG(LogTemp, Display, TEXT("[ProjectTimeMenus] native=%d wait=%d waiting=%d"),
				Clock->IsNativePauseMenuOpen(), Clock->IsWaitMenuOpen(), Clock->IsWaiting());
		}
	}));

#if !UE_BUILD_SHIPPING
// Bounded rendered QA enters the actual Slate input path, including preprocessors.
static FAutoConsoleCommandWithWorldAndArgs ProjectTimeKey(
	TEXT("Project.Time.Key"), TEXT("Development QA: inject one time/menu key through Slate."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld() || Args.Num() != 1 || !FSlateApplication::IsInitialized()) return;
		const FKey Key(*Args[0]);
		if (Key != EKeys::P && Key != EKeys::Hyphen && Key != EKeys::Equals && Key != EKeys::Apostrophe && Key != EKeys::F6 && Key != EKeys::F7 && Key != EKeys::F8 && Key != EKeys::Escape) return;
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		FSlateApplication::Get().ProcessKeyDownEvent(Event);
		FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}));

static FAutoConsoleCommandWithWorld ProjectTimeHud(
	TEXT("Project.Time.Hud"), TEXT("Show the existing Needs/Status and clock HUD for rendered time validation."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (World)
			if (auto* Needs = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>()) Needs->SetNeedsHudVisible(true);
	}));
#endif
