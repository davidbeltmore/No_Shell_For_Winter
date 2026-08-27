#include "EFClothingMorphDirectorPolicy.h"

#include "EFClothingGarmentCatalog.h"
#include "Engine/DataTable.h"

UEFClothingMorphDirectorPolicy::UEFClothingMorphDirectorPolicy()
	: CompileCatalog(FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments.DT_EFClothingGarments")))
	, RuntimeTuningCatalog(FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarmentTuning.DT_EFClothingGarmentTuning")))
{
	AuthoringGuide = FText::FromString(TEXT(
		"Use DT_EFClothingGarments to register a garment (its row name is the stable index) and run Compile All Garments after structural changes. "
		"Use DT_EFClothingGarmentTuning to change Extra Surface Offset (cm) without touching any Skeletal Mesh or recompiling. "
		"Never use Skeletal Mesh Editor > Deform > Offset on a certified source; duplicate/recompile instead."));
}

bool UEFClothingMorphDirectorPolicy::ValidatePolicy(FString& OutError) const
{
	OutError.Reset();
	if (SchemaVersion != 1 || DirectorId != TEXT("EFClothingMorphV2"))
	{
		OutError = TEXT("EF Clothing Morph Director identity/schema is invalid.");
		return false;
	}
	if (!FMath::IsFinite(GlobalAdditionalClearanceCm)
		|| !FMath::IsFinite(MaximumAdditionalClearanceCm)
		|| GlobalAdditionalClearanceCm < 0.0f
		|| MaximumAdditionalClearanceCm < 0.0f
		|| GlobalAdditionalClearanceCm > MaximumAdditionalClearanceCm
		|| MaximumAdditionalClearanceCm > 0.35f)
	{
		OutError = TEXT("EF Clothing Morph Director runtime clearance budget is invalid.");
		return false;
	}

	// The runtime preloads these paths asynchronously, but the policy also has
	// to validate correctly in commandlets and editor tools. Resolve its own
	// soft references so the result never depends on asset load order.
	const UDataTable* CompileTable = CompileCatalog.LoadSynchronous();
	const UDataTable* TuningTable = RuntimeTuningCatalog.LoadSynchronous();
	if (!IsValid(CompileTable)
		|| CompileTable->GetRowStruct() != FEFClothingGarmentRow::StaticStruct())
	{
		OutError = TEXT("EF Clothing Morph Director compile catalog is missing or has the wrong row struct.");
		return false;
	}
	if (!IsValid(TuningTable)
		|| TuningTable->GetRowStruct() != FEFClothingGarmentTuningRow::StaticStruct())
	{
		OutError = TEXT("EF Clothing Morph Director runtime tuning catalog is missing or has the wrong row struct.");
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
	const float SafeMaximum = FMath::Clamp(
		FMath::IsFinite(MaximumAdditionalClearanceCm) ? MaximumAdditionalClearanceCm : 0.0f,
		0.0f,
		0.35f);
	return FMath::Clamp(
		FMath::IsFinite(RequestedClearanceCm) ? RequestedClearanceCm : 0.0f,
		0.0f,
		SafeMaximum);
}
