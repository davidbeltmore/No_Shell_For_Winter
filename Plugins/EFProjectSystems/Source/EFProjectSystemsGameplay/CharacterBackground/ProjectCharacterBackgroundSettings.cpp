#include "CharacterBackground/ProjectCharacterBackgroundSettings.h"

#include "CharacterBackground/UI/ProjectCharacterBackgroundCreationWidget.h"

UProjectCharacterBackgroundSettings::UProjectCharacterBackgroundSettings()
{
	StorySelectionMapNames = { TEXT("StorySelection") };
	BackstoryDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/CharacterBackground/DT_ProjectBackstories.DT_ProjectBackstories")));
	ProfessionDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/CharacterBackground/DT_ProjectProfessions.DT_ProjectProfessions")));
	CreationWidgetClass = TSoftClassPtr<UProjectCharacterBackgroundCreationWidget>(FSoftObjectPath(TEXT("/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget.WBP_ProjectCharacterBackgroundCreationWidget_C")));
}

const UProjectCharacterBackgroundSettings* UProjectCharacterBackgroundSettings::Get()
{
	return GetDefault<UProjectCharacterBackgroundSettings>();
}

FName UProjectCharacterBackgroundSettings::GetCategoryName() const
{
	return TEXT("Game");
}
