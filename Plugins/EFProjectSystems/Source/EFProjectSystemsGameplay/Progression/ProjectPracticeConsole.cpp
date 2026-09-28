#include "Progression/ProjectPracticeProgressionComponent.h"
#include "Training/ProjectPracticeComponent.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "ACFGASStatisticsComponent.h"
#include "ACFSkillTreeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
FAutoConsoleCommandWithWorldAndArgs PracticeStart(TEXT("Project.Practice.Start"), TEXT("Start an authored training activity through the Y interaction route."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (World && Args.Num() == 1) if (auto* Emotes = World->GetSubsystem<UProjectEmoteSubsystem>())
            Emotes->RequestStartInteractionById(FName(*(TEXT("Actions.Training.") + Args[0])));
    }));
FAutoConsoleCommandWithWorld PracticeReport(TEXT("Project.Practice.Report"), TEXT("Report practice and native progression without changing them."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        auto* Player = UGameplayStatics::GetPlayerPawn(World, 0);
        if (!Player) return;
        const auto* Practice = Player->FindComponentByClass<UProjectPracticeProgressionComponent>();
        const auto* Training = Player->FindComponentByClass<UProjectPracticeComponent>();
        const auto* Stats = Player->FindComponentByClass<UACFGASStatisticsComponent>();
        const auto* Skills = Player->FindComponentByClass<UACFSkillTreeComponent>();
        if (Practice && Training && Stats && Skills)
        {
            UE_LOG(LogTemp, Display, TEXT("[ProjectPractice] active=%d activity=%s hours=%.9f book=%.9f intelligence=%.3f practice=%.9f efficiency=%.9f level=%d xp=%d perks=%d skills=%d paused=%d"),
                Training->IsTrainingActive(), *Training->GetActiveTrainingId().ToString(), Training->GetSessionHours(), Training->GetActivityProgress(),
                Practice->GetLearnedRank(TEXT("Intelligence")), Practice->GetPracticeProgress(TEXT("Intelligence")), Practice->GetPracticeEfficiency(TEXT("Intelligence")),
                Stats->GetCurrentLevel(), Stats->GetCurrentExp(), Stats->GetAvailablePerks(), Skills->GetSkillPoints(), UGameplayStatics::IsGamePaused(World));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[ProjectPractice] Missing runtime integration"));
        }
    }));
}
#endif
