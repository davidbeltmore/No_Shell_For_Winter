#include "Companions/ProjectRecruitableCompanionComponent.h"

#include "Actors/ACFCharacter.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Components/ACFCompanionGroupAIComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UProjectRecruitableCompanionComponent::UProjectRecruitableCompanionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

bool UProjectRecruitableCompanionComponent::InitializeRecruitmentHook(
	const FProjectCompanionDefinition& InDefinition,
	UACFCompanionGroupAIComponent* InCompanionGroup,
	FString& OutError)
{
	OutError.Reset();
	FString DefinitionError;
	AACFCharacter* Character = Cast<AACFCharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (bInitialized || !Character || !InCompanionGroup
		|| !World
		|| InDefinition.Lifecycle != EProjectCompanionLifecycle::Recruitable
		|| !InDefinition.IsValid(DefinitionError))
	{
		OutError = DefinitionError.IsEmpty()
			? TEXT("Recruitment hook requires one valid recruitable ACF actor and companion group.")
			: DefinitionError;
		return false;
	}

	Definition = InDefinition;
	CompanionGroup = InCompanionGroup;
	bInitialized = true;
	// ACF 4.3.5 does not broadcast OnAgentsChanged when a live actor is added to
	// the group. Observe its public membership API at a fixed cadence instead.
	World->GetTimerManager().SetTimer(
		RecruitmentPollHandle,
		this,
		&ThisClass::PollACFCompanionGroupMembership,
		0.25f,
		true,
		0.25f);
	FString SynchronizeError;
	if (!SynchronizeFromACFGroup(SynchronizeError))
	{
		OutError = SynchronizeError;
		return false;
	}
	return true;
}

void UProjectRecruitableCompanionComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecruitmentPollHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UProjectRecruitableCompanionComponent::SynchronizeFromACFGroup(FString& OutError)
{
	OutError.Reset();
	AACFCharacter* Character = Cast<AACFCharacter>(GetOwner());
	if (!bInitialized || bHandlingChange || !Character || !CompanionGroup)
	{
		OutError = TEXT("The typed ACF recruitment observer is not initialized.");
		return false;
	}
	if (bRosterAdopted || !CompanionGroup->IsAlreadyInGroup(Character))
	{
		return true;
	}

	TGuardValue<bool> Guard(bHandlingChange, true);
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectRunCompanionSubsystem* Roster = GameInstance
		? GameInstance->GetSubsystem<UProjectRunCompanionSubsystem>()
		: nullptr;
	if (Roster && Roster->RegisterRecruitedCompanion(Definition, Character, true))
	{
		bRosterAdopted = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RecruitmentPollHandle);
		}
		return true;
	}

	// ACF accepted a recruitment that would violate the stable roster/party
	// contract. Remove that actor from the group immediately and fail closed.
	CompanionGroup->RemoveAgentFromGroup(Character);
	OutError = TEXT("ACF accepted the live actor, but the stable V4 roster rejected recruitment.");
	return false;
}

void UProjectRecruitableCompanionComponent::PollACFCompanionGroupMembership()
{
	FString Error;
	if (!SynchronizeFromACFGroup(Error) && !Error.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Calysto V4 recruitment observation failed closed: %s"), *Error);
	}
}
