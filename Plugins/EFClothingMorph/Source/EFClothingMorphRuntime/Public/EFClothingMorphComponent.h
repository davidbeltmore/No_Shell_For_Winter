#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "EFClothingMorphComponent.generated.h"

class UActorComponent;
class UEFCharacterCustomizationComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UScriptStruct;
class USkinnedAsset;

UENUM(BlueprintType)
enum class EEFClothingPiece : uint8
{
	Auto UMETA(DisplayName = "Auto"),
	Head UMETA(DisplayName = "Head"),
	Chest UMETA(DisplayName = "Chest"),
	Pants UMETA(DisplayName = "Pants"),
	Gloves UMETA(DisplayName = "Gloves"),
	Boots UMETA(DisplayName = "Boots"),
	Arms UMETA(DisplayName = "Arms"),
	Loin UMETA(DisplayName = "Loin"),
	Other UMETA(DisplayName = "Other")
};

UENUM(BlueprintType)
enum class EEFBodyRegion : uint8
{
	None UMETA(DisplayName = "None"),
	Head UMETA(DisplayName = "Head"),
	Chest UMETA(DisplayName = "Chest"),
	Waist UMETA(DisplayName = "Waist"),
	Pelvis UMETA(DisplayName = "Pelvis"),
	Glute UMETA(DisplayName = "Glute"),
	Thigh UMETA(DisplayName = "Thigh"),
	Calf UMETA(DisplayName = "Calf"),
	Foot UMETA(DisplayName = "Foot"),
	UpperArm UMETA(DisplayName = "Upper Arm"),
	Forearm UMETA(DisplayName = "Forearm"),
	Hand UMETA(DisplayName = "Hand")
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFBodyRegionClearanceProxy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (DisplayName = "Body Region"))
	EEFBodyRegion Region = EEFBodyRegion::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (DisplayName = "Anchor Bone", ToolTip = "Main bone that represents this body region."))
	FName AnchorBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (DisplayName = "Measure Bone A", ToolTip = "Optional first bone used to estimate the current body span for this region."))
	FName MeasureBoneA = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (DisplayName = "Measure Bone B", ToolTip = "Optional second bone used to estimate the current body span for this region."))
	FName MeasureBoneB = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Base Proxy Radius Cm", ToolTip = "Base radius used by this mini collision proxy when no body measurement is available."))
	float BaseProxyRadiusCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Target Clearance Cm", ToolTip = "Desired separation above the body for this region."))
	float TargetClearanceCm = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Morph Expansion Per Unit Cm", ToolTip = "How much this region should grow per 1.0 of region morph magnitude."))
	float MorphExpansionPerUnitCm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precision Layer", meta = (DisplayName = "Driving Morph Tokens", ToolTip = "Optional tokens that force this region to watch specific body morphs. Leave empty to use automatic semantic region matching."))
	TArray<FString> DrivingMorphTokens;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphScaleRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Morph Name Contains", ToolTip = "If a morph name contains this text, the clothing mesh will receive extra scale for that morph. Example: glute"))
	FString MatchToken;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Scale", ToolTip = "Extra multiplier applied when the token matches. 1.10 means 10% bigger than the incoming body morph value."))
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Clothing Piece", ToolTip = "Optional clothing piece filter. Auto means the rule can apply to any clothing mesh."))
	EEFClothingPiece ClothingPiece = EEFClothingPiece::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Positive Scale", ToolTip = "Extra multiplier used when the incoming body morph value is positive."))
	float PositiveScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Negative Scale", ToolTip = "Extra multiplier used when the incoming body morph value is negative."))
	float NegativeScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Activation Threshold", ToolTip = "Minimum absolute body morph value required before this rule adds extra influence."))
	float ActivationThreshold = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Additive Bias", ToolTip = "Optional extra additive value applied after scaling. Useful as a preventive push once the morph is strong enough."))
	float AdditiveBias = 0.0f;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMeshScaleOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Clothing Mesh Component Name", ToolTip = "Component name for a clothing mesh that needs a custom multiplier. Example: Panties"))
	FName MeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Multiplier", ToolTip = "Extra multiplier applied to every copied morph for this clothing mesh after the base clothing scale."))
	float Multiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingPieceScaleOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Clothing Piece"))
	EEFClothingPiece ClothingPiece = EEFClothingPiece::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Multiplier"))
	float Multiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMeshPieceOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Clothing Mesh Component Name"))
	FName MeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Clothing Piece"))
	EEFClothingPiece ClothingPiece = EEFClothingPiece::Auto;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFACFUSlotPieceOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "ACFU Slot Tag", ToolTip = "ACFU slot tag used by the armor slot component, for example Itemslot.Armor.Legs."))
	FGameplayTag SlotTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Clothing Piece", ToolTip = "Optional clothing piece override for this ACFU slot. Useful when one ACFU slot should behave like Loin instead of Pants."))
	EEFClothingPiece ClothingPiece = EEFClothingPiece::Auto;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFACFUBaseClothingMeshEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Source Mesh Component Name", ToolTip = "Existing clothing mesh component to convert into an ACFU base armor slot, for example Panties."))
	FName SourceMeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "ACFU Slot Tag", ToolTip = "Target ACFU equipment slot, for example Itemslot.Armor.Legs. The created or reused armor slot will keep this mesh as its empty fallback."))
	FGameplayTag SlotTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Clothing Piece", ToolTip = "Optional piece override used by EF Clothing Morph after the mesh is promoted into the ACFU slot."))
	EEFClothingPiece ClothingPiece = EEFClothingPiece::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Hide Source Mesh", ToolTip = "If enabled, the original source mesh component will be hidden after the ACFU armor slot is created."))
	bool bHideSourceMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Copy Relative Transform", ToolTip = "If enabled, the generated ACFU armor slot copies the source mesh relative transform."))
	bool bCopyRelativeTransform = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Copy Materials", ToolTip = "If enabled, the generated ACFU armor slot copies the source mesh material overrides."))
	bool bCopyMaterials = true;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFACFUQuickEquipItemEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "ACFU Item Class", MetaClass = "/Script/InventorySystem.ACFItem", ToolTip = "ACFU clothing or armor item class to equip through the owner's ACFU equipment component."))
	TSoftClassPtr<UObject> ItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Preferred Slot Tag", ToolTip = "Optional preferred ACFU slot tag. Leave empty to let ACFU choose from the item configuration."))
	FGameplayTag PreferredSlotTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Equip On Begin Play", ToolTip = "If enabled, this ACFU item will be equipped automatically during BeginPlay when ACFU integration is active."))
	bool bEquipOnBeginPlay = false;
};

UCLASS(ClassGroup = (Character), meta = (BlueprintSpawnableComponent, DisplayName = "EF Clothing Morph", ShortTooltip = "Copies shared or compatible body morphs to clothing meshes and lets you enlarge clothing morphs to reduce clipping."))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFClothingMorphComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Clothing Morph")
	void RefreshMeshBindings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Clothing Morph")
	void ApplyMorphsNow();

	UFUNCTION(BlueprintCallable, Category = "Clothing Morph|Scaling")
	void SetBaseClothingMorphMultiplier(float NewMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Clothing Morph|Scaling")
	void SetClothingMeshMorphMultiplier(FName MeshComponentName, float NewMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Clothing Morph|Scaling")
	void SetClothingPieceMorphMultiplier(EEFClothingPiece ClothingPiece, float NewMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Clothing Morph|Scaling")
	void ClearClothingMeshMorphMultiplier(FName MeshComponentName);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Clothing Morph")
	void ResetToRecommendedScaling();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Clothing Morph|ACFU")
	void ConvertConfiguredMeshesToACFUBaseClothing();

	UFUNCTION(BlueprintCallable, Category = "Clothing Morph|ACFU")
	void EquipConfiguredACFUClothing();

	UFUNCTION(BlueprintCallable, Category = "Clothing Morph|ACFU")
	bool EquipACFUClothingItemByClass(TSubclassOf<UObject> ItemClass, FGameplayTag PreferredSlotTag);

	UFUNCTION(BlueprintPure, Category = "Clothing Morph|Debug")
	FString GetSetupSummary() const;

protected:
	UFUNCTION()
	void HandleACFUArmorSlotChanged(FGameplayTag ArmorSlot);

	bool ResolveBodyMeshComponent();
	void ResolveClothingMeshComponents();
	void RebuildMorphBindingCache();
	TArray<FName> GetMorphNames(const USkeletalMeshComponent* MeshComponent) const;
	bool ShouldTrackAsClothing(const USkeletalMeshComponent* MeshComponent) const;
	void UpdatePrecisionLayer(const USkeletalMeshComponent* BodyMeshComponent);
	FName ResolveCompatibleClothingMorphName(const FName BodyMorphName, const USkeletalMeshComponent* ClothingMesh, const TArray<FName>& ClothingMorphNames, TSet<FName>& InOutUsedClothingMorphNames, bool& bOutUsedCompatibleMatch) const;
	float ResolveScaleForMorph(const FName BodyMorphName, float BodyValue, const USkeletalMeshComponent* ClothingMesh) const;
	float ResolveMeshScaleMultiplier(const USkeletalMeshComponent* ClothingMesh) const;
	float ResolvePieceScaleMultiplier(EEFClothingPiece ClothingPiece) const;
	EEFClothingPiece ResolveClothingPieceForMesh(const USkeletalMeshComponent* ClothingMesh) const;
	EEFBodyRegion ResolveBodyRegionForMorph(const FName BodyMorphName) const;
	float ResolveRegionRiskMultiplier(EEFBodyRegion BodyRegion, EEFClothingPiece ClothingPiece) const;
	float ResolveRegionRiskBias(EEFBodyRegion BodyRegion, float BodyValue, EEFClothingPiece ClothingPiece) const;
	bool NeedsBindingRefresh() const;
	void CacheResolvedMeshAssets();
	void FinalizeMorphUpdate(USkeletalMeshComponent* MeshComponent) const;
	void ResetDebugState();
	void UpdateResolvedMeshDebugNames();
	UEFCharacterCustomizationComponent* ResolveCharacterCustomizationComponent() const;
	USkeletalMeshComponent* ResolveMeshComponentByName(FName MeshComponentName) const;
	void SyncACFUSlotLeaderPose();
	void RefreshACFUIntegrationState();
	void ProcessPendingACFURefreshes();
	bool IsACFUArmorSlotComponent(const UActorComponent* Component) const;
	UActorComponent* ResolveACFUEquipmentComponent() const;
	UClass* ResolveACFUArmorSlotComponentClass() const;
	void ResolveACFUSlotMappings();
	bool TryGetACFUSlotRenderMesh(const UObject* ArmorSlotComponent, USkeletalMeshComponent*& OutRenderMesh) const;
	bool CanUseLeaderPose(const USkeletalMeshComponent* LeaderMesh, const USkeletalMeshComponent* FollowerMesh) const;
	bool BindToACFUEquipmentEvents();
	void UnbindFromACFUEquipmentEvents();
	bool TryGetACFUSlotTag(const UObject* ArmorSlotComponent, FGameplayTag& OutSlotTag) const;
	EEFClothingPiece ResolveClothingPieceFromACFUSlot(const FGameplayTag& SlotTag) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Integration", meta = (DisplayName = "Prefer EF Character Customization", ToolTip = "If enabled and the owner has an EFCharacterCustomizationComponent, this component will reuse its resolved body and clothing meshes first."))
	bool bPreferCharacterCustomizationComponent = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Enable ACFU Integration", ToolTip = "If enabled, EF Clothing Morph will detect ACFU equipment and armor slot components by reflection when available."))
	bool bEnableACFUIntegration = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Use ACFU Armor Slots As Clothing", ToolTip = "If enabled, EF Clothing Morph tracks the real skeletal mesh components rendered by ACFU armor slots and classifies them by slot tag."))
	bool bUseACFUArmorSlotsAsClothing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Apply Leader Pose To ACFU Armor Meshes", ToolTip = "If enabled, EF Clothing Morph applies SetLeaderPoseComponent to the real modular armor meshes rendered by ACFU so the visual result matches the manual setup."))
	bool bApplyLeaderPoseToACFUArmorMeshes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Bind To ACFU Equipment Changes", ToolTip = "If enabled, the component listens to ACFU OnEquippedArmorChanged and refreshes mesh bindings after armor changes."))
	bool bBindToACFUEquipmentChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Auto Convert Configured Base Clothing", ToolTip = "Legacy experimental path kept for backward compatibility. It is ignored at runtime so ACFU remains the sole owner of real equipment slots."))
	bool bAutoConvertConfiguredBaseClothing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "Auto Equip Configured ACFU Items", ToolTip = "Legacy experimental path kept for backward compatibility. It is ignored at runtime so ACFU remains the sole owner of inventory and equip flow."))
	bool bAutoEquipConfiguredACFUItems = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "ACFU Refresh Delay Seconds", ToolTip = "Delay between automatic refresh retries after an ACFU armor change. Useful because ACFU armor meshes are loaded asynchronously."))
	float ACFURefreshDelaySeconds = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (ClampMin = "0", UIMin = "0", DisplayName = "ACFU Refresh Retry Count", ToolTip = "How many delayed refresh retries EF Clothing Morph performs after an ACFU armor change."))
	int32 ACFURefreshRetryCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "ACFU Slot Piece Overrides", ToolTip = "Optional clothing piece overrides by ACFU slot tag. Example: Itemslot.Armor.Legs -> Loin."))
	TArray<FEFACFUSlotPieceOverride> ACFUSlotPieceOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "ACFU Base Clothing Meshes", ToolTip = "Simple setup to promote existing follower meshes, such as Panties, into reusable ACFU armor slots with an empty-slot fallback."))
	TArray<FEFACFUBaseClothingMeshEntry> ACFUBaseClothingMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACFU Integration", meta = (DisplayName = "ACFU Quick Equip Items", ToolTip = "Optional ACFU item classes that this component can equip directly through the owner's ACFU equipment component."))
	TArray<FEFACFUQuickEquipItemEntry> ACFUQuickEquipItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Body Mesh Component Name", ToolTip = "Name of the body mesh component that drives the morphs. Leave empty to use EFCharacterCustomization when available, otherwise auto-pick the best body mesh for Test or ACF characters."))
	FName BodyMeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Clothing Mesh Component Names", ToolTip = "Optional clothing mesh component names to follow. Example: Panties. Leave empty if auto-find is enabled."))
	TArray<FName> ClothingMeshComponentNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Auto Find Attached Clothing Meshes", ToolTip = "If enabled and Clothing Mesh Component Names is empty, the component will consider skeletal meshes attached to the body mesh as clothing candidates."))
	bool bAutoFindAttachedClothingMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Auto Find Leader Pose Clothing Meshes", ToolTip = "If enabled and Clothing Mesh Component Names is empty, the component will consider meshes using the body mesh as Leader Pose / Set Leader Component followers."))
	bool bAutoFindLeaderPoseClothingMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Allow Compatible Morph Names", ToolTip = "If enabled, the component also matches clothing morphs that normalize to the same name as the body morph, ignoring case, separators, and a few common DAZ variations."))
	bool bAllowCompatibleMorphNames = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Allow Semantic Region Matching", ToolTip = "If enabled, the component can bind morphs by semantic body region when exact or normalized names do not match, for example glute to hip on lower-body clothing."))
	bool bAllowSemanticRegionMatching = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Enable Precision Layer", DisplayAfter = "bAllowSemanticRegionMatching", ToolTip = "If enabled, the component evaluates risk by body region and adds extra preventive clearance without using real collision."))
	bool bEnablePrecisionLayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Enable Body Clearance Proxies", DisplayAfter = "bEnablePrecisionLayer", ToolTip = "If enabled, body region proxies estimate body size and desired separation to reduce clipping."))
	bool bEnableBodyClearanceModel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Region Risk Smoothing Speed", DisplayAfter = "bEnableBodyClearanceModel", ToolTip = "How fast region risk reacts to body morph changes. Higher is more responsive, lower is smoother."))
	float RegionRiskSmoothingSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Precision Multiplier Scale", DisplayAfter = "ClothingPieceScaleOverrides", ToolTip = "Converts region clearance risk into an extra clothing morph multiplier."))
	float PrecisionLayerMultiplierScale = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Precision Bias Scale", DisplayAfter = "PrecisionLayerMultiplierScale", ToolTip = "Converts region clearance in cm into an additive bias over the copied morph value."))
	float PrecisionLayerBiasScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Max Precision Multiplier Bonus", DisplayAfter = "PrecisionLayerBiasScale", ToolTip = "Maximum extra multiplier added by the precision layer."))
	float MaxPrecisionMultiplierBonus = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Max Precision Bias", DisplayAfter = "MaxPrecisionMultiplierBonus", ToolTip = "Maximum additive bias contributed by the precision layer."))
	float MaxPrecisionBias = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Base Clothing Morph Multiplier", ToolTip = "Global multiplier applied to every copied clothing morph. 1.0 means exact copy. 1.08 means the clothing stays 8% larger than the body morph."))
	float BaseClothingScale = 1.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Body Clearance Proxies", DisplayAfter = "BaseClothingScale", ToolTip = "Mini collision-like body proxies used to estimate how much extra space each body region needs."))
	TArray<FEFBodyRegionClearanceProxy> BodyClearanceProxies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Setup", meta = (DisplayName = "Clothing Mesh Piece Overrides", ToolTip = "Optional manual clothing piece assignment per mesh component. Useful when automatic piece detection gets it wrong."))
	TArray<FEFClothingMeshPieceOverride> ClothingMeshPieceOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Clothing Mesh Multipliers", ToolTip = "Optional per-mesh multipliers so you can push one clothing piece more than the others, for example Panties = 1.12."))
	TArray<FEFClothingMeshScaleOverride> ClothingMeshScaleOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Clothing Piece Multipliers", ToolTip = "Optional extra multipliers by clothing piece, inspired by piece-based cloth setups. Example: Chest = 1.10 or Pants = 1.08."))
	TArray<FEFClothingPieceScaleOverride> ClothingPieceScaleOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Scaling", meta = (DisplayName = "Special Morph Scale Rules", ToolTip = "Optional per-token multipliers for morphs that need extra room, like glutes or thighs."))
	TArray<FEFClothingMorphScaleRule> SpecialMorphScaleRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Update Interval Seconds", ToolTip = "0 updates every tick. A positive value throttles how often morphs are copied."))
	float UpdateIntervalSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (DisplayName = "Auto Refresh Mesh Bindings", ToolTip = "If enabled, the component will rebind when the body mesh or clothing meshes change their skeletal mesh asset at runtime."))
	bool bAutoRefreshMeshBindings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (DisplayName = "Log Setup Warnings", ToolTip = "Logs a warning once when the body mesh or clothing meshes cannot be resolved."))
	bool bLogSetupWarnings = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved Body Mesh"))
	FName ResolvedBodyMeshDebugName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved Clothing Meshes"))
	TArray<FName> ResolvedClothingMeshDebugNames;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved Clothing Pieces"))
	TArray<FString> ResolvedClothingPieceDebug;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved ACFU Equipment Component"))
	FName ResolvedACFUEquipmentComponentDebugName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved ACFU Slot Tags"))
	TArray<FString> ResolvedACFUSlotDebug;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Body Region Risk Summary"))
	TArray<FString> BodyRegionRiskDebug;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Clothing Binding Summary"))
	TArray<FString> ResolvedClothingBindingDebug;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Resolved Body Morph Count"))
	int32 LastResolvedBodyMorphCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Compatible Match Count"))
	int32 LastCompatibleMatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Applied Morph Count"))
	int32 LastAppliedMorphCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Updated Clothing Mesh Count"))
	int32 LastUpdatedClothingMeshCount = 0;

private:
	struct FEFClothingMorphBinding
	{
		FName BodyMorphName = NAME_None;
		FName ClothingMorphName = NAME_None;
		bool bUsedCompatibleMatch = false;
	};

	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> BodyMeshComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> ClothingMeshComponents;

	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMesh> CachedBodyMeshAsset;

	UPROPERTY(Transient)
	TWeakObjectPtr<UActorComponent> BoundACFUEquipmentComponent;

	TMap<USkeletalMeshComponent*, TArray<FEFClothingMorphBinding>> MorphBindingsByClothingMesh;
	TMap<USkeletalMeshComponent*, TWeakObjectPtr<USkeletalMesh>> CachedClothingMeshAssets;
	TMap<USkeletalMeshComponent*, EEFClothingPiece> ResolvedClothingPiecesByMesh;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FGameplayTag> ACFUSlotTagsByMesh;
	TMap<EEFBodyRegion, float> SmoothedRegionRiskByRegion;
	TMap<EEFBodyRegion, float> RegionMultiplierBonusByRegion;
	TMap<EEFBodyRegion, float> RegionBiasByRegion;

	double NextUpdateTimeSeconds = 0.0;
	double NextACFURefreshTimeSeconds = 0.0;
	int32 PendingACFURefreshes = 0;
	bool bLoggedMissingBodyWarning = false;
	bool bLoggedMissingClothingWarning = false;
	bool bACFUArmorChangedDelegateBound = false;
};
