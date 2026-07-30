#include "Debug/ProjectGameplayDebugMenuWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EFProjectUISettings.h"
#include "Engine/Texture2D.h"
#include "UI/ProjectWidgetClassResolver.h"

namespace ProjectGameplayDebugMenuWidgetPrivate
{
	static constexpr float PreviewRowHeight = 104.0f;

	struct FDebugRowDefinition
	{
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> WidgetClass;
		FString AssetName;
		FName OptionId = NAME_None;
		FString Label;
		FString Description;
		EProjectEmoteMenuNodeType NodeType = EProjectEmoteMenuNodeType::Action;
		FName VisualIconId = NAME_None;
		FName VisualAttribute = TEXT("None");
		EProjectEmoteMenuVisualMode VisualMode = EProjectEmoteMenuVisualMode::Category;
		int32 PreviewIndex = 0;
		bool bEnabled = true;
	};

	static FText Text(const FString& Value)
	{
		return Value.IsEmpty() ? FText::GetEmpty() : FText::FromString(Value);
	}

	static FProjectEmoteMenuOption MakeOption(const FDebugRowDefinition& Definition)
	{
		FProjectEmoteMenuOption Option;
		Option.OptionId = Definition.OptionId;
		Option.Label = Text(Definition.Label);
		Option.Description = Text(Definition.Description);
		Option.bEnabled = Definition.bEnabled;
		Option.NodeType = Definition.NodeType;
		Option.VisualIconId = Definition.VisualIconId;
		Option.VisualAttribute = Definition.VisualAttribute;
		return Option;
	}

	static void AddRow(
		TArray<FDebugRowDefinition>& Rows,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> WidgetClass,
		const TCHAR* AssetName,
		const TCHAR* OptionId,
		const TCHAR* Label,
		const TCHAR* Description,
		const EProjectEmoteMenuNodeType NodeType,
		const int32 PreviewIndex,
		const FName VisualIconId,
		const FName VisualAttribute = TEXT("None"),
		const EProjectEmoteMenuVisualMode VisualMode = EProjectEmoteMenuVisualMode::Category)
	{
		FDebugRowDefinition Definition;
		Definition.WidgetClass = WidgetClass;
		Definition.AssetName = AssetName;
		Definition.OptionId = FName(OptionId);
		Definition.Label = Label;
		Definition.Description = Description;
		Definition.NodeType = NodeType;
		Definition.PreviewIndex = PreviewIndex;
		Definition.VisualIconId = VisualIconId;
		Definition.VisualAttribute = VisualAttribute;
		Definition.VisualMode = VisualMode;
		Rows.Add(MoveTemp(Definition));
	}

	static void AddAttributeRows(
		TArray<FDebugRowDefinition>& Rows,
		const TCHAR* Prefix,
		const TCHAR* AssetSuffix,
		const TCHAR* Description,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> WillpowerClass,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> OffensiveClass,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> DefensiveClass,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> FaithClass,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> CunningClass,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> CelerityClass,
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> CharismaClass)
	{
		AddRow(Rows, WillpowerClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugWillpower%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Willpower"), Prefix), TEXT("Willpower"), Description, EProjectEmoteMenuNodeType::Action, 0, NAME_None, TEXT("Willpower"));
		AddRow(Rows, OffensiveClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugOffensive%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Offensive"), Prefix), TEXT("Offensive"), Description, EProjectEmoteMenuNodeType::Action, 1, NAME_None, TEXT("Offensive"));
		AddRow(Rows, DefensiveClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugDefensive%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Defensive"), Prefix), TEXT("Defensive"), Description, EProjectEmoteMenuNodeType::Action, 2, NAME_None, TEXT("Defensive"));
		AddRow(Rows, FaithClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugFaith%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Faith"), Prefix), TEXT("Faith"), Description, EProjectEmoteMenuNodeType::Action, 3, NAME_None, TEXT("Faith"));
		AddRow(Rows, CunningClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugCunning%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Cunning"), Prefix), TEXT("Cunning"), Description, EProjectEmoteMenuNodeType::Action, 4, NAME_None, TEXT("Cunning"));
		AddRow(Rows, CelerityClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugCelerity%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Celerity"), Prefix), TEXT("Celerity"), Description, EProjectEmoteMenuNodeType::Action, 5, NAME_None, TEXT("Celerity"));
		AddRow(Rows, CharismaClass, *FString::Printf(TEXT("WBP_ProjectGameplayDebugCharisma%sRow"), AssetSuffix), *FString::Printf(TEXT("%s.Charisma"), Prefix), TEXT("Charisma"), Description, EProjectEmoteMenuNodeType::Action, 6, NAME_None, TEXT("Charisma"));
	}

	static TArray<FDebugRowDefinition> MakeDebugRowDefinitions()
	{
		TArray<FDebugRowDefinition> Rows;
		Rows.Reserve(56);

		AddRow(Rows, UProjectGameplayDebugRootDebugRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugRootDebugRow"), TEXT("Root.Debug"), TEXT("Debug"), TEXT("Gameplay state, survival, combat, and status commands."), EProjectEmoteMenuNodeType::Folder, 0, TEXT("Combat"), TEXT("None"), EProjectEmoteMenuVisualMode::Root);
		AddRow(Rows, UProjectGameplayDebugRootTestRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugRootTestRow"), TEXT("Root.Test"), TEXT("Test"), TEXT("Progression shortcuts for targeted validation."), EProjectEmoteMenuNodeType::Folder, 1, TEXT("Special"), TEXT("None"), EProjectEmoteMenuVisualMode::Root);
		AddRow(Rows, UProjectGameplayDebugRootCancelRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugRootCancelRow"), TEXT("Root.Cancel"), TEXT("Cancel"), TEXT("Close the gameplay debug menu."), EProjectEmoteMenuNodeType::Cancel, 2, TEXT("Cancel"), TEXT("None"), EProjectEmoteMenuVisualMode::Root);

		AddRow(Rows, UProjectGameplayDebugImmediateDefeatRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugImmediateDefeatRow"), TEXT("Debug.ImmediateDefeat"), TEXT("Immediate Defeat"), TEXT("Trigger defeated travel and respawn in the dungeon now."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Combat"));
		AddRow(Rows, UProjectGameplayDebugDownedModeRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugDownedModeRow"), TEXT("Debug.DownedMode"), TEXT("Downed Mode"), TEXT("Force knockout or pending crawl; nearby enemies can start the struggle minigame."), EProjectEmoteMenuNodeType::Action, 1, TEXT("Combat"));
		AddRow(Rows, UProjectGameplayDebugRestoreAcfHealthRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugRestoreAcfHealthRow"), TEXT("Debug.RestoreAcfHealth"), TEXT("Restore ACF Health"), TEXT("Restore the owner health resource to its current maximum."), EProjectEmoteMenuNodeType::Action, 2, TEXT("Basic"));
		AddRow(Rows, UProjectGameplayDebugRestoreNeedsSensationsRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugRestoreNeedsSensationsRow"), TEXT("Debug.RestoreNeedsSensations"), TEXT("Restore Needs & Sensations"), TEXT("Set Hunger, Thirst, and Sleep to max; Madness, Curse, and Pain to zero."), EProjectEmoteMenuNodeType::Action, 3, TEXT("Basic"));
		AddRow(Rows, UProjectGameplayDebugSetTo100RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugSetTo100Row"), TEXT("Debug.SetTo100"), TEXT("Set to 100%"), TEXT("Set a negative sensation to its current maximum."), EProjectEmoteMenuNodeType::Folder, 4, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugSetTo50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugSetTo50Row"), TEXT("Debug.SetTo50"), TEXT("Set to 50%"), TEXT("Set a need or sensation to half of its current maximum."), EProjectEmoteMenuNodeType::Folder, 5, TEXT("Basic"));
		AddRow(Rows, UProjectGameplayDebugSetTo0RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugSetTo0Row"), TEXT("Debug.SetTo0"), TEXT("Set to 0%"), TEXT("Set a survival need to zero."), EProjectEmoteMenuNodeType::Folder, 6, TEXT("Objects"));
		AddRow(Rows, UProjectGameplayDebugApplyStatusRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugApplyStatusRow"), TEXT("Debug.ApplyStatus"), TEXT("Apply Status"), TEXT("Force-apply a configured status, bypassing immunity for debug."), EProjectEmoteMenuNodeType::Folder, 7, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugBackRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugBackRow"), TEXT("Navigation.Back"), TEXT("Back"), TEXT("Return to the previous debug menu."), EProjectEmoteMenuNodeType::Back, 8, TEXT("Back"));

		AddRow(Rows, UProjectGameplayDebugMadness100RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugMadness100Row"), TEXT("Debug.SetTo100.Madness"), TEXT("Madness"), TEXT("Set Madness to its current maximum."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugCurse100RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugCurse100Row"), TEXT("Debug.SetTo100.Curse"), TEXT("Curse"), TEXT("Set Curse to its current maximum."), EProjectEmoteMenuNodeType::Action, 1, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugPain100RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugPain100Row"), TEXT("Debug.SetTo100.Pain"), TEXT("Pain"), TEXT("Set Pain to its current maximum."), EProjectEmoteMenuNodeType::Action, 2, TEXT("Special"));

		AddRow(Rows, UProjectGameplayDebugHunger50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugHunger50Row"), TEXT("Debug.SetTo50.Hunger"), TEXT("Hunger"), TEXT("Set Hunger to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Objects"));
		AddRow(Rows, UProjectGameplayDebugThirst50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugThirst50Row"), TEXT("Debug.SetTo50.Thirst"), TEXT("Thirst"), TEXT("Set Thirst to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 1, TEXT("Objects"));
		AddRow(Rows, UProjectGameplayDebugSleep50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugSleep50Row"), TEXT("Debug.SetTo50.Sleep"), TEXT("Sleep"), TEXT("Set Sleep to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 2, TEXT("Objects"));
		AddRow(Rows, UProjectGameplayDebugMadness50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugMadness50Row"), TEXT("Debug.SetTo50.Madness"), TEXT("Madness"), TEXT("Set Madness to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 3, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugCurse50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugCurse50Row"), TEXT("Debug.SetTo50.Curse"), TEXT("Curse"), TEXT("Set Curse to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 4, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugPain50RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugPain50Row"), TEXT("Debug.SetTo50.Pain"), TEXT("Pain"), TEXT("Set Pain to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 5, TEXT("Special"));

		AddRow(Rows, UProjectGameplayDebugHunger0RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugHunger0Row"), TEXT("Debug.SetTo0.Hunger"), TEXT("Hunger"), TEXT("Set Hunger to zero."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Objects"));
		AddRow(Rows, UProjectGameplayDebugThirst0RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugThirst0Row"), TEXT("Debug.SetTo0.Thirst"), TEXT("Thirst"), TEXT("Set Thirst to zero."), EProjectEmoteMenuNodeType::Action, 1, TEXT("Objects"));
		AddRow(Rows, UProjectGameplayDebugSleep0RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugSleep0Row"), TEXT("Debug.SetTo0.Sleep"), TEXT("Sleep"), TEXT("Set Sleep to zero."), EProjectEmoteMenuNodeType::Action, 2, TEXT("Objects"));

		AddRow(Rows, UProjectGameplayDebugStatusStarvingRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusStarvingRow"), TEXT("Debug.Status.Starving"), TEXT("Starving"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusThirstRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusThirstRow"), TEXT("Debug.Status.Thirst"), TEXT("Thirst"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 1, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusSleepDeprivedRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusSleepDeprivedRow"), TEXT("Debug.Status.SleepDeprived"), TEXT("SleepDeprived"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 2, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusExhaustedRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusExhaustedRow"), TEXT("Debug.Status.Exhausted"), TEXT("Exhausted"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 3, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusExhaustedRecoveryRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusExhaustedRecoveryRow"), TEXT("Debug.Status.ExhaustedRecovery"), TEXT("ExhaustedRecovery"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 4, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusFrenzyRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusFrenzyRow"), TEXT("Debug.Status.Frenzy"), TEXT("Frenzy"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 5, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusCursedRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusCursedRow"), TEXT("Debug.Status.Cursed"), TEXT("Cursed"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 6, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusExtremePainRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusExtremePainRow"), TEXT("Debug.Status.ExtremePain"), TEXT("ExtremePain"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 7, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusGraceStepRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusGraceStepRow"), TEXT("Debug.Status.GraceStep"), TEXT("GraceStep"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 8, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusKnockedOutRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusKnockedOutRow"), TEXT("Debug.Status.KnockedOut"), TEXT("KnockedOut"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 9, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusBleedingRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusBleedingRow"), TEXT("Debug.Status.Bleeding"), TEXT("Bleeding"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusDizzyRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusDizzyRow"), TEXT("Debug.Status.Dizzy"), TEXT("Dizzy"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 11, TEXT("Social"));
		AddRow(Rows, UProjectGameplayDebugStatusFearRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugStatusFearRow"), TEXT("Debug.Status.Fear"), TEXT("Fear"), TEXT("Force-apply this status through the debug bypass path."), EProjectEmoteMenuNodeType::Action, 12, TEXT("Social"));

		AddRow(Rows, UProjectGameplayDebugLevel5RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugLevel5Row"), TEXT("Test.Level5"), TEXT("Raise to Level 5"), TEXT("Raise one Inner Doctrine attribute to level 5."), EProjectEmoteMenuNodeType::Folder, 0, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugLevel10RowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugLevel10Row"), TEXT("Test.Level10"), TEXT("Raise to Level 10"), TEXT("Raise one Inner Doctrine attribute to level 10."), EProjectEmoteMenuNodeType::Folder, 1, TEXT("Special"));
		AddRow(Rows, UProjectGameplayDebugRuntimeFpsBenchmarkRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugRuntimeFpsBenchmarkRow"), TEXT("Test.RuntimeFpsBenchmark"), TEXT("Runtime FPS Benchmark"), TEXT("Run the dungeon combat performance benchmark and write FPS artifacts."), EProjectEmoteMenuNodeType::Action, 2, TEXT("Combat"));
		AddRow(Rows, UProjectGameplayDebugFullStackOverloadBenchmarkRowWidget::StaticClass(), TEXT("WBP_ProjectGameplayDebugFullStackOverloadBenchmarkRow"), TEXT("Test.FullStackOverloadBenchmark"), TEXT("Full Stack Overload Benchmark"), TEXT("Run the full-stack dungeon gameplay overload benchmark and write segmented FPS artifacts."), EProjectEmoteMenuNodeType::Action, 3, TEXT("Combat"));

		AddAttributeRows(
			Rows,
			TEXT("Test.Level5"),
			TEXT("Level5"),
			TEXT("Raise this attribute to level 5 without downgrading it."),
			UProjectGameplayDebugWillpowerLevel5RowWidget::StaticClass(),
			UProjectGameplayDebugOffensiveLevel5RowWidget::StaticClass(),
			UProjectGameplayDebugDefensiveLevel5RowWidget::StaticClass(),
			UProjectGameplayDebugFaithLevel5RowWidget::StaticClass(),
			UProjectGameplayDebugCunningLevel5RowWidget::StaticClass(),
			UProjectGameplayDebugCelerityLevel5RowWidget::StaticClass(),
			UProjectGameplayDebugCharismaLevel5RowWidget::StaticClass());
		AddAttributeRows(
			Rows,
			TEXT("Test.Level10"),
			TEXT("Level10"),
			TEXT("Raise this attribute to level 10 without downgrading it."),
			UProjectGameplayDebugWillpowerLevel10RowWidget::StaticClass(),
			UProjectGameplayDebugOffensiveLevel10RowWidget::StaticClass(),
			UProjectGameplayDebugDefensiveLevel10RowWidget::StaticClass(),
			UProjectGameplayDebugFaithLevel10RowWidget::StaticClass(),
			UProjectGameplayDebugCunningLevel10RowWidget::StaticClass(),
			UProjectGameplayDebugCelerityLevel10RowWidget::StaticClass(),
			UProjectGameplayDebugCharismaLevel10RowWidget::StaticClass());

		return Rows;
	}

	static const FDebugRowDefinition* FindDefinitionForClass(const UClass* WidgetClass)
	{
		static const TArray<FDebugRowDefinition> Rows = MakeDebugRowDefinitions();
		for (const FDebugRowDefinition& Definition : Rows)
		{
			if (Definition.WidgetClass && WidgetClass && WidgetClass->IsChildOf(Definition.WidgetClass))
			{
				return &Definition;
			}
		}
		return nullptr;
	}

	static FDebugRowDefinition MakeDefaultTemplateDefinition()
	{
		FDebugRowDefinition Definition;
		Definition.WidgetClass = UProjectGameplayDebugMenuOptionRowWidget::StaticClass();
		Definition.AssetName = TEXT("WBP_ProjectGameplayDebugMenuOptionRow");
		Definition.OptionId = TEXT("Debug.ImmediateDefeat");
		Definition.Label = TEXT("Immediate Defeat");
		Definition.Description = TEXT("Trigger defeated travel and respawn in the dungeon now.");
		Definition.NodeType = EProjectEmoteMenuNodeType::Action;
		Definition.VisualIconId = TEXT("Combat");
		Definition.VisualMode = EProjectEmoteMenuVisualMode::Category;
		return Definition;
	}

	static const TCHAR* ResolveIconPath(const FName VisualIconId, const FName VisualAttribute)
	{
		if (VisualAttribute == TEXT("Willpower"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Willpower.T_InnerDoctrine_Altar_Icon_Willpower");
		}
		if (VisualAttribute == TEXT("Offensive"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Offensive.T_InnerDoctrine_Altar_Icon_Offensive");
		}
		if (VisualAttribute == TEXT("Defensive"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Defensive.T_InnerDoctrine_Altar_Icon_Defensive");
		}
		if (VisualAttribute == TEXT("Faith"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Faith.T_InnerDoctrine_Altar_Icon_Faith");
		}
		if (VisualAttribute == TEXT("Cunning"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Cunning.T_InnerDoctrine_Altar_Icon_Cunning");
		}
		if (VisualAttribute == TEXT("Celerity"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Celerity.T_InnerDoctrine_Altar_Icon_Celerity");
		}
		if (VisualAttribute == TEXT("Charisma"))
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Charisma.T_InnerDoctrine_Altar_Icon_Charisma");
		}
		if (VisualIconId == TEXT("Basic"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Basic.T_ActionMenu_Icon_Basic");
		}
		if (VisualIconId == TEXT("Combat"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Combat.T_ActionMenu_Icon_Combat");
		}
		if (VisualIconId == TEXT("Objects"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Objects.T_ActionMenu_Icon_Objects");
		}
		if (VisualIconId == TEXT("Social"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Social.T_ActionMenu_Icon_Social");
		}
		if (VisualIconId == TEXT("Special"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Special.T_ActionMenu_Icon_Special");
		}
		if (VisualIconId == TEXT("Cancel"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Cancel.T_ActionMenu_Icon_Cancel");
		}
		if (VisualIconId == TEXT("Back"))
		{
			return TEXT("/Game/UI/Emote/Textures/T_ActionMenu_Icon_Back.T_ActionMenu_Icon_Back");
		}
		return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Default.T_InnerDoctrine_Altar_Icon_Default");
	}

	static FLinearColor ResolveAccentColor(const FProjectEmoteMenuOption& Option)
	{
		if (Option.VisualAttribute == TEXT("Willpower")) return EFProjectUIPalette::AttributeWillpower();
		if (Option.VisualAttribute == TEXT("Offensive")) return EFProjectUIPalette::AttributeOffensive();
		if (Option.VisualAttribute == TEXT("Defensive")) return EFProjectUIPalette::AttributeDefensive();
		if (Option.VisualAttribute == TEXT("Faith")) return EFProjectUIPalette::AttributeFaith();
		if (Option.VisualAttribute == TEXT("Cunning")) return EFProjectUIPalette::AttributeCunning();
		if (Option.VisualAttribute == TEXT("Celerity")) return EFProjectUIPalette::AttributeCelerity();
		if (Option.VisualAttribute == TEXT("Charisma")) return EFProjectUIPalette::AttributeCharisma();
		if (Option.VisualIconId == TEXT("Combat")) return EFProjectUIPalette::Warning();
		if (Option.VisualIconId == TEXT("Objects")) return EFProjectUIPalette::AccentMuted();
		if (Option.VisualIconId == TEXT("Social")) return EFProjectUIPalette::AccentSoft();
		return EFProjectUIPalette::Accent();
	}

	static UTexture2D* LoadPreviewIcon(const FDebugRowDefinition& Definition)
	{
		return LoadObject<UTexture2D>(nullptr, ResolveIconPath(Definition.VisualIconId, Definition.VisualAttribute));
	}

	static void ApplyDefinitionToRow(UProjectGameplayDebugMenuOptionRowWidget& RowWidget, const FDebugRowDefinition& Definition, const bool bSelected)
	{
		const FProjectEmoteMenuOption Option = MakeOption(Definition);
		UTexture2D* PreviewIcon = LoadPreviewIcon(Definition);
		RowWidget.SetDesignerPreviewOption(Option, Definition.VisualMode, Definition.PreviewIndex);
		RowWidget.SetUseDesignerIconOverride(true);
		RowWidget.SetDesignerIconOverride(PreviewIcon);
		RowWidget.ConfigureOption(
			Option,
			Definition.VisualMode,
			Definition.PreviewIndex,
			PreviewRowHeight,
			PreviewIcon,
			ResolveAccentColor(Option),
			true);
		RowWidget.SetOptionVisualState(bSelected, Definition.bEnabled);
	}

	static TArray<FProjectEmoteMenuOption> MakeRootDebugPreviewOptions()
	{
		TArray<FProjectEmoteMenuOption> Options;
		const TArray<FDebugRowDefinition> Rows = MakeDebugRowDefinitions();
		for (const FDebugRowDefinition& Row : Rows)
		{
			if (Row.VisualMode == EProjectEmoteMenuVisualMode::Root)
			{
				Options.Add(MakeOption(Row));
			}
		}
		return Options;
	}

	static TArray<FName> PanelWidgetNames()
	{
		return {
			TEXT("RootCanvas"),
			TEXT("BackdropBorder"),
			TEXT("PanelSizeBox"),
			TEXT("MenuPanelOverlay"),
			TEXT("MenuPanelBorder"),
			TEXT("PanelInnerBorder"),
			TEXT("MenuPanelLayout"),
			TEXT("TitleText"),
			TEXT("HintText"),
			TEXT("MenuDivider"),
			TEXT("OptionsLayout"),
			TEXT("OptionsScrollBox"),
			TEXT("MenuFooterBorder"),
			TEXT("MenuFooterText")
		};
	}

	static TArray<FName> RowWidgetNames()
	{
		return {
			TEXT("RowSizeBox"),
			TEXT("RowOverlay"),
			TEXT("OptionSelectionFrame"),
			TEXT("OptionGlowBorder"),
			TEXT("OptionFrameImage"),
			TEXT("OptionBorder"),
			TEXT("OptionInnerBorder"),
			TEXT("OptionDisabledOverlay"),
			TEXT("SelectorText"),
			TEXT("OptionIconBorder"),
			TEXT("OptionIconImage"),
			TEXT("OptionLabelText"),
			TEXT("OptionDescriptionText"),
			TEXT("OptionArrowText")
		};
	}

	static void AddManifestSpec(
		FCodeWidgetDesignerConversionManifest& Manifest,
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& TargetAssetPath,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank,
		const bool bRuntimeDefault,
		const TArray<FName>& ExpectedWidgetNames,
		const bool bRequiresStableRootWrapper = false)
	{
		FCodeWidgetDesignerWidgetAssetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.TargetAssetPath = TargetAssetPath;
		Spec.Role = Role;
		Spec.PriorityGroup = TEXT("Debug");
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.bRequiresStableRootWrapper = bRequiresStableRootWrapper;
		Spec.ExpectedWidgetNames = ExpectedWidgetNames;
		Spec.ExpectedBlueprintEvents = { TEXT("OnOptionDataChanged"), TEXT("OnOptionVisualStateChanged") };
		Spec.ExpectedVisualStates = { TEXT("Selected"), TEXT("Hover"), TEXT("Disabled"), TEXT("Warning"), TEXT("Active"), TEXT("Cursor"), TEXT("Submenu") };
		Manifest.WidgetAssets.Add(Spec);
	}
}

bool UProjectGameplayDebugMenuOptionRowWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	ProjectGameplayDebugMenuWidgetPrivate::FDebugRowDefinition Definition = ProjectGameplayDebugMenuWidgetPrivate::MakeDefaultTemplateDefinition();
	if (GetClass() == UProjectGameplayDebugMenuOptionRowGlobalWidget::StaticClass())
	{
		Definition.AssetName = TEXT("WBP_ProjectGameplayDebugMenuOptionRowGlobal");
	}
	else if (const ProjectGameplayDebugMenuWidgetPrivate::FDebugRowDefinition* FixedDefinition = ProjectGameplayDebugMenuWidgetPrivate::FindDefinitionForClass(GetClass()))
	{
		Definition = *FixedDefinition;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	ProjectGameplayDebugMenuWidgetPrivate::ApplyDefinitionToRow(*this, Definition, true);
	const bool bBuiltTree = BuildDefaultOptionTree(TargetWidgetTree);
	ProjectGameplayDebugMenuWidgetPrivate::ApplyDefinitionToRow(*this, Definition, true);
	return bBuiltTree;
}

void UProjectGameplayDebugMenuOptionRowWidget::RefreshOptionData()
{
	Super::RefreshOptionData();

	if (RowSizeBox && (!RowSizeBox->IsHeightOverride() || RowSizeBox->GetHeightOverride() < ProjectGameplayDebugMenuWidgetPrivate::PreviewRowHeight))
	{
		RowSizeBox->SetHeightOverride(ProjectGameplayDebugMenuWidgetPrivate::PreviewRowHeight);
	}
}

bool UProjectGameplayDebugMenuWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	VisualMode = EProjectEmoteMenuVisualMode::Root;
	MenuOptions = ProjectGameplayDebugMenuWidgetPrivate::MakeRootDebugPreviewOptions();
	SelectedIndex = 0;
	bUsingNativeFallbackTree = true;
	WidgetTree = TargetWidgetTree;

	const bool bBuiltTree = BuildDefaultMenuTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		if (TitleText)
		{
			TitleText->SetText(FText::FromString(TEXT("GAMEPLAY DEBUG")));
		}
		if (HintText)
		{
			HintText->SetText(FText::FromString(TEXT("Runtime-only commands for fast gameplay verification.")));
		}
		RebuildOptionWidgets();
		RefreshVisualState();
	}

	return bBuiltTree;
}

bool UProjectGameplayDebugMenuWidget::GatherCodeWidgetDesignerConversionManifest(FCodeWidgetDesignerConversionManifest& OutManifest) const
{
	using namespace ProjectGameplayDebugMenuWidgetPrivate;

	OutManifest = FCodeWidgetDesignerConversionManifest();
	OutManifest.SystemName = TEXT("Debug");
	OutManifest.RootPath = TEXT("/Game/_Game/Widgets");
	OutManifest.MainFolder = TEXT("Main");
	OutManifest.GlobalFolder = TEXT("Globals");
	OutManifest.AssetFolders = { TEXT("Debug/Assets/Fonts"), TEXT("Debug/Assets/Textures") };

	OutManifest.HostWidget.WidgetClass = UProjectGameplayDebugMenuWidget::StaticClass();
	OutManifest.HostWidget.TargetAssetPath = TEXT("/Game/_Game/Widgets/Debug/Main/WBP_ProjectGameplayDebugMenu");
	OutManifest.HostWidget.Role = ECodeWidgetDesignerAssetRole::Host;
	OutManifest.HostWidget.PriorityGroup = TEXT("Debug");
	OutManifest.HostWidget.PriorityRank = 9000;
	OutManifest.HostWidget.ExpectedWidgetNames = PanelWidgetNames();

	AddManifestSpec(
		OutManifest,
		UProjectGameplayDebugMenuGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/Debug/Globals/WBP_ProjectGameplayDebugMenuGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		20000,
		true,
		PanelWidgetNames());
	AddManifestSpec(
		OutManifest,
		UProjectGameplayDebugMenuOptionRowGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/Debug/Globals/WBP_ProjectGameplayDebugMenuOptionRowGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		19000,
		true,
		RowWidgetNames());
	AddManifestSpec(
		OutManifest,
		UProjectGameplayDebugMenuRowsGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/Debug/Globals/WBP_ProjectGameplayDebugMenuRowsGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		18000,
		false,
		{ TEXT("DesignerRootOverlay"), TEXT("RootSizeBox"), TEXT("RootOverlay"), TEXT("RowsScrollBox"), TEXT("RowsLayout") },
		true);
	AddManifestSpec(
		OutManifest,
		UProjectGameplayDebugMenuOptionRowWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/Debug/Main/WBP_ProjectGameplayDebugMenuOptionRow"),
		ECodeWidgetDesignerAssetRole::MainBase,
		5000,
		false,
		RowWidgetNames());

	for (const FDebugRowDefinition& RowDefinition : MakeDebugRowDefinitions())
	{
		AddManifestSpec(
			OutManifest,
			RowDefinition.WidgetClass,
			FString::Printf(TEXT("/Game/_Game/Widgets/Debug/Rows/%s"), *RowDefinition.AssetName),
			ECodeWidgetDesignerAssetRole::Individual,
			1000 + RowDefinition.PreviewIndex,
			false,
			RowWidgetNames());
	}

	return true;
}

void UProjectGameplayDebugMenuWidget::GatherCodeWidgetDesignerChildWidgetClasses(TArray<TSubclassOf<UUserWidget>>& OutWidgetClasses) const
{
	OutWidgetClasses.Reset();
}

void UProjectGameplayDebugMenuWidget::GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	OutWidgetSpecs.Reset();
}

TSubclassOf<UProjectEmoteMenuOptionWidget> UProjectGameplayDebugMenuWidget::ResolveOptionRowWidgetClass() const
{
	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	if (UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		UISettings ? UISettings->GameplayDebugMenuOptionRowWidgetClass : FSoftClassPath(),
		UProjectGameplayDebugMenuOptionRowWidget::StaticClass(),
		TEXT("ProjectGameplayDebugMenuOptionRow")))
	{
		return ResolvedClass;
	}

	return UProjectGameplayDebugMenuOptionRowWidget::StaticClass();
}

float UProjectGameplayDebugMenuWidget::ResolveOptionHeight() const
{
	return ProjectGameplayDebugMenuWidgetPrivate::PreviewRowHeight;
}

bool UProjectGameplayDebugMenuRowsGlobalWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	using namespace ProjectGameplayDebugMenuWidgetPrivate;

	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;

	USizeBox* RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(760.0f);
	RootSizeBox->SetHeightOverride(900.0f);
	TargetWidgetTree->RootWidget = RootSizeBox;

	UOverlay* DesignerRootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DesignerRootOverlay"));
	RootSizeBox->SetContent(DesignerRootOverlay);

	UOverlay* RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	if (UOverlaySlot* RootOverlaySlot = DesignerRootOverlay->AddChildToOverlay(RootOverlay))
	{
		RootOverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		RootOverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}

	UScrollBox* RowsScrollBox = TargetWidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RowsScrollBox"));
	RowsScrollBox->SetOrientation(Orient_Vertical);
	if (UOverlaySlot* ScrollSlot = RootOverlay->AddChildToOverlay(RowsScrollBox))
	{
		ScrollSlot->SetHorizontalAlignment(HAlign_Fill);
		ScrollSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* RowsLayout = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowsLayout"));
	RowsScrollBox->AddChild(RowsLayout);

	const TArray<FDebugRowDefinition> Rows = MakeDebugRowDefinitions();
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const FDebugRowDefinition& RowDefinition = Rows[RowIndex];
		const FName RowName(*FString::Printf(TEXT("DebugRow_%s"), *RowDefinition.AssetName));
		TSubclassOf<UProjectGameplayDebugMenuOptionRowWidget> RowClass = UProjectGameplayDebugMenuOptionRowWidget::StaticClass();
		if (RowDefinition.WidgetClass)
		{
			RowClass = RowDefinition.WidgetClass;
		}
		UProjectGameplayDebugMenuOptionRowWidget* RowWidget = TargetWidgetTree->ConstructWidget<UProjectGameplayDebugMenuOptionRowWidget>(
			RowClass,
			RowName);
		if (!RowWidget)
		{
			continue;
		}

		ApplyDefinitionToRow(*RowWidget, RowDefinition, RowIndex == 0);
		if (UVerticalBoxSlot* RowSlot = RowsLayout->AddChildToVerticalBox(RowWidget))
		{
			RowSlot->SetPadding(FMargin(0.0f, RowIndex == 0 ? 0.0f : 12.0f, 0.0f, 0.0f));
		}
	}

	return true;
}
