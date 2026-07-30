#include "DayCycle/ProjectDayCycleSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "DayCycle/ProjectDayCycleStateActor.h"
#include "DayCycle/ProjectDayCycleWidget.h"
#include "EFCharacterCreationSubsystem.h"
#include "EFProjectUISettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UI/ProjectWidgetClassResolver.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectDayCycle, Log, All);

namespace ProjectDayCycleSubsystemPrivate
{
	constexpr int32 DayCycleHudZOrder = 140;

	bool IsUsableGameWorld(const UWorld* World)
	{
		return World && World->IsGameWorld() && !World->bIsTearingDown;
	}
}

bool UProjectDayCycleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer)
		&& World
		&& (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UProjectDayCycleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StateActor = nullptr;
	LocalPlayerController = nullptr;
	DayCycleWidget = nullptr;
	HudRefreshAccumulator = 0.0f;
}

void UProjectDayCycleSubsystem::Deinitialize()
{
	RemoveHudWidget();
	StateActor = nullptr;
	LocalPlayerController = nullptr;
	Super::Deinitialize();
}

void UProjectDayCycleSubsystem::Tick(const float DeltaTime)
{
	const UProjectDayCycleSettings* Settings = UProjectDayCycleSettings::Get();
	if (!ProjectDayCycleSubsystemPrivate::IsUsableGameWorld(GetWorld()) || (Settings && !Settings->bEnableDayCycle))
	{
		RemoveHudWidget();
		return;
	}

	ResolveOrCreateStateActor();
	ResolveLocalPlayerController();
	EnsureHudWidget();

	HudRefreshAccumulator += FMath::Max(0.0f, DeltaTime);
	const float RefreshInterval = FMath::Clamp(Settings ? Settings->HudRefreshIntervalSeconds : 0.1f, 0.02f, 1.0f);
	if (HudRefreshAccumulator >= RefreshInterval)
	{
		HudRefreshAccumulator = FMath::Fmod(HudRefreshAccumulator, RefreshInterval);
		RefreshHud();
	}
}

TStatId UProjectDayCycleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectDayCycleSubsystem, STATGROUP_Tickables);
}

bool UProjectDayCycleSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject) && ProjectDayCycleSubsystemPrivate::IsUsableGameWorld(GetWorld());
}

bool UProjectDayCycleSubsystem::IsTickableInEditor() const
{
	return false;
}

bool UProjectDayCycleSubsystem::IsTickableWhenPaused() const
{
	return false;
}

FProjectDayCycleSnapshot UProjectDayCycleSubsystem::GetCurrentSnapshot() const
{
	if (StateActor)
	{
		return StateActor->GetCurrentSnapshot();
	}

	FProjectDayCycleSnapshot Snapshot;
	const UProjectDayCycleSettings* Settings = UProjectDayCycleSettings::Get();
	Snapshot.DayNumber = FMath::Max(1, Settings ? Settings->InitialDayNumber : 1);
	Snapshot.DayLengthSeconds = FMath::Max(1.0f, Settings ? Settings->DayLengthSeconds : 600.0f);
	return Snapshot;
}

AProjectDayCycleStateActor* UProjectDayCycleSubsystem::GetDayCycleStateActor() const
{
	return StateActor;
}

void UProjectDayCycleSubsystem::ResolveOrCreateStateActor()
{
	UWorld* World = GetWorld();
	if (!ProjectDayCycleSubsystemPrivate::IsUsableGameWorld(World))
	{
		StateActor = nullptr;
		return;
	}

	if (IsValid(StateActor))
	{
		return;
	}

	StateActor = nullptr;
	for (TActorIterator<AProjectDayCycleStateActor> It(World); It; ++It)
	{
		StateActor = *It;
		break;
	}

	if (!StateActor && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("ProjectDayCycleState");
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		StateActor = World->SpawnActor<AProjectDayCycleStateActor>(AProjectDayCycleStateActor::StaticClass(), SpawnParameters);
		if (StateActor)
		{
			UE_LOG(LogProjectDayCycle, Log, TEXT("[ProjectDayCycle] Started a %.1f second day cycle."), StateActor->GetCurrentSnapshot().DayLengthSeconds);
		}
	}
}

void UProjectDayCycleSubsystem::ResolveLocalPlayerController()
{
	UWorld* World = GetWorld();
	APlayerController* ResolvedController = nullptr;
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Candidate = It->Get();
			if (Candidate && Candidate->IsLocalController())
			{
				ResolvedController = Candidate;
				break;
			}
		}
	}

	if (LocalPlayerController != ResolvedController)
	{
		RemoveHudWidget();
		LocalPlayerController = ResolvedController;
	}
}

void UProjectDayCycleSubsystem::EnsureHudWidget()
{
	const UProjectDayCycleSettings* Settings = UProjectDayCycleSettings::Get();
	const UWorld* World = GetWorld();
	const UEFCharacterCreationSubsystem* CharacterCreationSubsystem =
		World && World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UEFCharacterCreationSubsystem>()
			: nullptr;

	if (!LocalPlayerController
		|| !LocalPlayerController->IsLocalController()
		|| (Settings && !Settings->bShowDayCycleHud)
		|| (CharacterCreationSubsystem && CharacterCreationSubsystem->IsCharacterCreationActive()))
	{
		RemoveHudWidget();
		return;
	}

	if (DayCycleWidget)
	{
		return;
	}

	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		UISettings ? UISettings->DayCycleWidgetClass : FSoftClassPath(),
		UProjectDayCycleWidget::StaticClass(),
		TEXT("ProjectDayCycleWidget"));
	if (!ResolvedClass)
	{
		ResolvedClass = UProjectDayCycleWidget::StaticClass();
	}

	DayCycleWidget = CreateWidget<UProjectDayCycleWidget>(LocalPlayerController, ResolvedClass, TEXT("ProjectDayCycleWidget"));
	if (!DayCycleWidget)
	{
		UE_LOG(LogProjectDayCycle, Warning, TEXT("[ProjectDayCycle] Failed to create the day cycle HUD."));
		return;
	}

	if (!DayCycleWidget->AddToPlayerScreen(ProjectDayCycleSubsystemPrivate::DayCycleHudZOrder))
	{
		DayCycleWidget->AddToViewport(ProjectDayCycleSubsystemPrivate::DayCycleHudZOrder);
	}

	RefreshHud();
}

void UProjectDayCycleSubsystem::RefreshHud()
{
	if (DayCycleWidget)
	{
		DayCycleWidget->ApplySnapshot(GetCurrentSnapshot());
	}
}

void UProjectDayCycleSubsystem::RemoveHudWidget()
{
	if (DayCycleWidget)
	{
		DayCycleWidget->RemoveFromParent();
		DayCycleWidget = nullptr;
	}
}
