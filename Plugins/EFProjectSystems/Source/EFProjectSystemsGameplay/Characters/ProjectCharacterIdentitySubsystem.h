#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectCharacterIdentitySubsystem.generated.h"

class AActor;
class APawn;

/** Applies the same exclusive gender-tag contract used by the player to project-owned enemies and companions. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterIdentitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Project|Identity")
	static bool ApplyGenderIdentity(APawn* Pawn, FGameplayTag GenderTag);

	UFUNCTION(BlueprintPure, Category = "Project|Identity")
	static bool IsMaleGenderTag(FGameplayTag GenderTag);

private:
	void LoadConfiguredClasses();
	void ProcessExistingPawns();
	void HandleActorSpawned(AActor* SpawnedActor);
	bool ProcessPawn(APawn* Pawn) const;
	FGameplayTag ResolveConfiguredGenderTag(const UClass* ActorClass) const;
	bool IsConfiguredClass(const UClass* ActorClass, const TArray<TSubclassOf<APawn>>& RegisteredClasses) const;

private:
	FDelegateHandle ActorSpawnedHandle;
	TArray<TSubclassOf<APawn>> MaleCharacterClasses;
	TArray<TSubclassOf<APawn>> FemaleCharacterClasses;
	TArray<TWeakObjectPtr<APawn>> PendingPawns;
	float SecondsUntilNextIdentityAudit = 0.0f;
	bool bInitialPawnScanPending = true;
};
