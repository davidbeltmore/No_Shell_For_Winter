#include "EFCharacterCreationACFUBridge.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ACFCharacterMovementComponent.h"
#include "Animation/ACFAnimInstance.h"
#include "Animation/ACFIKLayer.h"
#include "Animation/ACFBaseOverlayLayer.h"
#include "UObject/UnrealType.h"
#include "EFDAZWeaponPoseSettings.h"
#include "EFDAZWeaponSocketAdapter.h"
#include "Engine/SkeletalMesh.h"

namespace
{
void ConfigureDAZWeaponOverlays(ACharacter* Character, UACFAnimInstance* Anim)
{
    auto* Mesh = Character ? Character->GetMesh() : nullptr;
    auto* Body = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    const auto* Settings = GetDefault<UEFDAZWeaponPoseSettings>();
    if (!Anim || !Body || !Settings->MeshRoots.ContainsByPredicate([Body](const FString& Root)
        { return !Root.IsEmpty() && Body->GetPathName().StartsWith(Root); })) return;
    UEFDAZWeaponSocketAdapter::EnsureAndRefresh(Character);
    auto* Property = FindFProperty<FArrayProperty>(UACFAnimInstance::StaticClass(), TEXT("OverlayLayers"));
    auto* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
    if (!Inner || Inner->Struct != FOverlayLayer::StaticStruct()) return;
    auto* Rows = Property->ContainerPtrToValuePtr<TArray<FOverlayLayer>>(Anim);
    const FGameplayTag Active = Anim->GetCurrentOverlay();
    bool RefreshActive = false;
    for (auto& Row : *Rows)
    {
        if (!Row.Overlay) continue;
        const FSoftObjectPath Source(Row.Overlay->GetPathName());
        const auto* Override = Settings->OverlayOverrides.FindByPredicate([&](const auto& Entry)
            { return Entry.Source.ToSoftObjectPath() == Source; });
        if (!Override) continue;
        UClass* Replacement = Override->Replacement.LoadSynchronous();
        if (!Replacement || !Replacement->IsChildOf(UACFBaseOverlayLayer::StaticClass()))
        {
            UE_LOG(LogTemp, Error, TEXT("[DAZWeaponPose] Invalid replacement for %s"), *Source.ToString());
            continue;
        }
        Row.Overlay = Replacement;
        RefreshActive |= Row.TagName == Active;
        UE_LOG(LogTemp, Display, TEXT("[DAZWeaponPose] actor=%s source=%s replacement=%s"),
            *Character->GetName(), *Source.ToString(), *Replacement->GetPathName());
    }
    if (RefreshActive) { Anim->RemoveOverlay(); Anim->SetAnimationOverlay(Active); }
}
}

void FEFCharacterCreationACFUBridge::RestoreAnimationAfterBodyChange(AActor* Actor)
{
	ACharacter* Character = Cast<ACharacter>(Actor);
	UACFCharacterMovementComponent* Movement = Character ? Cast<UACFCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	UACFAnimInstance* Current = Character && Character->GetMesh() ? Cast<UACFAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr;
	UACFAnimInstance* Previous = Movement ? Movement->GetACFAnimInstance() : nullptr;
	if (!IsValid(Current)) return;
	if (!IsValid(Previous) || Previous == Current)
	{
		ConfigureDAZWeaponOverlays(Character, Current);
		return;
	}

	// ACF caches the instance at BeginPlay. Changing skeleton recreates it,
	// losing fragment-authored tables and leaving equipment updates on the old one.
	// Reflect only the installed ACF contract's protected fields; never copy live
	// linked instances, delegates, pose state or arbitrary Blueprint properties.
	FObjectPropertyBase* Cache = FindFProperty<FObjectPropertyBase>(Movement->GetClass(), TEXT("animInstance"));
	if (!Cache || !Current->IsA(Cache->PropertyClass))
	{
		UE_LOG(LogTemp, Error, TEXT("Body preset: unsupported ACF animation cache contract on %s"), *GetNameSafe(Actor));
		return;
	}
	const TCHAR* Tables[] = { TEXT("MovesetLayers"), TEXT("OverlayLayers"), TEXT("RiderLayers"), TEXT("IKLayer"), TEXT("ClimbingLayer") };
	for (const TCHAR* Name : Tables)
	{
		FProperty* Property = FindFProperty<FProperty>(UACFAnimInstance::StaticClass(), Name);
		if (!Property)
		{
			UE_LOG(LogTemp, Error, TEXT("Body preset: missing ACF animation table %s"), Name);
			return;
		}
		Property->CopyCompleteValue_InContainer(Current, Previous);
	}
	Cache->SetObjectPropertyValue_InContainer(Movement, Current);
	ConfigureDAZWeaponOverlays(Character, Current);
	const auto ReadTag = [Movement](const TCHAR* Name)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(Movement->GetClass(), Name);
		return Property && Property->Struct == FGameplayTag::StaticStruct()
			? *Property->ContainerPtrToValuePtr<FGameplayTag>(Movement) : FGameplayTag();
	};
	Current->SetMoveset(ReadTag(TEXT("currentMoveset")));
	Current->SetAnimationOverlay(ReadTag(TEXT("currentOverlay")));
	const FGameplayTag Rider = ReadTag(TEXT("currentRider"));
	if (Rider.IsValid()) Current->SetRidingLayer(Rider);
	if (const FClassProperty* IK = FindFProperty<FClassProperty>(UACFAnimInstance::StaticClass(), TEXT("IKLayer")))
	{
		UClass* Layer = Cast<UClass>(IK->GetObjectPropertyValue_InContainer(Current));
		if (Layer && Layer != UACFIKLayer::StaticClass()) Current->LinkAnimClassLayers(Layer);
	}
	if (Movement->GetIsClimbingReplicated()) Current->ActivateClimbingLayer();
}

void FEFCharacterCreationACFUBridge::SetPawnCanMove(APawn* Pawn, bool bCanMove)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	for (UActorComponent* ActorComponent : Pawn->GetComponents())
	{
		if (!IsValid(ActorComponent))
		{
			continue;
		}

		if (ActorComponent->GetClass()->GetName().Contains(TEXT("ACFCharacterMovementComponent")))
		{
			if (UFunction* SetCanMoveFunction = ActorComponent->FindFunction(TEXT("SetCanMove")))
			{
				struct FSetCanMoveParams
				{
					bool bCanMove = true;
				};

				FSetCanMoveParams Params;
				Params.bCanMove = bCanMove;
				ActorComponent->ProcessEvent(SetCanMoveFunction, &Params);
			}
		}
	}
}

void FEFCharacterCreationACFUBridge::CancelPawnAbilities(APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	if (IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = AbilitySystemInterface->GetAbilitySystemComponent())
		{
			AbilitySystemComponent->CancelAllAbilities();
		}
	}
}
