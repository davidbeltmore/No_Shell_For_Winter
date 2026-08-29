#include "EFClothingMorphDirectorPolicy.h"

#include "EFClothingFitProfile.h"
#include "EFClothingSurfaceBinding.h"

namespace EFClothingMorphDirectorPrivate
{
	constexpr int32 CurrentSchemaVersion = 5;
	const FName CurrentDirectorId(TEXT("EFClothingMorphV4"));

	static FText MakeAuthoringGuide()
	{
		return FText::FromString(TEXT(
			"Add one entry to Clothes for each clothing mesh and body mesh pair. Clothing Name is created automatically when both meshes are assigned and remains editable. "
			"Skin Gap and Surface Volume update only that clothing at runtime. Several clothes can be active together, and an unfinished draft cannot disable ready clothes. "
			"Body Sections to Hide in Gameplay controls visibility only; leave it empty to show every body section. Body Sections Excluded from Fit controls solver geometry only. "
			"Advanced mesh edits are explicit Unreal Engine operations. Fit-data updates never edit the body, its skin weights, or the shared skeleton."));
	}

#if WITH_EDITOR
	static FName MakeUniqueClothingName(
		const FEFClothingGarmentRow& Clothing,
		TSet<FName>& UsedNames)
	{
		const FString ClothingMeshName =
			Clothing.SourceGarment.ToSoftObjectPath().GetAssetName();
		const FString BodyMeshName =
			Clothing.BodySurface.ToSoftObjectPath().GetAssetName();
		if (ClothingMeshName.IsEmpty() || BodyMeshName.IsEmpty())
		{
			return NAME_None;
		}

		const FString BaseName = ClothingMeshName + TEXT("_") + BodyMeshName;
		FName Candidate(*BaseName);
		for (int32 Suffix = 2; UsedNames.Contains(Candidate); ++Suffix)
		{
			Candidate = FName(*FString::Printf(TEXT("%s_%d"), *BaseName, Suffix));
		}
		UsedNames.Add(Candidate);
		return Candidate;
	}
#endif
}

UEFClothingMorphDirectorPolicy::UEFClothingMorphDirectorPolicy()
{
	SchemaVersion = EFClothingMorphDirectorPrivate::CurrentSchemaVersion;
	DirectorId = EFClothingMorphDirectorPrivate::CurrentDirectorId;
	AuthoringGuide = EFClothingMorphDirectorPrivate::MakeAuthoringGuide();
}

bool FEFClothingGarmentRow::ValidateClothingForUse(FString& OutError) const
{
	OutError.Reset();
	if (!bEnabled)
	{
		OutError = TEXT("This clothing is disabled.");
		return false;
	}
	if (!HasCompleteClothingSetup())
	{
		OutError = TEXT("This clothing is still a draft. Assign a Clothing Name, Clothing Mesh, and Body Mesh.");
		return false;
	}
	if (Backend != EEFClothingSurfaceBackend::GeometryFitFallback
		&& Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
	{
		OutError = TEXT("This clothing uses an unsupported fit method.");
		return false;
	}
	if (!FMath::IsFinite(MinimumClearanceMultiplier)
		|| MinimumClearanceMultiplier < EFClothingMorphV25::ClearanceTierMin
		|| MinimumClearanceMultiplier > EFClothingMorphV25::ClearanceTierMax)
	{
		OutError = TEXT("This clothing has an invalid internal clearance value.");
		return false;
	}
	const bool bFabricClearanceValid = EFClothingMorphV26::IsAutomaticCentimeterValue(
		FabricClearanceCm)
		|| (FMath::IsFinite(FabricClearanceCm)
			&& FabricClearanceCm >= 0.0f
			&& FabricClearanceCm <= 5.0f);
	const bool bMaximumCorrectionValid = EFClothingMorphV26::IsAutomaticCentimeterValue(
		MaximumCorrectionCm)
		|| (FMath::IsFinite(MaximumCorrectionCm)
			&& MaximumCorrectionCm > 0.0f
			&& MaximumCorrectionCm <= 10.0f);
	if (!bFabricClearanceValid || !bMaximumCorrectionValid)
	{
		OutError = TEXT("This clothing has invalid internal fit limits.");
		return false;
	}
	return true;
}

bool UEFClothingMorphDirectorPolicy::ValidateIdentity(FString& OutError) const
{
	OutError.Reset();
	if (SchemaVersion != EFClothingMorphDirectorPrivate::CurrentSchemaVersion
		|| DirectorId != EFClothingMorphDirectorPrivate::CurrentDirectorId)
	{
		OutError = TEXT("EF Clothing Morph Director V4 identity or schema is invalid.");
		return false;
	}
	return true;
}

bool UEFClothingMorphDirectorPolicy::IsIdentityValid() const
{
	FString ValidationError;
	return ValidateIdentity(ValidationError);
}

FString UEFClothingMorphDirectorPolicy::GetIdentityValidationError() const
{
	FString ValidationError;
	ValidateIdentity(ValidationError);
	return ValidationError;
}

bool UEFClothingMorphDirectorPolicy::ValidatePolicy(FString& OutError) const
{
	OutError.Reset();
	if (!ValidateIdentity(OutError))
	{
		return false;
	}
	// Runtime controls are deliberately not policy-invalidating authoring. Every
	// consumer clamps finite values to the safe budget and treats NaN/Inf as zero.
	if (Garments.IsEmpty())
	{
		OutError = TEXT("EF Clothing Morph Director V4 has no clothing entries.");
		return false;
	}

	TSet<FName> ClothingNames;
	TSet<FString> SourceBodyPairs;
	int32 ReadyClothingCount = 0;
	for (const FEFClothingGarmentRow& Clothing : Garments)
	{
		// V4 isolates authoring mistakes to their own entry. Disabled or incomplete
		// rows are drafts; complete rows with invalid internal values are skipped by
		// ordinary runtime validation and remain available for compiler diagnostics.
		if (Clothing.IsClothingDraft())
		{
			continue;
		}
		FString ClothingError;
		if (!Clothing.ValidateClothingForUse(ClothingError))
		{
			continue;
		}
		if (ClothingNames.Contains(Clothing.GarmentId))
		{
			continue;
		}
		const FString PairKey = Clothing.SourceGarment.ToSoftObjectPath().ToString()
			+ TEXT("|") + Clothing.BodySurface.ToSoftObjectPath().ToString();
		if (SourceBodyPairs.Contains(PairKey))
		{
			continue;
		}
		ClothingNames.Add(Clothing.GarmentId);
		SourceBodyPairs.Add(PairKey);
		++ReadyClothingCount;
	}
	if (ReadyClothingCount == 0)
	{
		OutError = TEXT("EF Clothing Morph Director V4 has no ready clothing entries.");
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
		EFClothingMorphV4::MaximumRuntimeClearanceCm);
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

#if WITH_EDITOR
bool UEFClothingMorphDirectorPolicy::EnsureMissingClothingNames()
{
	using namespace EFClothingMorphDirectorPrivate;

	TSet<FName> UsedNames;
	for (const FEFClothingGarmentRow& Clothing : Garments)
	{
		if (!Clothing.GarmentId.IsNone())
		{
			UsedNames.Add(Clothing.GarmentId);
		}
	}

	bool bChanged = false;
	for (FEFClothingGarmentRow& Clothing : Garments)
	{
		if (!Clothing.GarmentId.IsNone()
			|| Clothing.SourceGarment.IsNull()
			|| Clothing.BodySurface.IsNull())
		{
			continue;
		}

		const FName GeneratedName = MakeUniqueClothingName(Clothing, UsedNames);
		if (!GeneratedName.IsNone())
		{
			Clothing.GarmentId = GeneratedName;
			bChanged = true;
		}
	}
	return bChanged;
}

void UEFClothingMorphDirectorPolicy::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	const bool bGeneratedName = EnsureMissingClothingNames();
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (bGeneratedName)
	{
		MarkPackageDirty();
	}
}
#endif
