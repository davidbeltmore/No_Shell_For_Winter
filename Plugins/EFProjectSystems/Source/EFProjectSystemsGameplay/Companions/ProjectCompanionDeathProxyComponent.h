#pragma once

#include "Components/ActorComponent.h"
#include "ProjectCompanionDeathProxyComponent.generated.h"

class AActor;
class UProjectRunCompanionSubsystem;

/** Per-instance bridge that deduplicates ACF and project-combat death signals. */
UCLASS(ClassGroup = (Project), NotBlueprintable, Transient)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCompanionDeathProxyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectCompanionDeathProxyComponent();

	bool InitializeProxy(const FGuid& InStableCompanionId, UProjectRunCompanionSubsystem* InSubsystem);
	virtual void OnUnregister() override;

private:
	bool BindDeathSources();
	void UnbindDeathSources();
	void ForwardDeath(FName SourceDomain);

	UFUNCTION()
	void HandleACFOwnerDeath();

	UFUNCTION()
	void HandleProjectCombatDeath(AActor* SourceActor);

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectRunCompanionSubsystem> CompanionSubsystem;

	UPROPERTY(Transient)
	FGuid StableCompanionId;

	bool bForwardedDeath = false;
};
