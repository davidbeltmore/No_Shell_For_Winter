#include "Misc/AutomationTest.h"
#include "Progression/ProjectPracticeMath.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Progression/ProjectAttributeSynergies.h"
#include "Progression/ProjectGameplayCueManager.h"
#include "AbilitySystemGlobals.h"
#include "GameplayCueNotify_Static.h"
#include "AssetRegistry/AssetData.h"
#include "Progression/ProjectQuestStartComponent.h"
#include "Graph/AQSQuest.h"
#include "Graph/AQSStartQuestNode.h"
#include "Graph/AQSObjectiveNode.h"
#include "AQSQuestObjective.h"
#include "AQSQuestTargetComponent.h"
#include "DirtyPawnComponent.h"
#include "DirtyPawnEffort.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPhysicalEffortTest, "NoShellForWinter.Practice.PhysicalEffort", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPhysicalEffortTest::RunTest(const FString& Parameters)
{
    float Effort=0.f;
    TestEqual(TEXT("Brief movement stays below sweat threshold"), FDirtyPawnEffort::Advance(Effort,1.f,2.f,10.f,20.f,.25f),0.f);
    TestTrue(TEXT("Effort warms gradually"),FMath::IsNearlyEqual(Effort,.2f));
    const float Before=Effort;
    TestEqual(TEXT("Pause adds no sweat"),FDirtyPawnEffort::Advance(Effort,1.f,0.f,10.f,20.f,.25f),0.f);
    TestEqual(TEXT("Pause freezes effort"),Effort,Before);
    float Reference=0.f;
    const float Whole=FDirtyPawnEffort::Advance(Reference,1.f,20.f,10.f,20.f,.25f);
    TestTrue(TEXT("Exact warmup integration"),FMath::IsNearlyEqual(Whole,13.75f));
    for (const float Speed : {1.f,2.f,4.f})
    {
        float Split=0.f, Sum=0.f;
        for (int32 Tick=0;Tick<static_cast<int32>(1200/Speed);++Tick)
            Sum+=FDirtyPawnEffort::Advance(Split,1.f,Speed/60.f,10.f,20.f,.25f);
        TestTrue(TEXT("Same simulation duration at x1/x2/x4"),FMath::IsNearlyEqual(Sum,Whole,.002f));
    }
    TestEqual(TEXT("Rest does not create sweat"),FDirtyPawnEffort::Advance(Reference,0.f,20.f,10.f,20.f,.25f),0.f);
    TestEqual(TEXT("Rest fully recovers effort"),Reference,0.f);
    TestEqual(TEXT("Quiet activity never builds effort"),FDirtyPawnEffort::Advance(Reference,0.f,3600.f,10.f,20.f,.25f),0.f);
    const auto* Settings=GetDefault<UProjectPracticeSettings>();
    for (const auto& Definition : Settings->Activities)
        TestEqual(*Definition.Id.ToString(),Definition.PhysicalEffortIntensity>0.f,Definition.Method==EProjectPracticeMethod::Physical);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeQuestTest, "NoShellForWinter.Practice.QuestTargetAvailability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeQuestTest::RunTest(const FString& Parameters)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* OtherWorld=UWorld::CreateWorld(EWorldType::Game,false);
    auto* Quest=NewObject<UAQSQuest>();
    auto* Start=NewObject<UAQSStartQuestNode>(Quest);
    auto* Node=NewObject<UAQSObjectiveNode>(Quest);
    auto* Objective=NewObject<UAQSQuestObjective>(Node);
    Quest->AllNodes={Start,Node}; Start->ChildrenNodes.Add(Node);
    FindFProperty<FObjectPropertyBase>(Node->GetClass(),TEXT("Objective"))->SetObjectPropertyValue_InContainer(Node,Objective);
    auto* References=FindFProperty<FArrayProperty>(Objective->GetClass(),TEXT("ReferencedActors"))->ContainerPtrToValuePtr<TArray<TSoftObjectPtr<AActor>>>(Objective);
    References->Add(TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/MissingQuestWorld.MissingQuestWorld:PersistentLevel.Target"))));
    TestFalse(TEXT("Unavailable map defers quest without loading it"),UProjectQuestStartComponent::AreInitialTargetsReady(Quest,World));
    auto* Target=World->SpawnActor<AActor>(); (*References)[0]=Target;
    TestFalse(TEXT("Actor alone is insufficient without its quest target component"),UProjectQuestStartComponent::AreInitialTargetsReady(Quest,World));
    auto* TargetComponent=NewObject<UAQSQuestTargetComponent>(Target); Target->AddInstanceComponent(TargetComponent); TargetComponent->RegisterComponent();
    TestTrue(TEXT("Available first objective can start normally"),UProjectQuestStartComponent::AreInitialTargetsReady(Quest,World));
    TestFalse(TEXT("Editor or other-world actors cannot satisfy runtime targets"),UProjectQuestStartComponent::AreInitialTargetsReady(Quest,OtherWorld));
    auto* Later=NewObject<UAQSObjectiveNode>(Quest); Node->ChildrenNodes.Add(Later); Quest->AllNodes.Add(Later);
    auto* LaterObjective=DuplicateObject(Objective,Later);
    FindFProperty<FObjectPropertyBase>(Later->GetClass(),TEXT("Objective"))->SetObjectPropertyValue_InContainer(Later,LaterObjective);
    auto* LaterReferences=FindFProperty<FArrayProperty>(LaterObjective->GetClass(),TEXT("ReferencedActors"))->ContainerPtrToValuePtr<TArray<TSoftObjectPtr<AActor>>>(LaterObjective);
    (*LaterReferences)[0]=TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/LaterWorld.LaterWorld:PersistentLevel.Target")));
    TestTrue(TEXT("Later objectives may legitimately require another map"),UProjectQuestStartComponent::AreInitialTargetsReady(Quest,World));
    World->DestroyWorld(false); OtherWorld->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeCueTest, "NoShellForWinter.Practice.CanonicalHitCue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeCueTest::RunTest(const FString& Parameters)
{
    const FString Root=TEXT("/Game/FullSample/Blueprints/GAS/Cues/");
    auto* Hit=LoadClass<UGameplayCueNotify_Static>(nullptr,*(Root+TEXT("ACF_DefaultHit_GC.ACF_DefaultHit_GC_C")));
    auto* Reaction=LoadClass<UGameplayCueNotify_Static>(nullptr,*(Root+TEXT("ACF_DefaultReaction_GC.ACF_DefaultReaction_GC_C")));
    if (!TestNotNull(TEXT("Canonical hit cue remains loadable"),Hit) || !TestNotNull(TEXT("Legacy reaction asset remains loadable"),Reaction)) return false;
    TestEqual(TEXT("Same native feedback implementation"),Hit->GetSuperClass(),Reaction->GetSuperClass());
    for (TFieldIterator<FProperty> P(Hit->GetSuperClass()); P; ++P)
        TestTrue(*FString::Printf(TEXT("Feedback defaults identical: %s"),*P->GetName()),P->Identical_InContainer(Hit->GetDefaultObject(),Reaction->GetDefaultObject()));
    for (FName Event : {FName(TEXT("OnExecute")),FName(TEXT("OnActive")),FName(TEXT("WhileActive")),FName(TEXT("OnRemove")),FName(TEXT("K2_HandleGameplayCue"))})
        TestEqual(*FString::Printf(TEXT("Both assets route %s identically"),*Event.ToString()),Hit->FindFunctionByName(Event),Reaction->FindFunctionByName(Event));
    auto* Manager=UAbilitySystemGlobals::Get().GetGameplayCueManager();
    TestTrue(TEXT("Production GAS uses project cue registration"),Manager->IsA<UProjectGameplayCueManager>());
    TestTrue(TEXT("Canonical hit is registered"),Manager->ShouldLoadGameplayCueAssetData(FAssetData(Hit->ClassGeneratedBy)));
    TestFalse(TEXT("Identical alias does not compete for the same tag"),Manager->ShouldLoadGameplayCueAssetData(FAssetData(Reaction->ClassGeneratedBy)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeSynergiesTest, "NoShellForWinter.Practice.AttributeSynergies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectPracticeSynergiesTest::RunTest(const FString& Parameters)
{
    auto* Settings=GetMutableDefault<UProjectPracticeSettings>();
    TGuardValue<bool> Enabled(Settings->bEnabled, true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AActor* Owner=World->SpawnActor<AActor>();
    auto* ASC=NewObject<UAbilitySystemComponent>(Owner);
    Owner->AddInstanceComponent(ASC); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner,Owner);
    ASC->AddAttributeSetSubobject(NewObject<UACFPrimaryAttributeSet>(Owner));
    TestEqual(TEXT("No rank means no pain discount"),FProjectAttributeSynergies::PainGainMultiplier(Owner),1.f);
    TestEqual(TEXT("No rank means no sweat discount"),FProjectAttributeSynergies::SweatGainMultiplier(Owner),1.f);
    const auto Constitution=UProjectPracticeProgressionComponent::ResolveAttribute(TEXT("Constitution"));
    const auto Endurance=UProjectPracticeProgressionComponent::ResolveAttribute(TEXT("Endurance"));
    const auto Intelligence=UProjectPracticeProgressionComponent::ResolveAttribute(TEXT("Intelligence"));
    ASC->SetNumericAttributeBase(Constitution,Settings->MaximumAttributeRank);
    ASC->SetNumericAttributeBase(Endurance,Settings->MaximumAttributeRank);
    auto* Dirty=NewObject<UDirtyPawnComponent>(Owner);
    TestTrue(TEXT("Portable sweat callback resolves effective endurance at use time"),FMath::IsNearlyEqual(Dirty->GetActivitySweatGainMultiplier(),1.f-Settings->EnduranceMaximumSweatReduction));
    Dirty->SetSweatPoints(10.f); Dirty->AddSweatPoints(5.f);
    TestEqual(TEXT("Direct sweat changes retain their absolute meaning"),Dirty->SweatPoints,15.f);
    TestTrue(TEXT("Constitution bounds pain intake reduction"),FMath::IsNearlyEqual(FProjectAttributeSynergies::PainGainMultiplier(Owner),1.f-Settings->ConstitutionMaximumPainReduction));
    TestTrue(TEXT("Endurance bounds activity sweat reduction"),FMath::IsNearlyEqual(FProjectAttributeSynergies::SweatGainMultiplier(Owner),1.f-Settings->EnduranceMaximumSweatReduction));
    ASC->SetNumericAttributeBase(Intelligence,0.f);
    auto* Effect=NewObject<UGameplayEffect>(); Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
    auto& Mod=Effect->Modifiers.AddDefaulted_GetRef(); Mod.Attribute=Intelligence;
    Mod.ModifierOp=EGameplayModOp::Additive; Mod.ModifierMagnitude=FScalableFloat(10000.f);
    const auto Handle=ASC->ApplyGameplayEffectToSelf(Effect,1.f,ASC->MakeEffectContext());
    TestTrue(TEXT("Equipment intelligence contributes a bounded lock bonus"),FProjectAttributeSynergies::IntelligenceLockpickBonus(Owner)>0 && FProjectAttributeSynergies::IntelligenceLockpickBonus(Owner)<=Settings->IntelligenceMaximumLockpickBonus);
    TestEqual(TEXT("Equipment did not become learned intelligence"),ASC->GetNumericAttributeBase(Intelligence),0.f);
    ASC->RemoveActiveGameplayEffect(Handle);
    TestEqual(TEXT("Unequip removes the lock bonus"),FProjectAttributeSynergies::IntelligenceLockpickBonus(Owner),0);
    Settings->bEnabled=false;
    TestEqual(TEXT("Disabled progression leaves legacy pain intact"),FProjectAttributeSynergies::PainGainMultiplier(Owner),1.f);
    TestEqual(TEXT("Disabled progression leaves legacy sweat intact"),FProjectAttributeSynergies::SweatGainMultiplier(Owner),1.f);
    TestEqual(TEXT("Disabled progression also leaves portable sweat intake intact"),Dirty->GetActivitySweatGainMultiplier(),1.f);
    TestEqual(TEXT("Malformed reduction fails to neutral"),FProjectAttributeSynergies::BoundedReduction(100,100,std::numeric_limits<float>::quiet_NaN()),1.f);
    World->DestroyWorld(false);
    return true;
}

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
