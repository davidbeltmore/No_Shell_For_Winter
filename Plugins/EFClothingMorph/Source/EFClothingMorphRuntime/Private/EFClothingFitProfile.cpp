#include "EFClothingFitProfile.h"

#include "Engine/SkeletalMesh.h"

bool UEFClothingFitProfile::MatchesSource(const USkeletalMesh* Mesh) const
{
	if (!IsValid(Mesh) || SourceGarment.IsNull())
	{
		return false;
	}

	return SourceGarment.ToSoftObjectPath() == FSoftObjectPath(Mesh);
}

const UEFClothingFitProfile* UEFClothingFitRegistry::FindProfileForSource(const USkeletalMesh* SourceMesh) const
{
	for (const UEFClothingFitProfile* Profile : Profiles)
	{
		if (IsValid(Profile) && Profile->MatchesSource(SourceMesh))
		{
			return Profile;
		}
	}

	return nullptr;
}
