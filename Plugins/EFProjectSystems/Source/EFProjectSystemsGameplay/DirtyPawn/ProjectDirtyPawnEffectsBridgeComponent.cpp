#include "DirtyPawn/ProjectDirtyPawnEffectsBridgeComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "DirtyPawnComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Character.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectDirtyPawnEffectsBridge, Log, All);

namespace ProjectDirtyPawnEffectsBridgePrivate
{
	FString CleanBlueprintObjectPath(const FSoftObjectPath& Path)
	{
		FString ObjectPath = Path.ToString();
		int32 QuoteStart = INDEX_NONE;
		int32 QuoteEnd = INDEX_NONE;
		if (ObjectPath.FindChar(TEXT('\''), QuoteStart) && ObjectPath.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
		{
			ObjectPath = ObjectPath.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		}

		return ObjectPath;
	}

	UClass* LoadBlueprintGeneratedClass(const FSoftObjectPath& BlueprintPath)
	{
		const FString ObjectPath = CleanBlueprintObjectPath(BlueprintPath);
		if (ObjectPath.IsEmpty())
		{
			return nullptr;
		}

		const FString GeneratedClassPath = ObjectPath.EndsWith(TEXT("_C")) ? ObjectPath : ObjectPath + TEXT("_C");
		return StaticLoadClass(AActor::StaticClass(), nullptr, *GeneratedClassPath);
	}

	bool ComponentNameMatches(const UActorComponent* Component, const FName NameHint, const FName TargetName)
	{
		if (!Component)
		{
			return false;
		}

		if (TargetName.IsNone())
		{
			return true;
		}

		const FString TargetString = TargetName.ToString();
		return Component->GetFName() == TargetName
			|| Component->GetName().Contains(TargetString)
			|| (!NameHint.IsNone() && (NameHint == TargetName || NameHint.ToString().Contains(TargetString)));
	}

	TArray<FName> BuildSocketCandidateList(const FName TemplateSocketName, const TArray<FName>& RuntimeSocketCandidates, const FName FallbackSocketName)
	{
		TArray<FName> Candidates;
		auto AddCandidate = [&Candidates](const FName Candidate)
		{
			if (!Candidate.IsNone())
			{
				Candidates.AddUnique(Candidate);
			}
		};

		AddCandidate(TemplateSocketName);
		for (const FName RuntimeCandidate : RuntimeSocketCandidates)
		{
			AddCandidate(RuntimeCandidate);
		}
		AddCandidate(FallbackSocketName);
		return Candidates;
	}

	const UNiagaraComponent* FindNiagaraComponentTemplate(UClass* SceneClass, const FName ComponentName)
	{
		if (!SceneClass)
		{
			return nullptr;
		}

		if (AActor* DefaultActor = Cast<AActor>(SceneClass->GetDefaultObject()))
		{
			TArray<UNiagaraComponent*> NiagaraComponents;
			DefaultActor->GetComponents(NiagaraComponents);
			for (const UNiagaraComponent* NiagaraComponent : NiagaraComponents)
			{
				if (ComponentNameMatches(NiagaraComponent, NAME_None, ComponentName))
				{
					return NiagaraComponent;
				}
			}
		}

		UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(SceneClass);
		USimpleConstructionScript* SimpleConstructionScript = BlueprintGeneratedClass
			? BlueprintGeneratedClass->SimpleConstructionScript.Get()
			: nullptr;

#if WITH_EDITOR
		if (!SimpleConstructionScript)
		{
			if (const UBlueprint* Blueprint = Cast<UBlueprint>(SceneClass->ClassGeneratedBy))
			{
				SimpleConstructionScript = Blueprint->SimpleConstructionScript.Get();
			}
		}
#endif

		if (!SimpleConstructionScript)
		{
			return nullptr;
		}

		for (const USCS_Node* Node : SimpleConstructionScript->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}

			UActorComponent* ComponentTemplate = nullptr;
			if (BlueprintGeneratedClass)
			{
				ComponentTemplate = const_cast<USCS_Node*>(Node)->GetActualComponentTemplate(BlueprintGeneratedClass);
			}

			if (!ComponentTemplate)
			{
				ComponentTemplate = Node->ComponentTemplate;
			}

			const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(ComponentTemplate);
			if (ComponentNameMatches(NiagaraComponent, Node->GetVariableName(), ComponentName))
			{
				return NiagaraComponent;
			}
		}

		return nullptr;
	}
}

UProjectDirtyPawnEffectsBridgeComponent::UProjectDirtyPawnEffectsBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;

	SweatyBreathingRuntimeSocketCandidates = {
		TEXT("mouth"),
		TEXT("Mouth"),
		TEXT("tongue1"),
		TEXT("Tongue1"),
		TEXT("tongue01"),
		TEXT("Tongue01"),
		TEXT("lowerJaw"),
		TEXT("LowerJaw"),
		TEXT("jaw"),
		TEXT("Jaw"),
		TEXT("head"),
		TEXT("Head")
	};
}

void UProjectDirtyPawnEffectsBridgeComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveComponents();
	PrewarmDirtyPawnVisualChannels();
	RefreshDirtyPawnEffects();
}

void UProjectDirtyPawnEffectsBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAppliedEffects();
	StopSweatyBreathingNiagara();

	if (DirtyPawnComponent)
	{
		DirtyPawnComponent->OnDirtyPawnStateChanged.RemoveDynamic(this, &ThisClass::HandleDirtyPawnStateChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void UProjectDirtyPawnEffectsBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	ResolveComponents();
	RefreshDirtyPawnEffects();
}

void UProjectDirtyPawnEffectsBridgeComponent::RefreshDirtyPawnEffects()
{
	if (!bEnableDirtyPawnEffects)
	{
		ClearAppliedEffects();
		return;
	}

	ResolveComponents();

	if (!DirtyPawnComponent)
	{
		ClearAppliedEffects();
		return;
	}

	const bool bDirtActive = DirtyPawnComponent->GetMaxVisiblePaintAlphaForState(EDirtyPawnPaintState::Dirt) >= FMath::Clamp(DirtActivationThreshold, 0.0f, 1.0f);
	const bool bSweatyActive = DirtyPawnComponent->IsSweaty()
		&& DirtyPawnComponent->GetMaxVisiblePaintAlphaForState(EDirtyPawnPaintState::Sweat) >= FMath::Clamp(DirtActivationThreshold, 0.0f, 1.0f);
	const bool bBloodActive = DirtyPawnComponent->GetMaxPaintAlphaForState(EDirtyPawnPaintState::Blood) >= FMath::Clamp(BloodActivationThreshold, 0.0f, 1.0f);

	SetDirtEffectsActive(bDirtActive);
	SetSweatyEffectsActive(bSweatyActive);
	SetBloodEffectsActive(bBloodActive);
	UpdateSweatyBreathingTrigger();
}

void UProjectDirtyPawnEffectsBridgeComponent::HandleDirtyPawnStateChanged()
{
	RefreshDirtyPawnEffects();
}

void UProjectDirtyPawnEffectsBridgeComponent::ResolveComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UDirtyPawnComponent* ResolvedDirtyPawn = UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(Owner);
	if (ResolvedDirtyPawn != DirtyPawnComponent)
	{
		if (DirtyPawnComponent)
		{
			DirtyPawnComponent->OnDirtyPawnStateChanged.RemoveDynamic(this, &ThisClass::HandleDirtyPawnStateChanged);
		}

		DirtyPawnComponent = ResolvedDirtyPawn;
		if (DirtyPawnComponent)
		{
			DirtyPawnComponent->OnDirtyPawnStateChanged.AddUniqueDynamic(this, &ThisClass::HandleDirtyPawnStateChanged);
		}
	}

	if (!InnerDoctrineComponent)
	{
		InnerDoctrineComponent = Owner->FindComponentByClass<UProjectInnerDoctrineComponent>();
	}

	if (!StatusComponent)
	{
		StatusComponent = Owner->FindComponentByClass<UProjectSurvivalStatusComponent>();
	}
}

void UProjectDirtyPawnEffectsBridgeComponent::PrewarmDirtyPawnVisualChannels()
{
	if (bDirtyPawnVisualChannelsPrewarmed || !bPrewarmDirtyPawnVisualChannelsOnBeginPlay)
	{
		return;
	}

	ResolveComponents();
	if (!DirtyPawnComponent)
	{
		return;
	}

	DirtyPawnComponent->PreinitializeDirtyPawn();
	DirtyPawnComponent->MudBandEvent(0.0f, 0.0f, 0.0f);
	DirtyPawnComponent->BloodBandEvent(0.0f, 0.0f, 0.0f);
	DirtyPawnComponent->SmearBandEvent(0.0f, 0.0f, 0.0f);
	DirtyPawnComponent->DirtBandEvent(0.0f, 0.0f, 0.0f);
	DirtyPawnComponent->SandBandEvent(0.0f, 0.0f, 0.0f);
	DirtyPawnComponent->SetFadeWashVariablesBand(0.0f, 0.0f, true, true, true);
	bDirtyPawnVisualChannelsPrewarmed = true;
}

void UProjectDirtyPawnEffectsBridgeComponent::ClearAppliedEffects()
{
	SetDirtEffectsActive(false);
	SetSweatyEffectsActive(false);
	SetBloodEffectsActive(false);
	StopSweatyBreathingNiagara();
	LastSweatNormalizedValue = 0.0f;
	bSweatyBreathingWasAtTrigger = false;
}

void UProjectDirtyPawnEffectsBridgeComponent::SetDirtEffectsActive(bool bActive)
{
	if (bActive == bDirtEffectsActive)
	{
		return;
	}

	ResolveComponents();
	bDirtEffectsActive = bActive;

	if (StatusComponent && !DirtStatusName.IsNone())
	{
		StatusComponent->SetForcedStatusActive(DirtStatusName, bActive);
	}

	if (InnerDoctrineComponent && !DirtEffectSourceId.IsNone())
	{
		if (bActive)
		{
			InnerDoctrineComponent->SetExternalDxpGainMultiplier(DirtEffectSourceId, FMath::Max(0.0f, DirtDxpMultiplier));
		}
		else
		{
			InnerDoctrineComponent->ClearExternalDxpGainMultiplier(DirtEffectSourceId);
		}
	}
}

void UProjectDirtyPawnEffectsBridgeComponent::SetSweatyEffectsActive(bool bActive)
{
	if (bActive == bSweatyEffectsActive)
	{
		return;
	}

	ResolveComponents();
	bSweatyEffectsActive = bActive;

	if (StatusComponent && !SweatyStatusName.IsNone())
	{
		StatusComponent->SetForcedStatusActive(SweatyStatusName, bActive);
	}
}

void UProjectDirtyPawnEffectsBridgeComponent::SetBloodEffectsActive(bool bActive)
{
	if (bActive == bBloodEffectsActive)
	{
		return;
	}

	ResolveComponents();
	bBloodEffectsActive = bActive;

	if (InnerDoctrineComponent && !BloodEffectSourceId.IsNone())
	{
		if (bActive)
		{
			InnerDoctrineComponent->SetExternalFlatDamageBonus(BloodEffectSourceId, FMath::Max(0.0f, BloodFlatDamageBonus));
		}
		else
		{
			InnerDoctrineComponent->ClearExternalFlatDamageBonus(BloodEffectSourceId);
		}
	}
}

void UProjectDirtyPawnEffectsBridgeComponent::UpdateSweatyBreathingTrigger()
{
	if (!DirtyPawnComponent)
	{
		LastSweatNormalizedValue = 0.0f;
		bSweatyBreathingWasAtTrigger = false;
		StopSweatyBreathingNiagara();
		return;
	}

	const float CurrentSweatNormalized = DirtyPawnComponent->GetSweatNormalizedValue();
	const float TriggerNormalized = FMath::Clamp(SweatyBreathingTriggerNormalized, 0.0f, 1.0f);
	const bool bAtTrigger = CurrentSweatNormalized >= TriggerNormalized;
	LastSweatNormalizedValue = CurrentSweatNormalized;

	if (!bEnableSweatyBreathingNiagara)
	{
		bSweatyBreathingWasAtTrigger = false;
		StopSweatyBreathingNiagara();
		return;
	}

	if (bAtTrigger)
	{
		if (!IsSweatyBreathingAllowedForOwner())
		{
			bSweatyBreathingWasAtTrigger = false;
			StopSweatyBreathingNiagara();
			return;
		}

		if (bSweatyBreathingStopScheduled)
		{
			ClearSweatyBreathingStopTimer();
			UE_LOG(
				LogProjectDirtyPawnEffectsBridge,
				Log,
				TEXT("[DirtyPawn] Cancelled sweaty breathing delayed stop because sweat returned to %.3f on %s."),
				CurrentSweatNormalized,
				*GetNameSafe(GetOwner()));
		}

		if (EnsureSweatyBreathingNiagaraActive())
		{
			bSweatyBreathingWasAtTrigger = true;
		}
		return;
	}

	bSweatyBreathingWasAtTrigger = false;

	if (ActiveSweatyBreathingNiagara)
	{
		ScheduleSweatyBreathingStopDelay();
	}
}

bool UProjectDirtyPawnEffectsBridgeComponent::IsSweatyBreathingAllowedForOwner() const
{
	if (!bSweatyBreathingOnlyLocalPlayer)
	{
		return true;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}

	if (OwnerPawn->IsLocallyControlled())
	{
		return true;
	}

	UWorld* World = GetWorld();
	return World && UGameplayStatics::GetPlayerPawn(World, 0) == OwnerPawn;
}

bool UProjectDirtyPawnEffectsBridgeComponent::ResolveSweatyBreathingAttachTarget(
	const FName TemplateSocketName,
	USkeletalMeshComponent*& OutMesh,
	FName& OutSocketName) const
{
	OutMesh = nullptr;
	OutSocketName = NAME_None;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	TArray<USkeletalMeshComponent*> OrderedMeshes;
	if (const ACharacter* Character = Cast<ACharacter>(OwnerActor))
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			OrderedMeshes.AddUnique(CharacterMesh);
		}
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	OwnerActor->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent)
		{
			OrderedMeshes.AddUnique(MeshComponent);
		}
	}

	const TArray<FName> SocketCandidates = ProjectDirtyPawnEffectsBridgePrivate::BuildSocketCandidateList(
		TemplateSocketName,
		SweatyBreathingRuntimeSocketCandidates,
		SweatyBreathingFallbackSocketName);

	for (USkeletalMeshComponent* MeshComponent : OrderedMeshes)
	{
		if (!MeshComponent)
		{
			continue;
		}

		for (const FName CandidateSocket : SocketCandidates)
		{
			if (!CandidateSocket.IsNone() && MeshComponent->DoesSocketExist(CandidateSocket))
			{
				OutMesh = MeshComponent;
				OutSocketName = CandidateSocket;
				return true;
			}
		}
	}

	return false;
}

bool UProjectDirtyPawnEffectsBridgeComponent::ResolveSweatyBreathingTemplate(
	UNiagaraSystem*& OutSystem,
	FName& OutSocketName,
	FTransform& OutRelativeTransform) const
{
	OutSystem = nullptr;
	OutSocketName = SweatyBreathingFallbackSocketName;
	OutRelativeTransform = FTransform(
		SweatyBreathingFallbackRelativeRotation,
		SweatyBreathingFallbackRelativeLocation,
		SweatyBreathingFallbackRelativeScale);

	if (!SweatyBreathingNiagaraSystem.IsNull())
	{
		OutSystem = SweatyBreathingNiagaraSystem.LoadSynchronous();
		if (OutSystem)
		{
			return true;
		}

		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Warning,
			TEXT("[DirtyPawn] Could not load direct sweaty breathing Niagara system from %s; trying legacy scene template."),
			*SweatyBreathingNiagaraSystem.ToSoftObjectPath().ToString());
	}

	UClass* SceneClass = ProjectDirtyPawnEffectsBridgePrivate::LoadBlueprintGeneratedClass(SweatyBreathingSceneBlueprintPath);
	if (!SceneClass)
	{
		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Warning,
			TEXT("[DirtyPawn] Could not resolve sweaty breathing Niagara system. DirectSystem=%s LegacyScene=%s."),
			*SweatyBreathingNiagaraSystem.ToSoftObjectPath().ToString(),
			*SweatyBreathingSceneBlueprintPath.ToString());
		return false;
	}

	const UNiagaraComponent* TemplateComponent = ProjectDirtyPawnEffectsBridgePrivate::FindNiagaraComponentTemplate(
		SceneClass,
		SweatyBreathingComponentName);
	if (!TemplateComponent)
	{
		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Warning,
			TEXT("[DirtyPawn] Could not resolve sweaty breathing Niagara system. Scene %s does not contain legacy component %s."),
			*GetNameSafe(SceneClass),
			*SweatyBreathingComponentName.ToString());
		return false;
	}

	OutSystem = TemplateComponent->GetAsset();
	if (!OutSystem)
	{
		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Warning,
			TEXT("[DirtyPawn] Niagara component %s in scene %s has no system asset."),
			*TemplateComponent->GetName(),
			*GetNameSafe(SceneClass));
		return false;
	}

	const FName TemplateSocketName = TemplateComponent->GetAttachSocketName();
	if (!TemplateSocketName.IsNone())
	{
		OutSocketName = TemplateSocketName;
	}

	OutRelativeTransform = TemplateComponent->GetRelativeTransform();
	return true;
}

bool UProjectDirtyPawnEffectsBridgeComponent::EnsureSweatyBreathingNiagaraActive()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	if (UNiagaraComponent* ExistingNiagara = ActiveSweatyBreathingNiagara)
	{
		if (!IsValid(ExistingNiagara))
		{
			ActiveSweatyBreathingNiagara = nullptr;
		}
		else if (ExistingNiagara->IsRegistered() && ExistingNiagara->IsActive() && ExistingNiagara->IsVisible())
		{
			return true;
		}
	}

	UNiagaraSystem* NiagaraSystem = nullptr;
	FName SocketName = NAME_None;
	FTransform RelativeTransform = FTransform::Identity;
	if (!ResolveSweatyBreathingTemplate(NiagaraSystem, SocketName, RelativeTransform) || !NiagaraSystem)
	{
		return false;
	}

	USkeletalMeshComponent* AttachMesh = nullptr;
	FName RuntimeSocketName = NAME_None;
	if (!ResolveSweatyBreathingAttachTarget(SocketName, AttachMesh, RuntimeSocketName) || !AttachMesh || RuntimeSocketName.IsNone())
	{
		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Warning,
			TEXT("[DirtyPawn] Could not find a valid mouth/head socket for sweaty breathing Niagara on %s. TemplateSocket=%s."),
			*GetNameSafe(OwnerActor),
			*SocketName.ToString());
		return false;
	}

	const FVector RuntimeScale = bUseSweatyBreathingTemplateRelativeScale
		? RelativeTransform.GetScale3D()
		: SweatyBreathingFallbackRelativeScale;

	if (ActiveSweatyBreathingNiagara)
	{
		ActiveSweatyBreathingNiagara->SetAsset(NiagaraSystem);
		ActiveSweatyBreathingNiagara->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, RuntimeSocketName);
		ActiveSweatyBreathingNiagara->SetRelativeLocation(SweatyBreathingMouthRelativeLocation);
		ActiveSweatyBreathingNiagara->SetRelativeRotation(SweatyBreathingFallbackRelativeRotation);
		ActiveSweatyBreathingNiagara->SetRelativeScale3D(RuntimeScale);
		ActiveSweatyBreathingNiagara->SetVisibility(true, true);
		ActiveSweatyBreathingNiagara->SetHiddenInGame(false, true);
		ActiveSweatyBreathingNiagara->SetComponentTickEnabled(true);
		ActiveSweatyBreathingNiagara->Activate(true);
		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Log,
			TEXT("[DirtyPawn] Reactivated sweaty breathing Niagara %s asset=%s owner=%s mesh=%s socket=%s templateSocket=%s offset=%s stopDelay=%.2f."),
			*ActiveSweatyBreathingNiagara->GetName(),
			*GetNameSafe(NiagaraSystem),
			*GetNameSafe(OwnerActor),
			*GetNameSafe(AttachMesh),
			*RuntimeSocketName.ToString(),
			*SocketName.ToString(),
			*SweatyBreathingMouthRelativeLocation.ToCompactString(),
			SweatyBreathingDurationSeconds);
		return true;
	}

	const FName ComponentName = MakeUniqueObjectName(OwnerActor, UNiagaraComponent::StaticClass(), TEXT("SweatyHeavyBreathing"));
	UNiagaraComponent* NiagaraComponent = NewObject<UNiagaraComponent>(OwnerActor, ComponentName, RF_Transient);
	if (!NiagaraComponent)
	{
		return false;
	}

	NiagaraComponent->SetAsset(NiagaraSystem);
	NiagaraComponent->bAutoActivate = false;
	NiagaraComponent->SetAutoDestroy(false);
	NiagaraComponent->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, RuntimeSocketName);
	NiagaraComponent->SetRelativeLocation(SweatyBreathingMouthRelativeLocation);
	NiagaraComponent->SetRelativeRotation(SweatyBreathingFallbackRelativeRotation);
	NiagaraComponent->SetRelativeScale3D(RuntimeScale);
	NiagaraComponent->SetVisibility(true, true);
	NiagaraComponent->SetHiddenInGame(false, true);
	NiagaraComponent->RegisterComponent();
	NiagaraComponent->Activate(true);

	ActiveSweatyBreathingNiagara = NiagaraComponent;

	UE_LOG(
		LogProjectDirtyPawnEffectsBridge,
		Log,
		TEXT("[DirtyPawn] Activated sweaty breathing Niagara %s asset=%s owner=%s mesh=%s socket=%s templateSocket=%s offset=%s stopDelay=%.2f."),
		*NiagaraComponent->GetName(),
		*GetNameSafe(NiagaraSystem),
		*GetNameSafe(OwnerActor),
		*GetNameSafe(AttachMesh),
		*RuntimeSocketName.ToString(),
		*SocketName.ToString(),
		*SweatyBreathingMouthRelativeLocation.ToCompactString(),
		SweatyBreathingDurationSeconds);
	return true;
}

void UProjectDirtyPawnEffectsBridgeComponent::StopSweatyBreathingNiagara()
{
	ClearSweatyBreathingStopTimer();

	if (UNiagaraComponent* NiagaraComponent = ActiveSweatyBreathingNiagara)
	{
		UE_LOG(
			LogProjectDirtyPawnEffectsBridge,
			Log,
			TEXT("[DirtyPawn] Stopped sweaty breathing Niagara %s owner=%s."),
			*NiagaraComponent->GetName(),
			*GetNameSafe(GetOwner()));
		NiagaraComponent->DeactivateImmediate();
		NiagaraComponent->SetVisibility(false, true);
		NiagaraComponent->SetHiddenInGame(true, true);
		NiagaraComponent->SetComponentTickEnabled(false);
		NiagaraComponent->DestroyComponent();
	}

	ActiveSweatyBreathingNiagara = nullptr;
}

void UProjectDirtyPawnEffectsBridgeComponent::ScheduleSweatyBreathingStopDelay()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!ActiveSweatyBreathingNiagara)
	{
		ClearSweatyBreathingStopTimer();
		return;
	}

	if (bSweatyBreathingStopScheduled && World->GetTimerManager().IsTimerActive(SweatyBreathingTimerHandle))
	{
		return;
	}

	bSweatyBreathingStopScheduled = false;
	World->GetTimerManager().ClearTimer(SweatyBreathingTimerHandle);
	const float Duration = FMath::Max(SweatyBreathingDurationSeconds, 0.0f);
	if (Duration <= 0.0f)
	{
		StopSweatyBreathingNiagara();
		return;
	}

	bSweatyBreathingStopScheduled = true;
	World->GetTimerManager().SetTimer(SweatyBreathingTimerHandle, this, &ThisClass::StopSweatyBreathingNiagara, Duration, false);
	UE_LOG(
		LogProjectDirtyPawnEffectsBridge,
		Log,
		TEXT("[DirtyPawn] Scheduled sweaty breathing delayed stop in %.2fs for %s."),
		Duration,
		*GetNameSafe(GetOwner()));
}

void UProjectDirtyPawnEffectsBridgeComponent::ClearSweatyBreathingStopTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SweatyBreathingTimerHandle);
	}
	bSweatyBreathingStopScheduled = false;
}
