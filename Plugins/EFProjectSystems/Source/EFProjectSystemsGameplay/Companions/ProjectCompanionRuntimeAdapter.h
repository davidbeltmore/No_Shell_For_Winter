#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Companions/ProjectRunCompanionTypes.h"
#include "ProjectCompanionRuntimeAdapter.generated.h"

class AACFCharacter;
class APawn;
class UACFCompanionGroupAIComponent;

/**
 * Project-owned boundary around ACF companion spawning. It never mutates an ACF
 * Blueprint CDO or DataAsset: the only repair uses a transient per-actor clone.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCompanionRuntimeAdapter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Project|Companions", meta = (WorldContext = "WorldContextObject"))
	static FProjectCompanionSpawnResult SpawnAndRegisterCompanion(
		UObject* WorldContextObject,
		const FProjectCompanionDefinition& Definition,
		const FTransform& SpawnTransform,
		UACFCompanionGroupAIComponent* CompanionGroup);

	/** Configure level/statistics on an actor that is still inside SpawnActorDeferred. */
	static bool PrepareDeferredCompanion(
		AACFCharacter* DeferredCharacter,
		const FProjectCompanionDefinition& Definition,
		FString& OutError,
		bool* OutStatisticsRepairApplied = nullptr);

	/**
	 * Validate/register a finished actor. CompanionGroup is mandatory only for a
	 * persistent recruited roster projection; floor NPCs remain outside party.
	 */
	static FProjectCompanionSpawnResult FinalizeDeferredCompanion(
		AACFCharacter* Character,
		const FProjectCompanionDefinition& Definition,
		UACFCompanionGroupAIComponent* CompanionGroup,
		bool bRegisterAsRecruited);

	/** Deterministic NavMesh projection; never calls a random navigation API. */
	static bool FindDeterministicSafeSpawnTransform(
		UWorld* World,
		const APawn* PlayerPawn,
		const FGuid& StableCompanionId,
		TSubclassOf<AACFCharacter> CharacterClass,
		const TArray<FVector>& ReservedLocations,
		FTransform& OutTransform,
		FString& OutError);

	static UACFCompanionGroupAIComponent* ResolveCompanionGroup(const APawn* PlayerPawn);
	static void RollbackSpawnedCompanion(AACFCharacter* Character, UACFCompanionGroupAIComponent* CompanionGroup);
};
