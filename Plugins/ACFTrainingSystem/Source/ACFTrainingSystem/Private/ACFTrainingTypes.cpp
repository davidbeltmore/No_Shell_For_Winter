#include "ACFTrainingTypes.h"

FACFTrainingScalarGate::FACFTrainingScalarGate()
	: ResourceName(NAME_None)
	, bUseMinimumValue(false)
	, MinimumValue(0.0f)
	, bUseMaximumValue(false)
	, MaximumValue(0.0f)
{
}

FACFTrainingFutureRequirements::FACFTrainingFutureRequirements()
	: bEnableScalarRequirements(false)
	, bEnableFailureCooldown(false)
	, FailureCooldownSeconds(0.0f)
	, bEnableInventoryTagRequirements(false)
	, bEnableNearbyActorRequirements(false)
	, NearbySearchRadius(300.0f)
{
}

FACFTrainingDefinition::FACFTrainingDefinition()
	: TrainingId(NAME_None)
	, SuccessReward(1.0f)
	, MinigameDifficulty(50)
{
}

FACFTrainingProgressEntry::FACFTrainingProgressEntry()
	: TrainingId(NAME_None)
	, Progress(0.0f)
{
}

FACFTrainingAttributeRewardEntry::FACFTrainingAttributeRewardEntry()
	: AccumulatedReward(0.0f)
{
}
