#include "Lockpicking/ProjectLockpickingACFBridge.h"

#include "GameFramework/Pawn.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectLockpickingACFBridge, Log, All);

namespace ProjectLockpickingACFBridgePrivate
{
	const FName OnInteractedByPawnFunctionName(TEXT("OnInteractedByPawn"));
	const FName OnLocalInteractedByPawnFunctionName(TEXT("OnLocalInteractedByPawn"));

	bool ExtractACFInteractionParams(UFunction* Function, void* Parms, APawn*& OutPawn, FString& OutInteractionType)
	{
		OutPawn = nullptr;
		OutInteractionType.Reset();

		if (!Function || !Parms)
		{
			return false;
		}

		for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			if (!OutPawn)
			{
				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					if (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(APawn::StaticClass()))
					{
						OutPawn = Cast<APawn>(ObjectProperty->GetObjectPropertyValue_InContainer(Parms));
						continue;
					}
				}
			}

			if (OutInteractionType.IsEmpty())
			{
				if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
				{
					OutInteractionType = StringProperty->GetPropertyValue_InContainer(Parms);
				}
			}
		}

		return OutPawn != nullptr;
	}
}

bool ProjectLockpickingACFBridge::TryConsumeACFInteractionProcessEvent(
	AActor* Owner,
	UProjectLockpickableComponent* LockpickableComponent,
	UFunction* Function,
	void* Parms,
	const bool bLogGate)
{
	if (!Owner || !Function || !LockpickableComponent)
	{
		return false;
	}

	const FName FunctionName = Function->GetFName();
	if (FunctionName != ProjectLockpickingACFBridgePrivate::OnInteractedByPawnFunctionName
		&& FunctionName != ProjectLockpickingACFBridgePrivate::OnLocalInteractedByPawnFunctionName)
	{
		return false;
	}

	APawn* Pawn = nullptr;
	FString InteractionType;
	if (!ProjectLockpickingACFBridgePrivate::ExtractACFInteractionParams(Function, Parms, Pawn, InteractionType))
	{
		return false;
	}

	if (FunctionName == ProjectLockpickingACFBridgePrivate::OnLocalInteractedByPawnFunctionName)
	{
		const bool bHandledLocally = LockpickableComponent->HandleACFLocalInteraction(Pawn, InteractionType);
		if (bHandledLocally && bLogGate)
		{
			UE_LOG(
				LogProjectLockpickingACFBridge,
				Log,
				TEXT("[Lockpicking] Consumed local ACF interaction on %s for %s (%s)"),
				*GetNameSafe(Owner),
				*GetNameSafe(Pawn),
				*InteractionType);
		}
		return bHandledLocally;
	}

	EProjectLockpickInteractionGateResult GateResult = EProjectLockpickInteractionGateResult::RunOriginal;
	LockpickableComponent->HandleACFInteraction(Pawn, InteractionType, GateResult);
	if (GateResult == EProjectLockpickInteractionGateResult::Consumed && bLogGate)
	{
		UE_LOG(
			LogProjectLockpickingACFBridge,
			Log,
			TEXT("[Lockpicking] Consumed server ACF interaction on %s for %s (%s)"),
			*GetNameSafe(Owner),
			*GetNameSafe(Pawn),
			*InteractionType);
	}
	return GateResult == EProjectLockpickInteractionGateResult::Consumed;
}
