#pragma once

#include "CoreMinimal.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectThemedUserWidget.h"
#include "TattooShop/UI/ProjectTattooShopUITypes.h"
#include "ProjectTattooShopLibraryWidget.generated.h"

class UBorder;
class UButton;
class UPanelWidget;
class UProjectTattooShopCardWidget;
class USizeBox;
class UTextBlock;
class UUniformGridPanel;
class UVerticalBox;
class UWidgetTree;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectTattooShopLibraryCardSignature,
	FProjectTattooShopCardData,
	CardData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectTattooShopLibraryTattooSignature,
	FGuid,
	TattooId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectTattooShopLibrarySimpleSignature);

/**
 * Main Character Creation Tattoo tab surface. It preserves TattooShop's
 * library/applied-card workflow while remaining a passive SkinnedDecal view.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopLibraryWidget
	: public UEFProjectThemedUserWidget
	, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectTattooShopLibraryWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual bool GatherCodeWidgetDesignerConversionManifest(FCodeWidgetDesignerConversionManifest& OutManifest) const override;
	virtual void GatherCodeWidgetDesignerChildWidgetClasses(TArray<TSubclassOf<UUserWidget>>& OutWidgetClasses) const override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetLibraryEntries(const TArray<FProjectTattooShopCardData>& InEntries);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetManualTattoos(const TArray<FProjectTattooShopCardData>& InEntries);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetAutomaticTattoos(const TArray<FProjectTattooShopCardData>& InEntries);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetSelectedEntry(const FProjectTattooShopCardData& InSelection);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void ClearSelection();

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetStatusText(const FText& InStatusText);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetPresentationMode(EProjectTattooShopLibraryPresentationMode InPresentationMode);

	UFUNCTION(BlueprintPure, Category = "Tattoo Shop|UI")
	EProjectTattooShopLibraryPresentationMode GetPresentationMode() const { return PresentationMode; }

	UFUNCTION(BlueprintPure, Category = "Tattoo Shop|UI")
	bool HasSelection() const { return bHasSelection; }

	UFUNCTION(BlueprintPure, Category = "Tattoo Shop|UI")
	FProjectTattooShopCardData GetSelectedEntry() const { return SelectedEntry; }

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopLibraryCardSignature OnSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopLibraryCardSignature OnAddRequested;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopLibraryTattooSignature OnEditRequested;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopLibraryTattooSignature OnRemoveRequested;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopLibrarySimpleSignature OnUploadRequested;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopLibraryCardSignature OnDeleteSourceRequested;

	UFUNCTION(BlueprintImplementableEvent, Category = "Tattoo Shop|UI")
	void OnLibraryRebuilt(int32 LibraryCount, int32 ManualCount, int32 AutomaticCount);

protected:
	void BuildWidgetTreeIfNeeded();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void BindControls();
	void RefreshActionState();
	void RefreshPresentationMode();
	void RebuildAllCards();
	void RebuildCardPanel(UPanelWidget* Panel, const TArray<FProjectTattooShopCardData>& Entries);
	TSubclassOf<UProjectTattooShopCardWidget> ResolveCardWidgetClass() const;

	UFUNCTION()
	void HandleCardSelected(FProjectTattooShopCardData CardData);

	UFUNCTION()
	void HandleUploadClicked();

	UFUNCTION()
	void HandleDeleteClicked();

	UFUNCTION()
	void HandleAddClicked();

	UFUNCTION()
	void HandleEditClicked();

	UFUNCTION()
	void HandleRemoveClicked();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo Shop|UI")
	TSoftClassPtr<UProjectTattooShopCardWidget> CardWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo Shop|UI", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CardColumnCount = 3;

	/** Designer-overridable size for the actions/applied-layers column. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo Shop|UI|Layout")
	FVector2D ManagementPanelSize = FVector2D(320.0f, 680.0f);

	/** Designer-overridable size for the catalog column. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo Shop|UI|Layout")
	FVector2D CatalogPanelSize = FVector2D(420.0f, 680.0f);

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ManagementToolbar;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> CatalogSection;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ManagementSection;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> UploadButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> DeleteButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> AddButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> EditButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> RemoveButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> LibraryGrid;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> ManualGrid;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> AutomaticGrid;

private:
	UPROPERTY(Transient)
	TArray<FProjectTattooShopCardData> LibraryEntries;

	UPROPERTY(Transient)
	TArray<FProjectTattooShopCardData> ManualEntries;

	UPROPERTY(Transient)
	TArray<FProjectTattooShopCardData> AutomaticEntries;

	UPROPERTY(Transient)
	FProjectTattooShopCardData SelectedEntry;

	FText CurrentStatusText;
	EProjectTattooShopLibraryPresentationMode PresentationMode = EProjectTattooShopLibraryPresentationMode::Management;
	bool bHasSelection = false;
	bool bUsingNativeFallbackTree = false;
};
