#include "ProjectCompatibilityInspectionLibrary.h"
#include "Graph/AQSQuest.h"
#include "Graph/AQSObjectiveNode.h"
#include "AQSQuestObjective.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Progression/ProjectAttributeSynergies.h"
#include "ACFAbilitySet.h"
#include "ACFGameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "Locomotion/ProjectSimulatedSustainedAction.h"
#include "AGSAction.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "Actions/ACFHitAction.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

bool UProjectCompatibilityInspectionLibrary::ConfigureOwnedZombieHitAssets(UAnimBlueprint* Animation,UACFAbilitySet* Abilities,UAnimMontage* LightMontage,FName StartBone,int32 BlendDepth)
{
    const FString Owned=TEXT("/Game/_Game/Characters/");
    if (!Animation || !Abilities || !LightMontage || !Animation->GetPathName().StartsWith(Owned) ||
        !Abilities->GetPathName().StartsWith(Owned) || !LightMontage->GetPathName().StartsWith(Owned) ||
        !Animation->TargetSkeleton || !Animation->TargetSkeleton->ContainsSlotName(TEXT("UpperBody")) ||
        Animation->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(StartBone)==INDEX_NONE || BlendDepth<1 || BlendDepth>5) return false;
    UEdGraph* Graph=nullptr;UAnimGraphNode_Root* Root=nullptr;UAnimGraphNode_LayeredBoneBlend* Layer=nullptr;
    for (UEdGraph* G:Animation->FunctionGraphs) if (G && G->GetFName()==TEXT("AnimGraph")) Graph=G;
    if (!Graph) return false;
    for (UEdGraphNode* N:Graph->Nodes)
    {
        if (auto* R=Cast<UAnimGraphNode_Root>(N)) Root=R;
        if (auto* L=Cast<UAnimGraphNode_LayeredBoneBlend>(N);L && L->NodeComment==TEXT("EF.Zombie.LightHit")) Layer=L;
    }
    if (!Root) return false;
    Graph->Modify();Animation->Modify();
    if (!Layer)
    {
        auto* Input=Root->FindPin(TEXT("Result"));
        if (!Input || Input->LinkedTo.Num()!=1) return false;
        auto* Original=Input->LinkedTo[0];
        auto Add=[&](UClass* Class,int32 X,int32 Y)->UEdGraphNode*
        {
            auto* N=NewObject<UEdGraphNode>(Graph,Class,NAME_None,RF_Transactional);
            Graph->AddNode(N,false,false);N->CreateNewGuid();N->PostPlacedNewNode();N->NodePosX=X;N->NodePosY=Y;return N;
        };
        auto* Save=CastChecked<UAnimGraphNode_SaveCachedPose>(Add(UAnimGraphNode_SaveCachedPose::StaticClass(),-500,100));
        Save->CacheName=TEXT("EF_ZombieFullBody");Save->Node.CachePoseName=FName(*Save->CacheName);Save->AllocateDefaultPins();
        auto* Base=CastChecked<UAnimGraphNode_UseCachedPose>(Add(UAnimGraphNode_UseCachedPose::StaticClass(),-350,-180));
        Base->SaveCachedPoseNode=Save;Base->AllocateDefaultPins();
        auto* OverlayBase=CastChecked<UAnimGraphNode_UseCachedPose>(Add(UAnimGraphNode_UseCachedPose::StaticClass(),-650,-380));
        OverlayBase->SaveCachedPoseNode=Save;OverlayBase->AllocateDefaultPins();
        auto* Slot=CastChecked<UAnimGraphNode_Slot>(Add(UAnimGraphNode_Slot::StaticClass(),-350,-380));
        Slot->Node.SlotName=TEXT("UpperBody");Slot->AllocateDefaultPins();
        Layer=CastChecked<UAnimGraphNode_LayeredBoneBlend>(Add(UAnimGraphNode_LayeredBoneBlend::StaticClass(),0,-180));
        Layer->NodeComment=TEXT("EF.Zombie.LightHit");Layer->Node.AddPose();Layer->AllocateDefaultPins();
        auto Output=[](UEdGraphNode* N)->UEdGraphPin*
        {for (auto* P:N->Pins) if (P->Direction==EGPD_Output && P->PinType.PinCategory==TEXT("struct")) return P;return nullptr;};
        const auto* Schema=Graph->GetSchema();
        Schema->BreakPinLinks(*Input,true);
        if (!Schema->TryCreateConnection(Original,Save->FindPin(TEXT("Pose"))) ||
            !Schema->TryCreateConnection(Output(Base),Layer->FindPin(TEXT("BasePose"))) ||
            !Schema->TryCreateConnection(Output(OverlayBase),Slot->FindPin(TEXT("Source"))) ||
            !Schema->TryCreateConnection(Output(Slot),Layer->FindPin(TEXT("BlendPoses_0"))) ||
            !Schema->TryCreateConnection(Output(Layer),Input)) return false;
    }
    Layer->Node.LayerSetup.SetNum(1);
    Layer->Node.LayerSetup[0].BranchFilters.Reset();
    FBranchFilter Filter;Filter.BoneName=StartBone;Filter.BlendDepth=BlendDepth;
    Layer->Node.LayerSetup[0].BranchFilters.Add(Filter);
    Layer->Node.bMeshSpaceRotationBlend=true;
    Layer->Node.bBlendRootMotionBasedOnRootBone=true;
    const FGameplayTag Light=FGameplayTag::RequestGameplayTag(TEXT("Actions.HitLight"));
    auto* Row=Abilities->ActionAbilities.FindByPredicate([&](const auto& R){return R.TriggeringTag==Light;});
    if (!Row)
    {
        const auto* Original=Abilities->ActionAbilities.FindByPredicate([](const auto& R){return Cast<UACFHitAction>(R.Action)!=nullptr;});
        if (!Original || !Original->Action) return false;
        FActionAbilityConfig New;
        New.TriggeringTag=Light;New.AbilityLevel=Original->AbilityLevel;
        New.Action=DuplicateObject<UACFHitAction>(CastChecked<UACFHitAction>(Original->Action),Abilities,TEXT("EF_LightHit"));
        Abilities->ActionAbilities.Add(New);Row=&Abilities->ActionAbilities.Last();
    }
    auto* LightAction=Cast<UACFHitAction>(Row->Action);if (!LightAction) return false;
    auto Config=LightAction->GetActionConfig();Config.bStopBehavioralThree=false;
    LightAction->SetActionConfig(Config);LightAction->SetAnimMontage(LightMontage);
    if (LightMontage->SlotAnimTracks.Num()!=1 || LightMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1) return false;
    LightMontage->SlotAnimTracks[0].SlotName=TEXT("UpperBody");
    auto& Segment=LightMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
    Segment.AnimStartTime=0.f;Segment.AnimEndTime=.65f;Segment.AnimPlayRate=1.15f;Segment.LoopingCount=1;
    LightMontage->BlendIn.SetBlendTime(.08f);LightMontage->BlendOut.SetBlendTime(.12f);
    LightMontage->SetCompositeLength(LightMontage->CalculateSequenceLength());
    LightMontage->CompositeSections.Reset();
    FCompositeSection Section;Section.SectionName=TEXT("Front");Section.Link(LightMontage,0.f);
    LightMontage->CompositeSections.Add(Section);
    // This short flinch uses one authored direction; avoid missing side/back sections.
    if (auto* Map=FindFProperty<FMapProperty>(LightAction->GetClass(),TEXT("HitDirectionToMontageSectionMap")))
    {
        FScriptMapHelper Values(Map,Map->ContainerPtrToValuePtr<void>(LightAction));
        if (auto* Value=CastField<FNameProperty>(Map->ValueProp))
            for (int32 I=0;I<Values.GetMaxIndex();++I) if (Values.IsValidIndex(I)) Value->SetPropertyValue(Values.GetValuePtr(I),TEXT("Front"));
    }
    LightMontage->MarkPackageDirty();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Animation);
    FKismetEditorUtilities::CompileBlueprint(Animation);
    Abilities->MarkPackageDirty();Animation->MarkPackageDirty();
    return Animation->Status!=BS_Error;
}

bool UProjectCompatibilityInspectionLibrary::IsNavigationFixtureReady(AActor* Context, AActor* NavigationData, FVector Location)
{
    if (!IsValid(Context) || !Context->GetWorld() || Context->GetWorld()->WorldType != EWorldType::PIE) return false;
    const auto* Data = Cast<ANavigationData>(NavigationData);
    auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context->GetWorld());
    FNavLocation Projected;
    return Data && Data->GetWorld() == Context->GetWorld() && Navigation &&
        Navigation->ProjectPointToNavigation(Location, Projected, FVector(100,100,200), Data);
}

bool UProjectCompatibilityInspectionLibrary::RemapTransientQuestTargets(UAQSQuest* Quest,AActor* Guy,AActor* Vendor,AActor* Spline)
{
    const auto* Controller=Quest ? Quest->GetTypedOuter<APlayerController>() : nullptr;
    if (!Controller || !Controller->GetWorld() || Controller->GetWorld()->WorldType!=EWorldType::PIE ||
        !IsValid(Guy) || !IsValid(Vendor) || Guy->GetWorld()!=Controller->GetWorld() || Vendor->GetWorld()!=Controller->GetWorld() ||
        (Spline && (!IsValid(Spline) || Spline->GetWorld()!=Controller->GetWorld()))) return false;
    // Validate every field before changing the transient duplicate.
    for (auto* Node:Quest->GetAllNodes()) if (auto* ObjectiveNode=Cast<UAQSObjectiveNode>(Node))
    {
        auto* Objective=ObjectiveNode->GetQuestObjective();
        if (!Objective) return false;
        auto* References=FindFProperty<FArrayProperty>(Objective->GetClass(),TEXT("ReferencedActors"));
        if (!References || !CastField<FSoftObjectProperty>(References->Inner)) return false;
        if (Spline)
            if (auto* Path=FindFProperty<FObjectPropertyBase>(Objective->GetClass(),TEXT("TargetSpline")); Path && !Spline->IsA(Path->PropertyClass)) return false;
    }
    for (auto* Node:Quest->GetAllNodes()) if (auto* ObjectiveNode=Cast<UAQSObjectiveNode>(Node))
    {
        auto* Objective=ObjectiveNode->GetQuestObjective();
        if (!Objective) return false;
        if (Spline)
        {
            if (auto* Path=FindFProperty<FObjectPropertyBase>(Objective->GetClass(),TEXT("TargetSpline")))
            {
                if (!Spline->IsA(Path->PropertyClass)) return false;
                Path->SetObjectPropertyValue_InContainer(Objective,Spline);
            }
        }
        auto* Property=FindFProperty<FArrayProperty>(Objective->GetClass(),TEXT("ReferencedActors"));
        if (!Property || !CastField<FSoftObjectProperty>(Property->Inner)) return false;
        auto* References=Property->ContainerPtrToValuePtr<TArray<TSoftObjectPtr<AActor>>>(Objective);
        for (auto& Reference:*References)
            Reference=Reference.ToSoftObjectPath().ToString().Contains(TEXT("ACF_Vendor")) ? Vendor : Guy;
    }
    for (auto* RawNode:Quest->GetAllNodes()) if (auto* Node=Cast<UAQSBaseNode>(RawNode))
    {
        TArray<UAGSAction*> Actions;
        for (auto* Action:Node->GetActivationActions()) Actions.Add(Action);
        for (auto* Action:Node->GetOnCompletionActions()) Actions.Add(Action);
        for (auto* Action:Actions) if (Action)
            if (auto* Speaker=FindFProperty<FSoftObjectProperty>(Action->GetClass(),TEXT("DialogueSpeaker")); Speaker && Guy->IsA(Speaker->PropertyClass))
                Speaker->SetObjectPropertyValue_InContainer(Action,Guy);
    }
    return true;
}

float UProjectCompatibilityInspectionLibrary::GetPhysicalActivityIntensity(AActor* Actor)
{
    return FProjectAttributeSynergies::PhysicalEffortIntensity(Actor);
}

bool UProjectCompatibilityInspectionLibrary::RepairPlayerLadderDownMontage()
{
    auto* Set=LoadObject<UACFAbilitySet>(nullptr,TEXT("/Game/FullSample/Blueprints/Actions/AbilitySets/AS_ACF_Player.AS_ACF_Player"));
    auto* Montage=LoadObject<UAnimMontage>(nullptr,TEXT("/Game/FullSample/Animations/LadderSystem/Animations/ACF_Climb-Down_Loop.ACF_Climb-Down_Loop"));
    if (!Set || !Montage) return false;
    for (auto& Config:Set->ActionAbilities)
    {
        if (Config.TriggeringTag.GetTagName()!=TEXT("Actions.LadderClimb.Loop")) continue;
        auto* Action=Config.Action.Get();
        if (!Action || Action->GetOutermost()!=Set->GetOutermost() ||
            Action->GetClass()->GetPathName()!=TEXT("/AscentCombatFramework/Blueprints/Abilities/ACF_ClimbLadderLoop_BP.ACF_ClimbLadderLoop_BP_C")) return false;
        auto* Down=FindFProperty<FObjectPropertyBase>(Action->GetClass(),TEXT("Down"));
        if (!Down || !Montage->IsA(Down->PropertyClass)) return false;
        UObject* Existing=Down->GetObjectPropertyValue_InContainer(Action);
        if (Existing) return Existing==Montage;
        Action->Modify();
        Down->SetObjectPropertyValue_InContainer(Action,Montage);
        Set->MarkPackageDirty();
        return true;
    }
    return false;
}

bool UProjectCompatibilityInspectionLibrary::InstallPlayerSimulatedBowCharge()
{
    auto* Set=LoadObject<UACFAbilitySet>(nullptr,TEXT("/Game/FullSample/Blueprints/Actions/AbilitySets/AS_ACF_NewMMBowActions.AS_ACF_NewMMBowActions"));
    auto* Class=LoadClass<UProjectSimulatedSustainedAction>(nullptr,TEXT("/Game/_Game/Blueprints/Compatibility/BP_ProjectChargedShoot.BP_ProjectChargedShoot_C"));
    if (!Set || !Class) return false;
    for (auto& Config:Set->ActionAbilities)
    {
        if (Config.TriggeringTag.GetTagName()!=TEXT("Actions.Shoot")) continue;
        auto* Old=Config.Action.Get();
        if (!Old || Old->GetOutermost()!=Set->GetOutermost()) return false;
        if (Old->GetClass()==Class) return true;
        if (Old->GetClass()->GetPathName()!=TEXT("/AscentCombatFramework/Blueprints/Actions/ACF_ChargedShootAction_BP.ACF_ChargedShootAction_BP_C")) return false;
        Set->Modify();
        auto* Replacement=NewObject<UProjectSimulatedSustainedAction>(Set,Class,NAME_None,RF_Transactional);
        UEngine::CopyPropertiesForUnrelatedObjects(Old,Replacement);
        Config.Action=Replacement;
        Set->MarkPackageDirty();
        return true;
    }
    return false;
}
