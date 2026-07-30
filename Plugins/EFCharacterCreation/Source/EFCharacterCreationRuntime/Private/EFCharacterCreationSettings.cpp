#include "EFCharacterCreationSettings.h"

#include "Engine/SkeletalMesh.h"

namespace EFCharacterCreationDefaults
{
	static FCharacterSkeletalMeshOption MakeSkeletalMeshOption(const TCHAR* DisplayName, const TCHAR* AssetPath)
	{
		FCharacterSkeletalMeshOption Option;
		Option.DisplayName = DisplayName;
		Option.SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(AssetPath));
		return Option;
	}

	static FCharacterGenderMeshOption MakeGenderMeshOption(ECharacterCreationGender Gender, const TCHAR* DisplayName, const TCHAR* AssetPath)
	{
		FCharacterGenderMeshOption Option;
		Option.Gender = Gender;
		Option.DisplayName = DisplayName;
		Option.SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(AssetPath));
		return Option;
	}

	static FMorphSliderEntry MakeMorphEntry(const TCHAR* MorphName, const TCHAR* Category, const TCHAR* Section)
	{
		FMorphSliderEntry Entry;
		Entry.MorphName = FName(MorphName);
		Entry.DisplayName = MorphName;
		Entry.Category = FName(Category);
		Entry.Section = Section;
		Entry.Target = ECharacterCustomizationTarget::Body;
		Entry.MinValue = -3.0f;
		Entry.MaxValue = 3.0f;
		Entry.DefaultValue = 0.0f;
		Entry.bAllowRandomize = true;
		return Entry;
	}

	static FCharacterCreationCategoryDefinition MakeCategory(const TCHAR* Category, const TCHAR* DisplayName, std::initializer_list<const TCHAR*> Tokens, int32 SortOrder)
	{
		FCharacterCreationCategoryDefinition Definition;
		Definition.Category = FName(Category);
		Definition.DisplayName = DisplayName;
		Definition.SortOrder = SortOrder;
		for (const TCHAR* Token : Tokens)
		{
			Definition.AutoAssignTokens.Add(Token);
		}

		return Definition;
	}
}

UEFCharacterCreationSettings::UEFCharacterCreationSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("EFCharacterCreation");

	AutoOpenMapNames = {};

	DazPathTokens = {
		TEXT("DazToUnreal"),
		TEXT("Genesis"),
		TEXT("DAZ")
	};

	DazMorphTokens = {
		TEXT("Breasts"),
		TEXT("Glute"),
		TEXT("Navel"),
		TEXT("Areolae"),
		TEXT("Torso"),
		TEXT("Waist"),
		TEXT("Body"),
		TEXT("Genesis"),
		TEXT("PBM"),
		TEXT("PHM")
	};

	BodyMeshComponentHints = {
		TEXT("CharacterMesh0"),
		TEXT("Mesh"),
		TEXT("Body"),
		TEXT("BodyMesh")
	};

	BodyMeshSelectionComponentHints = {
		TEXT("SkeletalMesh"),
		TEXT("Preview"),
		TEXT("CharacterCreationPreview")
	};

	ClothingMeshComponentHints = {
		TEXT("Cloth"),
		TEXT("Clothes"),
		TEXT("Clothing"),
		TEXT("Bra"),
		TEXT("Panties"),
		TEXT("Skirt"),
		TEXT("Top"),
		TEXT("Bottom")
	};

	HairComponentHints = {
		TEXT("Hair")
	};

	HairMeshSearchPaths = {
		TEXT("/Game/QuangPhan/G2_HairCard_01/Meshes/Hairs")
	};

	ExcludedHairNameTokens = {
		TEXT("Dreadlock"),
		TEXT("Hair02"),
		TEXT("Hair04"),
		TEXT("Hair10"),
		TEXT("Hair00")
	};

	BodyMeshOptions = {
		EFCharacterCreationDefaults::MakeSkeletalMeshOption(TEXT("Trans"), TEXT("/Game/DazToUnreal/AmalaforGenesis9/AmalaforGenesis9.AmalaforGenesis9"))
	};

	DefaultCharacterName = TEXT("Player");
	MaxCharacterNameLength = 32;
	DefaultGender = ECharacterCreationGender::Female;
	GenderMeshOptions = {
		EFCharacterCreationDefaults::MakeGenderMeshOption(ECharacterCreationGender::Male, TEXT("Male"), TEXT("/Game/DazToUnreal/Male/Male.Male")),
		EFCharacterCreationDefaults::MakeGenderMeshOption(ECharacterCreationGender::Female, TEXT("Female"), TEXT("/Game/DazToUnreal/Female/Female.Female"))
	};

	SkinColorParameterNames = {
		TEXT("Diffuse Color")
	};

	SkinMaterialHints = {
		TEXT("KatforGenesis9_Body"),
		TEXT("KatforGenesis9_Head"),
		TEXT("KatforGenesis9_Arms"),
		TEXT("KatforGenesis9_Legs"),
		TEXT("KatforGenesis9_Fingernails")
	};

	IrisColorParameterNames = {
		TEXT("Diffuse Color")
	};

	IrisMaterialHints = {
		TEXT("Genesis9Eyes_EyeLeft"),
		TEXT("Genesis9Eyes_EyeRight"),
		TEXT("EyeLeft"),
		TEXT("EyeRight"),
		TEXT("Iris")
	};

	Categories = {
		EFCharacterCreationDefaults::MakeCategory(TEXT("Head"), TEXT("Head"), { TEXT("Face"), TEXT("Eye"), TEXT("Brow"), TEXT("Cheek"), TEXT("Chin"), TEXT("Jaw"), TEXT("Nose"), TEXT("Mouth"), TEXT("Lip"), TEXT("Forehead"), TEXT("Ear"), TEXT("Eyelid") }, 0),
		EFCharacterCreationDefaults::MakeCategory(TEXT("Body"), TEXT("Body"), { TEXT("Body"), TEXT("Breast"), TEXT("Breasts"), TEXT("Waist"), TEXT("Hip"), TEXT("Glute"), TEXT("Arm"), TEXT("Leg"), TEXT("Shoulder"), TEXT("Torso"), TEXT("Stomach"), TEXT("Abdominal"), TEXT("Abdominals"), TEXT("Navel") }, 1),
		EFCharacterCreationDefaults::MakeCategory(TEXT("Skin"), TEXT("Skin"), { TEXT("Skin"), TEXT("Complexion"), TEXT("Tone"), TEXT("Freckle") }, 2)
	};

	MorphEntries = {
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Face Older"), TEXT("Head"), TEXT("Face")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Face Round"), TEXT("Head"), TEXT("Face")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Face Size"), TEXT("Head"), TEXT("Face")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Face Young"), TEXT("Head"), TEXT("Face")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Eye Whole Size"), TEXT("Head"), TEXT("Eyes")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Nose Whole Width"), TEXT("Head"), TEXT("Nose")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Mouth Width"), TEXT("Head"), TEXT("Mouth")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Lip Size Upper"), TEXT("Head"), TEXT("Mouth")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Lip Size Lower"), TEXT("Head"), TEXT("Mouth")),
		EFCharacterCreationDefaults::MakeMorphEntry(TEXT("Brow Whole Height"), TEXT("Head"), TEXT("Brows"))
	};
}

const UEFCharacterCreationSettings* UEFCharacterCreationSettings::Get()
{
	return GetDefault<UEFCharacterCreationSettings>();
}

FName UEFCharacterCreationSettings::GetCategoryName() const
{
	return TEXT("Game");
}
