#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Companions/ProjectRunCompanionTypes.h"
#include "TimerManager.h"
#include "ProjectRecruitableCompanionComponent.generated.h"

class AACFCharacter;
class UACFCompanionGroupAIComponent;

/**
 * One-shot typed hook from ACF's real group-change event into the persistent
 * run roster. Merely spawning a recruitable NPC never adds it to the roster.
 */
UCLASS(NotBlueprintable, Transient)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRecruitableCompanionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectRecruitableCompanionComponent();

	bool InitializeRecruitmentHook(
		const FProjectCompanionDefinition& InDefinition,
		UACFCompanionGroupAIComponent* InCompanionGroup,
		FString& OutError);

	/** Typed, idempotent observation of ACF's real live-group membership. */
	bool SynchronizeFromACFGroup(FString& OutError);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void PollACFCompanionGroupMembership();

	UPROPERTY(Transient)
	FProjectCompanionDefinition Definition;

	UPROPERTY(Transient)
	TObjectPtr<UACFCompanionGroupAIComponent> CompanionGroup;

	bool bInitialized = false;
	bool bHandlingChange = false;
	bool bRosterAdopted = false;
	FTimerHandle RecruitmentPollHandle;
};
