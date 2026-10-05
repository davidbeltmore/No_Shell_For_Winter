#include "Survival/ProjectInnerStateExposureVolume.h"
#include "Survival/ProjectInnerStateComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"

AProjectInnerStateExposureVolume::AProjectInnerStateExposureVolume()
{
    PrimaryActorTick.bCanEverTick=false;
    Bounds=CreateDefaultSubobject<UBoxComponent>(TEXT("ExposureBounds")); SetRootComponent(Bounds);
    Bounds->SetBoxExtent(FVector(200)); Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Bounds->SetCollisionResponseToAllChannels(ECR_Ignore); Bounds->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Bounds->SetGenerateOverlapEvents(true);
    OnActorBeginOverlap.AddDynamic(this,&ThisClass::Enter); OnActorEndOverlap.AddDynamic(this,&ThisClass::Leave);
}
void AProjectInnerStateExposureVolume::Enter(AActor*,AActor* Other)
{
    if (!HasAuthority() || !Cast<APawn>(Other) || Occupants.Contains(Other)) return;
    if (auto* Inner=Other->FindComponentByClass<UProjectInnerStateComponent>())
    {
        Occupants.Add(Other);
        const FName Source(*GetPathName());
        Inner->SetBreathingSource(Source,BreathableAir);
        Inner->SetContaminationSource(Source,Contamination);
        Inner->SetComposureContext(Source,ComposureTarget);
        if (EntryPoisonPoints>0) Inner->AddPoison(EntryPoisonPoints,PoisonSpeed,Toxin,FGuid::NewGuid());
    }
}
void AProjectInnerStateExposureVolume::Leave(AActor*,AActor* Other)
{
    Occupants.Remove(Other);
    if (IsValid(Other)) if (auto* Inner=Other->FindComponentByClass<UProjectInnerStateComponent>())
    {
        const FName Source(*GetPathName());
        Inner->ClearBreathingSource(Source); Inner->ClearContaminationSource(Source); Inner->ClearComposureContext(Source);
    }
}
void AProjectInnerStateExposureVolume::EndPlay(EEndPlayReason::Type Reason)
{
    const auto Previous=Occupants;
    for (const auto& Actor:Previous) Leave(this,Actor.Get());
    Super::EndPlay(Reason);
}
