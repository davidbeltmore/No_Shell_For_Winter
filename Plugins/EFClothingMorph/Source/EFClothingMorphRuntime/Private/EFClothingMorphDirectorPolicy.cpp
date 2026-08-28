#include "EFClothingMorphDirectorPolicy.h"

#include "EFClothingFitProfile.h"
#include "EFClothingSurfaceBinding.h"

namespace EFClothingMorphDirectorPrivate
{
	constexpr int32 CurrentSchemaVersion = 4;
	const FName CurrentDirectorId(TEXT("EFClothingMorphV3"));

	static FText MakeAuthoringGuide()
	{
		return FText::FromString(TEXT(
			"Add one entry to Garments for each garment/body pair. Editable Garment Mesh is always the authoritative source and may be changed with Unreal Engine's native Skeletal Mesh tools. "
			"Skin Clearance and Surface Inflate are immediate, non-destructive runtime controls owned by that garment entry. Native UE Offset values do nothing until Apply Native Offset to Editable Mesh is pressed. "
			"Refresh Binding rebuilds only project-owned runtime data; it never edits the body, its weights, or the shared skeleton."));
	}
}

UEFClothingMorphDirectorPolicy::UEFClothingMorphDirectorPolicy()
{
	SchemaVersion = EFClothingMorphDirectorPrivate::CurrentSchemaVersion;
	DirectorId = EFClothingMorphDirectorPrivate::CurrentDirectorId;
	AuthoringGuide = EFClothingMorphDirectorPrivate::MakeAuthoringGuide();
}

bool UEFClothingMorphDirectorPolicy::ValidatePolicy(FString& OutError) const
{
	OutError.Reset();
	if (SchemaVersion != EFClothingMorphDirectorPrivate::CurrentSchemaVersion
		|| DirectorId != EFClothingMorphDirectorPrivate::CurrentDirectorId)
	{
		OutError = TEXT("EF Clothing Morph Director identity/schema is invalid.");
		return false;
	}
	// Runtime offsets are deliberately not policy-invalidating authoring. Every
	// consumer clamps finite values to the certified budget and treats NaN/Inf as
	// zero, so a harmless tuning edit can never hide the complete garment catalog.
	if (Garments.IsEmpty())
	{
		OutError = TEXT("EF Clothing Morph Director has no garment entries.");
		return false;
	}

	TSet<FName> GarmentIds;
	TSet<FString> SourceBodyPairs;
	int32 EnabledGarmentCount = 0;
	for (const FEFClothingGarmentRow& Garment : Garments)
	{
		const bool bHasSource = !Garment.SourceGarment.IsNull();
		const bool bHasBody = !Garment.BodySurface.IsNull();
		if (Garment.IsDisabledEmptyPlaceholder())
		{
			continue;
		}
		if (Garment.GarmentId.IsNone())
		{
			OutError = TEXT("EF Clothing Morph Director contains a garment with an empty Garment Id.");
			return false;
		}
		if (GarmentIds.Contains(Garment.GarmentId))
		{
			OutError = FString::Printf(
				TEXT("EF Clothing Morph Director contains duplicate Garment Id %s."),
				*Garment.GarmentId.ToString());
			return false;
		}
		GarmentIds.Add(Garment.GarmentId);

		if (Garment.bEnabled && bHasSource && bHasBody)
		{
			const FString PairKey = Garment.SourceGarment.ToSoftObjectPath().ToString()
				+ TEXT("|") + Garment.BodySurface.ToSoftObjectPath().ToString();
			if (SourceBodyPairs.Contains(PairKey))
			{
				OutError = FString::Printf(
					TEXT("Garment %s duplicates an existing Source Garment / Body Surface pair."),
					*Garment.GarmentId.ToString());
				return false;
			}
			SourceBodyPairs.Add(PairKey);
		}

		if (!Garment.bEnabled)
		{
			continue;
		}
		++EnabledGarmentCount;
		if (!bHasSource || !bHasBody)
		{
			OutError = FString::Printf(
				TEXT("Enabled garment %s requires both Source Garment and Body Surface."),
				*Garment.GarmentId.ToString());
			return false;
		}
		if (Garment.Backend != EEFClothingSurfaceBackend::GeometryFitFallback
			&& Garment.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
		{
			OutError = FString::Printf(
				TEXT("Enabled garment %s selects an unsupported backend."),
				*Garment.GarmentId.ToString());
			return false;
		}
		if (!FMath::IsFinite(Garment.MinimumClearanceMultiplier)
			|| Garment.MinimumClearanceMultiplier < EFClothingMorphV25::ClearanceTierMin
			|| Garment.MinimumClearanceMultiplier > EFClothingMorphV25::ClearanceTierMax)
		{
			OutError = FString::Printf(
				TEXT("Garment %s has an invalid certified clearance multiplier."),
				*Garment.GarmentId.ToString());
			return false;
		}
		const bool bFabricClearanceValid = EFClothingMorphV26::IsAutomaticCentimeterValue(
			Garment.FabricClearanceCm)
			|| (FMath::IsFinite(Garment.FabricClearanceCm)
				&& Garment.FabricClearanceCm >= 0.0f
				&& Garment.FabricClearanceCm <= 5.0f);
		const bool bMaximumCorrectionValid = EFClothingMorphV26::IsAutomaticCentimeterValue(
			Garment.MaximumCorrectionCm)
			|| (FMath::IsFinite(Garment.MaximumCorrectionCm)
				&& Garment.MaximumCorrectionCm > 0.0f
				&& Garment.MaximumCorrectionCm <= 10.0f);
		if (!bFabricClearanceValid || !bMaximumCorrectionValid)
		{
			OutError = FString::Printf(
				TEXT("Garment %s has an invalid fabric clearance or maximum correction."),
				*Garment.GarmentId.ToString());
			return false;
		}
	}
	if (EnabledGarmentCount == 0)
	{
		OutError = TEXT("EF Clothing Morph Director has no enabled garment definitions.");
		return false;
	}
	return true;
}

bool UEFClothingMorphDirectorPolicy::IsPolicyValid() const
{
	FString ValidationError;
	return ValidatePolicy(ValidationError);
}

FString UEFClothingMorphDirectorPolicy::GetPolicyValidationError() const
{
	FString ValidationError;
	ValidatePolicy(ValidationError);
	return ValidationError;
}

float UEFClothingMorphDirectorPolicy::ClampAdditionalClearanceCm(const float RequestedClearanceCm) const
{
	return FMath::Clamp(
		FMath::IsFinite(RequestedClearanceCm) ? RequestedClearanceCm : 0.0f,
		0.0f,
		EFClothingMorphV3::MaximumRuntimeClearanceCm);
}

const FEFClothingGarmentRow* UEFClothingMorphDirectorPolicy::FindGarmentById(const FName GarmentId) const
{
	if (GarmentId.IsNone())
	{
		return nullptr;
	}
	return Garments.FindByPredicate([GarmentId](const FEFClothingGarmentRow& Garment)
	{
		return Garment.GarmentId == GarmentId;
	});
}

bool UEFClothingMorphDirectorPolicy::GetGarmentById(
	const FName GarmentId,
	FEFClothingGarmentRow& OutGarment) const
{
	if (const FEFClothingGarmentRow* Garment = FindGarmentById(GarmentId))
	{
		OutGarment = *Garment;
		return true;
	}
	OutGarment = FEFClothingGarmentRow();
	return false;
}
