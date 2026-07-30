#include "RuntimePerformance/ProjectPerformanceBudgetSubsystem.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "EFProjectEnemySettings.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Intimacy/ProjectIntimacySettings.h"
#include "NiagaraComponent.h"
#include "RuntimePerformance/ProjectPerformanceBudgetSettings.h"

namespace ProjectPerformanceBudgetPrivate
{
	static const TCHAR* StruggleUiAssetPaths[] =
	{
		TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel.FF_Cinzel"),
		TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed.FF_BarlowSemiCondensed"),
		TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium.FF_BarlowSemiCondensedMedium"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel.T_Struggle_TopPanel"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel.T_Struggle_MainPanel"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber.T_Struggle_TargetChamber"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing.T_Struggle_TargetRing"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse.T_Struggle_TargetPulse"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow.T_Struggle_Arrow"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak.T_Struggle_GlowStreak"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise.T_Struggle_Noise"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette.T_Struggle_BackdropVignette")
	};

	static float SquaredDistanceTo(const AActor* Actor, const FVector& Location)
	{
		return IsValid(Actor) ? FVector::DistSquared(Actor->GetActorLocation(), Location) : TNumericLimits<float>::Max();
	}

	static void SetActorTickInterval(AActor* Actor, const float TickInterval)
	{
		if (IsValid(Actor) && Actor->PrimaryActorTick.bCanEverTick)
		{
			Actor->PrimaryActorTick.TickInterval = TickInterval;
		}
	}

	static void AddValidAssetPath(TArray<FSoftObjectPath>& AssetsToPreload, const TCHAR* AssetPath)
	{
		const FSoftObjectPath SoftPath(AssetPath);
		if (SoftPath.IsValid())
		{
			AssetsToPreload.AddUnique(SoftPath);
		}
	}
}

void UProjectPerformanceBudgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResolveRuntimeEnemyClasses();
}

void UProjectPerformanceBudgetSubsystem::Deinitialize()
{
	RuntimeEnemyClasses.Reset();
	LastSnapshot = FProjectPerformanceBudgetSnapshot();
	Super::Deinitialize();
}

void UProjectPerformanceBudgetSubsystem::Tick(const float DeltaTime)
{
	const UProjectPerformanceBudgetSettings* Settings = UProjectPerformanceBudgetSettings::Get();
	if (!Settings || !Settings->bEnableRuntimeBudgeting)
	{
		return;
	}

	if (!bRuntimePreloadRequested && Settings->bPreloadRuntimeCombatAssets)
	{
		RequestRuntimePreload(*Settings);
	}

	UpdateAccumulatorSeconds += DeltaTime;
	const float UpdateInterval = FMath::Max(Settings->BudgetUpdateIntervalSeconds, 0.1f);
	if (UpdateAccumulatorSeconds < UpdateInterval)
	{
		return;
	}

	UpdateAccumulatorSeconds = 0.0f;
	ApplyBudgets();
}

TStatId UProjectPerformanceBudgetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectPerformanceBudgetSubsystem, STATGROUP_Tickables);
}

bool UProjectPerformanceBudgetSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	const UProjectPerformanceBudgetSettings* Settings = UProjectPerformanceBudgetSettings::Get();
	return World != nullptr
		&& World->IsGameWorld()
		&& Settings != nullptr
		&& Settings->bEnableRuntimeBudgeting;
}

bool UProjectPerformanceBudgetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FProjectPerformanceBudgetSnapshot UProjectPerformanceBudgetSubsystem::GetLastSnapshot() const
{
	return LastSnapshot;
}

void UProjectPerformanceBudgetSubsystem::ResolveRuntimeEnemyClasses()
{
	RuntimeEnemyClasses.Reset();

	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	if (!EnemySettings)
	{
		return;
	}

	for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
	{
		UClass* EnemyClass = Cast<UClass>(EnemyClassPath.TryLoad());
		if (EnemyClass && EnemyClass->IsChildOf(APawn::StaticClass()))
		{
			RuntimeEnemyClasses.AddUnique(EnemyClass);
		}
	}
}

void UProjectPerformanceBudgetSubsystem::RequestRuntimePreload(const UProjectPerformanceBudgetSettings& Settings)
{
	bRuntimePreloadRequested = true;

	TArray<FSoftObjectPath> AssetsToPreload;
	AssetsToPreload.Reserve(Settings.AdditionalPreloadAssets.Num() + 8);

	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	if (EnemySettings)
	{
		for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
		{
			if (EnemyClassPath.IsValid())
			{
				AssetsToPreload.AddUnique(EnemyClassPath);
			}
		}
	}

	if (const UProjectDefeatFlowSettings* DefeatSettings = UProjectDefeatFlowSettings::Get())
	{
		const FSoftObjectPath KnockoutWidgetPath = DefeatSettings->KnockoutStruggleWidgetClass.ToSoftObjectPath();
		if (KnockoutWidgetPath.IsValid())
		{
			AssetsToPreload.AddUnique(KnockoutWidgetPath);
		}
	}

	if (const UProjectIntimacySettings* IntimacySettings = UProjectIntimacySettings::Get())
	{
		if (IntimacySettings->SocialCardRowsTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->SocialCardRowsTable);
		}
	}

	for (const TCHAR* AssetPath : ProjectPerformanceBudgetPrivate::StruggleUiAssetPaths)
	{
		ProjectPerformanceBudgetPrivate::AddValidAssetPath(AssetsToPreload, AssetPath);
	}

	for (const FSoftObjectPath& AssetPath : Settings.AdditionalPreloadAssets)
	{
		if (AssetPath.IsValid())
		{
			AssetsToPreload.AddUnique(AssetPath);
		}
	}

	if (AssetsToPreload.IsEmpty())
	{
		return;
	}

	RuntimePreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToPreload,
		FStreamableDelegate(),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("ProjectRuntimeCombatPreload"));
}

void UProjectPerformanceBudgetSubsystem::ApplyBudgets()
{
	UWorld* World = GetWorld();
	const UProjectPerformanceBudgetSettings* Settings = UProjectPerformanceBudgetSettings::Get();
	if (!World || !Settings)
	{
		return;
	}

	if (RuntimeEnemyClasses.IsEmpty())
	{
		ResolveRuntimeEnemyClasses();
	}

	TArray<APawn*> Enemies;
	CollectRuntimeEnemies(Enemies);
	ApplyEnemyBudget(Enemies, *Settings);

	if (Settings->bApplyWorldVfxBudget)
	{
		if (const APawn* PlayerPawn = World->GetFirstPlayerController()
			? World->GetFirstPlayerController()->GetPawn()
			: nullptr)
		{
			ApplyNiagaraBudget(PlayerPawn->GetActorLocation(), *Settings);
		}
	}
}

void UProjectPerformanceBudgetSubsystem::CollectRuntimeEnemies(TArray<APawn*>& OutEnemies) const
{
	OutEnemies.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APawn* PlayerPawn = World->GetFirstPlayerController()
		? World->GetFirstPlayerController()->GetPawn()
		: nullptr;

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (IsValid(Pawn) && Pawn != PlayerPawn && IsRuntimeEnemyPawn(Pawn))
		{
			OutEnemies.Add(Pawn);
		}
	}
}

bool UProjectPerformanceBudgetSubsystem::IsRuntimeEnemyPawn(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || RuntimeEnemyClasses.IsEmpty())
	{
		return false;
	}

	const UClass* PawnClass = Pawn->GetClass();
	for (const TSubclassOf<APawn>& EnemyClass : RuntimeEnemyClasses)
	{
		if (EnemyClass && PawnClass && PawnClass->IsChildOf(EnemyClass))
		{
			return true;
		}
	}

	return false;
}

void UProjectPerformanceBudgetSubsystem::ApplyEnemyBudget(
	const TArray<APawn*>& Enemies,
	const UProjectPerformanceBudgetSettings& Settings)
{
	UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	TArray<APawn*> SortedEnemies = Enemies;
	SortedEnemies.Sort([&PlayerLocation](const APawn& Left, const APawn& Right)
	{
		return ProjectPerformanceBudgetPrivate::SquaredDistanceTo(&Left, PlayerLocation)
			< ProjectPerformanceBudgetPrivate::SquaredDistanceTo(&Right, PlayerLocation);
	});

	LastSnapshot = FProjectPerformanceBudgetSnapshot();

	const int32 FullRateLimit = FMath::Max(1, Settings.MaxFullRateEnemyAnimations);
	const int32 AwakeLimit = FMath::Max(FullRateLimit, Settings.MaxAwakeDungeonEnemies);
	const int32 RuntimeEnemyLimit = FMath::Max(FullRateLimit, Settings.MaxRuntimeEnemyActors);
	const int32 MidRateLimit = FMath::Clamp(Settings.MaxMidRateEnemyAnimations, 0, AwakeLimit);
	const float NearDistanceSquared = FMath::Square(Settings.NearEnemyDistance);
	const float MidDistanceSquared = FMath::Square(Settings.MidEnemyDistance);

	LastSnapshot.RuntimeEnemyCount = SortedEnemies.Num();

	for (int32 EnemyIndex = 0; EnemyIndex < SortedEnemies.Num(); ++EnemyIndex)
	{
		APawn* EnemyPawn = SortedEnemies[EnemyIndex];
		if (!IsValid(EnemyPawn))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(EnemyPawn->GetActorLocation(), PlayerLocation);
		const bool bSuspended = Settings.bCullExcessRuntimeEnemies && EnemyIndex >= RuntimeEnemyLimit;
		const bool bFullRate = !bSuspended && EnemyIndex < FullRateLimit && DistanceSquared <= NearDistanceSquared;
		const bool bMidRate = !bSuspended && !bFullRate && EnemyIndex < MidRateLimit && DistanceSquared <= MidDistanceSquared;
		const bool bFarRate = !bSuspended && !bFullRate && !bMidRate && EnemyIndex < AwakeLimit;
		const float ActorTickInterval = bFullRate
			? 0.0f
			: (bMidRate
				? Settings.MidEnemyTickInterval
				: (bFarRate ? Settings.FarEnemyTickInterval : Settings.DormantEnemyTickInterval));
		const float MeshTickInterval = bFullRate
			? 0.0f
			: (bMidRate
				? Settings.MidEnemyTickInterval
				: (bFarRate ? Settings.FarVisualTickInterval : Settings.DormantEnemyTickInterval));
		const int32 ForcedLod = bFullRate ? 0 : (bMidRate ? 2 : 3);

		if (bSuspended)
		{
			++LastSnapshot.SuspendedExcessEnemyCount;
		}
		else if (bFullRate)
		{
			++LastSnapshot.FullRateEnemyCount;
		}
		else if (bMidRate)
		{
			++LastSnapshot.MidRateEnemyCount;
		}
		else
		{
			++LastSnapshot.FarRateEnemyCount;
		}

		EnemyPawn->SetActorHiddenInGame(bSuspended);
		EnemyPawn->SetActorEnableCollision(!bSuspended);

		if (Settings.bApplyEnemyActorTickBudget)
		{
			EnemyPawn->SetActorTickEnabled(!bSuspended);
			ProjectPerformanceBudgetPrivate::SetActorTickInterval(EnemyPawn, ActorTickInterval);
			if (AController* Controller = EnemyPawn->GetController())
			{
				Controller->SetActorTickEnabled(!bSuspended);
				ProjectPerformanceBudgetPrivate::SetActorTickInterval(Controller, ActorTickInterval);
				if (AAIController* AIController = Cast<AAIController>(Controller))
				{
					if (bSuspended)
					{
						AIController->StopMovement();
					}
					if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
					{
						BrainComponent->SetComponentTickInterval(ActorTickInterval);
						BrainComponent->SetComponentTickEnabled(!bSuspended);
					}
				}
			}
		}

		if (Settings.bApplyEnemyMovementTickBudget)
		{
			TInlineComponentArray<UCharacterMovementComponent*> MovementComponents(EnemyPawn);
			EnemyPawn->GetComponents(MovementComponents);
			for (UCharacterMovementComponent* MovementComponent : MovementComponents)
			{
				if (IsValid(MovementComponent))
				{
					MovementComponent->SetComponentTickInterval(ActorTickInterval);
					MovementComponent->SetComponentTickEnabled(!bSuspended);
				}
			}
		}

		if (Settings.bApplyEnemyAnimationBudget)
		{
			TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(EnemyPawn);
			EnemyPawn->GetComponents(MeshComponents);
			for (USkeletalMeshComponent* MeshComponent : MeshComponents)
			{
				if (!IsValid(MeshComponent))
				{
					continue;
				}

				MeshComponent->bEnableUpdateRateOptimizations = true;
				MeshComponent->SetCullDistance(Settings.EnemyMeshCullDistance);
				MeshComponent->SetComponentTickInterval(MeshTickInterval);
				MeshComponent->SetComponentTickEnabled(!bSuspended);
				MeshComponent->SetForcedLOD(ForcedLod);
				MeshComponent->VisibilityBasedAnimTickOption = bFullRate
					? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
					: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			}
		}
	}
}

void UProjectPerformanceBudgetSubsystem::ApplyNiagaraBudget(
	const FVector& PlayerLocation,
	const UProjectPerformanceBudgetSettings& Settings)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CullDistance = FMath::Max(Settings.NiagaraCullDistance, 0.0f);
	const float CullDistanceSquared = FMath::Square(CullDistance);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(Actor);
		Actor->GetComponents(NiagaraComponents);
		for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
		{
			if (!IsValid(NiagaraComponent))
			{
				continue;
			}

			NiagaraComponent->SetCullDistance(CullDistance);
			const bool bFar = FVector::DistSquared(NiagaraComponent->GetComponentLocation(), PlayerLocation) > CullDistanceSquared;
			NiagaraComponent->SetComponentTickInterval(bFar ? Settings.NiagaraFarTickInterval : 0.0f);
			++LastSnapshot.BudgetedNiagaraComponentCount;
		}
	}
}
