#include "DungeonCurse/RoomGameplayMarker.h"

#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

ARoomGameplayMarker::ARoomGameplayMarker()
{
	PrimaryActorTick.bCanEverTick = true;

	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("RoomBounds"));
	SetRootComponent(BoxComponent);
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoxComponent->SetHiddenInGame(true);

	BoxExtent = FVector(600.0f, 600.0f, 180.0f);
	RoomType = EGeneratedRoomType::Unknown;
	bAllowCurses = true;
	CurseChanceOverride = -1.0f;
	DebugColor = FColor::Green;
	Priority = 0;
	bDebugDraw = false;
	bAllowEnemyLevelCurse = true;
	bAllowLightCurse = true;
	bAllowInnerStateCurse = true;

	UpdateBoxExtent();
}

void ARoomGameplayMarker::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateBoxExtent();
}

void ARoomGameplayMarker::BeginPlay()
{
	Super::BeginPlay();
	UpdateBoxExtent();
}

void ARoomGameplayMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDebugDraw)
	{
		DrawMarkerDebug();
	}
}

void ARoomGameplayMarker::UpdateBoxExtent()
{
	BoxExtent = BoxExtent.ComponentMax(FVector(1.0f, 1.0f, 1.0f));
	if (BoxComponent)
	{
		BoxComponent->SetBoxExtent(BoxExtent);
	}
}

void ARoomGameplayMarker::DrawMarkerDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugBox(
		World,
		GetActorLocation(),
		BoxExtent,
		GetActorQuat(),
		DebugColor,
		false,
		0.0f,
		0,
		3.0f);
}
