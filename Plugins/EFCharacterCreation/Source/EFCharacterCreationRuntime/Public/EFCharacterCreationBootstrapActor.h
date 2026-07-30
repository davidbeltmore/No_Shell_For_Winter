#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "EFCharacterCreationBootstrapActor.generated.h"

/**
 * Minimal compatibility actor for legacy character creation bootstrap blueprints.
 * New flows should enter character creation through UEFCharacterCreationSubsystem directly.
 */
UCLASS()
class EFCHARACTERCREATIONRUNTIME_API AEFCharacterCreationBootstrapActor : public AActor
{
	GENERATED_BODY()

public:
	AEFCharacterCreationBootstrapActor();

protected:
	virtual void BeginPlay() override;

private:
	void TryStartCharacterCreation();

private:
	UPROPERTY(EditAnywhere, Category = "Character Creation")
	bool bOnlyRunInTestingMap = true;

	UPROPERTY(EditAnywhere, Category = "Character Creation")
	int32 MaxRetryCount = 90;

	int32 RetryCount = 0;
};
