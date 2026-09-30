#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectCompatibilityInspectionLibrary.generated.h"

class UAQSQuest;
class UPhysicsAsset;
class USkeletalMesh;
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
    /** Repairs only the null base of the RealisticBlood demo's additive upper-body branch. */
    UFUNCTION(BlueprintCallable, Category="Project|Compatibility QA")
    static bool RepairBloodDemoAdditiveBase();
    /** Authors secondary bodies on a project-owned duplicate, never the source mesh/asset. */
    UFUNCTION(BlueprintCallable, Category="Project|Compatibility QA")
    static FString AuthorMorphPhysicsCandidate(USkeletalMesh* Mesh, UPhysicsAsset* Candidate);
};
