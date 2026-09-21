#include "EFClothingDefinition.h"

bool UEFClothingDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	if (SchemaVersion != EFClothingMorphV5::ClothingDefinitionSchemaVersion)
	{
		OutError = TEXT("The clothing definition schema is not supported by EF Clothing Morph V5.");
		return false;
	}
	if (GarmentId.IsNone())
	{
		OutError = TEXT("Garment Id is required.");
		return false;
	}
	if (SourceGarment.IsNull())
	{
		OutError = TEXT("Source Garment is required.");
		return false;
	}
	if (BodyProfile.IsNull())
	{
		OutError = TEXT("Body Profile is required.");
		return false;
	}
	if (!FMath::IsFinite(AdditionalClearanceCm)
		|| AdditionalClearanceCm < 0.0f
		|| AdditionalClearanceCm > EFClothingMorphV5::MaximumClearanceCm)
	{
		OutError = TEXT("Additional Clearance is outside the supported V5 range.");
		return false;
	}
	if (!FMath::IsFinite(ShellThicknessCm)
		|| ShellThicknessCm < 0.0f
		|| ShellThicknessCm > EFClothingMorphV5::MaximumShellThicknessCm)
	{
		OutError = TEXT("Shell Thickness is outside the supported V5 range.");
		return false;
	}
	if (!LowerBodyMorphGuard.Validate(OutError))
	{
		return false;
	}

	TSet<FName> OccupiedRegionIds;
	for (const FEFClothingRegionOccupancyRule& Occupancy : LayerRule.OccupiedRegions)
	{
		if (Occupancy.RegionId.IsNone() || OccupiedRegionIds.Contains(Occupancy.RegionId))
		{
			OutError = TEXT("Occupied Regions contains an empty or duplicate Region Id.");
			return false;
		}
		OccupiedRegionIds.Add(Occupancy.RegionId);
		if (!FMath::IsFinite(Occupancy.OccupiedThicknessCm)
			|| Occupancy.OccupiedThicknessCm < 0.0f
			|| !FMath::IsFinite(Occupancy.MinimumOuterGapCm)
			|| Occupancy.MinimumOuterGapCm < 0.0f)
		{
			OutError = FString::Printf(
				TEXT("Occupied region '%s' has an invalid thickness or gap."),
				*Occupancy.RegionId.ToString());
			return false;
		}
	}

	TSet<FName> CoveredRegionIds;
	for (const FName RegionId : CoveredBodyRegionIds)
	{
		if (RegionId.IsNone() || CoveredRegionIds.Contains(RegionId))
		{
			OutError = TEXT("Covered Body Regions contains an empty or duplicate Region Id.");
			return false;
		}
		CoveredRegionIds.Add(RegionId);
	}

	for (const FEFClothingMaterialPolicyRule& Rule : MaterialPolicy.Overrides)
	{
		if (Rule.bEnabled && !Rule.HasFilter())
		{
			OutError = TEXT("Every enabled material override needs a material slot or exact material filter.");
			return false;
		}
	}

	const bool bHasBindingId = !BindingStableId.IsNone();
	const bool bHasBindingVersion = BindingVersion > 0;
	if (bHasBindingId != bHasBindingVersion || BindingVersion < 0)
	{
		OutError = TEXT("Compiled Binding must provide both a Stable Id and a positive Version, or neither.");
		return false;
	}
	return true;
}

bool UEFClothingDefinition::IsDefinitionValid() const
{
	FString Error;
	return ValidateDefinition(Error);
}

FString UEFClothingDefinition::GetDefinitionValidationError() const
{
	FString Error;
	ValidateDefinition(Error);
	return Error;
}
