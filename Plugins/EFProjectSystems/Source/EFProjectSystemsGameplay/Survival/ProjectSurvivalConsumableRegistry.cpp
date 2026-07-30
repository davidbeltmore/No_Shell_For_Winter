#include "Survival/ProjectSurvivalConsumableRegistry.h"

bool UProjectSurvivalConsumableRegistry::FindProfileByRegistryId(FName RegistryId, FProjectSurvivalConsumableProfile& OutProfile) const
{
	const FProjectSurvivalConsumableRegistryEntry* Entry = Entries.FindByPredicate(
		[RegistryId](const FProjectSurvivalConsumableRegistryEntry& Candidate)
		{
			return !Candidate.RegistryId.IsNone() && Candidate.RegistryId == RegistryId;
		});

	if (!Entry)
	{
		return false;
	}

	OutProfile = Entry->Profile;
	return true;
}
