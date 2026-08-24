#include "RuntimePerformance/ProjectRuntimeAssetPreloadSubsystem.h"

#include "Characters/ProjectEnemyLevelSettings.h"
#include "Characters/ProjectEnemyVisualVariationSettings.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "EFProjectEnemySettings.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Pawn.h"
#include "Intimacy/ProjectIntimacySettings.h"
#include "NiagaraSystem.h"
#include "RuntimePerformance/ProjectPerformanceBudgetSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectRuntimePreload, Log, All);

namespace ProjectRuntimeAssetPreloadPrivate
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

	static void AddValidAssetPath(TArray<FSoftObjectPath>& AssetsToPreload, const FSoftObjectPath& AssetPath)
	{
		if (AssetPath.IsValid())
		{
			AssetsToPreload.AddUnique(AssetPath);
		}
	}
}

void UProjectRuntimeAssetPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UProjectPerformanceBudgetSettings* Settings = UProjectPerformanceBudgetSettings::Get();
	if (Settings && Settings->bPreloadRuntimeCombatAssets)
	{
		RequestRuntimePreload();
	}
}

void UProjectRuntimeAssetPreloadSubsystem::Deinitialize()
{
	ResidentEnemyClasses.Reset();
	RuntimePreloadHandle.Reset();
	bRuntimePreloadRequested = false;
	bRuntimePreloadComplete = false;
	Super::Deinitialize();
}

bool UProjectRuntimeAssetPreloadSubsystem::IsRuntimePreloadRequested() const
{
	return bRuntimePreloadRequested;
}

bool UProjectRuntimeAssetPreloadSubsystem::IsRuntimePreloadComplete() const
{
	return bRuntimePreloadComplete;
}

void UProjectRuntimeAssetPreloadSubsystem::CopyResidentEnemyClasses(
	TArray<TSubclassOf<APawn>>& OutEnemyClasses) const
{
	OutEnemyClasses = ResidentEnemyClasses;
}

void UProjectRuntimeAssetPreloadSubsystem::RequestRuntimePreload()
{
	if (bRuntimePreloadRequested)
	{
		return;
	}

	bRuntimePreloadRequested = true;
	bRuntimePreloadComplete = false;

	const UProjectPerformanceBudgetSettings* Settings = UProjectPerformanceBudgetSettings::Get();
	TArray<FSoftObjectPath> AssetsToPreload;
	AssetsToPreload.Reserve((Settings ? Settings->AdditionalPreloadAssets.Num() : 0) + 24);

	if (const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get())
	{
		for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, EnemyClassPath);
		}
		for (const FSoftClassPath& EnemyClassPath : EnemySettings->MaleCharacterClasses)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, EnemyClassPath);
		}
		for (const FSoftClassPath& EnemyClassPath : EnemySettings->FemaleCharacterClasses)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, EnemyClassPath);
		}
	}

	if (const UProjectEnemyLevelSettings* EnemyLevelSettings = UProjectEnemyLevelSettings::Get())
	{
		for (const TSoftClassPtr<APawn>& EnemyClass : EnemyLevelSettings->TargetEnemyBaseClasses)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, EnemyClass.ToSoftObjectPath());
		}
	}

	if (const UProjectEnemyVisualVariationSettings* VisualSettings = UProjectEnemyVisualVariationSettings::Get())
	{
		for (const TSoftClassPtr<APawn>& EnemyClass : VisualSettings->TargetEnemyClasses)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, EnemyClass.ToSoftObjectPath());
		}
		for (const TSoftClassPtr<APawn>& EnemyClass : VisualSettings->OptionalMatureMorphTargetEnemyClasses)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, EnemyClass.ToSoftObjectPath());
		}
	}

	if (const UProjectDefeatFlowSettings* DefeatSettings = UProjectDefeatFlowSettings::Get())
	{
		ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(
			AssetsToPreload,
			DefeatSettings->KnockoutStruggleWidgetClass.ToSoftObjectPath());
	}

	if (const UProjectIntimacySettings* IntimacySettings = UProjectIntimacySettings::Get())
	{
		ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, IntimacySettings->SocialCardRowsTable);
	}

	for (const TCHAR* AssetPath : ProjectRuntimeAssetPreloadPrivate::StruggleUiAssetPaths)
	{
		ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, FSoftObjectPath(AssetPath));
	}

	if (Settings)
	{
		for (const FSoftObjectPath& AssetPath : Settings->AdditionalPreloadAssets)
		{
			ProjectRuntimeAssetPreloadPrivate::AddValidAssetPath(AssetsToPreload, AssetPath);
		}
	}

	if (AssetsToPreload.IsEmpty())
	{
		HandleRuntimePreloadComplete();
		return;
	}

	RuntimePreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToPreload,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleRuntimePreloadComplete),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("ProjectRuntimeSessionPreload"));

	if (!RuntimePreloadHandle.IsValid())
	{
		UE_LOG(LogProjectRuntimePreload, Warning, TEXT("Unable to create the session runtime preload handle."));
		HandleRuntimePreloadComplete();
		return;
	}

	UE_LOG(
		LogProjectRuntimePreload,
		Log,
		TEXT("Requested one session-persistent runtime preload for %d assets; gameplay budgeting remains %s."),
		AssetsToPreload.Num(),
		Settings && Settings->bEnableRuntimeBudgeting ? TEXT("enabled") : TEXT("disabled"));
}

void UProjectRuntimeAssetPreloadSubsystem::HandleRuntimePreloadComplete()
{
	ResolveResidentEnemyClasses();

	int32 NiagaraSystemsPrecached = 0;
	if (RuntimePreloadHandle.IsValid())
	{
		TArray<UNiagaraSystem*> LoadedNiagaraSystems;
		RuntimePreloadHandle->GetLoadedAssets(LoadedNiagaraSystems);
		for (UNiagaraSystem* NiagaraSystem : LoadedNiagaraSystems)
		{
			if (NiagaraSystem)
			{
				NiagaraSystem->PrecacheAssetPSOs();
				++NiagaraSystemsPrecached;
			}
		}
	}

	bRuntimePreloadComplete = true;
	UE_LOG(
		LogProjectRuntimePreload,
		Log,
		TEXT("Session runtime preload completed with %d resident enemy classes and %d Niagara PSO precache requests."),
		ResidentEnemyClasses.Num(),
		NiagaraSystemsPrecached);
}

void UProjectRuntimeAssetPreloadSubsystem::ResolveResidentEnemyClasses()
{
	ResidentEnemyClasses.Reset();

	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	if (!EnemySettings)
	{
		return;
	}

	for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
	{
		if (UClass* EnemyClass = Cast<UClass>(EnemyClassPath.ResolveObject());
			EnemyClass && EnemyClass->IsChildOf(APawn::StaticClass()))
		{
			ResidentEnemyClasses.AddUnique(EnemyClass);
		}
	}
}
