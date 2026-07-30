#pragma once

#include "GameFramework/Actor.h"
#include "DirtyPawnVolumeActors.generated.h"

class UBoxComponent;
class UDirtyPawnComponent;

UENUM(BlueprintType)
enum class EDirtyPawnVolumeKind : uint8
{
	Water,
	MudWater,
	BleachWater,
	FadeWash,
	Sand,
	Snow,
	SandAndSnow,
	HurtBlood,
	HurtSmear,
	HurtDirt,
	HurtBurn,
	Interior
};

UCLASS(Abstract, Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnVolumeBase : public AActor
{
	GENERATED_BODY()

public:
	ADirtyPawnVolumeBase();

	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	TObjectPtr<UBoxComponent> ContactVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn")
	EDirtyPawnVolumeKind VolumeKind = EDirtyPawnVolumeKind::Water;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn")
	float RuntimeUpdateInterval = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn")
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn")
	bool bUseVolumeTopAsHeight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn")
	bool bLogMissingComponent = true;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	virtual void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	virtual void OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	virtual void ApplyToDirtyPawn(UDirtyPawnComponent* DirtyPawn, AActor* SourceActor);
	virtual void EndApplyToDirtyPawn(UDirtyPawnComponent* DirtyPawn, AActor* SourceActor);

	UDirtyPawnComponent* ResolveDirtyPawn(AActor* OtherActor, UPrimitiveComponent* OtherComponent) const;
	float ResolveNodeHeight(UDirtyPawnComponent* DirtyPawn) const;
	bool ResolveContactHeightBand(UDirtyPawnComponent* DirtyPawn, float& OutMinHeight, float& OutMaxHeight) const;

private:
	void RefreshContactPrimitives();
	void ApplyCurrentOverlaps();
	FBox ResolveContactBounds() const;
	static bool CanUseContactPrimitive(const UPrimitiveComponent* Primitive);
	bool IsPreferredContactPrimitive(const UPrimitiveComponent* Primitive) const;
	static UDirtyPawnComponent* ResolveDirtyPawnFromActorChain(AActor* Actor);

	UPROPERTY()
	TArray<TObjectPtr<UDirtyPawnComponent>> OverlappingPawns;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> ContactPrimitives;

	float TimeUntilNextUpdate = 0.0f;
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnWaterVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnWaterVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnMudWaterVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnMudWaterVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnBleachWaterVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnBleachWaterVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnFadeWashVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnFadeWashVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnFadeSandSnowVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnFadeSandSnowVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnBloodVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnBloodVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnSmearVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnSmearVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnDirtVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnDirtVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnBurnVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnBurnVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnSandVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnSandVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnSnowVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnSnowVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnHurtVolume : public ADirtyPawnBloodVolume
{
	GENERATED_BODY()

public:
	ADirtyPawnHurtVolume();
};

UCLASS(Blueprintable)
class DIRTYPAWNRUNTIME_API ADirtyPawnInteriorVolume : public ADirtyPawnVolumeBase
{
	GENERATED_BODY()

public:
	ADirtyPawnInteriorVolume();
};
