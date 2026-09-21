#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFCalystoDungeonDirectorPolicyV6.generated.h"

class AActor;
class UMaterialInstance;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;
#if WITH_EDITORONLY_DATA
class FObjectPreSaveContext;
#endif
struct FPropertyChangedEvent;
#if WITH_EDITOR
class FDataValidationContext;
enum class EDataValidationResult : uint8;
#endif

namespace EFCalystoIdentityHashSchemaV6
{
inline constexpr int32 CaseFoldedStableIds = 3;
}

UENUM(BlueprintType)
enum class EEFCalystoCatalogOverlayModeV6 : uint8
{
	Inherit UMETA(DisplayName = "Inherit Style"),
	Extend UMETA(DisplayName = "Extend Style"),
	Replace UMETA(DisplayName = "Replace Style"),
	Block UMETA(DisplayName = "Block Category")
};

UENUM(BlueprintType)
enum class EEFCalystoMaterialResolutionModeV6 : uint8
{
	InheritStyle UMETA(DisplayName = "Inherit Style"),
	Override UMETA(DisplayName = "Override")
};

UENUM(BlueprintType)
enum class EEFCalystoDecalResolutionModeV6 : uint8
{
	InheritStyle UMETA(DisplayName = "Inherit Style"),
	Replace UMETA(DisplayName = "Replace Style"),
	Block UMETA(DisplayName = "Block Decals")
};

UENUM(BlueprintType)
enum class EEFCalystoCatalogEntryRuleV6 : uint8
{
	Allow UMETA(DisplayName = "Allow"),
	Block UMETA(DisplayName = "Block")
};

UENUM(BlueprintType)
enum class EEFCalystoRarityTierV6 : uint8
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Winter
};

UENUM(BlueprintType)
enum class EEFCalystoGenderV6 : uint8
{
	Any,
	Female,
	Male
};

UENUM(BlueprintType)
enum class EEFCalystoLifecycleV6 : uint8
{
	FloorLocal UMETA(DisplayName = "Floor Local"),
	Recruitable
};

UENUM(BlueprintType, meta = (Bitflags))
enum class EEFCalystoRoomFlagsV6 : uint8
{
	None = 0 UMETA(Hidden),
	Start = 1 << 0,
	End = 1 << 1,
	Critical = 1 << 2,
	Progression = 1 << 3,
	MainPath = 1 << 4,
	DoorClearance = 1 << 5
};
ENUM_CLASS_FLAGS(EEFCalystoRoomFlagsV6);

UENUM(BlueprintType, meta = (Bitflags))
enum class EEFCalystoDecalSurfaceV6 : uint8
{
	None = 0 UMETA(Hidden),
	Floor = 1 << 0,
	Wall = 1 << 1,
	Roof = 1 << 2
};
ENUM_CLASS_FLAGS(EEFCalystoDecalSurfaceV6);

UENUM(BlueprintType)
enum class EEFCalystoLightingModeV6 : uint8
{
	Balanced,
	Warm,
	Cold,
	Dark
};

UENUM(BlueprintType)
enum class EEFCalystoArchitectureObjectTypeV6 : uint8
{
	StaticMesh UMETA(DisplayName = "Static Mesh"),
	ActorBlueprint UMETA(DisplayName = "Actor Blueprint"),
	LevelInstance UMETA(DisplayName = "Level Instance")
};

UENUM(BlueprintType)
enum class EEFCalystoArchitectureRotationV6 : uint8
{
	None,
	Degrees45 UMETA(DisplayName = "45 Degrees"),
	Degrees90 UMETA(DisplayName = "90 Degrees"),
	Full360 UMETA(DisplayName = "Full 360 Degrees")
};

UENUM(BlueprintType)
enum class EEFCalystoPlacementZoneV6 : uint8
{
	Floor,
	WallBottom UMETA(DisplayName = "Wall Bottom"),
	WallMiddle UMETA(DisplayName = "Wall Middle"),
	WallTop UMETA(DisplayName = "Wall Top"),
	CornerBottom UMETA(DisplayName = "Corner Bottom"),
	CornerMiddle UMETA(DisplayName = "Corner Middle"),
	CornerTop UMETA(DisplayName = "Corner Top"),
	Roof
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTierMixV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "0.90"))
	float Common = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "0.90"))
	float Uncommon = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "0.90"))
	float Rare = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "0.90"))
	float Epic = 0.02f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probability", meta = (DisplayName = "Nothing (Calculated)"))
	float Nothing = 0.10f;

	void RefreshNothing();
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTierCurveV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiers", meta = (DisplayName = "Floor 1"))
	FEFCalystoTierMixV6 AtFloor1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiers", meta = (DisplayName = "Floor 100"))
	FEFCalystoTierMixV6 AtFloor100;

	void RefreshNothing();
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoChanceCurveV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Chance at Floor 1"))
	float ChanceAtFloor1 = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Chance at Floor 100"))
	float ChanceAtFloor100 = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.1", ClampMax = "1000.0", DisplayName = "Progression Speed"))
	float Tau = 12.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoPertRangeV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range")
	float Minimum = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range")
	float Mode = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range")
	float Maximum = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "0.01", ClampMax = "100.0", AdvancedDisplay))
	float Shape = 4.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSurfaceMaterialSetV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6", DisplayName = "Floor Material"))
	TSoftObjectPtr<UMaterialInstance> FloorMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6", DisplayName = "Wall Material"))
	TSoftObjectPtr<UMaterialInstance> WallMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6", DisplayName = "Roof Material"))
	TSoftObjectPtr<UMaterialInstance> RoofMaterial;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoThemeMaterialPolicyV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Materials", meta = (DisplayName = "Floor Mode"))
	EEFCalystoMaterialResolutionModeV6 FloorMode = EEFCalystoMaterialResolutionModeV6::Override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Materials", meta = (DisplayName = "Wall Mode"))
	EEFCalystoMaterialResolutionModeV6 WallMode = EEFCalystoMaterialResolutionModeV6::Override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Materials", meta = (DisplayName = "Roof Mode"))
	EEFCalystoMaterialResolutionModeV6 RoofMode = EEFCalystoMaterialResolutionModeV6::Override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Materials", meta = (ShowOnlyInnerProperties, DisplayName = "Overrides"))
	FEFCalystoSurfaceMaterialSetV6 Overrides;
};

/** Exact, editor-friendly equivalent of Calysto's fixed object transform. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureTransformV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (DisplayName = "Uniform Scale"))
	bool bUniformScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (DisplayName = "Location Offset"))
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (DisplayName = "Rotation Offset"))
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector Scale = FVector::OneVector;
};

/** Exact, editor-friendly equivalent of Calysto's ranged room-object transform. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureVariationV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Location Minimum"))
	FVector LocationMinimum = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Location Maximum"))
	FVector LocationMaximum = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Rotation Minimum"))
	FRotator RotationMinimum = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Rotation Maximum"))
	FRotator RotationMaximum = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Uniform Scale"))
	bool bUniformScale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Scale Minimum"))
	FVector ScaleMinimum = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (DisplayName = "Scale Maximum"))
	FVector ScaleMaximum = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureMeshV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6"))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ClampMin = "0.0", DisplayName = "Selection Weight"))
	float SelectionWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties))
	FEFCalystoArchitectureTransformV6 Transform;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureActorV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "Actor Blueprint"))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ClampMin = "0.0", DisplayName = "Selection Weight"))
	float SelectionWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties))
	FEFCalystoArchitectureTransformV6 Transform;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureDoorwayV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "Wall Mesh"))
	TSoftObjectPtr<UStaticMesh> WallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "Frame Mesh"))
	TSoftObjectPtr<UStaticMesh> FrameMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "Door Blueprint"))
	TSoftClassPtr<AActor> DoorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ClampMin = "0.0", DisplayName = "Selection Weight"))
	float SelectionWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Transform"))
	FEFCalystoArchitectureTransformV6 WallTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Frame Transform"))
	FEFCalystoArchitectureTransformV6 FrameTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Door Transform"))
	FEFCalystoArchitectureTransformV6 DoorTransform;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureLightV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "Light Blueprint"))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ClampMin = "0", DisplayName = "Selection Weight"))
	int32 SelectionWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (DisplayName = "Placement Zone"))
	EEFCalystoPlacementZoneV6 PlacementZone = EEFCalystoPlacementZoneV6::WallMiddle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0", ClampMax = "25.0", DisplayName = "Position Jitter (cm)"))
	float PositionJitterCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties))
	FEFCalystoArchitectureVariationV6 Variation;
};

/** Inline equivalent of Calysto ST_ObjectDungeon. Only the selected object field is cooked. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureObjectV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	EEFCalystoArchitectureObjectTypeV6 Type = EEFCalystoArchitectureObjectTypeV6::StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6"))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6", DisplayName = "Actor Blueprint"))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6", AllowedClasses = "/Script/PCG.PCGDataAsset", DisplayName = "Level Instance"))
	TSoftObjectPtr<UObject> LevelInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ClampMin = "0", DisplayName = "Selection Weight"))
	int32 SelectionWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Rotation Mode"))
	EEFCalystoArchitectureRotationV6 Rotation = EEFCalystoArchitectureRotationV6::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties))
	FEFCalystoArchitectureVariationV6 Variation;
};

/** Floor-wide architecture owned directly by a Dungeon Style. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoStyleArchitectureV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Mesh"))
	TArray<FEFCalystoArchitectureMeshV6> Floor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Mesh"))
	TArray<FEFCalystoArchitectureMeshV6> Wall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Mesh"))
	TArray<FEFCalystoArchitectureMeshV6> Roof;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "WallMesh", DisplayName = "Wall Door"))
	TArray<FEFCalystoArchitectureDoorwayV6> WallDoor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Mesh", DisplayName = "Door Frame"))
	TArray<FEFCalystoArchitectureMeshV6> DoorFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "ActorClass"))
	TArray<FEFCalystoArchitectureActorV6> Door;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "Start Blueprint"))
	TSoftClassPtr<AActor> StartBlueprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoStyleV6", DisplayName = "End Blueprint"))
	TSoftClassPtr<AActor> EndBlueprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Mesh", DisplayName = "Ramp Top"))
	TArray<FEFCalystoArchitectureMeshV6> RampTop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Mesh", DisplayName = "Ramp Bottom"))
	TArray<FEFCalystoArchitectureMeshV6> RampBottom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "ActorClass", DisplayName = "Wall Lights"))
	TArray<FEFCalystoArchitectureLightV6> WallLights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Bottom Objects"))
	TArray<FEFCalystoArchitectureObjectV6> WallBottomObjects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Middle Objects"))
	TArray<FEFCalystoArchitectureObjectV6> WallMiddleObjects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Top Objects"))
	TArray<FEFCalystoArchitectureObjectV6> WallTopObjects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Roof Objects"))
	TArray<FEFCalystoArchitectureObjectV6> RoofObjects;
};

/** Per-room architecture owned directly by a Room Theme. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomArchitectureV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Bottom"))
	TArray<FEFCalystoArchitectureObjectV6> WallBottom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Middle"))
	TArray<FEFCalystoArchitectureObjectV6> WallMiddle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Wall Top"))
	TArray<FEFCalystoArchitectureObjectV6> WallTop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	TArray<FEFCalystoArchitectureObjectV6> Floor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Corner Bottom"))
	TArray<FEFCalystoArchitectureObjectV6> CornerBottom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Corner Middle"))
	TArray<FEFCalystoArchitectureObjectV6> CornerMiddle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Corner Top"))
	TArray<FEFCalystoArchitectureObjectV6> CornerTop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	TArray<FEFCalystoArchitectureObjectV6> Roof;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLayoutProfileV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "18.0", ClampMax = "30.0", DisplayName = "Dungeon Size"))
	FEFCalystoPertRangeV6 DungeonSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.20", ClampMax = "0.50", DisplayName = "Candidate Density"))
	FEFCalystoPertRangeV6 CandidateDensity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.30", ClampMax = "0.70", DisplayName = "Side Path Chance"))
	FEFCalystoPertRangeV6 SidePathChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "4", ClampMax = "8", DisplayName = "Minimum Room Size"))
	int32 MinimumRoomSize = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "4", ClampMax = "8", DisplayName = "Maximum Room Size"))
	int32 MaximumRoomSize = 8;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoThreatCurveV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat", meta = (ClampMin = "0.0", DisplayName = "Budget at Floor 1"))
	float BudgetAtFloor1 = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat", meta = (ClampMin = "0.0", DisplayName = "Budget at Floor 100"))
	float BudgetAtFloor100 = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat", meta = (ClampMin = "0.1", ClampMax = "1000.0", DisplayName = "Progression Speed"))
	float Tau = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat", meta = (ClampMin = "-100", ClampMax = "100", DisplayName = "Minimum Level Offset"))
	int32 MinimumLevelOffset = -2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat", meta = (ClampMin = "-100", ClampMax = "100", DisplayName = "Maximum Level Offset"))
	int32 MaximumLevelOffset = 2;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoContextTraitsV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Mystery = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Danger = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Safe = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Abundance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Clothing Influence"))
	float ClothingInfluence = 0.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLightingPolicyV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	EEFCalystoLightingModeV6 Mode = EEFCalystoLightingModeV6::Balanced;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float IntensityMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDecorationPolicyV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Density = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoration", meta = (ClampMin = "0", ClampMax = "64"))
	int32 MaximumDecorationsPerRoom = 4;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloorBudgetV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets", meta = (ClampMin = "0", ClampMax = "25"))
	int32 MaximumEnemies = 25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaximumLooseFood = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaximumChests = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets", meta = (ClampMin = "0", ClampMax = "4"))
	int32 MaximumLootActors = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets", meta = (ClampMin = "0", ClampMax = "4"))
	int32 MaximumSpecialEvents = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets", meta = (ClampMin = "0", ClampMax = "36"))
	int32 MaximumDirectorActors = 36;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCategoryLimitsV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0", ClampMax = "36", DisplayName = "Minimum When Present"))
	int32 MinimumWhenPresent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0", ClampMax = "36", DisplayName = "Maximum Per Floor"))
	int32 MaximumPerFloor = 1;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCatalogEntryV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	FName StableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	EEFCalystoCatalogEntryRuleV6 Rule = EEFCalystoCatalogEntryRuleV6::Allow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6"))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (DisplayName = "Placement Zone", ToolTip = "Approximate architectural surface used to place this actor. V6 resolves the nearest valid native Calysto candidate in this zone."))
	EEFCalystoPlacementZoneV6 PlacementZone = EEFCalystoPlacementZoneV6::Floor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0", ClampMax = "25.0", DisplayName = "Position Jitter (cm)", ToolTip = "Deterministic positional variation applied after selecting the architectural zone."))
	float PositionJitterCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	FName Archetype = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	EEFCalystoGenderV6 Gender = EEFCalystoGenderV6::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog", meta = (ClampMin = "0.0", DisplayName = "Base Threat Cost"))
	float BaseThreatCost = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog", meta = (ClampMin = "1"))
	int32 FirstEligibleFloor = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog", meta = (ClampMin = "1", ClampMax = "36"))
	int32 MaximumPerVariant = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog", meta = (ClampMin = "0"))
	int32 CooldownFloors = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog")
	EEFCalystoLifecycleV6 Lifecycle = EEFCalystoLifecycleV6::FloorLocal;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoChestContentEntryV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents")
	FName StableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents", meta = (AssetBundles = "CalystoStyleV6,CalystoThemeV6"))
	TSoftClassPtr<UObject> ContentClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents")
	EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents", meta = (ClampMin = "1"))
	int32 FirstEligibleFloor = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents", meta = (ClampMin = "0"))
	int32 CooldownFloors = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents", meta = (ClampMin = "1", ClampMax = "36"))
	int32 MaximumPerFloor = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Contents", meta = (DisplayName = "Requires Graveyard Eligibility"))
	bool bRequiresGraveyardEligibility = false;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCatalogOverlayV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	FName CategoryId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	EEFCalystoCatalogOverlayModeV6 Mode = EEFCalystoCatalogOverlayModeV6::Inherit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	FEFCalystoChanceCurveV6 Presence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	FEFCalystoTierCurveV6 Tiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	FEFCalystoCategoryLimitsV6 Limits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category", meta = (TitleProperty = "StableId"))
	TArray<FEFCalystoCatalogEntryV6> Catalog;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category", meta = (TitleProperty = "StableId", DisplayName = "Chest Contents Catalog"))
	TArray<FEFCalystoChestContentEntryV6> ChestContentsCatalog;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDecalVariantV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
	FName StableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (AssetBundles = "CalystoDecalsV6"))
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (AssetBundles = "CalystoDecalsV6", AdvancedDisplay))
	TSoftObjectPtr<UTexture2D> SourceColorTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (AssetBundles = "CalystoDecalsV6", AdvancedDisplay))
	TSoftObjectPtr<UTexture2D> SourceNormalTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (Bitmask, BitmaskEnum = "/Script/EFProceduralRuntime.EEFCalystoDecalSurfaceV6", DisplayName = "Allowed Surfaces"))
	int32 AllowedSurfaces = static_cast<int32>(EEFCalystoDecalSurfaceV6::Wall);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDecalProfileV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals")
	EEFCalystoDecalResolutionModeV6 Mode = EEFCalystoDecalResolutionModeV6::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Chance Per Eligible Room"))
	float ChancePerEligibleRoom = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (Bitmask, BitmaskEnum = "/Script/EFProceduralRuntime.EEFCalystoDecalSurfaceV6", DisplayName = "Allowed Surfaces"))
	int32 AllowedSurfaces = static_cast<int32>(EEFCalystoDecalSurfaceV6::Floor) |
		static_cast<int32>(EEFCalystoDecalSurfaceV6::Wall) |
		static_cast<int32>(EEFCalystoDecalSurfaceV6::Roof);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "24"))
	int32 MaximumPerRoom = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "24"))
	int32 MaximumActivePerFloor = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "24"))
	int32 FloorLimit = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "24"))
	int32 WallLimit = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "24"))
	int32 RoofLimit = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "1.0", DisplayName = "Minimum Size (cm)"))
	float MinimumSizeCm = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "1.0", DisplayName = "Maximum Size (cm)"))
	float MaximumSizeCm = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "1.0", DisplayName = "Fade Start Distance (cm)"))
	float FadeStartDistanceCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "1.0", DisplayName = "Cull Distance (cm)"))
	float CullDistanceCm = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (TitleProperty = "StableId"))
	TArray<FEFCalystoDecalVariantV6> Catalog;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoStyleProfileV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	FString EditorSummary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (DisplayName = "Style ID"))
	FName StyleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", DisplayName = "Selection Weight"))
	float SelectionWeight = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probability", meta = (DisplayName = "Normalized Floor Probability"))
	float NormalizedSelectionProbability = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Room Theme Chance", ToolTip = "Independent probability that each ordinary eligible room receives a Room Theme. The value is frozen once for the selected floor."))
	float RoomThemeChance = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FEFCalystoLayoutProfileV6 Layout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat")
	FEFCalystoThreatCurveV6 Threat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budgets")
	FEFCalystoFloorBudgetV6 GlobalBudgets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits")
	FEFCalystoContextTraitsV6 Traits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	FEFCalystoLightingPolicyV6 Lighting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoration")
	FEFCalystoDecorationPolicyV6 Decoration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (TitleProperty = "CategoryId"))
	TArray<FEFCalystoCatalogOverlayV6> Catalogs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon Materials", meta = (ShowOnlyInnerProperties, DisplayName = "Style Materials"))
	FEFCalystoSurfaceMaterialSetV6 DungeonMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties, DisplayName = "Style Architecture"))
	FEFCalystoStyleArchitectureV6 Architecture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ShowOnlyInnerProperties))
	FEFCalystoDecalProfileV6 Decals;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomThemeProfileV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Summary")
	FString EditorSummary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (DisplayName = "Theme ID"))
	FName ThemeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (MultiLine = "true"))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FLinearColor PreviewColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", DisplayName = "Selection Weight"))
	float SelectionWeight = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probability", meta = (DisplayName = "Conditional Theme Probability"))
	float ConditionalSelectionProbability = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probability", meta = (DisplayName = "Overall Eligible Room Probability"))
	float OverallEligibleRoomProbability = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	FEFCalystoContextTraitsV6 Traits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (TitleProperty = "CategoryId", DisplayName = "Catalog Overlays"))
	TArray<FEFCalystoCatalogOverlayV6> Catalogs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Materials", meta = (ShowOnlyInnerProperties))
	FEFCalystoThemeMaterialPolicyV6 RoomMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties, DisplayName = "Room Architecture"))
	FEFCalystoRoomArchitectureV6 Architecture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ShowOnlyInnerProperties))
	FEFCalystoDecalProfileV6 Decals;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoPerformanceSafetyV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "18", ClampMax = "30"))
	TArray<int32> ValidatedDungeonSizes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FEFCalystoFloorBudgetV6 HardCeilings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "1.0", ClampMax = "100.0", DisplayName = "Room Identity Quantization (cm)"))
	float RoomIdentityQuantizationCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decals", meta = (ClampMin = "1", ClampMax = "24", DisplayName = "Decal Component Pool Capacity", ToolTip = "Hard upper bound for the one shared, non-ticking decal component pool. V6 never permits more than 24 pooled components."))
	int32 DecalComponentPoolCapacity = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "1", ClampMax = "10000", DisplayName = "Maximum Room Records"))
	int32 MaximumRoomRecords = 2048;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoThemeProbabilityPreviewV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	FName ThemeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	float ConditionalProbability = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	float EligibleRoomProbability = 0.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoStyleProbabilityPreviewV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	float FloorProbability = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	float ThemeRoomChance = 0.25f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	float NoThemeProbability = 0.75f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview", meta = (TitleProperty = "ThemeId"))
	TArray<FEFCalystoThemeProbabilityPreviewV6> Themes;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedThemeProfileV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FName ThemeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	float SelectionWeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FEFCalystoRoomArchitectureV6 Architecture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FString ArchitectureHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme", meta = (DisplayName = "Floor Material Mode"))
	EEFCalystoMaterialResolutionModeV6 FloorMaterialMode = EEFCalystoMaterialResolutionModeV6::InheritStyle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme", meta = (DisplayName = "Wall Material Mode"))
	EEFCalystoMaterialResolutionModeV6 WallMaterialMode = EEFCalystoMaterialResolutionModeV6::InheritStyle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme", meta = (DisplayName = "Roof Material Mode"))
	EEFCalystoMaterialResolutionModeV6 RoofMaterialMode = EEFCalystoMaterialResolutionModeV6::InheritStyle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FEFCalystoSurfaceMaterialSetV6 EffectiveMaterials;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	TArray<FEFCalystoCatalogOverlayV6> Catalogs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FEFCalystoDecalProfileV6 Decals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FString CatalogHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FString MaterialHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resolved Theme")
	FString DecalHash;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedFloorPlanV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	int64 FloorSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	float ThemeRoomChance = 0.25f;

	/** Frozen with the floor so PCG extraction and manifest construction cannot diverge after an editor-side policy change. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan", meta = (DisplayName = "Room Identity Quantization (cm)"))
	float RoomIdentityQuantizationCm = 10.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan", meta = (DisplayName = "Maximum Room Records"))
	int32 MaximumRoomRecords = 2048;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan", meta = (DisplayName = "Decal Component Pool Capacity"))
	int32 DecalComponentPoolCapacity = 24;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FEFCalystoLayoutProfileV6 Layout;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FEFCalystoThreatCurveV6 Threat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FEFCalystoFloorBudgetV6 GlobalBudgets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FEFCalystoSurfaceMaterialSetV6 StyleMaterials;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FEFCalystoStyleArchitectureV6 StyleArchitecture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FString StyleArchitectureHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	TArray<FEFCalystoCatalogOverlayV6> StyleCatalogs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FEFCalystoDecalProfileV6 StyleDecals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FString StyleCatalogHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FString StyleMaterialHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FString StyleDecalHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan", meta = (TitleProperty = "ThemeId"))
	TArray<FEFCalystoResolvedThemeProfileV6> Themes;

	/** Vose alias table frozen once per floor; room Theme selection is O(1). */
	TArray<float> ThemeAliasProbability;
	TArray<int32> ThemeAliasIndex;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	TArray<FSoftObjectPath> ReachableVisualPreloadPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FString PolicyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Plan")
	FString FloorPlanHash;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomIdentityInputV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Identity")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Identity")
	FVector Extents = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Identity")
	FName TopologyKind = TEXT("Ordinary");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Identity", meta = (ClampMin = "0"))
	int32 CollisionOrdinal = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Identity", meta = (Bitmask, BitmaskEnum = "/Script/EFProceduralRuntime.EEFCalystoRoomFlagsV6"))
	int32 RoomFlags = 0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomContextV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	int64 StableRoomId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FVector Extents = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FName TopologyKind = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	int32 CollisionOrdinal = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room", meta = (Bitmask, BitmaskEnum = "/Script/EFProceduralRuntime.EEFCalystoRoomFlagsV6"))
	int32 RoomFlags = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	bool bIsThemed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FName ThemeId = TEXT("NoTheme");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FString ArchitectureHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FEFCalystoSurfaceMaterialSetV6 EffectiveMaterials;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FString CatalogHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FString DecalHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FString RoomContextHash;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomManifestV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int64 FloorSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString FloorPlanHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int32 EligibleRoomCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int32 ThemedRoomCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest", meta = (TitleProperty = "StableRoomId"))
	TArray<FEFCalystoRoomContextV6> Rooms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString ManifestHash;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonDirectorMathV6
{
	static FString HashCanonicalText(const FString& Text);
	static int64 BuildStableRoomId(int64 FloorSeed, const FEFCalystoRoomIdentityInputV6& Input, float QuantizationCm);
	static bool IsProtectedRoom(int32 RoomFlags);
	static double ThemePresenceUniform(int64 FloorSeed, int64 StableRoomId, FName StyleId);
	static double ThemeGuaranteeUniform(int64 FloorSeed, int64 StableRoomId, FName StyleId);
	static double ThemeTypeUniform(int64 FloorSeed, int64 StableRoomId, FName StyleId);
	/** Selects one order-independent eligible room for the floor's minimum Theme guarantee. */
	static int64 SelectGuaranteedThemeRoomId(
		int64 FloorSeed,
		FName StyleId,
		const TArray<int64>& EligibleStableRoomIds);
	static bool ResolveCatalogs(const TArray<FEFCalystoCatalogOverlayV6>& StyleCatalogs,
		const TArray<FEFCalystoCatalogOverlayV6>& ThemeOverlays,
		TArray<FEFCalystoCatalogOverlayV6>& OutCatalogs, FString& OutError);
	static FEFCalystoDecalProfileV6 ResolveDecals(
		const FEFCalystoDecalProfileV6& StyleDecals,
		const FEFCalystoDecalProfileV6& ThemeDecals);
	static bool ResolveRoomContext(
		const FEFCalystoResolvedFloorPlanV6& Plan,
		int64 StableRoomId,
		int32 RoomFlags,
		FEFCalystoRoomContextV6& OutContext,
		FString& OutError,
		bool bForceThemePresence = false);
};

UCLASS(BlueprintType, meta = (DisplayName = "Calysto Dungeon Director Policy V6"))
class EFPROCEDURALRUNTIME_API UEFCalystoDungeonDirectorPolicyV6Asset final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 HashSchemaVersion = 3;
	static const FName NoThemeId;

	UEFCalystoDungeonDirectorPolicyV6Asset();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity")
	int32 SchemaVersion = 6;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity")
	int32 RuntimeGeneratorVersion = 6;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity")
	int32 IdentityHashSchemaVersion = HashSchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity", meta = (DisplayName = "Policy ID"))
	FName PolicyId = TEXT("CalystoDungeonDirectorV6");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity", meta = (DisplayName = "Migration Source Hash", AdvancedDisplay))
	FString MigrationSourceHash;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 Dungeon Styles", meta = (TitleProperty = "EditorSummary", DisplayName = "Dungeon Styles"))
	TArray<FEFCalystoStyleProfileV6> Styles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 Room Themes", meta = (TitleProperty = "EditorSummary", DisplayName = "Room Themes", ToolTip = "Only authored Themes belong here. NoTheme is an internal deterministic result and must never be added."))
	TArray<FEFCalystoRoomThemeProfileV6> RoomThemes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 Performance & Safety", meta = (ShowOnlyInnerProperties))
	FEFCalystoPerformanceSafetyV6 PerformanceAndSafety;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "05 Probability Preview", meta = (TitleProperty = "StyleId"))
	TArray<FEFCalystoStyleProbabilityPreviewV6> ProbabilityPreview;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "06 Cook Validation", meta = (AssetBundles = "CalystoFloorV6", AdvancedDisplay, DisplayName = "Cook Bundle References"))
	TArray<TSoftObjectPtr<UObject>> CookBundleReferences;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|V6", meta = (DevelopmentOnly, DisplayName = "Initialize V6 Defaults"))
	void InitializeV6Defaults();

	bool Validate(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DisplayName = "Validate V6 Policy"))
	bool ValidatePolicy() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DisplayName = "Get V6 Gameplay Hash"))
	FString GetGameplayHash() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DisplayName = "Get V6 Authoring Hash"))
	FString GetAuthoringHash() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DisplayName = "Get V6 Material Hash"))
	FString GetMaterialHash() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DisplayName = "Get V6 Decal Hash"))
	FString GetDecalHash() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DisplayName = "Get V6 Probability Preview"))
	TArray<FEFCalystoStyleProbabilityPreviewV6> GetProbabilityPreview() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DevelopmentOnly, DisplayName = "Get V6 Cook Bundle Names"))
	TArray<FName> GetCookBundleNames() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6", meta = (DevelopmentOnly, DisplayName = "Get V6 Cook Bundle Asset Paths"))
	TArray<FString> GetCookBundleAssetPaths() const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|V6")
	bool BuildResolvedFloorPlan(int64 FloorSeed, FEFCalystoResolvedFloorPlanV6& OutPlan, FString& OutError) const;

	/** Freezes a floor around a Style already selected by the run ecology. */
	bool BuildResolvedFloorPlanForStyle(int64 FloorSeed, FName StyleId,
		FEFCalystoResolvedFloorPlanV6& OutPlan, FString& OutError) const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|V6")
	bool BuildRoomManifest(const FEFCalystoResolvedFloorPlanV6& Plan, const TArray<FEFCalystoRoomIdentityInputV6>& RoomInputs, FEFCalystoRoomManifestV6& OutManifest, FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Streaming")
	bool GatherReachableVisualPreloadPaths(const FEFCalystoResolvedFloorPlanV6& Plan, TArray<FSoftObjectPath>& OutPaths, FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Streaming")
	bool GatherPostTopologyContentPaths(const FEFCalystoResolvedFloorPlanV6& Plan, const FEFCalystoRoomManifestV6& Manifest, TArray<FSoftObjectPath>& OutPaths, FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Streaming")
	bool GatherPostTopologyDecalPaths(const FEFCalystoResolvedFloorPlanV6& Plan, const FEFCalystoRoomManifestV6& Manifest, TArray<FSoftObjectPath>& OutPaths, FString& OutError) const;

	const FEFCalystoStyleProfileV6* FindStyle(FName StyleId) const;
	const FEFCalystoRoomThemeProfileV6* FindRoomTheme(FName ThemeId) const;

	virtual void PostLoad() override;
#if WITH_EDITORONLY_DATA
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
#endif
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	void RefreshDerivedData();
	void RebuildCookBundleReferences();
	FString BuildGameplayCanonicalString() const;
	FString BuildAuthoringCanonicalString() const;
	FString BuildMaterialCanonicalString() const;
	FString BuildDecalCanonicalString() const;

	UPROPERTY(Transient)
	TArray<int32> CompiledStyleSourceIndices;

	UPROPERTY(Transient)
	TArray<float> CompiledStyleAliasProbability;

	UPROPERTY(Transient)
	TArray<int32> CompiledStyleAliasIndex;

	UPROPERTY(Transient)
	TArray<int32> CompiledThemeSourceIndices;

	UPROPERTY(Transient)
	TArray<float> CompiledThemeAliasProbability;

	UPROPERTY(Transient)
	TArray<int32> CompiledThemeAliasIndex;
};
