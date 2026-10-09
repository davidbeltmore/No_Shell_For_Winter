#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ALSSavableInterface.h"
#include "EFCharacterCreationTypes.h"
#include "ProjectNPCAppearanceState.generated.h"
class APawn;

USTRUCT(BlueprintType)
struct FProjectNPCAppearanceRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame, BlueprintReadOnly) int32 GeneratorVersion = 1;
	UPROPERTY(SaveGame, BlueprintReadOnly) int32 Seed = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly) FCharacterCustomizationState Appearance;
	/** Updates only catalog-declared neutral morphs in old generated NPC records. */
	void MigrateNeutralDefaults();
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectNPCAppearanceTravel : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UPROPERTY() TMap<FString, FProjectNPCAppearanceRecord> Records;
};

/** ALS saves this actor alongside the world. NPC tags persist generated identities without modifying their Blueprints. */
UCLASS(NotPlaceable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectNPCAppearanceState : public AInfo, public IALSSavableInterface
{
	GENERATED_BODY()
public:
	AProjectNPCAppearanceState();
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void OnLoaded_Implementation() override;
	UPROPERTY(SaveGame) TMap<FString, FProjectNPCAppearanceRecord> Records;
	static AProjectNPCAppearanceState* Ensure(UWorld* World);
	/** Explicit EF.AppearanceId.* tags override identity; generated IDs are serialized by ALS actor tags. */
	static FString IdentityFor(APawn* Pawn);
	static int32 SeedFor(const FString& Identity);
	void Publish();
};
