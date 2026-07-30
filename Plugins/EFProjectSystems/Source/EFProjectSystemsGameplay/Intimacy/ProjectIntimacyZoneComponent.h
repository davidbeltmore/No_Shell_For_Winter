#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ProjectIntimacyZoneComponent.generated.h"

class AActor;

/**
 * Explicit spatial authority for voluntary Intimacy.
 *
 * The component is fail-closed and performs no overlap polling. Eligibility
 * queries inspect registered allowed zones only when an interaction is
 * requested or while an active session is being validated.
 */
UCLASS(ClassGroup = (Project), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectIntimacyZoneComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UProjectIntimacyZoneComponent();

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Zone")
	bool ContainsParticipants(const AActor* FirstParticipant, const AActor* SecondParticipant) const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Zone", meta = (WorldContext = "WorldContextObject"))
	static bool IsAnyAllowedZoneContaining(
		const UObject* WorldContextObject,
		const AActor* FirstParticipant,
		const AActor* SecondParticipant);

	static bool AreLocationsWithinZone(
		const FVector& ZoneLocation,
		float Radius,
		const FVector& FirstLocation,
		const FVector& SecondLocation,
		bool bZoneAllowsIntimacy);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Zone")
	bool bAllowsIntimacy = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Zone", meta = (ClampMin = "100.0"))
	float AllowedRadius = 650.0f;
};
