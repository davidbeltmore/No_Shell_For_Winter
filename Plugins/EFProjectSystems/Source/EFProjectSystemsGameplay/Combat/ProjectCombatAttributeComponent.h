#pragma once

#include "Components/ActorComponent.h"
#include "ProjectCombatTypes.h"
#include "ProjectCombatAttributeComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FProjectCombatAttributeChangedSignature, FName, AttributeName, float, OldValue, float, NewValue, float, MaxValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FProjectCombatDamageAppliedSignature, AActor*, SourceActor, FName, DamageType, float, RequestedDamage, float, AppliedDamage, float, RemainingValue, bool, bKilledTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectCombatDeathSignature, AActor*, SourceActor);

UCLASS(ClassGroup = (Combat), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCombatAttributeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectCombatAttributeComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Combat")
	FProjectCombatDamageResult ApplyDamage(const FProjectCombatDamageSpec& DamageSpec);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	float HealAttribute(FName AttributeName, float HealAmount);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	float ModifyAttribute(FName AttributeName, float DeltaAmount);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetAttributeRecoveryBlocked(FName AttributeName, bool bBlocked);

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAttributeRecoveryBlocked(FName AttributeName) const;

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool SetAttributeCurrentValue(FName AttributeName, float NewValue);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool SetAttributeMaxValue(FName AttributeName, float NewMaxValue, bool bClampCurrentValue);

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetAttributeCurrentValue(FName AttributeName) const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetAttributeMaxValue(FName AttributeName) const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool HasAttribute(FName AttributeName) const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDead() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FProjectCombatAttribute> Attributes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FProjectCombatResistance> Resistances;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName HealthAttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName ArmorAttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bPauseRegenerationWhenDead;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FProjectCombatAttributeChangedSignature OnAttributeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FProjectCombatDamageAppliedSignature OnDamageApplied;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FProjectCombatDeathSignature OnDeath;

protected:
	virtual float GetResistanceMultiplier(FName DamageType) const;
	virtual float GetArmorMitigation(const FProjectCombatDamageSpec& DamageSpec) const;
	virtual bool ShouldDieFromDamage(const FProjectCombatDamageResult& DamageResult) const;
	virtual void HandlePostDamageApplied(const FProjectCombatDamageSpec& DamageSpec, const FProjectCombatDamageResult& DamageResult);
	virtual void HandleDeath(AActor* SourceActor);

	FProjectCombatAttribute* FindMutableAttribute(FName AttributeName);
	const FProjectCombatAttribute* FindAttribute(FName AttributeName) const;
	float ClampAttributeValue(const FProjectCombatAttribute& Attribute, float Value) const;
	void SanitizeAttributes();
	bool InternalSetAttributeCurrentValue(FProjectCombatAttribute& Attribute, float NewValue);
	FProjectIncomingHitContext BuildIncomingHitContext(
		const FProjectCombatDamageSpec& DamageSpec,
		float RequestedDamage,
		float AppliedDamage,
		float RemainingHealth,
		bool bKilledTarget,
		float DefensiveFlatNegatedDamage = 0.f,
		float GuardRecoveryAbsorbedDamage = 0.f) const;
	float ApplyQualifiedPainHit(FProjectIncomingHitContext& HitContext);

private:
	bool bIsDead;
	TSet<FName> RecoveryBlockedAttributes;
};
