#if WITH_DEV_AUTOMATION_TESTS
#include "Survival/ProjectInnerStateMath.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectInnerStateComponent.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInnerStatePoisonConservation,
    "NoShellForWinter.InnerState.Poison.ConservationHealingAndRepeatedExposure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInnerStatePoisonConservation::RunTest(const FString&)
{
    FProjectPoisonTuning T;
    FProjectPoisonLedger L;
    const FGuid First = FGuid::NewGuid();
    TestTrue(TEXT("Accept first exposure"), L.AddDose(First, TEXT("Food"), EProjectPoisonSpeed::Fast, 100, 1000, T));
    TestFalse(TEXT("Same event cannot duplicate exposure"), L.AddDose(First, TEXT("Food"), EProjectPoisonSpeed::Fast, 100, 1000, T));
    double Health = 1000 - L.Advance(99, T);
    TestTrue(TEXT("One percent life and debt remain"), FMath::IsNearlyEqual(Health, 10.0, 1.e-6) && FMath::IsNearlyEqual(L.GetPendingDamage(), 10.0, 1.e-6));
    Health += 250;
    Health -= L.Advance(1, T);
    TestTrue(TEXT("Healing does not create new poison debt"), FMath::IsNearlyEqual(Health, 250.0, 1.e-6));
    TestTrue(TEXT("New poisoned food adds debt after old dose"), L.AddDose(FGuid::NewGuid(), TEXT("Food"), EProjectPoisonSpeed::Fast, 100, 1000, T));
    L.AddDose(FGuid::NewGuid(), TEXT("Food"), EProjectPoisonSpeed::Fast, 100, 1000, T);
    TestEqual(TEXT("No gameplay 100 cap"), L.GetPendingPoints(), 200.0);
    TestEqual(TEXT("Twice max health debt"), L.GetPendingDamage(), 2000.0);
    TestTrue(TEXT("Many same-family doses share one throughput"), FMath::IsNearlyEqual(L.Advance(1, T), 10.0, 1.e-6));
    TestTrue(TEXT("Antidote removes only remaining debt"), FMath::IsNearlyEqual(L.Neutralize(), 1990.0, 1.e-6));
    TestEqual(TEXT("No damage after antidote"), L.Advance(300, T), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInnerStatePoisonPartition,
    "NoShellForWinter.InnerState.Poison.LatencyPartitionAndSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInnerStatePoisonPartition::RunTest(const FString&)
{
    FProjectPoisonTuning T;
    FProjectPoisonLedger L;
    L.AddDose(FGuid::NewGuid(), TEXT("Food"), EProjectPoisonSpeed::Fast, 25, 1000, T);
    TestEqual(TEXT("Fast latency is three seconds"), L.Advance(3, T), 0.0);
    TestEqual(TEXT("Fast dose consumes finite 25 percent"), L.Advance(25, T), 250.0);
    L.AddDose(FGuid::NewGuid(), TEXT("Gas"), EProjectPoisonSpeed::Normal, 25, 1000, T);
    L.Advance(5, T);
    L.AddDose(FGuid::NewGuid(), TEXT("Food"), EProjectPoisonSpeed::Slow, 25, 2000, T);
    TestEqual(TEXT("Different max-health snapshots retained"), L.GetPendingDamage(), 750.0);
    // Save/load the actual USTRUCT, not an imitation of its fields.
    TArray<uint8> Bytes;
    { FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Ar(Writer, false); Ar.ArIsSaveGame = true;
      FProjectPoisonLedger::StaticStruct()->SerializeItem(Ar, &L, nullptr); }
    FProjectPoisonLedger Restored;
    { FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Ar(Reader, true); Ar.ArIsSaveGame = true;
      FProjectPoisonLedger::StaticStruct()->SerializeItem(Ar, &Restored, nullptr); }
    double SplitDamage = 0.0;
    for (int32 I = 0; I < 800; ++I) SplitDamage += Restored.Advance(0.1, T);
    TestTrue(TEXT("Mixed type/delay integration independent of subdivision and serialization"),
        FMath::IsNearlyEqual(L.Advance(80, T), SplitDamage, 1.e-6));
    TestTrue(TEXT("Pending debt invariant survives save/load"), FMath::IsNearlyEqual(L.GetPendingDamage(), Restored.GetPendingDamage(), 1.e-6));
    TestTrue(TEXT("Total debt conserved"), FMath::IsNearlyEqual(SplitDamage + Restored.GetPendingDamage(), 750.0, 1.e-6));
    TestFalse(TEXT("Reject infinity without modifying ledger"), L.AddDose(FGuid::NewGuid(), NAME_None, EProjectPoisonSpeed::Fast,
        std::numeric_limits<double>::infinity(), 1000, T));
    FProjectPoisonLedger Threshold;
    Threshold.AddDose(FGuid::NewGuid(), NAME_None, EProjectPoisonSpeed::Slow, 99, 100, T);
    TestFalse(TEXT("Below threshold stays latent"), Threshold.HasActiveDose());
    Threshold.AddDose(FGuid::NewGuid(), NAME_None, EProjectPoisonSpeed::Slow, 1, 100, T);
    TestTrue(TEXT("100 activates all pending doses immediately"), Threshold.HasActiveDose());
    TestTrue(TEXT("Immediate activation is not instantaneous full damage"), FMath::IsNearlyEqual(Threshold.Advance(1, T), 0.25, 1.e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInnerStateDynamics,
    "NoShellForWinter.InnerState.Dynamics.RecoveryWellbeingAndAwareness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInnerStateDynamics::RunTest(const FString&)
{
    using M = FProjectInnerStateMath;
    TestTrue(TEXT("Stress reaches half of sustained target after 2 calendar hours"), FMath::IsNearlyEqual(M::ApproachHalfLife(0, 70, 2, 2), 35.0, 1.e-6));
    TestTrue(TEXT("Sleep recovery 80 to 20 in 2 hours"), FMath::IsNearlyEqual(M::ApproachHalfLife(80, 0, 2, 1), 20.0, 1.e-6));
    TestTrue(TEXT("Three days adverse wellbeing matches approved balance"), FMath::IsNearlyEqual(M::IntegrateWellbeing(0, -1, 72, 48), -44.766015244, 1.e-6));
    double Split = 0;
    for (int32 I = 0; I < 720; ++I) Split = M::IntegrateWellbeing(Split, -1, 0.1, 48);
    TestTrue(TEXT("Wellbeing constant inputs are partition invariant"), FMath::IsNearlyEqual(Split, M::IntegrateWellbeing(0, -1, 72, 48), 1.e-6));
    TestTrue(TEXT("Opposing effects cancel before integration"), FMath::IsNearlyEqual(M::IntegrateWellbeing(0, -.4-.1+.25+.25, 48, 48), 0.0, 1.e-6));
    TestEqual(TEXT("Acute loss cannot be cancelled by passive recovery"), M::AdvanceAwareness(100, 100, 5, 4, 2, 20, true), 0.0);
    TestEqual(TEXT("Impaired capacity cannot create negative consciousness"), M::AdvanceAwareness(100, 20, 0, 4, 2, 100, true), 20.0);
    TestEqual(TEXT("Recovery held after a hit"), M::AdvanceAwareness(30, 100, 0, 4, 2, 5, false), 30.0);
    TestEqual(TEXT("Awareness recovery does not overshoot"), M::AdvanceAwareness(0, 100, 0, 4, 2, 40, true), 100.0);
    TestTrue(TEXT("Exposure probability composes across time intervals"), FMath::IsNearlyEqual(M::ExposureProbability(.005, 24),
        1.0 - FMath::Square(1.0 - M::ExposureProbability(.005, 12)), 1.e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectInnerStateBipolarSave,
    "NoShellForWinter.InnerState.Compatibility.BipolarLegacyAndComponentSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectInnerStateBipolarSave::RunTest(const FString&)
{
    auto* Needs=NewObject<UProjectSurvivalNeedsComponent>(); Needs->ResetToDefaults();
    TestTrue(TEXT("Bipolar public adapter accepts pleasure"),Needs->SetSensationCurrentValue(TEXT("Sensation"),-50));
    TestEqual(TEXT("Pleasure is retained"),Needs->GetSensationCurrentValue(TEXT("Sensation")),-50.f);
    TestEqual(TEXT("Legacy pain never reports pleasure as pain"),Needs->GetSensationCurrentValue(TEXT("Pain")),0.f);
    Needs->ModifySensationValue(TEXT("Pain"),-25);
    TestEqual(TEXT("Legacy analgesia neither creates nor removes pleasure"),Needs->GetSensationCurrentValue(TEXT("Sensation")),-50.f);
    Needs->ModifySensationValue(TEXT("Pain"),20);
    TestEqual(TEXT("Pleasure never absorbs a harmful hit"),Needs->GetSensationCurrentValue(TEXT("Pain")),20.f);
    Needs->ModifySensationValue(TEXT("Pain"),-50);
    TestEqual(TEXT("Analgesia stops at neutral"),Needs->GetSensationCurrentValue(TEXT("Sensation")),0.f);
    Needs->SetSensationCurrentValue(TEXT("Sensation"),-75);
    TArray<uint8> Bytes;
    { FMemoryWriter W(Bytes); FObjectAndNameAsStringProxyArchive A(W,false); A.ArIsSaveGame=true; Needs->Serialize(A); }
    Needs->ResetToDefaults();
    { FMemoryReader R(Bytes); FObjectAndNameAsStringProxyArchive A(R,true); A.ArIsSaveGame=true; Needs->Serialize(A); }
    Needs->OnInnerStateLoaded();
    TestEqual(TEXT("Actual component save retains signed value"),Needs->GetSensationCurrentValue(TEXT("Sensation")),-75.f);
    FProjectInnerStatePersistentState State; State.Awareness=12; State.Hygiene=8; State.BathroomHours=47;
    State.Poison.AddDose(FGuid::NewGuid(),TEXT("Food"),EProjectPoisonSpeed::Slow,200,300,FProjectPoisonTuning());
    Bytes.Reset();
    { FMemoryWriter W(Bytes); FObjectAndNameAsStringProxyArchive A(W,false); A.ArIsSaveGame=true; FProjectInnerStatePersistentState::StaticStruct()->SerializeItem(A,&State,nullptr); }
    State=FProjectInnerStatePersistentState();
    { FMemoryReader R(Bytes); FObjectAndNameAsStringProxyArchive A(R,true); A.ArIsSaveGame=true; FProjectInnerStatePersistentState::StaticStruct()->SerializeItem(A,&State,nullptr); }
    TestEqual(TEXT("Awareness persists"),State.Awareness,12.0);
    TestEqual(TEXT("Bathroom deadline persists"),State.BathroomHours,47.0);
    TestEqual(TEXT("Uncapped debt persists"),State.Poison.GetPendingDamage(),600.0);
    return true;
}
#endif
