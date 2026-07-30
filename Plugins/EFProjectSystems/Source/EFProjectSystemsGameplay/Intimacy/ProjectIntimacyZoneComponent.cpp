#include "Intimacy/ProjectIntimacyZoneComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"

UProjectIntimacyZoneComponent::UProjectIntimacyZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

bool UProjectIntimacyZoneComponent::ContainsParticipants(
	const AActor* FirstParticipant,
	const AActor* SecondParticipant) const
{
	return IsValid(FirstParticipant)
		&& IsValid(SecondParticipant)
		&& IsRegistered()
		&& IsActive()
		&& AreLocationsWithinZone(
			GetComponentLocation(),
			AllowedRadius,
			FirstParticipant->GetActorLocation(),
			SecondParticipant->GetActorLocation(),
			bAllowsIntimacy);
}

bool UProjectIntimacyZoneComponent::IsAnyAllowedZoneContaining(
	const UObject* WorldContextObject,
	const AActor* FirstParticipant,
	const AActor* SecondParticipant)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !IsValid(FirstParticipant) || !IsValid(SecondParticipant))
	{
		return false;
	}

	for (TObjectIterator<UProjectIntimacyZoneComponent> It; It; ++It)
	{
		const UProjectIntimacyZoneComponent* Zone = *It;
		if (IsValid(Zone)
			&& !Zone->IsTemplate()
			&& Zone->GetWorld() == World
			&& Zone->ContainsParticipants(FirstParticipant, SecondParticipant))
		{
			return true;
		}
	}

	return false;
}

bool UProjectIntimacyZoneComponent::AreLocationsWithinZone(
	const FVector& ZoneLocation,
	const float Radius,
	const FVector& FirstLocation,
	const FVector& SecondLocation,
	const bool bZoneAllowsIntimacy)
{
	if (!bZoneAllowsIntimacy || Radius < 100.0f)
	{
		return false;
	}

	const double RadiusSquared = FMath::Square(static_cast<double>(Radius));
	return FVector::DistSquared(ZoneLocation, FirstLocation) <= RadiusSquared
		&& FVector::DistSquared(ZoneLocation, SecondLocation) <= RadiusSquared;
}
