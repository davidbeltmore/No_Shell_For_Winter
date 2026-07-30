#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ProjectDefeatTestEnemyActor.generated.h"

class UProjectCombatAttributeComponent;

UCLASS()
class EFPROJECTSYSTEMSEDITOR_API AProjectDefeatTestMeleeMaleEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	AProjectDefeatTestMeleeMaleEnemy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tests")
	TObjectPtr<UProjectCombatAttributeComponent> CombatAttributeComponent;
};
