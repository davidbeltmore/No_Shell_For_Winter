#include "EFBodyPresetCatalog.h"
#include "Animation/AnimInstance.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFCharacterCreationSubsystem.h"
#include "EFMorphPhysicsConstraintComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "UI/EFCharacterCreationRootWidget.h"
#include "UI/EFBodyPresetCheckBox.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace
{
// Development-only bounded test, usable in both PIE and a cooked executable.
// No actor or asset is saved; the original appearance and camera are restored.
struct FBodyPresetRuntimeQA
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UEFCharacterCustomizationComponent> Custom;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AActor> OldView;
	FCharacterCustomizationState Original;
	FString Directory = FPaths::ProjectSavedDir() / TEXT("BodyPresetsQA/Runtime");
	FString Events;
	double Start = FPlatformTime::Seconds(), Due = 0;
	int32 Stage = -1;
	bool bQuit = false;
	FVector MoveOrigin;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FTransform> PoseSamples;
	bool bAwaitingMotion = false;
	TArray<FName> Cases = {TEXT("Male.01"),TEXT("Male.02"),TEXT("Female.01"),TEXT("Male.02")};
	void Record(const FString& Text) { Events += Text + TEXT("\n"); UE_LOG(LogTemp, Display, TEXT("[BodyPresetQA] %s"), *Text); }
	bool Finish(bool bPass, const FString& Reason)
	{
		if (World.IsValid() && World->GetGameInstance())
			if (auto* Creator = World->GetGameInstance()->GetSubsystem<UEFCharacterCreationSubsystem>())
				if (Creator->IsCharacterCreationActive()) Creator->ExitCharacterCreationMode(false);
		if (Custom.IsValid()) Custom->ApplyState(Original);
		if (Controller.IsValid() && OldView.IsValid()) Controller->SetViewTarget(OldView.Get());
		if (Camera.IsValid()) Camera->Destroy();
		Record(FString::Printf(TEXT("status=%s reason=%s elapsed_seconds=%.3f"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Reason,FPlatformTime::Seconds()-Start));
		IFileManager::Get().MakeDirectory(*Directory,true);
		FFileHelper::SaveStringToFile(Events, *(Directory / TEXT("runtime.txt")));
		if (bQuit) FPlatformMisc::RequestExit(false);
		return false;
	}
	bool Tick(float Delta)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now-Start > 90) return Finish(false,TEXT("90-second watchdog"));
		UWorld* W = World.Get();
		APawn* Pawn = W ? UGameplayStatics::GetPlayerPawn(W,0) : nullptr;
		if (!Pawn) return true;
		if (Stage == -1)
		{
			auto* C = Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>();
			auto* PC = UGameplayStatics::GetPlayerController(W,0);
			if (!C || !PC || C->GetBodyPresetId().IsNone()) return true;
			Custom=C; Controller=PC; Original=C->CaptureCurrentState(); OldView=PC->GetViewTarget();
			FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
			Camera=W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),FTransform::Identity,Params);
			if (!Camera.IsValid()) return Finish(false,TEXT("Camera fixture unavailable"));
			PC->SetViewTarget(Camera.Get());
			IFileManager::Get().MakeDirectory(*Directory,true);
			Stage=0; Due=Now+2; Record(TEXT("player_ready=true"));
		}
		if (Stage == 9 && Now < Due)
		{
			Pawn->AddMovementInput(Pawn->GetActorForwardVector(),.5f,true);
			return true;
		}
		if (Now < Due) return true;
		if (!Custom.IsValid() || !Controller.IsValid()) return Finish(false,TEXT("Player replaced during test"));
		if (Stage < 8)
		{
			const FName Preset = Cases[Stage/2];
			if ((Stage % 2) == 0)
			{
				const double SwitchStart=FPlatformTime::Seconds();
				if (!Custom->SelectBodyPreset(Preset)) return Finish(false,TEXT("Preset unavailable: ")+Preset.ToString());
				Record(FString::Printf(TEXT("select=%s switch_ms=%.3f"),*Preset.ToString(),(FPlatformTime::Seconds()-SwitchStart)*1000));
				Due=Now+2;
			}
			else
			{
				if (Custom->GetBodyPresetId()!=Preset) return Finish(false,TEXT("Selection changed unexpectedly"));
				TArray<USkeletalMeshComponent*> Meshes; Pawn->GetComponents(Meshes);
				int32 Bodies=0, PhysicsOwners=0;
				for (auto* Mesh : Meshes)
				{
					const auto* Definition=UEFBodyPresetCatalog::Get()->Find(Preset);
					if (Mesh->GetSkeletalMeshAsset()!=Definition->Mesh.Get()) continue;
					++Bodies;
					if (!Mesh->GetAnimInstance()) return Finish(false,TEXT("Body lost animation instance"));
					if (auto* Physics=Pawn->FindComponentByClass<UEFMorphPhysicsConstraintComponent>(); Physics && Mesh->GetFName()==Physics->GetResolvedTargetMeshName())
					{
						++PhysicsOwners;
						if (!Definition->MorphPhysicsAsset.IsNull() && Mesh->GetPhysicsAsset()!=Definition->MorphPhysicsAsset.Get())
							return Finish(false,TEXT("Preset physics profile was not applied"));
						for (const auto& Group : Physics->GetConstraintGroups())
							for (FName Name : Group.ConstraintNames)
							{
								const FConstraintInstance* Constraint=Mesh->FindConstraintInstance(Name);
								if (!Constraint || !FMath::IsFinite(Constraint->GetLinearLimit()))
									return Finish(false,TEXT("Missing/invalid morph physics constraint: ")+Name.ToString());
								if (!Mesh->IsSimulatingPhysics(Name)) return Finish(false,TEXT("Morph physics body is not simulating: ")+Name.ToString());
							}
					}
					const FTransform Hand=Mesh->GetSocketTransform(TEXT("hand_l"),RTS_Component);
					if (!bAwaitingMotion) PoseSamples.Add(Mesh,Hand);
					else
					{
						const FTransform* Before=PoseSamples.Find(Mesh);
						if (!Before || (FVector::Dist(Before->GetLocation(),Hand.GetLocation())<0.02 &&
							Before->GetRotation().AngularDistance(Hand.GetRotation())<0.0001))
							return Finish(false,TEXT("Body pose remains frozen across samples"));
					}
					for (FName Foot : {FName(TEXT("foot_l")),FName(TEXT("foot_r"))})
					{
						const float Height=Mesh->GetSocketTransform(Foot,RTS_Component).GetLocation().Z;
						if (Height < -15 || Height > 30) return Finish(false,TEXT("Standing foot outside ground-height envelope"));
					}
					// The retarget follower can inherit the ACF property without owning a
					// locomotion layer. Only CharacterMesh0 is the animation source.
					if (const ACharacter* Character=Cast<ACharacter>(Pawn); Character && Mesh==Character->GetMesh())
						if (const FObjectPropertyBase* Moveset=FindFProperty<FObjectPropertyBase>(Mesh->GetAnimInstance()->GetClass(),TEXT("currentMovesetInstance")))
							if (!Moveset->GetObjectPropertyValue_InContainer(Mesh->GetAnimInstance())) return Finish(false,TEXT("ACF locomotion layer missing on ")+Mesh->GetName());
					for (FName Bone : {FName(TEXT("head")),FName(TEXT("hand_l")),FName(TEXT("foot_r"))})
					{
						const FVector Position=Mesh->GetSocketLocation(Bone);
						if (Mesh->GetBoneIndex(Bone)==INDEX_NONE || Position.ContainsNaN() || FVector::Dist(Position,Pawn->GetActorLocation())>1000)
							return Finish(false,TEXT("Invalid animated body pose"));
					}
				}
				if (Bodies<1) return Finish(false,TEXT("No active body component"));
				if (Pawn->FindComponentByClass<UEFMorphPhysicsConstraintComponent>() && PhysicsOwners!=1)
					return Finish(false,TEXT("Morph physics owner was not resolved uniquely"));
				if (!bAwaitingMotion) { bAwaitingMotion=true; Due=Now+0.75; return true; }
				bAwaitingMotion=false; PoseSamples.Reset();
				const FVector Location=Pawn->GetActorLocation();
				const FVector View=Location+Pawn->GetActorForwardVector()*240+FVector(0,0,15);
				Camera->SetActorLocationAndRotation(View,(Location-View).Rotation());
				FScreenshotRequest::RequestScreenshot(Directory / (Preset.ToString()+TEXT(".png")),true,false);
				Record(FString::Printf(TEXT("pose=%s components=%d valid=true frame_delta=%.5f"),*Preset.ToString(),Bodies,Delta));
				Due=Now+1;
			}
			++Stage;
			return true;
		}
		if (Stage==8) { MoveOrigin=Pawn->GetActorLocation(); Stage=9; Due=Now+1.5; return true; }
		if (Stage==9)
		{
			const float Distance=FVector::Dist2D(MoveOrigin,Pawn->GetActorLocation());
			Record(FString::Printf(TEXT("Male2_movement_cm=%.3f"),Distance));
			if (Distance<10) return Finish(false,TEXT("Movement fixture did not move"));
			auto* Creator=W->GetGameInstance()->GetSubsystem<UEFCharacterCreationSubsystem>();
			if (!Creator || !Creator->OpenCharacterCreationForAutomation()) return Finish(false,TEXT("Creator failed to open"));
			auto* Widget=Creator->GetActiveRootWidgetForAutomation();
			if (!Widget) return Finish(false,TEXT("Creator widget unavailable"));
			Widget->SelectPresentationCategory(TEXT("Info"));
			Stage=10; Due=Now+2; return true;
		}
		if (Stage==10)
		{
			auto* Creator=W->GetGameInstance()->GetSubsystem<UEFCharacterCreationSubsystem>();
			auto* Widget=Creator?Creator->GetActiveRootWidgetForAutomation():nullptr;
            auto Choices=[Widget]() {
                TArray<UWidget*> All; Widget->WidgetTree->GetAllWidgets(All);
                TArray<UEFBodyPresetCheckBox*> Result;
                for (UWidget* Item : All) if (auto* Choice=Cast<UEFBodyPresetCheckBox>(Item)) Result.Add(Choice);
                return Result;
            };
            auto Click=[&Choices](FName Id) {
                for (auto* Choice : Choices()) if (Choice->PresetId==Id) { Choice->SetIsChecked(true); Choice->OnCheckStateChanged.Broadcast(true); return true; }
                return false;
            };
            auto Selected=[&Choices](FName Id,int32 Count) {
                auto Rows=Choices(); int32 Checked=0; bool Found=false;
                for (auto* Row : Rows) if (Row->IsChecked()) { ++Checked; Found=Row->PresetId==Id; }
                return Rows.Num()==Count && Checked==1 && Found;
            };
            if (!Widget || !Selected(TEXT("Male.02"),2)) return Finish(false,TEXT("Info does not expose two male checkboxes with one selected"));
            if (!Click(TEXT("Male.01")) || Custom->GetBodyPresetId()!=TEXT("Male.01") || !Selected(TEXT("Male.01"),2))
                return Finish(false,TEXT("Preset 1 checkbox did not select atomically"));
            for (auto* Choice : Choices()) if (Choice->IsChecked()) { Choice->SetIsChecked(false); Choice->OnCheckStateChanged.Broadcast(false); }
            if (!Selected(TEXT("Male.01"),2)) return Finish(false,TEXT("Selected preset can be unchecked"));
            Widget->ProcessEvent(Widget->FindFunction(TEXT("HandleGenderFemaleClicked")),nullptr);
            if (!Selected(TEXT("Female.01"),1)) return Finish(false,TEXT("Female choices not filtered"));
            Widget->ProcessEvent(Widget->FindFunction(TEXT("HandleGenderMaleClicked")),nullptr);
            if (!Selected(TEXT("Male.01"),2)) return Finish(false,TEXT("Male selection not remembered"));
            if (!Click(TEXT("Male.02")) || !Selected(TEXT("Male.02"),2) || Custom->GetBodyPresetId()!=TEXT("Male.02"))
                return Finish(false,TEXT("Preset 2 checkbox failed"));
            Record(TEXT("info_checkboxes=true male_choices=2 female_choices=1 exclusive=true gender_memory=true"));
            FScreenshotRequest::RequestScreenshot(Directory / TEXT("CreatorMale2.png"),true,false);
            Stage=11; Due=Now+2; return true;
        }
        if (Stage==11)
        {
            auto* Creator=W->GetGameInstance()->GetSubsystem<UEFCharacterCreationSubsystem>();
            auto* Widget=Creator->GetActiveRootWidgetForAutomation();
            Widget->SelectBodyPresetFromUI(TEXT("Male.01"));
            Widget->ProcessEvent(Widget->FindFunction(TEXT("HandleBackClicked")),nullptr);
            if (Creator->IsCharacterCreationActive() || Custom->GetBodyPresetId()!=TEXT("Male.02")) return Finish(false,TEXT("Cancel did not restore session body"));
            if (!Creator->OpenCharacterCreationForAutomation()) return Finish(false,TEXT("Reopen after cancel failed"));
            Widget=Creator->GetActiveRootWidgetForAutomation();
            Widget->SelectBodyPresetFromUI(TEXT("Male.01"));
            Widget->ProcessEvent(Widget->FindFunction(TEXT("HandleStartGameClicked")),nullptr);
            if (Creator->IsCharacterCreationActive() || Custom->GetBodyPresetId()!=TEXT("Male.01")) return Finish(false,TEXT("Apply did not retain body"));
            if (!Creator->OpenCharacterCreationForAutomation() || Custom->GetBodyPresetId()!=TEXT("Male.01")) return Finish(false,TEXT("Applied body not retained on reopen"));
            Record(TEXT("creator_cancel_restores=true apply_reopen_retains=true"));
            Stage=12; Due=Now+1; return true;
        }
		return Finish(true,TEXT("switches, animated poses, movement and creator open; visual review required"));
	}
};
TWeakPtr<FBodyPresetRuntimeQA> ActiveQA;
FAutoConsoleCommandWithWorldAndArgs BodyPresetQA(TEXT("Project.BodyPresets.QA"),TEXT("Bounded body preset runtime/visual QA. Optional argument: quit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
	{
		if (!World || ActiveQA.IsValid()) return;
		auto State=MakeShared<FBodyPresetRuntimeQA>(); State->World=World; State->bQuit=Args.Contains(TEXT("quit")); ActiveQA=State;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float Delta){return State->Tick(Delta);}));
	}));
}
#endif
