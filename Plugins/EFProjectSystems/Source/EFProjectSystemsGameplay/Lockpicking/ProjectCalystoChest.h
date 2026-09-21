#pragma once

#include "CoreMinimal.h"
#include "Lockpicking/ProjectLockedWorldItem.h"
#include "ProjectCalystoChest.generated.h"

class UACFItem;
class USkeletalMesh;
class USkeletalMeshComponent;

/** Immutable content entry selected by the Dungeon Director before actor materialization. */
USTRUCT()
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoResolvedChestEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName StableAttemptId = NAME_None;

	UPROPERTY()
	FName ContentCatalogId = NAME_None;

	UPROPERTY()
	TSubclassOf<UACFItem> ItemClass;

	UPROPERTY()
	int32 Quantity = 1;
};

/**
 * Project-owned ACF storage bridge for Calysto. Geometry and placement remain
 * under Calysto/PCG; only the Director's already-resolved content is injected.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectCalystoChest : public AProjectLockedWorldItem
{
	GENERATED_BODY()

public:
	AProjectCalystoChest();
	static FSoftObjectPath GetDefaultVisualMeshPath();

	/** Called exclusively while the actor is deferred, before BeginPlay. */
	bool ConfigureResolvedLoot(
		TConstArrayView<FProjectCalystoResolvedChestEntry> Entries,
		FString& OutError);

	/** Seeds ACF storage and returns the exact frozen catalog IDs after verification. */
	bool FinalizeAndVerifyResolvedLoot(TArray<FName>& OutVerifiedContentIds, FString& OutError);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Calysto")
	TObjectPtr<USkeletalMeshComponent> ChestVisual;

	/** Loaded only if this concrete chest class was selected for the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Calysto",
		meta = (DisplayName = "Chest Appearance", ToolTip = "Project-owned soft reference; does not modify the vendor chest.",
			EFCalystoDeferredMeshBounds = "true", EFCalystoDeferredMeshComponent = "ProjectCalystoChestVisual"))
	TSoftObjectPtr<USkeletalMesh> ChestVisualMesh;

private:
	bool VerifyFrozenLootStorage(FString& OutError) const;

	UPROPERTY(Transient)
	TArray<FProjectCalystoResolvedChestEntry> FrozenResolvedLoot;

	bool bConfiguredByDirector = false;
	bool bFrozenLootSeeded = false;
	bool bVerifiedByDirector = false;
};
