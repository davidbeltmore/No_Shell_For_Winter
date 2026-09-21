#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "EFClothingMorphSettings.generated.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UEFClothingSystemManifest;

/**
 * Canonical version-neutral runtime settings. V5 may run beside V4 during the
 * staged migration; every fallback poll and load wait has a hard runtime bound.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Clothing Morph"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnabled = true;

	/** Equipment adapters should call the runtime directly; polling remains a bounded safety net. */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bPreferEquipmentEvents = true;

	/** Internal pointer to the one stable project-owned authoring table. */
	UPROPERTY(Config)
	TSoftObjectPtr<UEFClothingMorphDirectorPolicy> Director = TSoftObjectPtr<UEFClothingMorphDirectorPolicy>(
		FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.DA_EFClothingMorphDirector")));

	/** Internal V5.1 compiled entry point; never an author-edited database. */
	UPROPERTY(Config)
	TSoftObjectPtr<UEFClothingSystemManifest> V5SystemManifest = TSoftObjectPtr<UEFClothingSystemManifest>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingSystemManifest.DA_EFClothingSystemManifest")));

	/** Internal soft registry; loading the table cannot pull every payload into memory. */
	UPROPERTY(Config)
	TSoftObjectPtr<UEFClothingFitRegistry> V5Registry = TSoftObjectPtr<UEFClothingFitRegistry>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry")));

	UPROPERTY(Config, EditAnywhere, Category = "Compatibility")
	bool bAllowV4Fallback = true;

	/** Internal read-only rollback path. */
	UPROPERTY(Config)
	TSoftObjectPtr<UEFClothingFitRegistry> V4FallbackRegistry = TSoftObjectPtr<UEFClothingFitRegistry>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry")));

	/** Event-miss safety net; event-driven integrations do not wait for this interval. */
	UPROPERTY(Config, EditAnywhere, Category = "Time Budgets", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "2.0", Units = "s"))
	float EquipmentReconcileFallbackIntervalSeconds = 2.0f;

	/** Bounded discovery fallback for pawns which cannot expose equipment events. */
	UPROPERTY(Config, EditAnywhere, Category = "Time Budgets", meta = (ClampMin = "0.10", ClampMax = "5.0", UIMin = "0.10", UIMax = "1.0", Units = "s"))
	float WorldDiscoveryFallbackIntervalSeconds = 0.50f;

	/** Zero evaluates morph curves after every animation update; positive values throttle evaluation. */
	UPROPERTY(Config, EditAnywhere, Category = "Time Budgets", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25", Units = "s"))
	float MorphSyncIntervalSeconds = 0.0f;

	/** A missing/corrupt streamed binding fails closed after this wait instead of stalling indefinitely. */
	UPROPERTY(Config, EditAnywhere, Category = "Time Budgets", meta = (ClampMin = "0.50", ClampMax = "15.0", UIMin = "0.50", UIMax = "5.0", Units = "s"))
	float BindingLoadTimeoutSeconds = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaximumConcurrentBindingLoads = 2;

	float GetEquipmentReconcileFallbackIntervalSeconds() const;
	float GetWorldDiscoveryFallbackIntervalSeconds() const;
	float GetMorphSyncIntervalSeconds() const;
	float GetBindingLoadTimeoutSeconds() const;
	int32 GetMaximumConcurrentBindingLoads() const;
};
