#pragma once

#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "AttributeSet.h"
#include "ProjectSurvivalAttributeBridgeComponent.generated.h"

class UAbilitySystemComponent;
class UProjectSurvivalNeedsComponent;

USTRUCT(BlueprintType)
struct FProjectSurvivalBridgeAttributeSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	FName LogicalAttributeName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	FString AttributeSetClassName;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	FString PropertyName;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	float BaselineValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	float CurrentBaseValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	float CurrentValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	float ExpectedEffectiveValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	float AppliedModifierValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	bool bResolved = false;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|Bridge")
	bool bGameplayAttribute = false;
};

struct FProjectSurvivalResolvedAttributeBinding
{
	FName LogicalAttributeName = NAME_None;
	FName ResolvedPropertyName = NAME_None;
	TWeakObjectPtr<UAttributeSet> AttributeSet;
	FGameplayAttribute GameplayAttribute;
	FProperty* ResolvedProperty = nullptr;
	FDelegateHandle ValueChangedHandle;
	float BaselineValue = 0.f;
	float AppliedModifierValue = 0.f;
	bool bResolved = false;
	bool bHasBaselineValue = false;
	bool bGameplayAttributeData = false;
	bool bFloatProperty = false;
};

UCLASS(ClassGroup = (Survival), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalAttributeBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectSurvivalAttributeBridgeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|Bridge")
	bool ForceResolveAndApplyBridge();

	UFUNCTION(BlueprintPure, Category = "Survival|Bridge")
	bool IsBridgeReady() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Bridge")
	FString GetResolvedStatisticsComponentClassName() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Bridge")
	FString GetResolvedAbilitySystemComponentClassName() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Bridge")
	TArray<FProjectSurvivalBridgeAttributeSnapshot> BuildAttributeBindingSnapshots() const;

	UFUNCTION(BlueprintCallable, Category = "Survival|Bridge")
	void SetExternalAttributeMultiplier(FName SourceName, FName LogicalAttributeName, float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Survival|Bridge")
	void ClearExternalAttributeMultiplier(FName SourceName, FName LogicalAttributeName);

	UFUNCTION(BlueprintCallable, Category = "Survival|Bridge")
	void ClearAllExternalAttributeMultipliersForSource(FName SourceName);

protected:
	UFUNCTION()
	void HandlePenaltyMultiplierChanged(float OldPenaltyMultiplier, float NewPenaltyMultiplier);

	void HandleGameplayAttributeValueChanged(const FOnAttributeChangeData& ChangeData);
	void BindToNeedsComponent();
	void ResolveBridge();
	void ClearAttributeDelegates();
	void ResetAppliedPenalty();
	void ApplyCurrentPenalty();
	float GetCurrentPenaltyMultiplier() const;
	float GetCombinedAttributeMultiplier(FName LogicalAttributeName) const;
	bool ResolveAttributeBinding(FProjectSurvivalResolvedAttributeBinding& Binding);
	float ReadBindingBaseValue(const FProjectSurvivalResolvedAttributeBinding& Binding) const;
	float ReadBindingCurrentValue(const FProjectSurvivalResolvedAttributeBinding& Binding) const;
	bool ApplyBindingModifierDelta(FProjectSurvivalResolvedAttributeBinding& Binding, float DeltaValue);
	FProjectSurvivalResolvedAttributeBinding* FindBindingByAttribute(const FGameplayAttribute& Attribute);
	const FProjectSurvivalResolvedAttributeBinding* FindBindingByAttribute(const FGameplayAttribute& Attribute) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Bridge")
	bool bApplyPenaltyToAbilitySystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Bridge")
	bool bLogDiscoveryToOutputLog;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Bridge")
	TArray<FName> BridgedAttributes;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> NeedsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<UActorComponent> StatisticsComponent;

	TArray<FProjectSurvivalResolvedAttributeBinding> ResolvedBindings;
	TMap<FName, TMap<FName, float>> ExternalAttributeMultipliersBySource;
	bool bBridgeReady;
	bool bApplyingPenalty;
};
