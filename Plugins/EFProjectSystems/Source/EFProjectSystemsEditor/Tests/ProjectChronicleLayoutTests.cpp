#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/AutomationTest.h"
#include "UI/ProjectActivityFeedEntryRowWidget.h"
#include "UI/ProjectActivityFeedSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/SWidget.h"

namespace ProjectChronicleLayoutTests
{
	UProjectActivityFeedEntryRowWidget* BuildRow(UWidgetTree*& OutWidgetTree)
	{
		UProjectActivityFeedEntryRowWidget* Row = NewObject<UProjectActivityFeedEntryRowWidget>(GetTransientPackage());
		OutWidgetTree = NewObject<UWidgetTree>(Row, TEXT("ChronicleAutomationWidgetTree"));
		return Row && Row->BuildCodeWidgetDesignerTree(OutWidgetTree) ? Row : nullptr;
	}

	UProjectActivityFeedEntryRowWidget* BuildDesignerRow(const TCHAR* ClassPath)
	{
		UClass* RowClass = FSoftClassPath(ClassPath).TryLoadClass<UProjectActivityFeedEntryRowWidget>();
		if (!RowClass)
		{
			return nullptr;
		}

		UProjectActivityFeedEntryRowWidget* Row = NewObject<UProjectActivityFeedEntryRowWidget>(
			GetTransientPackage(),
			RowClass);
		return Row && Row->Initialize() ? Row : nullptr;
	}

	FProjectActivityFeedRowDisplayData MakeRowData(const FText& Message)
	{
		FProjectActivityFeedRowDisplayData Data;
		Data.BadgeLabel = TEXT("ENEMY");
		Data.Message = Message;
		Data.RowWidth = 320.0f;
		Data.RowHeight = 22.0f;
		Data.TextWrapWidth = 96.0f;
		Data.InlinePrimaryWidthRatio = 0.38f;
		Data.LineHeightPercentage = 1.22f;
		Data.BodyFontSize = 13;
		return Data;
	}

	float PrepassAndGetHeight(UWidget* Widget)
	{
		const TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
		SlateWidget->SlatePrepass(1.0f);
		return SlateWidget->GetDesiredSize().Y;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectChronicleLayoutPolicyTest,
	"NoShellForWinter.ProjectSystems.UI.Chronicle.LayoutPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectChronicleLayoutPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UProjectActivityFeedSubsystem* Subsystem = GetDefault<UProjectActivityFeedSubsystem>();
	TestNotNull(TEXT("Activity Feed subsystem CDO resolves"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	const FProjectChronicleLayoutPolicy Compact = Subsystem->GetChronicleLayoutPolicy(false);
	const FProjectChronicleLayoutPolicy Expanded = Subsystem->GetChronicleLayoutPolicy(true);
	TestEqual(TEXT("Compact text width"), Compact.MaximumTextWidth, 390.0f);
	TestEqual(TEXT("Expanded text width"), Expanded.MaximumTextWidth, 450.0f);
	TestEqual(TEXT("Inline primary ratio"), Compact.InlinePrimaryWidthRatio, 0.38f);
	TestEqual(TEXT("Compact line height"), Compact.LineHeightPercentage, 1.22f);
	TestEqual(TEXT("Expanded line height"), Expanded.LineHeightPercentage, 1.18f);
	TestTrue(TEXT("Compact minimum height is positive"), Compact.MinimumRowHeight >= 12.0f);
	TestTrue(TEXT("Expanded minimum height is positive"), Expanded.MinimumRowHeight >= 18.0f);
	TestTrue(TEXT("Row gap is non-negative"), Compact.RowGap >= 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectChronicleDynamicRowTest,
	"NoShellForWinter.ProjectSystems.UI.Chronicle.DynamicRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectChronicleDynamicRowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWidgetTree* ShortTree = nullptr;
	UWidgetTree* LongTree = nullptr;
	UProjectActivityFeedEntryRowWidget* ShortRow = ProjectChronicleLayoutTests::BuildRow(ShortTree);
	UProjectActivityFeedEntryRowWidget* LongRow = ProjectChronicleLayoutTests::BuildRow(LongTree);
	TestNotNull(TEXT("Short Chronicle row builds"), ShortRow);
	TestNotNull(TEXT("Long Chronicle row builds"), LongRow);
	if (!ShortRow || !LongRow || !ShortTree || !LongTree)
	{
		return false;
	}

	ShortRow->ApplyDisplayData(ProjectChronicleLayoutTests::MakeRowData(FText::FromString(TEXT("Short entry."))));
	LongRow->ApplyDisplayData(ProjectChronicleLayoutTests::MakeRowData(FText::FromString(
		TEXT("Watcher says: \"A long Chronicle entry wraps at spaces, keeps explicit\nline breaks, and grows vertically without covering the next entry.\""))));

	const float ShortHeight = ProjectChronicleLayoutTests::PrepassAndGetHeight(ShortRow);
	const float LongHeight = ProjectChronicleLayoutTests::PrepassAndGetHeight(LongRow);
	TestTrue(TEXT("Long wrapped text produces a taller row"), LongHeight > ShortHeight);

	UTextBlock* LongMessage = Cast<UTextBlock>(LongTree->FindWidget(TEXT("MessageText")));
	USizeBox* LongRoot = Cast<USizeBox>(LongTree->FindWidget(TEXT("RootSizeBox")));
	TestNotNull(TEXT("MessageText remains available to WBP binding"), LongMessage);
	TestNotNull(TEXT("RootSizeBox remains available to WBP binding"), LongRoot);
	if (LongMessage)
	{
		TestTrue(TEXT("Automatic wrapping is enabled"), LongMessage->GetAutoWrapText());
		TestEqual(TEXT("Maximum wrap width is applied"), LongMessage->GetWrapTextAt(), 96.0f);
		TestEqual(TEXT("Unbreakable token overflow remains clipped to its maximum width"), LongMessage->GetTextOverflowPolicy(), ETextOverflowPolicy::Clip);
	}
	if (LongRoot)
	{
		TestEqual(TEXT("Configured row height is a minimum"), LongRoot->GetMinDesiredHeight(), 22.0f);
	}

	UVerticalBox* RowsBox = NewObject<UVerticalBox>(GetTransientPackage());
	UVerticalBoxSlot* ShortSlot = RowsBox->AddChildToVerticalBox(ShortRow);
	UVerticalBoxSlot* LongSlot = RowsBox->AddChildToVerticalBox(LongRow);
	if (ShortSlot)
	{
		ShortSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	if (LongSlot)
	{
		LongSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		LongSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
	}
	const float StackHeight = ProjectChronicleLayoutTests::PrepassAndGetHeight(RowsBox);
	TestTrue(TEXT("VerticalBox reserves both dynamic heights and row gap"), StackHeight + KINDA_SMALL_NUMBER >= ShortHeight + LongHeight + 3.0f);

	const FProjectActivityFeedRowDisplayData UrlData = ProjectChronicleLayoutTests::MakeRowData(FText::FromString(TEXT(
		"https://chronicle.example.test/an-indivisible-token-that-is-deliberately-wider-than-the-configured-wrap-width")));
	LongRow->ApplyDisplayData(UrlData);
	TestTrue(
		TEXT("An indivisible URL remains constrained to a valid row"),
		ProjectChronicleLayoutTests::PrepassAndGetHeight(LongRow) >= UrlData.RowHeight);

	FProjectActivityFeedRowDisplayData InlineData = ProjectChronicleLayoutTests::MakeRowData(FText::GetEmpty());
	InlineData.RenderStyle = EProjectActivityFeedRenderStyle::DialogueQuote;
	InlineData.PrimaryText = FText::FromString(TEXT("ACFDummy Enemy BPMale"));
	InlineData.SecondaryText = FText::FromString(TEXT("\"You're already wet for me. This line must wrap by words.\""));
	LongRow->ApplyDisplayData(InlineData);
	TestTrue(
		TEXT("Inline dialogue also grows beyond the minimum height"),
		ProjectChronicleLayoutTests::PrepassAndGetHeight(LongRow) > InlineData.RowHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectChronicleDesignerDynamicRowTest,
	"NoShellForWinter.ProjectSystems.UI.Chronicle.DesignerDynamicRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectChronicleDesignerDynamicRowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	static const TCHAR* DesignerRowClasses[] = {
		TEXT("/Game/_Game/Widgets/Chronicle/Normal/WBP_ProjectChronicleNormalStandardRow.WBP_ProjectChronicleNormalStandardRow_C"),
		TEXT("/Game/_Game/Widgets/Chronicle/Normal/WBP_ProjectChronicleNormalGainRow.WBP_ProjectChronicleNormalGainRow_C"),
		TEXT("/Game/_Game/Widgets/Chronicle/Normal/WBP_ProjectChronicleNormalDialogueQuoteRow.WBP_ProjectChronicleNormalDialogueQuoteRow_C"),
		TEXT("/Game/_Game/Widgets/Chronicle/Expanded/WBP_ProjectChronicleExpandedStandardRow.WBP_ProjectChronicleExpandedStandardRow_C"),
		TEXT("/Game/_Game/Widgets/Chronicle/Expanded/WBP_ProjectChronicleExpandedGainRow.WBP_ProjectChronicleExpandedGainRow_C"),
		TEXT("/Game/_Game/Widgets/Chronicle/Expanded/WBP_ProjectChronicleExpandedDialogueQuoteRow.WBP_ProjectChronicleExpandedDialogueQuoteRow_C")
	};

	for (const TCHAR* ClassPath : DesignerRowClasses)
	{
		UProjectActivityFeedEntryRowWidget* ShortRow = ProjectChronicleLayoutTests::BuildDesignerRow(ClassPath);
		UProjectActivityFeedEntryRowWidget* LongRow = ProjectChronicleLayoutTests::BuildDesignerRow(ClassPath);
		TestNotNull(FString::Printf(TEXT("Designer short row loads: %s"), ClassPath), ShortRow);
		TestNotNull(FString::Printf(TEXT("Designer long row loads: %s"), ClassPath), LongRow);
		if (!ShortRow || !LongRow)
		{
			continue;
		}

		const bool bDialogueRow = FString(ClassPath).Contains(TEXT("DialogueQuote"));
		const bool bGainRow = FString(ClassPath).Contains(TEXT("GainRow"));
		const bool bExpandedRow = FString(ClassPath).Contains(TEXT("/Expanded/"));

		FProjectActivityFeedRowDisplayData ShortData =
			ProjectChronicleLayoutTests::MakeRowData(FText::FromString(TEXT("Short entry.")));
		FProjectActivityFeedRowDisplayData LongData =
			ProjectChronicleLayoutTests::MakeRowData(FText::FromString(
				TEXT("The Chronicle must wrap this deliberately long sentence at spaces and grow vertically without drawing over the following entry.")));
		ShortData.RowWidth = LongData.RowWidth = bExpandedRow ? 580.0f : 520.0f;
		ShortData.TextWrapWidth = LongData.TextWrapWidth = bExpandedRow ? 450.0f : 390.0f;
		ShortData.RowHeight = LongData.RowHeight = bExpandedRow ? 32.0f : 22.0f;
		ShortData.bExpanded = LongData.bExpanded = bExpandedRow;

		if (bDialogueRow || bGainRow)
		{
			ShortData.RenderStyle = LongData.RenderStyle = bDialogueRow
				? EProjectActivityFeedRenderStyle::DialogueQuote
				: EProjectActivityFeedRenderStyle::Gain;
			ShortData.PrimaryText = FText::FromString(bDialogueRow ? TEXT("Watcher") : TEXT("+15"));
			LongData.PrimaryText = FText::FromString(bDialogueRow ? TEXT("Watcher of the Winter Gate") : TEXT("+1500"));
			ShortData.SecondaryText = FText::FromString(TEXT("Short entry."));
			LongData.SecondaryText = FText::FromString(
				TEXT("This deliberately long secondary message must wrap by complete words and increase the row height."));
		}

		ShortRow->ApplyDisplayData(ShortData);
		LongRow->ApplyDisplayData(LongData);

		const float ShortHeight = ProjectChronicleLayoutTests::PrepassAndGetHeight(ShortRow);
		const float LongHeight = ProjectChronicleLayoutTests::PrepassAndGetHeight(LongRow);
		TestTrue(
			FString::Printf(TEXT("Designer long row grows vertically: %s"), ClassPath),
			LongHeight > ShortHeight);

		USizeBox* RootSizeBox = Cast<USizeBox>(LongRow->GetWidgetFromName(TEXT("RootSizeBox")));
		TestNotNull(FString::Printf(TEXT("Designer RootSizeBox is bound: %s"), ClassPath), RootSizeBox);
		if (RootSizeBox)
		{
			TestFalse(
				FString::Printf(TEXT("Designer row clears fixed height: %s"), ClassPath),
				RootSizeBox->IsHeightOverride());
		}

		UVerticalBox* RowsBox = NewObject<UVerticalBox>(GetTransientPackage());
		UVerticalBoxSlot* LongSlot = RowsBox->AddChildToVerticalBox(LongRow);
		UVerticalBoxSlot* ShortSlot = RowsBox->AddChildToVerticalBox(ShortRow);
		if (LongSlot)
		{
			LongSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		if (ShortSlot)
		{
			ShortSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			ShortSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
		}

		const float StackHeight = ProjectChronicleLayoutTests::PrepassAndGetHeight(RowsBox);
		TestTrue(
			FString::Printf(TEXT("Designer stack reserves dynamic heights: %s"), ClassPath),
			StackHeight + KINDA_SMALL_NUMBER >= LongHeight + ShortHeight + 3.0f);
	}

	return true;
}

#endif
