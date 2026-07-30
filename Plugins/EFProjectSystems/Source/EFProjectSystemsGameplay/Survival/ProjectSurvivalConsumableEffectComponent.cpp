#include "Survival/ProjectSurvivalConsumableEffectComponent.h"

#include "Survival/ProjectSurvivalLog.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

namespace
{
	constexpr float SurvivalConsumableEpsilon = KINDA_SMALL_NUMBER;

	FName ResolveConsumableSourceId(const FProjectSurvivalConsumableProfile& Profile, const UObject* SourceAsset)
	{
		if (!Profile.SourceId.IsNone())
		{
			return Profile.SourceId;
		}

		if (SourceAsset)
		{
			return FName(*SourceAsset->GetName());
		}

		return TEXT("SurvivalConsumable");
	}
}

UProjectSurvivalConsumableEffectComponent::UProjectSurvivalConsumableEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	NextTimedEffectInstanceId = 1;
}

void UProjectSurvivalConsumableEffectComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveNeedsComponent();
}

void UProjectSurvivalConsumableEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTimedEffects();

	Super::EndPlay(EndPlayReason);
}

bool UProjectSurvivalConsumableEffectComponent::ApplySurvivalConsumableProfile(const FProjectSurvivalConsumableProfile& Profile, UObject* SourceAsset)
{
	UProjectSurvivalNeedsComponent* NeedsComponent = ResolveNeedsComponent();
	if (!NeedsComponent)
	{
		UE_LOG(
			LogProjectSurvival,
			Warning,
			TEXT("[ProjectSurvivalConsumable] Cannot apply profile from %s because no UProjectSurvivalNeedsComponent was found on %s"),
			*GetNameSafe(SourceAsset),
			*GetNameSafe(GetOwner()));
		return false;
	}

	FProjectSurvivalConsumableProfile ResolvedProfile = Profile;
	ResolvedProfile.SourceId = ResolveConsumableSourceId(Profile, SourceAsset);

	const bool bAppliedImmediate = ApplyImmediateDeltas(NeedsComponent, ResolvedProfile);

	FActiveTimedEffect TimedEffect;
	if (!BuildTimedEffect(ResolvedProfile, SourceAsset, TimedEffect))
	{
		return bAppliedImmediate;
	}

	const bool bRegisteredTimed = RegisterTimedEffect(MoveTemp(TimedEffect), ResolvedProfile.StackPolicy);
	return bAppliedImmediate || bRegisteredTimed;
}

int32 UProjectSurvivalConsumableEffectComponent::GetActiveTimedEffectCount() const
{
	return ActiveTimedEffects.Num();
}

UProjectSurvivalNeedsComponent* UProjectSurvivalConsumableEffectComponent::ResolveNeedsComponent()
{
	if (CachedNeedsComponent && CachedNeedsComponent->IsRegistered())
	{
		return CachedNeedsComponent;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		CachedNeedsComponent = nullptr;
		return nullptr;
	}

	CachedNeedsComponent = OwnerPawn->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	return CachedNeedsComponent;
}

bool UProjectSurvivalConsumableEffectComponent::ApplyImmediateDeltas(UProjectSurvivalNeedsComponent* NeedsComponent, const FProjectSurvivalConsumableProfile& Profile) const
{
	if (!NeedsComponent)
	{
		return false;
	}

	FProjectSurvivalConsumableEffect ImmediateEffect;
	ImmediateEffect.SourceId = Profile.SourceId;
	ImmediateEffect.bClampToRange = Profile.bClampToRange;

	for (const FProjectSurvivalNeedDelta& Delta : Profile.NeedDeltas)
	{
		if (Delta.EntryName.IsNone() || FMath::IsNearlyZero(Delta.DeltaValue))
		{
			continue;
		}

		ImmediateEffect.NeedDeltas.Add(Delta);
	}

	for (const FProjectSurvivalNeedDelta& Delta : Profile.SensationDeltas)
	{
		if (Delta.EntryName.IsNone() || FMath::IsNearlyZero(Delta.DeltaValue))
		{
			continue;
		}

		ImmediateEffect.SensationDeltas.Add(Delta);
	}

	if (ImmediateEffect.NeedDeltas.Num() == 0 && ImmediateEffect.SensationDeltas.Num() == 0)
	{
		return false;
	}

	NeedsComponent->ApplyConsumableEffect(ImmediateEffect);
	UE_LOG(
		LogProjectSurvival,
		Log,
		TEXT("[ProjectSurvivalConsumable] Applied instant profile SourceId=%s NeedDeltas=%d SensationDeltas=%d Owner=%s"),
		*Profile.SourceId.ToString(),
		ImmediateEffect.NeedDeltas.Num(),
		ImmediateEffect.SensationDeltas.Num(),
		*GetNameSafe(GetOwner()));
	return true;
}

bool UProjectSurvivalConsumableEffectComponent::BuildTimedEffect(
	const FProjectSurvivalConsumableProfile& Profile,
	UObject* SourceAsset,
	FActiveTimedEffect& OutTimedEffect) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	OutTimedEffect = FActiveTimedEffect();
	OutTimedEffect.SourceId = Profile.SourceId;
	OutTimedEffect.SourceName = GetNameSafe(SourceAsset);
	OutTimedEffect.bClampToRange = Profile.bClampToRange;

	const float StartTimeSeconds = World->GetTimeSeconds();

	for (const FProjectSurvivalTimedNeedDelta& Delta : Profile.TimedNeedDeltas)
	{
		if (Delta.EntryName.IsNone() || FMath::IsNearlyZero(Delta.TotalDelta))
		{
			continue;
		}

		if (Delta.DurationSeconds <= 0.f || Delta.TickIntervalSeconds <= 0.f)
		{
			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumable] Ignoring invalid timed need delta SourceId=%s Entry=%s Duration=%0.3f TickInterval=%0.3f"),
				*Profile.SourceId.ToString(),
				*Delta.EntryName.ToString(),
				Delta.DurationSeconds,
				Delta.TickIntervalSeconds);
			continue;
		}

		FActiveTimedChannel& Channel = OutTimedEffect.Channels.AddDefaulted_GetRef();
		Channel.EntryName = Delta.EntryName;
		Channel.TotalDelta = Delta.TotalDelta;
		Channel.DurationSeconds = Delta.DurationSeconds;
		Channel.TickIntervalSeconds = Delta.TickIntervalSeconds;
		Channel.StartTimeSeconds = StartTimeSeconds;
		Channel.NextTickElapsedSeconds = FMath::Min(Delta.TickIntervalSeconds, Delta.DurationSeconds);
		Channel.bIsSensation = false;
	}

	for (const FProjectSurvivalTimedSensationDelta& Delta : Profile.TimedSensationDeltas)
	{
		if (Delta.EntryName.IsNone() || FMath::IsNearlyZero(Delta.TotalDelta))
		{
			continue;
		}

		if (Delta.DurationSeconds <= 0.f || Delta.TickIntervalSeconds <= 0.f)
		{
			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumable] Ignoring invalid timed sensation delta SourceId=%s Entry=%s Duration=%0.3f TickInterval=%0.3f"),
				*Profile.SourceId.ToString(),
				*Delta.EntryName.ToString(),
				Delta.DurationSeconds,
				Delta.TickIntervalSeconds);
			continue;
		}

		FActiveTimedChannel& Channel = OutTimedEffect.Channels.AddDefaulted_GetRef();
		Channel.EntryName = Delta.EntryName;
		Channel.TotalDelta = Delta.TotalDelta;
		Channel.DurationSeconds = Delta.DurationSeconds;
		Channel.TickIntervalSeconds = Delta.TickIntervalSeconds;
		Channel.StartTimeSeconds = StartTimeSeconds;
		Channel.NextTickElapsedSeconds = FMath::Min(Delta.TickIntervalSeconds, Delta.DurationSeconds);
		Channel.bIsSensation = true;
	}

	if (OutTimedEffect.Channels.Num() == 0)
	{
		return false;
	}

	return true;
}

bool UProjectSurvivalConsumableEffectComponent::RegisterTimedEffect(
	FActiveTimedEffect&& TimedEffect,
	EProjectSurvivalConsumableStackPolicy StackPolicy)
{
	if (TimedEffect.SourceId.IsNone())
	{
		return false;
	}

	const bool bAlreadyActive = ActiveTimedEffects.ContainsByPredicate(
		[&TimedEffect](const FActiveTimedEffect& ExistingEffect)
		{
			return ExistingEffect.SourceId == TimedEffect.SourceId;
		});

	if (StackPolicy == EProjectSurvivalConsumableStackPolicy::IgnoreIfActive && bAlreadyActive)
	{
		UE_LOG(
			LogProjectSurvival,
			Log,
			TEXT("[ProjectSurvivalConsumable] Skipping timed profile SourceId=%s because an active effect already exists"),
			*TimedEffect.SourceId.ToString());
		return false;
	}

	if (StackPolicy == EProjectSurvivalConsumableStackPolicy::RefreshDuration)
	{
		const int32 RemovedCount = RemoveTimedEffectsBySourceId(TimedEffect.SourceId);
		if (RemovedCount > 0)
		{
			UE_LOG(
				LogProjectSurvival,
				Log,
				TEXT("[ProjectSurvivalConsumable] Refreshing timed profile SourceId=%s removed=%d"),
				*TimedEffect.SourceId.ToString(),
				RemovedCount);
		}
	}

	TimedEffect.InstanceId = NextTimedEffectInstanceId++;
	UE_LOG(
		LogProjectSurvival,
		Log,
		TEXT("[ProjectSurvivalConsumable] Activated timed profile SourceId=%s InstanceId=%d Channels=%d Owner=%s"),
		*TimedEffect.SourceId.ToString(),
		TimedEffect.InstanceId,
		TimedEffect.Channels.Num(),
		*GetNameSafe(GetOwner()));

	ActiveTimedEffects.Add(MoveTemp(TimedEffect));
	ScheduleNextTimedTick();
	return true;
}

void UProjectSurvivalConsumableEffectComponent::HandleTimedEffectsTick()
{
	UWorld* World = GetWorld();
	UProjectSurvivalNeedsComponent* NeedsComponent = ResolveNeedsComponent();
	if (!World || !NeedsComponent)
	{
		ClearTimedEffects();
		return;
	}

	const float CurrentWorldTimeSeconds = World->GetTimeSeconds();

	for (int32 EffectIndex = ActiveTimedEffects.Num() - 1; EffectIndex >= 0; --EffectIndex)
	{
		FActiveTimedEffect& Effect = ActiveTimedEffects[EffectIndex];
		for (int32 ChannelIndex = Effect.Channels.Num() - 1; ChannelIndex >= 0; --ChannelIndex)
		{
			FActiveTimedChannel& Channel = Effect.Channels[ChannelIndex];
			AdvanceTimedChannel(NeedsComponent, Effect, Channel, CurrentWorldTimeSeconds);
			if (IsTimedChannelComplete(Channel))
			{
				Effect.Channels.RemoveAt(ChannelIndex);
			}
		}

		if (Effect.Channels.Num() == 0)
		{
			UE_LOG(
				LogProjectSurvival,
				Log,
				TEXT("[ProjectSurvivalConsumable] Finished timed profile SourceId=%s InstanceId=%d Owner=%s"),
				*Effect.SourceId.ToString(),
				Effect.InstanceId,
				*GetNameSafe(GetOwner()));
			ActiveTimedEffects.RemoveAt(EffectIndex);
		}
	}

	ScheduleNextTimedTick();
}

void UProjectSurvivalConsumableEffectComponent::ScheduleNextTimedTick()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	TimerManager.ClearTimer(TimedEffectTimerHandle);

	if (ActiveTimedEffects.Num() == 0)
	{
		return;
	}

	const float CurrentWorldTimeSeconds = World->GetTimeSeconds();
	float NextDelaySeconds = TNumericLimits<float>::Max();

	for (const FActiveTimedEffect& Effect : ActiveTimedEffects)
	{
		for (const FActiveTimedChannel& Channel : Effect.Channels)
		{
			if (IsTimedChannelComplete(Channel))
			{
				continue;
			}

			const float NextTickWorldTimeSeconds = Channel.StartTimeSeconds + Channel.NextTickElapsedSeconds;
			NextDelaySeconds = FMath::Min(NextDelaySeconds, FMath::Max(0.f, NextTickWorldTimeSeconds - CurrentWorldTimeSeconds));
		}
	}

	if (!FMath::IsFinite(NextDelaySeconds) || NextDelaySeconds == TNumericLimits<float>::Max())
	{
		return;
	}

	if (NextDelaySeconds <= SurvivalConsumableEpsilon)
	{
		TimerManager.SetTimerForNextTick(this, &ThisClass::HandleTimedEffectsTick);
		return;
	}

	TimerManager.SetTimer(TimedEffectTimerHandle, this, &ThisClass::HandleTimedEffectsTick, NextDelaySeconds, false);
}

void UProjectSurvivalConsumableEffectComponent::ClearTimedEffects()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimedEffectTimerHandle);
	}

	ActiveTimedEffects.Reset();
}

int32 UProjectSurvivalConsumableEffectComponent::RemoveTimedEffectsBySourceId(FName SourceId)
{
	const int32 InitialCount = ActiveTimedEffects.Num();
	ActiveTimedEffects.RemoveAll(
		[SourceId](const FActiveTimedEffect& Effect)
		{
			return Effect.SourceId == SourceId;
		});
	return InitialCount - ActiveTimedEffects.Num();
}

bool UProjectSurvivalConsumableEffectComponent::IsTimedChannelComplete(const FActiveTimedChannel& Channel) const
{
	const float Tolerance = FMath::Max(0.001f, FMath::Abs(Channel.TotalDelta) * 0.001f);
	return Channel.NextTickElapsedSeconds >= Channel.DurationSeconds
		&& FMath::Abs(Channel.ScheduledAppliedDelta - Channel.TotalDelta) <= Tolerance;
}

void UProjectSurvivalConsumableEffectComponent::AdvanceTimedChannel(
	UProjectSurvivalNeedsComponent* NeedsComponent,
	FActiveTimedEffect& Effect,
	FActiveTimedChannel& Channel,
	float CurrentWorldTimeSeconds)
{
	if (!NeedsComponent || Channel.EntryName.IsNone())
	{
		Channel.ScheduledAppliedDelta = Channel.TotalDelta;
		Channel.NextTickElapsedSeconds = Channel.DurationSeconds;
		return;
	}

	const float ElapsedSeconds = FMath::Clamp(CurrentWorldTimeSeconds - Channel.StartTimeSeconds, 0.f, Channel.DurationSeconds);
	while (Channel.NextTickElapsedSeconds <= ElapsedSeconds + SurvivalConsumableEpsilon)
	{
		const float TickElapsedSeconds = FMath::Min(Channel.NextTickElapsedSeconds, Channel.DurationSeconds);
		const float TargetScheduledDelta = Channel.TotalDelta * (TickElapsedSeconds / Channel.DurationSeconds);
		const float TickDelta = TargetScheduledDelta - Channel.ScheduledAppliedDelta;
		Channel.ScheduledAppliedDelta = TargetScheduledDelta;

		float ActualAppliedDelta = 0.f;
		if (!FMath::IsNearlyZero(TickDelta))
		{
			ActualAppliedDelta = Channel.bIsSensation
				? NeedsComponent->ApplySensationDeltaValue(Channel.EntryName, TickDelta, true, Effect.bClampToRange)
				: NeedsComponent->ApplyNeedDeltaValue(Channel.EntryName, TickDelta, true, Effect.bClampToRange);
			Channel.ActualAppliedDelta += ActualAppliedDelta;

			UE_LOG(
				LogProjectSurvival,
				Log,
				TEXT("[ProjectSurvivalConsumable] Tick SourceId=%s InstanceId=%d Entry=%s Mode=%s Requested=%0.3f Applied=%0.3f Elapsed=%0.3f/%0.3f"),
				*Effect.SourceId.ToString(),
				Effect.InstanceId,
				*Channel.EntryName.ToString(),
				Channel.bIsSensation ? TEXT("Sensation") : TEXT("Need"),
				TickDelta,
				ActualAppliedDelta,
				TickElapsedSeconds,
				Channel.DurationSeconds);
		}

		if (TickElapsedSeconds >= Channel.DurationSeconds - SurvivalConsumableEpsilon)
		{
			Channel.NextTickElapsedSeconds = Channel.DurationSeconds;
			break;
		}

		Channel.NextTickElapsedSeconds = FMath::Min(Channel.NextTickElapsedSeconds + Channel.TickIntervalSeconds, Channel.DurationSeconds);
	}
}
