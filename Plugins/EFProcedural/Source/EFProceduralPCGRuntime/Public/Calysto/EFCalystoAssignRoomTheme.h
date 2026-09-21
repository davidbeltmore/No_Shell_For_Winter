#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDirectorProbability.h"
#include "PCGSettings.h"
#include "EFCalystoAssignRoomTheme.generated.h"

struct FPCGDataCollection;

/** Failure category owned by the room-stream reader.  A topology that produces
 * no rooms may be reseeded; malformed metadata or protection semantics may not. */
enum class EEFCalystoNativeRoomReadFailure : uint8 { Spatial, Configuration };

USTRUCT()
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeTheme
{
	GENERATED_BODY()
	UPROPERTY() FGuid Id;
	UPROPERTY() double Weight = 1.0;
	UPROPERTY() TObjectPtr<UObject> RoomSchema;
	UPROPERTY() FSoftObjectPath FloorMaterial;
	UPROPERTY() FSoftObjectPath WallMaterial;
	UPROPERTY() FSoftObjectPath RoofMaterial;
	UPROPERTY() bool bOverrideFloor = false;
	UPROPERTY() bool bOverrideWall = false;
	UPROPERTY() bool bOverrideRoof = false;
};

/** Attempt-owned, already loaded inputs. Routing IDs and mutable subsystems are absent. */
USTRUCT()
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeRoomConfig
{
	GENERATED_BODY()
	UPROPERTY() int64 RunSeed = 0;
	UPROPERTY() int64 FloorNumber = 1;
	UPROPERTY() int32 RerollIndex = 0;
	UPROPERTY() int32 AttemptIndex = 0;
	UPROPERTY() int32 TopologySeed = 0;
	UPROPERTY() FGuid StyleId;
	UPROPERTY() FTransform DungeonTransform;
	UPROPERTY() double QuantizationCm = 10.0;
	UPROPERTY() int32 MaximumRooms = 2048;
	UPROPERTY() double AdditionalThemeChancePercent = 25.0;
	UPROPERTY() TObjectPtr<UObject> EmptyRoomSchema;
	UPROPERTY() FSoftObjectPath FloorMaterial;
	UPROPERTY() FSoftObjectPath WallMaterial;
	UPROPERTY() FSoftObjectPath RoofMaterial;
	UPROPERTY() TArray<FEFCalystoNativeTheme> Themes;
	FEFCalystoRandomKey RandomKey() const;
	bool Validate(FString& Error) const;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeRoom
{
	int64 RoomId = 0;
	FGuid ThemeId;
	FGuid StyleId;
	FTransform Transform;
	FBox LocalBounds = FBox(ForceInit);
	EEFCalystoProtectedRoom Protection = EEFCalystoProtectedRoom::None;
	bool bMainPath = false;
	bool bDoorClearance = false;
	bool bGuaranteedTheme = false;
	FSoftObjectPath FloorMaterial;
	FSoftObjectPath WallMaterial;
	FSoftObjectPath RoofMaterial;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeRoomPins
{
	static const FName MainRooms;
	static const FName SideRooms;
	static const FName StartRooms;
	static const FName EndRooms;
	static const FName CriticalRooms;
	static const FName ProgressionRooms;
	static const FName NativeRooms;
	static const FName RoomContexts;
};

/** Native geometry receives every room. Theme absence only changes room decoration metadata. */
UCLASS()
class EFPROCEDURALPCGRUNTIME_API UPCGCalystoDirectorRoomSettings final : public UPCGSettings
{
	GENERATED_BODY()
public:
	UPROPERTY() FEFCalystoNativeRoomConfig Config;
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CalystoDirectorRooms"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif
protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Exact inner-loop pins used to carry the active RoomOverride owner onto
 * native wall pieces after the vendor intersection has completed. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoRoomOverrideOwnerPins
{
	static const FName RoomOverride;
	static const FName Pieces;
};

/**
 * Restores the active room's canonical identity and frozen wall material only
 * within the exact native PCG_SetRoomTheme loop iteration. It never infers
 * ownership or materials from position, and rejects malformed active metadata.
 */
UCLASS()
class EFPROCEDURALPCGRUNTIME_API UPCGCalystoRoomOverrideOwnerSettings final : public UPCGSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CalystoRoomOverrideOwner"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif
protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Exact outer construction pins for restoring door material provenance after
 * native PCG_SetDungeonMesh deliberately removes material metadata. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDoorMaterialProvenancePins
{
	static const FName DoorSource;
	static const FName FinalDoorPieces;
};

/**
 * Copies the effective WallMaterial into a Director-owned typed carrier on
 * the exact Wall - Door stream before PCG_SetDungeonMesh removes
 * WallMaterial. The carrier is per piece, so shared room IDs never collapse
 * distinct valid doorway materials.
 */
UCLASS()
class EFPROCEDURALPCGRUNTIME_API UPCGCalystoDoorMaterialCarrierSettings final : public UPCGSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CalystoDoorMaterialCarrier"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif
protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/**
 * Reattaches the effective material to final native door pieces using only the
 * same-attempt upstream Wall - Door stream and its typed per-piece carrier.
 * This is never a spatial, tag, or array-order lookup. Every carrier must be
 * a resident material copied from the exact source stream before the native
 * graph runs, and every final door piece must retain one of those carriers.
 */
UCLASS()
class EFPROCEDURALPCGRUNTIME_API UPCGCalystoDoorMaterialProvenanceSettings final : public UPCGSettings
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CalystoDoorMaterialProvenance"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif
protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/**
 * The native PCG_SetRoomTheme Difference branch contains corridor/default
 * walls that have no RoomOverride.  This node marks that exact branch as
 * explicitly Style-owned and writes the frozen Style wall material.  It never
 * chooses a nearby room or inherits a vendor material.
 */
UCLASS()
class EFPROCEDURALPCGRUNTIME_API UPCGCalystoStyleWallFallbackSettings final : public UPCGSettings
{
	GENERATED_BODY()
public:
	/** Preloaded material from the selected immutable Style. */
	UPROPERTY() FSoftObjectPath StyleWallMaterial;
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CalystoStyleWallFallback"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif
protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoNativeRoomReader
{
	static bool Read(const FPCGDataCollection& Data, const FEFCalystoNativeRoomConfig& Config,
		TArray<FEFCalystoNativeRoom>& Rooms, FString& Error,
		EEFCalystoNativeRoomReadFailure* OutFailure = nullptr);
};
