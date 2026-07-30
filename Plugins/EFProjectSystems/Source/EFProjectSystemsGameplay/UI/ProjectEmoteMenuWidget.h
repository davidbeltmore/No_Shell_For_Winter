#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "InputCoreTypes.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "ProjectEmoteMenuWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UProjectEmoteMenuOptionWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class UWidgetTree;

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectEmoteMenuOptionEvent, FName);
DECLARE_MULTICAST_DELEGATE(FProjectEmoteMenuCancelEvent);
DECLARE_MULTICAST_DELEGATE(FProjectEmoteMenuBackEvent);

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual void GatherCodeWidgetDesignerChildWidgetClasses(TArray<TSubclassOf<UUserWidget>>& OutWidgetClasses) const override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

	void SetMenuContent(const FText& InTitle, const FText& InHint, const TArray<FProjectEmoteMenuOption>& InOptions);
	void SetMenuContent(const FText& InTitle, const FText& InHint, const TArray<FProjectEmoteMenuOption>& InOptions, EProjectEmoteMenuVisualMode InVisualMode);
	void SetAlternateCancelKey(FKey InAlternateCancelKey);
	void FocusInitialOption();
	int32 GetSelectedIndex() const;
	void SelectOptionByIndex(int32 InIndex);
	void NavigateSelectionByDirection(int32 Direction);
	void ConfirmCurrentSelection();
	void ActivateOptionByIndex(int32 InIndex);
	void RequestCancel();
	void RequestBack();

	FProjectEmoteMenuOptionEvent OnOptionConfirmed;
	FProjectEmoteMenuCancelEvent OnCancelRequested;
	FProjectEmoteMenuBackEvent OnBackRequested;

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;
	virtual void BuildWidgetTree();
	virtual bool BuildDefaultMenuTree(UWidgetTree* TargetWidgetTree);
	virtual void RebuildOptionWidgets();
	virtual void RebuildFixedRootOptionWidgets();
	virtual void RebuildDynamicOptionWidgets();
	virtual void RefreshVisualState();
	virtual void FocusMenuWidget();
	virtual void ScrollSelectedOptionIntoView();
	virtual TSubclassOf<UProjectEmoteMenuOptionWidget> ResolveOptionRowWidgetClass() const;
	virtual TSubclassOf<UProjectEmoteMenuOptionWidget> ResolveFixedRootRowWidgetClass(const FProjectEmoteMenuOption& Option) const;
	virtual bool CanUseFixedRootOptionWidgets() const;
	virtual void ResetFixedRootOptionVisibility();
	virtual UProjectEmoteMenuOptionWidget* ResolveFixedRootRowForOption(const FProjectEmoteMenuOption& Option) const;
	virtual UProjectEmoteMenuOptionWidget* ConstructFixedRootRow(UWidgetTree* TargetWidgetTree, UVerticalBox* Parent, const FProjectEmoteMenuOption& PreviewOption, FName WidgetName, int32 PreviewIndex);
	virtual bool HandleKeyNavigation(const FKey& Key);
	virtual int32 FindFirstEnabledOptionIndex() const;
	virtual int32 FindNextEnabledOptionIndex(int32 StartIndex, int32 Direction) const;
	virtual FVector2D ResolvePanelSize() const;
	virtual FMargin ResolvePanelPadding() const;
	virtual float ResolveOptionHeight() const;
	virtual FLinearColor ResolveAccentColor(const FProjectEmoteMenuOption& Option) const;
	virtual UTexture2D* ResolveIconTexture(const FProjectEmoteMenuOption& Option) const;
	virtual UTexture2D* LoadTextureByPath(const TCHAR* TexturePath) const;
	virtual bool ShouldShowScrollBar() const;
	virtual void AddControlFooter(UWidgetTree* TargetWidgetTree);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Emote|Designer", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UProjectEmoteMenuOptionWidget> OptionRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Emote|Designer|Root Rows", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UProjectEmoteMenuOptionWidget> ActionsRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Emote|Designer|Root Rows", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UProjectEmoteMenuOptionWidget> ObjectsRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Emote|Designer|Root Rows", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UProjectEmoteMenuOptionWidget> SocialRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Emote|Designer|Root Rows", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UProjectEmoteMenuOptionWidget> SpecialRowWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Emote|Designer|Root Rows", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UProjectEmoteMenuOptionWidget> CancelRowWidgetClass;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> BackdropBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<USizeBox> PanelSizeBox;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> PanelInnerBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UVerticalBox> PanelLayout;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UVerticalBox> OptionsLayout;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UScrollBox> OptionsScrollBox;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Root Menu")
	TObjectPtr<UProjectEmoteMenuOptionWidget> ActionsRow;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Root Menu")
	TObjectPtr<UProjectEmoteMenuOptionWidget> ObjectsRow;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Root Menu")
	TObjectPtr<UProjectEmoteMenuOptionWidget> SocialRow;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Root Menu")
	TObjectPtr<UProjectEmoteMenuOptionWidget> SpecialRow;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Root Menu")
	TObjectPtr<UProjectEmoteMenuOptionWidget> CancelRow;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProjectEmoteMenuOptionWidget>> OptionWidgets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> OptionRowWidgets;

	TArray<FProjectEmoteMenuOption> MenuOptions;
	EProjectEmoteMenuVisualMode VisualMode = EProjectEmoteMenuVisualMode::Root;
	FKey AlternateCancelKey;
	int32 SelectedIndex = INDEX_NONE;
	bool bUsingNativeFallbackTree = false;
};
