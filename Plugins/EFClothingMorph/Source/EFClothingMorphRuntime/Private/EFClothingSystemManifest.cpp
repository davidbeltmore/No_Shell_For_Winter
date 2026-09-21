#include "EFClothingSystemManifest.h"

namespace EFClothingSystemManifestPrivate
{
	constexpr int32 CurrentSchemaVersion = 1;
	const FName CurrentSystemId(TEXT("EFClothingMorphV5"));

	template <typename ObjectType>
	bool ValidateUniquePaths(
		const TArray<TSoftObjectPtr<ObjectType>>& Objects,
		const TCHAR* CollectionName,
		FString& OutError)
	{
		TSet<FSoftObjectPath> Paths;
		for (int32 Index = 0; Index < Objects.Num(); ++Index)
		{
			const FSoftObjectPath Path = Objects[Index].ToSoftObjectPath();
			if (Path.IsNull())
			{
				OutError = FString::Printf(
					TEXT("%s[%d] has no asset assigned."),
					CollectionName,
					Index);
				return false;
			}
			if (Paths.Contains(Path))
			{
				OutError = FString::Printf(
					TEXT("%s contains duplicate asset '%s'."),
					CollectionName,
					*Path.ToString());
				return false;
			}
			Paths.Add(Path);
		}
		return true;
	}
}

UEFClothingSystemManifest::UEFClothingSystemManifest()
{
	SchemaVersion = EFClothingSystemManifestPrivate::CurrentSchemaVersion;
	SystemId = EFClothingSystemManifestPrivate::CurrentSystemId;
}

bool UEFClothingSystemManifest::Validate(FString& OutError) const
{
	using namespace EFClothingSystemManifestPrivate;
	OutError.Reset();
	if (SchemaVersion != CurrentSchemaVersion || SystemId != CurrentSystemId)
	{
		OutError = TEXT("The EF Clothing Morph V5 manifest identity or schema is invalid.");
		return false;
	}
	if (BindingRegistry.IsNull())
	{
		OutError = TEXT("The EF Clothing Morph V5 manifest has no streamable binding registry.");
		return false;
	}
	if (Garments.IsEmpty())
	{
		OutError = TEXT("The EF Clothing Morph V5 manifest has no garment definitions.");
		return false;
	}
	return ValidateUniquePaths(BodyProfiles, TEXT("BodyProfiles"), OutError)
		&& ValidateUniquePaths(Garments, TEXT("Garments"), OutError);
}

bool UEFClothingSystemManifest::IsValidManifest() const
{
	FString Error;
	return Validate(Error);
}

FString UEFClothingSystemManifest::GetValidationError() const
{
	FString Error;
	Validate(Error);
	return Error;
}
