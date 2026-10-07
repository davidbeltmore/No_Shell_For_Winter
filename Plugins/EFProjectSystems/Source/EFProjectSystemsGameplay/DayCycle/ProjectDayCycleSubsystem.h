#pragma once

#include "CoreMinimal.h"
#include "DayCycle/ProjectDayCycleTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectDayCycleSubsystem.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDayCycleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override;
	virtual bool IsTickableWhenPaused() const override;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	FProjectDayCycleSnapshot GetCurrentSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	class AProjectDayCycleStateActor* GetDayCycleStateActor() const;

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool RequestTimeSpeed(float Speed);

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool TogglePlayerPause();

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool StepTimeSpeed(int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool ToggleNativePauseMenu();

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool ToggleWaitMenu();

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool ConfirmWait(float CalendarHours);

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	void CancelWait();

	UFUNCTION(BlueprintPure, Category = "Time Control")
	bool IsWaiting() const { return WaitUntilCalendarHours >= 0.0; }

	UFUNCTION(BlueprintPure, Category = "Time Control")
	bool IsNativePauseMenuOpen() const;

	UFUNCTION(BlueprintPure, Category = "Time Control")
	bool IsWaitMenuOpen() const;

	void CloseWaitMenu();

	// Named owners prevent closing a modal from releasing another owner's pause.
	UFUNCTION(BlueprintCallable, Category = "Time Control")
	bool SetPauseReason(FName Reason, bool bPause);

	UFUNCTION(BlueprintPure, Category = "Time Control")
	bool CanFastForward(FText& OutReason) const;

	UFUNCTION(BlueprintPure, Category = "Time Control")
	double GetCalendarHours() const;

	bool RouteTimeKey(const struct FKeyEvent& Event);
	bool ShouldBlockPausedGameplayInput() const;
	bool HasTimeInputFocus() const;
	static bool IsSupportedSpeed(float Speed);
	static double SimulationSecondsToCalendarHours(double Seconds, double DaySeconds);

private:
	bool CanFastForwardInternal(FText& OutReason, bool bIgnorePlayerPause) const;
	bool IsTimeMenuOpen() const;
	double WaitUntilCalendarHours = -1.0;
	UPROPERTY(Transient)
	TObjectPtr<class UCommonActivatableWidget> NativePauseMenu;
	UPROPERTY(Transient)
	TObjectPtr<class UProjectWaitWidget> WaitMenu;
	void ResolveOrCreateStateActor();
	void ResolveLocalPlayerController();
	void EnsureHudWidget();
	void RefreshHud();
	void RemoveHudWidget();
	void UpdateTimeControl();
	void ShutdownTimeControl();
	UFUNCTION()
	void HandleTimeOwnerDamaged(AActor* Actor, float Damage, const class UDamageType* Type, class AController* Instigator, AActor* Causer);

	TSharedPtr<class IInputProcessor> TimeInputProcessor;
	TSet<FName> PauseReasons;
	TWeakObjectPtr<class APawn> TimePlayerPawn;
	float RequestedSpeed = 1.f;
	bool bOwnsEnginePause = false;
	double LastSafetyCheck = -1.0;
	float DamageBlockUntil = 0.f;
	float LastAcceleratedHealth = -1.f;
	FText TimeControlMessage;

	UPROPERTY(Transient)
	TObjectPtr<class AProjectDayCycleStateActor> StateActor;

	UPROPERTY(Transient)
	TObjectPtr<class APlayerController> LocalPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectDayCycleWidget> DayCycleWidget;

	float HudRefreshAccumulator = 0.0f;
};
