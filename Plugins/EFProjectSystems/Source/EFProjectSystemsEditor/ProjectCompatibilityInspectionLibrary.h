#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectCompatibilityInspectionLibrary.generated.h"

class UAQSQuest;
/** Editor-only PIE fixtures and narrowly scoped project-owned asset repairs. */
UCLASS()
class UProjectCompatibilityInspectionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Project|Compatibility QA")
    static bool RemapTransientQuestTargets(UAQSQuest* Quest, AActor* Guy, AActor* Vendor, AActor* Spline = nullptr);
    UFUNCTION(BlueprintPure, Category="Project|Compatibility QA")
    static float GetPhysicalActivityIntensity(AActor* Actor);
    UFUNCTION(BlueprintPure, Category="Project|Compatibility QA")
    static bool IsNavigationFixtureReady(AActor* Context, AActor* NavigationData, FVector Location);
    /** Fills the missing Down montage on the project-owned player ability set only. */
    UFUNCTION(BlueprintCallable, Category="Project|Compatibility QA")
    static bool RepairPlayerLadderDownMontage();
    UFUNCTION(BlueprintCallable, Category="Project|Compatibility QA")
    static bool InstallPlayerSimulatedBowCharge();
};
