#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Storage/ProjectChestContent.h"
#include "ProjectProceduralChestComponent.generated.h"

/** Authoring only. The inherited ProjectChestInventoryComponent owns all items. */
UCLASS(ClassGroup=(Storage), BlueprintType, meta=(BlueprintSpawnableComponent, DisplayName="Procedural Chest"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectProceduralChestComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectProceduralChestComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chest", meta=(DisplayPriority="0", ToolTip="Generate once on a valid unlocked opening. Disabled preserves manual stock. Editing this flag never clears or refills an inventory.")) bool bProceduralMode = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chest", meta=(EditCondition="bProceduralMode", Categories="Rarity", ToolTip="Chest classification only; does not constrain item rarity percentages.")) FGameplayTag ChestRarity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chest", meta=(EditCondition="bProceduralMode", ToolTip="Reusable content type, filters, selection count, stacks and item rarity percentages.")) TObjectPtr<UProjectChestLootProfile> Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chest", meta=(EditCondition="bProceduralMode", ToolTip="Use the complete local content settings instead of the reusable profile.")) bool bOverrideContent = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chest", meta=(EditCondition="bProceduralMode && bOverrideContent", EditConditionHides, ToolTip="Per-instance content settings. No changes are made to the shared profile.")) FProjectChestContentSettings ContentOverride;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chest|Reproduction", meta=(EditCondition="bProceduralMode", ToolTip="Reproduction seed combined with Stable Identity. Never uses global random state.")) int32 Seed = 12345;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Chest|Reproduction", meta=(ToolTip="Persistent unique chest GUID. Assigned from the editor actor GUID for placed chests. Runtime spawners must supply it before opening; never derive it from an actor name.")) FGuid StableIdentity;
    UFUNCTION(BlueprintCallable, Category="Chest") bool PreviewContents(TArray<FBaseItem>& Contents, FString& Error) const;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Chest") void ValidateConfiguration();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Chest", meta=(ToolTip="Explicit validation result. Validation does not generate or alter inventory.")) FString ValidationResult;
    virtual void OnRegister() override;
};
