#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InputComponent.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/ProjectActivityFeedSubsystem.h"
#include "UI/ProjectChroniclePanelWidget.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

namespace ProjectChronicleHistoryTests
{
	class FHistoryScenario final : public IAutomationLatentCommand
	{
	public:
		explicit FHistoryScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90.0)
			{
				Test->AddError(TEXT("Chronicle history PIE timed out"));
				Cleanup();
				return true;
			}
			if (Now < NextStep)
			{
				return false;
			}
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			UProjectActivityFeedSubsystem* Feed = World ? World->GetSubsystem<UProjectActivityFeedSubsystem>() : nullptr;
			UProjectSurvivalNeedsSubsystem* Needs = World ? World->GetSubsystem<UProjectSurvivalNeedsSubsystem>() : nullptr;
			if (!PC || !Feed || !Needs || !Feed->HasFeedWidget() || Now - Started < 5.0)
			{
				return false;
			}
			Controller = PC;
			NextStep = Now + 0.35;
			switch (Step++)
			{
			case 0:
				Sentinel = NewObject<UInputComponent>(PC, TEXT("ChronicleLowerPriorityInputTest"));
				Sentinel->Priority = 74;
				Sentinel->RegisterComponent();
				{
					FInputKeyBinding Binding(FInputChord(EKeys::Down), IE_Pressed);
					Binding.bConsumeInput = true;
					Binding.KeyDelegate.GetDelegateForManualSet().BindLambda([this]() { ++LowerPriorityDowns; });
					Sentinel->KeyBindings.Add(Binding);
				}
				PC->PushInputComponent(Sentinel.Get());
				Needs->SetNeedsHudVisible(true);
				if (Feed->IsFeedExpanded()) Feed->RequestToggleExpanded();
				Needs->SetNeedsHudVisible(false);
				for (int32 Index = 0; Index < 35; ++Index)
				{
					Feed->DebugAddSystemEntry(FText::FromString(FString::Printf(
						TEXT("History %02d: A recorded Chronicle event that remains available while the player reads earlier entries."), Index)));
				}
				Tap(EKeys::Down);
				break;
			case 1:
				Test->TestFalse(TEXT("Closed comma HUD cannot scroll"), Feed->CanScrollHistory());
				Test->TestEqual(TEXT("Down reaches normal input outside comma HUD"), LowerPriorityDowns, 1);
				Tap(EKeys::Comma);
				break;
			case 2:
				Test->TestTrue(TEXT("Comma opens the needs HUD"), Needs->IsNeedsHudVisible());
				Test->TestFalse(TEXT("Compact Chronicle cannot scroll"), Feed->CanScrollHistory());
				Tap(EKeys::Down);
				break;
			case 3:
				Test->TestEqual(TEXT("Compact Chronicle leaves Down alone"), LowerPriorityDowns, 2);
				Tap(EKeys::J);
				break;
			case 4:
				Test->TestTrue(TEXT("Comma plus J enables history navigation"), Feed->CanScrollHistory());
				for (TObjectIterator<UProjectChronicleExpandedGlobalWidget> It; It; ++It)
				{
					if (It->GetWorld() == World && It->GetEntriesBox() && It->GetEntriesBox()->GetChildrenCount() > 0)
					{
						Panel = *It;
						break;
					}
				}
				if (!Test->TestNotNull(TEXT("Expanded panel is live"), Panel.Get()))
				{
					Cleanup();
					return true;
				}
				Test->TestEqual(TEXT("Expanded history includes every stored entry, beyond 20"), Panel->GetEntriesBox()->GetChildrenCount(), Feed->GetStoredEntryCount());
				PreviousOffset = Scroll()->GetScrollOffset();
				Test->TestTrue(TEXT("Long history has a scrollable range"), PreviousOffset > 0.0f);
				Key(EKeys::Up, IE_Pressed);
				break;
			case 5:
				Test->TestTrue(TEXT("First Up press immediately moves toward older entries"), Scroll()->GetScrollOffset() < PreviousOffset);
				PreviousOffset = Scroll()->GetScrollOffset();
				Key(EKeys::Up, IE_Repeat);
				break;
			case 6:
				Test->TestTrue(TEXT("Held Up repeats scrolling"), Scroll()->GetScrollOffset() < PreviousOffset);
				Key(EKeys::Up, IE_Released);
				for (int32 Index = 0; Index < 200; ++Index) Feed->RequestScrollHistory(-1);
				Test->TestEqual(TEXT("Up clamps at the oldest stored event"), Scroll()->GetScrollOffset(), 0.0f);
				Feed->DebugAddSystemEntry(FText::FromString(TEXT("New event while reading the oldest entry")));
				break;
			case 7:
				Test->TestEqual(TEXT("New events preserve the reading position"), Scroll()->GetScrollOffset(), 0.0f);
				Tap(EKeys::Down);
				break;
			case 8:
				Test->TestTrue(TEXT("Down moves toward newer entries"), Scroll()->GetScrollOffset() > 0.0f);
				Test->TestEqual(TEXT("Expanded Chronicle consumes Down before gameplay"), LowerPriorityDowns, 2);
				for (int32 Index = 0; Index < 200; ++Index) Feed->RequestScrollHistory(1);
				Test->TestTrue(TEXT("Down clamps at the newest event"), FMath::IsNearlyEqual(Scroll()->GetScrollOffset(), Scroll()->GetScrollOffsetOfEnd(), 1.0f));
				Tap(EKeys::J);
				break;
			case 9:
				Test->TestFalse(TEXT("Collapsing immediately disables history input"), Feed->CanScrollHistory());
				Tap(EKeys::Down);
				break;
			case 10:
				Test->TestEqual(TEXT("Down returns to normal input after collapse"), LowerPriorityDowns, 3);
				Tap(EKeys::J);
				break;
			case 11:
				Test->TestTrue(TEXT("History navigation reactivates on expansion"), Feed->CanScrollHistory());
				Tap(EKeys::Comma);
				break;
			case 12:
				Test->TestFalse(TEXT("Hiding comma HUD disables arrows while Chronicle stays expanded"), Feed->CanScrollHistory());
				Test->TestTrue(TEXT("Expanded state alone is insufficient"), Feed->IsFeedExpanded());
				Tap(EKeys::Down);
				break;
			case 13:
				Test->TestEqual(TEXT("Down returns to normal input after hiding HUD"), LowerPriorityDowns, 4);
				Needs->SetNeedsHudVisible(true);
				for (int32 Index = 0; Index < 200; ++Index) Feed->RequestScrollHistory(-1);
				break;
			case 14:
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Migration/ChronicleHistory/OldestEntries.png"), true, false);
				NextStep = Now + 5.0;
				break;
			default:
				Cleanup();
				return true;
			}
			return false;
		}

	private:
		UScrollBox* Scroll() const { return Panel->GetEntriesScrollBox(); }
		void Key(const FKey& InKey, const EInputEvent Event)
		{
			Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), InKey, Event, FPlatformTime::Cycles64()));
		}
		void Tap(const FKey& InKey) { Key(InKey, IE_Pressed); Key(InKey, IE_Released); }
		void Cleanup()
		{
			if (Sentinel.IsValid())
			{
				if (Controller.IsValid()) Controller->PopInputComponent(Sentinel.Get());
				Sentinel->DestroyComponent();
			}
		}
		FAutomationTestBase* Test;
		double Started;
		double NextStep = 0.0;
		int32 Step = 0;
		int32 LowerPriorityDowns = 0;
		float PreviousOffset = 0.0f;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UInputComponent> Sentinel;
		TWeakObjectPtr<UProjectChronicleExpandedGlobalWidget> Panel;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectChronicleHistoryPIETest,
	"NoShellForWinter.ProjectSystems.UI.Chronicle.HistoryPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectChronicleHistoryPIETest::RunTest(const FString& Parameters)
{
	// HUB currently emits this unrelated quest startup error once during PIE.
	AddExpectedError(TEXT("Can't Start the quest"), EAutomationExpectedErrorFlags::Contains, 1);
	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/_Game/Hub/HUB"));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(ProjectChronicleHistoryTests::FHistoryScenario(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
