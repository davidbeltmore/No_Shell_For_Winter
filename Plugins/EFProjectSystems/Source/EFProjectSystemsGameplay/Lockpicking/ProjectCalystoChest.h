#pragma once

#include "CoreMinimal.h"
#include "Lockpicking/ProjectLockedWorldItem.h"
#include "Calysto/EFCalystoDeferredMeshBoundsProvider.h"
#include "Storage/ProjectChestContent.h"
#include "ProjectCalystoChest.generated.h"

class UACFItem;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * Project-owned ACF storage bridge for Calysto. Geometry and placement remain
 * under Calysto/PCG; only the Director's already-resolved content is injected.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectCalystoChest : public AProjectLockedWorldItem, public IEFCalystoDeferredMeshBoundsProvider
{
	GENERATED_BODY()

public:
	AProjectCalystoChest();
	static FSoftObjectPath GetDefaultVisualMeshPath();
	virtual void GetDeferredMeshBoundsBindings(TArray<FEFCalystoDeferredMeshBinding>& OutBindings) const override;

	/** Called exclusively while the actor is deferred, before BeginPlay. */
	bool ConfigureResolvedLoot(
		TConstArrayView<FProjectChestResolvedEntry> Entries,
		FString& OutError);

	/** Seeds ACF storage and returns the exact frozen catalog IDs after verification. */
	bool FinalizeAndVerifyResolvedLoot(TArray<FName>& OutVerifiedContentIds, FString& OutError);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Calysto")
	TObjectPtr<USkeletalMeshComponent> ChestVisual;

	/** Loaded only if this concrete chest class was selected for the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Calysto",
		meta = (DisplayName = "Chest Appearance", ToolTip = "Project-owned soft reference; does not modify the vendor chest."))
	TSoftObjectPtr<USkeletalMesh> ChestVisualMesh;

private:
	bool VerifyFrozenLootStorage(FString& OutError) const;

	UPROPERTY(Transient)
	TArray<FProjectChestResolvedEntry> FrozenResolvedLoot;

	bool bConfiguredByDirector = false;
	bool bFrozenLootSeeded = false;
	bool bVerifiedByDirector = false;
};
