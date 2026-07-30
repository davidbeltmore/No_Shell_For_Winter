#pragma once

#include "CoreMinimal.h"
#include "Survival/ProjectCurseTypes.h"
#include "DungeonCurseTypes.generated.h"

class AActor;
class ARoomGameplayMarker;
class UMaterialInterface;
class USoundBase;

UENUM(BlueprintType)
enum class ERoomCurseType : uint8
{
	None UMETA(DisplayName = "None"),
	LightReduction UMETA(DisplayName = "Light Reduction"),
	MadnessPerSecond UMETA(DisplayName = "Madness Per Second"),
	CurseBuildupPerSecond UMETA(DisplayName = "Curse Buildup Per Second"),
	HungerDrainMultiplier UMETA(DisplayName = "Hunger Drain Multiplier"),
	ThirstDrainMultiplier UMETA(DisplayName = "Thirst Drain Multiplier"),
	MaxLevelEnemies UMETA(DisplayName = "Max Level Enemies"),
	TynaWhisper UMETA(DisplayName = "Tyna Whisper"),
	SealedCombatRoom_Phase2 UMETA(DisplayName = "Sealed Combat Room - Phase 2")
};

UENUM(BlueprintType)
enum class EGeneratedRoomType : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	Combat UMETA(DisplayName = "Combat"),
	Treasure UMETA(DisplayName = "Treasure"),
	Rest UMETA(DisplayName = "Rest"),
	Trap UMETA(DisplayName = "Trap"),
	Shrine UMETA(DisplayName = "Shrine"),
	Boss UMETA(DisplayName = "Boss"),
	Corridor UMETA(DisplayName = "Corridor"),
	Entrance UMETA(DisplayName = "Entrance"),
	Exit UMETA(DisplayName = "Exit"),
	Special UMETA(DisplayName = "Special")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FRoomCurseDefinition
{
	GENERATED_BODY()

	FRoomCurseDefinition()
		: CurseType(ERoomCurseType::None)
		, CurseID(NAME_None)
		, Weight(1.0f)
		, Duration(0.0f)
		, bRemoveOnExit(true)
		, bCanRepeatInSameFloor(true)
		, bDebugEnabled(false)
		, LightMultiplier(1.0f)
		, MadnessPerSecond(0.0f)
		, CurseBuildupPerSecond(0.0f)
		, CurseSourceKind(EProjectCurseSourceKind::Room)
		, HungerDrainMultiplier(1.0f)
		, ThirstDrainMultiplier(1.0f)
		, bForceMaxLevelEnemies(false)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse")
	ERoomCurseType CurseType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse")
	FName CurseID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Weight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Duration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse")
	bool bRemoveOnExit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse")
	bool bCanRepeatInSameFloor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Debug")
	bool bDebugEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Light", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float LightMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Inner State")
	float MadnessPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Curse Buildup", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CurseBuildupPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Curse Buildup")
	EProjectCurseSourceKind CurseSourceKind;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Inner State", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HungerDrainMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Inner State", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ThirstDrainMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies")
	bool bForceMaxLevelEnemies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Atmosphere")
	TSubclassOf<AActor> OptionalVFXActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Atmosphere")
	TObjectPtr<USoundBase> OptionalSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Atmosphere")
	TObjectPtr<UMaterialInterface> OptionalPostProcessMaterial;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FDetectedDungeonCurseRoom
{
	GENERATED_BODY()

	FDetectedDungeonCurseRoom()
		: RoomID(NAME_None)
		, RoomType(EGeneratedRoomType::Unknown)
		, Center(FVector::ZeroVector)
		, Extent(FVector(600.0f, 600.0f, 180.0f))
		, Rotation(FRotator::ZeroRotator)
		, SourceActor(nullptr)
		, SourceMarker(nullptr)
		, CurseChanceOverride(-1.0f)
		, Priority(0)
		, bAllowCurses(true)
		, bAllowEnemyLevelCurse(true)
		, bAllowLightCurse(true)
		, bAllowInnerStateCurse(true)
		, AssignedCurseType(ERoomCurseType::None)
		, AssignedCurseID(NAME_None)
	{
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	FName RoomID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	EGeneratedRoomType RoomType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	FVector Center;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	FVector Extent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	FRotator Rotation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	TObjectPtr<AActor> SourceActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	TObjectPtr<ARoomGameplayMarker> SourceMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	float CurseChanceOverride;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	int32 Priority;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	bool bAllowCurses;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	bool bAllowEnemyLevelCurse;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	bool bAllowLightCurse;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	bool bAllowInnerStateCurse;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	ERoomCurseType AssignedCurseType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse")
	FName AssignedCurseID;

	bool HasValidBounds() const
	{
		return Extent.X > KINDA_SMALL_NUMBER && Extent.Y > KINDA_SMALL_NUMBER && Extent.Z > KINDA_SMALL_NUMBER;
	}

	bool IsCurseTypeAllowed(const ERoomCurseType CurseType, const bool bEnableMaxLevelEnemyCurse, const bool bEnableSealedCombatRooms) const
	{
		if (!bAllowCurses || CurseType == ERoomCurseType::None)
		{
			return false;
		}

		switch (CurseType)
		{
		case ERoomCurseType::LightReduction:
			return bAllowLightCurse;
		case ERoomCurseType::MadnessPerSecond:
		case ERoomCurseType::CurseBuildupPerSecond:
		case ERoomCurseType::HungerDrainMultiplier:
		case ERoomCurseType::ThirstDrainMultiplier:
			return bAllowInnerStateCurse;
		case ERoomCurseType::MaxLevelEnemies:
			return bAllowEnemyLevelCurse && bEnableMaxLevelEnemyCurse;
		case ERoomCurseType::SealedCombatRoom_Phase2:
			return bEnableSealedCombatRooms;
		default:
			return true;
		}
	}
};
