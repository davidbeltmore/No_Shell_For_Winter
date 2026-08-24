#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "TattooShop/ProjectTattooShopStateSubsystem.h"
#include "TattooShop/UI/ProjectTattooShopUITypes.h"
#include "ProjectTattooShopInputSubsystem.generated.h"

class APlayerController;
class APawn;
class UGridPanel;
class UInputComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPanelWidget;
class UProjectTattooShopEditorWidget;
class UProjectTattooShopLibraryWidget;
class USkeletalMeshComponent;
class UTexture;
class UTexture2D;
class UUserWidget;
class UWidget;
class SWidget;

struct FProjectTattooShopInputSnapshot
{
	bool bHasControllerState = false;
	bool bWasMouseCursorVisible = false;
	bool bWereClickEventsEnabled = false;
	bool bWereMouseOverEventsEnabled = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopInputSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop")
	void RequestToggleTattooShop();

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop")
	void RequestOpenTattooShopInHost(UPanelWidget* HostPanel);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop")
	void RequestOpenTattooShopInHosts(UPanelWidget* HostPanel, UPanelWidget* AssetPreviewPanel);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop")
	void RequestCloseTattooShop();

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop")
	bool IsTattooShopOpen() const;

	/** SkinnedDecal-only transaction API used by Character Creation schema v5. */
	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	FGuid BeginCreateTattoo(const FProjectTattooParameters& InitialParameters);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	bool BeginEditTattoo(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	bool PreviewTattoo(const FGuid& TattooId, const FProjectTattooParameters& Parameters);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	bool CommitTattoo(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	bool CancelTattoo(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	bool DeleteTattoo(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	TArray<FProjectTattooShopCardData> GetTattooCatalog();

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	TArray<FProjectTattooShopCardData> GetManualTattooLayers();

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Skinned Decal")
	TArray<FProjectTattooShopCardData> GetActiveAutomaticTattooLayers();

	/** Resolves or creates one isolated component/MID and snapshots it for cancel. */
	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Edit")
	UMaterialInstanceDynamic* BeginEdit(
		UMaterialInstanceDynamic* CandidateMID,
		int32 LegacyTattooIndex,
		FGuid& OutTattooId);

	/** Applies a native preview only to TattooId. The persisted state is unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Edit")
	bool Preview(const FGuid& TattooId, const FProjectTattooParameters& Parameters);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Edit")
	bool Commit(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Edit")
	bool Cancel(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Edit")
	bool Delete(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Runtime Textures")
	bool RequestUploadRuntimeTattooTexture();

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Runtime Textures")
	bool RequestDeleteRuntimeTattooTexture();

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|Automation")
	UUserWidget* GetTattooShopWidgetForAutomation() const;

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|Automation")
	UUserWidget* GetAssetPreviewWidgetForAutomation() const;

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	void NormalizeTattooCardGridsForAutomation() const;

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	FString GetTattooCardGridReportForAutomation() const;

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	bool ApplyTattooTextureForAutomation(UTexture2D* TattooTexture);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	bool ApplyDefaultHeartTattooForAutomation();

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	FString GetTattooOverlayReportForAutomation() const;

private:
	void TryResolveRuntimeContext();
	bool UsesSkinnedDecalTattooShop() const;
	UProjectTattooShopStateSubsystem* ResolveTattooState() const;
	UProjectTattooShopLibraryWidget* EnsureProjectTattooLibraryWidget(APlayerController* PlayerController);
	UProjectTattooShopLibraryWidget* EnsureProjectTattooCatalogWidget(APlayerController* PlayerController);
	UProjectTattooShopEditorWidget* EnsureProjectTattooEditorWidget(APlayerController* PlayerController);
	void OpenSkinnedDecalTattooShop(APlayerController* PlayerController, APawn* Pawn);
	void CloseSkinnedDecalTattooShop();
	void BindProjectTattooShopUI();
	void RefreshProjectTattooShopUI();
	void ShowProjectTattooEditor(const FGuid& TattooId, FName SourceEntryId = NAME_None);
	void HideProjectTattooEditor();
	FProjectTattooShopEditorModel MakeEditorModel(const FProjectTattooRecord& Record, FName SourceEntryId) const;
	FProjectTattooParameters ApplyEditorModelToParameters(
		const FProjectTattooShopEditorModel& Model,
		const FProjectTattooParameters& Existing) const;
	bool PopulateTextureIdentityFromCard(const FProjectTattooShopCardData& Card, FProjectTattooParameters& InOutParameters) const;
	void RequestManualTattooSynchronization(bool bImmediate);
	void FlushManualTattooPreview();
	void RemoveLegacyTattooRuntimeArtifacts(APawn* Pawn);
	bool IsRuntimeTextureInUse(const FProjectTattooShopCardData& Card) const;

	UFUNCTION()
	void HandleProjectTattooSelectionChanged(FProjectTattooShopCardData CardData);

	UFUNCTION()
	void HandleProjectTattooAddRequested(FProjectTattooShopCardData CardData);

	UFUNCTION()
	void HandleProjectTattooEditRequested(FGuid TattooId);

	UFUNCTION()
	void HandleProjectTattooRemoveRequested(FGuid TattooId);

	UFUNCTION()
	void HandleProjectTattooUploadRequested();

	UFUNCTION()
	void HandleProjectTattooDeleteSourceRequested(FProjectTattooShopCardData CardData);

	UFUNCTION()
	void HandleProjectTattooPreviewChanged(FProjectTattooShopEditorModel EditorModel);

	UFUNCTION()
	void HandleProjectTattooAcceptRequested(FGuid TattooId);

	UFUNCTION()
	void HandleProjectTattooCancelRequested(FGuid TattooId);
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController();
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();

	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleRuntimeUploadClicked();

	UFUNCTION()
	void HandleRuntimeDeleteClicked();

	UFUNCTION()
	void HandleTattooCustomizationAcceptClicked();

	UFUNCTION()
	void HandleTattooCustomizationCancelClicked();

	void HandleTogglePressed();
	bool CanUseTattooShopPawn(APawn* Pawn) const;
	UUserWidget* EnsureTattooShopWidget(APlayerController* PlayerController, APawn* Pawn);
	UUserWidget* GetTattooShopWidgetFromPawn(APawn* Pawn) const;
	void SetTattooShopWidgetOnPawn(APawn* Pawn, UUserWidget* Widget) const;
	void SetTattooShopActorReference(UUserWidget* Widget, APawn* Pawn) const;
	TSubclassOf<UUserWidget> ResolveTattooShopWidgetClass();
	TSubclassOf<UUserWidget> ResolveAssetPreviewWidgetClass();
	TSubclassOf<UUserWidget> ResolveTattooViewerCardWidgetClass();
	void MountWidgetInHostedPanel(UUserWidget* Widget, UPanelWidget* HostPanel, bool bClearHost) const;
	void CaptureAssetPreviewWidget();
	void BindTattooShopRuntimeButtons(UUserWidget* TattooShopWidget);
	void BindTattooCustomizationRuntimeButtons(UUserWidget* AssetPreviewWidget);
	UUserWidget* ResolveTrackedAssetPreviewWidget();
	UGridPanel* ResolveAssetPreviewGrid(UUserWidget* PreviewWidget) const;
	bool RefreshRuntimeTattooCards(UUserWidget* PreferredPreviewWidget = nullptr);
	UTexture2D* LoadPngTextureFromFile(const FString& FilePath, const FString& TextureObjectName);
	bool OpenNativePngFileDialog(const FString& DialogTitle, FString& OutSelectedFilePath) const;
	FString GetRuntimeTattooTextureDirectory() const;
	FString NormalizeTattooFilePath(const FString& FilePath) const;
	FString MakeRuntimeTattooDisplayName(const FString& StoredFilePath) const;
	FString MakeUniqueRuntimeTattooDestination(const FString& SourceFilePath) const;
	bool IsRuntimeTattooCard(UWidget* CardWidget) const;
	bool FindRuntimeTattooFileForTexture(UTexture2D* Texture, FString& OutFilePath) const;
	UTexture2D* ResolveTattooCardTexture(UWidget* CardWidget) const;
	FString ResolveTattooCardDisplayName(UWidget* CardWidget, UTexture2D* Texture) const;
	void GatherDeletableTattooTextures(TArray<UTexture2D*>& OutTextures, TArray<FString>& OutDisplayNames);
	bool ShowDeleteTattooTextureMenu();
	void DismissDeleteTattooTextureMenu();
	bool DeleteTattooTexture(UTexture2D* Texture, const FString& DisplayName);
	bool DeleteGameTattooTextureAsset(UTexture2D* Texture) const;
	void CollectTattooCardsForTexture(UPanelWidget* Panel, UTexture2D* Texture, const FString& TextureName, TArray<UWidget*>& OutCards) const;
	UUserWidget* CreateRuntimeTattooCard(UUserWidget* PreviewWidget, UTexture2D* Texture, const FText& DisplayName);
	void SetRuntimeWidgetObjectProperty(UObject* Target, FName PropertyName, UObject* Value) const;
	void SetRuntimeWidgetTextProperty(UObject* Target, FName PropertyName, const FText& Value) const;
	FString GetRuntimeWidgetTextProperty(UObject* Target, FName PropertyName) const;
	void LayoutRuntimeTattooCardPanel(UPanelWidget* Panel, int32 ColumnLimit) const;
	void RepairTattooCustomizationRuntime();
	UUserWidget* FindLiveTattooCustomizationWidget() const;
	USkeletalMeshComponent* EnsureRuntimeTattooBaseComponent(
		APawn* Pawn,
		UMaterialInstanceDynamic* DynamicMaterial,
		bool& bOutCreatedComponent);
	void AddTattooBaseComponentToPawnArray(APawn* Pawn, USkeletalMeshComponent* Component) const;
	void CollectTattooCardPanels(UUserWidget* Widget, TArray<UPanelWidget*>& OutPanels) const;
	int32 CountTattooCardsInPanel(UPanelWidget* Panel) const;
	void NormalizeTattooCardPanel(UPanelWidget* Panel, int32 ColumnLimit) const;
	void NormalizeTattooCardGrid(UUserWidget* Widget) const;
	FString ResolveTattooCardIdentity(UWidget* Widget) const;
	FString BuildTattooCardGridReport(UUserWidget* PreferredWidget, UUserWidget* FallbackWidget) const;
	void OpenTattooShop();
	void CloseTattooShop();
	void ApplyTattooShopInputMode(UUserWidget* Widget);
	void RestoreTattooShopInputMode();
	void SynchronizeTattooOverlayToVisibleSkin();
	void CollectTattooBaseComponents(APawn* Pawn, TArray<USkeletalMeshComponent*>& OutComponents) const;
	void IsolateLegacyTattooShopWidgets(UUserWidget* RootWidget) const;
	void SuppressLegacyTattooBaseComponents(APawn* Pawn, USkeletalMeshComponent* TargetSkin) const;
	USkeletalMeshComponent* ResolveVisibleSkinComponent(APawn* Pawn, const TSet<USkeletalMeshComponent*>& TattooBaseComponents) const;
	UMaterialInterface* ResolveActiveTattooOverlayMaterial(APawn* Pawn, const TArray<USkeletalMeshComponent*>& TattooBaseComponents) const;
	void PrepareTattooBaseComponentsForVisibleSkin(APawn* Pawn, const TArray<USkeletalMeshComponent*>& TattooBaseComponents, USkeletalMeshComponent* TargetSkin) const;
	void ClearMirroredTattooOverlay();
	void RehydrateManualTattoos();
	void DisableLegacyTattooMeshComponents(APawn* Pawn);
	void ResetManualTattooRuntime();
	USkeletalMeshComponent* CreateManualTattooComponent(APawn* Pawn, USkeletalMeshComponent* TargetSkin, const FGuid& TattooId);
	bool ConfigureManualTattooMaterials(USkeletalMeshComponent* Component, UMaterialInstanceDynamic* TattooMID) const;
	UMaterialInstanceDynamic* CreateIsolatedTattooMID(APawn* Pawn, UMaterialInterface* SourceMaterial) const;
	FGuid FindTattooIdForMID(const UMaterialInstanceDynamic* MID) const;
	FGuid FindTattooIdForComponent(const USkeletalMeshComponent* Component) const;
	FProjectTattooParameters CaptureTattooParameters(UMaterialInstanceDynamic* MID, int32 LayerOrder) const;
	void ApplyTattooParameters(UMaterialInstanceDynamic* MID, const FProjectTattooParameters& Parameters, UTexture* ResolvedTexture) const;
	UTexture* ResolvePersistedTattooTexture(const FProjectTattooParameters& Parameters, bool& bOutMissingRuntimeFile);
	void BindCustomizationWidgetToMID(UUserWidget* Widget, UMaterialInstanceDynamic* MID, bool bSetPreEditSnapshot);
	bool InvokeCustomizationMaterialEvent(UUserWidget* Widget, FName EventName, UMaterialInstanceDynamic* MID) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> TrackedInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> TrackedTattooShopWidget;

	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> TattooShopHostPanel;

	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> TattooAssetPreviewHostPanel;

	UPROPERTY(Transient)
	TSubclassOf<UUserWidget> TattooShopWidgetClass;

	UPROPERTY(Transient)
	TSubclassOf<UUserWidget> TattooAssetPreviewWidgetClass;

	UPROPERTY(Transient)
	TSubclassOf<UUserWidget> TattooViewerCardWidgetClass;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> RuntimeTattooTextureCache;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> TrackedAssetPreviewWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTattooShopLibraryWidget> TrackedProjectTattooLibraryWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTattooShopLibraryWidget> TrackedProjectTattooCatalogWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTattooShopEditorWidget> TrackedProjectTattooEditorWidget;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MirroredOverlayTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LastMirroredOverlayMaterial;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> AutomationTattooBaseComponent;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> RuntimeTattooCustomizationWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RuntimeTattooMID;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LastRuntimeTattooTexture;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> RuntimeTattooBaseComponent;

	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UMaterialInstanceDynamic>> ManualTattooMIDs;

	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<USkeletalMeshComponent>> ManualTattooComponents;

	UPROPERTY(Transient)
	TMap<FGuid, FProjectTattooParameters> ManualTattooEditSnapshots;
	TMap<FGuid, FProjectTattooParameters> ManualTattooPreviewParameters;
	TMap<FGuid, int32> ManualTattooLayerOrders;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> LastManualTattooTargetSkin;

	FGuid ActiveManualTattooId;
	FName ActiveManualTattooSourceEntryId = NAME_None;
	TWeakObjectPtr<APawn> LastSkinnedTattooSynchronizedPawn;
	FTimerHandle ManualTattooPreviewTimerHandle;

	FProjectTattooShopInputSnapshot InputSnapshot;
	TSharedPtr<SWidget> ActiveDeleteMenuSlateWidget;
	bool bTattooShopOpen = false;
	bool bTattooShopHostedInPanel = false;
	bool bRuntimeTattooCardsInitialized = false;
	bool bManualTattooPreviewPending = false;
};
