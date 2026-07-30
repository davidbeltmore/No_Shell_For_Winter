#include "EFCharacterCreationBootstrapActor.h"

#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCharacterCreationBootstrap, Log, All);

AEFCharacterCreationBootstrapActor::AEFCharacterCreationBootstrapActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorHiddenInGame(true);
}

void AEFCharacterCreationBootstrapActor::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimerForNextTick(this, &AEFCharacterCreationBootstrapActor::TryStartCharacterCreation);
}

void AEFCharacterCreationBootstrapActor::TryStartCharacterCreation()
{
	UE_LOG(
		LogEFCharacterCreationBootstrap,
		Warning,
		TEXT("EFCharacterCreationBootstrapActor is retained only for legacy compatibility. Use UEFCharacterCreationSubsystem directly."));
}
