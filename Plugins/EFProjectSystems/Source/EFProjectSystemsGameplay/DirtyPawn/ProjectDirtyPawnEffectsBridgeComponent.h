#pragma once

#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "ProjectDirtyPawnEffectsBridgeComponent.generated.h"

class UDirtyPawnComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectInnerDoctrineComponent;
class UProjectSurvivalStatusComponent;

UCLASS(ClassGroup = (DirtyPawn), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDirtyPawnEffectsBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectDirtyPawnEffectsBridgeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Inner Doctrine")
	void RefreshDirtyPawnEffects();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine")
	bool bEnableDirtyPawnEffects = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Performance")
	bool bPrewarmDirtyPawnVisualChannelsOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine")
	FName DirtStatusName = TEXT("Dirty");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine")
	FName SweatyStatusName = TEXT("Sweaty");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine")
	FName DirtEffectSourceId = TEXT("DirtyPawn.Dirt");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine")
	FName BloodEffectSourceId = TEXT("DirtyPawn.Blood");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirtActivationThreshold = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BloodActivationThreshold = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine", meta = (ClampMin = "0.0"))
	float DirtDxpMultiplier = 1.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Inner Doctrine", meta = (ClampMin = "0.0"))
	float BloodFlatDamageBonus = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	bool bEnableSweatyBreathingNiagara = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	bool bSweatyBreathingOnlyLocalPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing", meta = (ClampMin = "0.0"))
	float SweatyBreathingDurationSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SweatyBreathingTriggerNormalized = 0.999f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SweatyBreathingRearmNormalized = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	TSoftObjectPtr<UNiagaraSystem> SweatyBreathingNiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Script/Niagara.NiagaraSystem'/Game/Ultimate_Smoke_Vfx/FX/Niagara/HeavyBreathing.HeavyBreathing'")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FSoftObjectPath SweatyBreathingSceneBlueprintPath = FSoftObjectPath(TEXT("/Script/Engine.Blueprint'/Game/_Game/Animations/Intimacy/Scenes/BP_IntimacyScene_0001.BP_IntimacyScene_0001'"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FName SweatyBreathingComponentName = TEXT("HeavyBreathing");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FName SweatyBreathingFallbackSocketName = TEXT("tongue1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	TArray<FName> SweatyBreathingRuntimeSocketCandidates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FVector SweatyBreathingMouthRelativeLocation = FVector(0.0f, 2.0f, 7.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	bool bUseSweatyBreathingTemplateRelativeScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FVector SweatyBreathingFallbackRelativeLocation = FVector(0.0f, 2.0f, 7.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FRotator SweatyBreathingFallbackRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweaty Breathing")
	FVector SweatyBreathingFallbackRelativeScale = FVector::OneVector;

protected:
	UFUNCTION()
	void HandleDirtyPawnStateChanged();

	void ResolveComponents();
	void PrewarmDirtyPawnVisualChannels();
	void ClearAppliedEffects();
	void SetDirtEffectsActive(bool bActive);
	void SetSweatyEffectsActive(bool bActive);
	void SetBloodEffectsActive(bool bActive);
	void UpdateSweatyBreathingTrigger();
	bool IsSweatyBreathingAllowedForOwner() const;
	bool ResolveSweatyBreathingAttachTarget(FName TemplateSocketName, class USkeletalMeshComponent*& OutMesh, FName& OutSocketName) const;
	bool ResolveSweatyBreathingTemplate(class UNiagaraSystem*& OutSystem, FName& OutSocketName, FTransform& OutRelativeTransform) const;
	bool EnsureSweatyBreathingNiagaraActive();
	void StopSweatyBreathingNiagara();
	void ScheduleSweatyBreathingStopDelay();
	void ClearSweatyBreathingStopTimer();

	UPROPERTY(Transient)
	TObjectPtr<UDirtyPawnComponent> DirtyPawnComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> InnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> StatusComponent;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveSweatyBreathingNiagara;

	FTimerHandle SweatyBreathingTimerHandle;
	float LastSweatNormalizedValue = 0.0f;
	bool bSweatyBreathingWasAtTrigger = false;
	bool bSweatyBreathingStopScheduled = false;
	bool bDirtEffectsActive = false;
	bool bSweatyEffectsActive = false;
	bool bBloodEffectsActive = false;
	bool bDirtyPawnVisualChannelsPrewarmed = false;
};
