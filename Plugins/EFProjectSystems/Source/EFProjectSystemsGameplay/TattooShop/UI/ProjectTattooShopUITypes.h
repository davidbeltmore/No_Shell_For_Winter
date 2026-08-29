#pragma once

#include "CoreMinimal.h"
#include "TattooShop/ProjectAutomaticTattooTypes.h"
#include "ProjectTattooShopUITypes.generated.h"

class UTexture2D;

/** Visual source represented by a Tattoo Shop card. */
UENUM(BlueprintType)
enum class EProjectTattooShopCardKind : uint8
{
	Catalog UMETA(DisplayName = "Library"),
	Manual UMETA(DisplayName = "Applied"),
	Automatic UMETA(DisplayName = "Automatic (Read Only)")
};

/** One WBP is mounted twice by Character Creation, with a different role per host. */
UENUM(BlueprintType)
enum class EProjectTattooShopLibraryPresentationMode : uint8
{
	Management UMETA(DisplayName = "Management"),
	Catalog UMETA(DisplayName = "Catalog")
};

/**
 * UI-only card description. It deliberately contains no material or decal
 * ownership: the integration controller remains the single runtime owner.
 */
USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectTattooShopCardData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FName EntryId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FGuid TattooId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FText Subtitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	TSoftObjectPtr<UTexture2D> ThumbnailTexture;

	/** Supports uploaded PNGs that do not have a /Game asset path. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	TObjectPtr<UTexture2D> RuntimeTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	EProjectTattooShopCardKind Kind = EProjectTattooShopCardKind::Catalog;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FLinearColor AccentColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	bool bReadOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	bool bSelected = false;
};

/** Editable SkinnedDecal-facing fields exposed by the Character Creation tab. */
USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectTattooShopEditorModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FGuid TattooId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FName SourceEntryId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	EProjectTattooShopCardKind Kind = EProjectTattooShopCardKind::Manual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	EProjectAutomaticTattooPlacementPreset PlacementPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FName AnchorBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI", meta = (ClampMin = "-30.0", ClampMax = "30.0"))
	float OffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI", meta = (ClampMin = "-30.0", ClampMax = "30.0"))
	float OffsetY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float Size = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float ProjectionDistance = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	FLinearColor Color = FLinearColor::White;

	/** When false the source texture keeps its original RGB colors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	bool bTintEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	bool bEnabled = true;

	/** Automatic narrative tattoos are displayed but never editable here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo Shop|UI")
	bool bReadOnly = false;
};
