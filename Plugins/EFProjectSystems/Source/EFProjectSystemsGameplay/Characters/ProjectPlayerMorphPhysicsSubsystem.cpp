#include "Characters/ProjectPlayerMorphPhysicsSubsystem.h"

#include "Characters/ProjectEnemyVisualVariationSettings.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFMorphPhysicsConstraintComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectPlayerMorphPhysics, Log, All);

void UProjectPlayerMorphPhysicsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPawn = nullptr;
	TrackedMorphPhysicsComponent = nullptr;
	TrackedCustomizationComponent = nullptr;
	CustomizationMorphStateHandle.Reset();
	bApplyingMaleNeutralMorph = false;
	LastObservedGenderValue = MAX_uint8;
	bNeedsMaintenanceTick = true;
}

void UProjectPlayerMorphPhysicsSubsystem::Deinitialize()
{
	DetachFromTrackedPlayerController();
	bNeedsMaintenanceTick = false;

	Super::Deinitialize();
}

void UProjectPlayerMorphPhysicsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	(void)DeltaTime;

	if (!bNeedsMaintenanceTick)
	{
		return;
	}

	bNeedsMaintenanceTick = !TryResolveRuntimeContext();
}

TStatId UProjectPlayerMorphPhysicsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectPlayerMorphPhysicsSubsystem, STATGROUP_Tickables);
}

bool UProjectPlayerMorphPhysicsSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld() && bNeedsMaintenanceTick;
}

bool UProjectPlayerMorphPhysicsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectPlayerMorphPhysicsSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		DetachFromTrackedPlayerController();
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController();
		return false;
	}

	AttachToPlayerController(PlayerController);
	EnsureMorphPhysicsComponent(PlayerController->GetPawn());
	const bool bMaleNeutralMorphResolved = EnsureMaleNeutralMorph(PlayerController->GetPawn());

	return TrackedPlayerController != nullptr
		&& TrackedPawn != nullptr
		&& TrackedMorphPhysicsComponent != nullptr
		&& bMaleNeutralMorphResolved;
}

void UProjectPlayerMorphPhysicsSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		return;
	}

	DetachFromTrackedPlayerController();
	TrackedPlayerController = PlayerController;
	TrackedPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandlePossessedPawnChanged);
}

void UProjectPlayerMorphPhysicsSubsystem::DetachFromTrackedPlayerController()
{
	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	UnbindCustomizationComponent();
	TrackedMorphPhysicsComponent = nullptr;
	TrackedPawn = nullptr;
	TrackedPlayerController = nullptr;
}

void UProjectPlayerMorphPhysicsSubsystem::EnsureMorphPhysicsComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		TrackedPawn = nullptr;
		TrackedMorphPhysicsComponent = nullptr;
		return;
	}

	TrackedPawn = Pawn;
	UEFMorphPhysicsConstraintComponent* MorphPhysicsComponent = Pawn->FindComponentByClass<UEFMorphPhysicsConstraintComponent>();
	if (!MorphPhysicsComponent)
	{
		MorphPhysicsComponent = NewObject<UEFMorphPhysicsConstraintComponent>(
			Pawn,
			UEFMorphPhysicsConstraintComponent::StaticClass(),
			TEXT("ProjectMorphPhysicsConstraintComponent"));
		if (MorphPhysicsComponent)
		{
			Pawn->AddInstanceComponent(MorphPhysicsComponent);
			MorphPhysicsComponent->OnComponentCreated();
			MorphPhysicsComponent->RegisterComponent();
			MorphPhysicsComponent->Activate(true);

			UE_LOG(
				LogProjectPlayerMorphPhysics,
				Log,
				TEXT("Attached morph physics driver to pawn %s."),
				*GetNameSafe(Pawn));
		}
	}
	else
	{
		if (!MorphPhysicsComponent->IsRegistered())
		{
			MorphPhysicsComponent->RegisterComponent();
		}

		if (!MorphPhysicsComponent->IsActive())
		{
			MorphPhysicsComponent->Activate(true);
		}
	}

	TrackedMorphPhysicsComponent = MorphPhysicsComponent;
	if (TrackedMorphPhysicsComponent)
	{
		TrackedMorphPhysicsComponent->RefreshFromCurrentMorphState();
	}
	else
	{
		UE_LOG(
			LogProjectPlayerMorphPhysics,
			Warning,
			TEXT("Could not attach morph physics driver to pawn %s."),
			*GetNameSafe(Pawn));
	}
}

bool UProjectPlayerMorphPhysicsSubsystem::EnsureMaleNeutralMorph(APawn* Pawn)
{
	if (!Pawn)
	{
		UnbindCustomizationComponent();
		return false;
	}

	UEFCharacterCustomizationComponent* CustomizationComponent =
		Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>();
	if (!CustomizationComponent)
	{
		CustomizationComponent = NewObject<UEFCharacterCustomizationComponent>(
			Pawn,
			UEFCharacterCustomizationComponent::StaticClass(),
			TEXT("ProjectPlayerCharacterCustomizationComponent"));
		if (!CustomizationComponent)
		{
			return false;
		}

		Pawn->AddInstanceComponent(CustomizationComponent);
		CustomizationComponent->RegisterComponent();
	}

	if (!CustomizationComponent->IsMeshCompatible()
		|| CustomizationComponent->GetAvailableMorphEntries().IsEmpty())
	{
		if (!CustomizationComponent->InitializeForActor(Pawn))
		{
			return false;
		}
	}

	BindCustomizationComponent(CustomizationComponent);
	const ECharacterCreationGender CurrentGender = CustomizationComponent->GetGender();
	LastObservedGenderValue = static_cast<uint8>(CurrentGender);
	if (CurrentGender != ECharacterCreationGender::Male)
	{
		return true;
	}

	const UProjectEnemyVisualVariationSettings* Settings =
		UProjectEnemyVisualVariationSettings::Get();
	if (!Settings
		|| Settings->NeutralBaseMorphName.IsNone()
		|| Settings->ActivePresentationMorphName.IsNone())
	{
		return false;
	}

	bool bFoundNeutralMorph = false;
	bool bFoundActiveMorph = false;
	TGuardValue<bool> ApplyingGuard(bApplyingMaleNeutralMorph, true);
	for (const FMorphSliderEntry& Entry : CustomizationComponent->GetAvailableMorphEntries())
	{
		if (Entry.MorphName == Settings->NeutralBaseMorphName)
		{
			bFoundNeutralMorph = true;
			if (!FMath::IsNearlyEqual(CustomizationComponent->GetCurrentMorphValue(Entry), 1.0f))
			{
				CustomizationComponent->ApplyMorph(Entry, 1.0f);
			}
		}
		else if (Entry.MorphName == Settings->ActivePresentationMorphName)
		{
			bFoundActiveMorph = true;
			if (!FMath::IsNearlyZero(CustomizationComponent->GetCurrentMorphValue(Entry)))
			{
				CustomizationComponent->ApplyMorph(Entry, 0.0f);
			}
		}
	}

	if (bFoundNeutralMorph && bFoundActiveMorph)
	{
		if (TrackedMorphPhysicsComponent)
		{
			TrackedMorphPhysicsComponent->RefreshFromCurrentMorphState();
		}

		UE_LOG(
			LogProjectPlayerMorphPhysics,
			Verbose,
			TEXT("Applied default neutral Male morph state to pawn %s."),
			*GetNameSafe(Pawn));
		return true;
	}

	UE_LOG(
		LogProjectPlayerMorphPhysics,
		Warning,
		TEXT("Male pawn %s does not expose both configured neutral and active morphs."),
		*GetNameSafe(Pawn));
	return false;
}

void UProjectPlayerMorphPhysicsSubsystem::BindCustomizationComponent(
	UEFCharacterCustomizationComponent* CustomizationComponent)
{
	if (TrackedCustomizationComponent == CustomizationComponent)
	{
		return;
	}

	UnbindCustomizationComponent();
	TrackedCustomizationComponent = CustomizationComponent;
	if (TrackedCustomizationComponent)
	{
		CustomizationMorphStateHandle =
			TrackedCustomizationComponent->OnMorphStateApplied().AddUObject(
				this,
				&ThisClass::HandleCustomizationMorphStateApplied);
	}
}

void UProjectPlayerMorphPhysicsSubsystem::UnbindCustomizationComponent()
{
	if (TrackedCustomizationComponent && CustomizationMorphStateHandle.IsValid())
	{
		TrackedCustomizationComponent->OnMorphStateApplied().Remove(
			CustomizationMorphStateHandle);
	}

	CustomizationMorphStateHandle.Reset();
	TrackedCustomizationComponent = nullptr;
	LastObservedGenderValue = MAX_uint8;
}

void UProjectPlayerMorphPhysicsSubsystem::MarkMaintenanceRequired()
{
	bNeedsMaintenanceTick = true;
}

void UProjectPlayerMorphPhysicsSubsystem::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	(void)OldPawn;

	TrackedPawn = NewPawn;
	TrackedMorphPhysicsComponent = nullptr;
	UnbindCustomizationComponent();
	MarkMaintenanceRequired();
}

void UProjectPlayerMorphPhysicsSubsystem::HandleCustomizationMorphStateApplied()
{
	const UEFCharacterCustomizationComponent* CustomizationComponent =
		TrackedCustomizationComponent.Get();
	if (!bApplyingMaleNeutralMorph
		&& CustomizationComponent
		&& LastObservedGenderValue
			!= static_cast<uint8>(CustomizationComponent->GetGender()))
	{
		MarkMaintenanceRequired();
	}
}
