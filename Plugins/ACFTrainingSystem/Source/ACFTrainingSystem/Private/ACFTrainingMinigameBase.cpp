#include "ACFTrainingMinigameBase.h"

#include "ACFTrainingComponent.h"

void UACFTrainingMinigameBase::InitializeMinigame(UACFTrainingComponent* InTrainingComponent, const FACFTrainingDefinition& InTrainingDefinition)
{
	TrainingComponent = InTrainingComponent;
	TrainingDefinition = InTrainingDefinition;
}

void UACFTrainingMinigameBase::StartMinigame_Implementation()
{
}

void UACFTrainingMinigameBase::CancelMinigame_Implementation()
{
	OnMinigameCancelled.Broadcast(this);
	if (TrainingComponent)
	{
		TrainingComponent->CancelTraining();
	}
}

void UACFTrainingMinigameBase::SubmitResult(bool bSuccess)
{
	OnMinigameResult.Broadcast(this, bSuccess);
	if (TrainingComponent)
	{
		TrainingComponent->CompleteTrainingMinigame(bSuccess);
	}
}

UACFTrainingComponent* UACFTrainingMinigameBase::GetTrainingComponent() const
{
	return TrainingComponent;
}

FACFTrainingDefinition UACFTrainingMinigameBase::GetTrainingDefinition() const
{
	return TrainingDefinition;
}
