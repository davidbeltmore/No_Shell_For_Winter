#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectTattooShopInputSubsystem.generated.h"

class APlayerController;
class APawn;
class UGridPanel;
class UInputComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPanelWidget;
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
	USkeletalMeshComponent* ResolveVisibleSkinComponent(APawn* Pawn, const TSet<USkeletalMeshComponent*>& TattooBaseComponents) const;
	UMaterialInterface* ResolveActiveTattooOverlayMaterial(APawn* Pawn, const TArray<USkeletalMeshComponent*>& TattooBaseComponents) const;
	void PrepareTattooBaseComponentsForVisibleSkin(APawn* Pawn, const TArray<USkeletalMeshComponent*>& TattooBaseComponents, USkeletalMeshComponent* TargetSkin) const;
	void ClearMirroredTattooOverlay();

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

	FProjectTattooShopInputSnapshot InputSnapshot;
	TSharedPtr<SWidget> ActiveDeleteMenuSlateWidget;
	bool bTattooShopOpen = false;
	bool bTattooShopHostedInPanel = false;
	bool bRuntimeTattooCardsInitialized = false;
};
