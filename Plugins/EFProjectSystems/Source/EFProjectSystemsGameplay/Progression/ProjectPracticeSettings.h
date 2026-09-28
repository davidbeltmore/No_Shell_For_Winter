#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectPracticeSettings.generated.h"

UENUM(BlueprintType)
enum class EProjectPracticeMethod : uint8 { Physical, Book, Continuous, Lock };

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPracticeDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Attribute;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EProjectPracticeMethod Method = EProjectPracticeMethod::Continuous;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double DurationHours = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double PracticePerHour = 25.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) float PhysicalEffortIntensity = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FSoftObjectPath Animation;
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Practice and unified progression"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPracticeSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UProjectPracticeSettings();
    UPROPERTY(Config, EditAnywhere, Category="Progression") bool bEnabled = false;
    UPROPERTY(Config, EditAnywhere, Category="Progression", meta=(ClampMin="1")) double PracticeForFirstRank = 100.0;
    UPROPERTY(Config, EditAnywhere, Category="Progression", meta=(ClampMin="0")) double RankCostGrowth = 0.08;
    UPROPERTY(Config, EditAnywhere, Category="Progression", meta=(ClampMin="0")) double ExperiencePerPractice = 0.1;
    UPROPERTY(Config, EditAnywhere, Category="Progression", meta=(ClampMin="1")) int32 MaximumAttributeRank = 100;
    UPROPERTY(Config, EditAnywhere, Category="Progression", meta=(ClampMin="0")) int32 SkillPointsPerLevel = 1;
    UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0.01")) double ExposureHalfLifeHours = 3.0;
    UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0.01")) double RecoveryPerCalendarHour = 2.0;
    UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0.01")) double RecoveryDebtScale = 12.0;
    UPROPERTY(Config, EditAnywhere, Category="Activities") TArray<FProjectPracticeDefinition> Activities;
    UPROPERTY(Config, EditAnywhere, Category="Synergies", meta=(ClampMin="0", ClampMax="0.9")) float ConstitutionMaximumPainReduction = 0.25f;
    UPROPERTY(Config, EditAnywhere, Category="Synergies", meta=(ClampMin="0", ClampMax="0.9")) float EnduranceMaximumSweatReduction = 0.30f;
    UPROPERTY(Config, EditAnywhere, Category="Synergies", meta=(ClampMin="0")) float IntelligenceLockpickContribution = 0.05f;
    UPROPERTY(Config, EditAnywhere, Category="Synergies", meta=(ClampMin="0")) int32 IntelligenceMaximumLockpickBonus = 5;
    const FProjectPracticeDefinition* Find(FName Id) const;
};
