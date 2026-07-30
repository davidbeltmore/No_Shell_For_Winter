#include "Survival/ProjectRealtimeSnapshotSettings.h"

UProjectRealtimeSnapshotSettings::UProjectRealtimeSnapshotSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("ProjectRealtimeSnapshot");

	RelevantEnemyClassNameHints = {
		TEXT("ACFFullEnemyBP"),
		TEXT("MeleeMale"),
		TEXT("RangedMale"),
		TEXT("MageMale"),
		TEXT("DummyMale"),
		TEXT("ACFMeleeEnemyBP"),
		TEXT("ACFRangedEnemyBP"),
		TEXT("ACFMageEnemyBP"),
		TEXT("ACFDummyEnemyBP")
	};
}

const UProjectRealtimeSnapshotSettings* UProjectRealtimeSnapshotSettings::Get()
{
	return GetDefault<UProjectRealtimeSnapshotSettings>();
}

FName UProjectRealtimeSnapshotSettings::GetCategoryName() const
{
	return TEXT("Game");
}
