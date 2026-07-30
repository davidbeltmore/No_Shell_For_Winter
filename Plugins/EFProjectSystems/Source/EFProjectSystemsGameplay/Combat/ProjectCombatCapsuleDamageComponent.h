#pragma once

#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "ProjectCombatTypes.h"
#include "ProjectCombatCapsuleDamageComponent.generated.h"

class UPrimitiveComponent;
class UCapsuleComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FProjectCombatHitSignature, AActor*, HitActor, FName, DamageType, float, AppliedDamage, bool, bKilledTarget, UPrimitiveComponent*, HitComponent);

UCLASS(ClassGroup = (Combat), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCombatCapsuleDamageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectCombatCapsuleDamageComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void RefreshTrackedCapsules();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void BeginDamageWindow(const FProjectCombatDamageSpec& DamageSpec, bool bResetHitCache);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EndDamageWindow();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetDamageEnabled(bool bEnabled, bool bResetHitCache);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ClearHitCache();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDamageEnabled() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FProjectCombatDamageSpec DefaultDamageSpec;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	EProjectCombatHitPolicy HitPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FName> PreferredCapsuleNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FString> PreferredCapsuleNameContains;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FString> CollisionManagerClassNameContains;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bAutoRegisterCapsules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bSearchAttachedActorsForCapsules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bFallbackToAllNonRootCapsulesWhenACFManagerExists;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bOnlyApplyDamageOnAuthority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bIgnoreOwner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bIgnoreInstigator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TObjectPtr<AActor> ExplicitDamageSourceActor;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FProjectCombatHitSignature OnSuccessfulHit;

protected:
	UFUNCTION()
	void HandleCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	virtual bool ShouldTrackCapsule(const UCapsuleComponent* CapsuleComponent) const;
	virtual bool ShouldDamageActor(AActor* CandidateActor) const;
	virtual FProjectCombatDamageSpec BuildDamageSpecForHit(AActor* HitActor, UPrimitiveComponent* HitComponent) const;
	virtual void HandleDamageDealt(AActor* HitActor, UPrimitiveComponent* HitComponent, const FProjectCombatDamageResult& DamageResult);
	virtual AActor* ResolveSourceActor() const;

private:
	void BindCapsule(UCapsuleComponent* CapsuleComponent);
	void UnbindAllCapsules();
	bool OwnerHasACFCollisionManager(const AActor* Actor) const;
	bool IsActorAlreadyHit(AActor* OtherActor) const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCapsuleComponent>> TrackedCapsules;

	bool bDamageEnabled;
	FProjectCombatDamageSpec ActiveDamageSpec;
	TSet<TWeakObjectPtr<AActor>> HitActors;
};
