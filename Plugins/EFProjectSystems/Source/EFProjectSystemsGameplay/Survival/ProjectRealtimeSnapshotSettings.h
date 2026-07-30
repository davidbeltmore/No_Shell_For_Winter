#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectRealtimeSnapshotSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Realtime Snapshot"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRealtimeSnapshotSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectRealtimeSnapshotSettings();

	static const UProjectRealtimeSnapshotSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Combat|Snapshots", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float HealthScanIntervalSeconds = 0.10f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat|Snapshots", meta = (ClampMin = "0.10", UIMin = "0.10"))
	float EnemyRefreshIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat|Snapshots", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RelevantEnemyRadius = 10000.f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat|Snapshots")
	TArray<FString> RelevantEnemyClassNameHints;
};
