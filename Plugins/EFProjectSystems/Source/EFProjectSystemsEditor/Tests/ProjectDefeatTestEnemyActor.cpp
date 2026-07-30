#include "Tests/ProjectDefeatTestEnemyActor.h"

#include "Combat/ProjectCombatAttributeComponent.h"

AProjectDefeatTestMeleeMaleEnemy::AProjectDefeatTestMeleeMaleEnemy()
{
	PrimaryActorTick.bCanEverTick = false;
	CombatAttributeComponent = CreateDefaultSubobject<UProjectCombatAttributeComponent>(TEXT("CombatAttributeComponent"));
}
