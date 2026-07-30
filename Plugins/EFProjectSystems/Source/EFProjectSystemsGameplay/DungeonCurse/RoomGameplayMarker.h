#pragma once

#include "CoreMinimal.h"
#include "DungeonCurse/DungeonCurseTypes.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoomGameplayMarker.generated.h"

class UBoxComponent;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API ARoomGameplayMarker : public AActor
{
	GENERATED_BODY()

public:
	ARoomGameplayMarker();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Dungeon Curse|Marker")
	UBoxComponent* GetBoxComponent() const { return BoxComponent; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse|Marker")
	TObjectPtr<UBoxComponent> BoxComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker", meta = (ClampMin = "1.0", UIMin = "1.0"))
	FVector BoxExtent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	EGeneratedRoomType RoomType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	bool bAllowCurses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float CurseChanceOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	FGameplayTagContainer GameplayTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	FColor DebugColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	int32 Priority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	bool bDebugDraw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	bool bAllowEnemyLevelCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	bool bAllowLightCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Marker")
	bool bAllowInnerStateCurse;

private:
	void UpdateBoxExtent();
	void DrawMarkerDebug() const;
};
