#include "RuntimePerformance/ProjectPerformanceBudgetSettings.h"

UProjectPerformanceBudgetSettings::UProjectPerformanceBudgetSettings()
{
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget.WBP_ProjectKnockoutStruggleWidget_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Data/Intimacy/DT_ProjectSocialCardRows.DT_ProjectSocialCardRows")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses.DT_ProjectSurvivalStatuses")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Emote/DA_ProjectEmoteMenu.DA_ProjectEmoteMenu")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/Chronicle/Main/WBP_ProjectChronicleWidget.WBP_ProjectChronicleWidget_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/Chronicle/Globals/WBP_ProjectChronicleExpandedGlobal.WBP_ProjectChronicleExpandedGlobal_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/Debug/Main/WBP_ProjectGameplayDebugMenu.WBP_ProjectGameplayDebugMenu_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/InnerState/Main/WBP_ProjectInnerStateWidget.WBP_ProjectInnerStateWidget_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/LockPick/Main/WBP_LockpickingMinigame.WBP_LockpickingMinigame_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/LockPick/Prompt/WBP_LockpickPrompt.WBP_LockpickPrompt_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/Status/Main/WBP_ProjectSurvivalStatusWidget.WBP_ProjectSurvivalStatusWidget_C")));
	AdditionalPreloadAssets.Add(FSoftObjectPath(TEXT("/Game/_Game/Widgets/Y/Main/WBP_ProjectEmoteMenu.WBP_ProjectEmoteMenu_C")));
}

const UProjectPerformanceBudgetSettings* UProjectPerformanceBudgetSettings::Get()
{
	return GetDefault<UProjectPerformanceBudgetSettings>();
}

FName UProjectPerformanceBudgetSettings::GetCategoryName() const
{
	return TEXT("Project");
}
