#include "ProjectCompatibilityInspectionLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/SphereElem.h"

bool UProjectCompatibilityInspectionLibrary::RepairBloodDemoAdditiveBase()
{
    auto* BP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/RealisticBlood/Demo/Characters/Mannequins/Animations/ABP_Manny.ABP_Manny"));
    if (!BP) return false;
    UEdGraph* Graph = nullptr;
    for (UEdGraph* G : BP->FunctionGraphs) if (G && G->GetFName() == TEXT("AnimGraph")) Graph = G;
    if (!Graph) return false;
    auto Find = [Graph](FName Name) -> UEdGraphNode*
    { for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->GetFName() == Name) return Node; return nullptr; };
    auto* Missing = Cast<UAnimGraphNode_SequencePlayer>(Find(TEXT("AnimGraphNode_SequencePlayer_1")));
    auto* Rig = Find(TEXT("AnimGraphNode_ControlRig_1"));
    auto* Additive = Find(TEXT("AnimGraphNode_ApplyAdditive_1"));
    if (!Missing || !Rig || !Additive || Missing->Node.GetSequence()) return false;
    UEdGraphPin* RigOut = Rig->FindPin(TEXT("Pose"), EGPD_Output);
    UEdGraphPin* MissingOut = Missing->FindPin(TEXT("Pose"), EGPD_Output);
    UEdGraphPin* Base = Additive->FindPin(TEXT("Base"), EGPD_Input);
    if (!RigOut || !MissingOut || !Base || MissingOut->LinkedTo.Num()!=1 || MissingOut->LinkedTo[0]!=Base || RigOut->LinkedTo.IsEmpty()) return false;
    const auto PreviousConsumers = RigOut->LinkedTo;
    BP->Modify(); Graph->Modify(); Rig->Modify(); Additive->Modify(); Missing->Modify();
    FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> SaveCreator(*Graph);
    auto* Save = SaveCreator.CreateNode();
    Save->CacheName = TEXT("ProjectEvaluatedMainPose");
    Save->NodePosX = Rig->NodePosX + 240; Save->NodePosY = Rig->NodePosY + 300;
    SaveCreator.Finalize();
    FGraphNodeCreator<UAnimGraphNode_UseCachedPose> UseCreator(*Graph);
    auto* Use = UseCreator.CreateNode();
    Use->SaveCachedPoseNode = Save;
    Use->NodePosX = Missing->NodePosX; Use->NodePosY = Missing->NodePosY;
    UseCreator.Finalize();
    auto* SaveIn = Save->FindPin(TEXT("Pose"), EGPD_Input);
    auto* UseOut = Use->FindPin(TEXT("Pose"), EGPD_Output);
    if (!SaveIn || !UseOut) return false;
    RigOut->BreakAllPinLinks(); MissingOut->BreakAllPinLinks();
    RigOut->MakeLinkTo(SaveIn);
    for (auto* Consumer : PreviousConsumers) UseOut->MakeLinkTo(Consumer);
    UseOut->MakeLinkTo(Base);
    FBlueprintEditorUtils::RemoveNode(BP, Missing, true);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    return true;
}

FString UProjectCompatibilityInspectionLibrary::AuthorMorphPhysicsCandidate(USkeletalMesh* Mesh, UPhysicsAsset* Candidate)
{
    if (!Mesh || !Candidate || Candidate == Mesh->GetPhysicsAsset() ||
        !Candidate->GetPathName().StartsWith(TEXT("/Game/_Game/Characters/Physics/"))) return TEXT("ERROR: candidate must be an owned duplicate");
    const TArray<FName> Bones = {TEXT("l_pectoral"),TEXT("r_pectoral"),TEXT("LeftGlute"),TEXT("RightGlute"),TEXT("LeftThighFat"),TEXT("RightThighFat"),TEXT("Belly")};
    auto ConfigureProfile = [](UPhysicsConstraintTemplate* Constraint)
    {
        auto& C = Constraint->DefaultInstance;
        C.SetLinearXLimit(LCM_Limited,2.f); C.SetLinearYLimit(LCM_Limited,2.f); C.SetLinearZLimit(LCM_Limited,2.f);
        C.SetAngularSwing1Limit(ACM_Locked,0.f); C.SetAngularSwing2Limit(ACM_Locked,0.f); C.SetAngularTwistLimit(ACM_Locked,0.f);
        C.SetLinearPositionDrive(true,true,true);
        C.SetLinearVelocityDrive(true,true,true);
        C.SetLinearDriveParams(500.f,50.f,0.f);
        C.SetDisableCollision(true);
        // Serialize writes DefaultProfile, not the currently edited ProfileInstance.
        Constraint->SetDefaultProfile(C);
    };
    bool bAllAuthored = true;
    for (FName Bone : Bones)
        bAllAuthored &= Candidate->FindBodyIndex(Bone)!=INDEX_NONE && Candidate->FindConstraintIndex(Bone)!=INDEX_NONE;
    if (bAllAuthored)
    {
        Candidate->Modify();
        for (FName Bone : Bones)
        {
            auto* Constraint = Candidate->ConstraintSetup[Candidate->FindConstraintIndex(Bone)].Get();
            Constraint->Modify();
            ConfigureProfile(Constraint);
        }
        Candidate->MarkPackageDirty();
        return TEXT("Updated seven owned default profiles");
    }
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    TArray<FTransform> ComponentPose;
    for (int32 I=0; I<Ref.GetNum(); ++I)
    {
        const int32 Parent = Ref.GetParentIndex(I);
        ComponentPose.Add(Parent==INDEX_NONE ? Ref.GetRefBonePose()[I] : Ref.GetRefBonePose()[I]*ComponentPose[Parent]);
    }
    // Fail before mutation if any required branch or anchor is missing.
    TArray<int32> Parents;
    for (FName Bone : Bones)
    {
        const int32 Index=Ref.FindBoneIndex(Bone);
        if (Index==INDEX_NONE || Candidate->FindBodyIndex(Bone)!=INDEX_NONE || Candidate->FindConstraintIndex(Bone)!=INDEX_NONE) return TEXT("ERROR: missing bone or candidate already authored");
        int32 Parent=Ref.GetParentIndex(Index);
        while (Parent!=INDEX_NONE && Candidate->FindBodyIndex(Ref.GetBoneName(Parent))==INDEX_NONE) Parent=Ref.GetParentIndex(Parent);
        if (Parent==INDEX_NONE) return TEXT("ERROR: no existing ancestor body");
        Parents.Add(Parent);
    }
    Candidate->Modify();
    const int32 OriginalCount=Candidate->SkeletalBodySetups.Num();
    FString Report;
    for (int32 I=0; I<Bones.Num(); ++I)
    {
        const FName Bone=Bones[I], ParentBone=Ref.GetBoneName(Parents[I]);
        auto* Body=NewObject<USkeletalBodySetup>(Candidate,NAME_None,RF_Transactional);
        Body->BoneName=Bone;
        Body->PhysicsType=PhysType_Simulated;
        Body->bConsiderForBounds=false;
        Body->DefaultInstance.bEnableGravity=false;
        Body->DefaultInstance.LinearDamping=15.f;
        Body->DefaultInstance.AngularDamping=15.f;
        Body->DefaultInstance.bOverrideMass=true;
        Body->DefaultInstance.SetMassOverride(0.1f);
        FKSphereElem Sphere(2.f);
        Sphere.SetName(Bone);
        Body->AggGeom.SphereElems.Add(Sphere);
        Candidate->SkeletalBodySetups.Add(Body);
        auto* Constraint=NewObject<UPhysicsConstraintTemplate>(Candidate,NAME_None,RF_Transactional);
        auto& C=Constraint->DefaultInstance;
        C.JointName=Bone; C.ConstraintBone1=Bone; C.ConstraintBone2=ParentBone;
        C.SetRefFrame(EConstraintFrame::Frame1,FTransform::Identity);
        C.SetRefFrame(EConstraintFrame::Frame2,ComponentPose[Ref.FindBoneIndex(Bone)].GetRelativeTransform(ComponentPose[Parents[I]]));
        ConfigureProfile(Constraint);
        Candidate->ConstraintSetup.Add(Constraint);
        Report+=FString::Printf(TEXT("%s -> %s\n"),*Bone.ToString(),*ParentBone.ToString());
    }
    // Secondary soft bodies must not collide with the original ragdoll or each other.
    for (int32 I=OriginalCount; I<Candidate->SkeletalBodySetups.Num(); ++I)
        for (int32 J=0; J<I; ++J) Candidate->DisableCollision(I,J);
    Candidate->UpdateBodySetupIndexMap(); Candidate->UpdateBoundsBodiesArray();
    Candidate->PostEditChange(); Candidate->MarkPackageDirty();
    return Report;
}
