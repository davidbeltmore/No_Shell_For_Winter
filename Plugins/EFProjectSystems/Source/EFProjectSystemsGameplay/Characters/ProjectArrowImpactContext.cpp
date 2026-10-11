#include "Characters/ProjectArrowImpactContext.h"
#include "ACMCollisionManagerComponent.h"
#include "Game/ACFDamageType.h"
#include "ItemActors/ACFProjectileActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool UProjectArrowImpactContext::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type==EWorldType::Game || Type==EWorldType::PIE; }
void UProjectArrowImpactContext::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SpawnHandle=GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this,&ThisClass::ActorSpawned));
}
void UProjectArrowImpactContext::Deinitialize()
{
    GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);Pending.Empty();Super::Deinitialize();
}
void UProjectArrowImpactContext::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    for (TActorIterator<AACFProjectileActor> It(&World);It;++It) ActorSpawned(*It);
}
void UProjectArrowImpactContext::ActorSpawned(AActor* Actor)
{
    if (!Actor || !Actor->HasAuthority() || !Actor->IsA<AACFProjectileActor>() || Actor->FindComponentByClass<UProjectArrowImpactObserver>()) return;
    bool Arrow=false;
    for (auto* C=Actor->GetClass();C;C=C->GetSuperClass())
        if (C->GetPathName()==TEXT("/Game/FullSample/Blueprints/ItemActors/ACF_ArrowActor_BP.ACF_ArrowActor_BP_C")) {Arrow=true;break;}
    if (!Arrow) return;
    auto* Observer=NewObject<UProjectArrowImpactObserver>(Actor);
    Actor->AddInstanceComponent(Observer);Observer->RegisterComponent();
}
void UProjectArrowImpactContext::Mark(const FHitResult& Hit,AActor* Dealer)
{
    auto* Receiver=Hit.GetActor();
    if (!Receiver || !Dealer || !Receiver->ActorHasTag(TEXT("EF.NPC.Zombie"))) return;
    // Normally consumed synchronously. Remove skipped/blocked traces on the next
    // mark so stale entries cannot accumulate or classify a later attack.
    for (auto It=Pending.CreateIterator();It;++It)
        if (!It.Key().IsValid() || It.Value().Frame!=GFrameCounter) It.RemoveCurrent();
    // ACF ConstructDamageReceived uses GetBestHitInfo().Location, rather than
    // the sphere sweep's surface ImpactPoint. Match that exact native contract.
    Pending.Add(Receiver,{Dealer,Hit.Location,Hit.BoneName,GFrameCounter});
}
bool UProjectArrowImpactContext::Consume(const FACFDamageEvent& Event)
{
    FPending Context;
    if (!Pending.RemoveAndCopyValue(Event.DamageReceiver,Context)) return false;
    return Context.Frame==GFrameCounter && Context.Dealer.Get()==Event.DamageDealer &&
        Context.Bone==Event.hitResult.BoneName && Context.Impact.Equals(Event.hitResult.ImpactPoint,.01);
}
UProjectArrowImpactObserver::UProjectArrowImpactObserver() {PrimaryComponentTick.bCanEverTick=false;}
void UProjectArrowImpactObserver::BeginPlay()
{
    Super::BeginPlay();
    auto* Arrow=Cast<AACFProjectileActor>(GetOwner());
    Collision=Arrow?Arrow->GetCollisionComp():nullptr;
    if (Collision.IsValid()) Collision->OnCollisionDetected.AddUniqueDynamic(this,&ThisClass::CollisionDetected);
}
void UProjectArrowImpactObserver::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Collision.IsValid()) Collision->OnCollisionDetected.RemoveDynamic(this,&ThisClass::CollisionDetected);
    Super::EndPlay(Reason);
}
void UProjectArrowImpactObserver::CollisionDetected(const FHitResult& Hit)
{
    if (auto* Context=GetWorld()->GetSubsystem<UProjectArrowImpactContext>())
        Context->Mark(Hit,Collision.IsValid()?Collision->GetActorOwner():nullptr);
}
