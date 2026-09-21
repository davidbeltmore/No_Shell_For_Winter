#include "Calysto/EFCalystoAsyncLoadingBudget.h"
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace EFCalystoAsyncLoadingBudget
{
    namespace
    {
        constexpr float CriticalBudgetMilliseconds = 5.0f;

        class FOwners
        {
        public:
            explicit FOwners(IConsoleVariable* InVariable) : Variable(InVariable) {}
            ~FOwners() { Restore(); }

            void SetActive(const void* Owner, bool bActive)
            {
                check(IsInGameThread());
                if (!Owner) return;
                if (!bActive)
                {
                    Owners.Remove(Owner);
                    if (Owners.IsEmpty()) Restore();
                    return;
                }
                if (Owners.Contains(Owner)) return;
                Owners.Add(Owner);
                if (Owners.Num() != 1 || !Variable) return;
                const float Previous = Variable->GetFloat();
                // Never reduce an existing larger budget or reinterpret zero/nonfinite settings.
                if (!FMath::IsFinite(Previous) || Previous <= 0 || Previous >= CriticalBudgetMilliseconds) return;
                PreviousValue = Variable->GetString();
                PreviousPriority = EConsoleVariableFlags(Variable->GetFlags() & ECVF_SetByMask);
                bExternalWrite = false;
                OwnWriteNotifications = 0;
                Changed = Variable->OnChangedDelegate().AddLambda([this](IConsoleVariable*)
                {
                    // UE invokes this even for same-value writes. An outside owner then wins.
                    if (!bWriting || ++OwnWriteNotifications > 1) bExternalWrite = true;
                });
                TGuardValue<bool> Writing(bWriting, true);
                Variable->Set(CriticalBudgetMilliseconds, PreviousPriority);
            }

        private:
            void Restore()
            {
                if (!Changed.IsValid()) return;
                Variable->OnChangedDelegate().Remove(Changed);
                Changed.Reset();
                if (!bExternalWrite && Variable->GetFloat() == CriticalBudgetMilliseconds &&
                    EConsoleVariableFlags(Variable->GetFlags() & ECVF_SetByMask) == PreviousPriority)
                {
                    // Keep the original SetBy priority; do not save config or clear outside flags.
                    Variable->Set(*PreviousValue, PreviousPriority);
                }
            }

            IConsoleVariable* Variable = nullptr;
            TSet<const void*> Owners;
            FString PreviousValue;
            EConsoleVariableFlags PreviousPriority = ECVF_SetByConstructor;
            FDelegateHandle Changed;
            bool bWriting = false;
            bool bExternalWrite = false;
            int32 OwnWriteNotifications = 0;
        };

        IConsoleVariable* BudgetVariable()
        {
            return IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit"));
        }
    }

    void SetOwnerActive(const void* Owner, bool bActive)
    {
        static FOwners SharedOwners(BudgetVariable());
        SharedOwners.SetActive(Owner, bActive);
    }

    float GetEffectiveMilliseconds()
    {
        const IConsoleVariable* Variable = BudgetVariable();
        const float Value = Variable ? Variable->GetFloat() : -1.0f;
        return FMath::IsFinite(Value) ? Value : -1.0f;
    }

#if WITH_DEV_AUTOMATION_TESTS
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBudgetOwnershipTest,
        "NoShellForWinter.CalystoDungeon.Director.Lifecycle.AsyncLoadingBudgetOwnership",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBudgetOwnershipTest::RunTest(const FString&)
    {
        float ReferencedBudget = 1.0f;
        auto& Console = IConsoleManager::Get();
        IConsoleVariable* Variable = Console.RegisterConsoleVariableRef(
            TEXT("Calysto.Test.AsyncLoadingBudgetOwnership"), ReferencedBudget, TEXT("Scoped fixture"));
        Variable->Set(1.0f, ECVF_SetByProjectSetting);
        const auto InitialFlags = Variable->GetFlags();
        const int32 First = 1, Second = 2;
        {
            FOwners Scope(Variable);
            Scope.SetActive(&First, true);
            TestEqual(TEXT("CVar changes the float used by the engine tick, not a detached copy"), ReferencedBudget, 5.0f);
            TestEqual(TEXT("The original priority is preserved"), Variable->GetFlags(), InitialFlags);
            Scope.SetActive(&First, true);
            Scope.SetActive(&Second, true);
            Scope.SetActive(&First, false);
            TestEqual(TEXT("A remaining owner retains the critical budget"), ReferencedBudget, 5.0f);
            Scope.SetActive(&Second, false);
            TestEqual(TEXT("Last owner restores the exact original budget"), ReferencedBudget, 1.0f);
            Scope.SetActive(&First, true);
            Variable->Set(5.0f, ECVF_SetByProjectSetting);
            Scope.SetActive(&First, false);
            TestEqual(TEXT("Even an external same-value write owns the result"), ReferencedBudget, 5.0f);
            Variable->Set(1.0f, ECVF_SetByProjectSetting);
            Scope.SetActive(&First, true);
            Variable->Set(2.5f, ECVF_SetByConsole);
            Scope.SetActive(&First, false);
            TestEqual(TEXT("Outside values survive restoration"), ReferencedBudget, 2.5f);
            TestEqual(TEXT("Outside priority survives restoration"),
                EConsoleVariableFlags(Variable->GetFlags() & ECVF_SetByMask), ECVF_SetByConsole);
            Variable->Set(8.0f, ECVF_SetByConsole);
            Scope.SetActive(&First, true);
            Scope.SetActive(&First, false);
            TestEqual(TEXT("An existing larger budget is never reduced"), ReferencedBudget, 8.0f);
            Variable->Set(1.0f, ECVF_SetByConsole);
            Scope.SetActive(&First, true);
        }
        TestEqual(TEXT("Scope teardown restores a remaining owner"), ReferencedBudget, 1.0f);
        Console.UnregisterConsoleObject(Variable, false);
        return true;
    }
#endif
}
