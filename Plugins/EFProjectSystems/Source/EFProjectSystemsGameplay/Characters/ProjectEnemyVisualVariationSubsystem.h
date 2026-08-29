#pragma once

#include "CoreMinimal.h"
#include "ContentPolicy/ProjectContentPolicyTypes.h"
#include "EFCharacterCreationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "ProjectEnemyVisualVariationSubsystem.generated.h"

class AActor;
class APawn;
class UEFCharacterCustomizationComponent;
class UProjectCombatAttributeComponent;
class UProjectContentPolicySubsystem;

struct FProjectEnemyOptionalMatureMorphState
{
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<UEFCharacterCustomizationComponent> CustomizationComponent;
	TWeakObjectPtr<UProjectCombatAttributeComponent> CombatComponent;
	TArray<FMorphSliderEntry> NeutralBaseEntries;
	TArray<FMorphSliderEntry> ActivePresentationEntries;
	float CurrentPresentationAlpha = 0.0f;
	bool bShouldShowActivePresentation = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyVisualVariationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Pure gate shared with automation; false unless Charisma and presentation policy both allow it. */
	static bool IsOptionalMatureMorphPresentationAllowedForPolicy(
		bool bFeatureEnabled,
		const FProjectContentPolicySnapshot& Policy);

	/** Active presentation is limited to the exact living participant of an active Intimacy Session. */
	static bool ShouldUseActiveOptionalMatureMorph(
		bool bPresentationAllowed,
		bool bActorInActiveIntimacySession,
		bool bActorDead);

	/** Neutral variation also processes explicitly registered Intimacy participants outside the hostile registry. */
	static bool IsClassEligibleForVisualVariation(
		const UClass* ActorClass,
		const TArray<TSubclassOf<APawn>>& StandardTargetClasses,
		const TArray<TSubclassOf<APawn>>& OptionalMatureTargetClasses);

private:
	bool ResolveConfiguredClasses();
	void ProcessExistingPawns();
	void HandleActorSpawned(AActor* SpawnedActor);
	void QueueVariationApplication(APawn* Pawn, int32 AttemptIndex);
	void TryApplyVariation(TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex);
	bool ShouldProcessPawn(const APawn* Pawn) const;
	bool IsTargetEnemyClass(const UClass* ActorClass) const;
	bool IsOptionalMatureMorphTargetClass(const UClass* ActorClass) const;
	UEFCharacterCustomizationComponent* FindOrCreateCustomizationComponent(APawn* Pawn) const;
	bool GatherAllowedMorphEntries(const UEFCharacterCustomizationComponent* CustomizationComponent, TArray<FMorphSliderEntry>& OutEntries) const;
	bool GatherMorphEntriesForConfiguredName(
		const UEFCharacterCustomizationComponent* CustomizationComponent,
		FName MorphName,
		TArray<FMorphSliderEntry>& OutEntries) const;
	void ApplyConfiguredOptionalMatureMorphGroups(APawn* Pawn, UEFCharacterCustomizationComponent* CustomizationComponent) const;
	int32 ResolveEnemyLevel(const APawn* Pawn) const;
	bool ApplyMorphEntriesWithValue(
		UEFCharacterCustomizationComponent* CustomizationComponent,
		const TArray<FMorphSliderEntry>& Entries,
		float Value) const;
	void InitializeOptionalMatureMorphState(APawn* Pawn, UEFCharacterCustomizationComponent* CustomizationComponent);
	void UpdateSessionDrivenOptionalMatureMorphs(float CurrentTimeSeconds);
	void AdvanceOptionalMatureMorphs(float DeltaTime);
	bool IsPawnDead(const APawn* Pawn, const UProjectCombatAttributeComponent* CombatComponent) const;
	void ApplyOptionalMatureMorphState(FProjectEnemyOptionalMatureMorphState& MorphState, float PresentationAlpha) const;
	void CleanupTrackedOptionalMatureMorphStates();
	bool IsOptionalMatureMorphPresentationAllowed() const;
	void EnableOptionalMatureMorphsForExistingPawns();
	void DisableOptionalMatureMorphsForExistingPawns();

	UFUNCTION()
	void HandleContentPolicyChanged(FProjectContentPolicySnapshot Policy);

	void MarkActorProcessed(const AActor* Actor);
	bool IsActorProcessed(const AActor* Actor) const;
	bool IsActorPending(const AActor* Actor) const;
	void MarkActorPending(const AActor* Actor);
	void ClearActorPending(const AActor* Actor);

private:
	FDelegateHandle ActorSpawnedHandle;
	TArray<TSubclassOf<APawn>> TargetEnemyClasses;
	TArray<TSubclassOf<APawn>> OptionalMatureMorphTargetEnemyClasses;
	TSet<FName> AllowedMorphNameSet;
	TSet<TObjectKey<UObject>> ProcessedActors;
	TSet<TObjectKey<UObject>> PendingActors;
	TMap<TObjectKey<APawn>, FProjectEnemyOptionalMatureMorphState> TrackedOptionalMatureMorphStates;
	TWeakObjectPtr<UProjectContentPolicySubsystem> ContentPolicySubsystem;
	bool bInitialPawnScanPending = true;
	bool bConfiguredClassesPending = true;
	bool bOptionalMatureMorphPolicyAllowed = false;
	double LastOptionalMatureVisibilityPollTimeSeconds = -1.0;
};
