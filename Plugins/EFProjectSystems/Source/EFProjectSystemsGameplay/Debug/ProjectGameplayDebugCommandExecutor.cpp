#include "Debug/ProjectGameplayDebugCommandExecutor.h"

#include "Combat/ProjectCombatAttributeComponent.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RuntimePerformance/ProjectRuntimePerformanceSubsystem.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectSurvivalStatusSettings.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectGameplayDebugCommands, Log, All);

namespace ProjectGameplayDebugCommandExecutorPrivate
{
	const FName HealthName(TEXT("Health"));
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName MadnessName(TEXT("Madness"));
	const FName CurseName(TEXT("Curse"));
	const FName PainName(TEXT("Pain"));

	UProjectSurvivalNeedsComponent* FindNeedsComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>() : nullptr;
	}

	UProjectSurvivalStatusComponent* FindStatusComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectSurvivalStatusComponent>() : nullptr;
	}

	UProjectInnerDoctrineComponent* FindInnerDoctrineComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>() : nullptr;
	}

	UProjectCombatAttributeComponent* FindCombatAttributeComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>() : nullptr;
	}

	UProjectRealtimeSnapshotComponent* FindRealtimeSnapshotComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectRealtimeSnapshotComponent>() : nullptr;
	}

	UProjectDefaultTattooSkinnedDecalSubsystem* FindAutomaticTattooSubsystem(AActor* OwnerActor)
	{
		UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	}

	bool TryFindAutomaticTattooSnapshot(
		AActor* OwnerActor,
		const FName RowName,
		FProjectAutomaticTattooRuntimeDebugSnapshot& OutSnapshot)
	{
		UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem = FindAutomaticTattooSubsystem(OwnerActor);
		if (!TattooSubsystem || RowName.IsNone())
		{
			return false;
		}

		TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> Snapshots;
		TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshots(Snapshots);
		for (const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot : Snapshots)
		{
			if (Snapshot.RowName == RowName)
			{
				OutSnapshot = Snapshot;
				return true;
			}
		}

		return false;
	}

	FString BuildAutomaticTattooStateText(const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot)
	{
		if (Snapshot.bForcedActiveForDebug)
		{
			return TEXT("Forced");
		}
		return Snapshot.bActive ? TEXT("Active") : TEXT("Locked");
	}
}

bool FProjectGameplayDebugCommandExecutor::TriggerImmediateDefeat(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UProjectDefeatFlowComponent* DefeatFlowComponent = OwnerActor
		? OwnerActor->FindComponentByClass<UProjectDefeatFlowComponent>()
		: nullptr;
	return DefeatFlowComponent && DefeatFlowComponent->DebugTriggerImmediateDefeat(TEXT("GameplayDebug.ImmediateDefeat"));
#endif
}

bool FProjectGameplayDebugCommandExecutor::TriggerDownedMode(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UProjectDefeatFlowComponent* DefeatFlowComponent = OwnerActor
		? OwnerActor->FindComponentByClass<UProjectDefeatFlowComponent>()
		: nullptr;
	return DefeatFlowComponent
		&& DefeatFlowComponent->RequestKnockoutOrPendingCrawl(
			EProjectKnockoutReason::DebugForced,
			TEXT("GameplayDebug.DownedMode"));
#endif
}

bool FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	if (!OwnerActor)
	{
		return false;
	}

	if (UProjectRealtimeSnapshotComponent* RealtimeSnapshotComponent = FindRealtimeSnapshotComponent(OwnerActor))
	{
		float CurrentHealth = 0.f;
		float MaxHealth = 0.f;
		if (RealtimeSnapshotComponent->TryReadOwnerResource(HealthName, CurrentHealth, MaxHealth) && MaxHealth > KINDA_SMALL_NUMBER)
		{
			if (CurrentHealth >= MaxHealth - KINDA_SMALL_NUMBER)
			{
				return true;
			}

			const float AppliedDelta = RealtimeSnapshotComponent->ApplyOwnerResourceDelta(HealthName, MaxHealth - CurrentHealth, true);
			if (AppliedDelta > KINDA_SMALL_NUMBER)
			{
				return true;
			}
		}
	}

	if (UProjectCombatAttributeComponent* CombatAttributeComponent = FindCombatAttributeComponent(OwnerActor))
	{
		const FName HealthAttributeName = CombatAttributeComponent->HealthAttributeName;
		if (!HealthAttributeName.IsNone() && CombatAttributeComponent->HasAttribute(HealthAttributeName))
		{
			const float MaxHealth = FMath::Max(CombatAttributeComponent->GetAttributeMaxValue(HealthAttributeName), 0.f);
			if (MaxHealth <= KINDA_SMALL_NUMBER)
			{
				return false;
			}

			CombatAttributeComponent->SetAttributeRecoveryBlocked(HealthAttributeName, false);
			if (CombatAttributeComponent->GetAttributeCurrentValue(HealthAttributeName) >= MaxHealth - KINDA_SMALL_NUMBER)
			{
				return true;
			}

			return CombatAttributeComponent->SetAttributeCurrentValue(HealthAttributeName, MaxHealth);
		}
	}

	return false;
#endif
}

bool FProjectGameplayDebugCommandExecutor::RestoreNeedsAndSensations(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	UProjectSurvivalNeedsComponent* NeedsComponent = FindNeedsComponent(OwnerActor);
	if (!NeedsComponent)
	{
		return false;
	}

	bool bApplied = false;
	bApplied |= NeedsComponent->SetNeedCurrentValue(HungerName, NeedsComponent->GetNeedMaxValue(HungerName), true);
	bApplied |= NeedsComponent->SetNeedCurrentValue(ThirstName, NeedsComponent->GetNeedMaxValue(ThirstName), true);
	bApplied |= NeedsComponent->SetNeedCurrentValue(SleepName, NeedsComponent->GetNeedMaxValue(SleepName), true);
	bApplied |= NeedsComponent->SetSensationCurrentValue(MadnessName, 0.f, true);
	if (UProjectInnerDoctrineComponent* DoctrineComponent = FindInnerDoctrineComponent(OwnerActor))
	{
		bApplied |= DoctrineComponent->CleanseCurse(DoctrineComponent->GetCurseMax()) > 0.f;
	}
	bApplied |= NeedsComponent->SetSensationCurrentValue(PainName, 0.f, true);
	return bApplied;
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetSensationToMax(AActor* OwnerActor, const FName SensationName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)SensationName;
	return false;
#else
	return SetSensationToPercent(OwnerActor, SensationName, 1.f);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetNeedToZero(AActor* OwnerActor, const FName NeedName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)NeedName;
	return false;
#else
	return SetNeedToPercent(OwnerActor, NeedName, 0.f);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(
	AActor* OwnerActor,
	const FName EntryName,
	const float Percent)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)EntryName;
	(void)Percent;
	return false;
#else
	if (EntryName == ProjectGameplayDebugCommandExecutorPrivate::CurseName)
	{
		return SetSensationToPercent(OwnerActor, EntryName, Percent);
	}

	UProjectSurvivalNeedsComponent* NeedsComponent = ProjectGameplayDebugCommandExecutorPrivate::FindNeedsComponent(OwnerActor);
	if (!NeedsComponent || EntryName.IsNone())
	{
		return false;
	}

	if (NeedsComponent->HasNeed(EntryName))
	{
		return SetNeedToPercent(OwnerActor, EntryName, Percent);
	}

	if (NeedsComponent->HasSensation(EntryName))
	{
		return SetSensationToPercent(OwnerActor, EntryName, Percent);
	}

	return false;
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetNeedToPercent(AActor* OwnerActor, const FName NeedName, const float Percent)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)NeedName;
	(void)Percent;
	return false;
#else
	UProjectSurvivalNeedsComponent* NeedsComponent = ProjectGameplayDebugCommandExecutorPrivate::FindNeedsComponent(OwnerActor);
	if (!NeedsComponent || !NeedsComponent->HasNeed(NeedName))
	{
		return false;
	}

	const float ClampedPercent = FMath::Clamp(Percent, 0.f, 1.f);
	return NeedsComponent->SetNeedCurrentValue(NeedName, NeedsComponent->GetNeedMaxValue(NeedName) * ClampedPercent, true);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetSensationToPercent(AActor* OwnerActor, const FName SensationName, const float Percent)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)SensationName;
	(void)Percent;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	if (SensationName == CurseName)
	{
		UProjectInnerDoctrineComponent* DoctrineComponent = FindInnerDoctrineComponent(OwnerActor);
		if (!DoctrineComponent)
		{
			return false;
		}

		const float TargetCurse = DoctrineComponent->GetCurseMax() * FMath::Clamp(Percent, 0.f, 1.f);
		DoctrineComponent->CleanseCurse(DoctrineComponent->GetCurseMax());
		if (TargetCurse <= KINDA_SMALL_NUMBER)
		{
			return true;
		}

		FProjectCurseApplicationContext Context;
		Context.Amount = TargetCurse;
		Context.SourceKind = EProjectCurseSourceKind::Debug;
		Context.ApplicationId = FGuid::NewGuid();
		Context.bResistible = false;
		Context.bCanTriggerCursed = true;
		return DoctrineComponent->ApplyCurse(Context).AppliedAmount > 0.f;
	}

	UProjectSurvivalNeedsComponent* NeedsComponent = FindNeedsComponent(OwnerActor);
	if (!NeedsComponent || !NeedsComponent->HasSensation(SensationName))
	{
		return false;
	}

	const float ClampedPercent = FMath::Clamp(Percent, 0.f, 1.f);
	return NeedsComponent->SetSensationCurrentValue(SensationName, NeedsComponent->GetSensationMaxValue(SensationName) * ClampedPercent, true);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ForceApplyStatus(AActor* OwnerActor, const FName StatusName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)StatusName;
	return false;
#else
	UProjectSurvivalStatusComponent* StatusComponent = ProjectGameplayDebugCommandExecutorPrivate::FindStatusComponent(OwnerActor);
	return StatusComponent && StatusComponent->ApplyDebugStatus(StatusName, true);
#endif
}

bool FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(
	AActor* OwnerActor,
	const EProjectDoctrineAttribute Attribute,
	const int32 TargetLevel)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)Attribute;
	(void)TargetLevel;
	return false;
#else
	UProjectInnerDoctrineComponent* DoctrineComponent = ProjectGameplayDebugCommandExecutorPrivate::FindInnerDoctrineComponent(OwnerActor);
	if (!DoctrineComponent || TargetLevel <= 0 || Attribute == EProjectDoctrineAttribute::Count)
	{
		return false;
	}

	const int32 CurrentLevel = DoctrineComponent->GetAttributeLevel(Attribute);
	if (CurrentLevel >= TargetLevel)
	{
		return true;
	}

	return DoctrineComponent->ApplyFreeAttributeLevels(Attribute, TargetLevel - CurrentLevel);
#endif
}

bool FProjectGameplayDebugCommandExecutor::StartRuntimeFpsBenchmark(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	if (!OwnerActor || !OwnerActor->GetWorld() || !OwnerActor->GetWorld()->GetGameInstance())
	{
		return false;
	}

	UProjectRuntimePerformanceSubsystem* PerformanceSubsystem =
		OwnerActor->GetWorld()->GetGameInstance()->GetSubsystem<UProjectRuntimePerformanceSubsystem>();
	return PerformanceSubsystem
		&& PerformanceSubsystem->RequestDefaultDungeonCombatBenchmark(-1.0f, -1, false);
#endif
}

bool FProjectGameplayDebugCommandExecutor::StartFullStackOverloadBenchmark(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	if (!OwnerActor || !OwnerActor->GetWorld() || !OwnerActor->GetWorld()->GetGameInstance())
	{
		return false;
	}

	UProjectRuntimePerformanceSubsystem* PerformanceSubsystem =
		OwnerActor->GetWorld()->GetGameInstance()->GetSubsystem<UProjectRuntimePerformanceSubsystem>();
	return PerformanceSubsystem
		&& PerformanceSubsystem->RequestDungeonFullStackOverloadBenchmark(-1.0f, -1, false);
#endif
}

TArray<FName> FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowNames(AActor* OwnerActor)
{
	TArray<FName> RowNames;
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	if (!TattooSubsystem)
	{
		return RowNames;
	}

	TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> Snapshots;
	TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshots(Snapshots);
	RowNames.Reserve(Snapshots.Num());
	for (const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot : Snapshots)
	{
		RowNames.Add(Snapshot.RowName);
	}
	RowNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
#endif
	return RowNames;
}

FText FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowLabel(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return FText::FromName(RowName);
#else
	FProjectAutomaticTattooRuntimeDebugSnapshot Snapshot;
	if (!ProjectGameplayDebugCommandExecutorPrivate::TryFindAutomaticTattooSnapshot(OwnerActor, RowName, Snapshot))
	{
		return FText::FromName(RowName);
	}

	const FString OverrideSuffix = Snapshot.bHasRuntimeOverride ? TEXT(" *") : TEXT("");
	return FText::FromString(FString::Printf(
		TEXT("%s [%s]%s"),
		*RowName.ToString(),
		*ProjectGameplayDebugCommandExecutorPrivate::BuildAutomaticTattooStateText(Snapshot),
		*OverrideSuffix));
#endif
}

FText FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowDescription(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return FText::GetEmpty();
#else
	FProjectAutomaticTattooRuntimeDebugSnapshot Snapshot;
	if (!ProjectGameplayDebugCommandExecutorPrivate::TryFindAutomaticTattooSnapshot(OwnerActor, RowName, Snapshot))
	{
		return FText::GetEmpty();
	}

	const FProjectAutomaticTattooTableRow& Row = Snapshot.EffectiveRow;
	return FText::FromString(FString::Printf(
		TEXT("AT runtime values: X %.2f | Y %.2f | Size %.2f | Rot %.1f | Projection %.2f. %s"),
		Row.OffsetX,
		Row.OffsetY,
		Row.Size,
		Row.RotationDegrees,
		Row.ProjectionDistance,
		*Row.UnlockDescription));
#endif
}

bool FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(
	AActor* OwnerActor,
	const FName RowName,
	const float DeltaOffsetX,
	const float DeltaOffsetY,
	const float DeltaSize,
	const float DeltaRotationDegrees,
	const float DeltaProjectionDistance)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	(void)DeltaOffsetX;
	(void)DeltaOffsetY;
	(void)DeltaSize;
	(void)DeltaRotationDegrees;
	(void)DeltaProjectionDistance;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	return TattooSubsystem
		&& TattooSubsystem->AdjustAutomaticTattooRuntimeDebugPlacement(
			Cast<APawn>(OwnerActor),
			RowName,
			DeltaOffsetX,
			DeltaOffsetY,
			DeltaSize,
			DeltaRotationDegrees,
			DeltaProjectionDistance);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ResetAutomaticTattooPlacement(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	return TattooSubsystem && TattooSubsystem->ResetAutomaticTattooRuntimeDebugPlacement(Cast<APawn>(OwnerActor), RowName);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ToggleAutomaticTattooForcedActive(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	return TattooSubsystem && TattooSubsystem->ToggleAutomaticTattooRuntimeDebugForcedActive(Cast<APawn>(OwnerActor), RowName);
#endif
}

bool FProjectGameplayDebugCommandExecutor::CopyAutomaticTattooPlacementValues(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	if (!TattooSubsystem)
	{
		return false;
	}

	const FString CopyText = TattooSubsystem->BuildAutomaticTattooRuntimeDebugCopyText(RowName);
	if (CopyText.IsEmpty())
	{
		return false;
	}

	FPlatformApplicationMisc::ClipboardCopy(*CopyText);
	UE_LOG(LogProjectGameplayDebugCommands, Display, TEXT("[GameplayDebug][AT] Copied Automatic Tattoo values: %s"), *CopyText);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			4.0f,
			FColor::Cyan,
			FString::Printf(TEXT("Copied AT values: %s"), *RowName.ToString()));
	}
	return true;
#endif
}

TArray<FName> FProjectGameplayDebugCommandExecutor::GetAvailableStatusNames()
{
	TArray<FName> StatusNames;
	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (!Settings)
	{
		return StatusNames;
	}

	for (const FProjectSurvivalStatusDefinition& Definition : Settings->BuildResolvedStatusDefinitions())
	{
		if (!Definition.StatusName.IsNone())
		{
			StatusNames.AddUnique(Definition.StatusName);
		}
	}

	StatusNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
	return StatusNames;
}

TArray<EProjectDoctrineAttribute> FProjectGameplayDebugCommandExecutor::GetDebugAttributes()
{
	return {
		EProjectDoctrineAttribute::Willpower,
		EProjectDoctrineAttribute::Offensive,
		EProjectDoctrineAttribute::Defensive,
		EProjectDoctrineAttribute::Faith,
		EProjectDoctrineAttribute::Cunning,
		EProjectDoctrineAttribute::Celerity,
		EProjectDoctrineAttribute::Charisma
	};
}

FName FProjectGameplayDebugCommandExecutor::GetAttributeId(const EProjectDoctrineAttribute Attribute)
{
	switch (Attribute)
	{
	case EProjectDoctrineAttribute::Willpower:
		return TEXT("Willpower");
	case EProjectDoctrineAttribute::Offensive:
		return TEXT("Offensive");
	case EProjectDoctrineAttribute::Defensive:
		return TEXT("Defensive");
	case EProjectDoctrineAttribute::Faith:
		return TEXT("Faith");
	case EProjectDoctrineAttribute::Cunning:
		return TEXT("Cunning");
	case EProjectDoctrineAttribute::Celerity:
		return TEXT("Celerity");
	case EProjectDoctrineAttribute::Charisma:
		return TEXT("Charisma");
	default:
		return NAME_None;
	}
}

FText FProjectGameplayDebugCommandExecutor::GetAttributeDisplayName(const EProjectDoctrineAttribute Attribute)
{
	const FName AttributeId = GetAttributeId(Attribute);
	return AttributeId.IsNone() ? FText::GetEmpty() : FText::FromName(AttributeId);
}
