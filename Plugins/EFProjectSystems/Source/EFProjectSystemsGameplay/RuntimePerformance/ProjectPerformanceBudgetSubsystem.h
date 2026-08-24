#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectPerformanceBudgetSubsystem.generated.h"

class APawn;
class UProjectPerformanceBudgetSettings;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPerformanceBudgetSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 RuntimeEnemyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 FullRateEnemyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 MidRateEnemyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 FarRateEnemyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 BudgetedNiagaraComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 CulledExcessEnemyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Performance Budget")
	int32 SuspendedExcessEnemyCount = 0;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPerformanceBudgetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintPure, Category = "Project|Performance Budget")
	FProjectPerformanceBudgetSnapshot GetLastSnapshot() const;

private:
	void ResolveRuntimeEnemyClasses(bool bAllowSynchronousLoad);
	void ApplyBudgets();
	void CollectRuntimeEnemies(TArray<APawn*>& OutEnemies) const;
	bool IsRuntimeEnemyPawn(const APawn* Pawn) const;
	void ApplyEnemyBudget(const TArray<APawn*>& Enemies, const UProjectPerformanceBudgetSettings& Settings);
	void ApplyNiagaraBudget(const FVector& PlayerLocation, const UProjectPerformanceBudgetSettings& Settings);

private:
	TArray<TSubclassOf<APawn>> RuntimeEnemyClasses;
	FProjectPerformanceBudgetSnapshot LastSnapshot;
	float UpdateAccumulatorSeconds = 0.0f;
};
