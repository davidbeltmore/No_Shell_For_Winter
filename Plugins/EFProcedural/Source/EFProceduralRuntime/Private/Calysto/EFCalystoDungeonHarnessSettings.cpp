#include "Calysto/EFCalystoDungeonHarnessSettings.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"

UEFCalystoDungeonHarnessSettings::UEFCalystoDungeonHarnessSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("EFCalystoDungeonHarness");

	DungeonMap = TSoftObjectPtr<UWorld>(
		FSoftObjectPath(TEXT("/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration")));
	DirectorPolicy = TSoftObjectPtr<UEFCalystoDungeonDirectorPolicyV4>(
		FSoftObjectPath(TEXT("/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy")));
	DungeonMeshDataAsset = TSoftObjectPtr<UObject>(
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.DA_DungeonMesh")));
	SpawnerDataAsset = TSoftObjectPtr<UObject>(
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner.DA_DemoSpawner")));
	RoomThemeDataAsset = TSoftObjectPtr<UObject>(
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.DA_RoomTheme")));
	DungeonFloorDoorClass = TSoftClassPtr<AActor>(
		FSoftObjectPath(TEXT("/Script/EFProceduralACFURuntime.EFCalystoFloorDoor")));
	DungeonFloorDoorMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors.SM_SquaredArchedWoodenDoors")));
	PopulationAnchorClass = TSoftClassPtr<AActor>(
		FSoftObjectPath(TEXT("/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor")));
}

const UEFCalystoDungeonHarnessSettings* UEFCalystoDungeonHarnessSettings::Get()
{
	return GetDefault<UEFCalystoDungeonHarnessSettings>();
}

FName UEFCalystoDungeonHarnessSettings::GetCategoryName() const
{
	return TEXT("Game");
}
