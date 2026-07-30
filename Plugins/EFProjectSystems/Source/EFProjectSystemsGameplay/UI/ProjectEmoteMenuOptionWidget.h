#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "ProjectEmoteMenuOptionWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidgetTree;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuOptionWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuOptionWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	void ConfigureOption(
		const FProjectEmoteMenuOption& InOption,
		EProjectEmoteMenuVisualMode InVisualMode,
		int32 InOptionIndex,
		float InRowHeight,
		UTexture2D* InIconTexture,
		const FLinearColor& InAccentColor,
		bool bInUseDesignerIconOverride = false);
	void SetOptionVisualState(bool bInSelected, bool bInEnabled);
	void SetDesignerIconOverride(UTexture2D* InDesignerIconOverride);
	void SetUseDesignerIconOverride(bool bInUseDesignerIconOverride);
	void SetDesignerPreviewOption(const FProjectEmoteMenuOption& InDesignerPreviewOption, EProjectEmoteMenuVisualMode InDesignerPreviewVisualMode, int32 InDesignerPreviewIndex);

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Menu")
	const FProjectEmoteMenuOption& GetMenuOption() const { return MenuOption; }

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Menu")
	int32 GetOptionIndex() const { return OptionIndex; }

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Menu")
	bool IsSelected() const { return bSelected; }

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Menu")
	bool IsOptionEnabled() const { return bOptionEnabled; }

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Designer")
	bool bUseDesignerIconOverride = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Designer", meta = (EditCondition = "bUseDesignerIconOverride"))
	TObjectPtr<UTexture2D> DesignerIconOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Designer")
	FProjectEmoteMenuOption DesignerPreviewOption;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Designer")
	EProjectEmoteMenuVisualMode DesignerPreviewVisualMode = EProjectEmoteMenuVisualMode::Root;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<USizeBox> RowSizeBox;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UOverlay> RowOverlay;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> OptionSelectionFrame;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> OptionGlowBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UImage> OptionFrameImage;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> OptionBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> OptionInnerBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> OptionDisabledOverlay;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> SelectorText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UBorder> OptionIconBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UImage> OptionIconImage;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> OptionLabelText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> OptionDescriptionText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> OptionArrowText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UHorizontalBox> NpcBox;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> NpcNumberText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UImage> NpcIconImage;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Project|Emote|Menu")
	TObjectPtr<UTextBlock> NpcFallbackText;

	UFUNCTION(BlueprintImplementableEvent, Category = "Project|Emote|Menu")
	void OnOptionDataChanged(const FProjectEmoteMenuOption& NewOption, EProjectEmoteMenuVisualMode NewVisualMode, bool bIsAnimationEntry, UTexture2D* NewIconTexture, const FLinearColor& NewAccentColor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Project|Emote|Menu")
	void OnOptionVisualStateChanged(bool bOptionIsSelected, bool bOptionIsEnabled);

protected:
	virtual void BuildWidgetTree();
	virtual bool BuildDefaultOptionTree(UWidgetTree* TargetWidgetTree);
	virtual void ApplyDesignerPreviewOptionIfNeeded();
	virtual void RefreshOptionData();
	virtual void RefreshVisualState();
	virtual bool IsAnimationEntry() const;
	virtual bool IsNavigationOption() const;
	virtual FSlateFontInfo MakeTitleFont(int32 Size) const;
	virtual FSlateFontInfo MakeBodyFont(int32 Size) const;
	virtual UTexture2D* LoadTextureByPath(const TCHAR* TexturePath) const;

	FProjectEmoteMenuOption MenuOption;
	EProjectEmoteMenuVisualMode VisualMode = EProjectEmoteMenuVisualMode::Root;
	TObjectPtr<UTexture2D> IconTexture;
	FLinearColor AccentColor = FLinearColor::White;
	int32 OptionIndex = INDEX_NONE;
	float RowHeight = 82.0f;
	bool bSelected = false;
	bool bOptionEnabled = true;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuActionsRowWidget : public UProjectEmoteMenuOptionWidget
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuActionsRowWidget(const FObjectInitializer& ObjectInitializer);
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuObjectsRowWidget : public UProjectEmoteMenuOptionWidget
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuObjectsRowWidget(const FObjectInitializer& ObjectInitializer);
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuSocialRowWidget : public UProjectEmoteMenuOptionWidget
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuSocialRowWidget(const FObjectInitializer& ObjectInitializer);
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuSpecialRowWidget : public UProjectEmoteMenuOptionWidget
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuSpecialRowWidget(const FObjectInitializer& ObjectInitializer);
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuCancelRowWidget : public UProjectEmoteMenuOptionWidget
{
	GENERATED_BODY()

public:
	UProjectEmoteMenuCancelRowWidget(const FObjectInitializer& ObjectInitializer);
};
