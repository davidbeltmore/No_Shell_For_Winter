#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "ProjectDefeatBlueprintBridgeComponent.generated.h"

class UProjectDefeatFlowComponent;
class UProjectSurvivalNeedsComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectDefeatBridgeSignalSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectDefeatPublicStateChangedSignature, EProjectDefeatPublicState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProjectDefeatPointsChangedSignature, float, CurrentPoints, float, MaxPoints, float, NormalizedPoints);

UCLASS(ClassGroup = (Defeat), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDefeatBlueprintBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectDefeatBlueprintBridgeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	EProjectDefeatPublicState GetPublicState() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	float GetDefeatPointsNormalized() const;

	bool TryStartExternalRuntimeScene(const FProjectDefeatTransferPayload& Payload);
	bool TryStopExternalRuntimeScene(const FProjectDefeatTransferPayload& Payload, bool bCancelledByPlayer);

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatBridgeSignalSignature OnInjured;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatBridgeSignalSignature OnDowned;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatBridgeSignalSignature OnDefeated;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatPublicStateChangedSignature OnDefeatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatPointsChangedSignature OnDefeatPointsChanged;

private:
	UFUNCTION()
	void HandleSurvivalValueChanged(FName EntryName, float OldValue, float NewValue, float MaxValue, bool bIsSensation);

	UFUNCTION()
	void HandleKnockoutStateChanged(EProjectDefeatPhase NewPhase, EProjectKnockoutReason KnockoutReason, bool bActive);

	UFUNCTION()
	void HandleDefeatedSceneChanged(const FProjectDefeatSceneDefinition& SceneDefinition, bool bActive);

	UFUNCTION()
	void HandleDefeatStateRefreshed();

	void RefreshDependencies();
	void BindDelegates();
	void UnbindDelegates();
	void UpdateReactiveTickState();
	void SyncPublicState(bool bBroadcastIfChanged);
	void EmitDefeatPointsChanged();
	void RefreshExternalTargets();
	void PushExternalPublicState(EProjectDefeatPublicState NewState);
	UObject* FindExternalObject(const TArray<FString>& ClassHints, const TArray<FName>& CallableNames) const;
	bool TryInvokeNoArgFunction(UObject* Target, FName FunctionName) const;
	bool TryInvokeBoolFunction(UObject* Target, FName FunctionName, bool bValue) const;
	bool TryInvokeStateFunction(UObject* Target, FName FunctionName, EProjectDefeatPublicState State) const;
	bool TryInvokeSceneStartFunction(UObject* Target, FName FunctionName, FName SceneId) const;
	bool TryInvokeSceneStopFunction(UObject* Target, FName FunctionName, bool bCancelledByPlayer) const;
	bool TryBroadcastNoArgDelegate(UObject* Target, FName DelegateName) const;
	bool TryBroadcastStateDelegate(UObject* Target, FName DelegateName, EProjectDefeatPublicState State) const;
	bool TryBroadcastExternalDefeatPoints(int32 Points) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectDefeatFlowComponent> DefeatFlowComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> NeedsComponent;

	TWeakObjectPtr<UObject> ExternalPlayerEventsTarget;
	TWeakObjectPtr<UObject> ExternalStateHandlerTarget;
	TWeakObjectPtr<UObject> ExternalSceneTarget;
	EProjectDefeatPublicState CurrentPublicState = EProjectDefeatPublicState::Normal;
	float CachedPainCurrent = 0.f;
	float CachedPainThreshold = 100.f;
	bool bExternalScenePrepared = false;
	bool bExternalSceneActive = false;
};
