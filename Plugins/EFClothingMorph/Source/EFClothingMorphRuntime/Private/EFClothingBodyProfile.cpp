#include "EFClothingBodyProfile.h"

namespace EFClothingBodyProfilePrivate
{
	bool ValidateUniqueName(const FName Value, TSet<FName>& UsedValues)
	{
		if (Value.IsNone() || UsedValues.Contains(Value))
		{
			return false;
		}
		UsedValues.Add(Value);
		return true;
	}
}

bool UEFClothingBodyProfile::ValidateProfile(FString& OutError) const
{
	OutError.Reset();
	if (SchemaVersion != EFClothingMorphV5::BodyProfileSchemaVersion)
	{
		OutError = TEXT("The body profile schema is not supported by EF Clothing Morph V5.");
		return false;
	}
	if (BodyProfileId.IsNone())
	{
		OutError = TEXT("Body Profile Id is required.");
		return false;
	}
	if (BodySurface.IsNull())
	{
		OutError = TEXT("Body Surface is required.");
		return false;
	}

	TSet<FName> UniqueSkinSlots;
	for (const FName Slot : SkinMaterialSlots)
	{
		if (!EFClothingBodyProfilePrivate::ValidateUniqueName(Slot, UniqueSkinSlots))
		{
			OutError = TEXT("Skin Material Slots contains an empty or duplicate slot.");
			return false;
		}
	}

	TSet<FName> UniqueRegionIds;
	for (const FEFClothingBodyRegionDefinition& Region : Regions)
	{
		if (!EFClothingBodyProfilePrivate::ValidateUniqueName(Region.RegionId, UniqueRegionIds))
		{
			OutError = TEXT("Body Regions contains an empty or duplicate Region Id.");
			return false;
		}
		TSet<FName> UniqueRegionSlots;
		for (const FName Slot : Region.SkinMaterialSlots)
		{
			if (!EFClothingBodyProfilePrivate::ValidateUniqueName(Slot, UniqueRegionSlots))
			{
				OutError = FString::Printf(
					TEXT("Body region '%s' contains an empty or duplicate skin slot."),
					*Region.RegionId.ToString());
				return false;
			}
			if (!UniqueSkinSlots.IsEmpty() && !UniqueSkinSlots.Contains(Slot))
			{
				OutError = FString::Printf(
					TEXT("Body region '%s' references skin slot '%s', which is not declared by the profile."),
					*Region.RegionId.ToString(),
					*Slot.ToString());
				return false;
			}
		}
	}

	TSet<FName> UniqueMorphGroupIds;
	for (const FEFClothingMorphGroupDefinition& MorphGroup : MorphGroups)
	{
		if (!EFClothingBodyProfilePrivate::ValidateUniqueName(
			MorphGroup.MorphGroupId,
			UniqueMorphGroupIds))
		{
			OutError = TEXT("Morph Groups contains an empty or duplicate Morph Group Id.");
			return false;
		}
		if (MorphGroup.ExactMorphNames.IsEmpty() && MorphGroup.MorphNamePrefixes.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Morph group '%s' has no exact names or prefixes."),
				*MorphGroup.MorphGroupId.ToString());
			return false;
		}
		if (!FMath::IsFinite(MorphGroup.MinimumValue)
			|| !FMath::IsFinite(MorphGroup.MaximumValue)
			|| MorphGroup.MinimumValue > MorphGroup.MaximumValue)
		{
			OutError = FString::Printf(
				TEXT("Morph group '%s' has an invalid value range."),
				*MorphGroup.MorphGroupId.ToString());
			return false;
		}
		for (const FName RegionId : MorphGroup.AffectedRegionIds)
		{
			if (RegionId.IsNone() || !UniqueRegionIds.Contains(RegionId))
			{
				OutError = FString::Printf(
					TEXT("Morph group '%s' references an unknown body region."),
					*MorphGroup.MorphGroupId.ToString());
				return false;
			}
		}
	}
	return true;
}

bool UEFClothingBodyProfile::IsProfileValid() const
{
	FString Error;
	return ValidateProfile(Error);
}

FString UEFClothingBodyProfile::GetProfileValidationError() const
{
	FString Error;
	ValidateProfile(Error);
	return Error;
}

const FEFClothingBodyRegionDefinition* UEFClothingBodyProfile::FindRegion(const FName RegionId) const
{
	if (RegionId.IsNone())
	{
		return nullptr;
	}
	return Regions.FindByPredicate([RegionId](const FEFClothingBodyRegionDefinition& Region)
	{
		return Region.RegionId == RegionId;
	});
}
