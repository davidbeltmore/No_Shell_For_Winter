#pragma once
#include "CoreMinimal.h"
#include "Items/ACFConsumable.h"
#include "Survival/ProjectInnerStateComponent.h"
#include "Survival/ProjectSurvivalNeedsTypes.h"
#include "ProjectInnerStateConsumable.generated.h"

/** Small, data-authored ACF inventory item. One use is one distinct exposure. */
UCLASS(Blueprintable, BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateConsumable : public UACFConsumable
{
    GENERATED_BODY()
public:
    UProjectInnerStateConsumable();
    virtual bool CanBeUsed_Implementation(const APawn* Pawn) const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State") FProjectSurvivalConsumableProfile SurvivalProfile;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State") TArray<FProjectInnerStateInfluence> Influences;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="0")) double PoisonPoints = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State") EProjectPoisonSpeed PoisonSpeed = EProjectPoisonSpeed::Normal;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State") FName Toxin;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="0",ClampMax="1")) double HealthRestoreFraction = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="0")) double SicknessTreatmentHours = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State") bool bAntidote = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Inner State") bool bPartialWash = false;
protected:
    virtual void OnItemUsed_Implementation(APawn* Target) override;
};
