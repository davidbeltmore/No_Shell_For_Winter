#include "Calysto/EFCalystoNavigationBoundsVolume.h"

#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"
#include "HAL/PlatformTime.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

AEFCalystoNavigationBoundsVolume::AEFCalystoNavigationBoundsVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GetBrushComponent()->SetMobility(EComponentMobility::Movable);
	GetBrushComponent()->SetCanEverAffectNavigation(false);
	GeneratedGeometryBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("GeneratedGeometryBounds"));
	GeneratedGeometryBounds->SetupAttachment(GetRootComponent());
	GeneratedGeometryBounds->SetMobility(EComponentMobility::Movable);
	GeneratedGeometryBounds->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	GeneratedGeometryBounds->SetGenerateOverlapEvents(false);
	GeneratedGeometryBounds->SetCanEverAffectNavigation(false);
	GeneratedGeometryBounds->SetBoxExtent(FVector::OneVector, false);
	SetActorHiddenInGame(true);
	SetCanBeDamaged(false);
	PrimaryActorTick.bCanEverTick = false;
}

bool AEFCalystoNavigationBoundsVolume::SetGeneratedGeometryBounds(const FBox& WorldBounds)
{
	if (!WorldBounds.IsValid || WorldBounds.Min.ContainsNaN() || WorldBounds.Max.ContainsNaN()
		|| WorldBounds.GetExtent().GetMin() <= 0.0)
	{
		return false;
	}
	// Outward centimetre rounding makes center/extent reconstruction exact within
	// the supported world range. Fractional mesh bounds otherwise lose a few ULPs
	// at an edge and fail the navigation system's strict enclosure postcondition.
	// This is one geometry-derived enclosure, never an expanding search.
	const FBox CoveringBounds(
		FVector(FMath::FloorToDouble(WorldBounds.Min.X), FMath::FloorToDouble(WorldBounds.Min.Y), FMath::FloorToDouble(WorldBounds.Min.Z)),
		FVector(FMath::CeilToDouble(WorldBounds.Max.X), FMath::CeilToDouble(WorldBounds.Max.Y), FMath::CeilToDouble(WorldBounds.Max.Z)));
	SetActorTransform(FTransform(FQuat::Identity, CoveringBounds.GetCenter(), FVector::OneVector));
	GeneratedGeometryBounds->SetRelativeTransform(FTransform::Identity);
	GeneratedGeometryBounds->SetBoxExtent(CoveringBounds.GetExtent(), false);
	GeneratedGeometryBounds->UpdateBounds();
	// UE 5.8 OnNavigationBoundsUpdated reads GetComponentsBoundingBox(true).
	// The real box contributes its extent even with gameplay collision disabled.
	const FBox ActualBounds = GetComponentsBoundingBox(true);
	return ActualBounds.IsValid && ActualBounds.IsInsideOrOn(WorldBounds.Min)
		&& ActualBounds.IsInsideOrOn(WorldBounds.Max);
}

void AEFCalystoNavigationBoundsVolume::ObserveNavigationCompletion(UNavigationSystemV1* Navigation, ANavigationData* RelevantData,
	const FNavAgentProperties* QueryAgent, const FVector& QueryOrigin)
{
	const bool bSameQuery = bResolveQueryAgent && QueryAgent
		&& ObservedAgent.AgentRadius == QueryAgent->AgentRadius && ObservedAgent.AgentHeight == QueryAgent->AgentHeight
		&& ObservedAgent.AgentStepHeight == QueryAgent->AgentStepHeight
		&& ObservedAgent.NavWalkingSearchHeightScale == QueryAgent->NavWalkingSearchHeightScale
		&& ObservedAgent.PreferredNavData == QueryAgent->PreferredNavData && ObservedQueryOrigin == QueryOrigin;
	// A completion can resolve the data between registration and the next poll.
	// Repeating the same query must preserve that already-captured revision.
	if (ObservedNavigation.Get() == Navigation
		&& (bSameQuery || (!bResolveQueryAgent && !QueryAgent && ObservedData.Get() == RelevantData))) return;
	StopObservingNavigationCompletion();
	NavigationCompletionRevision = 0;
	LastNavigationCompletionSeconds = 0.0;
	if (!IsValid(Navigation) || Navigation->GetWorld() != GetWorld()
		|| (RelevantData && (!IsValid(RelevantData) || RelevantData->GetWorld() != GetWorld()))
		|| (QueryAgent && (!QueryAgent->IsValid() || QueryOrigin.ContainsNaN()))
		|| (!RelevantData && !QueryAgent)) return;
	ObservedNavigation = Navigation;
	ObservedData = RelevantData;
	bResolveQueryAgent = QueryAgent != nullptr;
	if (QueryAgent) { ObservedAgent = *QueryAgent; ObservedQueryOrigin = QueryOrigin; }
	Navigation->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this, &ThisClass::NavigationGenerationFinished);
}

void AEFCalystoNavigationBoundsVolume::NavigationGenerationFinished(ANavigationData* Data)
{
	UNavigationSystemV1* Navigation = ObservedNavigation.Get();
	if (!Navigation || !IsValid(Data) || Navigation->GetWorld() != GetWorld() || Data->GetWorld() != GetWorld()) return;
	ANavigationData* RelevantData = bResolveQueryAgent
		? Navigation->GetNavDataForProps(ObservedAgent, ObservedQueryOrigin) : ObservedData.Get();
	if (!IsValid(RelevantData) || Data != RelevantData
		|| (ObservedData.IsValid() && ObservedData.Get() != RelevantData)) return;
	ObservedData = RelevantData;
	++NavigationCompletionRevision;
	LastNavigationCompletionSeconds = FPlatformTime::Seconds();
}

void AEFCalystoNavigationBoundsVolume::StopObservingNavigationCompletion()
{
	if (UNavigationSystemV1* Navigation = ObservedNavigation.Get())
		Navigation->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this, &ThisClass::NavigationGenerationFinished);
	ObservedNavigation.Reset();
	ObservedData.Reset();
	bResolveQueryAgent = false;
}

void AEFCalystoNavigationBoundsVolume::Destroyed()
{
	StopObservingNavigationCompletion();
	Super::Destroyed();
}
