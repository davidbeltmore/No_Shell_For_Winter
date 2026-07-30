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
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override;
	virtual bool IsTickableWhenPaused() const override;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	FProjectDayCycleSnapshot GetCurrentSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	class AProjectDayCycleStateActor* GetDayCycleStateActor() const;

private:
	void ResolveOrCreateStateActor();
	void ResolveLocalPlayerController();
	void EnsureHudWidget();
	void RefreshHud();
	void RemoveHudWidget();

	UPROPERTY(Transient)
	TObjectPtr<class AProjectDayCycleStateActor> StateActor;

	UPROPERTY(Transient)
	TObjectPtr<class APlayerController> LocalPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectDayCycleWidget> DayCycleWidget;

	float HudRefreshAccumulator = 0.0f;
};
