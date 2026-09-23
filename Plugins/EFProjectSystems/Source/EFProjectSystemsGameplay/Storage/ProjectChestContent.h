#pragma once

#include "CoreMinimal.h"
#include "ACFItemTypes.h"
#include "Items/ACFItem.h"
#include "Engine/DataAsset.h"
#include "ProjectChestContent.generated.h"

class UDataTable;
class UACFItem;

/** Generator-neutral immutable manifest. Adapters may supply it before BeginPlay. */
USTRUCT()
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectChestResolvedEntry
{
    GENERATED_BODY()
    UPROPERTY() FName StableAttemptId;
    UPROPERTY() FName ContentCatalogId;
    UPROPERTY() TSubclassOf<UACFItem> ItemClass;
    UPROPERTY() int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectChestRarityChance
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rarity", meta=(Categories="Rarity")) FGameplayTag Rarity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rarity", meta=(ClampMin="0", ClampMax="100", Units="Percent", ToolTip="Exact percentage per selection. All entries must sum to 100. Empty positive tiers are errors.")) double Percentage = 100;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectChestContentSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="Data-defined content type identifier, such as Wealth, Armament or Ammunition. Filters determine eligibility.")) FName ContentType = TEXT("Wealth");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(RowType="ItemGenerationSlot", ToolTip="ACF catalog used directly, independent of the player and dungeon generator.")) TObjectPtr<UDataTable> Catalog;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="At least one category is required. Child categories match.")) FGameplayTagContainer Categories;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="Exclude these categories and their descendants.")) FGameplayTagContainer ExcludedCategories;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="Optional exact catalog row allowlist. Empty allows all matching rows.")) TArray<FName> AllowedRows;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="Exact catalog rows excluded after category filtering.")) TArray<FName> ExcludedRows;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="Disabled uses Fixed Selections. Enabled samples the inclusive minimum/maximum interval.")) bool bUseSelectionRange = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ClampMin="0", ClampMax="1024", EditCondition="!bUseSelectionRange", EditConditionHides)) int32 FixedSelections = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ClampMin="0", ClampMax="1024", EditCondition="bUseSelectionRange", EditConditionHides)) int32 MinSelections = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ClampMin="0", ClampMax="1024", EditCondition="bUseSelectionRange", EditConditionHides)) int32 MaxSelections = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ClampMin="1", ClampMax="255", ToolTip="Inclusive minimum units per selected item. Native ACF may merge identical stacks.")) int32 MinUnitsPerStack = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ClampMin="1", ClampMax="255", ToolTip="Must fit the native maximum stack of every eligible item; otherwise validation fails.")) int32 MaxUnitsPerStack = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(ToolTip="Allow repeated item classes. When disabled every positive tier must support the maximum selections without replacement.")) bool bAllowDuplicates = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content", meta=(TitleProperty="Rarity", ToolTip="Exact item rarity percentages, independent of chest rarity. Must sum to 100; no silent normalization or substitution.")) TArray<FProjectChestRarityChance> ItemRarities;
};

UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChestLootProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chest", meta=(ToolTip="Reusable content configuration. Chest rarity and seed remain independent.")) FProjectChestContentSettings Content;
};

/** Pure selection: no world, inventory, dungeon, or global random state. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectChestSelector
{
public:
    static bool Generate(const FProjectChestContentSettings& Settings, const FGuid& Identity, int32 Seed, TArray<FBaseItem>& Out, FString& Error);
};
