#include "Characters/ProjectNPCOutfitComponent.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "Characters/ProjectNPCOutfitSubsystem.h"
#include "Characters/ProjectNPCRoster.h"
#include "Components/ACFEquipmentComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/GCObject.h"

#if !UE_BUILD_SHIPPING
namespace
{
// Transient definitions exercise the real selector and inventory, without
// introducing QA assets into the authoring catalog or changing roster data.
struct FOutfitSelectionQA : FGCObject, TSharedFromThis<FOutfitSelectionQA>
{
    TWeakObjectPtr<UWorld> World;
    UProjectNPCOutfit* Full=nullptr;
    UProjectNPCOutfit* Alternate=nullptr;
    UProjectNPCOutfit* Zero=nullptr;
    TArray<TWeakObjectPtr<ACharacter>> Pawns;
    TArray<FString> Identities;
    TArray<FName> FirstSelection;
    TArray<FGuid> SavedGuids;
    TArray<TSharedPtr<FJsonValue>> Samples;
    FString Name;
    int32 Count=16,Seed=731,Round=0,Stage=0;
    double Started=FPlatformTime::Seconds(),Timeout=120;
    virtual FString GetReferencerName() const override { return TEXT("FOutfitSelectionQA"); }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    { Collector.AddReferencedObject(Full);Collector.AddReferencedObject(Alternate);Collector.AddReferencedObject(Zero); }
    void Destroy()
    { for (auto P:Pawns) if (P.IsValid()) { if(P->GetController()) P->GetController()->Destroy(); P->Destroy(); } Pawns.Reset(); }
    bool Finish(bool Passed,const FString& Reason)
    {
        Destroy();
        if (auto* W=World.Get()) if (auto* State=AProjectNPCAppearanceState::Ensure(W))
            for(const auto& Id:Identities) State->Records.Remove(Id);
        auto Report=MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("status"),Passed?TEXT("PASS"):TEXT("FAIL"));Report->SetStringField(TEXT("reason"),Reason);
        Report->SetNumberField(TEXT("elapsed_seconds"),FPlatformTime::Seconds()-Started);Report->SetArrayField(TEXT("samples"),Samples);
        Report->SetNumberField(TEXT("weighted_count"),Count);Report->SetNumberField(TEXT("seed"),Seed);
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("NPCOutfits/Native")/Name;
        IFileManager::Get().MakeDirectory(*Directory,true);
        FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Report,Writer);
        FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("runtime.json")));
        return false;
    }
    bool Tick(float)
    {
        auto* W=World.Get();if(!W || FPlatformTime::Seconds()-Started>Timeout) return Finish(false,TEXT("Selection fixture timeout/world closed"));
        if(Stage==0)
        {
            Full=LoadObject<UProjectNPCOutfit>(nullptr,TEXT("/Game/_Game/Characters/Data/Outfits/DA_FullRag.DA_FullRag"));
            if(!Full || Full->Items.Num()!=3) return Finish(false,TEXT("FullRag definition missing"));
            Alternate=DuplicateObject<UProjectNPCOutfit>(Full,GetTransientPackage());Alternate->Id=TEXT("QA.RagLower");Alternate->Items.RemoveAt(0);
            Zero=DuplicateObject<UProjectNPCOutfit>(Full,GetTransientPackage());Zero->Id=TEXT("QA.ZeroWeight");
            Stage=1;
        }
        if(Stage==1)
        {
            auto* Player=UGameplayStatics::GetPlayerPawn(W,0);if(!Player) return true;
            auto* Roster=LoadObject<UProjectNPCRoster>(nullptr,TEXT("/Game/_Game/Characters/Data/DA_NPCRoster.DA_NPCRoster"));
            const auto* Row=Roster?Roster->Archetypes.FindByPredicate([](const auto& R){return R.Id==TEXT("SkillGuy");}):nullptr;
            auto* Class=Row?Row->Male.LoadSynchronous():nullptr;if(!Class) return Finish(false,TEXT("SkillGuy class missing"));
            for(int32 I=0;I<=Count;++I)
            {
                const FTransform Transform(FRotator(0,180,0),Player->GetActorLocation()+FVector(1800,(I-Count/2)*200,25));
                auto* P=W->SpawnActorDeferred<ACharacter>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                if(!P) return Finish(false,TEXT("Spawn failed"));
                P->Tags.Add(FName(*FString::Printf(TEXT("EF.AppearanceId.OutfitSelection.%d.%d"),Seed,I)));
                auto* Outfit=NewObject<UProjectNPCOutfitComponent>(P,TEXT("ProjectNPCOutfit"));
                auto Add=[&](UProjectNPCOutfit* Definition,float Weight){FProjectNPCOutfitChoice Choice;Choice.Outfit=Definition;Choice.Weight=Weight;Outfit->OverrideChoices.Add(Choice);};
                if(I==Count) Add(Round==0?Alternate:Full,1);
                else {Add(Full,1);Add(Alternate,3);Add(Zero,0);}
                P->AddInstanceComponent(Outfit);Outfit->RegisterComponent();
                UGameplayStatics::FinishSpawningActor(P,Transform);Pawns.Add(P);
                P->SetActorEnableCollision(false);P->GetCharacterMovement()->DisableMovement();
                if(Round==0) Identities.Add(AProjectNPCAppearanceState::IdentityFor(P));
            }
            Stage=2;return true;
        }
        if(Stage==2)
        {
            for(auto P:Pawns)
            {
                auto* Outfit=P.IsValid()?P->FindComponentByClass<UProjectNPCOutfitComponent>():nullptr;
                if(!Outfit || Outfit->State==EProjectNPCOutfitState::Failed) return Finish(false,Outfit?Outfit->Failure:TEXT("Fixture lost"));
                if(Outfit->State!=EProjectNPCOutfitState::Ready) return true;
            }
            for(int32 I=0;I<Pawns.Num();++I)
            {
                auto* P=Pawns[I].Get();auto* Outfit=P->FindComponentByClass<UProjectNPCOutfitComponent>();
                auto* Equipment=P->FindComponentByClass<UACFEquipmentComponent>();
                if(Outfit->SelectedId!=Full->Id && Outfit->SelectedId!=Alternate->Id) return Finish(false,TEXT("Zero-weight or unrelated roster choice selected"));
                const auto* Definition=Outfit->SelectedId==Full->Id?Full:Alternate;
                for(const auto& Item:Definition->Items)
                {
                    int32 Found=0;
                    for(const auto& Native:Equipment->GetInventory()) if(FSoftObjectPath(Native.ItemClass.Get())==Item.Item.ToSoftObjectPath())
                    { if(Native.Count!=1 || !Native.bIsEquipped) return Finish(false,TEXT("Chosen outfit inventory mismatch")); ++Found; }
                    if(Found!=1) return Finish(false,TEXT("Missing/duplicate chosen item"));
                }
                TArray<FGuid> Guids;for(const auto& Item:Equipment->GetInventory()) Guids.Add(Item.GetItemGuid());
                Guids.Sort([](const FGuid& A,const FGuid& B){return A.ToString()<B.ToString();});
                if(Round==0) {FirstSelection.Add(Outfit->SelectedId);if(I==Count) SavedGuids=Guids;}
                else if(Outfit->SelectedId!=FirstSelection[I]) return Finish(false,TEXT("Same seed changed selection"));
                if(I==Count && (Outfit->SelectedId!=Alternate->Id || (Round==1 && Guids!=SavedGuids))) return Finish(false,TEXT("Fixed override/saved precedence failed"));
                auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("round"),Round);Sample->SetNumberField(TEXT("index"),I);
                Sample->SetStringField(TEXT("selected"),Outfit->SelectedId.ToString());Sample->SetNumberField(TEXT("inventory_entries"),Guids.Num());
                Samples.Add(MakeShared<FJsonValueObject>(Sample));
            }
            if(Round==0)
            {
                if(!FirstSelection.Contains(Full->Id)) return Finish(false,TEXT("Seed sample lacks the positive lower-weight choice"));
                Destroy();auto* State=AProjectNPCAppearanceState::Ensure(W);
                for(int32 I=0;I<Count;++I) State->Records.Remove(Identities[I]); // fresh generation, not a saved-result comparison
                Round=1;Stage=1;return true;
            }
            return Finish(true,TEXT("Weighted fresh-generation repeat, zero weight, fixed instance override and saved inventory precedence"));
        }
        return true;
    }
};
TWeakPtr<FOutfitSelectionQA> ActiveSelection;
FAutoConsoleCommandWithWorldAndArgs SelectionCommand(TEXT("Project.NPCOutfits.SelectionQA"),TEXT("Bounded transient outfit selection/override QA."),
FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
{
    if(!W || ActiveSelection.IsValid()) return;
    auto Test=MakeShared<FOutfitSelectionQA>();Test->World=W;const FString Options=FString::Join(Args,TEXT(" "));
    FParse::Value(*Options,TEXT("name="),Test->Name);FParse::Value(*Options,TEXT("seed="),Test->Seed);FParse::Value(*Options,TEXT("count="),Test->Count);
    Test->Count=FMath::Clamp(Test->Count,2,32);if(Test->Name.IsEmpty() || Test->Name.Contains(TEXT("/")) || Test->Name.Contains(TEXT("\\"))) return;
    ActiveSelection=Test;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Dt){return Test->Tick(Dt);}));
}));
}
#endif
