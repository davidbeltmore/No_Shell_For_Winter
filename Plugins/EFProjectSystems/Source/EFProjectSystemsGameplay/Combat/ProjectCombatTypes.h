#pragma once

#include "CoreMinimal.h"
#include "ProjectCombatTypes.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCombatAttribute
{
	GENERATED_BODY()

	FProjectCombatAttribute()
		: AttributeName(TEXT("Health"))
		, CurrentValue(100.f)
		, MaxValue(100.f)
		, MinValue(0.f)
		, RegenPerSecond(0.f)
	{
	}

	FProjectCombatAttribute(FName InAttributeName, float InCurrentValue, float InMaxValue)
		: AttributeName(InAttributeName)
		, CurrentValue(InCurrentValue)
		, MaxValue(InMaxValue)
		, MinValue(0.f)
		, RegenPerSecond(0.f)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float MaxValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float MinValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float RegenPerSecond;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCombatResistance
{
	GENERATED_BODY()

	FProjectCombatResistance()
		: DamageType(TEXT("Physical"))
		, Multiplier(1.f)
	{
	}

	FProjectCombatResistance(FName InDamageType, float InMultiplier)
		: DamageType(InDamageType)
		, Multiplier(InMultiplier)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName DamageType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Multiplier;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCombatDamageSpec
{
	GENERATED_BODY()

	FProjectCombatDamageSpec()
		: DamageType(TEXT("Physical"))
		, TargetAttribute(TEXT("Health"))
		, BaseDamage(10.f)
		, FlatBonusDamage(0.f)
		, ArmorPenetration(0.f)
		, SecondaryAttribute(NAME_None)
		, SecondaryDamage(0.f)
		, bIgnoreArmor(false)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName DamageType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName TargetAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float BaseDamage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float FlatBonusDamage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float ArmorPenetration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName SecondaryAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float SecondaryDamage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bIgnoreArmor;

	UPROPERTY(BlueprintReadWrite, Category = "Combat")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Combat")
	TObjectPtr<AActor> DamageCauser = nullptr;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCombatDamageResult
{
	GENERATED_BODY()

	FProjectCombatDamageResult()
		: RequestedDamage(0.f)
		, AppliedDamage(0.f)
		, MitigatedDamage(0.f)
		, DefensiveFlatNegatedDamage(0.f)
		, GuardRecoveryAbsorbedDamage(0.f)
		, PreDamageTargetValue(0.f)
		, TargetMaxValue(0.f)
		, RemainingValue(0.f)
		, SecondaryAppliedDamage(0.f)
		, bKilledTarget(false)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FName DamageType;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FName TargetAttribute;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float RequestedDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float AppliedDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float MitigatedDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float DefensiveFlatNegatedDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float GuardRecoveryAbsorbedDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float PreDamageTargetValue;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float TargetMaxValue;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float RemainingValue;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FName SecondaryAttribute;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float SecondaryAppliedDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bKilledTarget;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIncomingHitContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> DamageCauser = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> OwnerActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> AttachParentActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FName DamageType = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float RequestedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float AppliedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float DefensiveFlatNegatedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float GuardRecoveryAbsorbedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float PainAppliedDelta = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float RemainingHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bKilledTarget = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float WorldTimeSeconds = 0.f;
};

UENUM(BlueprintType)
enum class EProjectCombatHitPolicy : uint8
{
	OncePerDamageWindow UMETA(DisplayName = "Once Per Damage Window"),
	AllowRepeatedHits UMETA(DisplayName = "Allow Repeated Hits")
};
