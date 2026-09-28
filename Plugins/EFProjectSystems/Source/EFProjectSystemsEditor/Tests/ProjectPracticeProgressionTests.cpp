#include "Misc/AutomationTest.h"
#include "Progression/ProjectPracticeMath.h"
#include "Progression/ProjectPracticeSettings.h"
#include <limits>
#include "Progression/ProjectPracticeProgressionComponent.h"
#include "Progression/ProjectDoctrineAttributeSet.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "ACFGASStatisticsComponent.h"
#include "ACFPrimaryAttributeSet.h"
#include "ACFSkillTreeComponent.h"
#include "Engine/World.h"
#include "Curves/CurveFloat.h"
#include "GameplayEffect.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Progression/ProjectProgressionState.h"
#include "Training/ProjectPracticeComponent.h"
#include "ACFStatisticsSet.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeMathTest, "NoShellForWinter.Practice.IntegrationAndRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeMathTest::RunTest(const FString& Parameters)
{
    using namespace ProjectPracticeMath;
    TestEqual(TEXT("Fresh efficiency"), Efficiency(0, 3), 1.0);
    TestTrue(TEXT("Repeated abuse asymptotically retains 0.01 percent"), FMath::Abs(Efficiency(300, 3) - 0.0001) < 1.e-12);
    TestTrue(TEXT("Nonzero reward even after extreme repetition"), EffectiveHours(300, 48, 3) > 0.0);
    TestTrue(TEXT("Reject NaN duration"), EffectiveHours(0, std::numeric_limits<double>::quiet_NaN(), 3) == 0.0);
    const double Whole = EffectiveHours(4, 48, 3);
    for (double Step : {1.0/75.0, 2.0/75.0, 4.0/75.0})
    {
        double Sum=0, Elapsed=0;
        while (Elapsed < 48.0) { const double H=FMath::Min(Step,48.0-Elapsed); Sum+=EffectiveHours(4+Elapsed,H,3); Elapsed+=H; }
        TestTrue(TEXT("x1 x2 x4 / frame partition invariant"), FMath::Abs(Whole-Sum)<1.e-9);
    }
    const double Split = EffectiveHours(4, 2, 3) + EffectiveHours(6, 46, 3);
    TestTrue(TEXT("Cancel/resume does not reset repetition"), FMath::Abs(Whole-Split)<1.e-12);
    TestEqual(TEXT("Pause awards nothing"), EffectiveHours(0,0,3), 0.0);
    const double Recovery = Recover(12, 3, 2, 12);
    TestTrue(TEXT("Recovery partition invariant"), FMath::Abs(Recovery-Recover(Recover(12,1,2,12),2,2,12))<1.e-12);
    TestTrue(TEXT("Abuse increases required rest superlinearly"), HoursToRecover(24,2,12)>2*HoursToRecover(12,2,12));
    TestTrue(TEXT("Enough rest fully recovers without negative debt"), Recover(24,HoursToRecover(24,2,12)+1,2,12)==0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeDefinitionsTest, "NoShellForWinter.Practice.Definitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeDefinitionsTest::RunTest(const FString& Parameters)
{
    const auto* S=GetDefault<UProjectPracticeSettings>();
    TSet<FName> Ids, Attributes;
    for (const auto& D:S->Activities)
    {
        TestFalse(TEXT("Activity id unique"),Ids.Contains(D.Id)); Ids.Add(D.Id); Attributes.Add(D.Attribute);
        TestTrue(TEXT("Practice rate valid"),FMath::IsFinite(D.PracticePerHour)&&D.PracticePerHour>0);
        if(D.Method==EProjectPracticeMethod::Book) TestTrue(TEXT("Book duration bounded"),D.DurationHours>0&&D.DurationHours<=48);
    }
    TestEqual(TEXT("Twelve trainable attributes"),Attributes.Num(),12);
    TestTrue(TEXT("Prayer unbounded"),S->Find(TEXT("Pray"))->DurationHours==0);
    TestTrue(TEXT("Lock duration resolved from context"),S->Find(TEXT("PracticeLock"))->Method==EProjectPracticeMethod::Lock);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeNativeTest, "NoShellForWinter.Practice.NativeProgression", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeNativeTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AActor* Owner=World->SpawnActor<AActor>();
    auto* ASC=NewObject<UAbilitySystemComponent>(Owner);
    auto* Stats=NewObject<UACFGASStatisticsComponent>(Owner);
    auto* Skills=NewObject<UACFSkillTreeComponent>(Owner);
    auto* Practice=NewObject<UProjectPracticeProgressionComponent>(Owner);
    for(UActorComponent* C : TArray<UActorComponent*>{ASC,Stats,Skills,Practice}) { Owner->AddInstanceComponent(C); C->RegisterComponent(); }
    ASC->InitAbilityActorInfo(Owner,Owner);
    ASC->AddAttributeSetSubobject(NewObject<UACFPrimaryAttributeSet>(Owner));
    auto* Curve=NewObject<UCurveFloat>(); Curve->FloatCurve.AddKey(1,100); Curve->FloatCurve.AddKey(255,100);
    Stats->SetLevelingType(ELevelingType::EAssignPerksManually); Stats->SetExpForNextLevelCurve(Curve); Stats->ForceSetLevel(1); Stats->InitializeAttributeSet();
    Practice->RefreshBindings(); Practice->RefreshBindings();
    TestNotNull(TEXT("Doctrine set exists on original ASC"),ASC->GetSet<UProjectDoctrineAttributeSet>());
    TestTrue(TEXT("Natural learning grants practice"),Practice->Practice(TEXT("Willpower"),2,100)>100);
    TestEqual(TEXT("Rank earned without points"),Practice->GetLearnedRank(TEXT("Willpower")),1.f);
    const double Fraction=Practice->GetPracticeProgress(TEXT("Willpower"));
    TestTrue(TEXT("Partial progress retained"),Fraction>0.0);
    TestTrue(TEXT("Rank bar measures learning toward the next rank"),FMath::Abs(Practice->GetRankProgressNormalized(TEXT("Willpower"))-Fraction/108.0)<1.e-6);
    const int32 OldPoints=Skills->GetSkillPoints();
    TestTrue(TEXT("Native XP accepts multi-level reward"),Practice->GrantExperience(250));
    TestEqual(TEXT("One native level counter"),Stats->GetCurrentLevel(),3);
    TestEqual(TEXT("Native perk balance unavailable for attributes"),Stats->GetAvailablePerks(),0);
    TestEqual(TEXT("Two levels award exactly two skill points"),Skills->GetSkillPoints(),OldPoints+2);
    Stats->OnCharacterLevelUp.Broadcast(3);
    TestEqual(TEXT("Replayed level event does not duplicate points"),Skills->GetSkillPoints(),OldPoints+2);
    auto* Effect=NewObject<UGameplayEffect>(); Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
    auto& Mod=Effect->Modifiers.AddDefaulted_GetRef(); Mod.Attribute=UProjectDoctrineAttributeSet::GetWillpowerAttribute();
    Mod.ModifierOp=EGameplayModOp::Additive; Mod.ModifierMagnitude=FScalableFloat(5.f);
    const auto Handle=ASC->ApplyGameplayEffectToSelf(Effect,1.f,ASC->MakeEffectContext());
    TestTrue(TEXT("Equipment effect applied"),Handle.IsValid());
    TestEqual(TEXT("Equipment does not become learned rank"),Practice->GetLearnedRank(TEXT("Willpower")),1.f);
    auto* Doctrine=NewObject<UProjectInnerDoctrineComponent>(Owner);
    Owner->AddInstanceComponent(Doctrine); Doctrine->RegisterComponent();
    const bool WasEnabled=GetDefault<UProjectPracticeSettings>()->bEnabled;
    GetMutableDefault<UProjectPracticeSettings>()->bEnabled=true;
    TestEqual(TEXT("Doctrine milestones read learned base"),Doctrine->GetAttributeLevel(EProjectDoctrineAttribute::Willpower),1);
    TestEqual(TEXT("Doctrine numerical effects include equipment"),Doctrine->GetEffectiveAttributeLevel(EProjectDoctrineAttribute::Willpower),6);
    ASC->RemoveActiveGameplayEffect(Handle);
    TestEqual(TEXT("Doctrine effective rank drops after unequip"),Doctrine->GetEffectiveAttributeLevel(EProjectDoctrineAttribute::Willpower),1);
    GetMutableDefault<UProjectPracticeSettings>()->bEnabled=WasEnabled;
    TestEqual(TEXT("Unequip keeps learning"),Practice->GetLearnedRank(TEXT("Willpower")),1.f);
    TArray<uint8> Bytes;
    { FMemoryWriter W(Bytes); FObjectAndNameAsStringProxyArchive A(W,false); A.ArIsSaveGame=true; Practice->Serialize(A); }
    Practice->PracticeState.Reset(); Practice->RewardedThroughLevel=1;
    { FMemoryReader R(Bytes); FObjectAndNameAsStringProxyArchive A(R,true); A.ArIsSaveGame=true; Practice->Serialize(A); }
    TestTrue(TEXT("Practice fractions survive serialization"),FMath::Abs(Practice->GetPracticeProgress(TEXT("Willpower"))-Fraction)<1.e-9);
    TestEqual(TEXT("Level reward marker survives serialization"),Practice->RewardedThroughLevel,3);
    TestFalse(TEXT("Invalid reward rejected"),Practice->GrantExperience(-1));
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeTransferTest, "NoShellForWinter.Practice.TransferKeepsRespawnVitals", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeTransferTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* GameInstance=NewObject<UGameInstance>(); World->SetGameInstance(GameInstance);
    auto* Owner=World->SpawnActor<APawn>();
    auto* Controller=World->SpawnActor<APlayerController>(); Controller->Possess(Owner);
    Owner->SetPlayerState(World->SpawnActor<APlayerState>());
    TestTrue(TEXT("Fixture is a player pawn"),Owner->IsPlayerControlled());
    auto* ASC=NewObject<UAbilitySystemComponent>(Owner);
    auto* Stats=NewObject<UACFGASStatisticsComponent>(Owner);
    auto* Skills=NewObject<UACFSkillTreeComponent>(Owner);
    auto* Practice=NewObject<UProjectPracticeProgressionComponent>(Owner);
    auto* Training=NewObject<UProjectPracticeComponent>(Owner);
    for(UActorComponent* C : TArray<UActorComponent*>{ASC,Stats,Skills,Practice,Training}) { Owner->AddInstanceComponent(C); C->RegisterComponent(); }
    ASC->InitAbilityActorInfo(Owner,Owner);
    ASC->AddAttributeSetSubobject(NewObject<UACFPrimaryAttributeSet>(Owner));
    ASC->AddAttributeSetSubobject(NewObject<UACFStatisticsSet>(Owner));
    auto* Curve=NewObject<UCurveFloat>(); Curve->FloatCurve.AddKey(1,100); Curve->FloatCurve.AddKey(255,100);
    Stats->SetLevelingType(ELevelingType::EAssignPerksManually); Stats->SetExpForNextLevelCurve(Curve); Stats->ForceSetLevel(1); Stats->InitializeAttributeSet();
    Practice->RefreshBindings();
    Practice->Practice(TEXT("Willpower"),2,100);
    Practice->GrantExperience(250);
    const auto Health=FGameplayAttribute(FindFProperty<FProperty>(UACFStatisticsSet::StaticClass(),TEXT("Health")));
    ASC->SetNumericAttributeBase(Health,0.f);
    Stats->OnComponentSaved(); // Simulate even a stale native health archive on the pawn.
    auto* State=World->SpawnActor<AProjectProgressionState>(); State->Capture(Owner);
    TestTrue(TEXT("Snapshot contains character identity"),State->Snapshot.CharacterId.IsValid());
    ASC->SetNumericAttributeBase(Health,75.f);
    ASC->SetNumericAttributeBase(UProjectDoctrineAttributeSet::GetWillpowerAttribute(),0.f);
    const bool WasEnabled=GetDefault<UProjectPracticeSettings>()->bEnabled;
    GetMutableDefault<UProjectPracticeSettings>()->bEnabled=true;
    const bool Restored=State->Restore(Owner);
    GetMutableDefault<UProjectPracticeSettings>()->bEnabled=WasEnabled;
    TestTrue(TEXT("Progression snapshot restored"),Restored);
    TestEqual(TEXT("Respawn health is not replaced by dead pawn health"),ASC->GetNumericAttributeBase(Health),75.f);
    TestEqual(TEXT("Learned rank survives pawn transfer"),Practice->GetLearnedRank(TEXT("Willpower")),1.f);
    TestEqual(TEXT("Native level survives pawn transfer"),Stats->GetCurrentLevel(),3);
    TestEqual(TEXT("Only native skill balance survives transfer"),Skills->GetSkillPoints(),4);
    TestEqual(TEXT("Perks stay zero"),Stats->GetAvailablePerks(),0);
    State->DiscardForNewCharacter();
    State->Capture(Owner);
    State->FreezeForTravel();
    TestFalse(TEXT("Departing character cannot repopulate a new-game snapshot"),State->Snapshot.CharacterId.IsValid());
    World->DestroyWorld(false);
    return true;
}
#endif
