#include "Calysto/EFCalystoPopulationAnchor.h"

#include "Components/SceneComponent.h"

const FName AEFCalystoPopulationAnchor::PopulationAnchorTag(TEXT("EF.Calysto.PopulationAnchor"));

AEFCalystoPopulationAnchor::AEFCalystoPopulationAnchor()
{
	USceneComponent* TransformRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TransformRoot"));
	TransformRoot->PrimaryComponentTick.bCanEverTick = false;
	TransformRoot->PrimaryComponentTick.bStartWithTickEnabled = false;
	TransformRoot->SetCanEverAffectNavigation(false);
	SetRootComponent(TransformRoot);

	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorTickEnabled(false);

	SetActorEnableCollision(false);
	SetCanBeDamaged(false);
	SetActorHiddenInGame(true);
	SetReplicates(false);
	SetReplicateMovement(false);
	bNetLoadOnClient = false;
	bNetUseOwnerRelevancy = false;

	Tags.AddUnique(PopulationAnchorTag);
}
