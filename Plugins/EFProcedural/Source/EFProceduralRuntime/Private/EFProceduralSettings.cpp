#include "EFProceduralSettings.h"

#include "EFProceduralProjectPreset.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	template <typename TValueType>
	static TValueType ResolveSoftOverride(const TValueType& LocalValue, const TValueType& PresetValue)
	{
		return !LocalValue.IsNull() ? LocalValue : PresetValue;
	}

	static TArray<FString> ResolveStringArrayOverride(const TArray<FString>& LocalValue, const TArray<FString>& PresetValue)
	{
		return LocalValue.Num() > 0 ? LocalValue : PresetValue;
	}
}

UEFProceduralSettings::UEFProceduralSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("EFProcedural");

	ManagedMapNames.Reset();

	DungeonActorClass.Reset();
	StartPointActorClass.Reset();
	MeleeAIControllerClass.Reset();
	RangedAIControllerClass.Reset();
	EnemyClassPathHints.Reset();
	EnemyClassNameHints.Reset();
	MeleeEnemyClassPathHints.Reset();
	MeleeEnemyClassNameHints.Reset();
	RangedEnemyClassPathHints.Reset();
	RangedEnemyClassNameHints.Reset();
}

const UEFProceduralSettings* UEFProceduralSettings::Get()
{
	return GetDefault<UEFProceduralSettings>();
}

FName UEFProceduralSettings::GetCategoryName() const
{
	return TEXT("Game");
}

const UEFProceduralProjectPreset* UEFProceduralSettings::LoadProjectPreset() const
{
	return ProjectPreset.IsNull()
		? nullptr
		: Cast<UEFProceduralProjectPreset>(ProjectPreset.LoadSynchronous());
}

TArray<FString> UEFProceduralSettings::GetManagedMapNamesResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(ManagedMapNames, Preset ? Preset->ManagedMapNames : TArray<FString>());
}

TSoftClassPtr<AActor> UEFProceduralSettings::GetDungeonActorClassResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveSoftOverride(DungeonActorClass, Preset ? Preset->DungeonActorClass : TSoftClassPtr<AActor>());
}

TSoftClassPtr<AActor> UEFProceduralSettings::GetStartPointActorClassResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveSoftOverride(StartPointActorClass, Preset ? Preset->StartPointActorClass : TSoftClassPtr<AActor>());
}

TSoftClassPtr<AController> UEFProceduralSettings::GetMeleeAIControllerClassResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveSoftOverride(MeleeAIControllerClass, Preset ? Preset->MeleeAIControllerClass : TSoftClassPtr<AController>());
}

TSoftClassPtr<AController> UEFProceduralSettings::GetRangedAIControllerClassResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveSoftOverride(RangedAIControllerClass, Preset ? Preset->RangedAIControllerClass : TSoftClassPtr<AController>());
}

TArray<FString> UEFProceduralSettings::GetEnemyClassPathHintsResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(EnemyClassPathHints, Preset ? Preset->EnemyClassPathHints : TArray<FString>());
}

TArray<FString> UEFProceduralSettings::GetEnemyClassNameHintsResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(EnemyClassNameHints, Preset ? Preset->EnemyClassNameHints : TArray<FString>());
}

TArray<FString> UEFProceduralSettings::GetMeleeEnemyClassPathHintsResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(MeleeEnemyClassPathHints, Preset ? Preset->MeleeEnemyClassPathHints : TArray<FString>());
}

TArray<FString> UEFProceduralSettings::GetMeleeEnemyClassNameHintsResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(MeleeEnemyClassNameHints, Preset ? Preset->MeleeEnemyClassNameHints : TArray<FString>());
}

TArray<FString> UEFProceduralSettings::GetRangedEnemyClassPathHintsResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(RangedEnemyClassPathHints, Preset ? Preset->RangedEnemyClassPathHints : TArray<FString>());
}

TArray<FString> UEFProceduralSettings::GetRangedEnemyClassNameHintsResolved() const
{
	const UEFProceduralProjectPreset* Preset = LoadProjectPreset();
	return ResolveStringArrayOverride(RangedEnemyClassNameHints, Preset ? Preset->RangedEnemyClassNameHints : TArray<FString>());
}
