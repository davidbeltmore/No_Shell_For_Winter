#include "Calysto/EFCalystoSpecialEventProbe.h"

#include "Components/SceneComponent.h"

const FName AEFCalystoSpecialEventProbe::SpecialEventProbeTag(
	TEXT("EF.Calysto.Automation.SpecialEventProbe"));

AEFCalystoSpecialEventProbe::AEFCalystoSpecialEventProbe()
{
	USceneComponent* TransformRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TransformRoot"));
	TransformRoot->PrimaryComponentTick.bCanEverTick = false;
	TransformRoot->PrimaryComponentTick.bStartWithTickEnabled = false;
	TransformRoot->SetCanEverAffectNavigation(false);
	SetRootComponent(TransformRoot);

	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorTickEnabled(false);

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetCanBeDamaged(false);
	SetReplicates(false);
	SetReplicateMovement(false);
	bNetLoadOnClient = false;
	bNetUseOwnerRelevancy = false;

	Tags.AddUnique(SpecialEventProbeTag);
}
