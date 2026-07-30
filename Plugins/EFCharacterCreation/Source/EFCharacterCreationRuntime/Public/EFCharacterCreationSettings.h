#pragma once

#include "CoreMinimal.h"
#include "EFCharacterCreationTypes.h"
#include "Engine/DeveloperSettings.h"
#include "EFCharacterCreationSettings.generated.h"

class UUserWidget;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Character Creation"))
class EFCHARACTERCREATIONRUNTIME_API UEFCharacterCreationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFCharacterCreationSettings();

	static const UEFCharacterCreationSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	bool bAutoEnterTestingMap = true;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	FString TestingMapName = TEXT("TestingMap");

	UPROPERTY(EditAnywhere, Config, Category = "General")
	int32 MaxAutoEnterAttempts = 90;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	bool bAutoOpenOnCompatibleMainPawn = true;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	TArray<FString> AutoOpenMapNames;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	bool bLogCompatibilityFailures = true;

	UPROPERTY(EditAnywhere, Config, Category = "Compatibility")
	TArray<FString> DazPathTokens;

	UPROPERTY(EditAnywhere, Config, Category = "Compatibility")
	TArray<FString> DazMorphTokens;

	UPROPERTY(EditAnywhere, Config, Category = "Compatibility")
	int32 MinimumCompatibilityMorphMatches = 3;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FString> BodyMeshComponentHints;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FString> BodyMeshSelectionComponentHints;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FString> ClothingMeshComponentHints;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FString> HairComponentHints;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FString> HairMeshSearchPaths;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FString> ExcludedHairNameTokens;

	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<FCharacterSkeletalMeshOption> BodyMeshOptions;

	UPROPERTY(EditAnywhere, Config, Category = "Identity")
	FString DefaultCharacterName = TEXT("Player");

	UPROPERTY(EditAnywhere, Config, Category = "Identity", meta = (ClampMin = "1"))
	int32 MaxCharacterNameLength = 32;

	UPROPERTY(EditAnywhere, Config, Category = "Identity")
	ECharacterCreationGender DefaultGender = ECharacterCreationGender::Female;

	UPROPERTY(EditAnywhere, Config, Category = "Identity")
	TArray<FCharacterGenderMeshOption> GenderMeshOptions;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	TArray<FName> SkinColorParameterNames;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	TArray<FString> SkinMaterialHints;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	TArray<FName> IrisColorParameterNames;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	TArray<FString> IrisMaterialHints;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	FLinearColor DefaultSkinColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	FLinearColor DefaultIrisColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, Config, Category = "Presets")
	FString SaveSlotName = TEXT("CharacterCreationPresets");

	UPROPERTY(EditAnywhere, Config, Category = "Presets")
	int32 SaveUserIndex = 0;

	UPROPERTY(EditAnywhere, Config, Category = "UI")
	TArray<FCharacterCreationCategoryDefinition> Categories;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs")
	TArray<FMorphSliderEntry> MorphEntries;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs")
	bool bAutoGenerateEntriesFromMesh = true;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs")
	bool bIncludeAutoDiscoveredMorphs = true;

	UPROPERTY(EditAnywhere, Config, Category = "Camera")
	FCharacterCreationCameraSettings FullBodyCamera;

	UPROPERTY(EditAnywhere, Config, Category = "UI")
	TSoftClassPtr<UUserWidget> RootWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "UI")
	TSoftClassPtr<UUserWidget> MorphSliderWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|Host")
	FVector2D TattooHostSize = FVector2D(790.0f, 720.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|Host")
	FMargin TattooHostPadding = FMargin(0.0f, 0.0f, 0.0f, 12.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|TattooShop", meta = (DisplayName = "Offset From Center"))
	FVector2D TattooShopOffsetFromCenter = FVector2D(-530.0f, -300.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|TattooShop")
	FVector2D TattooShopSize = FVector2D(900.0f, 720.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|TattooShop")
	FVector2D TattooShopRenderScale = FVector2D(0.65f, 0.65f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|TattooShop")
	FVector2D TattooShopRenderTranslation = FVector2D(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|AssetPreviewer", meta = (DisplayName = "Offset From Center"))
	FVector2D TattooAssetPreviewerOffsetFromCenter = FVector2D(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|AssetPreviewer")
	bool bStackAssetPreviewerBelowTattooShop = true;

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|AssetPreviewer", meta = (EditCondition = "bStackAssetPreviewerBelowTattooShop"))
	float TattooAssetPreviewerGapBelowTattooShop = 0.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|AssetPreviewer")
	FVector2D TattooAssetPreviewerSize = FVector2D(1350.0f, 1000.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|AssetPreviewer")
	FVector2D TattooAssetPreviewerRenderScale = FVector2D(0.99f, 1.09f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|AssetPreviewer")
	FVector2D TattooAssetPreviewerRenderTranslation = FVector2D(-176.0f, -520.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Tattoo|Host")
	bool bClipTattooWidgetToHost = false;
};
