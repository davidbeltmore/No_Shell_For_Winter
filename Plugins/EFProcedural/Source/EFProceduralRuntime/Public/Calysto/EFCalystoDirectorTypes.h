#pragma once

#include "CoreMinimal.h"
#include "EFCalystoDirectorTypes.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class EEFCalystoDistribution : uint8 { Fixed, Uniform, Triangular };

UENUM(BlueprintType)
enum class EEFCalystoContentMode : uint8 { Inherit, Extend, Replace, Block };

UENUM(BlueprintType)
enum class EEFCalystoRarity : uint8 { Common, Uncommon, Rare, Epic, Winter };

UENUM(BlueprintType)
enum class EEFCalystoGender : uint8 { Any, Female, Male };

UENUM(BlueprintType)
enum class EEFCalystoLifecycle : uint8 { FloorLocal, Recruitable };

/** Gameplay dispatch never depends on a display name or entry identifier. */
UENUM(BlueprintType)
enum class EEFCalystoGameplayRole : uint8
{
	Enemy, SupportNPC, Armor, LooseLoot, Food, Drink, Container, ContainerContent, Prop, SpecialEvent
};

UENUM(BlueprintType)
enum class EEFCalystoBudgetMembership : uint8
{
	None, Enemy, LooseFood, Chest, LootActor, SpecialEvent
};

UENUM(BlueprintType)
enum class EEFCalystoPlacementZone : uint8
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

UENUM(BlueprintType)
enum class EEFCalystoArchitecturePayload : uint8
{
	Mesh, Actor, BakedPCG UMETA(DisplayName = "Baked PCG / Level Instance"), Empty
};

UENUM(BlueprintType)
enum class EEFCalystoArchitectureRotation : uint8
{
	None, Degrees45 UMETA(DisplayName = "45 Degrees"),
	Degrees90 UMETA(DisplayName = "90 Degrees"), Full360 UMETA(DisplayName = "Full 360 Degrees")
};

UENUM(BlueprintType)
enum class EEFCalystoMaterialMode : uint8 { Inherit, Override };

UENUM(BlueprintType)
enum class EEFCalystoDecalMode : uint8 { Inherit, Replace, Block };

UENUM(BlueprintType, meta = (Bitflags))
enum class EEFCalystoProtectedRoom : uint8
{
	None = 0 UMETA(Hidden), Start = 1, End = 2, Critical = 4, Progression = 8
};
ENUM_CLASS_FLAGS(EEFCalystoProtectedRoom);

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloatDistribution
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	EEFCalystoDistribution Distribution = EEFCalystoDistribution::Fixed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution", meta = (EditCondition = "Distribution == EEFCalystoDistribution::Fixed", EditConditionHides))
	double Value = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution", meta = (EditCondition = "Distribution != EEFCalystoDistribution::Fixed", EditConditionHides))
	double Minimum = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution", meta = (EditCondition = "Distribution == EEFCalystoDistribution::Triangular", EditConditionHides, ToolTip = "Most likely value of a continuous triangular distribution."))
	double Mode = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution", meta = (EditCondition = "Distribution != EEFCalystoDistribution::Fixed", EditConditionHides))
	double Maximum = 1.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoAmountDistribution
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount")
	EEFCalystoDistribution Distribution = EEFCalystoDistribution::Fixed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount", meta = (ClampMin = "1", ClampMax = "1024", EditCondition = "Distribution == EEFCalystoDistribution::Fixed", EditConditionHides))
	int32 Amount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount", meta = (ClampMin = "1", ClampMax = "1024", EditCondition = "Distribution != EEFCalystoDistribution::Fixed", EditConditionHides))
	int32 Minimum = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount", meta = (ClampMin = "1", ClampMax = "1024", EditCondition = "Distribution == EEFCalystoDistribution::Triangular", EditConditionHides, ToolTip = "Peak of a continuous triangular distribution rounded to the nearest integer. Feasibility conditions these exact count probabilities."))
	int32 Mode = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount", meta = (ClampMin = "1", ClampMax = "1024", EditCondition = "Distribution != EEFCalystoDistribution::Fixed", EditConditionHides))
	int32 Maximum = 1;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoPercentageCurve
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (ClampMin = "1"))
	int32 FirstFloor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (ClampMin = "1"))
	int32 LastFloor = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (ClampMin = "0", ClampMax = "100", Units = "Percent", DisplayName = "Chance at First Floor", ToolTip = "Percentage from 0 to 100. Linear interpolation between authored floor endpoints."))
	double FirstPercent = 25.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (ClampMin = "0", ClampMax = "100", Units = "Percent", DisplayName = "Chance at Last Floor"))
	double LastPercent = 25.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoScalarCurve
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1"))
	int32 FirstFloor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1"))
	int32 LastFloor = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth")
	double FirstValue = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth")
	double LastValue = 1.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRarityWeights
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "0"))
	double Common = 60.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "0"))
	double Uncommon = 20.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "0"))
	double Rare = 8.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "0"))
	double Epic = 2.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "0"))
	double Winter = 1.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRarityCurve
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "1"))
	int32 FirstFloor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity", meta = (ClampMin = "1"))
	int32 LastFloor = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity")
	FEFCalystoRarityWeights FirstWeights;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rarity")
	FEFCalystoRarityWeights LastWeights;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSelection
{
	GENERATED_BODY()
	/** Persisted editor identity; never inferred from DisplayName. */
	UPROPERTY()
	FGuid Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (DisplayName = "Name"))
	FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ClampMin = "0", ToolTip = "Relative weight among eligible enabled alternatives. Zero cannot be selected."))
	double Weight = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ClampMin = "1"))
	int32 FirstEligibleFloor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ClampMin = "0", ToolTip = "Zero has no upper depth limit."))
	int32 LastEligibleFloor = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ClampMin = "0"))
	int32 CooldownFloors = 0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoPlacement
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EEFCalystoPlacementZone Zone = EEFCalystoPlacementZone::Floor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0", Units = "cm"))
	FVector FootprintHalfExtent = FVector(25.0, 25.0, 50.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0", Units = "cm"))
	double Spacing = 50.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0", Units = "cm"))
	double Clearance = 5.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0", Units = "cm", DisplayName = "Position Variation"))
	double PositionVariationCm = 2.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bRequiresNavigation = false;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSurfaceMaterials
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (AssetBundles = "CalystoVisuals", DisplayName = "Floor Material"))
	TSoftObjectPtr<UMaterialInterface> Floor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (AssetBundles = "CalystoVisuals", DisplayName = "Wall Material"))
	TSoftObjectPtr<UMaterialInterface> Wall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (AssetBundles = "CalystoVisuals", DisplayName = "Roof Material"))
	TSoftObjectPtr<UMaterialInterface> Roof;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoMaterialOverride
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	EEFCalystoMaterialMode Mode = EEFCalystoMaterialMode::Inherit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (EditCondition = "Mode == EEFCalystoMaterialMode::Override", EditConditionHides, AssetBundles = "CalystoVisuals"))
	TSoftObjectPtr<UMaterialInterface> Material;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomMaterials
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Materials")
	FEFCalystoMaterialOverride Floor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Materials")
	FEFCalystoMaterialOverride Wall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Materials")
	FEFCalystoMaterialOverride Roof;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureTransform
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	bool bUniformScale = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector LocationOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FRotator RotationOffset = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector Scale = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureVariation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	FVector LocationMinimum = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	FVector LocationMaximum = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	FRotator RotationMinimum = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	FRotator RotationMaximum = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	bool bUniformScale = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	FVector ScaleMinimum = FVector::OneVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	FVector ScaleMaximum = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FEFCalystoSelection Selection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	EEFCalystoArchitecturePayload Payload = EEFCalystoArchitecturePayload::Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (EditCondition = "Payload == EEFCalystoArchitecturePayload::Mesh", EditConditionHides, AssetBundles = "CalystoVisuals"))
	TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (EditCondition = "Payload == EEFCalystoArchitecturePayload::Actor", EditConditionHides, AssetBundles = "CalystoVisuals"))
	TSoftClassPtr<AActor> ActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (EditCondition = "Payload == EEFCalystoArchitecturePayload::BakedPCG", EditConditionHides, AllowedClasses = "/Script/PCG.PCGDataAsset", AssetBundles = "CalystoVisuals"))
	TSoftObjectPtr<UObject> BakedPCG;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (EditCondition = "Payload != EEFCalystoArchitecturePayload::Empty", EditConditionHides))
	FEFCalystoArchitectureTransform Transform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (EditCondition = "Payload != EEFCalystoArchitecturePayload::Empty", EditConditionHides))
	FEFCalystoArchitectureVariation Variation;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (EditCondition = "Payload != EEFCalystoArchitecturePayload::Empty", EditConditionHides))
	EEFCalystoArchitectureRotation Rotation = EEFCalystoArchitectureRotation::None;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDoorway
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FEFCalystoSelection Selection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoVisuals"))
	TSoftObjectPtr<UStaticMesh> WallMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoVisuals"))
	TSoftObjectPtr<UStaticMesh> FrameMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoVisuals"))
	TSoftClassPtr<AActor> DoorClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	FEFCalystoArchitectureTransform WallTransform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	FEFCalystoArchitectureTransform FrameTransform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture")
	FEFCalystoArchitectureTransform DoorTransform;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSurfaceDecoration
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EEFCalystoPlacementZone Zone = EEFCalystoPlacementZone::Floor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (ToolTip = "Use this zone's Chance instead of the selected Style's default optional-decoration Chance. This is one roll, not an additional roll."))
	bool bOverrideDefaultChance = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (EditCondition = "bOverrideDefaultChance", EditConditionHides))
	FEFCalystoPercentageCurve Chance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> Alternatives;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoStyleArchitecture
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ClampMin = "0", ClampMax = "256", ToolTip = "Shared room capacity for optional decorations across all eight zones, including Theme decoration. Required structure and native lights do not consume this capacity."))
	int32 MaximumDecorationsPerRoom = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (DisplayName = "Default Optional Decoration Chance", ToolTip = "Chance per feasible native optional-decoration opportunity. A zone may explicitly override it; no second Chance roll is introduced."))
	FEFCalystoPercentageCurve DecorationChance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> Floor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> Wall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> Roof;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoDoorway> Doorways;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> DoorFrames;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> Doors;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> RampTop;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> RampBottom;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Zone"))
	TArray<FEFCalystoSurfaceDecoration> Decoration;
	/** Travel integration owns progression actors; this changes supported mesh appearance only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (AssetBundles = "CalystoVisuals", DisplayName = "Floor Door Appearance", ToolTip = "Upright door mesh: height along Z, width along X. Centered on the End marker with its base at floor level and its original size."))
	TSoftObjectPtr<UStaticMesh> ProgressionDoorMesh;
	FEFCalystoStyleArchitecture() { DecorationChance.FirstPercent = DecorationChance.LastPercent = 35.0; }
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLayout
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FEFCalystoFloatDistribution DungeonSize;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Native Calysto candidate density; fraction from 0.20 to 0.50."))
	FEFCalystoFloatDistribution CandidateDensity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (DisplayName = "Side Path Chance (%)", ToolTip = "Percent from 30 to 70, converted once for native Calysto."))
	FEFCalystoFloatDistribution SidePathPercent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "4", ClampMax = "8"))
	int32 MinimumRoomSize = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "4", ClampMax = "8"))
	int32 MaximumRoomSize = 8;
	FEFCalystoLayout() { DungeonSize.Value = 24.0; CandidateDensity.Value = 0.32; SidePathPercent.Value = 50.0; }
};

UENUM(BlueprintType)
enum class EEFCalystoTileSpacingDistribution : uint8
{
	Fixed, Uniform, Triangular, Weighted
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTileSpacingChoice
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Tiles = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (ClampMin = "0", ToolTip = "Relative probability among the authored tile counts. Tile counts must be unique."))
	double Weight = 1.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTileSpacing
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing")
	EEFCalystoTileSpacingDistribution Distribution = EEFCalystoTileSpacingDistribution::Fixed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (ClampMin = "1", ClampMax = "100", EditCondition = "Distribution == EEFCalystoTileSpacingDistribution::Fixed", EditConditionHides))
	int32 Tiles = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (ClampMin = "1", ClampMax = "100", EditCondition = "Distribution == EEFCalystoTileSpacingDistribution::Uniform || Distribution == EEFCalystoTileSpacingDistribution::Triangular", EditConditionHides))
	int32 Minimum = 9;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (ClampMin = "1", ClampMax = "100", EditCondition = "Distribution == EEFCalystoTileSpacingDistribution::Triangular", EditConditionHides, ToolTip = "Peak of a continuous triangular distribution rounded to the nearest tile count."))
	int32 Mode = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (ClampMin = "1", ClampMax = "100", EditCondition = "Distribution == EEFCalystoTileSpacingDistribution::Uniform || Distribution == EEFCalystoTileSpacingDistribution::Triangular", EditConditionHides))
	int32 Maximum = 11;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (EditCondition = "Distribution == EEFCalystoTileSpacingDistribution::Weighted", EditConditionHides, TitleProperty = "Tiles"))
	TArray<FEFCalystoTileSpacingChoice> Choices;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLightFlicker
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ToolTip = "Deterministic smooth changes to light-component intensity. Native torch particle effects remain independent."))
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bEnabled", EditConditionHides))
	double MinimumMultiplier = 0.8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bEnabled", EditConditionHides))
	double MaximumMultiplier = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0.01", ClampMax = "30", Units = "Hz", EditCondition = "bEnabled", EditConditionHides, ToolTip = "New deterministic intensity target values per second, with smooth interpolation between them."))
	double FrequencyHz = 6.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLighting
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	FEFCalystoFloatDistribution IntensityMultiplier;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = "0", ClampMax = "4", ToolTip = "Explicit intensity saturation ceiling. Any sampled value above the ceiling becomes the ceiling, preserving its probability mass."))
	double IntensityCeiling = 4.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (Units = "cm", ToolTip = "Wall light height distribution in native dungeon centimetres, sampled once per floor."))
	FEFCalystoFloatDistribution WallLightHeight;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Wall Light Tile Spacing", ToolTip = "Distance between native wall-light opportunities in native tile units. Weighted outcomes are an explicit discrete distribution."))
	FEFCalystoTileSpacing WallLightTileDistance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ShowOnlyInnerProperties, DisplayName = "Light Intensity Flicker"))
	FEFCalystoLightFlicker Flicker;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	FEFCalystoPlacement Placement;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoArchitectureEntry> WallLights;
	FEFCalystoLighting() { Placement.Zone = EEFCalystoPlacementZone::WallMiddle; WallLightHeight.Value = 200.0; }
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloorBudgets
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 Enemies = 25;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 LooseFood = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 Chests = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 LootActors = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 SpecialEvents = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 TotalActors = 36;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits")
	FEFCalystoScalarCurve Threat;
	FEFCalystoFloorBudgets() { Threat.FirstValue = 12.0; Threat.LastValue = 30.0; }
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoContentEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FEFCalystoSelection Selection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (AssetBundles = "CalystoGameplay"))
	TSoftClassPtr<AActor> ActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (AssetBundles = "CalystoGameplay", ToolTip = "Inventory payload for Container Content roles."))
	TSoftClassPtr<UObject> InventoryClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FEFCalystoPlacement Placement;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	EEFCalystoRarity Rarity = EEFCalystoRarity::Common;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	FName Archetype = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	EEFCalystoGender Gender = EEFCalystoGender::Any;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	EEFCalystoLifecycle Lifecycle = EEFCalystoLifecycle::FloorLocal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	double ThreatCost = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	double ResourceCost = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0"))
	int32 MaximumPerFloor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	int32 MinimumLevelOffset = -2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	int32 MaximumLevelOffset = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	bool bRequiresGraveyardEligibility = false;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoContentGroup
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Name"))
	FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (ToolTip = "Typed category for inheritance and gameplay dispatch. One group per role in each Style or Theme."))
	EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Prop;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	EEFCalystoBudgetMembership Budget = EEFCalystoBudgetMembership::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	EEFCalystoContentMode Mode = EEFCalystoContentMode::Replace;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (EditCondition = "Mode == EEFCalystoContentMode::Extend", EditConditionHides))
	bool bOverrideChance = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chance", meta = (EditCondition = "Mode == EEFCalystoContentMode::Replace || (Mode == EEFCalystoContentMode::Extend && bOverrideChance)", EditConditionHides))
	FEFCalystoPercentageCurve Chance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount", meta = (EditCondition = "Mode == EEFCalystoContentMode::Extend", EditConditionHides))
	bool bOverrideAmount = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Amount", meta = (EditCondition = "Mode == EEFCalystoContentMode::Replace || (Mode == EEFCalystoContentMode::Extend && bOverrideAmount)", EditConditionHides))
	FEFCalystoAmountDistribution Amount;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (EditCondition = "Mode == EEFCalystoContentMode::Extend", EditConditionHides))
	bool bOverrideRarity = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (EditCondition = "Mode == EEFCalystoContentMode::Replace || (Mode == EEFCalystoContentMode::Extend && bOverrideRarity)", EditConditionHides))
	FEFCalystoRarityCurve Rarity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (ClampMin = "0", EditCondition = "Mode == EEFCalystoContentMode::Replace", EditConditionHides, ToolTip = "Category capacity; Style-wide category and floor budgets always remain in force."))
	int32 MaximumPerFloor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (EditCondition = "Mode == EEFCalystoContentMode::Replace || Mode == EEFCalystoContentMode::Extend", EditConditionHides, TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoContentEntry> Entries;
	/** Editor/importer resolved references; hidden identities are not an ordinary authoring control. */
	UPROPERTY()
	TArray<FGuid> RemovedEntryIds;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDecalVariant
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FEFCalystoSelection Selection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (AssetBundles = "CalystoDecals"))
	TSoftObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (AssetBundles = "CalystoDecals"))
	TSoftObjectPtr<UTexture2D> ColorTexture;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (AssetBundles = "CalystoDecals"))
	TSoftObjectPtr<UTexture2D> NormalTexture;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals")
	bool bFloor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals")
	bool bWall = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals")
	bool bRoof = true;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDecals
{
	GENERATED_BODY()
#if WITH_EDITORONLY_DATA
	/** Archived input to the retired source screen-size approximation, never an active distance-fade control. */
	UPROPERTY()
	double SourceFadeStartDistanceCm = 0.0;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals")
	EEFCalystoDecalMode Mode = EEFCalystoDecalMode::Replace;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "100", Units = "Percent", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	double ChancePercent = 10.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	int32 MaximumPerRoom = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", ClampMax = "24", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	int32 ActiveFloorBudget = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	int32 FloorLimit = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	int32 WallLimit = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	int32 RoofLimit = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides))
	FEFCalystoFloatDistribution SizeCm;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", Units = "cm", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides, ToolTip = "Actual world distance beyond which a pooled decal is culled."))
	double CullDistanceCm = 4000.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ClampMin = "0", EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides, ToolTip = "Renderer screen-size fading threshold. This is not world-distance fading."))
	double FadeScreenSize = 0.01;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (EditCondition = "Mode == EEFCalystoDecalMode::Replace", EditConditionHides, TitleProperty = "Selection.DisplayName"))
	TArray<FEFCalystoDecalVariant> Variants;
	FEFCalystoDecals() { SizeCm.Distribution = EEFCalystoDistribution::Uniform; SizeCm.Minimum = 35.0; SizeCm.Maximum = 110.0; }
};

UENUM(BlueprintType)
enum class EEFCalystoTrait : uint8 { Mystery, Danger, Safe, Abundance, ClothingInfluence };

UENUM(BlueprintType)
enum class EEFCalystoTraitSource : uint8 { Style, Theme, Snapshot };

UENUM(BlueprintType)
enum class EEFCalystoTraitControl : uint8 { Chance, EntryWeight };

/** Normalized authored or explicitly captured context. Names never imply a gameplay target. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTraits
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0", ClampMax = "1"))
	double Mystery = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0", ClampMax = "1"))
	double Danger = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0", ClampMax = "1"))
	double Safe = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0", ClampMax = "1"))
	double Abundance = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits", meta = (ClampMin = "0", ClampMax = "1"))
	double ClothingInfluence = 0.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTraitBinding
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EEFCalystoTraitSource Source = EEFCalystoTraitSource::Style;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EEFCalystoTrait Trait = EEFCalystoTrait::Mystery;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Prop;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EEFCalystoTraitControl Control = EEFCalystoTraitControl::Chance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (EditCondition = "Control == EEFCalystoTraitControl::EntryWeight", EditConditionHides, ToolTip = "Required exact persisted content entry identity. Chance bindings must leave this empty."))
	FGuid EntryId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (ClampMin = "-100", ClampMax = "100", Units = "Percent", ToolTip = "Multiplicative percentage change at trait value 0. Linear interpolation to value 1; matching effects sum canonically and clamp once to Maximum Effect Percent."))
	double EffectAtZeroPercent = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (ClampMin = "-100", ClampMax = "100", Units = "Percent"))
	double EffectAtOnePercent = 0.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoAdaptation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ClampMin = "0", ClampMax = "100", Units = "Percent", EditCondition = "bEnabled", EditConditionHides, ToolTip = "Maximum multiplicative change from a normalized input. Disabled adaptation has exactly zero effect."))
	double MaximumEffectPercent = 20.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (EditCondition = "bEnabled", EditConditionHides, ToolTip = "Explicit trait-to-content bindings, at most 64. Empty means no effect. No binding changes Theme presence, Amount, capacity or resource selection rules."))
	TArray<FEFCalystoTraitBinding> Bindings;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoStyle
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FEFCalystoSelection Selection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FEFCalystoLayout Layout;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Materials", meta = (ShowOnlyInnerProperties))
	FEFCalystoSurfaceMaterials Materials;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (ShowOnlyInnerProperties))
	FEFCalystoStyleArchitecture Architecture;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ShowOnlyInnerProperties))
	FEFCalystoLighting Lighting;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (TitleProperty = "DisplayName"))
	TArray<FEFCalystoContentGroup> Content;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ShowOnlyInnerProperties))
	FEFCalystoDecals Decals;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	FEFCalystoFloorBudgets FloorBudgets;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Advanced", meta = (ToolTip = "Normalized inputs consumed only by explicit Advanced Adaptation bindings. Adaptation off or no matching binding has exactly zero effect."))
	FEFCalystoTraits Traits;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTheme
{
	GENERATED_BODY()
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Advanced", meta = (MultiLine = "true", ToolTip = "Authoring notes only. Does not change Theme selection, content, or materials."))
	FString Description;
	/** Authoring swatch retained for the on-demand inspector; never a surface material override. */
	UPROPERTY()
	FLinearColor PreviewColor = FLinearColor::White;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FEFCalystoSelection Selection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Materials", meta = (ShowOnlyInnerProperties))
	FEFCalystoRoomMaterials Materials;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Architecture", meta = (TitleProperty = "Zone"))
	TArray<FEFCalystoSurfaceDecoration> Architecture;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (TitleProperty = "DisplayName"))
	TArray<FEFCalystoContentGroup> Content;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decals", meta = (ShowOnlyInnerProperties))
	FEFCalystoDecals Decals;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Advanced", meta = (ToolTip = "This room Theme's normalized inputs, used only by explicit Theme-source Adaptation bindings. Never changes Theme presence or material selection."))
	FEFCalystoTraits Traits;
	FEFCalystoTheme() { Decals.Mode = EEFCalystoDecalMode::Inherit; }
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonRules
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon", meta = (ClampMin = "1", ClampMax = "1", ToolTip = "One eligible room is guaranteed a Theme. A floor with no eligible room is rejected."))
	int32 GuaranteedThemedRooms = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon", meta = (ClampMin = "0", ClampMax = "100", Units = "Percent", DisplayName = "Additional Room Theme Chance", ToolTip = "Independent chance for each eligible room after the single guaranteed room. Protected rooms are excluded."))
	double AdditionalRoomThemeChancePercent = 25.0;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSafetyCeilings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety Ceilings", meta = (ClampMin = "0"))
	int32 Enemies = 25;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety Ceilings", meta = (ClampMin = "0"))
	int32 LooseFood = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety Ceilings", meta = (ClampMin = "0"))
	int32 Chests = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety Ceilings", meta = (ClampMin = "0"))
	int32 LootActors = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety Ceilings", meta = (ClampMin = "0"))
	int32 SpecialEvents = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety Ceilings", meta = (ClampMin = "0"))
	int32 TotalActors = 36;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorAdvanced
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (DisplayName = "Floor Safety Limits", ToolTip = "Every enabled positive-weight Style capacity must fit these floor-wide safety ceilings. Exceeding one rejects authoring; values are never clamped or used as Chance."))
	FEFCalystoSafetyCeilings HardCeilings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	FEFCalystoAdaptation Adaptation;
	/** Internal execution limits are hidden until the transaction consumes and verifies them. */
	UPROPERTY()
	int32 MaximumAttempts = 4;
	UPROPERTY()
	double RequestDeadlineSeconds = 30.0;
	UPROPERTY()
	int32 DecalPoolCapacity = 24;
	UPROPERTY()
	int32 MaximumRoomRecords = 2048;
	UPROPERTY()
	double RoomIdentityQuantizationCm = 10.0;
};
