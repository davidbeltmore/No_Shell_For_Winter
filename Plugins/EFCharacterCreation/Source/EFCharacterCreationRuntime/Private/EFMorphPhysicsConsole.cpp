#include "EFMorphPhysicsConstraintComponent.h"
#include "EFCharacterCustomizationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if !UE_BUILD_SHIPPING
namespace
{
FAutoConsoleCommandWithWorld MorphReport(TEXT("Project.MorphPhysics.Report"), TEXT("Report live player morph constraints without changing state."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        auto* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
        auto* Driver = Pawn ? Pawn->FindComponentByClass<UEFMorphPhysicsConstraintComponent>() : nullptr;
        if (!Driver) { UE_LOG(LogTemp, Warning, TEXT("[MorphPhysicsQA] missing driver")); return; }
        TArray<USkeletalMeshComponent*> Meshes;
        Pawn->GetComponents(Meshes);
        for (auto* Mesh : Meshes)
        {
            if (Mesh->GetFName() != Driver->GetResolvedTargetMeshName()) continue;
            UE_LOG(LogTemp, Display, TEXT("[MorphPhysicsQA] mesh=%s profile=%s"), *GetNameSafe(Mesh->GetSkeletalMeshAsset()), *GetNameSafe(Mesh->GetPhysicsAsset()));
            for (const auto& Group : Driver->GetConstraintGroups())
                for (FName Bone : Group.ConstraintNames)
                {
                    auto* Constraint = Mesh->FindConstraintInstance(Bone);
                    UE_LOG(LogTemp, Display, TEXT("[MorphPhysicsQA] bone=%s sim=%d valid=%d limit=%.4f"), *Bone.ToString(), Mesh->IsSimulatingPhysics(Bone), Constraint && !Constraint->IsTerminated(), Constraint ? Constraint->GetLinearLimit() : -1.f);
                }
        }
    }));
FAutoConsoleCommandWithWorldAndArgs MorphGender(TEXT("Project.MorphPhysics.Gender"), TEXT("Development QA: select Female or Male through the creator."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num()!=1 || (!Args[0].Equals(TEXT("Female"),ESearchCase::IgnoreCase) && !Args[0].Equals(TEXT("Male"),ESearchCase::IgnoreCase))) return;
        auto* Pawn = UGameplayStatics::GetPlayerPawn(World,0);
        if (auto* Creator = Pawn ? Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>() : nullptr)
            Creator->SelectGender(Args[0].Equals(TEXT("Male"),ESearchCase::IgnoreCase) ? ECharacterCreationGender::Male : ECharacterCreationGender::Female);
    }));
}
#endif
