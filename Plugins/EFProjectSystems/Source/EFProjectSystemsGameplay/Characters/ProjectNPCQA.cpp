#include "Characters/ProjectNPCRoster.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "Characters/ProjectEnemyVisualVariationSubsystem.h"
#include "EFBodyPresetCatalog.h"
#include "EFCharacterCustomizationComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ACFCharacterMovementComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Animation/ACFAnimInstance.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING
namespace
{
struct FProjectNPCQA : TSharedFromThis<FProjectNPCQA>
{
	TWeakObjectPtr<UWorld> World;
	TArray<TWeakObjectPtr<ACharacter>> Pawns;
	TArray<FTransform> Poses;
	TArray<FCharacterCustomizationState> Appearances;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AActor> OldView;
	FString Events, Directory = FPaths::ProjectSavedDir()/TEXT("NPCDAZ/Runtime");
	double Start = FPlatformTime::Seconds(), Due=0;
	int32 Stage=0;
	bool bQuit=false;
	void Record(const FString& Text) { Events+=Text+TEXT("\n"); UE_LOG(LogTemp,Display,TEXT("[NPCDAZ] %s"),*Text); }
	bool Finish(bool bPass,const FString& Reason)
	{
		if (World.IsValid())
		{
			if (auto* PC=UGameplayStatics::GetPlayerController(World.Get(),0); PC && OldView.IsValid()) PC->SetViewTarget(OldView.Get());
			if (auto* State=AProjectNPCAppearanceState::Ensure(World.Get()))
				for (const auto& Pawn : Pawns) if (Pawn.IsValid()) State->Records.Remove(AProjectNPCAppearanceState::IdentityFor(Pawn.Get()));
		}
		for (auto& Pawn:Pawns) if (Pawn.IsValid()) Pawn->Destroy();
		if (Camera.IsValid()) Camera->Destroy();
		Record(FString::Printf(TEXT("status=%s reason=%s elapsed_seconds=%.3f"),bPass?TEXT("PASS"):TEXT("FAIL"),*Reason,FPlatformTime::Seconds()-Start));
		IFileManager::Get().MakeDirectory(*Directory,true);
		FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("runtime.txt")));
		if (bQuit) FPlatformMisc::RequestExit(false);
		return false;
	}
	bool Tick(float)
	{
		const double Now=FPlatformTime::Seconds();
		if (Now-Start>90) return Finish(false,TEXT("90-second watchdog"));
		UWorld* W=World.Get();
		APlayerController* PC=W?UGameplayStatics::GetPlayerController(W,0):nullptr;
		APawn* Player=PC?PC->GetPawn():nullptr;
		if (!Player) return true;
		if (Now<Due) return true;
		if (Stage==0)
		{
			auto* Roster=LoadObject<UProjectNPCRoster>(nullptr,TEXT("/Game/_Game/Characters/Data/DA_NPCRoster.DA_NPCRoster"));
			if (!Roster || Roster->Archetypes.Num()<20) return Finish(false,TEXT("Roster unavailable/incomplete"));
			const TSet<FName> Sample={TEXT("SkillGuy"),TEXT("Vendor"),TEXT("Routine"),TEXT("Zombie"),TEXT("NPCAI"),TEXT("MeleeEnemy")};
			const FVector Origin=Player->GetActorLocation()+FVector(1200,0,25);
			for (const auto& Row:Roster->Archetypes)
			{
				if (!Sample.Contains(Row.Id)) continue;
				for (const auto& Class:{Row.Male,Row.Female})
				{
					FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transient;
					Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					const int32 I=Pawns.Num();
					const FVector Location=Origin+FVector((I/4)*230,(I%4)*180,0);
					const double T=FPlatformTime::Seconds();
					auto* P=W->SpawnActor<ACharacter>(Class.LoadSynchronous(),Location,FRotator(0,180,0),Params);
					if (!P) return Finish(false,TEXT("Spawn failed: ")+Row.Id.ToString());
					Pawns.Add(P);
					P->SetActorEnableCollision(false);
					P->GetCharacterMovement()->DisableMovement();
					P->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					if (auto* AI=Cast<AAIController>(P->GetController()); AI && AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Bounded NPC QA"));
					Record(FString::Printf(TEXT("spawn=%s class=%s synchronous_ms=%.2f"),*Row.Id.ToString(),*Class.ToString(),1000*(FPlatformTime::Seconds()-T)));
				}
			}
			if (Pawns.Num()!=12) return Finish(false,TEXT("Expected 12 representative NPCs"));
			OldView=PC->GetViewTarget();
			Camera=W->SpawnActor<ACameraActor>();
			const FVector Focus=Origin+FVector(230,270,-10), Eye=Focus+FVector(-1100,0,300);
			Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
			Stage=1; Due=Now+5; return true;
		}
		if (Stage==1)
		{
			for (auto& Weak:Pawns)
			{
				auto* P=Weak.Get(); auto* C=P?P->FindComponentByClass<UEFCharacterCustomizationComponent>():nullptr;
				if (!C || C->GetBodyPresetId().IsNone()) return Finish(false,TEXT("Missing generated DAZ appearance"));
				auto* Mesh=P->GetMesh();
				if (auto* Damage=P->FindComponentByClass<UACFDamageHandlerComponent>(); Damage && !Damage->GetIsAlive()) return Finish(false,TEXT("NPC died during spawn initialization"));
				if (Mesh->IsSimulatingPhysics(TEXT("pelvis")) || Mesh->GetSocketLocation(TEXT("head")).Z-Mesh->GetSocketLocation(TEXT("foot_l")).Z<75)
					return Finish(false,TEXT("NPC is not upright: ")+P->GetClass()->GetName());
				const auto* Body=UEFBodyPresetCatalog::Get()->Find(C->GetBodyPresetId());
				if (!Body || Mesh->GetSkeletalMeshAsset()!=Body->Mesh.Get()) return Finish(false,TEXT("Initializer replaced DAZ body"));
				if (Body->Gender==ECharacterCreationGender::Male && (Mesh->GetMorphTarget(TEXT("DK_Flacid 03"))<.99f || FMath::Abs(Mesh->GetMorphTarget(TEXT("DK_Erection")))>.001f)) return Finish(false,TEXT("Male neutral state missing"));
				if (!Mesh->GetAnimInstance()) return Finish(false,TEXT("Missing animation instance"));
				if (auto* Movement=Cast<UACFCharacterMovementComponent>(P->GetCharacterMovement()); Movement && Movement->GetACFAnimInstance()!=Mesh->GetAnimInstance()) return Finish(false,TEXT("Stale ACF animation cache"));
				Poses.Add(Mesh->GetSocketTransform(TEXT("hand_l"),RTS_Component));
				Appearances.Add(C->CaptureCurrentState());
				Record(FString::Printf(TEXT("generated=%s body=%s morph_count=%d neutral=true"),*P->GetClass()->GetName(),*C->GetBodyPresetId().ToString(),Appearances.Last().MorphValues.Num()));
			}
			Stage=2; Due=Now+1; return true;
		}
		if (Stage==2)
		{
			for (int32 I=0;I<Pawns.Num();++I)
			{
				auto* P=Pawns[I].Get(); if (!P) return Finish(false,TEXT("NPC lost"));
				const auto Pose=P->GetMesh()->GetSocketTransform(TEXT("hand_l"),RTS_Component);
				const double Distance=FVector::Distance(Pose.GetLocation(),Poses[I].GetLocation());
				const double Angle=Pose.GetRotation().AngularDistance(Poses[I].GetRotation());
				Record(FString::Printf(TEXT("animation=%s hand_delta_cm=%.5f angle=%.6f"),*P->GetClass()->GetName(),Distance,Angle));
				if (Distance<.002 && Angle<.00001) return Finish(false,TEXT("Frozen animation: ")+P->GetClass()->GetName());
			}
			FScreenshotRequest::RequestScreenshot(Directory/TEXT("Roster.png"),false,false);
			// Verify the exact legacy migration contract without touching the user's saves.
			FProjectNPCAppearanceRecord Legacy; Legacy.Appearance=Appearances[0];
			for (auto& M:Legacy.Appearance.MorphValues) if (M.MorphName==TEXT("DK_Flacid 03")) M.Value=0;
			Legacy.MigrateNeutralDefaults();
			const auto* Neutral=Legacy.Appearance.MorphValues.FindByPredicate([](const auto& M){return M.MorphName==TEXT("DK_Flacid 03");});
			if (Legacy.GeneratorVersion!=2 || !Neutral || Neutral->Value!=1) return Finish(false,TEXT("Legacy neutral migration failed"));
			Record(TEXT("legacy_migration=PASS"));
			Stage=3; Due=Now+2; return true;
		}
		if (Stage==3)
		{
			for (int32 I=0;I<Pawns.Num();++I)
				Pawns[I]->FindComponentByClass<UEFCharacterCustomizationComponent>()->SelectBodyPreset(I%2?FName(TEXT("Male.02")):FName(TEXT("Female.01")),true);
			W->GetSubsystem<UProjectEnemyVisualVariationSubsystem>()->RequestAppearanceRestore();
			Stage=4; Due=Now+3; return true;
		}
		if (Stage==4)
		{
			for (int32 I=0;I<Pawns.Num();++I)
			{
				auto* C=Pawns[I]->FindComponentByClass<UEFCharacterCustomizationComponent>();
				const auto State=C->CaptureCurrentState();
				if (State.BodyPresetId!=Appearances[I].BodyPresetId || State.MorphValues.Num()!=Appearances[I].MorphValues.Num()) return Finish(false,TEXT("Appearance restore mismatch"));
				for (int32 J=0;J<State.MorphValues.Num();++J) if (!FMath::IsNearlyEqual(State.MorphValues[J].Value,Appearances[I].MorphValues[J].Value)) return Finish(false,TEXT("Morph restore mismatch"));
			}
			Record(TEXT("restore_12=PASS"));
			return Finish(true,TEXT("12 NPCs: DAZ, neutral, animation and restore"));
		}
		return true;
	}
};
TWeakPtr<FProjectNPCQA> Active;
FAutoConsoleCommandWithWorldAndArgs Command(TEXT("Project.NPCDAZ.QA"),TEXT("90-second representative NPC QA; optional quit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
	{
		if (!World || Active.IsValid()) return;
		auto Test=MakeShared<FProjectNPCQA>(); Test->World=World; Test->bQuit=Args.Contains(TEXT("quit")); Active=Test;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Dt){return Test->Tick(Dt);}));
	}));
}
#endif
