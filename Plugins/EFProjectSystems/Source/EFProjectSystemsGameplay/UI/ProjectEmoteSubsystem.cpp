#include "UI/ProjectEmoteSubsystem.h"

#include "ACFTrainingComponent.h"
#include "ContentPolicy/ProjectContentPolicySubsystem.h"
#include "EFProjectInputSettings.h"
#include "EFCharacterCreationSubsystem.h"
#include "Components/ActorComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Intimacy/ProjectIntimacySettings.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Social/ProjectSocialSubsystem.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "UI/ProjectActivityFeedSubsystem.h"
#include "UI/ProjectEmoteMenuWidget.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "Training/ProjectTrainingSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEmoteSubsystem, Log, All);

namespace ProjectEmoteSubsystemPrivate
{
	constexpr int32 MenuZOrder = 10000;
	constexpr int32 InputPriority = 2;
	constexpr int32 ActiveEmoteInputPriority = 50;
	constexpr float CombatMenuLockoutSeconds = 8.0f;

	const FName RootActionsOptionId(TEXT("Root.Actions"));
	const FName RootObjectsOptionId(TEXT("Root.Objects"));
	const FName RootCancelOptionId(TEXT("Root.Cancel"));
	const FName ActionsObjectsOptionId(TEXT("Actions.Objects"));
	const FName ActionsSpecialOptionId(TEXT("Actions.Special"));
	const FName SystemPrivateRespawnActionId(TEXT("Presentation.System.Respawn.Private01"));
	const FName PrivateSoloFolderId(TEXT("Intimacy.Solo"));
	const FName PrivateSoloInteractionId(TEXT("Intimacy.Solo.Private01"));
	const FName LocalPlayerParticipantId(TEXT("Player.Local"));
	const FName BackOptionId(TEXT("Navigation.Back"));
	const FName EmptyObjectsOptionId(TEXT("Objects.Empty"));
	const FString TrainingInteractionPrefix(TEXT("Actions.Training."));

	struct FSetHudEnabledParams
	{
		bool bEnabled = false;
	};
}

void UProjectEmoteSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedInputComponent = nullptr;
	ActiveEmoteInputComponent = nullptr;
	TrackedEmoteComponent = nullptr;
	TrackedTrainingComponent = nullptr;
	TrackedEmoteMenuWidget = nullptr;
	MenuStateSnapshot = FProjectEmoteMenuStateSnapshot();
	DelayedRecoverySnapshot = FProjectEmoteMenuStateSnapshot();
	ActiveRuntimeActionRequest = FProjectEmoteRuntimeActionRequest();
	CachedVisibleMenuNodes.Reset();
	MenuNodeStack.Reset();
	PrivateSoloPresentationRequestId.Invalidate();
	bMenuOpen = false;
	bActiveEmoteInputCaptureApplied = false;
	bRuntimeActionActive = false;

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr)
	{
		ContentPolicy->RegisterMatureContentProvider(this);
	}
}

void UProjectEmoteSubsystem::Deinitialize()
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;
	if (ContentPolicy)
	{
		ContentPolicy->UnregisterMatureContentProvider(this);
	}
	else if (PrivateSoloPresentationRequestId.IsValid())
	{
		CancelMaturePresentation(PrivateSoloPresentationRequestId);
	}

	DetachFromTrackedPlayerController(true);
	Super::Deinitialize();
}

void UProjectEmoteSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();

	if (IsGamePauseBlockingEmoteFlow())
	{
		CleanupMenuAndEmoteState();
	}

	RefreshActiveEmoteInputCapture();

	if (bRuntimeActionActive
		&& (!TrackedEmoteComponent || (!TrackedEmoteComponent->IsEmoteActive() && !TrackedEmoteComponent->IsEmoteTransitionPending())))
	{
		CompleteRuntimeAction(EProjectEmoteRuntimeActionEndReason::Completed);
	}

	if (PrivateSoloPresentationRequestId.IsValid() && !IsPrivateSoloPresentationPlaying())
	{
		EndPrivateSoloPresentationRegistration(true);
	}
}

TStatId UProjectEmoteSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectEmoteSubsystem, STATGROUP_Tickables);
}

bool UProjectEmoteSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

bool UProjectEmoteSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectEmoteSubsystem::SupportsMatureFeature(
	const EProjectOptionalMatureFeature Feature) const
{
	return Feature == EProjectOptionalMatureFeature::PrivateSoloPresentation;
}

bool UProjectEmoteSubsystem::IsMatureFeatureAvailable(
	const EProjectOptionalMatureFeature Feature) const
{
	const UWorld* World = GetWorld();
	return SupportsMatureFeature(Feature)
		&& World
		&& DoesSupportWorldType(World->WorldType);
}

bool UProjectEmoteSubsystem::TryBeginMaturePresentation(
	const FProjectMaturePresentationRequest& Request)
{
	if (!SupportsMatureFeature(Request.Feature)
		|| !Request.RequestId.IsValid()
		|| Request.PresentationId != ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId
		|| IsValid(Request.SecondaryParticipant.Get())
		|| PrivateSoloPresentationRequestId.IsValid())
	{
		return false;
	}

	TryResolveRuntimeContext();
	if (!IsValid(TrackedPlayerPawn.Get())
		|| Request.PrimaryParticipant.Get() != TrackedPlayerPawn.Get()
		|| !UProjectIntimacySubsystem::HasRequiredCharismaForActor(TrackedPlayerPawn.Get())
		|| !TrackedEmoteComponent
		|| bRuntimeActionActive)
	{
		return false;
	}

	CloseMenu(false);
	if (!TrackedEmoteComponent->StartInteractionById(
			ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId))
	{
		return false;
	}

	PrivateSoloPresentationRequestId = Request.RequestId;
	ApplyActiveEmoteInputCapture();
	return true;
}

void UProjectEmoteSubsystem::CancelMaturePresentation(const FGuid& RequestId)
{
	if (!RequestId.IsValid() || RequestId != PrivateSoloPresentationRequestId)
	{
		return;
	}

	PrivateSoloPresentationRequestId.Invalidate();
	if (IsPrivateSoloPresentationPlaying() && TrackedEmoteComponent)
	{
		TrackedEmoteComponent->StopEmote();
		RestoreActiveEmoteInputCapture();
	}
}

void UProjectEmoteSubsystem::RequestToggleEmoteMenu()
{
	HandleToggleMenuPressed();
}

void UProjectEmoteSubsystem::RequestNavigateEmoteMenu(const int32 Direction)
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->NavigateSelectionByDirection(Direction);
	}
}

void UProjectEmoteSubsystem::RequestSelectEmoteMenuOption(const int32 SelectionIndex)
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->SelectOptionByIndex(SelectionIndex);
	}
}

void UProjectEmoteSubsystem::RequestConfirmEmoteMenuSelection()
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->ConfirmCurrentSelection();
	}
}

void UProjectEmoteSubsystem::RequestActivateEmoteMenuOption(const int32 SelectionIndex)
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->ActivateOptionByIndex(SelectionIndex);
	}
}

void UProjectEmoteSubsystem::RequestCancelEmoteMenu()
{
	if (bMenuOpen)
	{
		CloseMenu(true);
	}
}

void UProjectEmoteSubsystem::RequestCancelActiveEmote()
{
	if (bRuntimeActionActive)
	{
		CancelRuntimeAction(TEXT("RequestCancelActiveEmote"));
		return;
	}

	if (TrackedEmoteComponent)
	{
		if (TrackedEmoteComponent->IsActiveInteractionBlueprintScene())
		{
			TrackedEmoteComponent->StopEmote();
			RestoreActiveEmoteInputCapture();
			return;
		}

		TrackedEmoteComponent->StopEmote();
	}
}

bool UProjectEmoteSubsystem::RequestStartInteractionById(const FName InteractionId)
{
	TryResolveRuntimeContext();
	if (InteractionId.IsNone())
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Quick interaction start failed: no interaction id."));
		return false;
	}

	if (bRuntimeActionActive)
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Quick interaction %s blocked by active runtime action."), *InteractionId.ToString());
		return false;
	}

	if (!TrackedEmoteComponent)
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Quick interaction %s failed: no emote component."), *InteractionId.ToString());
		return false;
	}

	if (TrackedEmoteComponent->IsEmoteActive() || TrackedEmoteComponent->IsEmoteTransitionPending())
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Quick interaction %s blocked by active emote/action."), *InteractionId.ToString());
		return false;
	}

	return StartInteractionById(InteractionId);
}

bool UProjectEmoteSubsystem::StartRuntimeAction(const FProjectEmoteRuntimeActionRequest& Request)
{
	TryResolveRuntimeContext();
	if (bRuntimeActionActive)
	{
		CancelRuntimeAction(TEXT("StartRuntimeActionReplacement"));
	}
	CloseMenu(false);

	if (!TrackedEmoteComponent)
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Runtime action %s failed: no emote component."), *Request.RuntimeActionId.ToString());
		return false;
	}

	FProjectEmoteRuntimeActionRequest NormalizedRequest = Request;
	if (NormalizedRequest.RuntimeActionId.IsNone())
	{
		NormalizedRequest.RuntimeActionId = NormalizedRequest.InteractionId;
	}

	const FName ResolvedInteractionId = ResolveRuntimeActionInteractionId(NormalizedRequest);
	if (ResolvedInteractionId.IsNone())
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Runtime action %s failed: no interaction id."), *NormalizedRequest.RuntimeActionId.ToString());
		return false;
	}

	NormalizedRequest.InteractionId = ResolvedInteractionId;

	const bool bAuthoritativeMatureDefeatPresentation =
		NormalizedRequest.Source == EProjectEmoteRuntimeActionSource::Respawn
		&& NormalizedRequest.RuntimeActionId == ProjectEmoteSubsystemPrivate::SystemPrivateRespawnActionId
		&& ResolvedInteractionId == ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId;
	const bool bInteractionStarted = bAuthoritativeMatureDefeatPresentation
		? TrackedEmoteComponent->StartMatureDefeatPresentationById(ResolvedInteractionId)
		: TrackedEmoteComponent->StartRuntimeInteractionById(ResolvedInteractionId);
	if (!bInteractionStarted)
	{
		UE_LOG(
			LogProjectEmoteSubsystem,
			Warning,
			TEXT("[ProjectEmote] Runtime action %s failed to start interaction %s."),
			*NormalizedRequest.RuntimeActionId.ToString(),
			*ResolvedInteractionId.ToString());
		OnRuntimeActionEnded.Broadcast(NormalizedRequest, EProjectEmoteRuntimeActionEndReason::Failed);
		return false;
	}

	ActiveRuntimeActionRequest = NormalizedRequest;
	bRuntimeActionActive = true;
	ApplyActiveEmoteInputCapture();
	OnRuntimeActionStarted.Broadcast(ActiveRuntimeActionRequest);
	return true;
}

bool UProjectEmoteSubsystem::CancelRuntimeAction(const FName ReasonId)
{
	if (!bRuntimeActionActive)
	{
		return false;
	}

	const FProjectEmoteRuntimeActionRequest RequestBeforeCancel = ActiveRuntimeActionRequest;
	if (!RequestBeforeCancel.bAllowCancel)
	{
		return false;
	}

	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->StopEmote();
		if (RequestBeforeCancel.bRestoreMovementOnEnd)
		{
			TrackedEmoteComponent->OverrideDelayedPostEmoteRecovery(0.0f, false, false);
		}
	}

	RestoreActiveEmoteInputCapture();
	CompleteRuntimeAction(EProjectEmoteRuntimeActionEndReason::Cancelled);
	UE_LOG(LogProjectEmoteSubsystem, Log, TEXT("[ProjectEmote] Runtime action %s cancelled by %s."), *RequestBeforeCancel.RuntimeActionId.ToString(), *ReasonId.ToString());
	return true;
}

bool UProjectEmoteSubsystem::IsRuntimeActionActive() const
{
	return bRuntimeActionActive;
}

FName UProjectEmoteSubsystem::GetActiveRuntimeActionId() const
{
	return bRuntimeActionActive ? ActiveRuntimeActionRequest.RuntimeActionId : NAME_None;
}

EProjectEmoteRuntimeActionSource UProjectEmoteSubsystem::GetActiveRuntimeActionSource() const
{
	return bRuntimeActionActive ? ActiveRuntimeActionRequest.Source : EProjectEmoteRuntimeActionSource::Script;
}

bool UProjectEmoteSubsystem::IsEmoteMenuOpen() const
{
	return bMenuOpen;
}

int32 UProjectEmoteSubsystem::GetEmoteMenuSelectedIndex() const
{
	return TrackedEmoteMenuWidget ? TrackedEmoteMenuWidget->GetSelectedIndex() : INDEX_NONE;
}

bool UProjectEmoteSubsystem::IsTrackedEmoteMenuWidgetInViewport() const
{
	return TrackedEmoteMenuWidget && TrackedEmoteMenuWidget->IsInViewport();
}

FString UProjectEmoteSubsystem::GetTrackedEmoteMenuWidgetClassName() const
{
	return TrackedEmoteMenuWidget ? TrackedEmoteMenuWidget->GetClass()->GetName() : FString();
}

bool UProjectEmoteSubsystem::IsEmoteActive() const
{
	return TrackedEmoteComponent && TrackedEmoteComponent->IsEmoteActive();
}

bool UProjectEmoteSubsystem::IsEmoteTransitionPending() const
{
	return TrackedEmoteComponent && TrackedEmoteComponent->IsEmoteTransitionPending();
}

bool UProjectEmoteSubsystem::IsEmotePlaybackStarted() const
{
	return TrackedEmoteComponent && TrackedEmoteComponent->IsEmotePlaybackStarted();
}

int32 UProjectEmoteSubsystem::GetActiveEmoteValue() const
{
	return TrackedEmoteComponent ? TrackedEmoteComponent->GetActiveEmoteValue() : 0;
}

void UProjectEmoteSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (OldPawn && OldPawn == TrackedPlayerPawn)
	{
		// The old pawn has already been detached from ACF's targeting runtime.
		// Cleanup must not attempt to restore its pre-interaction target.
		CleanupMenuAndEmoteState(false);
	}

	TrackedPlayerPawn = NewPawn;
	TrackedEmoteComponent = nullptr;
	TrackedTrainingComponent = nullptr;
	EnsureEmoteComponent(NewPawn);
}

void UProjectEmoteSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		DetachFromTrackedPlayerController(true);
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController(true);
		return;
	}

	AttachToPlayerController(PlayerController);
	EnsureEmoteComponent(PlayerController->GetPawn());
}

void UProjectEmoteSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindInputToTrackedPlayerController();
		if (TrackedPlayerPawn != PlayerController->GetPawn())
		{
			HandleTrackedPawnChanged(TrackedPlayerPawn, PlayerController->GetPawn());
		}
		return;
	}

	DetachFromTrackedPlayerController(true);

	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	BindInputToTrackedPlayerController();
}

void UProjectEmoteSubsystem::DetachFromTrackedPlayerController(const bool bStopActiveEmote)
{
	if (bMenuOpen)
	{
		CloseMenu(false);
	}

	if (bStopActiveEmote && TrackedEmoteComponent)
	{
		TrackedEmoteComponent->StopEmote(false);
	}

	if (TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->RemoveFromParent();
		TrackedEmoteMenuWidget = nullptr;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	RestoreActiveEmoteInputCapture();
	UnbindInputFromTrackedPlayerController();
	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedEmoteComponent = nullptr;
	TrackedTrainingComponent = nullptr;
	MenuStateSnapshot = FProjectEmoteMenuStateSnapshot();
	DelayedRecoverySnapshot = FProjectEmoteMenuStateSnapshot();
	CachedVisibleMenuNodes.Reset();
	MenuNodeStack.Reset();
	bMenuOpen = false;
}

void UProjectEmoteSubsystem::BindInputToTrackedPlayerController()
{
	if (!TrackedPlayerController || TrackedInputComponent)
	{
		return;
	}

	TrackedInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectEmoteInputComponent"));
	if (!TrackedInputComponent)
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Failed to create input component for %s"), *GetNameSafe(TrackedPlayerController));
		return;
	}

	TrackedInputComponent->bBlockInput = false;
	TrackedInputComponent->Priority = ProjectEmoteSubsystemPrivate::InputPriority;
	TrackedInputComponent->RegisterComponent();
	const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get();
	TrackedInputComponent->BindKey(InputSettings ? InputSettings->ToggleInteractionMenuKey : EKeys::Y, IE_Pressed, this, &ThisClass::HandleToggleMenuPressed);
	TrackedPlayerController->PushInputComponent(TrackedInputComponent);
}

void UProjectEmoteSubsystem::UnbindInputFromTrackedPlayerController()
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

void UProjectEmoteSubsystem::EnsureEmoteComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		TrackedPlayerPawn = nullptr;
		TrackedEmoteComponent = nullptr;
		TrackedTrainingComponent = nullptr;
		return;
	}

	TrackedPlayerPawn = Pawn;
	UACFTrainingComponent* PreviousTrainingComponent = TrackedTrainingComponent;
	const bool bHadTrainingComponent = Pawn->FindComponentByClass<UACFTrainingComponent>() != nullptr;
	UACFTrainingComponent* TrainingComponent = EnsureTrainingComponent(Pawn);

	UProjectEmoteComponent* EmoteComponent = Pawn->FindComponentByClass<UProjectEmoteComponent>();
	const bool bEmoteComponentChanged = EmoteComponent != TrackedEmoteComponent;
	bool bCreatedEmoteComponent = false;
	if (!EmoteComponent)
	{
		EmoteComponent = NewObject<UProjectEmoteComponent>(Pawn, UProjectEmoteComponent::StaticClass(), TEXT("ProjectEmoteComponent"));
		if (EmoteComponent)
		{
			Pawn->AddInstanceComponent(EmoteComponent);
			EmoteComponent->OnComponentCreated();
			EmoteComponent->RegisterComponent();
			EmoteComponent->Activate(true);
			bCreatedEmoteComponent = true;
		}
	}

	TrackedEmoteComponent = EmoteComponent;
	if (TrackedEmoteComponent && (bCreatedEmoteComponent || bEmoteComponentChanged || !bHadTrainingComponent || TrainingComponent != PreviousTrainingComponent))
	{
		TrackedEmoteComponent->RefreshMenuCatalog();
	}
	TrackedTrainingComponent = TrainingComponent;
}

UACFTrainingComponent* UProjectEmoteSubsystem::EnsureTrainingComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		TrackedTrainingComponent = nullptr;
		return nullptr;
	}

	UACFTrainingComponent* TrainingComponent = Pawn->FindComponentByClass<UACFTrainingComponent>();
	if (!TrainingComponent)
	{
		TrainingComponent = NewObject<UACFTrainingComponent>(Pawn, UACFTrainingComponent::StaticClass(), TEXT("ACFTrainingComponent"));
		if (TrainingComponent)
		{
			Pawn->AddInstanceComponent(TrainingComponent);
			TrainingComponent->OnComponentCreated();
			TrainingComponent->RegisterComponent();
			TrainingComponent->Activate(true);
		}
	}

	TrackedTrainingComponent = TrainingComponent;
	return TrainingComponent;
}

void UProjectEmoteSubsystem::EnsureEmoteMenuWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedEmoteMenuWidget)
	{
		return;
	}

	const TSubclassOf<UProjectEmoteMenuWidget> MenuWidgetClass =
		ProjectWidgetClassResolver::DiscoverWidgetClass<UProjectEmoteMenuWidget>(TEXT("ProjectEmoteMenu"));

	TrackedEmoteMenuWidget = CreateWidget<UProjectEmoteMenuWidget>(PlayerController, MenuWidgetClass, TEXT("ProjectEmoteMenuWidget"));
	if (!TrackedEmoteMenuWidget)
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Failed to create ProjectEmoteMenuWidget for %s"), *GetNameSafe(PlayerController));
		return;
	}

	TrackedEmoteMenuWidget->OnOptionConfirmed.AddUObject(this, &ThisClass::HandleMenuOptionConfirmed);
	TrackedEmoteMenuWidget->OnCancelRequested.AddUObject(this, &ThisClass::HandleMenuCancelRequested);
	TrackedEmoteMenuWidget->OnBackRequested.AddUObject(this, &ThisClass::HandleActiveEmoteMenuNavigateBack);
}

void UProjectEmoteSubsystem::OpenMenu()
{
	if (bMenuOpen || !TrackedPlayerController || !TrackedPlayerPawn || IsGamePauseBlockingEmoteFlow() || IsCombatBlockingEmoteMenu())
	{
		return;
	}

	EnsureEmoteMenuWidget(TrackedPlayerController);
	if (!TrackedEmoteMenuWidget)
	{
		return;
	}

	if (!TrackedEmoteMenuWidget->IsInViewport())
	{
		if (!TrackedEmoteMenuWidget->AddToPlayerScreen(ProjectEmoteSubsystemPrivate::MenuZOrder))
		{
			TrackedEmoteMenuWidget->AddToViewport(ProjectEmoteSubsystemPrivate::MenuZOrder);
		}
	}

	MenuNodeStack.Reset();
	RefreshCurrentMenuModel(GetPreferredRootOptionId());
	ApplyMenuInputCapture();
	bMenuOpen = true;
	RefreshMenuFocusNextTick();
}

void UProjectEmoteSubsystem::CloseMenu(const bool bScheduleDelayedRecoveryCheck)
{
	if (!bMenuOpen && !TrackedEmoteMenuWidget)
	{
		return;
	}

	const FProjectEmoteMenuStateSnapshot SnapshotBeforeRestore = MenuStateSnapshot;

	if (TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->RemoveFromParent();
	}

	RestoreMenuInputCapture();
	CachedVisibleMenuNodes.Reset();
	MenuNodeStack.Reset();
	bMenuOpen = false;

	if (bScheduleDelayedRecoveryCheck)
	{
		ScheduleDelayedMovementRecoveryCheck(SnapshotBeforeRestore);
	}
}

bool UProjectEmoteSubsystem::StartInteractionById(const FName InteractionId)
{
	FName TrainingId;
	if (TryParseTrainingInteractionId(InteractionId, TrainingId))
	{
		StartTrainingInteractionById(InteractionId);
		return true;
	}

	if (InteractionId == ProjectEmoteSubsystemPrivate::PrivateSoloFolderId
		|| InteractionId == ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId)
	{
		return RequestPrivateSoloPresentation();
	}

	CloseMenu(false);

	if (TrackedEmoteComponent)
	{
		if (TrackedEmoteComponent->StartInteractionById(InteractionId))
		{
			ApplyActiveEmoteInputCapture();
			return true;
		}
	}

	UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Failed to start interaction %s."), *InteractionId.ToString());
	return false;
}

bool UProjectEmoteSubsystem::RequestPrivateSoloPresentation()
{
	TryResolveRuntimeContext();
	EnsureTrackedPlayerSocialRegistration();
	if (!IsValid(TrackedPlayerPawn.Get())
		|| !TrackedEmoteComponent
		|| PrivateSoloPresentationRequestId.IsValid()
		|| bRuntimeActionActive)
	{
		return false;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;
	if (!ContentPolicy)
	{
		return false;
	}

	FProjectMaturePresentationRequest Request;
	Request.Feature = EProjectOptionalMatureFeature::PrivateSoloPresentation;
	Request.PresentationId = ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId;
	Request.PrimaryParticipant = TrackedPlayerPawn.Get();
	const FGuid RequestId = ContentPolicy->TryBeginMaturePresentation(Request);
	if (!RequestId.IsValid()
		|| RequestId != PrivateSoloPresentationRequestId
		|| !IsPrivateSoloPresentationPlaying())
	{
		if (RequestId.IsValid())
		{
			ContentPolicy->CancelMaturePresentation(RequestId);
		}
		return false;
	}

	return true;
}

void UProjectEmoteSubsystem::EndPrivateSoloPresentationRegistration(
	const bool bNotifyPolicy)
{
	if (!PrivateSoloPresentationRequestId.IsValid())
	{
		return;
	}

	const FGuid RequestId = PrivateSoloPresentationRequestId;
	PrivateSoloPresentationRequestId.Invalidate();
	if (!bNotifyPolicy)
	{
		return;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr)
	{
		ContentPolicy->NotifyMaturePresentationEnded(RequestId, this);
	}
}

bool UProjectEmoteSubsystem::IsPrivateSoloPresentationPlaying() const
{
	if (!TrackedEmoteComponent)
	{
		return false;
	}

	FProjectEmoteInteractionDefinition ActiveDefinition;
	return TrackedEmoteComponent->FindActiveInteractionDefinition(ActiveDefinition)
		&& ActiveDefinition.InteractionId == ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId;
}

void UProjectEmoteSubsystem::EnsureTrackedPlayerSocialRegistration()
{
	AActor* PlayerActor = TrackedPlayerPawn.Get();
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	if (!IsValid(PlayerActor) || !SocialSubsystem)
	{
		return;
	}

	const UProjectIntimacySettings* IntimacySettings = UProjectIntimacySettings::Get();
	const bool bVerifiedAdult = IntimacySettings
		&& IntimacySettings->bPlayerCharacterAdultVerified;

	FProjectSocialParticipantState PlayerState;
	const bool bAlreadyRegistered = SocialSubsystem->TryGetParticipantState(
		PlayerActor,
		PlayerState);
	if (bAlreadyRegistered
		&& PlayerState.ParticipantId == ProjectEmoteSubsystemPrivate::LocalPlayerParticipantId
		&& PlayerState.bVerifiedAdult == bVerifiedAdult)
	{
		return;
	}

	if (!bAlreadyRegistered)
	{
		PlayerState.bAlive = true;
		PlayerState.bConscious = true;
		PlayerState.bHostile = false;
	}
	PlayerState.ParticipantId = ProjectEmoteSubsystemPrivate::LocalPlayerParticipantId;
	PlayerState.bVerifiedAdult = bVerifiedAdult;
	SocialSubsystem->RegisterOrUpdateParticipant(PlayerActor, PlayerState);
}

bool UProjectEmoteSubsystem::TryParseTrainingInteractionId(const FName InteractionId, FName& OutTrainingId) const
{
	OutTrainingId = NAME_None;
	const FString InteractionString = InteractionId.ToString();
	if (!InteractionString.StartsWith(ProjectEmoteSubsystemPrivate::TrainingInteractionPrefix))
	{
		return false;
	}

	const FString TrainingIdString = InteractionString.RightChop(ProjectEmoteSubsystemPrivate::TrainingInteractionPrefix.Len());
	if (TrainingIdString.IsEmpty())
	{
		return false;
	}

	OutTrainingId = FName(*TrainingIdString);
	return !OutTrainingId.IsNone();
}

void UProjectEmoteSubsystem::StartTrainingInteractionById(const FName InteractionId)
{
	FName TrainingId;
	if (!TryParseTrainingInteractionId(InteractionId, TrainingId))
	{
		return;
	}

	UACFTrainingComponent* TrainingComponent = EnsureTrainingComponent(TrackedPlayerPawn);
	if (!TrainingComponent)
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Cannot start training %s without an ACF training component"), *TrainingId.ToString());
		return;
	}

	FText Reason;
	if (!TrainingComponent->CanStartTraining(TrainingId, Reason))
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Training %s blocked: %s"), *TrainingId.ToString(), *Reason.ToString());
		return;
	}

	FACFTrainingDefinition TrainingDefinition;
	if (!TrainingComponent->GetTrainingDefinition(TrainingId, TrainingDefinition))
	{
		UE_LOG(LogProjectEmoteSubsystem, Warning, TEXT("[ProjectEmote] Training %s has no definition after validation"), *TrainingId.ToString());
		return;
	}

	CloseMenu(false);

	if (!TrainingComponent->StartTrainingById(TrainingId))
	{
		return;
	}

	const int32 Difficulty = FMath::Clamp(TrainingDefinition.MinigameDifficulty, 1, 100);
	const int32 CunningLevel = ResolveTrainingCunningLevel(TrackedPlayerPawn);
	const float SpeedMultiplier = UProjectLockpickableComponent::ComputeSpeedMultiplier(Difficulty, CunningLevel);
	const float TargetHalfRange = UProjectLockpickableComponent::ComputeTargetHalfRange(Difficulty, CunningLevel);
	if (!TrainingComponent->StartTimingMinigameSession(Difficulty, CunningLevel, SpeedMultiplier, TargetHalfRange, 0.5f))
	{
		TrainingComponent->CancelTraining();
		return;
	}

	if (!TrackedEmoteComponent || !TrackedEmoteComponent->StartInteractionById(InteractionId))
	{
		TrainingComponent->CancelTraining();
		return;
	}

	ApplyActiveEmoteInputCapture();

	UWorld* World = GetWorld();
	UProjectTrainingSubsystem* TrainingSubsystem = World ? World->GetSubsystem<UProjectTrainingSubsystem>() : nullptr;
	if (!TrainingSubsystem || !TrainingSubsystem->OpenTraining(TrainingComponent, TrackedPlayerPawn))
	{
		TrainingComponent->CancelTraining();
		if (TrackedEmoteComponent)
		{
			TrackedEmoteComponent->StopEmote();
		}
		RestoreActiveEmoteInputCapture();
	}
}

int32 UProjectEmoteSubsystem::ResolveTrainingCunningLevel(APawn* Pawn) const
{
	if (!Pawn)
	{
		return 0;
	}

	if (const UProjectInnerDoctrineComponent* DoctrineComponent = Pawn->FindComponentByClass<UProjectInnerDoctrineComponent>())
	{
		return DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Cunning);
	}

	if (AController* Controller = Pawn->GetController())
	{
		if (const UProjectInnerDoctrineComponent* DoctrineComponent = Controller->FindComponentByClass<UProjectInnerDoctrineComponent>())
		{
			return DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Cunning);
		}
	}

	return 0;
}

FName UProjectEmoteSubsystem::ResolveRuntimeActionInteractionId(const FProjectEmoteRuntimeActionRequest& Request) const
{
	if (!Request.InteractionId.IsNone()
		&& Request.InteractionId != ProjectEmoteSubsystemPrivate::SystemPrivateRespawnActionId)
	{
		return Request.InteractionId;
	}

	if (Request.RuntimeActionId == ProjectEmoteSubsystemPrivate::SystemPrivateRespawnActionId
		|| Request.InteractionId == ProjectEmoteSubsystemPrivate::SystemPrivateRespawnActionId)
	{
		return ProjectEmoteSubsystemPrivate::PrivateSoloInteractionId;
	}

	return Request.RuntimeActionId;
}

void UProjectEmoteSubsystem::CompleteRuntimeAction(const EProjectEmoteRuntimeActionEndReason EndReason)
{
	if (!bRuntimeActionActive)
	{
		return;
	}

	const FProjectEmoteRuntimeActionRequest CompletedRequest = ActiveRuntimeActionRequest;
	bRuntimeActionActive = false;
	ActiveRuntimeActionRequest = FProjectEmoteRuntimeActionRequest();
	OnRuntimeActionEnded.Broadcast(CompletedRequest, EndReason);
}

void UProjectEmoteSubsystem::CleanupMenuAndEmoteState(const bool bRestoreTargetActor)
{
	if (bMenuOpen)
	{
		CloseMenu(false);
	}

	if (bRuntimeActionActive)
	{
		if (TrackedEmoteComponent)
		{
			TrackedEmoteComponent->StopEmote(bRestoreTargetActor);
		}
		CompleteRuntimeAction(EProjectEmoteRuntimeActionEndReason::Interrupted);
		return;
	}

	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->StopEmote(bRestoreTargetActor);
	}
}

void UProjectEmoteSubsystem::ApplyMenuInputCapture()
{
	MenuStateSnapshot = FProjectEmoteMenuStateSnapshot();
	ApplyMenuHudSuppression();

	if (TrackedPlayerController)
	{
		MenuStateSnapshot.bHasSavedControllerState = true;
		MenuStateSnapshot.bWasMoveInputIgnored = TrackedPlayerController->IsMoveInputIgnored();
		MenuStateSnapshot.bWasLookInputIgnored = TrackedPlayerController->IsLookInputIgnored();

		TrackedPlayerController->FlushPressedKeys();
		if (!MenuStateSnapshot.bWasMoveInputIgnored)
		{
			TrackedPlayerController->SetIgnoreMoveInput(true);
			MenuStateSnapshot.bAppliedMoveInputIgnore = true;
		}

		if (!MenuStateSnapshot.bWasLookInputIgnored)
		{
			TrackedPlayerController->SetIgnoreLookInput(true);
			MenuStateSnapshot.bAppliedLookInputIgnore = true;
		}

		TrackedPlayerController->DisableInput(TrackedPlayerController);
		TrackedPlayerController->bShowMouseCursor = false;
		TrackedPlayerController->bEnableClickEvents = false;
		TrackedPlayerController->bEnableMouseOverEvents = false;

		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(TrackedEmoteMenuWidget ? TrackedEmoteMenuWidget->TakeWidget() : TSharedPtr<SWidget>());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		TrackedPlayerController->SetInputMode(InputMode);
	}

	if (TrackedPlayerPawn)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			Character->StopJumping();
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				MenuStateSnapshot.bHasSavedMovementState = true;
				MenuStateSnapshot.PreviousMovementMode = CharacterMovement->MovementMode;
				MenuStateSnapshot.PreviousCustomMovementMode = CharacterMovement->CustomMovementMode;
				CharacterMovement->StopMovementImmediately();
				CharacterMovement->DisableMovement();
			}
		}

		if (TrackedPlayerController)
		{
			TrackedPlayerPawn->DisableInput(TrackedPlayerController);
			MenuStateSnapshot.bPawnInputSuspended = true;
		}
	}
}

void UProjectEmoteSubsystem::RestoreMenuInputCapture()
{
	RestoreMenuHudSuppression();

	if (TrackedPlayerPawn && MenuStateSnapshot.bPawnInputSuspended)
	{
		if (APlayerController* OwningController = Cast<APlayerController>(TrackedPlayerPawn->GetController()))
		{
			TrackedPlayerPawn->EnableInput(OwningController);
		}
		else if (TrackedPlayerController)
		{
			TrackedPlayerPawn->EnableInput(TrackedPlayerController);
		}
	}

	if (TrackedPlayerPawn && MenuStateSnapshot.bHasSavedMovementState)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				CharacterMovement->SetMovementMode(MenuStateSnapshot.PreviousMovementMode, MenuStateSnapshot.PreviousCustomMovementMode);
			}
		}
	}

	if (TrackedPlayerController && MenuStateSnapshot.bHasSavedControllerState)
	{
		TrackedPlayerController->EnableInput(TrackedPlayerController);

		if (MenuStateSnapshot.bAppliedMoveInputIgnore)
		{
			TrackedPlayerController->SetIgnoreMoveInput(false);
		}

		if (MenuStateSnapshot.bAppliedLookInputIgnore)
		{
			TrackedPlayerController->SetIgnoreLookInput(false);
		}

		TrackedPlayerController->bShowMouseCursor = false;
		TrackedPlayerController->bEnableClickEvents = false;
		TrackedPlayerController->bEnableMouseOverEvents = false;

		FInputModeGameOnly InputMode;
		TrackedPlayerController->SetInputMode(InputMode);
		TrackedPlayerController->FlushPressedKeys();
	}

	MenuStateSnapshot = FProjectEmoteMenuStateSnapshot();
}

void UProjectEmoteSubsystem::ApplyMenuHudSuppression()
{
	if (UWorld* World = GetWorld())
	{
		if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			MenuStateSnapshot.bHasSavedProjectHudVisibility = true;
			MenuStateSnapshot.bWasProjectHudVisible = NeedsSubsystem->IsNeedsHudVisible();
			NeedsSubsystem->SetNeedsHudVisible(false);
		}
	}

	if (!TrackedPlayerController)
	{
		return;
	}

	if (AHUD* HudActor = TrackedPlayerController->GetHUD())
	{
		MenuStateSnapshot.bHasSavedPlayerHudVisibility = true;
		MenuStateSnapshot.bWasPlayerHudVisible = HudActor->bShowHUD;
		HudActor->bShowHUD = false;
		MenuStateSnapshot.bAppliedAcfHudDisable = TrySetReflectedHudEnabled(HudActor, false);
	}
}

void UProjectEmoteSubsystem::RestoreMenuHudSuppression()
{
	if (TrackedPlayerController && MenuStateSnapshot.bHasSavedPlayerHudVisibility)
	{
		if (AHUD* HudActor = TrackedPlayerController->GetHUD())
		{
			HudActor->bShowHUD = MenuStateSnapshot.bWasPlayerHudVisible;

			if (MenuStateSnapshot.bAppliedAcfHudDisable)
			{
				TrySetReflectedHudEnabled(HudActor, MenuStateSnapshot.bWasPlayerHudVisible);
			}
		}
	}

	if (MenuStateSnapshot.bHasSavedProjectHudVisibility)
	{
		if (UWorld* World = GetWorld())
		{
			if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
			{
				NeedsSubsystem->SetNeedsHudVisible(MenuStateSnapshot.bWasProjectHudVisible);
			}
		}
	}
}

bool UProjectEmoteSubsystem::TrySetReflectedHudEnabled(AHUD* HudActor, const bool bEnabled) const
{
	if (!HudActor)
	{
		return false;
	}

	UFunction* SetHudEnabledFunction = HudActor->FindFunction(TEXT("SetHudEnabled"));
	if (!SetHudEnabledFunction)
	{
		return false;
	}

	ProjectEmoteSubsystemPrivate::FSetHudEnabledParams Parameters;
	Parameters.bEnabled = bEnabled;
	HudActor->ProcessEvent(SetHudEnabledFunction, &Parameters);
	return true;
}

void UProjectEmoteSubsystem::RefreshMenuFocusNextTick()
{
	if (!TrackedEmoteMenuWidget)
	{
		return;
	}

	TrackedEmoteMenuWidget->FocusInitialOption();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bMenuOpen || !TrackedEmoteMenuWidget)
			{
				return;
			}

			TrackedEmoteMenuWidget->FocusInitialOption();
		}));
	}
}

void UProjectEmoteSubsystem::RefreshCurrentMenuModel(const FName PreferredOptionId)
{
	if (!TrackedEmoteMenuWidget)
	{
		return;
	}

	CachedVisibleMenuNodes.Reset();

	FText MenuTitle = ResolveCurrentMenuTitle();
	FText MenuHint = ResolveCurrentMenuHint();
	TArray<FProjectEmoteMenuOption> MenuOptions;
	BuildMenuNodeOptions(MenuOptions);

	TrackedEmoteMenuWidget->SetMenuContent(MenuTitle, MenuHint, MenuOptions, ResolveCurrentMenuVisualMode());

	if (PreferredOptionId != NAME_None)
	{
		for (int32 OptionIndex = 0; OptionIndex < MenuOptions.Num(); ++OptionIndex)
		{
			if (MenuOptions[OptionIndex].OptionId == PreferredOptionId)
			{
				TrackedEmoteMenuWidget->SelectOptionByIndex(OptionIndex);
				TrackedEmoteMenuWidget->FocusInitialOption();
				return;
			}
		}
	}

	TrackedEmoteMenuWidget->FocusInitialOption();
}

FName UProjectEmoteSubsystem::GetPreferredRootOptionId() const
{
	return TrackedEmoteComponent && TrackedEmoteComponent->IsEmoteActive()
		? ProjectEmoteSubsystemPrivate::RootCancelOptionId
		: ProjectEmoteSubsystemPrivate::RootActionsOptionId;
}

FName UProjectEmoteSubsystem::GetCurrentMenuParentNodeId() const
{
	return MenuNodeStack.IsEmpty() ? NAME_None : MenuNodeStack.Last();
}

bool UProjectEmoteSubsystem::IsAtMenuRoot() const
{
	return MenuNodeStack.IsEmpty();
}

bool UProjectEmoteSubsystem::IsCurrentMenuActionsCategoryRoot() const
{
	return GetCurrentMenuParentNodeId() == ProjectEmoteSubsystemPrivate::RootActionsOptionId;
}

FText UProjectEmoteSubsystem::ResolveCurrentMenuTitle() const
{
	if (IsAtMenuRoot() || !TrackedEmoteComponent)
	{
		return NSLOCTEXT("ProjectEmoteSubsystem", "RootTitle", "Interactions");
	}

	if (!IsCurrentMenuActionsCategoryRoot())
	{
		return NSLOCTEXT("ProjectEmoteSubsystem", "SelectAnimationTitle", "Select Animation");
	}

	FProjectEmoteMenuNodeDefinition CurrentNode;
	if (TrackedEmoteComponent->FindMenuNodeDefinition(GetCurrentMenuParentNodeId(), CurrentNode) && !CurrentNode.DisplayName.IsEmpty())
	{
		return CurrentNode.DisplayName;
	}

	return NSLOCTEXT("ProjectEmoteSubsystem", "SubmenuTitle", "Menu");
}

FText UProjectEmoteSubsystem::ResolveCurrentMenuHint() const
{
	if (IsAtMenuRoot())
	{
		return NSLOCTEXT("ProjectEmoteSubsystem", "RootMenuHint", "Use Up/Down, Enter, Q, Y or Esc");
	}

	if (IsCurrentMenuActionsCategoryRoot())
	{
		return NSLOCTEXT("ProjectEmoteSubsystem", "ActionsMenuHint", "Select a category of actions. Q goes back.");
	}

	return NSLOCTEXT("ProjectEmoteSubsystem", "SelectAnimationHint", "Select an animation. Q goes back.");
}

EProjectEmoteMenuVisualMode UProjectEmoteSubsystem::ResolveCurrentMenuVisualMode() const
{
	if (IsAtMenuRoot())
	{
		return EProjectEmoteMenuVisualMode::Root;
	}

	return IsCurrentMenuActionsCategoryRoot()
		? EProjectEmoteMenuVisualMode::Category
		: EProjectEmoteMenuVisualMode::AnimationList;
}

void UProjectEmoteSubsystem::BuildMenuNodeOptions(TArray<FProjectEmoteMenuOption>& OutOptions)
{
	OutOptions.Reset();

	if (TrackedEmoteComponent)
	{
		TArray<FProjectEmoteMenuNodeDefinition> Nodes;
		TrackedEmoteComponent->GetChildMenuNodes(GetCurrentMenuParentNodeId(), Nodes);

		for (const FProjectEmoteMenuNodeDefinition& Node : Nodes)
		{
			if (IsCurrentMenuActionsCategoryRoot()
				&& (Node.NodeId == ProjectEmoteSubsystemPrivate::ActionsObjectsOptionId
					|| Node.NodeId == ProjectEmoteSubsystemPrivate::ActionsSpecialOptionId))
			{
				continue;
			}

			FProjectEmoteMenuOption MenuOption;
			MenuOption.OptionId = Node.NodeId;
			MenuOption.Label = Node.DisplayName;
			MenuOption.Description = Node.Description;
			MenuOption.bEnabled = true;
			MenuOption.NodeType = Node.NodeType;
			MenuOption.VisualIconId = Node.VisualIconId;
			MenuOption.VisualAttribute = Node.VisualAttribute;
			MenuOption.MenuIconTexture = Node.MenuIconTexture;
			MenuOption.RequiredExtraNpcCount = FMath::Clamp(Node.RequiredExtraNpcCount, 0, 3);
			OutOptions.Add(MenuOption);
			CachedVisibleMenuNodes.Add(Node.NodeId, Node);
		}
	}

	if (!IsAtMenuRoot() && OutOptions.IsEmpty())
	{
		FProjectEmoteMenuOption EmptyOption;
		EmptyOption.OptionId = ProjectEmoteSubsystemPrivate::EmptyObjectsOptionId;
		EmptyOption.Label = NSLOCTEXT("ProjectEmoteSubsystem", "NoCompatibleOptions", "No compatible options");
		EmptyOption.Description = NSLOCTEXT("ProjectEmoteSubsystem", "NoCompatibleOptionsDescription", "This category has no available animations yet.");
		EmptyOption.bEnabled = false;
		EmptyOption.VisualIconId = TEXT("Default");
		EmptyOption.VisualAttribute = TEXT("None");
		OutOptions.Add(EmptyOption);
	}

	if (IsAtMenuRoot() && !CachedVisibleMenuNodes.Contains(ProjectEmoteSubsystemPrivate::RootCancelOptionId))
	{
		FProjectEmoteMenuNodeDefinition CancelNode;
		CancelNode.NodeId = ProjectEmoteSubsystemPrivate::RootCancelOptionId;
		CancelNode.DisplayName = NSLOCTEXT("ProjectEmoteSubsystem", "CancelOption", "Cancel");
		CancelNode.Description = NSLOCTEXT("ProjectEmoteSubsystem", "CancelOptionDescription", "Close the interaction menu.");
		CancelNode.NodeType = EProjectEmoteMenuNodeType::Cancel;
		CancelNode.VisualIconId = TEXT("Cancel");
		CancelNode.VisualAttribute = TEXT("None");
		CancelNode.SortOrder = 100;
		CachedVisibleMenuNodes.Add(CancelNode.NodeId, CancelNode);

		FProjectEmoteMenuOption CancelOption;
		CancelOption.OptionId = CancelNode.NodeId;
		CancelOption.Label = CancelNode.DisplayName;
		CancelOption.Description = CancelNode.Description;
		CancelOption.NodeType = CancelNode.NodeType;
		CancelOption.VisualIconId = CancelNode.VisualIconId;
		CancelOption.VisualAttribute = CancelNode.VisualAttribute;
		OutOptions.Add(CancelOption);
	}
}

void UProjectEmoteSubsystem::ScheduleDelayedMovementRecoveryCheck(const FProjectEmoteMenuStateSnapshot& Snapshot)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DelayedMovementRecoveryTimerHandle);
	}

	DelayedRecoverySnapshot = Snapshot;
	if (!TrackedPlayerController || !TrackedPlayerPawn || !Snapshot.bHasSavedControllerState)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DelayedMovementRecoveryTimerHandle,
			this,
			&ThisClass::HandleDelayedMovementRecoveryCheck,
			3.0f,
			false);
	}
}

void UProjectEmoteSubsystem::HandleDelayedMovementRecoveryCheck()
{
	if (ShouldSkipDelayedMovementRecovery())
	{
		return;
	}

	APlayerController* PlayerController = TrackedPlayerController.Get();
	APawn* Pawn = TrackedPlayerPawn.Get();
	if (!PlayerController || !Pawn || !DelayedRecoverySnapshot.bHasSavedControllerState)
	{
		DelayedRecoverySnapshot = FProjectEmoteMenuStateSnapshot();
		return;
	}

	PlayerController->EnableInput(PlayerController);
	PlayerController->ResetIgnoreMoveInput();
	if (DelayedRecoverySnapshot.bWasMoveInputIgnored)
	{
		PlayerController->SetIgnoreMoveInput(true);
	}

	PlayerController->ResetIgnoreLookInput();
	if (DelayedRecoverySnapshot.bWasLookInputIgnored)
	{
		PlayerController->SetIgnoreLookInput(true);
	}

	PlayerController->bShowMouseCursor = false;
	PlayerController->bEnableClickEvents = false;
	PlayerController->bEnableMouseOverEvents = false;

	FInputModeGameOnly InputMode;
	PlayerController->SetInputMode(InputMode);
	PlayerController->FlushPressedKeys();
	Pawn->EnableInput(PlayerController);

	if (DelayedRecoverySnapshot.bHasSavedMovementState)
	{
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				if (CharacterMovement->MovementMode == MOVE_None)
				{
					CharacterMovement->SetMovementMode(
						DelayedRecoverySnapshot.PreviousMovementMode,
						DelayedRecoverySnapshot.PreviousCustomMovementMode);
				}
			}
		}
	}

	DelayedRecoverySnapshot = FProjectEmoteMenuStateSnapshot();
}

bool UProjectEmoteSubsystem::ShouldSkipDelayedMovementRecovery() const
{
	if (bMenuOpen || IsGamePauseBlockingEmoteFlow())
	{
		return true;
	}

	if (TrackedEmoteComponent && (TrackedEmoteComponent->IsEmoteActive() || TrackedEmoteComponent->IsEmoteTransitionPending()))
	{
		return true;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (const UEFCharacterCreationSubsystem* CharacterCreationSubsystem = GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>())
			{
				if (CharacterCreationSubsystem->IsCharacterCreationActive())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool UProjectEmoteSubsystem::IsGamePauseBlockingEmoteFlow() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld() && UGameplayStatics::IsGamePaused(World);
}

bool UProjectEmoteSubsystem::IsCombatBlockingEmoteMenu() const
{
	if (TrackedEmoteComponent && TrackedEmoteComponent->IsEmoteActive())
	{
		return false;
	}

	// T + Y is an explicit contextual interaction request. A selected enemy or
	// companion with an Intimacy partner identity must keep the Partner/Social/
	// Actions menu available even while ACF still reports the surrounding battle.
	if (TrackedEmoteComponent)
	{
		if (const AActor* CurrentTarget = TrackedEmoteComponent->GetCurrentInteractionTargetActor())
		{
			const UProjectIntimacyPartnerComponent* PartnerComponent =
				CurrentTarget->FindComponentByClass<UProjectIntimacyPartnerComponent>();
			if (PartnerComponent && PartnerComponent->bSocialCompanion)
			{
				return false;
			}
		}
	}

	if (TrackedEmoteComponent && TrackedEmoteComponent->IsCombatLockoutActive(ProjectEmoteSubsystemPrivate::CombatMenuLockoutSeconds))
	{
		return true;
	}

	return IsACFBattleActive();
}

bool UProjectEmoteSubsystem::IsACFBattleActive() const
{
	const UWorld* World = GetWorld();
	AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return false;
	}

	TInlineComponentArray<UActorComponent*> Components;
	GameState->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component) || !Component->GetClass()->GetName().Contains(TEXT("ACFAIManagerComponent")))
		{
			continue;
		}

		if (TryInvokeBoolFunction(Component, TEXT("IsInBattle")))
		{
			return true;
		}
	}

	return false;
}

bool UProjectEmoteSubsystem::TryInvokeBoolFunction(UObject* TargetObject, const FName FunctionName) const
{
	if (!IsValid(TargetObject))
	{
		return false;
	}

	UFunction* Function = TargetObject->FindFunction(FunctionName);
	if (!Function || Function->NumParms != 1 || Function->ReturnValueOffset == MAX_uint16)
	{
		return false;
	}

	uint8 Buffer[16] = {};
	TargetObject->ProcessEvent(Function, Buffer);
	return *reinterpret_cast<bool*>(Buffer + Function->ReturnValueOffset);
}

void UProjectEmoteSubsystem::RefreshActiveEmoteInputCapture()
{
	const bool bShouldCapture = TrackedEmoteComponent
		&& (TrackedEmoteComponent->IsEmoteActive() || TrackedEmoteComponent->IsEmoteTransitionPending());

	if (bShouldCapture)
	{
		ApplyActiveEmoteInputCapture();
	}
	else
	{
		RestoreActiveEmoteInputCapture();
	}
}

void UProjectEmoteSubsystem::ApplyActiveEmoteInputCapture()
{
	if (bActiveEmoteInputCaptureApplied || !TrackedPlayerController)
	{
		return;
	}

	ActiveEmoteInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectActiveEmoteInputComponent"));
	if (!ActiveEmoteInputComponent)
	{
		return;
	}

	ActiveEmoteInputComponent->bBlockInput = true;
	ActiveEmoteInputComponent->Priority = ProjectEmoteSubsystemPrivate::ActiveEmoteInputPriority;
	ActiveEmoteInputComponent->RegisterComponent();

	auto BindActionKey = [this](const FKey& Key, const EInputEvent InputEvent, void (ThisClass::*Handler)(), const bool bExecuteWhenPaused = false)
	{
		FInputKeyBinding& Binding = ActiveEmoteInputComponent->BindKey(Key, InputEvent, this, Handler);
		Binding.bConsumeInput = true;
		Binding.bExecuteWhenPaused = bExecuteWhenPaused;
	};

	auto BindBlockedKey = [&BindActionKey](const FKey& Key)
	{
		BindActionKey(Key, IE_Pressed, &ThisClass::HandleBlockedActiveEmoteInput);
		BindActionKey(Key, IE_Released, &ThisClass::HandleBlockedActiveEmoteInput);
	};

	BindActionKey(EKeys::W, IE_Pressed, &ThisClass::HandleFreeCameraForwardPressed);
	BindActionKey(EKeys::W, IE_Released, &ThisClass::HandleFreeCameraForwardReleased);
	BindActionKey(EKeys::S, IE_Pressed, &ThisClass::HandleFreeCameraBackwardPressed);
	BindActionKey(EKeys::S, IE_Released, &ThisClass::HandleFreeCameraBackwardReleased);
	BindActionKey(EKeys::D, IE_Pressed, &ThisClass::HandleFreeCameraRightPressed);
	BindActionKey(EKeys::D, IE_Released, &ThisClass::HandleFreeCameraRightReleased);
	BindActionKey(EKeys::A, IE_Pressed, &ThisClass::HandleFreeCameraLeftPressed);
	BindActionKey(EKeys::A, IE_Released, &ThisClass::HandleFreeCameraLeftReleased);
	BindActionKey(EKeys::E, IE_Pressed, &ThisClass::HandleFreeCameraUpPressed);
	BindActionKey(EKeys::E, IE_Released, &ThisClass::HandleFreeCameraUpReleased);
	BindActionKey(EKeys::Q, IE_Pressed, &ThisClass::HandleFreeCameraDownPressed);
	BindActionKey(EKeys::Q, IE_Released, &ThisClass::HandleFreeCameraDownReleased);
	BindActionKey(EKeys::LeftShift, IE_Pressed, &ThisClass::HandleFreeCameraBoostPressed);
	BindActionKey(EKeys::LeftShift, IE_Released, &ThisClass::HandleFreeCameraBoostReleased);
	BindActionKey(EKeys::RightShift, IE_Pressed, &ThisClass::HandleFreeCameraBoostPressed);
	BindActionKey(EKeys::RightShift, IE_Released, &ThisClass::HandleFreeCameraBoostReleased);
	BindActionKey(EKeys::Y, IE_Pressed, &ThisClass::HandleToggleMenuPressed);
	BindActionKey(EKeys::Escape, IE_Pressed, &ThisClass::HandleActiveEmoteCancelPressed);
	BindActionKey(EKeys::Up, IE_Pressed, &ThisClass::HandleActiveEmoteMenuNavigateUp);
	BindActionKey(EKeys::Down, IE_Pressed, &ThisClass::HandleActiveEmoteMenuNavigateDown);
	BindActionKey(EKeys::Left, IE_Pressed, &ThisClass::HandleActiveEmoteMenuNavigateBack);
	BindActionKey(EKeys::Right, IE_Pressed, &ThisClass::HandleActiveEmoteMenuNavigateForward);
	BindActionKey(EKeys::Enter, IE_Pressed, &ThisClass::HandleActiveEmoteMenuConfirm);
	BindActionKey(EKeys::Comma, IE_Pressed, &ThisClass::HandleAllowedNeedsHudPressed);
	BindActionKey(EKeys::J, IE_Pressed, &ThisClass::HandleAllowedChronicleExpandPressed);
	BindActionKey(EKeys::P, IE_Pressed, &ThisClass::HandleAllowedPausePressed, true);

	static const FKey BlockedKeys[] =
	{
		EKeys::LeftMouseButton,
		EKeys::RightMouseButton,
		EKeys::MiddleMouseButton,
		EKeys::ThumbMouseButton,
		EKeys::ThumbMouseButton2,
		EKeys::SpaceBar,
		EKeys::LeftControl,
		EKeys::RightControl,
		EKeys::LeftAlt,
		EKeys::RightAlt,
		EKeys::F,
		EKeys::R,
		EKeys::Tab,
		EKeys::One,
		EKeys::Two,
		EKeys::Three,
		EKeys::Four,
		EKeys::Five,
		EKeys::Six,
		EKeys::Seven,
		EKeys::Eight,
		EKeys::Nine,
		EKeys::Zero,
		EKeys::NumPadOne,
		EKeys::NumPadTwo,
		EKeys::NumPadThree,
		EKeys::NumPadFour,
		EKeys::NumPadFive,
		EKeys::NumPadSix,
		EKeys::NumPadSeven,
		EKeys::NumPadEight,
		EKeys::NumPadNine,
		EKeys::NumPadZero,
		EKeys::Gamepad_LeftTrigger,
		EKeys::Gamepad_RightTrigger,
		EKeys::Gamepad_LeftShoulder,
		EKeys::Gamepad_RightShoulder,
		EKeys::Gamepad_DPad_Up,
		EKeys::Gamepad_DPad_Down,
		EKeys::Gamepad_DPad_Left,
		EKeys::Gamepad_DPad_Right,
		EKeys::Gamepad_FaceButton_Bottom,
		EKeys::Gamepad_FaceButton_Left,
		EKeys::Gamepad_FaceButton_Right,
		EKeys::Gamepad_FaceButton_Top
	};

	for (const FKey& Key : BlockedKeys)
	{
		BindBlockedKey(Key);
	}

	TrackedPlayerController->PushInputComponent(ActiveEmoteInputComponent);
	TrackedPlayerController->FlushPressedKeys();
	bActiveEmoteInputCaptureApplied = true;
}

void UProjectEmoteSubsystem::RestoreActiveEmoteInputCapture()
{
	if (!bActiveEmoteInputCaptureApplied && !ActiveEmoteInputComponent)
	{
		return;
	}

	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->ClearFreeCameraMoveInput();
	}

	if (TrackedPlayerController && ActiveEmoteInputComponent)
	{
		TrackedPlayerController->PopInputComponent(ActiveEmoteInputComponent);
		TrackedPlayerController->FlushPressedKeys();
	}

	if (ActiveEmoteInputComponent && ActiveEmoteInputComponent->IsRegistered())
	{
		ActiveEmoteInputComponent->DestroyComponent();
	}

	ActiveEmoteInputComponent = nullptr;
	bActiveEmoteInputCaptureApplied = false;
}

void UProjectEmoteSubsystem::SetFreeCameraForwardInput(const float Value)
{
	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->SetFreeCameraForwardInput(Value);
	}
}

void UProjectEmoteSubsystem::SetFreeCameraRightInput(const float Value)
{
	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->SetFreeCameraRightInput(Value);
	}
}

void UProjectEmoteSubsystem::SetFreeCameraVerticalInput(const float Value)
{
	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->SetFreeCameraVerticalInput(Value);
	}
}

void UProjectEmoteSubsystem::SetFreeCameraBoostActive(const bool bActive)
{
	if (TrackedEmoteComponent)
	{
		TrackedEmoteComponent->SetFreeCameraBoostActive(bActive);
	}
}

void UProjectEmoteSubsystem::HandleToggleMenuPressed()
{
	if (bRuntimeActionActive)
	{
		if (ActiveRuntimeActionRequest.bAllowCancel && ActiveRuntimeActionRequest.bCancelWithY)
		{
			CancelRuntimeAction(TEXT("Y"));
		}
		return;
	}

	if (bMenuOpen)
	{
		CloseMenu(true);
		return;
	}

	if (TrackedEmoteComponent && TrackedEmoteComponent->IsActiveInteractionBlueprintScene())
	{
		TrackedEmoteComponent->StopEmote();
		RestoreActiveEmoteInputCapture();
		return;
	}

	if (IsCombatBlockingEmoteMenu())
	{
		return;
	}

	OpenMenu();
}

void UProjectEmoteSubsystem::HandleMenuOptionConfirmed(const FName OptionId)
{
	if (OptionId == ProjectEmoteSubsystemPrivate::BackOptionId)
	{
		if (!MenuNodeStack.IsEmpty())
		{
			const FName PreviousNodeId = MenuNodeStack.Pop(EAllowShrinking::No);
			RefreshCurrentMenuModel(PreviousNodeId);
		}
		else
		{
			RefreshCurrentMenuModel(GetPreferredRootOptionId());
		}
		return;
	}

	if (const FProjectEmoteMenuNodeDefinition* MenuNode = CachedVisibleMenuNodes.Find(OptionId))
	{
		switch (MenuNode->NodeType)
		{
		case EProjectEmoteMenuNodeType::Folder:
			MenuNodeStack.Add(MenuNode->NodeId);
			RefreshCurrentMenuModel();
			return;
		case EProjectEmoteMenuNodeType::Action:
			StartInteractionById(MenuNode->NodeId);
			return;
		case EProjectEmoteMenuNodeType::Cancel:
		{
			const bool bShouldStopActiveEmote = TrackedEmoteComponent && TrackedEmoteComponent->IsEmoteActive();
			CloseMenu(!bShouldStopActiveEmote);
			if (bShouldStopActiveEmote && TrackedEmoteComponent)
			{
				TrackedEmoteComponent->StopEmote();
			}
			return;
		}
		case EProjectEmoteMenuNodeType::Back:
			if (!MenuNodeStack.IsEmpty())
			{
				MenuNodeStack.Pop(EAllowShrinking::No);
			}
			RefreshCurrentMenuModel(GetPreferredRootOptionId());
			return;
		default:
			return;
		}
	}
}

void UProjectEmoteSubsystem::HandleMenuCancelRequested()
{
	CloseMenu(true);
}

void UProjectEmoteSubsystem::HandleFreeCameraForwardPressed()
{
	SetFreeCameraForwardInput(1.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraForwardReleased()
{
	SetFreeCameraForwardInput(0.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraBackwardPressed()
{
	SetFreeCameraForwardInput(-1.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraBackwardReleased()
{
	SetFreeCameraForwardInput(0.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraRightPressed()
{
	SetFreeCameraRightInput(1.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraRightReleased()
{
	SetFreeCameraRightInput(0.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraLeftPressed()
{
	SetFreeCameraRightInput(-1.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraLeftReleased()
{
	SetFreeCameraRightInput(0.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraUpPressed()
{
	SetFreeCameraVerticalInput(1.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraUpReleased()
{
	SetFreeCameraVerticalInput(0.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraDownPressed()
{
	if (bMenuOpen)
	{
		HandleActiveEmoteMenuNavigateBack();
		return;
	}

	SetFreeCameraVerticalInput(-1.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraDownReleased()
{
	if (bMenuOpen)
	{
		return;
	}

	SetFreeCameraVerticalInput(0.0f);
}

void UProjectEmoteSubsystem::HandleFreeCameraBoostPressed()
{
	SetFreeCameraBoostActive(true);
}

void UProjectEmoteSubsystem::HandleFreeCameraBoostReleased()
{
	SetFreeCameraBoostActive(false);
}

void UProjectEmoteSubsystem::HandleActiveEmoteMenuNavigateUp()
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->NavigateSelectionByDirection(-1);
	}
}

void UProjectEmoteSubsystem::HandleActiveEmoteMenuNavigateDown()
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->NavigateSelectionByDirection(1);
	}
}

void UProjectEmoteSubsystem::HandleActiveEmoteMenuNavigateBack()
{
	if (bMenuOpen)
	{
		if (!MenuNodeStack.IsEmpty())
		{
			const FName PreviousNodeId = MenuNodeStack.Pop(EAllowShrinking::No);
			RefreshCurrentMenuModel(PreviousNodeId);
			return;
		}

		HandleMenuCancelRequested();
	}
}

void UProjectEmoteSubsystem::HandleActiveEmoteMenuNavigateForward()
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->ConfirmCurrentSelection();
	}
}

void UProjectEmoteSubsystem::HandleActiveEmoteMenuConfirm()
{
	if (bMenuOpen && TrackedEmoteMenuWidget)
	{
		TrackedEmoteMenuWidget->ConfirmCurrentSelection();
	}
}

void UProjectEmoteSubsystem::HandleActiveEmoteCancelPressed()
{
	if (bMenuOpen)
	{
		CloseMenu(true);
		return;
	}

	if (bRuntimeActionActive)
	{
		CancelRuntimeAction(TEXT("Escape"));
		return;
	}

	if (TrackedEmoteComponent)
	{
		if (TrackedEmoteComponent->IsActiveInteractionBlueprintScene())
		{
			return;
		}

		TrackedEmoteComponent->StopEmote();
	}
	RestoreActiveEmoteInputCapture();
}

void UProjectEmoteSubsystem::HandleAllowedNeedsHudPressed()
{
	if (UWorld* World = GetWorld())
	{
		if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			NeedsSubsystem->ToggleNeedsHudVisibility();
		}
	}
}

void UProjectEmoteSubsystem::HandleAllowedChronicleExpandPressed()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bChronicleAllowed = false;
	if (const UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
	{
		bChronicleAllowed = NeedsSubsystem->IsNeedsHudVisible();
	}
	if (!bChronicleAllowed)
	{
		if (const UProjectIntimacySubsystem* IntimacySubsystem = World->GetSubsystem<UProjectIntimacySubsystem>())
		{
			bChronicleAllowed = IntimacySubsystem->IsIntimacySessionActive();
		}
	}
	if (!bChronicleAllowed)
	{
		return;
	}

	if (UProjectActivityFeedSubsystem* ActivityFeedSubsystem = World->GetSubsystem<UProjectActivityFeedSubsystem>())
	{
		ActivityFeedSubsystem->RequestToggleExpanded();
	}
}

void UProjectEmoteSubsystem::HandleAllowedPausePressed()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	const bool bWasPaused = UGameplayStatics::IsGamePaused(World);
	if (!bWasPaused)
	{
		CleanupMenuAndEmoteState();
		RestoreActiveEmoteInputCapture();
	}

	UGameplayStatics::SetGamePaused(World, !bWasPaused);
}

void UProjectEmoteSubsystem::HandleBlockedActiveEmoteInput()
{
}
