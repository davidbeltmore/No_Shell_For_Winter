#pragma once

#include "Engine/DeveloperSettings.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "EFDAZWeaponPoseSettings.generated.h"

USTRUCT()
struct FEFDAZWeaponOverlayOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) TSoftClassPtr<UAnimInstance> Source;
    UPROPERTY(EditAnywhere) TSoftClassPtr<UAnimInstance> Replacement;
};

// Exact class substitutions after appearance initialization; native meshes and
// unrelated overlays are unaffected. The replacement assets own the pose policy.
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="DAZ Weapon Poses"))
class EFCHARACTERCREATIONACFURUNTIME_API UEFDAZWeaponPoseSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Config, Category="Compatibility")
    TArray<FEFDAZWeaponOverlayOverride> OverlayOverrides;
    UPROPERTY(EditAnywhere, Config, Category="Compatibility")
    TArray<FString> MeshRoots = { TEXT("/Game/DazToUnreal/") };
    UPROPERTY(EditAnywhere, Config, Category="Sockets")
    TSoftObjectPtr<USkeletalMesh> SocketReferenceMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/AscentCombatFramework/Animation/SkeletalMeshes/SKM_Manny.SKM_Manny")));
    UPROPERTY(EditAnywhere, Config, Category="Sockets")
    TArray<FName> MissingBodySockets = { TEXT("PistolEquip") };
};
