#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UEFCalystoDirectorSettings::UEFCalystoDirectorSettings()
{
	CategoryName = TEXT("Game");
	Director = TSoftObjectPtr<UEFCalystoDungeonDirectorAsset>(FSoftObjectPath(TEXT("/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector.DA_CalystoDungeonDirector")));
	DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration")));
	HubMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/_Game/Hub/HUB.HUB")));
}

bool UEFCalystoDirectorSettings::IsEnabled()
{
	return GetDefault<UEFCalystoDirectorSettings>()->bEnabled
#if !UE_BUILD_SHIPPING
		|| FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorCandidate"))
		|| FParse::Param(FCommandLine::Get(), TEXT("CalystoDirectorNativeParity"))
#endif
		;
}
