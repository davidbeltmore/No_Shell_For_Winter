#include "DayCycle/ProjectDayCycleSubsystem.h"

#include "DayCycle/ProjectWaitWidget.h"
#include "ANSUIPlayerSubsystem.h"
#include "CommonActivatableWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ProjectActivityFeedSubsystem.h"

bool UProjectDayCycleSubsystem::IsTimeMenuOpen() const
{
	return IsNativePauseMenuOpen() || IsWaitMenuOpen();
}

bool UProjectDayCycleSubsystem::IsNativePauseMenuOpen() const
{
	return NativePauseMenu && NativePauseMenu->IsActivated();
}

bool UProjectDayCycleSubsystem::IsWaitMenuOpen() const
{
	return WaitMenu && WaitMenu->IsActivated();
}

bool UProjectDayCycleSubsystem::StepTimeSpeed(int32 Direction)
{
	if (!GetWorld() || IsTimeMenuOpen() || Direction == 0) return false;
	if (UGameplayStatics::IsGamePaused(GetWorld()))
	{
		if (PauseReasons.Num() != 1 || !PauseReasons.Contains(TEXT("Player"))) return false;
		return Direction < 0 || SetPauseReason(TEXT("Player"), false);
	}
	const float Current = RequestedSpeed;
	if (IsWaiting()) CancelWait();
	if (Direction < 0 && Current <= 1.f) return SetPauseReason(TEXT("Player"), true);
	return RequestTimeSpeed(Direction > 0 ? (Current < 2.f ? 2.f : 4.f) : (Current > 2.f ? 2.f : 1.f));
}

bool UProjectDayCycleSubsystem::ToggleNativePauseMenu()
{
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	auto* UI = GI ? GI->GetSubsystem<UANSUIPlayerSubsystem>() : nullptr;
	if (!UI || !LocalPlayerController) return false;
	if (NativePauseMenu && NativePauseMenu->IsActivated())
	{
		UI->RemoveInGameWidget(NativePauseMenu);
		NativePauseMenu = nullptr;
		return true;
	}
	CloseWaitMenu();
	CancelWait();
	// Use the existing ACF page and its native CommonUI stack, buttons and lifecycle.
	const auto Class = LoadClass<UCommonActivatableWidget>(nullptr, TEXT("/Game/FullSample/Integrations/SharedAssets/ACF_PauseMenu_WB.ACF_PauseMenu_WB_C"));
	NativePauseMenu = Cast<UCommonActivatableWidget>(UI->SpawnInGameWidget(Class, true, true));
	return NativePauseMenu != nullptr;
}

bool UProjectDayCycleSubsystem::ToggleWaitMenu()
{
	if (WaitMenu && WaitMenu->IsActivated()) { CloseWaitMenu(); return true; }
	if (IsWaiting()) { CancelWait(); return true; }
	FText Reason;
	if (!CanFastForwardInternal(Reason, true))
	{
		if (auto* Feed = GetWorld()->GetSubsystem<UProjectActivityFeedSubsystem>()) Feed->AddSystemEntry(Reason);
		return false;
	}
	UGameInstance* GI = GetWorld()->GetGameInstance();
	auto* UI = GI ? GI->GetSubsystem<UANSUIPlayerSubsystem>() : nullptr;
	if (!UI) return false;
	RequestTimeSpeed(1.f);
	WaitMenu = Cast<UProjectWaitWidget>(UI->SpawnInGameWidget(UProjectWaitWidget::StaticClass(), true, true));
	return WaitMenu != nullptr;
}

void UProjectDayCycleSubsystem::CloseWaitMenu()
{
	if (WaitMenu && WaitMenu->IsActivated())
	{
		UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (auto* UI = GI ? GI->GetSubsystem<UANSUIPlayerSubsystem>() : nullptr) UI->RemoveInGameWidget(WaitMenu);
	}
	WaitMenu = nullptr;
}

bool UProjectDayCycleSubsystem::ConfirmWait(float CalendarHours)
{
	if (!FMath::IsFinite(CalendarHours) || CalendarHours <= 0.f || CalendarHours > 24.f || !WaitMenu || !WaitMenu->IsActivated()) return false;
	CloseWaitMenu();
	const bool bWasPlayerPaused = PauseReasons.Contains(TEXT("Player"));
	if (bWasPlayerPaused) SetPauseReason(TEXT("Player"), false);
	if (!RequestTimeSpeed(4.f))
	{
		if (bWasPlayerPaused) SetPauseReason(TEXT("Player"), true);
		return false;
	}
	WaitUntilCalendarHours = GetCalendarHours() + CalendarHours;
	return true;
}

void UProjectDayCycleSubsystem::CancelWait()
{
	WaitUntilCalendarHours = -1.0;
	RequestTimeSpeed(1.f);
}
