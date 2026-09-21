#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "EFCalystoAssignRoomThemeV6.generated.h"

class UMaterialInstance;

/**
 * PCG-side mirror of the authoritative V6 topology flags. Values intentionally
 * match EEFCalystoRoomFlagsV6; Theme membership is expressed by EF_ThemeId and
 * never folded into this identity-bearing mask.
 */
UENUM(BlueprintType, meta = (Bitflags))
enum class EEFCalystoPCGRoomFlagsV6 : uint8
{
	None = 0,
	Start = 1 << 0,
	End = 1 << 1,
	Critical = 1 << 2,
	Progression = 1 << 3,
	MainPath = 1 << 4,
	DoorClearance = 1 << 5
};
ENUM_CLASS_FLAGS(EEFCalystoPCGRoomFlagsV6);

/** Material authority for one surface after Style -> Room Theme precedence is resolved. */
UENUM(BlueprintType)
enum class EEFCalystoMaterialAuthorityV6 : uint8
{
	None,
	Style,
	RoomTheme
};

/** Floor-wide materials owned by the selected dungeon Style. */
USTRUCT(BlueprintType)
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGStyleMaterialsV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Style ID"))
	FName StyleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style|Materials")
	TSoftObjectPtr<UMaterialInstance> FloorMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style|Materials")
	TSoftObjectPtr<UMaterialInstance> WallMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style|Materials")
	TSoftObjectPtr<UMaterialInstance> RoofMaterial;
};

/** One independently weighted Room Theme and its optional per-surface material overrides. */
USTRUCT(BlueprintType)
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGThemeProfileV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme", meta = (DisplayName = "Theme ID"))
	FName ThemeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme", meta = (ClampMin = "0.000001", UIMin = "0.01"))
	double SelectionWeight = 1.0;

	/**
	 * Runtime-only Calysto room architecture object synthesized from the inline
	 * V6 Theme profile. The adapter owns a strong reference for the whole floor;
	 * this node never loads or persists a separate Room Type Data Asset.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Theme")
	TObjectPtr<UObject> RoomType = nullptr;

	/** Stable identity of the inline architecture, never a transient UObject path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Theme")
	FString ArchitectureHash;

	/** Null means inherit the selected Style material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Material Overrides")
	TSoftObjectPtr<UMaterialInstance> FloorMaterial;

	/** Null means inherit the selected Style material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Material Overrides")
	TSoftObjectPtr<UMaterialInstance> WallMaterial;

	/** Null means inherit the selected Style material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Material Overrides")
	TSoftObjectPtr<UMaterialInstance> RoofMaterial;

	/** Stable editor/runtime catalog identity; it is metadata only and does not affect probability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Catalog")
	FName CatalogId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Catalog")
	FLinearColor CatalogColor = FLinearColor::White;
};

/**
 * Adapter-facing resolved V6 payload. It is self-contained at the PCG adapter boundary.
 * Materials remain soft paths for Calysto's mesh override helper, while RoomType is the
 * already-resident transient object synthesized from the authoritative inline profile.
 */
USTRUCT(BlueprintType)
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoRoomThemeGenerationConfigV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability")
	int64 GenerationSeed = 0;

	/** Hash of the immutable Director floor plan. Non-empty runtime payloads require its frozen alias table. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FString FloorPlanHash;

	/** Independent chance evaluated once for every ordinary room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double ThemePresenceChance = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FEFCalystoPCGStyleMaterialsV6 Style;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Themes", meta = (TitleProperty = "ThemeId"))
	TArray<FEFCalystoPCGThemeProfileV6> Themes;

	/**
	 * Frozen V6 alias table copied from the resolved floor plan. Runtime graph
	 * execution requires this table so its ThemeType result is byte-for-byte
	 * identical to the authoritative Director manifest.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Themes|Compiled")
	TArray<float> ThemeAliasProbability;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Themes|Compiled")
	TArray<int32> ThemeAliasIndex;

	/** Transform used to make Room IDs independent from world placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FTransform DungeonTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "10.0", Units = "cm"))
	double RoomIdentityGridCm = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Protection", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0", Units = "cm"))
	double ProtectionToleranceCm = 2.0;

	/**
	 * Deterministic room-AABB halo around the vendor Start Main and End Main
	 * connector markers. The protected vendor graph exposes no pre-theme door
	 * point stream, so physical door hits are rejected later and this halo owns
	 * the topology-side DoorClearance flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Protection", meta = (ClampMin = "0.0", ClampMax = "1000.0", UIMin = "0.0", UIMax = "500.0", Units = "cm"))
	double DoorClearanceRadiusCm = 200.0;

	/** Development-only probability values are emitted only when this is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics")
	bool bEmitProbabilityDiagnostics = false;

	bool Validate(FString& OutError) const;
};

/** Stable pin labels used by the native node and transient graph builder. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoRoomThemePinsV6
{
	static const FName MainRooms;
	static const FName SideRooms;
	static const FName StartRooms;
	static const FName EndRooms;
	static const FName CriticalRooms;
	static const FName ProgressionRooms;
	static const FName ThemedRooms;
	static const FName UnthemedRooms;
	static const FName RoomContexts;
};

/** Stable metadata names consumed by the runtime Director after PCG completes. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoRoomThemeMetadataV6
{
	static const FName RoomId;
	static const FName ThemeId;
	static const FName StyleId;
	static const FName RoomFlags;
	static const FName TopologyKind;
	static const FName CollisionOrdinal;
	static const FName CatalogId;
	static const FName CatalogColor;
	static const FName EffectiveFloorMaterial;
	static const FName EffectiveWallMaterial;
	static const FName EffectiveRoofMaterial;
	static const FName FloorMaterialAuthority;
	static const FName WallMaterialAuthority;
	static const FName RoofMaterialAuthority;
	static const FName VendorRoomType;
	static const FName VendorOverrideFloorMaterial;
	static const FName VendorFloorMaterial;
	static const FName VendorOverrideWallMaterial;
	static const FName VendorWallMaterial;
	static const FName VendorOverrideRoofMaterial;
	static const FName VendorRoofMaterial;
	static const FName PresenceDraw;
	static const FName ThemeDraw;

	/** Ensures every physical PCG attribute name is valid and unique for the running engine version. */
	static bool ValidateSchema(FString& OutError);
};

/** Pure deterministic math shared by the PCG element, runtime validation, and tests. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoRoomThemeDeterminismV6 final
{
public:
	static FString BuildStableRoomId(
		const FPCGPoint& Point,
		const FTransform& DungeonTransform,
		double IdentityGridCm,
		int64 GenerationSeed,
		const FString& CanonicalSourceKey);

	static double Draw01(int64 GenerationSeed, const FString& RoomId, const TCHAR* Lane);

	static bool IsThemePresent(
		int64 GenerationSeed,
		const FString& RoomId,
		double ThemePresenceChance);

	/** Accepts only upstream Critical/Progression protection bits and rejects all other flags. */
	static bool ResolveUpstreamProtectionFlags(
		int32 UpstreamRoomFlags,
		EEFCalystoPCGRoomFlagsV6& OutProtectionFlags,
		FString& OutError);

	/** Returns the authored-array index while making authored order irrelevant. */
	static int32 SelectThemeIndex(
		int64 GenerationSeed,
		const FString& RoomId,
		const TArray<FEFCalystoPCGThemeProfileV6>& Themes);

	/** Returns INDEX_NONE when no box contains the marker or an exact tie is ambiguous. */
	static int32 ResolveProtectedRoomIndex(
		const FVector& MarkerLocation,
		const TArray<FBox>& RoomBounds,
		const TArray<FString>& RoomIds,
		double ToleranceCm);

	/**
	 * Resolves a deterministic Euclidean halo from connector markers to room
	 * AABBs. Every marker must map to one unambiguous anchor room or the entire
	 * operation fails closed.
	 */
	static bool ResolveDoorClearanceRoomIndexes(
		const TArray<FVector>& ConnectorLocations,
		const TArray<FBox>& RoomBounds,
		const TArray<FString>& RoomIds,
		double ContainmentToleranceCm,
		double ClearanceRadiusCm,
		TArray<int32>& OutRoomIndexes,
		FString& OutError);

	static FString BuildConfigFingerprint(const FEFCalystoRoomThemeGenerationConfigV6& Config);
};

/** Assigns zero or one Room Theme per room and freezes authoritative topology protection flags. */
UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (DisplayName = "Calysto Assign Room Theme V6"))
class EFPROCEDURALPCGRUNTIME_API UPCGCalystoAssignRoomThemeSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calysto V6", meta = (ShowOnlyInnerProperties))
	FEFCalystoRoomThemeGenerationConfigV6 ResolvedConfig;

	void SetResolvedConfig(const FEFCalystoRoomThemeGenerationConfigV6& InConfig);

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CalystoAssignRoomThemeV6"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class EFPROCEDURALPCGRUNTIME_API FPCGCalystoAssignRoomThemeElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
};
