#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectRuntimeAssetPreloadSubsystem.generated.h"

class APawn;
struct FStreamableHandle;

/**
 * Owns the product preload for the lifetime of the game instance.
 *
 * The old world-owned preload was destroyed and requested again on every map
 * travel. Keeping the streamable handle here makes the loaded objects resident
 * across MenuMap -> dungeon travel without enabling any gameplay budgeting.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRuntimeAssetPreloadSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsRuntimePreloadRequested() const;
	bool IsRuntimePreloadComplete() const;
	void CopyResidentEnemyClasses(TArray<TSubclassOf<APawn>>& OutEnemyClasses) const;

private:
	void RequestRuntimePreload();
	void HandleRuntimePreloadComplete();
	void ResolveResidentEnemyClasses();

private:
	TArray<TSubclassOf<APawn>> ResidentEnemyClasses;
	TSharedPtr<FStreamableHandle> RuntimePreloadHandle;
	bool bRuntimePreloadRequested = false;
	bool bRuntimePreloadComplete = false;
};
