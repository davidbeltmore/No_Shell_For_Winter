#include "Survival/ProjectSurvivalNeedsSubsystem.h"

#include "EFProjectUISettings.h"
#include "EFProjectInputSettings.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Defeat/ProjectDefeatBlueprintBridgeComponent.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineWidget.h"
#include "Survival/ProjectSurvivalAttributeBridgeComponent.h"
#include "Survival/ProjectSurvivalLog.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectSurvivalStatusCatalog.h"
#include "Survival/ProjectSurvivalNeedsWidget.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectSurvivalStatusWidget.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr int32 ProjectSurvivalNeedsHudZOrder = 150;
	constexpr int32 ProjectInnerDoctrineHudZOrder = 120;
	constexpr int32 ProjectSurvivalStatusHudZOrder = 180;

	bool IsWorldUnsafeForWidgetCreation(const UWorld* World)
	{
		return !World
			|| !World->IsGameWorld()
			|| World->bIsTearingDown;
	}

	template <typename TWidgetType>
	TSubclassOf<TWidgetType> ResolveWidgetClass(
		const FSoftClassPath& WidgetClassPath,
		TSubclassOf<TWidgetType> FallbackClass,
		const TCHAR* ContextName = TEXT("ProjectSurvivalWidget"))
	{
		UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
			WidgetClassPath,
			FallbackClass.Get(),
			ContextName);
		return ResolvedClass ? TSubclassOf<TWidgetType>(ResolvedClass) : FallbackClass;
	}
}

void UProjectSurvivalNeedsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedNeedsComponent = nullptr;
	TrackedCombatAttributeComponent = nullptr;
	TrackedAttributeBridgeComponent = nullptr;
	TrackedRealtimeSnapshotComponent = nullptr;
	TrackedNeedsWidget = nullptr;
	TrackedStatusComponent = nullptr;
	TrackedStatusWidget = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	TrackedInnerDoctrineWidget = nullptr;
	TrackedPlayerController = nullptr;
	TrackedInputComponent = nullptr;
	BoundNeedsComponent = nullptr;
	BoundStatusComponent = nullptr;
	BoundInnerDoctrineComponent = nullptr;
	BoundRealtimeSnapshotComponent = nullptr;
	bNeedsHudVisible = false;
	bNeedsMaintenanceTick = true;

	TryResolveRuntimeContext();
}

void UProjectSurvivalNeedsSubsystem::Deinitialize()
{
	UnbindFromTrackedComponents();
	DetachFromTrackedPlayerController(true);

	TrackedNeedsComponent = nullptr;
	TrackedCombatAttributeComponent = nullptr;
	TrackedAttributeBridgeComponent = nullptr;
	TrackedStatusComponent = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	TrackedRealtimeSnapshotComponent = nullptr;
	BoundNeedsComponent = nullptr;
	BoundStatusComponent = nullptr;
	BoundInnerDoctrineComponent = nullptr;
	BoundRealtimeSnapshotComponent = nullptr;
	bNeedsMaintenanceTick = false;

	Super::Deinitialize();
}

void UProjectSurvivalNeedsSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();
}

TStatId UProjectSurvivalNeedsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectSurvivalNeedsSubsystem, STATGROUP_Tickables);
}

bool UProjectSurvivalNeedsSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return bNeedsMaintenanceTick && World != nullptr && World->IsGameWorld();
}

bool UProjectSurvivalNeedsSubsystem::IsTickableInEditor() const
{
	return false;
}

bool UProjectSurvivalNeedsSubsystem::IsTickableWhenPaused() const
{
	return false;
}

bool UProjectSurvivalNeedsSubsystem::SetNeedsHudVisible(const bool bVisible)
{
	bNeedsHudVisible = bVisible;

	UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalNeedsUI] SetNeedsHudVisible -> %s"), bNeedsHudVisible ? TEXT("true") : TEXT("false"));

	if (TrackedNeedsWidget)
	{
		TrackedNeedsWidget->SetHudVisible(bNeedsHudVisible);
	}

	if (TrackedStatusWidget)
	{
		TrackedStatusWidget->SetHudVisible(bNeedsHudVisible);
	}

	if (TrackedInnerDoctrineWidget)
	{
		TrackedInnerDoctrineWidget->SetHudVisible(bNeedsHudVisible);
	}

	if (bNeedsHudVisible)
	{
		RefreshNeedsWidget(true);
		RefreshStatusWidget(true);
		RefreshInnerDoctrineWidget(true);
	}

	return TrackedNeedsWidget != nullptr;
}

bool UProjectSurvivalNeedsSubsystem::ToggleNeedsHudVisibility()
{
	return SetNeedsHudVisible(!bNeedsHudVisible);
}

bool UProjectSurvivalNeedsSubsystem::IsNeedsHudVisible() const
{
	return bNeedsHudVisible;
}

UProjectInnerDoctrineWidget* UProjectSurvivalNeedsSubsystem::GetTrackedInnerDoctrineWidget() const
{
	return TrackedInnerDoctrineWidget;
}

void UProjectSurvivalNeedsSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	TrackedNeedsComponent = nullptr;
	TrackedCombatAttributeComponent = nullptr;
	TrackedAttributeBridgeComponent = nullptr;
	TrackedStatusComponent = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	TrackedRealtimeSnapshotComponent = nullptr;

	UnbindFromTrackedComponents();

	if (TrackedNeedsWidget)
	{
		TrackedNeedsWidget->SetNeedsComponent(nullptr);
	}

	if (TrackedStatusWidget)
	{
		TrackedStatusWidget->SetStatusComponent(nullptr);
	}

	if (TrackedInnerDoctrineWidget)
	{
		TrackedInnerDoctrineWidget->SetInnerDoctrineComponent(nullptr);
	}

	MarkMaintenanceRequired();
	TryResolveRuntimeContext();
}

void UProjectSurvivalNeedsSubsystem::HandleSurvivalValueChanged(FName EntryName, float OldValue, float NewValue, float MaxValue, bool bIsSensation)
{
	RefreshNeedsWidget(false);
}

void UProjectSurvivalNeedsSubsystem::HandlePenaltyMultiplierChanged(float OldPenaltyMultiplier, float NewPenaltyMultiplier)
{
	RefreshNeedsWidget(false);
}

void UProjectSurvivalNeedsSubsystem::HandleStatusChanged(FName StatusName, bool bActive)
{
	RefreshStatusWidget(true);
}

void UProjectSurvivalNeedsSubsystem::HandleBlackoutChanged(bool bBlackoutActive)
{
	RefreshStatusWidget(true);
}

void UProjectSurvivalNeedsSubsystem::HandleInnerDoctrineDxpChanged(int32 OldCurrentRunDxp, int32 NewCurrentRunDxp, int32 OldMetaBankDxp, int32 NewMetaBankDxp)
{
	RefreshInnerDoctrineWidget(false);
}

void UProjectSurvivalNeedsSubsystem::HandleDoctrineAttributeLevelChanged(EProjectDoctrineAttribute Attribute, int32 OldLevel, int32 NewLevel, int32 NextLevelCost)
{
	RefreshInnerDoctrineWidget(false);
}

void UProjectSurvivalNeedsSubsystem::HandleDoctrineMilestoneTriggered(FName AbilityId, EProjectDoctrineAttribute Attribute, int32 Level)
{
	RefreshInnerDoctrineWidget(false);
}

void UProjectSurvivalNeedsSubsystem::HandleUnifiedRuntimeSnapshotChanged(const FProjectUnifiedRuntimeSnapshot& Snapshot)
{
	RefreshNeedsWidget(false);
	RefreshStatusWidget(false);
	RefreshInnerDoctrineWidget(false);
	(void)Snapshot;
}

void UProjectSurvivalNeedsSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (IsWorldUnsafeForWidgetCreation(World))
	{
		if (TrackedPlayerController || TrackedNeedsWidget || TrackedStatusWidget || TrackedInnerDoctrineWidget || TrackedInputComponent)
		{
			UnbindFromTrackedComponents();
			DetachFromTrackedPlayerController(true);
		}

		bNeedsMaintenanceTick = World != nullptr && World->IsGameWorld() && !World->bIsTearingDown;
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		if (TrackedPlayerController || TrackedNeedsWidget || TrackedStatusWidget || TrackedInputComponent)
		{
			UnbindFromTrackedComponents();
			DetachFromTrackedPlayerController(true);
			TrackedNeedsComponent = nullptr;
			TrackedCombatAttributeComponent = nullptr;
			TrackedAttributeBridgeComponent = nullptr;
			TrackedStatusComponent = nullptr;
			TrackedInnerDoctrineComponent = nullptr;
			TrackedRealtimeSnapshotComponent = nullptr;
		}

		MarkMaintenanceRequired();
		return;
	}

	AttachToPlayerController(PlayerController);
	EnsureNeedsComponentOnPlayerPawn();
	BindToTrackedComponents();
	EnsureNeedsHudWidget(PlayerController);
	EnsureInnerDoctrineHudWidget(PlayerController);
	EnsureStatusHudWidget(PlayerController);

	const bool bRuntimeReady = TrackedPlayerController != nullptr
		&& TrackedInputComponent != nullptr
		&& TrackedNeedsComponent != nullptr
		&& TrackedStatusComponent != nullptr
		&& TrackedInnerDoctrineComponent != nullptr
		&& TrackedRealtimeSnapshotComponent != nullptr
		&& TrackedNeedsWidget != nullptr
		&& TrackedInnerDoctrineWidget != nullptr
		&& TrackedStatusWidget != nullptr;

	bNeedsMaintenanceTick = !bRuntimeReady;
}

void UProjectSurvivalNeedsSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindInputToTrackedPlayerController();
		return;
	}

	DetachFromTrackedPlayerController(true);

	TrackedPlayerController = PlayerController;
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	BindInputToTrackedPlayerController();
	MarkMaintenanceRequired();
}

void UProjectSurvivalNeedsSubsystem::DetachFromTrackedPlayerController(const bool bRemoveWidgets)
{
	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	UnbindInputFromTrackedPlayerController();

	if (bRemoveWidgets)
	{
		if (TrackedNeedsWidget)
		{
			TrackedNeedsWidget->RemoveFromParent();
			TrackedNeedsWidget = nullptr;
		}

		if (TrackedStatusWidget)
		{
			TrackedStatusWidget->RemoveFromParent();
			TrackedStatusWidget = nullptr;
		}

		if (TrackedInnerDoctrineWidget)
		{
			TrackedInnerDoctrineWidget->RemoveFromParent();
			TrackedInnerDoctrineWidget = nullptr;
		}
	}

	TrackedPlayerController = nullptr;
}

void UProjectSurvivalNeedsSubsystem::BindInputToTrackedPlayerController()
{
	if (!TrackedPlayerController || TrackedInputComponent)
	{
		return;
	}

	TrackedInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectSurvivalNeedsInputComponent"));
	if (!TrackedInputComponent)
	{
		UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalNeedsUI] Failed to create input component for %s"), *GetNameSafe(TrackedPlayerController));
		return;
	}

	TrackedInputComponent->bBlockInput = false;
	TrackedInputComponent->Priority = 1;
	TrackedInputComponent->RegisterComponent();
	const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get();
	TrackedInputComponent->BindKey(InputSettings ? InputSettings->ToggleNeedsHudKey : EKeys::Comma, IE_Pressed, this, &ThisClass::HandleNeedsHudTogglePressed);
	if (GetProjectSurvivalStatusCatalog().bEnableDebugStatusCycling)
	{
		TrackedInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ThisClass::HandleDebugStatusCyclePressed);
	}
	TrackedPlayerController->PushInputComponent(TrackedInputComponent);
}

void UProjectSurvivalNeedsSubsystem::UnbindInputFromTrackedPlayerController()
{
	if (!TrackedInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(TrackedInputComponent);
	}

	if (TrackedInputComponent->IsRegistered())
	{
		TrackedInputComponent->DestroyComponent();
	}

	TrackedInputComponent = nullptr;
}

void UProjectSurvivalNeedsSubsystem::BindToTrackedComponents()
{
	if (BoundNeedsComponent != TrackedNeedsComponent)
	{
		if (BoundNeedsComponent)
		{
			BoundNeedsComponent->OnSurvivalValueChanged.RemoveDynamic(this, &ThisClass::HandleSurvivalValueChanged);
			BoundNeedsComponent->OnPenaltyMultiplierChanged.RemoveDynamic(this, &ThisClass::HandlePenaltyMultiplierChanged);
		}

		BoundNeedsComponent = TrackedNeedsComponent;
		if (BoundNeedsComponent)
		{
			BoundNeedsComponent->OnSurvivalValueChanged.AddUniqueDynamic(this, &ThisClass::HandleSurvivalValueChanged);
			BoundNeedsComponent->OnPenaltyMultiplierChanged.AddUniqueDynamic(this, &ThisClass::HandlePenaltyMultiplierChanged);
		}
	}

	if (BoundStatusComponent != TrackedStatusComponent)
	{
		if (BoundStatusComponent)
		{
			BoundStatusComponent->OnStatusChanged.RemoveDynamic(this, &ThisClass::HandleStatusChanged);
			BoundStatusComponent->OnBlackoutChanged.RemoveDynamic(this, &ThisClass::HandleBlackoutChanged);
		}

		BoundStatusComponent = TrackedStatusComponent;
		if (BoundStatusComponent)
		{
			BoundStatusComponent->OnStatusChanged.AddUniqueDynamic(this, &ThisClass::HandleStatusChanged);
			BoundStatusComponent->OnBlackoutChanged.AddUniqueDynamic(this, &ThisClass::HandleBlackoutChanged);
		}
	}

	if (BoundInnerDoctrineComponent != TrackedInnerDoctrineComponent)
	{
		if (BoundInnerDoctrineComponent)
		{
			BoundInnerDoctrineComponent->OnDxpChanged.RemoveDynamic(this, &ThisClass::HandleInnerDoctrineDxpChanged);
			BoundInnerDoctrineComponent->OnAttributeLevelChanged.RemoveDynamic(this, &ThisClass::HandleDoctrineAttributeLevelChanged);
			BoundInnerDoctrineComponent->OnMilestoneTriggered.RemoveDynamic(this, &ThisClass::HandleDoctrineMilestoneTriggered);
		}

		BoundInnerDoctrineComponent = TrackedInnerDoctrineComponent;
		if (BoundInnerDoctrineComponent)
		{
			BoundInnerDoctrineComponent->OnDxpChanged.AddUniqueDynamic(this, &ThisClass::HandleInnerDoctrineDxpChanged);
			BoundInnerDoctrineComponent->OnAttributeLevelChanged.AddUniqueDynamic(this, &ThisClass::HandleDoctrineAttributeLevelChanged);
			BoundInnerDoctrineComponent->OnMilestoneTriggered.AddUniqueDynamic(this, &ThisClass::HandleDoctrineMilestoneTriggered);
		}
	}

	if (BoundRealtimeSnapshotComponent != TrackedRealtimeSnapshotComponent)
	{
		if (BoundRealtimeSnapshotComponent)
		{
			BoundRealtimeSnapshotComponent->OnUnifiedRuntimeSnapshotChanged.RemoveDynamic(this, &ThisClass::HandleUnifiedRuntimeSnapshotChanged);
		}

		BoundRealtimeSnapshotComponent = TrackedRealtimeSnapshotComponent;
		if (BoundRealtimeSnapshotComponent)
		{
			BoundRealtimeSnapshotComponent->OnUnifiedRuntimeSnapshotChanged.AddUniqueDynamic(this, &ThisClass::HandleUnifiedRuntimeSnapshotChanged);
		}
	}
}

void UProjectSurvivalNeedsSubsystem::UnbindFromTrackedComponents()
{
	if (BoundNeedsComponent)
	{
		BoundNeedsComponent->OnSurvivalValueChanged.RemoveDynamic(this, &ThisClass::HandleSurvivalValueChanged);
		BoundNeedsComponent->OnPenaltyMultiplierChanged.RemoveDynamic(this, &ThisClass::HandlePenaltyMultiplierChanged);
		BoundNeedsComponent = nullptr;
	}

	if (BoundStatusComponent)
	{
		BoundStatusComponent->OnStatusChanged.RemoveDynamic(this, &ThisClass::HandleStatusChanged);
		BoundStatusComponent->OnBlackoutChanged.RemoveDynamic(this, &ThisClass::HandleBlackoutChanged);
		BoundStatusComponent = nullptr;
	}

	if (BoundInnerDoctrineComponent)
	{
		BoundInnerDoctrineComponent->OnDxpChanged.RemoveDynamic(this, &ThisClass::HandleInnerDoctrineDxpChanged);
		BoundInnerDoctrineComponent->OnAttributeLevelChanged.RemoveDynamic(this, &ThisClass::HandleDoctrineAttributeLevelChanged);
		BoundInnerDoctrineComponent->OnMilestoneTriggered.RemoveDynamic(this, &ThisClass::HandleDoctrineMilestoneTriggered);
		BoundInnerDoctrineComponent = nullptr;
	}

	if (BoundRealtimeSnapshotComponent)
	{
		BoundRealtimeSnapshotComponent->OnUnifiedRuntimeSnapshotChanged.RemoveDynamic(this, &ThisClass::HandleUnifiedRuntimeSnapshotChanged);
		BoundRealtimeSnapshotComponent = nullptr;
	}
}

void UProjectSurvivalNeedsSubsystem::RefreshNeedsWidget(const bool bForceVisibleRefresh)
{
	if (!TrackedNeedsWidget)
	{
		return;
	}

	TrackedNeedsWidget->SetNeedsComponent(TrackedNeedsComponent);
	if (bForceVisibleRefresh || TrackedNeedsWidget->IsHudVisible())
	{
		TrackedNeedsWidget->RefreshDisplay();
	}
}

void UProjectSurvivalNeedsSubsystem::RefreshStatusWidget(const bool bForceVisibleRefresh)
{
	if (!TrackedStatusWidget)
	{
		return;
	}

	TrackedStatusWidget->SetStatusComponent(TrackedStatusComponent);
	if (bForceVisibleRefresh || TrackedStatusWidget->IsHudVisible() || (TrackedStatusComponent && TrackedStatusComponent->IsBlackoutActive()))
	{
		TrackedStatusWidget->RefreshDisplay();
	}
}

void UProjectSurvivalNeedsSubsystem::RefreshInnerDoctrineWidget(const bool bForceVisibleRefresh)
{
	if (!TrackedInnerDoctrineWidget)
	{
		return;
	}

	TrackedInnerDoctrineWidget->SetInnerDoctrineComponent(TrackedInnerDoctrineComponent);
	if (bForceVisibleRefresh || TrackedInnerDoctrineWidget->IsHudVisible())
	{
		TrackedInnerDoctrineWidget->RefreshDisplay();
	}
}

void UProjectSurvivalNeedsSubsystem::HandleNeedsHudTogglePressed()
{
	UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalNeedsUI] Comma pressed, toggling HUD"));
	ToggleNeedsHudVisibility();
}

void UProjectSurvivalNeedsSubsystem::HandleDebugStatusCyclePressed()
{
	if (TrackedStatusComponent)
	{
		TrackedStatusComponent->CycleDebugStatus();
		RefreshStatusWidget(true);
	}
}

void UProjectSurvivalNeedsSubsystem::MarkMaintenanceRequired()
{
	bNeedsMaintenanceTick = true;
}

void UProjectSurvivalNeedsSubsystem::EnsureNeedsComponentOnPlayerPawn()
{
	APawn* PlayerPawn = TrackedPlayerController ? TrackedPlayerController->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		TrackedNeedsComponent = nullptr;
		TrackedCombatAttributeComponent = nullptr;
		TrackedAttributeBridgeComponent = nullptr;
		TrackedStatusComponent = nullptr;
		TrackedInnerDoctrineComponent = nullptr;
		TrackedRealtimeSnapshotComponent = nullptr;
		return;
	}

	UProjectCombatAttributeComponent* CombatAttributeComponent = PlayerPawn->FindComponentByClass<UProjectCombatAttributeComponent>();
	if (!CombatAttributeComponent)
	{
		CombatAttributeComponent = NewObject<UProjectCombatAttributeComponent>(PlayerPawn, UProjectCombatAttributeComponent::StaticClass(), TEXT("ProjectCombatAttributeComponent"));
		if (!CombatAttributeComponent)
		{
			return;
		}

		PlayerPawn->AddInstanceComponent(CombatAttributeComponent);
		CombatAttributeComponent->OnComponentCreated();
		CombatAttributeComponent->RegisterComponent();
		CombatAttributeComponent->Activate(true);
	}

	TrackedCombatAttributeComponent = CombatAttributeComponent;

	UProjectSurvivalNeedsComponent* NeedsComponent = PlayerPawn->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	if (!NeedsComponent)
	{
		NeedsComponent = NewObject<UProjectSurvivalNeedsComponent>(PlayerPawn, UProjectSurvivalNeedsComponent::StaticClass(), TEXT("ProjectSurvivalNeedsComponent"));
		if (!NeedsComponent)
		{
			return;
		}

		PlayerPawn->AddInstanceComponent(NeedsComponent);
		NeedsComponent->OnComponentCreated();
		NeedsComponent->RegisterComponent();
		NeedsComponent->Activate(true);
		NeedsComponent->ResetToDefaults();
	}

	TrackedNeedsComponent = NeedsComponent;

	UProjectSurvivalAttributeBridgeComponent* AttributeBridgeComponent = PlayerPawn->FindComponentByClass<UProjectSurvivalAttributeBridgeComponent>();
	if (!AttributeBridgeComponent)
	{
		AttributeBridgeComponent = NewObject<UProjectSurvivalAttributeBridgeComponent>(PlayerPawn, UProjectSurvivalAttributeBridgeComponent::StaticClass(), TEXT("ProjectSurvivalAttributeBridgeComponent"));
		if (!AttributeBridgeComponent)
		{
			return;
		}

		PlayerPawn->AddInstanceComponent(AttributeBridgeComponent);
		AttributeBridgeComponent->OnComponentCreated();
		AttributeBridgeComponent->RegisterComponent();
		AttributeBridgeComponent->Activate(true);
	}

	TrackedAttributeBridgeComponent = AttributeBridgeComponent;
	TrackedAttributeBridgeComponent->ForceResolveAndApplyBridge();

	UProjectSurvivalStatusComponent* StatusComponent = PlayerPawn->FindComponentByClass<UProjectSurvivalStatusComponent>();
	if (!StatusComponent)
	{
		StatusComponent = NewObject<UProjectSurvivalStatusComponent>(PlayerPawn, UProjectSurvivalStatusComponent::StaticClass(), TEXT("ProjectSurvivalStatusComponent"));
		if (!StatusComponent)
		{
			return;
		}

		PlayerPawn->AddInstanceComponent(StatusComponent);
		StatusComponent->OnComponentCreated();
		StatusComponent->RegisterComponent();
		StatusComponent->Activate(true);
	}

	TrackedStatusComponent = StatusComponent;
	TrackedStatusComponent->ForceRefresh();

	UProjectInnerDoctrineComponent* InnerDoctrineComponent = PlayerPawn->FindComponentByClass<UProjectInnerDoctrineComponent>();
	if (!InnerDoctrineComponent)
	{
		InnerDoctrineComponent = NewObject<UProjectInnerDoctrineComponent>(PlayerPawn, UProjectInnerDoctrineComponent::StaticClass(), TEXT("ProjectInnerDoctrineComponent"));
		if (!InnerDoctrineComponent)
		{
			return;
		}

		PlayerPawn->AddInstanceComponent(InnerDoctrineComponent);
		InnerDoctrineComponent->OnComponentCreated();
		InnerDoctrineComponent->RegisterComponent();
		InnerDoctrineComponent->Activate(true);
	}

	TrackedInnerDoctrineComponent = InnerDoctrineComponent;

	UProjectRealtimeSnapshotComponent* RealtimeSnapshotComponent = PlayerPawn->FindComponentByClass<UProjectRealtimeSnapshotComponent>();
	if (!RealtimeSnapshotComponent)
	{
		RealtimeSnapshotComponent = NewObject<UProjectRealtimeSnapshotComponent>(PlayerPawn, UProjectRealtimeSnapshotComponent::StaticClass(), TEXT("ProjectRealtimeSnapshotComponent"));
		if (!RealtimeSnapshotComponent)
		{
			return;
		}

		PlayerPawn->AddInstanceComponent(RealtimeSnapshotComponent);
		RealtimeSnapshotComponent->OnComponentCreated();
		RealtimeSnapshotComponent->RegisterComponent();
		RealtimeSnapshotComponent->Activate(true);
	}

	TrackedRealtimeSnapshotComponent = RealtimeSnapshotComponent;
	TrackedRealtimeSnapshotComponent->ForceRefreshSnapshot();

	UProjectDefeatFlowComponent* DefeatFlowComponent = PlayerPawn->FindComponentByClass<UProjectDefeatFlowComponent>();
	if (!DefeatFlowComponent)
	{
		DefeatFlowComponent = NewObject<UProjectDefeatFlowComponent>(PlayerPawn, UProjectDefeatFlowComponent::StaticClass(), TEXT("ProjectDefeatFlowComponent"));
		if (DefeatFlowComponent)
		{
			PlayerPawn->AddInstanceComponent(DefeatFlowComponent);
			DefeatFlowComponent->OnComponentCreated();
			DefeatFlowComponent->RegisterComponent();
			DefeatFlowComponent->Activate(true);
		}
	}

	UProjectDefeatBlueprintBridgeComponent* DefeatBlueprintBridgeComponent = PlayerPawn->FindComponentByClass<UProjectDefeatBlueprintBridgeComponent>();
	if (!DefeatBlueprintBridgeComponent)
	{
		DefeatBlueprintBridgeComponent = NewObject<UProjectDefeatBlueprintBridgeComponent>(
			PlayerPawn,
			UProjectDefeatBlueprintBridgeComponent::StaticClass(),
			TEXT("ProjectDefeatBlueprintBridgeComponent"));
		if (DefeatBlueprintBridgeComponent)
		{
			PlayerPawn->AddInstanceComponent(DefeatBlueprintBridgeComponent);
			DefeatBlueprintBridgeComponent->OnComponentCreated();
			DefeatBlueprintBridgeComponent->RegisterComponent();
			DefeatBlueprintBridgeComponent->Activate(true);
		}
	}
}

void UProjectSurvivalNeedsSubsystem::EnsureNeedsHudWidget(APlayerController* PlayerController)
{
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController() || IsWorldUnsafeForWidgetCreation(World))
	{
		return;
	}

	if (!TrackedNeedsWidget)
	{
		const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
		const TSubclassOf<UProjectSurvivalNeedsWidget> NeedsWidgetClass = ResolveWidgetClass<UProjectSurvivalNeedsWidget>(
			UISettings ? UISettings->NeedsWidgetClass : FSoftClassPath(),
			UProjectSurvivalNeedsWidget::StaticClass(),
			TEXT("ProjectSurvivalNeedsWidget"));

		TrackedNeedsWidget = CreateWidget<UProjectSurvivalNeedsWidget>(
			PlayerController,
			NeedsWidgetClass,
			TEXT("ProjectSurvivalNeedsWidget"));
		if (!TrackedNeedsWidget)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalNeedsUI] Failed to create ProjectSurvivalNeedsWidget for %s"), *GetNameSafe(PlayerController));
			return;
		}

		if (!TrackedNeedsWidget->AddToPlayerScreen(ProjectSurvivalNeedsHudZOrder))
		{
			TrackedNeedsWidget->AddToViewport(ProjectSurvivalNeedsHudZOrder);
		}

		UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalNeedsUI] Created ProjectSurvivalNeedsWidget for %s"), *GetNameSafe(PlayerController));
		TrackedNeedsWidget->SetHudVisible(bNeedsHudVisible);
	}

	RefreshNeedsWidget(true);
}

void UProjectSurvivalNeedsSubsystem::EnsureInnerDoctrineHudWidget(APlayerController* PlayerController)
{
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController() || IsWorldUnsafeForWidgetCreation(World))
	{
		return;
	}

	if (!TrackedInnerDoctrineWidget)
	{
		const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
		const TSubclassOf<UProjectInnerDoctrineWidget> InnerDoctrineWidgetClass = ResolveWidgetClass<UProjectInnerDoctrineWidget>(
			UISettings ? UISettings->InnerDoctrineWidgetClass : FSoftClassPath(),
			UProjectInnerDoctrineWidget::StaticClass(),
			TEXT("ProjectInnerDoctrineWidget"));

		TrackedInnerDoctrineWidget = CreateWidget<UProjectInnerDoctrineWidget>(
			PlayerController,
			InnerDoctrineWidgetClass,
			TEXT("ProjectInnerDoctrineWidget"));
		if (!TrackedInnerDoctrineWidget)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectInnerDoctrineUI] Failed to create ProjectInnerDoctrineWidget for %s"), *GetNameSafe(PlayerController));
			return;
		}

		if (!TrackedInnerDoctrineWidget->AddToPlayerScreen(ProjectInnerDoctrineHudZOrder))
		{
			TrackedInnerDoctrineWidget->AddToViewport(ProjectInnerDoctrineHudZOrder);
		}

		UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectInnerDoctrineUI] Created ProjectInnerDoctrineWidget for %s"), *GetNameSafe(PlayerController));
		TrackedInnerDoctrineWidget->SetHudVisible(bNeedsHudVisible);
	}

	RefreshInnerDoctrineWidget(true);
}

void UProjectSurvivalNeedsSubsystem::EnsureStatusHudWidget(APlayerController* PlayerController)
{
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController() || IsWorldUnsafeForWidgetCreation(World))
	{
		return;
	}

	if (!TrackedStatusWidget)
	{
		const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
		const TSubclassOf<UProjectSurvivalStatusWidget> StatusWidgetClass = ResolveWidgetClass<UProjectSurvivalStatusWidget>(
			UISettings ? UISettings->StatusWidgetClass : FSoftClassPath(),
			UProjectSurvivalStatusWidget::StaticClass(),
			TEXT("ProjectSurvivalStatusWidget"));

		TrackedStatusWidget = CreateWidget<UProjectSurvivalStatusWidget>(PlayerController, StatusWidgetClass, TEXT("ProjectSurvivalStatusWidget"));
		if (!TrackedStatusWidget)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalNeedsUI] Failed to create ProjectSurvivalStatusWidget for %s"), *GetNameSafe(PlayerController));
			return;
		}

		if (!TrackedStatusWidget->AddToPlayerScreen(ProjectSurvivalStatusHudZOrder))
		{
			TrackedStatusWidget->AddToViewport(ProjectSurvivalStatusHudZOrder);
		}

		UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalStatusUI] Created ProjectSurvivalStatusWidget for %s"), *GetNameSafe(PlayerController));
		TrackedStatusWidget->SetHudVisible(bNeedsHudVisible);
	}

	RefreshStatusWidget(true);
}
