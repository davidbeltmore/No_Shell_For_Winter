#include "Characters/ProjectTargetingFixComponent.h"

#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyTargetInfoComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "UI/ProjectTargetLevelWidget.h"
#include "UI/ProjectSocialCardWidget.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectTargetingFix, Log, All);

namespace ProjectTargetingFixPrivate
{
	template <typename ComponentType>
	static ComponentType* FindComponentByClassHint(AActor* Owner, const TArray<FString>& ClassHints)
	{
		if (!Owner)
		{
			return nullptr;
		}

		TInlineComponentArray<UActorComponent*> Components(Owner);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const FString ComponentClassName = Component->GetClass()->GetName();
			for (const FString& Hint : ClassHints)
			{
				if (ComponentClassName.Contains(Hint))
				{
					return Cast<ComponentType>(Component);
				}
			}
		}

		return nullptr;
	}

	static FString NormalizeToken(const FString& Value)
	{
		FString Normalized;
		Normalized.Reserve(Value.Len());
		for (const TCHAR Character : Value)
		{
			if (FChar::IsAlnum(Character))
			{
				Normalized.AppendChar(FChar::ToLower(Character));
			}
		}

		return Normalized;
	}

	static bool NameContainsAnyHint(const FName PropertyName, const TArray<FString>& Hints)
	{
		const FString NormalizedName = NormalizeToken(PropertyName.ToString());
		for (const FString& Hint : Hints)
		{
			if (NormalizedName.Contains(NormalizeToken(Hint)))
			{
				return true;
			}
		}

		return false;
	}

	static AActor* FindBestActorPropertyValue(UObject* TargetObject, const AActor* IgnoredActor)
	{
		if (!TargetObject)
		{
			return nullptr;
		}

		AActor* BestActor = nullptr;
		int32 BestScore = 0;

		for (TFieldIterator<FObjectPropertyBase> It(TargetObject->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FObjectPropertyBase* Property = *It;
			if (!Property)
			{
				continue;
			}

			AActor* CandidateActor = Cast<AActor>(Property->GetObjectPropertyValue_InContainer(TargetObject));
			if (!CandidateActor || CandidateActor == IgnoredActor)
			{
				continue;
			}

			int32 Score = 1;
			if (NameContainsAnyHint(Property->GetFName(), { TEXT("CurrentTarget"), TEXT("CurrentTargetActor"), TEXT("LockedTarget"), TEXT("TargetActor") }))
			{
				Score += 100;
			}
			else if (NameContainsAnyHint(Property->GetFName(), { TEXT("Target") }))
			{
				Score += 30;
			}

			if (CandidateActor->FindComponentByClass<UProjectEnemyLevelComponent>() || CandidateActor->FindComponentByClass<UProjectEnemyTargetInfoComponent>())
			{
				Score += 80;
			}

			if (Score > BestScore)
			{
				BestScore = Score;
				BestActor = CandidateActor;
			}
		}

		return BestActor;
	}
	static USceneComponent* FindBestSceneComponentPropertyValue(UObject* TargetObject, const AActor* DesiredOwnerActor)
	{
		if (!TargetObject)
		{
			return nullptr;
		}

		USceneComponent* BestComponent = nullptr;
		int32 BestScore = 0;

		for (TFieldIterator<FObjectPropertyBase> It(TargetObject->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FObjectPropertyBase* Property = *It;
			if (!Property)
			{
				continue;
			}

			USceneComponent* CandidateComponent = Cast<USceneComponent>(Property->GetObjectPropertyValue_InContainer(TargetObject));
			if (!CandidateComponent)
			{
				continue;
			}

			if (DesiredOwnerActor && CandidateComponent->GetOwner() != DesiredOwnerActor)
			{
				continue;
			}

			int32 Score = 1;
			if (NameContainsAnyHint(Property->GetFName(), { TEXT("CurrentTargetPoint"), TEXT("TargetPoint"), TEXT("CurrentPoint") }))
			{
				Score += 100;
			}
			else if (NameContainsAnyHint(Property->GetFName(), { TEXT("Target") }))
			{
				Score += 30;
			}

			if (CandidateComponent->GetClass()->GetName().Contains(TEXT("TargetPoint")))
			{
				Score += 70;
			}

			if (Score > BestScore)
			{
				BestScore = Score;
				BestComponent = CandidateComponent;
			}
		}

		return BestComponent;
	}

}

UProjectTargetingFixComponent::UProjectTargetingFixComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bAutoActivate = true;
}

void UProjectTargetingFixComponent::BeginPlay()
{
	Super::BeginPlay();
	HideScreenWidget();
	EnsureRuntimeContext();
}

void UProjectTargetingFixComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideTargetInfoForActor(CachedTargetActor.Get());
	HideScreenWidget();

	if (LevelWidget)
	{
		LevelWidget->RemoveFromParent();
		LevelWidget = nullptr;
	}

	if (SocialCardWidget)
	{
		SocialCardWidget->RemoveFromParent();
		SocialCardWidget = nullptr;
	}

	CachedTargetingComponent = nullptr;
	CachedPlayerController = nullptr;
	CachedOwnerPawn = nullptr;
	CachedTargetActor = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UProjectTargetingFixComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!EnsureRuntimeContext())
	{
		if (!bManualSocialCardPreview)
		{
			HideTargetInfoForActor(CachedTargetActor.Get());
			HideScreenWidget();
		}
		CachedTargetActor = nullptr;
		return;
	}

	UActorComponent* TargetingComponent = ResolveTargetingComponent();
	if (!TargetingComponent)
	{
		if (!bManualSocialCardPreview)
		{
			HideTargetInfoForActor(CachedTargetActor.Get());
			HideScreenWidget();
		}
		CachedTargetActor = nullptr;
		return;
	}

	AActor* TargetActor = nullptr;
	USceneComponent* TargetPointComponent = nullptr;
	if (!TryRepairCurrentTarget(TargetingComponent, TargetActor, TargetPointComponent) || !TargetActor)
	{
		if (!bManualSocialCardPreview)
		{
			HideTargetInfoForActor(CachedTargetActor.Get());
			HideScreenWidget();
		}
		CachedTargetActor = nullptr;
		return;
	}

	if (CachedTargetActor != TargetActor)
	{
		HideTargetInfoForActor(CachedTargetActor.Get());
		CachedTargetActor = TargetActor;
	}

	ShowTargetInfoForActor(TargetActor);
	UpdateScreenWidget(TargetActor, TargetPointComponent);
}

bool UProjectTargetingFixComponent::HasResolvedTargetingComponent()
{
	return ResolveTargetingComponent() != nullptr;
}

AActor* UProjectTargetingFixComponent::GetCurrentTargetActor() const
{
	return CachedTargetActor.Get();
}

bool UProjectTargetingFixComponent::DebugSetCurrentTargetActor(AActor* TargetActor)
{
#if UE_BUILD_SHIPPING
	(void)TargetActor;
	return false;
#else
	return RestoreCurrentTargetActor(TargetActor);
#endif
}

bool UProjectTargetingFixComponent::RestoreCurrentTargetActor(AActor* TargetActor)
{
	if (!IsValid(TargetActor) || TargetActor == GetOwner() || TargetActor->IsActorBeingDestroyed())
	{
		return false;
	}

	// ACF's UATSTargetingComponent::SetCurrentTarget assumes its private
	// ControlledPawn and camera manager references are valid. During
	// OnPossessedPawnChanged the old pawn no longer satisfies that contract;
	// calling SetCurrentTarget then dereferences a null ControlledPawn in
	// GetBestTargetPointForTarget. Never enter ACF while the owner is detached.
	if (!EnsureRuntimeContext())
	{
		UE_LOG(LogProjectTargetingFix, Verbose,
			TEXT("Skipped target restore for %s because its owner is not the currently possessed local pawn."),
			*GetNameSafe(TargetActor));
		return false;
	}

	APawn* OwnerPawn = CachedOwnerPawn.Get();
	APlayerController* PlayerController = CachedPlayerController.Get();
	if (!IsValid(OwnerPawn)
		|| OwnerPawn->IsActorBeingDestroyed()
		|| !IsValid(PlayerController)
		|| PlayerController->GetPawn() != OwnerPawn
		|| !IsValid(PlayerController->PlayerCameraManager)
		|| TargetActor->GetWorld() != GetWorld())
	{
		return false;
	}

	UActorComponent* TargetingComponent = ResolveTargetingComponent();
	bool bChangedTargetingState = false;
	if (IsValid(TargetingComponent)
		&& TargetingComponent->IsRegistered()
		&& TargetingComponent->HasBegunPlay()
		&& IsValid(TargetingComponent->GetOwner())
		&& !TargetingComponent->GetOwner()->IsActorBeingDestroyed())
	{
		if (UFunction* SetCurrentTargetFunction = TargetingComponent->FindFunction(TEXT("SetCurrentTarget")))
		{
			struct FSetCurrentTargetParams
			{
				AActor* Target = nullptr;
			};

			FSetCurrentTargetParams Params;
			Params.Target = TargetActor;
			TargetingComponent->ProcessEvent(SetCurrentTargetFunction, &Params);
			bChangedTargetingState = true;
		}

		if (FBoolProperty* IsTargetingProperty = FindFProperty<FBoolProperty>(TargetingComponent->GetClass(), TEXT("bIsTargeting")))
		{
			IsTargetingProperty->SetPropertyValue_InContainer(TargetingComponent, true);
			bChangedTargetingState = true;
		}

		if (FObjectPropertyBase* CurrentTargetProperty = FindFProperty<FObjectPropertyBase>(TargetingComponent->GetClass(), TEXT("CurrentTarget")))
		{
			CurrentTargetProperty->SetObjectPropertyValue_InContainer(TargetingComponent, TargetActor);
			bChangedTargetingState = true;
		}

		TargetingComponent->SetComponentTickEnabled(true);
	}

	CachedTargetActor = TargetActor;
	ShowTargetInfoForActor(TargetActor);
	RefreshSocialCardForActor(TargetActor, true);
	return bChangedTargetingState || IsValid(CachedTargetActor.Get());
}

bool UProjectTargetingFixComponent::DeactivateCurrentTargetingLock()
{
	UActorComponent* TargetingComponent = ResolveTargetingComponent();
	if (!TargetingComponent)
	{
		HideTargetInfoForActor(CachedTargetActor.Get());
		HideScreenWidget();
		CachedTargetActor = nullptr;
		return false;
	}

	bool bChangedTargetingState = false;
	if (UFunction* TriggerTargetingFunction = TargetingComponent->FindFunction(TEXT("TriggerTargeting")))
	{
		struct FTriggerTargetingParams
		{
			bool bActivate = false;
		};

		FTriggerTargetingParams Params;
		Params.bActivate = false;
		TargetingComponent->ProcessEvent(TriggerTargetingFunction, &Params);
		bChangedTargetingState = true;
	}
	else if (UFunction* SetCurrentTargetFunction = TargetingComponent->FindFunction(TEXT("SetCurrentTarget")))
	{
		struct FSetCurrentTargetParams
		{
			AActor* Target = nullptr;
		};

		FSetCurrentTargetParams Params;
		Params.Target = nullptr;
		TargetingComponent->ProcessEvent(SetCurrentTargetFunction, &Params);
		bChangedTargetingState = true;
	}

	if (FBoolProperty* IsTargetingProperty = FindFProperty<FBoolProperty>(TargetingComponent->GetClass(), TEXT("bIsTargeting")))
	{
		IsTargetingProperty->SetPropertyValue_InContainer(TargetingComponent, false);
		bChangedTargetingState = true;
	}

	if (FObjectPropertyBase* CurrentTargetProperty = FindFProperty<FObjectPropertyBase>(TargetingComponent->GetClass(), TEXT("CurrentTarget")))
	{
		CurrentTargetProperty->SetObjectPropertyValue_InContainer(TargetingComponent, nullptr);
		bChangedTargetingState = true;
	}

	if (FObjectPropertyBase* CurrentTargetPointProperty = FindFProperty<FObjectPropertyBase>(TargetingComponent->GetClass(), TEXT("CurrentTargetPoint")))
	{
		CurrentTargetPointProperty->SetObjectPropertyValue_InContainer(TargetingComponent, nullptr);
		bChangedTargetingState = true;
	}

	TargetingComponent->SetComponentTickEnabled(false);
	HideTargetInfoForActor(CachedTargetActor.Get());
	HideScreenWidget();
	CachedTargetActor = nullptr;
	return bChangedTargetingState;
}

bool UProjectTargetingFixComponent::EnsureRuntimeContext()
{
	CachedOwnerPawn = Cast<APawn>(GetOwner());
	CachedPlayerController = CachedOwnerPawn ? Cast<APlayerController>(CachedOwnerPawn->GetController()) : nullptr;
	return IsValid(CachedOwnerPawn)
		&& !CachedOwnerPawn->IsActorBeingDestroyed()
		&& IsValid(CachedPlayerController)
		&& CachedPlayerController->IsLocalController()
		&& CachedPlayerController->GetPawn() == CachedOwnerPawn;
}

UActorComponent* UProjectTargetingFixComponent::ResolveTargetingComponent()
{
	if (CachedTargetingComponent)
	{
		return CachedTargetingComponent.Get();
	}

	CachedTargetingComponent = ProjectTargetingFixPrivate::FindComponentByClassHint<UActorComponent>(
		GetOwner(),
		{ TEXT("ATSTargetingComponent") });

	if (!CachedTargetingComponent && CachedPlayerController)
	{
		CachedTargetingComponent = ProjectTargetingFixPrivate::FindComponentByClassHint<UActorComponent>(
			CachedPlayerController,
			{ TEXT("ATSTargetingComponent") });
	}

	return CachedTargetingComponent.Get();
}

void UProjectTargetingFixComponent::EnsureScreenWidget(APlayerController* PlayerController)
{
	EnsureSocialCardWidget(PlayerController);
}

void UProjectTargetingFixComponent::UpdateScreenWidget(AActor* TargetActor, USceneComponent* TargetPointComponent)
{
	if (!TargetActor || !ShouldShowSocialCard())
	{
		HideSocialCard();
		return;
	}

	RefreshSocialCardForActor(TargetActor, false);
}

UProjectSocialCardWidget* UProjectTargetingFixComponent::EnsureSocialCardWidget(APlayerController* PlayerController)
{
	if (SocialCardWidget)
	{
		return SocialCardWidget;
	}

	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return nullptr;
	}

	SocialCardWidget = CreateWidget<UProjectSocialCardWidget>(
		PlayerController,
		UProjectSocialCardWidget::StaticClass());
	if (!SocialCardWidget)
	{
		UE_LOG(LogProjectTargetingFix, Warning, TEXT("Could not create ProjectSocialCardWidget for %s."), *GetNameSafe(PlayerController));
		return nullptr;
	}

	constexpr int32 SocialCardZOrder = 650;
	if (!SocialCardWidget->AddToPlayerScreen(SocialCardZOrder))
	{
		SocialCardWidget->AddToViewport(SocialCardZOrder);
	}
	SocialCardWidget->SetHudVisible(false);
	return SocialCardWidget;
}

bool UProjectTargetingFixComponent::ShouldShowSocialCard() const
{
	const UWorld* World = GetWorld();
	const UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World ? World->GetSubsystem<UProjectSurvivalNeedsSubsystem>() : nullptr;
	return NeedsSubsystem && NeedsSubsystem->IsNeedsHudVisible();
}

bool UProjectTargetingFixComponent::ShowSocialCardForActor(AActor* TargetActor)
{
	return RefreshSocialCardForActor(TargetActor, true);
}

bool UProjectTargetingFixComponent::RefreshSocialCardForActor(AActor* TargetActor, const bool bManualRequest)
{
	EnsureRuntimeContext();

	if (!TargetActor || !CachedPlayerController || !CachedPlayerController->IsLocalController())
	{
		HideSocialCard();
		return false;
	}

	if (!ShouldShowSocialCard())
	{
		HideSocialCard();
		return false;
	}

	UWorld* World = GetWorld();
	UProjectIntimacySubsystem* IntimacySubsystem = World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr;
	if (!IntimacySubsystem)
	{
		HideSocialCard();
		return false;
	}

	FProjectSocialCardSnapshot Snapshot;
	if (!IntimacySubsystem->BuildTargetSocialCardSnapshot(TargetActor, Snapshot) || Snapshot.Rows.Num() <= 0)
	{
		HideSocialCard();
		return false;
	}

	UProjectSocialCardWidget* Widget = EnsureSocialCardWidget(CachedPlayerController);
	if (!Widget)
	{
		HideSocialCard();
		return false;
	}

	Widget->SetSocialCardSnapshot(Snapshot);
	Widget->SetHudVisible(true);
	bManualSocialCardPreview = bManualRequest;
	return true;
}

void UProjectTargetingFixComponent::HideSocialCard()
{
	bManualSocialCardPreview = false;
	if (SocialCardWidget)
	{
		SocialCardWidget->SetHudVisible(false);
	}
}

bool UProjectTargetingFixComponent::IsSocialCardVisible() const
{
	return SocialCardWidget && SocialCardWidget->IsHudVisible();
}

bool UProjectTargetingFixComponent::TryRepairCurrentTarget(
	UActorComponent* TargetingComponent,
	AActor*& OutTargetActor,
	USceneComponent*& OutTargetPointComponent)
{
	OutTargetActor = nullptr;
	OutTargetPointComponent = nullptr;

	if (!TargetingComponent)
	{
		return false;
	}

	UObject* CurrentTargetObject = nullptr;
	const AActor* OwningActor = Cast<AActor>(GetOwner());
	CurrentTargetObject = ProjectTargetingFixPrivate::FindBestActorPropertyValue(TargetingComponent, OwningActor);
	OutTargetActor = Cast<AActor>(CurrentTargetObject);

	UObject* CurrentTargetPointObject = nullptr;
	CurrentTargetPointObject = ProjectTargetingFixPrivate::FindBestSceneComponentPropertyValue(TargetingComponent, OutTargetActor);

	USceneComponent* CurrentTargetPointComponent = Cast<USceneComponent>(CurrentTargetPointObject);
	if (!OutTargetActor && CurrentTargetPointComponent)
	{
		OutTargetActor = CurrentTargetPointComponent->GetOwner();
	}

	if (!OutTargetActor)
	{
		return false;
	}

	if (!CurrentTargetPointComponent || !IsValidTargetPointForActor(OutTargetActor, CurrentTargetPointComponent))
	{
		if (UProjectEnemyLevelComponent* LevelComponent = OutTargetActor->FindComponentByClass<UProjectEnemyLevelComponent>())
		{
			CurrentTargetPointComponent = LevelComponent->GetPreferredTargetPointComponent();
		}
	}

	OutTargetPointComponent = CurrentTargetPointComponent;
	return true;
}

void UProjectTargetingFixComponent::ShowTargetInfoForActor(AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	if (UProjectEnemyTargetInfoComponent* TargetInfoComponent = TargetActor->FindComponentByClass<UProjectEnemyTargetInfoComponent>())
	{
		TargetInfoComponent->ShowTargetInfo();
	}
}

void UProjectTargetingFixComponent::HideTargetInfoForActor(AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	if (UProjectEnemyTargetInfoComponent* TargetInfoComponent = TargetActor->FindComponentByClass<UProjectEnemyTargetInfoComponent>())
	{
		TargetInfoComponent->HideTargetInfo();
	}
}

void UProjectTargetingFixComponent::HideScreenWidget()
{
	bManualSocialCardPreview = false;

	if (LevelWidget)
	{
		LevelWidget->SetOverlayVisible(false);
		LevelWidget->RemoveFromParent();
		LevelWidget = nullptr;
	}

	if (SocialCardWidget)
	{
		SocialCardWidget->SetHudVisible(false);
		SocialCardWidget->RemoveFromParent();
		SocialCardWidget = nullptr;
	}
}

bool UProjectTargetingFixComponent::IsValidTargetPointForActor(const AActor* TargetActor, const USceneComponent* TargetPointComponent) const
{
	if (!TargetActor || !TargetPointComponent)
	{
		return false;
	}

	if (TargetPointComponent->GetOwner() != TargetActor)
	{
		return false;
	}

	FVector Origin = FVector::ZeroVector;
	FVector BoxExtent = FVector::ZeroVector;
	TargetActor->GetActorBounds(true, Origin, BoxExtent);

	const float AllowedDistance = FMath::Max(BoxExtent.Size() * 3.0f, 250.0f);
	return FVector::Dist(TargetPointComponent->GetComponentLocation(), Origin) <= AllowedDistance;
}
