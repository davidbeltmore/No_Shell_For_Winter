#pragma once

#include "Components/ActorComponent.h"
#include "ProjectSurvivalNeedsTypes.h"
#include "ProjectSurvivalConsumableEffectComponent.generated.h"

class UProjectSurvivalNeedsComponent;

UCLASS(ClassGroup = (Survival), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalConsumableEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectSurvivalConsumableEffectComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|Consumables")
	bool ApplySurvivalConsumableProfile(const FProjectSurvivalConsumableProfile& Profile, UObject* SourceAsset);

	UFUNCTION(BlueprintPure, Category = "Survival|Consumables")
	int32 GetActiveTimedEffectCount() const;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> CachedNeedsComponent;

private:
	struct FActiveTimedChannel
	{
		FName EntryName = NAME_None;
		float TotalDelta = 0.f;
		float ScheduledAppliedDelta = 0.f;
		float ActualAppliedDelta = 0.f;
		float DurationSeconds = 0.f;
		float TickIntervalSeconds = 0.f;
		float StartTimeSeconds = 0.f;
		float NextTickElapsedSeconds = 0.f;
		bool bIsSensation = false;
	};

	struct FActiveTimedEffect
	{
		FName SourceId = NAME_None;
		FString SourceName;
		int32 InstanceId = INDEX_NONE;
		bool bClampToRange = true;
		TArray<FActiveTimedChannel> Channels;
	};

	UProjectSurvivalNeedsComponent* ResolveNeedsComponent();
	bool ApplyImmediateDeltas(UProjectSurvivalNeedsComponent* NeedsComponent, const FProjectSurvivalConsumableProfile& Profile) const;
	bool BuildTimedEffect(const FProjectSurvivalConsumableProfile& Profile, UObject* SourceAsset, FActiveTimedEffect& OutTimedEffect) const;
	bool RegisterTimedEffect(FActiveTimedEffect&& TimedEffect, EProjectSurvivalConsumableStackPolicy StackPolicy);
	void HandleTimedEffectsTick();
	void ScheduleNextTimedTick();
	void ClearTimedEffects();
	int32 RemoveTimedEffectsBySourceId(FName SourceId);
	bool IsTimedChannelComplete(const FActiveTimedChannel& Channel) const;
	void AdvanceTimedChannel(UProjectSurvivalNeedsComponent* NeedsComponent, FActiveTimedEffect& Effect, FActiveTimedChannel& Channel, float CurrentWorldTimeSeconds);

	TArray<FActiveTimedEffect> ActiveTimedEffects;
	FTimerHandle TimedEffectTimerHandle;
	int32 NextTimedEffectInstanceId;
};
