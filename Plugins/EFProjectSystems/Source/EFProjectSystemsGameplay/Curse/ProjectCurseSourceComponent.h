#pragma once

#include "Components/ActorComponent.h"
#include "NativeGameplayTags.h"
#include "ProjectCurseSourceComponent.generated.h"

namespace ProjectCurseGameplayTags
{
	EFPROJECTSYSTEMSGAMEPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Source);
}

/**
 * Explicit project-owned marker for enemies and hazards that are canonical
 * Curse sources. When an Ability System is present, the component mirrors its
 * state into the Project.Curse.Source Gameplay Tag.
 */
UCLASS(ClassGroup = (Curse), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCurseSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectCurseSourceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Curse")
	bool IsCurseSourceEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Curse")
	void SetCurseSourceEnabled(bool bEnabled);

private:
	void SynchronizeAbilitySystemTag();
	void RemoveOwnedAbilitySystemTag();

	UPROPERTY(EditAnywhere, Category = "Curse")
	bool bCurseSourceEnabled = true;

	bool bAddedLooseTagToAbilitySystem = false;
};
