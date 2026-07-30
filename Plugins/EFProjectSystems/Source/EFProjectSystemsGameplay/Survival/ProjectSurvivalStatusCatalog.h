#pragma once

#include "CoreMinimal.h"
#include "Survival/ProjectSurvivalStatusTypes.h"

struct FProjectSurvivalStatusIncomingHitRule
{
	FName StatusName = NAME_None;
	TArray<FString> SourceClassNameHints;
	float ApplyChance = 1.f;
};

struct FProjectSurvivalStatusCatalog
{
	TArray<FProjectSurvivalStatusDefinition> StatusDefinitions;
	TArray<FProjectSurvivalStatusIncomingHitRule> IncomingHitRules;
	TArray<FName> DebugCycleStatusNames;
	bool bEnableDebugStatusCycling = true;
	FVector2D StatusHudOffset = FVector2D(456.f, 466.f);
	FVector2D StatusIconSize = FVector2D(70.f, 70.f);
	float StatusIconSpacing = 8.f;
	int32 StatusIconsPerRow = 5;
};

EFPROJECTSYSTEMSGAMEPLAY_API const FProjectSurvivalStatusCatalog& GetProjectSurvivalStatusCatalog();
