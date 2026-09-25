#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "ProjectDoctrineAttributeSet.generated.h"

// Opt-in set for the unified progression pilot; never auto-attached to the player.
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDoctrineAttributeSet : public UAttributeSet
{
 GENERATED_BODY()
public:
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Willpower, Category="Progression")
 FGameplayAttributeData Willpower;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Willpower)
 UFUNCTION() void OnRep_Willpower(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Willpower, OldValue); }
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Offensive, Category="Progression")
 FGameplayAttributeData Offensive;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Offensive)
 UFUNCTION() void OnRep_Offensive(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Offensive, OldValue); }
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Defensive, Category="Progression")
 FGameplayAttributeData Defensive;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Defensive)
 UFUNCTION() void OnRep_Defensive(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Defensive, OldValue); }
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Faith, Category="Progression")
 FGameplayAttributeData Faith;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Faith)
 UFUNCTION() void OnRep_Faith(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Faith, OldValue); }
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Cunning, Category="Progression")
 FGameplayAttributeData Cunning;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Cunning)
 UFUNCTION() void OnRep_Cunning(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Cunning, OldValue); }
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Celerity, Category="Progression")
 FGameplayAttributeData Celerity;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Celerity)
 UFUNCTION() void OnRep_Celerity(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Celerity, OldValue); }
 UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Charisma, Category="Progression")
 FGameplayAttributeData Charisma;
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UProjectDoctrineAttributeSet, Charisma)
 UFUNCTION() void OnRep_Charisma(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UProjectDoctrineAttributeSet, Charisma, OldValue); }
};
