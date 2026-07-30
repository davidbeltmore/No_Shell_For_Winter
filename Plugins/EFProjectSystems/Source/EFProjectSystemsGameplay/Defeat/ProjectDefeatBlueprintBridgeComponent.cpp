#include "Defeat/ProjectDefeatBlueprintBridgeComponent.h"

#include "Components/ActorComponent.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "GameFramework/Actor.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectDefeatBridge, Log, All);

namespace
{
	const FName PainName(TEXT("Pain"));

	FString GetPublicStateString(const EProjectDefeatPublicState State)
	{
		if (const UEnum* Enum = StaticEnum<EProjectDefeatPublicState>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(State));
		}

		return TEXT("Normal");
	}

	FName GetPublicStateName(const EProjectDefeatPublicState State)
	{
		return FName(*GetPublicStateString(State));
	}

	bool ClassNameContainsAnyHint(const UObject* Object, const TArray<FString>& ClassHints)
	{
		if (!Object)
		{
			return false;
		}

		const UClass* ObjectClass = Object->GetClass();
		for (const UClass* CurrentClass = ObjectClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			const FString ClassName = CurrentClass->GetName();
			for (const FString& Hint : ClassHints)
			{
				if (!Hint.IsEmpty() && ClassName.Contains(Hint, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}

		return false;
	}
}

UProjectDefeatBlueprintBridgeComponent::UProjectDefeatBlueprintBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
}

void UProjectDefeatBlueprintBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshDependencies();
	BindDelegates();
	RefreshExternalTargets();
	EmitDefeatPointsChanged();
	SyncPublicState(true);
	UpdateReactiveTickState();
}

void UProjectDefeatBlueprintBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindDelegates();
	Super::EndPlay(EndPlayReason);
}

void UProjectDefeatBlueprintBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)DeltaTime;
	(void)TickType;
	(void)ThisTickFunction;

	if (!DefeatFlowComponent || !NeedsComponent)
	{
		RefreshDependencies();
		BindDelegates();
		if (DefeatFlowComponent || NeedsComponent)
		{
			EmitDefeatPointsChanged();
			SyncPublicState(true);
		}
	}
	UpdateReactiveTickState();
}

EProjectDefeatPublicState UProjectDefeatBlueprintBridgeComponent::GetPublicState() const
{
	return CurrentPublicState;
}

float UProjectDefeatBlueprintBridgeComponent::GetDefeatPointsNormalized() const
{
	const float SafeThreshold = FMath::Max(CachedPainThreshold, 1.f);
	return FMath::Clamp(CachedPainCurrent / SafeThreshold, 0.f, 1.f);
}

bool UProjectDefeatBlueprintBridgeComponent::TryStartExternalRuntimeScene(const FProjectDefeatTransferPayload& Payload)
{
	if (Payload.PostDefeatPresentation != EProjectPostDefeatPresentation::MatureSoloVignette)
	{
		return false;
	}

	RefreshExternalTargets();

	UObject* SceneTarget = ExternalSceneTarget.Get();
	if (!SceneTarget)
	{
		return false;
	}

	const bool bCapsuleToggled = TryInvokeBoolFunction(SceneTarget, TEXT("ToggleCapsuleCollision"), false);
	const bool bRegenPaused = TryInvokeBoolFunction(SceneTarget, TEXT("ToggleDefeatRegen"), true);
	const bool bStoppedAnimation = TryInvokeNoArgFunction(SceneTarget, TEXT("StopCurrentAnimation"));
	const bool bSetActivity = TryInvokeSceneStartFunction(SceneTarget, TEXT("SetActivity"), Payload.SceneDefinition.InteractionId);
	const bool bStartedRuntimeScene = TryInvokeSceneStartFunction(SceneTarget, TEXT("StartRuntimeScene"), Payload.SceneDefinition.InteractionId);
	const bool bPlayedMontage = TryInvokeSceneStartFunction(SceneTarget, TEXT("PlayAnimationMontage"), Payload.SceneDefinition.InteractionId)
		|| TryInvokeSceneStartFunction(SceneTarget, TEXT("PlayAnimMontage"), Payload.SceneDefinition.InteractionId);
	bExternalScenePrepared = bCapsuleToggled || bRegenPaused || bStoppedAnimation;
	bExternalSceneActive = bSetActivity || bStartedRuntimeScene || bPlayedMontage;

	if (bExternalScenePrepared || bExternalSceneActive)
	{
		UE_LOG(
			LogProjectDefeatBridge,
			Log,
			TEXT("[SceneAdapter] Prepared=%s Active=%s SceneTarget=%s SceneId=%s"),
			bExternalScenePrepared ? TEXT("true") : TEXT("false"),
			bExternalSceneActive ? TEXT("true") : TEXT("false"),
			*SceneTarget->GetName(),
			*Payload.SceneDefinition.InteractionId.ToString());
	}

	return bExternalSceneActive;
}

bool UProjectDefeatBlueprintBridgeComponent::TryStopExternalRuntimeScene(const FProjectDefeatTransferPayload& Payload, const bool bCancelledByPlayer)
{
	UObject* SceneTarget = ExternalSceneTarget.Get();
	if (!SceneTarget || !bExternalScenePrepared)
	{
		return false;
	}

	const bool bStoppedRuntimeScene = bExternalSceneActive
		? TryInvokeSceneStopFunction(SceneTarget, TEXT("StopRuntimeScene"), bCancelledByPlayer)
		: false;
	TryInvokeBoolFunction(SceneTarget, TEXT("ToggleDefeatRegen"), false);
	TryInvokeBoolFunction(SceneTarget, TEXT("ToggleCapsuleCollision"), true);
	TryInvokeNoArgFunction(SceneTarget, TEXT("StopCurrentAnimation"));
	bExternalScenePrepared = false;
	bExternalSceneActive = false;

	(void)Payload;
	return bStoppedRuntimeScene;
}

void UProjectDefeatBlueprintBridgeComponent::HandleSurvivalValueChanged(
	const FName EntryName,
	const float OldValue,
	const float NewValue,
	const float MaxValue,
	const bool bIsSensation)
{
	(void)OldValue;
	if (!bIsSensation || EntryName != PainName)
	{
		return;
	}

	CachedPainCurrent = NewValue;
	CachedPainThreshold = FMath::Max(MaxValue, 1.f);
	EmitDefeatPointsChanged();
	SyncPublicState(true);
}

void UProjectDefeatBlueprintBridgeComponent::HandleKnockoutStateChanged(
	const EProjectDefeatPhase NewPhase,
	const EProjectKnockoutReason KnockoutReason,
	const bool bActive)
{
	(void)NewPhase;
	(void)KnockoutReason;
	(void)bActive;
	SyncPublicState(true);
}

void UProjectDefeatBlueprintBridgeComponent::HandleDefeatedSceneChanged(
	const FProjectDefeatSceneDefinition& SceneDefinition,
	const bool bActive)
{
	(void)SceneDefinition;
	(void)bActive;
	SyncPublicState(true);
}

void UProjectDefeatBlueprintBridgeComponent::HandleDefeatStateRefreshed()
{
	SyncPublicState(true);
}

void UProjectDefeatBlueprintBridgeComponent::RefreshDependencies()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	DefeatFlowComponent = OwnerActor->FindComponentByClass<UProjectDefeatFlowComponent>();
	NeedsComponent = OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	if (NeedsComponent)
	{
		CachedPainCurrent = NeedsComponent->GetSensationCurrentValue(PainName);
		CachedPainThreshold = FMath::Max(NeedsComponent->GetSensationMaxValue(PainName), 1.f);
	}
	else if (const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get())
	{
		CachedPainThreshold = FMath::Max(Settings->PainKnockoutThreshold, 1.f);
	}
}

void UProjectDefeatBlueprintBridgeComponent::BindDelegates()
{
	if (NeedsComponent)
	{
		NeedsComponent->OnSurvivalValueChanged.RemoveDynamic(this, &ThisClass::HandleSurvivalValueChanged);
		NeedsComponent->OnSurvivalValueChanged.AddUniqueDynamic(this, &ThisClass::HandleSurvivalValueChanged);
	}

	if (DefeatFlowComponent)
	{
		DefeatFlowComponent->OnKnockoutStateChanged.RemoveDynamic(this, &ThisClass::HandleKnockoutStateChanged);
		DefeatFlowComponent->OnKnockoutStateChanged.AddUniqueDynamic(this, &ThisClass::HandleKnockoutStateChanged);
		DefeatFlowComponent->OnDefeatedSceneChanged.RemoveDynamic(this, &ThisClass::HandleDefeatedSceneChanged);
		DefeatFlowComponent->OnDefeatedSceneChanged.AddUniqueDynamic(this, &ThisClass::HandleDefeatedSceneChanged);
		DefeatFlowComponent->OnDefeatStateRefreshed.RemoveDynamic(this, &ThisClass::HandleDefeatStateRefreshed);
		DefeatFlowComponent->OnDefeatStateRefreshed.AddUniqueDynamic(this, &ThisClass::HandleDefeatStateRefreshed);
	}
}

void UProjectDefeatBlueprintBridgeComponent::UnbindDelegates()
{
	if (NeedsComponent)
	{
		NeedsComponent->OnSurvivalValueChanged.RemoveDynamic(this, &ThisClass::HandleSurvivalValueChanged);
	}

	if (DefeatFlowComponent)
	{
		DefeatFlowComponent->OnKnockoutStateChanged.RemoveDynamic(this, &ThisClass::HandleKnockoutStateChanged);
		DefeatFlowComponent->OnDefeatedSceneChanged.RemoveDynamic(this, &ThisClass::HandleDefeatedSceneChanged);
		DefeatFlowComponent->OnDefeatStateRefreshed.RemoveDynamic(this, &ThisClass::HandleDefeatStateRefreshed);
	}
}

void UProjectDefeatBlueprintBridgeComponent::UpdateReactiveTickState()
{
	SetComponentTickEnabled(!DefeatFlowComponent || !NeedsComponent);
}

void UProjectDefeatBlueprintBridgeComponent::SyncPublicState(const bool bBroadcastIfChanged)
{
	EProjectDefeatPublicState NewState = EProjectDefeatPublicState::Normal;
	if (DefeatFlowComponent)
	{
		switch (DefeatFlowComponent->GetCurrentPhase())
		{
		case EProjectDefeatPhase::KnockedOut:
		case EProjectDefeatPhase::Struggle:
		case EProjectDefeatPhase::DefeatedBlackout:
		case EProjectDefeatPhase::TravelPending:
			NewState = EProjectDefeatPublicState::Downed;
			break;
		case EProjectDefeatPhase::DefeatedScene:
			NewState = EProjectDefeatPublicState::DefeatedScene;
			break;
		case EProjectDefeatPhase::None:
		default:
			NewState = DefeatFlowComponent->IsLosingActive()
				? EProjectDefeatPublicState::Injured
				: EProjectDefeatPublicState::Normal;
			break;
		}
	}

	if (NewState == CurrentPublicState)
	{
		return;
	}

	CurrentPublicState = NewState;
	if (!bBroadcastIfChanged)
	{
		return;
	}

	RefreshExternalTargets();
	OnDefeatStateChanged.Broadcast(CurrentPublicState);
	PushExternalPublicState(CurrentPublicState);
	switch (CurrentPublicState)
	{
	case EProjectDefeatPublicState::Injured:
		OnInjured.Broadcast();
		TryBroadcastNoArgDelegate(ExternalPlayerEventsTarget.Get(), TEXT("OnInjured"));
		TryInvokeNoArgFunction(ExternalPlayerEventsTarget.Get(), TEXT("OnInjured"));
		break;
	case EProjectDefeatPublicState::Downed:
		OnDowned.Broadcast();
		TryBroadcastNoArgDelegate(ExternalPlayerEventsTarget.Get(), TEXT("OnDowned"));
		TryInvokeNoArgFunction(ExternalPlayerEventsTarget.Get(), TEXT("OnDowned"));
		break;
	case EProjectDefeatPublicState::DefeatedScene:
		OnDefeated.Broadcast();
		TryBroadcastNoArgDelegate(ExternalPlayerEventsTarget.Get(), TEXT("OnDefeated"));
		TryInvokeNoArgFunction(ExternalPlayerEventsTarget.Get(), TEXT("OnDefeated"));
		break;
	case EProjectDefeatPublicState::Normal:
	default:
		break;
	}
}

void UProjectDefeatBlueprintBridgeComponent::EmitDefeatPointsChanged()
{
	const float SafeThreshold = FMath::Max(CachedPainThreshold, 1.f);
	const float NormalizedPoints = FMath::Clamp(CachedPainCurrent / SafeThreshold, 0.f, 1.f);
	OnDefeatPointsChanged.Broadcast(CachedPainCurrent, SafeThreshold, NormalizedPoints);
	TryBroadcastExternalDefeatPoints(FMath::RoundToInt(CachedPainCurrent));
}

void UProjectDefeatBlueprintBridgeComponent::RefreshExternalTargets()
{
	ExternalPlayerEventsTarget = FindExternalObject({ TEXT("AC_PlayerEvents") }, {});
	ExternalStateHandlerTarget = FindExternalObject({ TEXT("AC_StateHandler") }, { TEXT("SetState") });
	ExternalSceneTarget = FindExternalObject(
		{ TEXT("AC_MaturePresentation"), TEXT("BPI_MaturePresentation") },
		{
			TEXT("ToggleCapsuleCollision"),
			TEXT("ToggleDefeatRegen"),
			TEXT("StopCurrentAnimation"),
			TEXT("SetActivity"),
			TEXT("StartRuntimeScene"),
			TEXT("StopRuntimeScene")
		});
}

void UProjectDefeatBlueprintBridgeComponent::PushExternalPublicState(const EProjectDefeatPublicState NewState)
{
	UObject* PlayerEventsTarget = ExternalPlayerEventsTarget.Get();
	if (PlayerEventsTarget)
	{
		TryBroadcastStateDelegate(PlayerEventsTarget, TEXT("OnDefeatStateChanged"), NewState);
		TryBroadcastStateDelegate(PlayerEventsTarget, TEXT("DefeatStateChanged"), NewState);
		TryInvokeStateFunction(PlayerEventsTarget, TEXT("OnDefeatStateChanged"), NewState);
	}

	UObject* StateHandlerTarget = ExternalStateHandlerTarget.Get();
	if (StateHandlerTarget)
	{
		TryInvokeStateFunction(StateHandlerTarget, TEXT("SetState"), NewState);
		TryInvokeStateFunction(StateHandlerTarget, TEXT("HandleStateChanged"), NewState);
	}
}

UObject* UProjectDefeatBlueprintBridgeComponent::FindExternalObject(
	const TArray<FString>& ClassHints,
	const TArray<FName>& CallableNames) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	auto Matches = [&ClassHints, &CallableNames](UObject* Candidate) -> bool
	{
		if (!Candidate)
		{
			return false;
		}

		if (ClassNameContainsAnyHint(Candidate, ClassHints))
		{
			return true;
		}

		for (const FName CallableName : CallableNames)
		{
			if (!CallableName.IsNone() && Candidate->FindFunction(CallableName))
			{
				return true;
			}
		}

		return false;
	};

	if (Matches(OwnerActor))
	{
		return OwnerActor;
	}

	TInlineComponentArray<UActorComponent*> Components(OwnerActor);
	for (UActorComponent* Component : Components)
	{
		if (Matches(Component))
		{
			return Component;
		}
	}

	return nullptr;
}

bool UProjectDefeatBlueprintBridgeComponent::TryInvokeNoArgFunction(UObject* Target, const FName FunctionName) const
{
	if (!Target)
	{
		return false;
	}

	UFunction* Function = Target->FindFunction(FunctionName);
	if (!Function || Function->NumParms != 0)
	{
		return false;
	}

	Target->ProcessEvent(Function, nullptr);
	return true;
}

bool UProjectDefeatBlueprintBridgeComponent::TryInvokeBoolFunction(UObject* Target, const FName FunctionName, const bool bValue) const
{
	if (!Target)
	{
		return false;
	}

	UFunction* Function = Target->FindFunction(FunctionName);
	if (!Function || Function->NumParms != 1)
	{
		return false;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
	FMemory::Memzero(Params, Function->ParmsSize);

	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		FProperty* Property = *It;
		if (!(Property->PropertyFlags & CPF_Parm) || (Property->PropertyFlags & CPF_ReturnParm))
		{
			continue;
		}

		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			BoolProperty->SetPropertyValue(Property->ContainerPtrToValuePtr<void>(Params), bValue);
			Target->ProcessEvent(Function, Params);
			return true;
		}

		return false;
	}

	return false;
}

bool UProjectDefeatBlueprintBridgeComponent::TryInvokeStateFunction(
	UObject* Target,
	const FName FunctionName,
	const EProjectDefeatPublicState State) const
{
	if (!Target)
	{
		return false;
	}

	UFunction* Function = Target->FindFunction(FunctionName);
	if (!Function || Function->NumParms != 1)
	{
		return false;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
	FMemory::Memzero(Params, Function->ParmsSize);

	const FString StateString = GetPublicStateString(State);
	const FName StateName = GetPublicStateName(State);
	const int64 NumericState = static_cast<int64>(State);

	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		FProperty* Property = *It;
		if (!(Property->PropertyFlags & CPF_Parm) || (Property->PropertyFlags & CPF_ReturnParm))
		{
			continue;
		}

		void* ParamValue = Property->ContainerPtrToValuePtr<void>(Params);
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ParamValue, NumericState);
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			ByteProperty->SetPropertyValue(ParamValue, static_cast<uint8>(State));
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			IntProperty->SetPropertyValue(ParamValue, static_cast<int32>(State));
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue(ParamValue, StateName);
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			StringProperty->SetPropertyValue(ParamValue, StateString);
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			TextProperty->SetPropertyValue(ParamValue, FText::FromString(StateString));
			Target->ProcessEvent(Function, Params);
			return true;
		}

		return false;
	}

	return false;
}

bool UProjectDefeatBlueprintBridgeComponent::TryInvokeSceneStartFunction(
	UObject* Target,
	const FName FunctionName,
	const FName SceneId) const
{
	if (!Target)
	{
		return false;
	}

	UFunction* Function = Target->FindFunction(FunctionName);
	if (!Function)
	{
		return false;
	}

	if (Function->NumParms == 0)
	{
		Target->ProcessEvent(Function, nullptr);
		return true;
	}

	if (Function->NumParms != 1)
	{
		return false;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
	FMemory::Memzero(Params, Function->ParmsSize);

	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		FProperty* Property = *It;
		if (!(Property->PropertyFlags & CPF_Parm) || (Property->PropertyFlags & CPF_ReturnParm))
		{
			continue;
		}

		void* ParamValue = Property->ContainerPtrToValuePtr<void>(Params);
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue(ParamValue, SceneId);
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			StringProperty->SetPropertyValue(ParamValue, SceneId.ToString());
			Target->ProcessEvent(Function, Params);
			return true;
		}

		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			TextProperty->SetPropertyValue(ParamValue, FText::FromString(SceneId.ToString()));
			Target->ProcessEvent(Function, Params);
			return true;
		}

		return false;
	}

	return false;
}

bool UProjectDefeatBlueprintBridgeComponent::TryInvokeSceneStopFunction(
	UObject* Target,
	const FName FunctionName,
	const bool bCancelledByPlayer) const
{
	if (TryInvokeBoolFunction(Target, FunctionName, bCancelledByPlayer))
	{
		return true;
	}

	return TryInvokeNoArgFunction(Target, FunctionName);
}

bool UProjectDefeatBlueprintBridgeComponent::TryBroadcastNoArgDelegate(UObject* Target, const FName DelegateName) const
{
	if (!Target)
	{
		return false;
	}

	const FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(Target->GetClass(), DelegateName);
	if (!DelegateProperty || !DelegateProperty->SignatureFunction || DelegateProperty->SignatureFunction->NumParms != 0)
	{
		return false;
	}

	if (FMulticastScriptDelegate* Delegate = DelegateProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Target))
	{
		Delegate->ProcessDelegate<UObject>(nullptr);
		return true;
	}

	return false;
}

bool UProjectDefeatBlueprintBridgeComponent::TryBroadcastStateDelegate(
	UObject* Target,
	const FName DelegateName,
	const EProjectDefeatPublicState State) const
{
	if (!Target)
	{
		return false;
	}

	const FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(Target->GetClass(), DelegateName);
	if (!DelegateProperty || !DelegateProperty->SignatureFunction || DelegateProperty->SignatureFunction->NumParms != 1)
	{
		return false;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(DelegateProperty->SignatureFunction->ParmsSize));
	FMemory::Memzero(Params, DelegateProperty->SignatureFunction->ParmsSize);

	const FString StateString = GetPublicStateString(State);
	const FName StateName = GetPublicStateName(State);
	const int64 NumericState = static_cast<int64>(State);

	for (TFieldIterator<FProperty> It(DelegateProperty->SignatureFunction); It; ++It)
	{
		FProperty* Property = *It;
		if (!(Property->PropertyFlags & CPF_Parm) || (Property->PropertyFlags & CPF_ReturnParm))
		{
			continue;
		}

		void* ParamValue = Property->ContainerPtrToValuePtr<void>(Params);
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ParamValue, NumericState);
		}
		else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			ByteProperty->SetPropertyValue(ParamValue, static_cast<uint8>(State));
		}
		else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			IntProperty->SetPropertyValue(ParamValue, static_cast<int32>(State));
		}
		else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue(ParamValue, StateName);
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			StringProperty->SetPropertyValue(ParamValue, StateString);
		}
		else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			TextProperty->SetPropertyValue(ParamValue, FText::FromString(StateString));
		}
		else
		{
			return false;
		}
	}

	if (FMulticastScriptDelegate* Delegate = DelegateProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Target))
	{
		Delegate->ProcessDelegate<UObject>(Params);
		return true;
	}

	return false;
}

bool UProjectDefeatBlueprintBridgeComponent::TryBroadcastExternalDefeatPoints(const int32 Points) const
{
	UObject* PlayerEventsTarget = ExternalPlayerEventsTarget.Get();
	if (!PlayerEventsTarget)
	{
		return false;
	}

	const FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(PlayerEventsTarget->GetClass(), TEXT("DefeatPointsChanged"));
	if (!DelegateProperty || !DelegateProperty->SignatureFunction)
	{
		return false;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(DelegateProperty->SignatureFunction->ParmsSize));
	FMemory::Memzero(Params, DelegateProperty->SignatureFunction->ParmsSize);

	for (TFieldIterator<FProperty> It(DelegateProperty->SignatureFunction); It; ++It)
	{
		FProperty* Property = *It;
		if (!(Property->PropertyFlags & CPF_Parm) || (Property->PropertyFlags & CPF_ReturnParm))
		{
			continue;
		}

		if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			IntProperty->SetPropertyValue(Property->ContainerPtrToValuePtr<void>(Params), Points);
		}
		else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			FloatProperty->SetPropertyValue(Property->ContainerPtrToValuePtr<void>(Params), static_cast<float>(Points));
		}
		else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			DoubleProperty->SetPropertyValue(Property->ContainerPtrToValuePtr<void>(Params), static_cast<double>(Points));
		}
		else
		{
			return false;
		}
	}

	if (FMulticastScriptDelegate* Delegate = DelegateProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(PlayerEventsTarget))
	{
		Delegate->ProcessDelegate<UObject>(Params);
		return true;
	}

	return false;
}
