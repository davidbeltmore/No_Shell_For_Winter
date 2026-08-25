#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFClothingFitRuntimeComponent.generated.h"

class UEFCharacterCustomizationComponent;
class UEFClothingFitProfile;
class UEFClothingFitRegistry;
class USkeletalMesh;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (EF), meta = (BlueprintSpawnableComponent))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingFitRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFClothingFitRuntimeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2")
	void ForceReconcile();

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2")
	int32 GetAppliedGarmentCount() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2")
	FString GetDebugSummary() const;

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2")
	void SetRuntimeClearanceMultiplier(float NewMultiplier);

private:
	struct FAppliedGarmentState
	{
		TWeakObjectPtr<const UEFClothingFitProfile> Profile;
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		TWeakObjectPtr<USkeletalMesh> FittedMesh;
		TWeakObjectPtr<USkeletalMeshComponent> BodyMesh;
		bool bWasVisible = true;
		bool bWaitingForSkinProfile = false;
		float PreviousBoundsScale = 1.0f;
		double ApplyStartedAtSeconds = 0.0;
		TMap<FName, float> LastWrittenMorphValues;
	};

	void ReconcileGarments();
	void SynchronizeMorphs();
	void ResolveCustomizationComponent();
	void HandleMorphStateApplied();
	USkeletalMeshComponent* ResolveBodyMesh(const UEFClothingFitProfile* Profile) const;
	bool TryApplyProfile(USkeletalMeshComponent* GarmentComponent, const UEFClothingFitProfile* Profile);
	void RemoveStaleStates();
	void RestoreGarment(USkeletalMeshComponent* GarmentComponent, FAppliedGarmentState& State, bool bRestoreSourceMesh);
	void RestoreAllGarments();
	bool ValidateProfileForComponents(
		const UEFClothingFitProfile* Profile,
		USkeletalMeshComponent* GarmentComponent,
		USkeletalMeshComponent* BodyComponent,
		USkeletalMesh* FittedMesh,
		FString& OutFailureReason) const;
	float ResolveBodyMorphValue(
		USkeletalMeshComponent* BodyComponent,
		USkeletalMeshComponent* GarmentComponent,
		FName MorphName) const;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingFitRegistry> LoadedRegistry;

	UPROPERTY(Transient)
	TObjectPtr<UEFCharacterCustomizationComponent> CustomizationComponent;

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState> AppliedGarments;
	FDelegateHandle MorphStateAppliedHandle;
	double NextReconcileAtSeconds = 0.0;
	double NextMorphSyncAtSeconds = 0.0;
	float RuntimeClearanceMultiplier = 1.0f;
	bool bLastRuntimeEnabled = false;
	FString LastStatus = TEXT("Not initialized");
};
