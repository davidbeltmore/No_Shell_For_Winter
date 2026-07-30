#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFCharacterCreationTypes.h"
#include "GameplayTagContainer.h"
#include "EFCharacterCustomizationComponent.generated.h"

class UEFCharacterCustomizationSaveGame;
class UMaterialInstanceDynamic;
class USkeletalMesh;
class USkeletalMeshComponent;

DECLARE_MULTICAST_DELEGATE(FEFCharacterCustomizationMorphStateApplied);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class EFCHARACTERCREATIONRUNTIME_API UEFCharacterCustomizationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFCharacterCustomizationComponent();

	bool InitializeForActor(AActor* InOwningActor);
	bool EvaluateCompatibilityForActor(AActor* InOwningActor, FString& OutFailureReason);

	bool IsMeshCompatible() const { return bMeshCompatible; }
	const FString& GetCompatibilityError() const { return CompatibilityError; }

	USkeletalMeshComponent* GetBodyMeshComponent() const { return BodyMeshComponent.Get(); }
	USkeletalMeshComponent* GetBodyMeshSelectionComponent() const { return BodyMeshSelectionComponent.Get(); }
	USkeletalMeshComponent* GetHairMeshComponent() const { return HairMeshComponent.Get(); }
	const TArray<TObjectPtr<USkeletalMeshComponent>>& GetClothingMeshComponents() const { return ClothingMeshComponents; }

	const TArray<FMorphSliderEntry>& GetAvailableMorphEntries() const { return AvailableMorphEntries; }
	TArray<FMorphSliderEntry> GetAvailableMorphEntriesForCategory(const FName Category, const FString& SearchFilter) const;
	TArray<FName> GetAvailableCategories() const;
	const TArray<FCharacterSkeletalMeshOption>& GetAvailableBodyMeshOptions() const { return AvailableBodyMeshOptions; }
	const TArray<FCharacterSkeletalMeshOption>& GetAvailableHairMeshOptions() const { return AvailableHairMeshOptions; }
	FEFCharacterCustomizationMorphStateApplied& OnMorphStateApplied() { return MorphStateAppliedEvent; }

	bool ApplyMorph(const FMorphSliderEntry& Entry, float NewValue);
	bool ResetMorph(const FMorphSliderEntry& Entry);
	void ResetAllToDefaults();
	void RandomizeAll(int32 Seed = INDEX_NONE);

	FCharacterCustomizationState CaptureCurrentState() const;
	void ApplyState(const FCharacterCustomizationState& NewState);
	bool SaveCurrentStateAsConfirmed();

	bool SavePreset(const FString& PresetName);
	bool LoadPreset(const FString& PresetName);
	TArray<FString> GetPresetNames() const;

	float GetCurrentMorphValue(const FMorphSliderEntry& Entry) const;

	void SetShowClothes(bool bShowClothes);
	bool GetShowClothes() const { return bShowClothes; }

	void SetPauseAnimation(bool bPauseAnimation);
	bool GetPauseAnimation() const { return bPauseAnimation; }

	void SetSkinColor(const FLinearColor& InSkinColor);
	FLinearColor GetSkinColor() const { return CurrentSkinColor; }

	void SetIrisColor(const FLinearColor& InIrisColor);
	FLinearColor GetIrisColor() const { return CurrentIrisColor; }

	UFUNCTION(BlueprintCallable, Category = "EF Character Creation|Identity")
	void SetCharacterName(const FString& InCharacterName);

	UFUNCTION(BlueprintPure, Category = "EF Character Creation|Identity")
	FString GetCharacterName() const { return CurrentCharacterName; }

	UFUNCTION(BlueprintPure, Category = "EF Character Creation|Identity")
	FString GetResolvedCharacterName() const;

	UFUNCTION(BlueprintCallable, Category = "EF Character Creation|Identity")
	bool SetGender(ECharacterCreationGender InGender);

	UFUNCTION(BlueprintCallable, Category = "EF Character Creation|Identity")
	bool SelectGender(ECharacterCreationGender InGender);

	UFUNCTION(BlueprintPure, Category = "EF Character Creation|Identity")
	ECharacterCreationGender GetGender() const { return CurrentGender; }

	UFUNCTION(BlueprintPure, Category = "EF Character Creation|Identity")
	FGameplayTag GetGenderGameplayTag() const;

	bool CanCustomizeBodyMesh() const { return AvailableBodyMeshOptions.Num() > 1; }
	bool HasHairCustomization() const { return IsValid(HairMeshComponent.Get()) && AvailableHairMeshOptions.Num() > 0; }
	bool CanEditHairTransform() const { return IsValid(HairMeshComponent.Get()); }
	FString GetCurrentBodyMeshDisplayName() const;
	FString GetCurrentHairMeshDisplayName() const;
	bool SelectRelativeBodyMeshOption(int32 Direction);
	bool SelectRelativeHairMeshOption(int32 Direction);
	FTransform GetHairRelativeTransform() const;
	bool SetHairRelativeTransform(const FTransform& InTransform);
	void ResetHairTransformToDefault();

	FCharacterCustomizationState BuildDefaultState() const;

private:
	void ResetDiscoveredData();
	void DiscoverMeshComponents(AActor* InOwningActor);
	void BuildMeshSelectionOptions();
	void BuildBodyMeshOptions();
	void BuildHairMeshOptions();
	void BuildMorphEntries();
	void SortMorphEntries();

	void ApplyStateInternal(const FCharacterCustomizationState& NewState, bool bApplyMeshSelections);
	void BroadcastIdentityChanged() const;
	bool ValidateCompatibility();
	bool IsMeshCompatibleWithSystem(USkeletalMesh* SkeletalMesh, const TArray<FName>& MorphNames, FString& OutFailureReason) const;
	bool IsSkeletonCompatibleWithReference(USkeletalMesh* SkeletalMesh, USkeletalMesh* ReferenceMesh) const;
	TArray<FName> GatherMorphNames(USkeletalMeshComponent* MeshComponent) const;
	TArray<FName> GatherMorphNames(USkeletalMesh* SkeletalMesh) const;
	void GatherMorphNamesFromAssetRegistry(USkeletalMesh* SkeletalMesh, TArray<FName>& OutMorphNames) const;

	FName InferCategoryForMorphName(const FString& MorphName) const;
	FString InferSectionForMorphName(const FString& MorphName) const;
	bool MatchesAnyHint(const FString& SourceString, const TArray<FString>& Hints) const;
	bool IsHairAssetExcluded(const FString& SourceString) const;
	FString GetDisplayNameForMesh(USkeletalMesh* SkeletalMesh, const FString& PreferredLabel = FString()) const;
	void AddMeshOptionIfValid(TArray<FCharacterSkeletalMeshOption>& Options, USkeletalMesh* SkeletalMesh, const FString& PreferredLabel) const;
	int32 FindMeshOptionIndex(const TArray<FCharacterSkeletalMeshOption>& Options, USkeletalMesh* SkeletalMesh) const;
	USkeletalMeshComponent* GetActiveBodyMeshComponent() const;
	USkeletalMesh* ResolveGenderMesh(ECharacterCreationGender InGender) const;
	ECharacterCreationGender InferGenderForMesh(USkeletalMesh* SkeletalMesh) const;
	ECharacterCreationGender ResolveInitialGender() const;
	bool IsConfiguredGenderMesh(USkeletalMesh* SkeletalMesh) const;
	FString SanitizeCharacterName(const FString& InCharacterName) const;
	bool ApplyBodySkeletalMesh(USkeletalMesh* SkeletalMesh);
	bool ApplyHairSkeletalMesh(USkeletalMesh* SkeletalMesh);
	void InvalidateMeshDependentCaches();
	void ResetMeshComponentMaterialsToAssetDefaults(USkeletalMeshComponent* MeshComponent);

	USkeletalMeshComponent* ResolveTargetMeshComponent(const FMorphSliderEntry& Entry) const;
	USkeletalMeshComponent* ResolveComponentByName(FName ComponentName) const;

	FString MakeMorphKey(const FMorphSliderEntry& Entry) const;
	FString MakeMorphKey(const FCharacterMorphValue& MorphValue) const;

	void SetCurrentMorphValue(const FMorphSliderEntry& Entry, float Value);
	bool ApplyMorphValue(const FMorphSliderEntry& Entry, float Value);
	void ApplyCurrentMorphState();
	void GatherTargetMeshComponents(const FMorphSliderEntry& Entry, TArray<USkeletalMeshComponent*>& OutMeshComponents) const;
	void ApplyMorphToMeshComponent(USkeletalMeshComponent* MeshComponent, FName MorphName, float Value) const;
	void FinalizeMorphUpdate(USkeletalMeshComponent* MeshComponent) const;
	bool SavePresetData(const FCharacterPresetData& PresetData);
	bool LoadPresetData(const FString& PresetName, FCharacterPresetData& OutPresetData) const;
	UEFCharacterCustomizationSaveGame* LoadOrCreateSaveGame() const;

	void ApplySkinColorToMesh(USkeletalMeshComponent* MeshComponent);
	void ApplyIrisColorToMesh(USkeletalMeshComponent* MeshComponent);
	void ApplyMaterialColorToMesh(USkeletalMeshComponent* MeshComponent, const FLinearColor& Color, const TArray<FName>& ParameterNames, const TArray<FString>& MaterialHints, TMap<TObjectPtr<USkeletalMeshComponent>, TArray<int32>>& MaterialSlotCache, bool bExcludeEyeMoisture);
	TArray<int32>& ResolveCachedMaterialSlots(USkeletalMeshComponent* MeshComponent, const TArray<FString>& MaterialHints, TMap<TObjectPtr<USkeletalMeshComponent>, TArray<int32>>& MaterialSlotCache, bool bExcludeEyeMoisture);

private:
	FEFCharacterCustomizationMorphStateApplied MorphStateAppliedEvent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> OwningActor;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMeshSelectionComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> DefaultBodyMeshSelectionAsset;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> HairMeshComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> ClothingMeshComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> AllDiscoveredMeshComponents;

	UPROPERTY(Transient)
	TArray<FMorphSliderEntry> AvailableMorphEntries;

	UPROPERTY(Transient)
	TArray<FCharacterSkeletalMeshOption> AvailableBodyMeshOptions;

	UPROPERTY(Transient)
	TArray<FCharacterSkeletalMeshOption> AvailableHairMeshOptions;

	TMap<FString, float> CurrentMorphValues;
	mutable TMap<FString, TArray<FName>> MeshMorphNameCache;
	mutable TMap<FString, TArray<TWeakObjectPtr<USkeletalMeshComponent>>> MorphTargetMeshComponentCache;
	TMap<TObjectPtr<USkeletalMeshComponent>, TArray<TObjectPtr<UMaterialInstanceDynamic>>> DynamicMaterialInstances;
	mutable TMap<TObjectPtr<USkeletalMeshComponent>, TArray<int32>> SkinMaterialSlotCache;
	mutable TMap<TObjectPtr<USkeletalMeshComponent>, TArray<int32>> IrisMaterialSlotCache;
	FTransform DefaultHairRelativeTransform = FTransform::Identity;

	bool bMeshCompatible = false;
	FString CompatibilityError;
	FString CurrentCharacterName;
	ECharacterCreationGender CurrentGender = ECharacterCreationGender::Female;
	bool bShowClothes = true;
	bool bPauseAnimation = false;
	FLinearColor CurrentSkinColor = FLinearColor::White;
	FLinearColor CurrentIrisColor = FLinearColor::White;
};

