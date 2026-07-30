#include "EFClothingMorphComponent.h"

#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFCharacterCustomizationComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "UObject/ScriptDelegates.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace EFClothingMorphPrivate
{
	static constexpr TCHAR ACFUEquipmentComponentClassPath[] = TEXT("/Script/InventorySystem.ACFEquipmentComponent");
	static constexpr TCHAR ACFUArmorSlotComponentClassPath[] = TEXT("/Script/InventorySystem.ACFArmorSlotComponent");

	static FString NormalizeToken(const FString& Value)
	{
		return Value.ToLower();
	}

	static UClass* ResolveClassByPath(const TCHAR* ClassPath)
	{
		if (ClassPath == nullptr || *ClassPath == TEXT('\0'))
		{
			return nullptr;
		}

		if (UClass* ExistingClass = FindObject<UClass>(nullptr, ClassPath))
		{
			return ExistingClass;
		}

		return LoadObject<UClass>(nullptr, ClassPath);
	}

	static bool InvokeNoArgFunction(UObject* TargetObject, const TCHAR* FunctionName)
	{
		if (!IsValid(TargetObject) || FunctionName == nullptr)
		{
			return false;
		}

		if (UFunction* Function = TargetObject->FindFunction(FName(FunctionName)))
		{
			TargetObject->ProcessEvent(Function, nullptr);
			return true;
		}

		return false;
	}

	static bool MatchesAnyHint(const FString& SourceString, const TArray<FString>& Hints)
	{
		const FString NormalizedSource = NormalizeToken(SourceString);
		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && NormalizedSource.Contains(NormalizeToken(Hint)))
			{
				return true;
			}
		}

		return false;
	}

	static const TArray<FString>& GetBodyMeshHints()
	{
		static const TArray<FString> Hints = {
			TEXT("CharacterMesh0"),
			TEXT("Mesh"),
			TEXT("Body"),
			TEXT("BodyMesh"),
			TEXT("SkeletalMesh")
		};
		return Hints;
	}

	static const TArray<FString>& GetClothingMeshHints()
	{
		static const TArray<FString> Hints = {
			TEXT("Cloth"),
			TEXT("Clothes"),
			TEXT("Clothing"),
			TEXT("Bra"),
			TEXT("Pant"),
			TEXT("Panties"),
			TEXT("Underwear"),
			TEXT("Skirt"),
			TEXT("Top"),
			TEXT("Bottom"),
			TEXT("Dress"),
			TEXT("Shirt"),
			TEXT("Short"),
			TEXT("Shoe"),
			TEXT("Boot"),
			TEXT("Glove")
		};
		return Hints;
	}

	static const TArray<FString>& GetHairMeshHints()
	{
		static const TArray<FString> Hints = {
			TEXT("Hair"),
			TEXT("Beard"),
			TEXT("Brow"),
			TEXT("Lash")
		};
		return Hints;
	}

	static const TArray<FString>& GetDazPathHints()
	{
		static const TArray<FString> Hints = {
			TEXT("DazToUnreal"),
			TEXT("Genesis"),
			TEXT("DAZ")
		};
		return Hints;
	}

	static const TArray<FString>& GetPieceHints(const EEFClothingPiece Piece)
	{
		static const TArray<FString> HeadHints = { TEXT("head"), TEXT("hood"), TEXT("mask"), TEXT("helmet") };
		static const TArray<FString> ChestHints = { TEXT("bra"), TEXT("chest"), TEXT("top"), TEXT("shirt"), TEXT("torso"), TEXT("jacket"), TEXT("coat"), TEXT("corset") };
		static const TArray<FString> PantsHints = { TEXT("pant"), TEXT("pants"), TEXT("short"), TEXT("skirt"), TEXT("legging"), TEXT("bottom"), TEXT("jean"), TEXT("trouser") };
		static const TArray<FString> GlovesHints = { TEXT("glove"), TEXT("gauntlet"), TEXT("hand") };
		static const TArray<FString> BootsHints = { TEXT("boot"), TEXT("shoe"), TEXT("sandal"), TEXT("feet"), TEXT("foot") };
		static const TArray<FString> ArmsHints = { TEXT("arm"), TEXT("sleeve"), TEXT("wrist"), TEXT("bracer") };
		static const TArray<FString> LoinHints = { TEXT("loin"), TEXT("groin"), TEXT("panties"), TEXT("underwear"), TEXT("thong"), TEXT("bikini") };
		static const TArray<FString> EmptyHints;

		switch (Piece)
		{
		case EEFClothingPiece::Head:
			return HeadHints;
		case EEFClothingPiece::Chest:
			return ChestHints;
		case EEFClothingPiece::Pants:
			return PantsHints;
		case EEFClothingPiece::Gloves:
			return GlovesHints;
		case EEFClothingPiece::Boots:
			return BootsHints;
		case EEFClothingPiece::Arms:
			return ArmsHints;
		case EEFClothingPiece::Loin:
			return LoinHints;
		case EEFClothingPiece::Auto:
		case EEFClothingPiece::Other:
		default:
			return EmptyHints;
		}
	}

	static EEFClothingPiece InferPieceFromName(const FString& SourceName)
	{
		const FString NormalizedName = NormalizeToken(SourceName);
		const TArray<EEFClothingPiece> CandidatePieces = {
			EEFClothingPiece::Loin,
			EEFClothingPiece::Chest,
			EEFClothingPiece::Pants,
			EEFClothingPiece::Boots,
			EEFClothingPiece::Gloves,
			EEFClothingPiece::Arms,
			EEFClothingPiece::Head
		};

		for (const EEFClothingPiece CandidatePiece : CandidatePieces)
		{
			for (const FString& Hint : GetPieceHints(CandidatePiece))
			{
				if (NormalizedName.Contains(Hint))
				{
					return CandidatePiece;
				}
			}
		}

		return EEFClothingPiece::Other;
	}

	static const TCHAR* GetClothingPieceLabel(const EEFClothingPiece Piece)
	{
		switch (Piece)
		{
		case EEFClothingPiece::Auto:
			return TEXT("Auto");
		case EEFClothingPiece::Head:
			return TEXT("Head");
		case EEFClothingPiece::Chest:
			return TEXT("Chest");
		case EEFClothingPiece::Pants:
			return TEXT("Pants");
		case EEFClothingPiece::Gloves:
			return TEXT("Gloves");
		case EEFClothingPiece::Boots:
			return TEXT("Boots");
		case EEFClothingPiece::Arms:
			return TEXT("Arms");
		case EEFClothingPiece::Loin:
			return TEXT("Loin");
		case EEFClothingPiece::Other:
		default:
			return TEXT("Other");
		}
	}

	static const TCHAR* GetBodyRegionLabel(const EEFBodyRegion Region)
	{
		switch (Region)
		{
		case EEFBodyRegion::None:
			return TEXT("None");
		case EEFBodyRegion::Head:
			return TEXT("Head");
		case EEFBodyRegion::Chest:
			return TEXT("Chest");
		case EEFBodyRegion::Waist:
			return TEXT("Waist");
		case EEFBodyRegion::Pelvis:
			return TEXT("Pelvis");
		case EEFBodyRegion::Glute:
			return TEXT("Glute");
		case EEFBodyRegion::Thigh:
			return TEXT("Thigh");
		case EEFBodyRegion::Calf:
			return TEXT("Calf");
		case EEFBodyRegion::Foot:
			return TEXT("Foot");
		case EEFBodyRegion::UpperArm:
			return TEXT("UpperArm");
		case EEFBodyRegion::Forearm:
			return TEXT("Forearm");
		case EEFBodyRegion::Hand:
			return TEXT("Hand");
		default:
			return TEXT("Unknown");
		}
	}

	struct FSemanticAlias
	{
		const TCHAR* Alias;
		const TCHAR* Canonical;
	};

	static void ExtractSemanticTokens(const FString& SourceName, TSet<FString>& OutTokens)
	{
		static const FSemanticAlias Aliases[] = {
			{ TEXT("breast"), TEXT("breast") },
			{ TEXT("boob"), TEXT("breast") },
			{ TEXT("bust"), TEXT("breast") },
			{ TEXT("chest"), TEXT("chest") },
			{ TEXT("pect"), TEXT("chest") },
			{ TEXT("rib"), TEXT("chest") },
			{ TEXT("torso"), TEXT("chest") },
			{ TEXT("underbust"), TEXT("chest") },
			{ TEXT("glute"), TEXT("glute") },
			{ TEXT("booty"), TEXT("glute") },
			{ TEXT("butt"), TEXT("glute") },
			{ TEXT("hip"), TEXT("hip") },
			{ TEXT("pelvis"), TEXT("pelvis") },
			{ TEXT("groin"), TEXT("pelvis") },
			{ TEXT("waist"), TEXT("waist") },
			{ TEXT("abdomen"), TEXT("waist") },
			{ TEXT("belly"), TEXT("waist") },
			{ TEXT("stomach"), TEXT("waist") },
			{ TEXT("tummy"), TEXT("waist") },
			{ TEXT("thigh"), TEXT("thigh") },
			{ TEXT("leg"), TEXT("leg") },
			{ TEXT("calf"), TEXT("calf") },
			{ TEXT("shin"), TEXT("calf") },
			{ TEXT("knee"), TEXT("calf") },
			{ TEXT("ankle"), TEXT("foot") },
			{ TEXT("foot"), TEXT("foot") },
			{ TEXT("toe"), TEXT("foot") },
			{ TEXT("shoulder"), TEXT("shoulder") },
			{ TEXT("arm"), TEXT("arm") },
			{ TEXT("bicep"), TEXT("arm") },
			{ TEXT("tricep"), TEXT("arm") },
			{ TEXT("forearm"), TEXT("forearm") },
			{ TEXT("wrist"), TEXT("forearm") },
			{ TEXT("hand"), TEXT("hand") },
			{ TEXT("head"), TEXT("head") },
			{ TEXT("face"), TEXT("head") }
		};

		const FString NormalizedName = NormalizeToken(SourceName);
		for (const FSemanticAlias& Alias : Aliases)
		{
			if (NormalizedName.Contains(Alias.Alias))
			{
				OutTokens.Add(Alias.Canonical);
			}
		}
	}

	static bool AreTokensInSameRegion(const FString& LeftToken, const FString& RightToken)
	{
		static const TArray<TArray<FString>> Regions = {
			{ TEXT("breast"), TEXT("chest") },
			{ TEXT("glute"), TEXT("hip"), TEXT("pelvis"), TEXT("waist"), TEXT("thigh"), TEXT("leg") },
			{ TEXT("calf"), TEXT("foot") },
			{ TEXT("shoulder"), TEXT("arm"), TEXT("forearm"), TEXT("hand") },
			{ TEXT("head") }
		};

		for (const TArray<FString>& Region : Regions)
		{
			if (Region.Contains(LeftToken) && Region.Contains(RightToken))
			{
				return true;
			}
		}

		return false;
	}

	static bool TokenMatchesPiece(const FString& Token, const EEFClothingPiece Piece)
	{
		switch (Piece)
		{
		case EEFClothingPiece::Chest:
			return Token == TEXT("breast") || Token == TEXT("chest");
		case EEFClothingPiece::Pants:
			return Token == TEXT("glute") || Token == TEXT("hip") || Token == TEXT("pelvis") || Token == TEXT("waist") || Token == TEXT("thigh") || Token == TEXT("leg");
		case EEFClothingPiece::Loin:
			return Token == TEXT("glute") || Token == TEXT("hip") || Token == TEXT("pelvis") || Token == TEXT("waist");
		case EEFClothingPiece::Boots:
			return Token == TEXT("calf") || Token == TEXT("foot");
		case EEFClothingPiece::Gloves:
			return Token == TEXT("forearm") || Token == TEXT("hand");
		case EEFClothingPiece::Arms:
			return Token == TEXT("shoulder") || Token == TEXT("arm") || Token == TEXT("forearm");
		case EEFClothingPiece::Head:
			return Token == TEXT("head");
		case EEFClothingPiece::Auto:
		case EEFClothingPiece::Other:
		default:
			return false;
		}
	}

	static EEFBodyRegion ResolveRegionFromCanonicalToken(const FString& Token)
	{
		if (Token == TEXT("breast") || Token == TEXT("chest"))
		{
			return EEFBodyRegion::Chest;
		}
		if (Token == TEXT("waist"))
		{
			return EEFBodyRegion::Waist;
		}
		if (Token == TEXT("pelvis"))
		{
			return EEFBodyRegion::Pelvis;
		}
		if (Token == TEXT("hip"))
		{
			return EEFBodyRegion::Pelvis;
		}
		if (Token == TEXT("glute"))
		{
			return EEFBodyRegion::Glute;
		}
		if (Token == TEXT("thigh") || Token == TEXT("leg"))
		{
			return EEFBodyRegion::Thigh;
		}
		if (Token == TEXT("calf"))
		{
			return EEFBodyRegion::Calf;
		}
		if (Token == TEXT("foot"))
		{
			return EEFBodyRegion::Foot;
		}
		if (Token == TEXT("shoulder") || Token == TEXT("arm"))
		{
			return EEFBodyRegion::UpperArm;
		}
		if (Token == TEXT("forearm"))
		{
			return EEFBodyRegion::Forearm;
		}
		if (Token == TEXT("hand"))
		{
			return EEFBodyRegion::Hand;
		}
		if (Token == TEXT("head"))
		{
			return EEFBodyRegion::Head;
		}

		return EEFBodyRegion::None;
	}

	static bool DoesRegionAffectPiece(const EEFBodyRegion Region, const EEFClothingPiece Piece)
	{
		switch (Piece)
		{
		case EEFClothingPiece::Chest:
			return Region == EEFBodyRegion::Chest || Region == EEFBodyRegion::Waist;
		case EEFClothingPiece::Pants:
			return Region == EEFBodyRegion::Waist || Region == EEFBodyRegion::Pelvis || Region == EEFBodyRegion::Glute || Region == EEFBodyRegion::Thigh;
		case EEFClothingPiece::Loin:
			return Region == EEFBodyRegion::Waist || Region == EEFBodyRegion::Pelvis || Region == EEFBodyRegion::Glute;
		case EEFClothingPiece::Boots:
			return Region == EEFBodyRegion::Calf || Region == EEFBodyRegion::Foot;
		case EEFClothingPiece::Gloves:
			return Region == EEFBodyRegion::Forearm || Region == EEFBodyRegion::Hand;
		case EEFClothingPiece::Arms:
			return Region == EEFBodyRegion::UpperArm || Region == EEFBodyRegion::Forearm;
		case EEFClothingPiece::Head:
			return Region == EEFBodyRegion::Head;
		case EEFClothingPiece::Auto:
		case EEFClothingPiece::Other:
		default:
			return true;
		}
	}

	static FString MakeNormalizedMorphKey(const FString& Value)
	{
		FString WorkingValue = NormalizeToken(Value);

		static const TArray<FString> PrefixesToTrim = {
			TEXT("pbm"),
			TEXT("phm"),
			TEXT("ctrl"),
			TEXT("jcm"),
			TEXT("mcm")
		};

		for (const FString& Prefix : PrefixesToTrim)
		{
			if (WorkingValue.StartsWith(Prefix))
			{
				WorkingValue.RightChopInline(Prefix.Len());
				break;
			}
		}

		WorkingValue.ReplaceInline(TEXT("glutes"), TEXT("glute"), ESearchCase::CaseSensitive);
		WorkingValue.ReplaceInline(TEXT("gluteus"), TEXT("glute"), ESearchCase::CaseSensitive);
		WorkingValue.ReplaceInline(TEXT("buttocks"), TEXT("butt"), ESearchCase::CaseSensitive);
		WorkingValue.ReplaceInline(TEXT("hips"), TEXT("hip"), ESearchCase::CaseSensitive);
		WorkingValue.ReplaceInline(TEXT("pelvic"), TEXT("pelvis"), ESearchCase::CaseSensitive);
		WorkingValue.ReplaceInline(TEXT("thighs"), TEXT("thigh"), ESearchCase::CaseSensitive);
		WorkingValue.ReplaceInline(TEXT("breasts"), TEXT("breast"), ESearchCase::CaseSensitive);

		FString Result;
		Result.Reserve(WorkingValue.Len());
		for (const TCHAR Character : WorkingValue)
		{
			if (FChar::IsAlnum(Character))
			{
				Result.AppendChar(Character);
			}
		}

		return Result;
	}

	static TArray<FEFClothingMorphScaleRule> BuildRecommendedRules()
	{
		TArray<FEFClothingMorphScaleRule> Rules;

		FEFClothingMorphScaleRule GluteRule;
		GluteRule.MatchToken = TEXT("glute");
		GluteRule.Scale = 1.10f;
		GluteRule.ClothingPiece = EEFClothingPiece::Pants;
		GluteRule.PositiveScale = 1.04f;
		GluteRule.ActivationThreshold = 0.20f;
		Rules.Add(GluteRule);

		FEFClothingMorphScaleRule ButtRule;
		ButtRule.MatchToken = TEXT("butt");
		ButtRule.Scale = 1.10f;
		ButtRule.ClothingPiece = EEFClothingPiece::Pants;
		ButtRule.PositiveScale = 1.04f;
		ButtRule.ActivationThreshold = 0.20f;
		Rules.Add(ButtRule);

		FEFClothingMorphScaleRule HipRule;
		HipRule.MatchToken = TEXT("hip");
		HipRule.Scale = 1.08f;
		HipRule.ClothingPiece = EEFClothingPiece::Pants;
		HipRule.PositiveScale = 1.03f;
		HipRule.ActivationThreshold = 0.15f;
		Rules.Add(HipRule);

		FEFClothingMorphScaleRule PelvisRule;
		PelvisRule.MatchToken = TEXT("pelvis");
		PelvisRule.Scale = 1.08f;
		PelvisRule.ClothingPiece = EEFClothingPiece::Pants;
		PelvisRule.PositiveScale = 1.03f;
		PelvisRule.ActivationThreshold = 0.15f;
		Rules.Add(PelvisRule);

		FEFClothingMorphScaleRule ThighRule;
		ThighRule.MatchToken = TEXT("thigh");
		ThighRule.Scale = 1.05f;
		ThighRule.ClothingPiece = EEFClothingPiece::Pants;
		ThighRule.PositiveScale = 1.02f;
		ThighRule.ActivationThreshold = 0.10f;
		Rules.Add(ThighRule);

		FEFClothingMorphScaleRule BreastRule;
		BreastRule.MatchToken = TEXT("breast");
		BreastRule.Scale = 1.08f;
		BreastRule.ClothingPiece = EEFClothingPiece::Chest;
		BreastRule.PositiveScale = 1.05f;
		BreastRule.ActivationThreshold = 0.15f;
		Rules.Add(BreastRule);

		FEFClothingMorphScaleRule ChestRule;
		ChestRule.MatchToken = TEXT("chest");
		ChestRule.Scale = 1.06f;
		ChestRule.ClothingPiece = EEFClothingPiece::Chest;
		ChestRule.PositiveScale = 1.03f;
		ChestRule.ActivationThreshold = 0.10f;
		Rules.Add(ChestRule);

		FEFClothingMorphScaleRule CalfRule;
		CalfRule.MatchToken = TEXT("calf");
		CalfRule.Scale = 1.03f;
		CalfRule.ClothingPiece = EEFClothingPiece::Boots;
		CalfRule.PositiveScale = 1.02f;
		CalfRule.ActivationThreshold = 0.10f;
		Rules.Add(CalfRule);

		FEFClothingMorphScaleRule ForearmRule;
		ForearmRule.MatchToken = TEXT("forearm");
		ForearmRule.Scale = 1.02f;
		ForearmRule.ClothingPiece = EEFClothingPiece::Arms;
		ForearmRule.PositiveScale = 1.01f;
		ForearmRule.ActivationThreshold = 0.10f;
		Rules.Add(ForearmRule);

		return Rules;
	}

	static TArray<FEFBodyRegionClearanceProxy> BuildRecommendedClearanceProxies()
	{
		TArray<FEFBodyRegionClearanceProxy> Proxies;

		FEFBodyRegionClearanceProxy ChestProxy;
		ChestProxy.Region = EEFBodyRegion::Chest;
		ChestProxy.AnchorBone = TEXT("spine_03");
		ChestProxy.MeasureBoneA = TEXT("upperarm_l");
		ChestProxy.MeasureBoneB = TEXT("upperarm_r");
		ChestProxy.BaseProxyRadiusCm = 10.0f;
		ChestProxy.TargetClearanceCm = 0.60f;
		ChestProxy.MorphExpansionPerUnitCm = 1.60f;
		ChestProxy.DrivingMorphTokens = { TEXT("breast"), TEXT("chest"), TEXT("pect"), TEXT("underbust") };
		Proxies.Add(ChestProxy);

		FEFBodyRegionClearanceProxy WaistProxy;
		WaistProxy.Region = EEFBodyRegion::Waist;
		WaistProxy.AnchorBone = TEXT("spine_02");
		WaistProxy.BaseProxyRadiusCm = 8.0f;
		WaistProxy.TargetClearanceCm = 0.45f;
		WaistProxy.MorphExpansionPerUnitCm = 1.00f;
		WaistProxy.DrivingMorphTokens = { TEXT("waist"), TEXT("abdomen"), TEXT("belly") };
		Proxies.Add(WaistProxy);

		FEFBodyRegionClearanceProxy PelvisProxy;
		PelvisProxy.Region = EEFBodyRegion::Pelvis;
		PelvisProxy.AnchorBone = TEXT("pelvis");
		PelvisProxy.MeasureBoneA = TEXT("thigh_l");
		PelvisProxy.MeasureBoneB = TEXT("thigh_r");
		PelvisProxy.BaseProxyRadiusCm = 9.0f;
		PelvisProxy.TargetClearanceCm = 0.50f;
		PelvisProxy.MorphExpansionPerUnitCm = 1.20f;
		PelvisProxy.DrivingMorphTokens = { TEXT("pelvis"), TEXT("hip"), TEXT("groin") };
		Proxies.Add(PelvisProxy);

		FEFBodyRegionClearanceProxy GluteProxy;
		GluteProxy.Region = EEFBodyRegion::Glute;
		GluteProxy.AnchorBone = TEXT("pelvis");
		GluteProxy.BaseProxyRadiusCm = 10.0f;
		GluteProxy.TargetClearanceCm = 0.70f;
		GluteProxy.MorphExpansionPerUnitCm = 1.80f;
		GluteProxy.DrivingMorphTokens = { TEXT("glute"), TEXT("butt"), TEXT("hip") };
		Proxies.Add(GluteProxy);

		FEFBodyRegionClearanceProxy ThighProxy;
		ThighProxy.Region = EEFBodyRegion::Thigh;
		ThighProxy.AnchorBone = TEXT("thigh_l");
		ThighProxy.MeasureBoneA = TEXT("thigh_l");
		ThighProxy.MeasureBoneB = TEXT("thigh_r");
		ThighProxy.BaseProxyRadiusCm = 6.5f;
		ThighProxy.TargetClearanceCm = 0.45f;
		ThighProxy.MorphExpansionPerUnitCm = 1.10f;
		ThighProxy.DrivingMorphTokens = { TEXT("thigh"), TEXT("leg") };
		Proxies.Add(ThighProxy);

		FEFBodyRegionClearanceProxy CalfProxy;
		CalfProxy.Region = EEFBodyRegion::Calf;
		CalfProxy.AnchorBone = TEXT("calf_l");
		CalfProxy.MeasureBoneA = TEXT("calf_l");
		CalfProxy.MeasureBoneB = TEXT("calf_r");
		CalfProxy.BaseProxyRadiusCm = 4.5f;
		CalfProxy.TargetClearanceCm = 0.35f;
		CalfProxy.MorphExpansionPerUnitCm = 0.75f;
		CalfProxy.DrivingMorphTokens = { TEXT("calf"), TEXT("shin"), TEXT("knee") };
		Proxies.Add(CalfProxy);

		FEFBodyRegionClearanceProxy UpperArmProxy;
		UpperArmProxy.Region = EEFBodyRegion::UpperArm;
		UpperArmProxy.AnchorBone = TEXT("upperarm_l");
		UpperArmProxy.MeasureBoneA = TEXT("upperarm_l");
		UpperArmProxy.MeasureBoneB = TEXT("upperarm_r");
		UpperArmProxy.BaseProxyRadiusCm = 4.5f;
		UpperArmProxy.TargetClearanceCm = 0.30f;
		UpperArmProxy.MorphExpansionPerUnitCm = 0.75f;
		UpperArmProxy.DrivingMorphTokens = { TEXT("arm"), TEXT("bicep"), TEXT("tricep"), TEXT("shoulder") };
		Proxies.Add(UpperArmProxy);

		FEFBodyRegionClearanceProxy ForearmProxy;
		ForearmProxy.Region = EEFBodyRegion::Forearm;
		ForearmProxy.AnchorBone = TEXT("lowerarm_l");
		ForearmProxy.MeasureBoneA = TEXT("lowerarm_l");
		ForearmProxy.MeasureBoneB = TEXT("lowerarm_r");
		ForearmProxy.BaseProxyRadiusCm = 3.5f;
		ForearmProxy.TargetClearanceCm = 0.25f;
		ForearmProxy.MorphExpansionPerUnitCm = 0.50f;
		ForearmProxy.DrivingMorphTokens = { TEXT("forearm"), TEXT("wrist"), TEXT("hand") };
		Proxies.Add(ForearmProxy);

		return Proxies;
	}
}

UEFClothingMorphComponent::UEFClothingMorphComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	SpecialMorphScaleRules = EFClothingMorphPrivate::BuildRecommendedRules();
	BodyClearanceProxies = EFClothingMorphPrivate::BuildRecommendedClearanceProxies();
}

void UEFClothingMorphComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshACFUIntegrationState();

	if ((bAutoConvertConfiguredBaseClothing && ACFUBaseClothingMeshes.Num() > 0)
		|| (bAutoEquipConfiguredACFUItems && ACFUQuickEquipItems.Num() > 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("EF Clothing Morph on %s ignores legacy intrusive ACFU conversion and quick-equip paths. ACFU integration is passive and only mirrors real armor meshes."),
			*GetNameSafe(GetOwner()));
	}

	RefreshMeshBindings();
	ApplyMorphsNow();
}

void UEFClothingMorphComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromACFUEquipmentEvents();
	Super::EndPlay(EndPlayReason);
}

void UEFClothingMorphComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	RefreshACFUIntegrationState();
	ProcessPendingACFURefreshes();

	if (UpdateIntervalSeconds > 0.0f)
	{
		const double CurrentTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (CurrentTimeSeconds < NextUpdateTimeSeconds)
		{
			return;
		}

		NextUpdateTimeSeconds = CurrentTimeSeconds + UpdateIntervalSeconds;
	}

	ApplyMorphsNow();
}

void UEFClothingMorphComponent::RefreshMeshBindings()
{
	RefreshACFUIntegrationState();
	ResetDebugState();
	ResolveBodyMeshComponent();
	ResolveACFUSlotMappings();
	SyncACFUSlotLeaderPose();
	ResolveClothingMeshComponents();
	RebuildMorphBindingCache();
	UpdateResolvedMeshDebugNames();
}

void UEFClothingMorphComponent::ApplyMorphsNow()
{
	LastAppliedMorphCount = 0;
	LastUpdatedClothingMeshCount = 0;

	USkeletalMeshComponent* ResolvedBodyMesh = BodyMeshComponent.Get();
	if (!IsValid(ResolvedBodyMesh) || !IsValid(ResolvedBodyMesh->GetSkeletalMeshAsset()))
	{
		RefreshMeshBindings();
		ResolvedBodyMesh = BodyMeshComponent.Get();
	}
	else if (bAutoRefreshMeshBindings && (MorphBindingsByClothingMesh.Num() == 0 || NeedsBindingRefresh()))
	{
		RefreshMeshBindings();
		ResolvedBodyMesh = BodyMeshComponent.Get();
	}

	if (!IsValid(ResolvedBodyMesh))
	{
		if (bLogSetupWarnings && !bLoggedMissingBodyWarning)
		{
			UE_LOG(LogTemp, Warning, TEXT("EF Clothing Morph on %s could not resolve a body mesh component."), *GetNameSafe(GetOwner()));
			bLoggedMissingBodyWarning = true;
		}

		return;
	}

	UpdatePrecisionLayer(ResolvedBodyMesh);

	int32 TotalAppliedMorphs = 0;
	int32 UpdatedClothingMeshes = 0;

	for (USkeletalMeshComponent* ClothingMesh : ClothingMeshComponents)
	{
		if (!IsValid(ClothingMesh))
		{
			continue;
		}

		const TArray<FEFClothingMorphBinding>* MorphBindings = MorphBindingsByClothingMesh.Find(ClothingMesh);
		if (MorphBindings == nullptr || MorphBindings->Num() == 0)
		{
			continue;
		}

		bool bAppliedAnyMorph = false;
		for (const FEFClothingMorphBinding& MorphBinding : *MorphBindings)
		{
			const float BodyValue = ResolvedBodyMesh->GetMorphTarget(MorphBinding.BodyMorphName);
			const float ClothingValue = ResolveScaleForMorph(MorphBinding.BodyMorphName, BodyValue, ClothingMesh);
			ClothingMesh->SetMorphTarget(MorphBinding.ClothingMorphName, ClothingValue, FMath::IsNearlyZero(ClothingValue));
			++TotalAppliedMorphs;
			bAppliedAnyMorph = true;
		}

		if (bAppliedAnyMorph)
		{
			FinalizeMorphUpdate(ClothingMesh);
			++UpdatedClothingMeshes;
		}
	}

	LastAppliedMorphCount = TotalAppliedMorphs;
	LastUpdatedClothingMeshCount = UpdatedClothingMeshes;
}

void UEFClothingMorphComponent::SetBaseClothingMorphMultiplier(float NewMultiplier)
{
	BaseClothingScale = FMath::Max(NewMultiplier, 0.0f);
	ApplyMorphsNow();
}

void UEFClothingMorphComponent::SetClothingMeshMorphMultiplier(FName MeshComponentName, float NewMultiplier)
{
	if (MeshComponentName.IsNone())
	{
		return;
	}

	const float ClampedMultiplier = FMath::Max(NewMultiplier, 0.0f);
	if (FEFClothingMeshScaleOverride* ExistingOverride = ClothingMeshScaleOverrides.FindByPredicate([MeshComponentName](const FEFClothingMeshScaleOverride& Override)
	{
		return Override.MeshComponentName == MeshComponentName;
	}))
	{
		ExistingOverride->Multiplier = ClampedMultiplier;
	}
	else
	{
		FEFClothingMeshScaleOverride& NewOverride = ClothingMeshScaleOverrides.AddDefaulted_GetRef();
		NewOverride.MeshComponentName = MeshComponentName;
		NewOverride.Multiplier = ClampedMultiplier;
	}

	ApplyMorphsNow();
}

void UEFClothingMorphComponent::SetClothingPieceMorphMultiplier(EEFClothingPiece ClothingPiece, float NewMultiplier)
{
	if (ClothingPiece == EEFClothingPiece::Auto)
	{
		return;
	}

	const float ClampedMultiplier = FMath::Max(NewMultiplier, 0.0f);
	if (FEFClothingPieceScaleOverride* ExistingOverride = ClothingPieceScaleOverrides.FindByPredicate([ClothingPiece](const FEFClothingPieceScaleOverride& Override)
	{
		return Override.ClothingPiece == ClothingPiece;
	}))
	{
		ExistingOverride->Multiplier = ClampedMultiplier;
	}
	else
	{
		FEFClothingPieceScaleOverride& NewOverride = ClothingPieceScaleOverrides.AddDefaulted_GetRef();
		NewOverride.ClothingPiece = ClothingPiece;
		NewOverride.Multiplier = ClampedMultiplier;
	}

	ApplyMorphsNow();
}

void UEFClothingMorphComponent::ClearClothingMeshMorphMultiplier(FName MeshComponentName)
{
	if (MeshComponentName.IsNone())
	{
		return;
	}

	ClothingMeshScaleOverrides.RemoveAll([MeshComponentName](const FEFClothingMeshScaleOverride& Override)
	{
		return Override.MeshComponentName == MeshComponentName;
	});

	ApplyMorphsNow();
}

void UEFClothingMorphComponent::ResetToRecommendedScaling()
{
	BaseClothingScale = 1.08f;
	ClothingMeshScaleOverrides.Reset();
	ClothingPieceScaleOverrides.Reset();
	SpecialMorphScaleRules = EFClothingMorphPrivate::BuildRecommendedRules();
	if (BodyClearanceProxies.Num() == 0)
	{
		BodyClearanceProxies = EFClothingMorphPrivate::BuildRecommendedClearanceProxies();
	}
	ApplyMorphsNow();
}

FString UEFClothingMorphComponent::GetSetupSummary() const
{
	const FString BodyName = ResolvedBodyMeshDebugName.IsNone() ? TEXT("None") : ResolvedBodyMeshDebugName.ToString();
	const FString ClothingNames = ResolvedClothingMeshDebugNames.Num() > 0 ? FString::JoinBy(ResolvedClothingMeshDebugNames, TEXT(", "), [](const FName& Name)
	{
		return Name.ToString();
	}) : TEXT("None");
	const FString PieceSummary = ResolvedClothingPieceDebug.Num() > 0 ? FString::Join(ResolvedClothingPieceDebug, TEXT(", ")) : TEXT("None");
	const FString ACFUEquipmentName = ResolvedACFUEquipmentComponentDebugName.IsNone() ? TEXT("None") : ResolvedACFUEquipmentComponentDebugName.ToString();
	const FString ACFUSlotSummary = ResolvedACFUSlotDebug.Num() > 0 ? FString::Join(ResolvedACFUSlotDebug, TEXT(", ")) : TEXT("None");
	const FString BindingSummary = ResolvedClothingBindingDebug.Num() > 0 ? FString::Join(ResolvedClothingBindingDebug, TEXT(" || ")) : TEXT("None");
	const FString RegionSummary = BodyRegionRiskDebug.Num() > 0 ? FString::Join(BodyRegionRiskDebug, TEXT(" || ")) : TEXT("None");

	return FString::Printf(TEXT("Body=%s | Clothing=%s | Pieces=%s | ACFUEquipment=%s | ACFUSlots=%s | BaseScale=%.3f | CompatibleMatches=%d | UpdatedMeshes=%d | AppliedMorphs=%d | Bindings=%s | RegionRisk=%s"),
		*BodyName,
		*ClothingNames,
		*PieceSummary,
		*ACFUEquipmentName,
		*ACFUSlotSummary,
		BaseClothingScale,
		LastCompatibleMatchCount,
		LastUpdatedClothingMeshCount,
		LastAppliedMorphCount,
		*BindingSummary,
		*RegionSummary);
}

bool UEFClothingMorphComponent::ResolveBodyMeshComponent()
{
	BodyMeshComponent = nullptr;

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);

	if (!BodyMeshComponentName.IsNone())
	{
		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (!IsValid(MeshComponent) || !IsValid(MeshComponent->GetSkeletalMeshAsset()))
			{
				continue;
			}

			if (MeshComponent->GetFName() == BodyMeshComponentName || MeshComponent->GetName().Equals(BodyMeshComponentName.ToString(), ESearchCase::IgnoreCase))
			{
				BodyMeshComponent = MeshComponent;
				return true;
			}
		}
	}

	if (bPreferCharacterCustomizationComponent)
	{
		if (UEFCharacterCustomizationComponent* CustomizationComponent = ResolveCharacterCustomizationComponent())
		{
			if (USkeletalMeshComponent* BodySelectionComponent = CustomizationComponent->GetBodyMeshSelectionComponent())
			{
				if (IsValid(BodySelectionComponent) && IsValid(BodySelectionComponent->GetSkeletalMeshAsset()))
				{
					BodyMeshComponent = BodySelectionComponent;
					return true;
				}
			}

			if (USkeletalMeshComponent* CustomBodyMesh = CustomizationComponent->GetBodyMeshComponent())
			{
				if (IsValid(CustomBodyMesh) && IsValid(CustomBodyMesh->GetSkeletalMeshAsset()))
				{
					BodyMeshComponent = CustomBodyMesh;
					return true;
				}
			}
		}
	}

	ACharacter* CharacterOwner = Cast<ACharacter>(Owner);

	struct FScoredMeshComponent
	{
		USkeletalMeshComponent* Component = nullptr;
		int32 Score = 0;
	};

	TArray<FScoredMeshComponent> ScoredComponents;
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || !IsValid(MeshComponent->GetSkeletalMeshAsset()))
		{
			continue;
		}

		FScoredMeshComponent ScoredComponent;
		ScoredComponent.Component = MeshComponent;
		ScoredComponent.Score += GetMorphNames(MeshComponent).Num();

		const FString ComponentName = MeshComponent->GetName();
		if (CharacterOwner && CharacterOwner->GetMesh() == MeshComponent)
		{
			ScoredComponent.Score += 500;
		}

		if (EFClothingMorphPrivate::MatchesAnyHint(ComponentName, EFClothingMorphPrivate::GetBodyMeshHints()))
		{
			ScoredComponent.Score += 250;
		}

		if (EFClothingMorphPrivate::MatchesAnyHint(ComponentName, EFClothingMorphPrivate::GetClothingMeshHints()))
		{
			ScoredComponent.Score -= 300;
		}

		if (EFClothingMorphPrivate::MatchesAnyHint(ComponentName, EFClothingMorphPrivate::GetHairMeshHints()))
		{
			ScoredComponent.Score -= 400;
		}

		if (MeshComponent->LeaderPoseComponent.IsValid())
		{
			ScoredComponent.Score -= 300;
		}

		if (MeshComponent->GetAttachParent() == nullptr)
		{
			ScoredComponent.Score += 50;
		}

		if (EFClothingMorphPrivate::MatchesAnyHint(MeshComponent->GetSkeletalMeshAsset()->GetPathName(), EFClothingMorphPrivate::GetDazPathHints()))
		{
			ScoredComponent.Score += 100;
		}

		ScoredComponents.Add(ScoredComponent);
	}

	ScoredComponents.Sort([](const FScoredMeshComponent& Left, const FScoredMeshComponent& Right)
	{
		return Left.Score > Right.Score;
	});

	if (ScoredComponents.Num() > 0)
	{
		BodyMeshComponent = ScoredComponents[0].Component;
	}

	return IsValid(BodyMeshComponent.Get());
}

void UEFClothingMorphComponent::ResolveClothingMeshComponents()
{
	ClothingMeshComponents.Reset();

	AActor* Owner = GetOwner();
	USkeletalMeshComponent* ResolvedBodyMesh = BodyMeshComponent.Get();
	if (!IsValid(Owner) || !IsValid(ResolvedBodyMesh))
	{
		return;
	}

	const bool bManualListActive = ClothingMeshComponentNames.Num() > 0;

	if (bPreferCharacterCustomizationComponent)
	{
		if (UEFCharacterCustomizationComponent* CustomizationComponent = ResolveCharacterCustomizationComponent())
		{
			for (USkeletalMeshComponent* ClothingMesh : CustomizationComponent->GetClothingMeshComponents())
			{
				if (!IsValid(ClothingMesh) || ClothingMesh == ResolvedBodyMesh || !IsValid(ClothingMesh->GetSkeletalMeshAsset()))
				{
					continue;
				}

				if (bManualListActive)
				{
					const bool bMatchesManualName = ClothingMeshComponentNames.Contains(ClothingMesh->GetFName())
						|| ClothingMeshComponentNames.ContainsByPredicate([ClothingMesh](const FName ClothingName)
						{
							return ClothingMesh->GetName().Equals(ClothingName.ToString(), ESearchCase::IgnoreCase);
						});

					if (!bMatchesManualName)
					{
						continue;
					}
				}

				ClothingMeshComponents.AddUnique(ClothingMesh);
			}
		}
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (ShouldTrackAsClothing(MeshComponent))
		{
			ClothingMeshComponents.AddUnique(MeshComponent);
		}
	}

	if (bLogSetupWarnings && ClothingMeshComponents.Num() == 0 && !bLoggedMissingClothingWarning)
	{
		UE_LOG(LogTemp, Warning, TEXT("EF Clothing Morph on %s could not resolve any clothing mesh components."), *GetNameSafe(GetOwner()));
		bLoggedMissingClothingWarning = true;
	}
}

void UEFClothingMorphComponent::RebuildMorphBindingCache()
{
	MorphBindingsByClothingMesh.Reset();
	ResolvedClothingPiecesByMesh.Reset();
	LastResolvedBodyMorphCount = 0;
	LastCompatibleMatchCount = 0;
	ResolvedClothingBindingDebug.Reset();
	ResolvedClothingPieceDebug.Reset();

	USkeletalMeshComponent* ResolvedBodyMesh = BodyMeshComponent.Get();
	if (!IsValid(ResolvedBodyMesh))
	{
		CacheResolvedMeshAssets();
		return;
	}

	const TArray<FName> BodyMorphNames = GetMorphNames(ResolvedBodyMesh);
	LastResolvedBodyMorphCount = BodyMorphNames.Num();
	if (BodyMorphNames.Num() == 0)
	{
		CacheResolvedMeshAssets();
		return;
	}

	for (USkeletalMeshComponent* ClothingMesh : ClothingMeshComponents)
	{
		if (!IsValid(ClothingMesh))
		{
			continue;
		}

		const EEFClothingPiece ClothingPiece = ResolveClothingPieceForMesh(ClothingMesh);
		ResolvedClothingPiecesByMesh.Add(ClothingMesh, ClothingPiece);
		ResolvedClothingPieceDebug.Add(FString::Printf(TEXT("%s=%s"),
			*ClothingMesh->GetName(),
			EFClothingMorphPrivate::GetClothingPieceLabel(ClothingPiece)));

		const TArray<FName> ClothingMorphNames = GetMorphNames(ClothingMesh);
		TSet<FName> UsedClothingMorphNames;
		TArray<FEFClothingMorphBinding> MorphBindings;
		int32 ExactMatchCount = 0;
		int32 CompatibleMatchCount = 0;

		for (const FName BodyMorphName : BodyMorphNames)
		{
			bool bUsedCompatibleMatch = false;
			const FName ClothingMorphName = ResolveCompatibleClothingMorphName(BodyMorphName, ClothingMesh, ClothingMorphNames, UsedClothingMorphNames, bUsedCompatibleMatch);
			if (ClothingMorphName.IsNone())
			{
				continue;
			}

			FEFClothingMorphBinding& NewBinding = MorphBindings.AddDefaulted_GetRef();
			NewBinding.BodyMorphName = BodyMorphName;
			NewBinding.ClothingMorphName = ClothingMorphName;
			NewBinding.bUsedCompatibleMatch = bUsedCompatibleMatch;

			if (bUsedCompatibleMatch)
			{
				++CompatibleMatchCount;
			}
			else
			{
				++ExactMatchCount;
			}
		}

		ResolvedClothingBindingDebug.Add(FString::Printf(TEXT("%s=%d morphs (%d exact, %d compatible) | Piece=%s"),
			*ClothingMesh->GetName(),
			MorphBindings.Num(),
			ExactMatchCount,
			CompatibleMatchCount,
			EFClothingMorphPrivate::GetClothingPieceLabel(ClothingPiece)));

		if (MorphBindings.Num() > 0)
		{
			MorphBindingsByClothingMesh.Add(ClothingMesh, MoveTemp(MorphBindings));
		}

		LastCompatibleMatchCount += CompatibleMatchCount;
	}

	CacheResolvedMeshAssets();
}

TArray<FName> UEFClothingMorphComponent::GetMorphNames(const USkeletalMeshComponent* MeshComponent) const
{
	TArray<FName> MorphNames;

	if (!IsValid(MeshComponent) || !IsValid(MeshComponent->GetSkeletalMeshAsset()))
	{
		return MorphNames;
	}

	USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMeshAsset();
	for (const TPair<FName, int32>& MorphTargetPair : SkeletalMesh->GetMorphTargetIndexMap())
	{
		MorphNames.AddUnique(MorphTargetPair.Key);
	}

	for (const TObjectPtr<UMorphTarget>& MorphTarget : SkeletalMesh->GetMorphTargets())
	{
		if (IsValid(MorphTarget))
		{
			MorphNames.AddUnique(MorphTarget->GetFName());
		}
	}

	return MorphNames;
}

bool UEFClothingMorphComponent::ShouldTrackAsClothing(const USkeletalMeshComponent* MeshComponent) const
{
	const USkeletalMeshComponent* ResolvedBodyMesh = BodyMeshComponent.Get();
	if (!IsValid(MeshComponent) || MeshComponent == ResolvedBodyMesh || !IsValid(MeshComponent->GetSkeletalMeshAsset()))
	{
		return false;
	}

	if (bEnableACFUIntegration && bUseACFUArmorSlotsAsClothing)
	{
		const TWeakObjectPtr<USkeletalMeshComponent> MeshKey(const_cast<USkeletalMeshComponent*>(MeshComponent));
		if (ACFUSlotTagsByMesh.Contains(MeshKey))
		{
			return true;
		}
	}

	if (ClothingMeshComponentNames.Num() > 0)
	{
		for (const FName ClothingName : ClothingMeshComponentNames)
		{
			if (MeshComponent->GetFName() == ClothingName || MeshComponent->GetName().Equals(ClothingName.ToString(), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	const FString ComponentName = MeshComponent->GetName();
	if (EFClothingMorphPrivate::MatchesAnyHint(ComponentName, EFClothingMorphPrivate::GetHairMeshHints()))
	{
		return false;
	}

	const bool bUsesBodyAsLeaderPose = bAutoFindLeaderPoseClothingMeshes && MeshComponent->LeaderPoseComponent.Get() == ResolvedBodyMesh;
	const bool bAttachedToBody = bAutoFindAttachedClothingMeshes && MeshComponent->GetAttachParent() == ResolvedBodyMesh;
	const bool bLooksLikeClothing = EFClothingMorphPrivate::MatchesAnyHint(ComponentName, EFClothingMorphPrivate::GetClothingMeshHints());

	if (bUsesBodyAsLeaderPose)
	{
		return true;
	}

	if (bAttachedToBody && bLooksLikeClothing)
	{
		return true;
	}

	return false;
}

void UEFClothingMorphComponent::UpdatePrecisionLayer(const USkeletalMeshComponent* BodyMesh)
{
	BodyRegionRiskDebug.Reset();
	RegionMultiplierBonusByRegion.Reset();
	RegionBiasByRegion.Reset();

	if (!bEnablePrecisionLayer || !IsValid(BodyMesh) || !IsValid(BodyMesh->GetSkeletalMeshAsset()))
	{
		SmoothedRegionRiskByRegion.Reset();
		return;
	}

	if (BodyClearanceProxies.Num() == 0)
	{
		BodyClearanceProxies = EFClothingMorphPrivate::BuildRecommendedClearanceProxies();
	}

	const TArray<FName> BodyMorphNames = GetMorphNames(BodyMesh);
	const float DeltaTime = FMath::Max(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f, 1.0f / 60.0f);
	const float SmoothingSpeed = FMath::Max(RegionRiskSmoothingSpeed, 0.0f);

	for (const FEFBodyRegionClearanceProxy& Proxy : BodyClearanceProxies)
	{
		if (Proxy.Region == EEFBodyRegion::None)
		{
			continue;
		}

		float RegionMagnitude = 0.0f;
		for (const FName BodyMorphName : BodyMorphNames)
		{
			const FString NormalizedMorphName = EFClothingMorphPrivate::NormalizeToken(BodyMorphName.ToString());
			bool bMatchesProxy = false;

			if (Proxy.DrivingMorphTokens.Num() > 0)
			{
				for (const FString& DrivingToken : Proxy.DrivingMorphTokens)
				{
					const FString NormalizedDrivingToken = EFClothingMorphPrivate::NormalizeToken(DrivingToken);
					if (!NormalizedDrivingToken.IsEmpty() && NormalizedMorphName.Contains(NormalizedDrivingToken))
					{
						bMatchesProxy = true;
						break;
					}
				}
			}
			else if (ResolveBodyRegionForMorph(BodyMorphName) == Proxy.Region)
			{
				bMatchesProxy = true;
			}

			if (!bMatchesProxy)
			{
				continue;
			}

			RegionMagnitude = FMath::Max(RegionMagnitude, FMath::Abs(BodyMesh->GetMorphTarget(BodyMorphName)));
		}

		float EstimatedRadiusCm = FMath::Max(Proxy.BaseProxyRadiusCm, 0.0f);
		if (bEnableBodyClearanceModel
			&& !Proxy.MeasureBoneA.IsNone()
			&& !Proxy.MeasureBoneB.IsNone()
			&& BodyMesh->GetBoneIndex(Proxy.MeasureBoneA) != INDEX_NONE
			&& BodyMesh->GetBoneIndex(Proxy.MeasureBoneB) != INDEX_NONE)
		{
			const FVector MeasureLocationA = BodyMesh->GetBoneLocation(Proxy.MeasureBoneA, EBoneSpaces::ComponentSpace);
			const FVector MeasureLocationB = BodyMesh->GetBoneLocation(Proxy.MeasureBoneB, EBoneSpaces::ComponentSpace);
			const float MeasuredSpanCm = FVector::Distance(MeasureLocationA, MeasureLocationB);
			if (MeasuredSpanCm > KINDA_SMALL_NUMBER)
			{
				EstimatedRadiusCm = FMath::Max(EstimatedRadiusCm, MeasuredSpanCm * 0.5f);
			}
		}

		const float DesiredClearanceCm = FMath::Max(Proxy.TargetClearanceCm + (RegionMagnitude * Proxy.MorphExpansionPerUnitCm), 0.0f);
		const float ClearanceRatio = EstimatedRadiusCm > KINDA_SMALL_NUMBER ? DesiredClearanceCm / EstimatedRadiusCm : 0.0f;
		const float TargetRisk = FMath::Clamp((RegionMagnitude * 0.85f) + (ClearanceRatio * 1.35f), 0.0f, 1.0f);
		const float PreviousRisk = SmoothedRegionRiskByRegion.FindRef(Proxy.Region);
		const float SmoothedRisk = SmoothingSpeed > 0.0f
			? FMath::FInterpTo(PreviousRisk, TargetRisk, DeltaTime, SmoothingSpeed)
			: TargetRisk;

		SmoothedRegionRiskByRegion.Add(Proxy.Region, SmoothedRisk);

		const float MultiplierBonus = FMath::Clamp(SmoothedRisk * PrecisionLayerMultiplierScale, 0.0f, MaxPrecisionMultiplierBonus);
		const float AdditiveBias = FMath::Clamp(DesiredClearanceCm * PrecisionLayerBiasScale * SmoothedRisk, 0.0f, MaxPrecisionBias);

		RegionMultiplierBonusByRegion.Add(Proxy.Region, MultiplierBonus);
		RegionBiasByRegion.Add(Proxy.Region, AdditiveBias);
		BodyRegionRiskDebug.Add(FString::Printf(TEXT("%s Risk=%.2f Morph=%.2f Radius=%.2fcm Clearance=%.2fcm Bonus=%.3f Bias=%.3f"),
			EFClothingMorphPrivate::GetBodyRegionLabel(Proxy.Region),
			SmoothedRisk,
			RegionMagnitude,
			EstimatedRadiusCm,
			DesiredClearanceCm,
			MultiplierBonus,
			AdditiveBias));
	}
}

EEFBodyRegion UEFClothingMorphComponent::ResolveBodyRegionForMorph(const FName BodyMorphName) const
{
	const FString NormalizedMorphName = EFClothingMorphPrivate::NormalizeToken(BodyMorphName.ToString());

	for (const FEFBodyRegionClearanceProxy& Proxy : BodyClearanceProxies)
	{
		if (Proxy.Region == EEFBodyRegion::None)
		{
			continue;
		}

		for (const FString& DrivingToken : Proxy.DrivingMorphTokens)
		{
			const FString NormalizedDrivingToken = EFClothingMorphPrivate::NormalizeToken(DrivingToken);
			if (!NormalizedDrivingToken.IsEmpty() && NormalizedMorphName.Contains(NormalizedDrivingToken))
			{
				return Proxy.Region;
			}
		}
	}

	TSet<FString> BodyTokens;
	EFClothingMorphPrivate::ExtractSemanticTokens(BodyMorphName.ToString(), BodyTokens);
	for (const FString& BodyToken : BodyTokens)
	{
		const EEFBodyRegion ResolvedRegion = EFClothingMorphPrivate::ResolveRegionFromCanonicalToken(BodyToken);
		if (ResolvedRegion != EEFBodyRegion::None)
		{
			return ResolvedRegion;
		}
	}

	return EEFBodyRegion::None;
}

float UEFClothingMorphComponent::ResolveRegionRiskMultiplier(const EEFBodyRegion BodyRegion, const EEFClothingPiece ClothingPiece) const
{
	if (!bEnablePrecisionLayer || BodyRegion == EEFBodyRegion::None)
	{
		return 1.0f;
	}

	if (!EFClothingMorphPrivate::DoesRegionAffectPiece(BodyRegion, ClothingPiece))
	{
		return 1.0f;
	}

	return 1.0f + FMath::Clamp(RegionMultiplierBonusByRegion.FindRef(BodyRegion), 0.0f, MaxPrecisionMultiplierBonus);
}

float UEFClothingMorphComponent::ResolveRegionRiskBias(const EEFBodyRegion BodyRegion, const float BodyValue, const EEFClothingPiece ClothingPiece) const
{
	if (!bEnablePrecisionLayer || BodyValue <= 0.0f || BodyRegion == EEFBodyRegion::None)
	{
		return 0.0f;
	}

	if (!EFClothingMorphPrivate::DoesRegionAffectPiece(BodyRegion, ClothingPiece))
	{
		return 0.0f;
	}

	return FMath::Clamp(RegionBiasByRegion.FindRef(BodyRegion), 0.0f, MaxPrecisionBias);
}

FName UEFClothingMorphComponent::ResolveCompatibleClothingMorphName(const FName BodyMorphName, const USkeletalMeshComponent* ClothingMesh, const TArray<FName>& ClothingMorphNames, TSet<FName>& InOutUsedClothingMorphNames, bool& bOutUsedCompatibleMatch) const
{
	bOutUsedCompatibleMatch = false;

	for (const FName ClothingMorphName : ClothingMorphNames)
	{
		if (ClothingMorphName == BodyMorphName && !InOutUsedClothingMorphNames.Contains(ClothingMorphName))
		{
			InOutUsedClothingMorphNames.Add(ClothingMorphName);
			return ClothingMorphName;
		}
	}

	if (!bAllowCompatibleMorphNames)
	{
		return NAME_None;
	}

	const FString NormalizedBodyMorphKey = EFClothingMorphPrivate::MakeNormalizedMorphKey(BodyMorphName.ToString());
	if (NormalizedBodyMorphKey.IsEmpty())
	{
		return NAME_None;
	}

	for (const FName ClothingMorphName : ClothingMorphNames)
	{
		if (InOutUsedClothingMorphNames.Contains(ClothingMorphName))
		{
			continue;
		}

		if (EFClothingMorphPrivate::MakeNormalizedMorphKey(ClothingMorphName.ToString()) == NormalizedBodyMorphKey)
		{
			InOutUsedClothingMorphNames.Add(ClothingMorphName);
			bOutUsedCompatibleMatch = true;
			return ClothingMorphName;
		}
	}

	if (!bAllowSemanticRegionMatching)
	{
		return NAME_None;
	}

	TSet<FString> BodyTokens;
	EFClothingMorphPrivate::ExtractSemanticTokens(BodyMorphName.ToString(), BodyTokens);
	if (BodyTokens.Num() == 0)
	{
		return NAME_None;
	}

	const EEFClothingPiece ClothingPiece = ResolveClothingPieceForMesh(ClothingMesh);
	FName BestClothingMorphName = NAME_None;
	int32 BestScore = 0;

	for (const FName ClothingMorphName : ClothingMorphNames)
	{
		if (InOutUsedClothingMorphNames.Contains(ClothingMorphName))
		{
			continue;
		}

		TSet<FString> ClothingTokens;
		EFClothingMorphPrivate::ExtractSemanticTokens(ClothingMorphName.ToString(), ClothingTokens);
		if (ClothingTokens.Num() == 0)
		{
			continue;
		}

		int32 MatchScore = 0;
		for (const FString& BodyToken : BodyTokens)
		{
			for (const FString& ClothingToken : ClothingTokens)
			{
				if (BodyToken == ClothingToken)
				{
					MatchScore += 120;
				}
				else if (EFClothingMorphPrivate::AreTokensInSameRegion(BodyToken, ClothingToken))
				{
					MatchScore += 70;
				}
			}

			if (EFClothingMorphPrivate::TokenMatchesPiece(BodyToken, ClothingPiece))
			{
				MatchScore += 20;
			}
		}

		for (const FString& ClothingToken : ClothingTokens)
		{
			if (EFClothingMorphPrivate::TokenMatchesPiece(ClothingToken, ClothingPiece))
			{
				MatchScore += 25;
			}
		}

		if (MatchScore > BestScore)
		{
			BestScore = MatchScore;
			BestClothingMorphName = ClothingMorphName;
		}
	}

	const int32 RequiredScore = (ClothingPiece == EEFClothingPiece::Auto || ClothingPiece == EEFClothingPiece::Other) ? 120 : 90;
	if (!BestClothingMorphName.IsNone() && BestScore >= RequiredScore)
	{
		InOutUsedClothingMorphNames.Add(BestClothingMorphName);
		bOutUsedCompatibleMatch = true;
		return BestClothingMorphName;
	}

	return NAME_None;
}

float UEFClothingMorphComponent::ResolveScaleForMorph(const FName BodyMorphName, float BodyValue, const USkeletalMeshComponent* ClothingMesh) const
{
	float Scale = FMath::Max(BaseClothingScale, 0.0f) * ResolveMeshScaleMultiplier(ClothingMesh);
	const EEFClothingPiece ClothingPiece = ResolveClothingPieceForMesh(ClothingMesh);
	Scale *= ResolvePieceScaleMultiplier(ClothingPiece);
	const FString NormalizedMorphName = EFClothingMorphPrivate::NormalizeToken(BodyMorphName.ToString());
	float AdditiveBias = 0.0f;

	for (const FEFClothingMorphScaleRule& Rule : SpecialMorphScaleRules)
	{
		if (Rule.MatchToken.IsEmpty() || FMath::IsNearlyZero(Rule.Scale))
		{
			continue;
		}

		if (Rule.ClothingPiece != EEFClothingPiece::Auto && Rule.ClothingPiece != ClothingPiece)
		{
			continue;
		}

		if (!NormalizedMorphName.Contains(EFClothingMorphPrivate::NormalizeToken(Rule.MatchToken)))
		{
			continue;
		}

		if (FMath::Abs(BodyValue) < Rule.ActivationThreshold)
		{
			Scale *= Rule.Scale;
			continue;
		}

		Scale *= Rule.Scale;
		Scale *= BodyValue >= 0.0f ? Rule.PositiveScale : Rule.NegativeScale;
		AdditiveBias += BodyValue >= 0.0f ? Rule.AdditiveBias : -Rule.AdditiveBias;
	}

	if (BodyValue > 0.0f)
	{
		const EEFBodyRegion BodyRegion = ResolveBodyRegionForMorph(BodyMorphName);
		Scale *= ResolveRegionRiskMultiplier(BodyRegion, ClothingPiece);
		AdditiveBias += ResolveRegionRiskBias(BodyRegion, BodyValue, ClothingPiece);
	}

	return (BodyValue * Scale) + AdditiveBias;
}

float UEFClothingMorphComponent::ResolveMeshScaleMultiplier(const USkeletalMeshComponent* ClothingMesh) const
{
	if (!IsValid(ClothingMesh))
	{
		return 1.0f;
	}

	for (const FEFClothingMeshScaleOverride& Override : ClothingMeshScaleOverrides)
	{
		if (Override.MeshComponentName.IsNone())
		{
			continue;
		}

		if (ClothingMesh->GetFName() == Override.MeshComponentName || ClothingMesh->GetName().Equals(Override.MeshComponentName.ToString(), ESearchCase::IgnoreCase))
		{
			return FMath::Max(Override.Multiplier, 0.0f);
		}
	}

	return 1.0f;
}

float UEFClothingMorphComponent::ResolvePieceScaleMultiplier(const EEFClothingPiece ClothingPiece) const
{
	for (const FEFClothingPieceScaleOverride& Override : ClothingPieceScaleOverrides)
	{
		if (Override.ClothingPiece == ClothingPiece)
		{
			return FMath::Max(Override.Multiplier, 0.0f);
		}
	}

	return 1.0f;
}

EEFClothingPiece UEFClothingMorphComponent::ResolveClothingPieceForMesh(const USkeletalMeshComponent* ClothingMesh) const
{
	if (!IsValid(ClothingMesh))
	{
		return EEFClothingPiece::Other;
	}

	for (const FEFClothingMeshPieceOverride& Override : ClothingMeshPieceOverrides)
	{
		if (Override.MeshComponentName.IsNone())
		{
			continue;
		}

		if (ClothingMesh->GetFName() == Override.MeshComponentName || ClothingMesh->GetName().Equals(Override.MeshComponentName.ToString(), ESearchCase::IgnoreCase))
		{
			if (Override.ClothingPiece != EEFClothingPiece::Auto)
			{
				return Override.ClothingPiece;
			}

			break;
		}
	}

	if (const FGameplayTag* ACFUSlotTag = ACFUSlotTagsByMesh.Find(TWeakObjectPtr<USkeletalMeshComponent>(const_cast<USkeletalMeshComponent*>(ClothingMesh))))
	{
		for (const FEFACFUSlotPieceOverride& Override : ACFUSlotPieceOverrides)
		{
			if (Override.ClothingPiece != EEFClothingPiece::Auto && Override.SlotTag == *ACFUSlotTag)
			{
				return Override.ClothingPiece;
			}
		}

		return ResolveClothingPieceFromACFUSlot(*ACFUSlotTag);
	}

	const FString CombinedName = FString::Printf(TEXT("%s %s"),
		*ClothingMesh->GetName(),
		IsValid(ClothingMesh->GetSkeletalMeshAsset()) ? *ClothingMesh->GetSkeletalMeshAsset()->GetName() : TEXT(""));

	const EEFClothingPiece InferredPiece = EFClothingMorphPrivate::InferPieceFromName(CombinedName);
	return InferredPiece == EEFClothingPiece::Auto ? EEFClothingPiece::Other : InferredPiece;
}

bool UEFClothingMorphComponent::NeedsBindingRefresh() const
{
	const USkeletalMeshComponent* ResolvedBodyMesh = BodyMeshComponent.Get();
	if (!IsValid(ResolvedBodyMesh))
	{
		return true;
	}

	if (ResolvedBodyMesh->GetSkeletalMeshAsset() != CachedBodyMeshAsset.Get())
	{
		return true;
	}

	if (bPreferCharacterCustomizationComponent)
	{
		if (const UEFCharacterCustomizationComponent* CustomizationComponent = ResolveCharacterCustomizationComponent())
		{
			const USkeletalMeshComponent* CustomBodyMesh = CustomizationComponent->GetBodyMeshSelectionComponent();
			if (!IsValid(CustomBodyMesh))
			{
				CustomBodyMesh = CustomizationComponent->GetBodyMeshComponent();
			}

			if (IsValid(CustomBodyMesh) && CustomBodyMesh != ResolvedBodyMesh)
			{
				return true;
			}
		}
	}

	if (bEnableACFUIntegration)
	{
		if (ResolveACFUEquipmentComponent() != BoundACFUEquipmentComponent.Get())
		{
			return true;
		}
	}

	if (const AActor* Owner = GetOwner())
	{
		TArray<USkeletalMeshComponent*> CurrentMeshComponents;
		Owner->GetComponents(CurrentMeshComponents);

		for (USkeletalMeshComponent* MeshComponent : CurrentMeshComponents)
		{
			if (ShouldTrackAsClothing(MeshComponent) && !ClothingMeshComponents.Contains(MeshComponent))
			{
				return true;
			}
		}
	}

	if (ClothingMeshComponents.Num() != CachedClothingMeshAssets.Num())
	{
		return true;
	}

	for (USkeletalMeshComponent* ClothingMesh : ClothingMeshComponents)
	{
		if (!IsValid(ClothingMesh))
		{
			return true;
		}

		if (!ShouldTrackAsClothing(ClothingMesh))
		{
			return true;
		}

		const TWeakObjectPtr<USkeletalMesh>* CachedClothingAsset = CachedClothingMeshAssets.Find(ClothingMesh);
		if (CachedClothingAsset == nullptr || ClothingMesh->GetSkeletalMeshAsset() != CachedClothingAsset->Get())
		{
			return true;
		}
	}

	return false;
}

void UEFClothingMorphComponent::CacheResolvedMeshAssets()
{
	CachedBodyMeshAsset = BodyMeshComponent.IsValid() ? BodyMeshComponent->GetSkeletalMeshAsset() : nullptr;
	CachedClothingMeshAssets.Reset();

	for (USkeletalMeshComponent* ClothingMesh : ClothingMeshComponents)
	{
		if (IsValid(ClothingMesh))
		{
			CachedClothingMeshAssets.Add(ClothingMesh, ClothingMesh->GetSkeletalMeshAsset());
		}
	}
}

void UEFClothingMorphComponent::FinalizeMorphUpdate(USkeletalMeshComponent* MeshComponent) const
{
	if (!IsValid(MeshComponent))
	{
		return;
	}

	if (MeshComponent->bPauseAnims)
	{
		MeshComponent->TickAnimation(0.0f, false);
		MeshComponent->RefreshBoneTransforms();
	}

	MeshComponent->MarkRenderDynamicDataDirty();
}

void UEFClothingMorphComponent::ResetDebugState()
{
	ResolvedBodyMeshDebugName = NAME_None;
	ResolvedClothingMeshDebugNames.Reset();
	ResolvedClothingPieceDebug.Reset();
	ResolvedACFUEquipmentComponentDebugName = BoundACFUEquipmentComponent.IsValid() ? BoundACFUEquipmentComponent->GetFName() : NAME_None;
	ResolvedACFUSlotDebug.Reset();
	BodyRegionRiskDebug.Reset();
	ResolvedClothingBindingDebug.Reset();
	ResolvedClothingPiecesByMesh.Reset();
	ACFUSlotTagsByMesh.Reset();
	SmoothedRegionRiskByRegion.Reset();
	RegionMultiplierBonusByRegion.Reset();
	RegionBiasByRegion.Reset();
	LastResolvedBodyMorphCount = 0;
	LastCompatibleMatchCount = 0;
	LastAppliedMorphCount = 0;
	LastUpdatedClothingMeshCount = 0;
}

void UEFClothingMorphComponent::UpdateResolvedMeshDebugNames()
{
	ResolvedBodyMeshDebugName = BodyMeshComponent.IsValid() ? BodyMeshComponent->GetFName() : NAME_None;
	ResolvedClothingMeshDebugNames.Reset();
	ResolvedACFUSlotDebug.Reset();

	for (USkeletalMeshComponent* ClothingMesh : ClothingMeshComponents)
	{
		if (IsValid(ClothingMesh))
		{
			ResolvedClothingMeshDebugNames.AddUnique(ClothingMesh->GetFName());

			if (const FGameplayTag* ACFUSlotTag = ACFUSlotTagsByMesh.Find(TWeakObjectPtr<USkeletalMeshComponent>(ClothingMesh)))
			{
				ResolvedACFUSlotDebug.AddUnique(FString::Printf(TEXT("%s=%s"),
					*ClothingMesh->GetName(),
					*ACFUSlotTag->ToString()));
			}
		}
	}
}

UEFCharacterCustomizationComponent* UEFClothingMorphComponent::ResolveCharacterCustomizationComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UEFCharacterCustomizationComponent>() : nullptr;
}

USkeletalMeshComponent* UEFClothingMorphComponent::ResolveMeshComponentByName(FName MeshComponentName) const
{
	if (MeshComponentName.IsNone() || !IsValid(GetOwner()))
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	GetOwner()->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		if (MeshComponent->GetFName() == MeshComponentName || MeshComponent->GetName().Equals(MeshComponentName.ToString(), ESearchCase::IgnoreCase))
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

void UEFClothingMorphComponent::SyncACFUSlotLeaderPose()
{
	if (!bEnableACFUIntegration || !bUseACFUArmorSlotsAsClothing || !bApplyLeaderPoseToACFUArmorMeshes)
	{
		return;
	}

	USkeletalMeshComponent* ResolvedBodyMesh = BodyMeshComponent.Get();
	if (!IsValid(ResolvedBodyMesh) || !IsValid(ResolvedBodyMesh->GetSkeletalMeshAsset()))
	{
		return;
	}

	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FGameplayTag>& Pair : ACFUSlotTagsByMesh)
	{
		USkeletalMeshComponent* ArmorMesh = Pair.Key.Get();
		if (!IsValid(ArmorMesh) || ArmorMesh == ResolvedBodyMesh || !CanUseLeaderPose(ResolvedBodyMesh, ArmorMesh))
		{
			continue;
		}

		if (ArmorMesh->LeaderPoseComponent.Get() != ResolvedBodyMesh)
		{
			ArmorMesh->SetLeaderPoseComponent(ResolvedBodyMesh);
		}

		ArmorMesh->bUseBoundsFromLeaderPoseComponent = true;
	}
}

void UEFClothingMorphComponent::RefreshACFUIntegrationState()
{
	if (!bEnableACFUIntegration)
	{
		ResolvedACFUEquipmentComponentDebugName = NAME_None;
		UnbindFromACFUEquipmentEvents();
		BoundACFUEquipmentComponent = nullptr;
		return;
	}

	UActorComponent* ResolvedEquipmentComponent = ResolveACFUEquipmentComponent();
	ResolvedACFUEquipmentComponentDebugName = IsValid(ResolvedEquipmentComponent) ? ResolvedEquipmentComponent->GetFName() : NAME_None;

	if (ResolvedEquipmentComponent != BoundACFUEquipmentComponent.Get())
	{
		UnbindFromACFUEquipmentEvents();
		BoundACFUEquipmentComponent = ResolvedEquipmentComponent;
	}

	if (bBindToACFUEquipmentChanges && IsValid(BoundACFUEquipmentComponent.Get()) && !bACFUArmorChangedDelegateBound)
	{
		BindToACFUEquipmentEvents();
	}
	else if ((!bBindToACFUEquipmentChanges || !IsValid(BoundACFUEquipmentComponent.Get())) && bACFUArmorChangedDelegateBound)
	{
		UnbindFromACFUEquipmentEvents();
	}
}

void UEFClothingMorphComponent::ProcessPendingACFURefreshes()
{
	if (PendingACFURefreshes <= 0)
	{
		return;
	}

	const double CurrentTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (CurrentTimeSeconds < NextACFURefreshTimeSeconds)
	{
		return;
	}

	RefreshMeshBindings();
	ApplyMorphsNow();

	--PendingACFURefreshes;
	NextACFURefreshTimeSeconds = CurrentTimeSeconds + FMath::Max(ACFURefreshDelaySeconds, 0.0f);
}

bool UEFClothingMorphComponent::IsACFUArmorSlotComponent(const UActorComponent* Component) const
{
	if (!IsValid(Component))
	{
		return false;
	}

	if (UClass* ArmorSlotClass = ResolveACFUArmorSlotComponentClass())
	{
		return Component->IsA(ArmorSlotClass);
	}

	return Component->GetClass()->GetName().Contains(TEXT("ACFArmorSlotComponent"));
}

UActorComponent* UEFClothingMorphComponent::ResolveACFUEquipmentComponent() const
{
	if (!bEnableACFUIntegration || !IsValid(GetOwner()))
	{
		return nullptr;
	}

	UClass* EquipmentComponentClass = EFClothingMorphPrivate::ResolveClassByPath(EFClothingMorphPrivate::ACFUEquipmentComponentClassPath);
	for (UActorComponent* ActorComponent : GetOwner()->GetComponents())
	{
		if (!IsValid(ActorComponent))
		{
			continue;
		}

		if ((EquipmentComponentClass && ActorComponent->IsA(EquipmentComponentClass))
			|| ActorComponent->GetClass()->GetName().Contains(TEXT("ACFEquipmentComponent")))
		{
			return ActorComponent;
		}
	}

	return nullptr;
}

UClass* UEFClothingMorphComponent::ResolveACFUArmorSlotComponentClass() const
{
	return EFClothingMorphPrivate::ResolveClassByPath(EFClothingMorphPrivate::ACFUArmorSlotComponentClassPath);
}

void UEFClothingMorphComponent::ResolveACFUSlotMappings()
{
	ACFUSlotTagsByMesh.Reset();
	ResolvedACFUSlotDebug.Reset();

	if (!bEnableACFUIntegration || !bUseACFUArmorSlotsAsClothing || !IsValid(GetOwner()))
	{
		return;
	}

	TArray<UActorComponent*> ActorComponents;
	GetOwner()->GetComponents(ActorComponents);
	for (UActorComponent* ActorComponent : ActorComponents)
	{
		if (!IsACFUArmorSlotComponent(ActorComponent))
		{
			continue;
		}

		FGameplayTag SlotTag;
		if (!TryGetACFUSlotTag(ActorComponent, SlotTag))
		{
			continue;
		}

		USkeletalMeshComponent* RenderMesh = nullptr;
		if (!TryGetACFUSlotRenderMesh(ActorComponent, RenderMesh) || !IsValid(RenderMesh))
		{
			continue;
		}

		ACFUSlotTagsByMesh.Add(RenderMesh, SlotTag);
		ResolvedACFUSlotDebug.AddUnique(FString::Printf(TEXT("%s->%s=%s"),
			*ActorComponent->GetName(),
			*RenderMesh->GetName(),
			*SlotTag.ToString()));
	}
}

bool UEFClothingMorphComponent::TryGetACFUSlotRenderMesh(const UObject* ArmorSlotComponent, USkeletalMeshComponent*& OutRenderMesh) const
{
	OutRenderMesh = nullptr;

	if (!IsValid(ArmorSlotComponent))
	{
		return false;
	}

	if (const FObjectPropertyBase* MeshProperty = FindFProperty<FObjectPropertyBase>(ArmorSlotComponent->GetClass(), TEXT("skinnedArmor")))
	{
		if (UObject* MeshObject = MeshProperty->GetObjectPropertyValue_InContainer(ArmorSlotComponent))
		{
			OutRenderMesh = Cast<USkeletalMeshComponent>(MeshObject);
		}
	}

	if (IsValid(OutRenderMesh))
	{
		return true;
	}

	FGameplayTag SlotTag;
	if (!TryGetACFUSlotTag(ArmorSlotComponent, SlotTag))
	{
		return false;
	}

	UObject* EquipmentObject = BoundACFUEquipmentComponent.Get();
	if (!IsValid(EquipmentObject))
	{
		EquipmentObject = ResolveACFUEquipmentComponent();
	}

	if (!IsValid(EquipmentObject))
	{
		return false;
	}

	if (UFunction* GetModularMeshesFunction = EquipmentObject->FindFunction(TEXT("GetModularMeshes")))
	{
		FStructOnScope ParamsScope(GetModularMeshesFunction);
		void* Params = ParamsScope.GetStructMemory();
		if (Params == nullptr)
		{
			return false;
		}

		EquipmentObject->ProcessEvent(GetModularMeshesFunction, Params);

		if (FArrayProperty* ReturnProperty = FindFProperty<FArrayProperty>(GetModularMeshesFunction, TEXT("ReturnValue")))
		{
			if (FStructProperty* ModularPartProperty = CastField<FStructProperty>(ReturnProperty->Inner))
			{
				const FStructProperty* ItemSlotProperty = FindFProperty<FStructProperty>(ModularPartProperty->Struct, TEXT("ItemSlot"));
				const FObjectPropertyBase* MeshCompProperty = FindFProperty<FObjectPropertyBase>(ModularPartProperty->Struct, TEXT("meshComp"));
				if (ItemSlotProperty != nullptr
					&& ItemSlotProperty->Struct == FGameplayTag::StaticStruct()
					&& MeshCompProperty != nullptr)
				{
					void* ReturnValuePtr = ReturnProperty->ContainerPtrToValuePtr<void>(Params);
					FScriptArrayHelper ArrayHelper(ReturnProperty, ReturnValuePtr);
					for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
					{
						void* ElementPtr = ArrayHelper.GetRawPtr(Index);
						const FGameplayTag* ModularSlotTag = ItemSlotProperty->ContainerPtrToValuePtr<FGameplayTag>(ElementPtr);
						if (ModularSlotTag == nullptr || *ModularSlotTag != SlotTag)
						{
							continue;
						}

						if (UObject* MeshObject = MeshCompProperty->GetObjectPropertyValue_InContainer(ElementPtr))
						{
							OutRenderMesh = Cast<USkeletalMeshComponent>(MeshObject);
							if (IsValid(OutRenderMesh))
							{
								return true;
							}
						}
					}
				}
			}
		}
	}

	return IsValid(OutRenderMesh);
}

bool UEFClothingMorphComponent::CanUseLeaderPose(const USkeletalMeshComponent* LeaderMesh, const USkeletalMeshComponent* FollowerMesh) const
{
	if (!IsValid(LeaderMesh) || !IsValid(FollowerMesh) || LeaderMesh == FollowerMesh)
	{
		return false;
	}

	const USkeletalMesh* LeaderAsset = LeaderMesh->GetSkeletalMeshAsset();
	const USkeletalMesh* FollowerAsset = FollowerMesh->GetSkeletalMeshAsset();
	if (!IsValid(LeaderAsset) || !IsValid(FollowerAsset))
	{
		return false;
	}

	if (LeaderAsset->GetSkeleton() != nullptr && LeaderAsset->GetSkeleton() == FollowerAsset->GetSkeleton())
	{
		return true;
	}

	const FReferenceSkeleton& LeaderSkeleton = LeaderAsset->GetRefSkeleton();
	const FReferenceSkeleton& FollowerSkeleton = FollowerAsset->GetRefSkeleton();
	const int32 BoneCompareCount = FMath::Min(LeaderSkeleton.GetNum(), FollowerSkeleton.GetNum());
	if (BoneCompareCount <= 0)
	{
		return false;
	}

	for (int32 BoneIndex = 0; BoneIndex < BoneCompareCount; ++BoneIndex)
	{
		if (LeaderSkeleton.GetBoneName(BoneIndex) != FollowerSkeleton.GetBoneName(BoneIndex))
		{
			return false;
		}
	}

	return true;
}

bool UEFClothingMorphComponent::BindToACFUEquipmentEvents()
{
	UActorComponent* EquipmentComponent = BoundACFUEquipmentComponent.Get();
	if (!bEnableACFUIntegration || !bBindToACFUEquipmentChanges || !IsValid(EquipmentComponent))
	{
		return false;
	}

	FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(EquipmentComponent->GetClass(), TEXT("OnEquippedArmorChanged"));
	if (DelegateProperty == nullptr)
	{
		return false;
	}

	void* DelegateValue = DelegateProperty->ContainerPtrToValuePtr<void>(EquipmentComponent);
	FMulticastScriptDelegate DelegateCopy;
	if (const FMulticastScriptDelegate* ExistingDelegate = DelegateProperty->GetMulticastDelegate(DelegateValue))
	{
		DelegateCopy = *ExistingDelegate;
	}
	const FName HandlerName = GET_FUNCTION_NAME_CHECKED(UEFClothingMorphComponent, HandleACFUArmorSlotChanged);

	if (!DelegateCopy.Contains(this, HandlerName))
	{
		FScriptDelegate ScriptDelegate;
		ScriptDelegate.BindUFunction(this, HandlerName);
		DelegateCopy.AddUnique(ScriptDelegate);
	}

	DelegateProperty->SetMulticastDelegate(DelegateValue, MoveTemp(DelegateCopy));
	const FMulticastScriptDelegate* UpdatedDelegate = DelegateProperty->GetMulticastDelegate(DelegateValue);
	bACFUArmorChangedDelegateBound = UpdatedDelegate != nullptr && UpdatedDelegate->Contains(this, HandlerName);
	return bACFUArmorChangedDelegateBound;
}

void UEFClothingMorphComponent::UnbindFromACFUEquipmentEvents()
{
	if (UActorComponent* EquipmentComponent = BoundACFUEquipmentComponent.Get())
	{
		if (FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(EquipmentComponent->GetClass(), TEXT("OnEquippedArmorChanged")))
		{
			void* DelegateValue = DelegateProperty->ContainerPtrToValuePtr<void>(EquipmentComponent);
			FMulticastScriptDelegate DelegateCopy;
			if (const FMulticastScriptDelegate* ExistingDelegate = DelegateProperty->GetMulticastDelegate(DelegateValue))
			{
				DelegateCopy = *ExistingDelegate;
				DelegateCopy.Remove(this, GET_FUNCTION_NAME_CHECKED(UEFClothingMorphComponent, HandleACFUArmorSlotChanged));
				DelegateProperty->SetMulticastDelegate(DelegateValue, MoveTemp(DelegateCopy));
			}
		}
	}

	bACFUArmorChangedDelegateBound = false;
}

bool UEFClothingMorphComponent::TryGetACFUSlotTag(const UObject* ArmorSlotComponent, FGameplayTag& OutSlotTag) const
{
	OutSlotTag = FGameplayTag();

	if (!IsValid(ArmorSlotComponent))
	{
		return false;
	}

	if (UFunction* Function = ArmorSlotComponent->FindFunction(TEXT("GetSlotTag")))
	{
		struct FGetSlotTagParams
		{
			FGameplayTag ReturnValue;
		};

		FGetSlotTagParams Params;
		const_cast<UObject*>(ArmorSlotComponent)->ProcessEvent(Function, &Params);
		OutSlotTag = Params.ReturnValue;
		return OutSlotTag.IsValid();
	}

	if (const FStructProperty* SlotProperty = FindFProperty<FStructProperty>(ArmorSlotComponent->GetClass(), TEXT("ArmorSlot")))
	{
		if (SlotProperty->Struct == FGameplayTag::StaticStruct())
		{
			const FGameplayTag* SlotTagPtr = SlotProperty->ContainerPtrToValuePtr<FGameplayTag>(ArmorSlotComponent);
			if (SlotTagPtr != nullptr)
			{
				OutSlotTag = *SlotTagPtr;
				return OutSlotTag.IsValid();
			}
		}
	}

	return false;
}

EEFClothingPiece UEFClothingMorphComponent::ResolveClothingPieceFromACFUSlot(const FGameplayTag& SlotTag) const
{
	const FString NormalizedSlot = SlotTag.ToString().ToLower();
	if (NormalizedSlot.Contains(TEXT("helmet")) || NormalizedSlot.Contains(TEXT("head")))
	{
		return EEFClothingPiece::Head;
	}

	if (NormalizedSlot.Contains(TEXT("chest")))
	{
		return EEFClothingPiece::Chest;
	}

	if (NormalizedSlot.Contains(TEXT("gauntlet")) || NormalizedSlot.Contains(TEXT("glove")) || NormalizedSlot.Contains(TEXT("hand")))
	{
		return EEFClothingPiece::Gloves;
	}

	if (NormalizedSlot.Contains(TEXT("shoulder")) || NormalizedSlot.Contains(TEXT("arm")) || NormalizedSlot.Contains(TEXT("sleeve")) || NormalizedSlot.Contains(TEXT("bracer")))
	{
		return EEFClothingPiece::Arms;
	}

	if (NormalizedSlot.Contains(TEXT("boot")) || NormalizedSlot.Contains(TEXT("foot")))
	{
		return EEFClothingPiece::Boots;
	}

	if (NormalizedSlot.Contains(TEXT("leg")) || NormalizedSlot.Contains(TEXT("pant")) || NormalizedSlot.Contains(TEXT("thigh")) || NormalizedSlot.Contains(TEXT("loin")) || NormalizedSlot.Contains(TEXT("waist")) || NormalizedSlot.Contains(TEXT("pelvis")))
	{
		return EEFClothingPiece::Pants;
	}

	if (NormalizedSlot.Contains(TEXT("cape")))
	{
		return EEFClothingPiece::Other;
	}

	return EEFClothingPiece::Other;
}

void UEFClothingMorphComponent::HandleACFUArmorSlotChanged(FGameplayTag ArmorSlot)
{
	RefreshMeshBindings();
	ApplyMorphsNow();

	PendingACFURefreshes = FMath::Max(PendingACFURefreshes, FMath::Max(ACFURefreshRetryCount, 1));
	NextACFURefreshTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (ArmorSlot.IsValid())
	{
		ResolvedACFUSlotDebug.AddUnique(FString::Printf(TEXT("Changed=%s"), *ArmorSlot.ToString()));
	}
}

void UEFClothingMorphComponent::ConvertConfiguredMeshesToACFUBaseClothing()
{
	UE_LOG(LogTemp, Warning, TEXT("EF Clothing Morph on %s ignores legacy ACFU base clothing conversion. ACFU integration is passive and only mirrors real armor meshes."),
		*GetNameSafe(GetOwner()));
	RefreshMeshBindings();
	ApplyMorphsNow();
}

void UEFClothingMorphComponent::EquipConfiguredACFUClothing()
{
	UE_LOG(LogTemp, Warning, TEXT("EF Clothing Morph on %s ignores legacy ACFU quick-equip. ACFU remains the sole owner of inventory and equip flow."),
		*GetNameSafe(GetOwner()));
	RefreshMeshBindings();
	ApplyMorphsNow();
}

bool UEFClothingMorphComponent::EquipACFUClothingItemByClass(TSubclassOf<UObject> ItemClass, FGameplayTag PreferredSlotTag)
{
	UE_LOG(LogTemp, Warning, TEXT("EF Clothing Morph on %s ignores legacy direct ACFU equip requests. Use ACFU's own inventory and equip flow, then let EF Clothing Morph mirror the real armor meshes."),
		*GetNameSafe(GetOwner()));
	return false;
}
