#include "EFCharacterCustomizationSaveGame.h"

int32 UEFCharacterCustomizationSaveGame::FindPresetIndexByName(const FString& PresetName) const
{
	return Presets.IndexOfByPredicate([&PresetName](const FCharacterPresetData& Preset)
	{
		return Preset.PresetName.Equals(PresetName, ESearchCase::IgnoreCase);
	});
}

void UEFCharacterCustomizationSaveGame::AddOrUpdatePreset(const FCharacterPresetData& PresetData)
{
	const int32 ExistingIndex = FindPresetIndexByName(PresetData.PresetName);
	if (ExistingIndex != INDEX_NONE)
	{
		Presets[ExistingIndex] = PresetData;
		return;
	}

	Presets.Add(PresetData);
}

bool UEFCharacterCustomizationSaveGame::TryGetPreset(const FString& PresetName, FCharacterPresetData& OutPresetData) const
{
	const int32 ExistingIndex = FindPresetIndexByName(PresetName);
	if (ExistingIndex == INDEX_NONE)
	{
		return false;
	}

	OutPresetData = Presets[ExistingIndex];
	return true;
}

TArray<FString> UEFCharacterCustomizationSaveGame::GetPresetNames() const
{
	TArray<FString> Names;
	Names.Reserve(Presets.Num());

	for (const FCharacterPresetData& Preset : Presets)
	{
		Names.Add(Preset.PresetName);
	}

	return Names;
}
