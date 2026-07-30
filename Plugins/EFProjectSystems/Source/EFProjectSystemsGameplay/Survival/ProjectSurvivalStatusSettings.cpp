#include "Survival/ProjectSurvivalStatusSettings.h"
#include "Survival/ProjectSurvivalStatusCatalog.h"

#include "Engine/DataTable.h"

UProjectSurvivalStatusSettings::UProjectSurvivalStatusSettings()
{
	const FProjectSurvivalStatusCatalog& Catalog = GetProjectSurvivalStatusCatalog();

	ExhaustedBlackoutSeconds = 15.f;
	ExhaustedSleepRestorePercent = 0.5f;
	StatusDefinitionsTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses.DT_ProjectSurvivalStatuses")));
	StatusHudOffset = Catalog.StatusHudOffset;
	StatusIconSize = Catalog.StatusIconSize;
	StatusIconSpacing = Catalog.StatusIconSpacing;
	StatusIconsPerRow = Catalog.StatusIconsPerRow;
	MaxVisibleStatuses = 5;
	StatusDefinitions = Catalog.StatusDefinitions;
}

const UProjectSurvivalStatusSettings* UProjectSurvivalStatusSettings::Get()
{
	return GetDefault<UProjectSurvivalStatusSettings>();
}

FName UProjectSurvivalStatusSettings::GetCategoryName() const
{
	return TEXT("Game");
}

TArray<FProjectSurvivalStatusDefinition> UProjectSurvivalStatusSettings::BuildResolvedStatusDefinitions() const
{
	UDataTable* DefinitionsTable = StatusDefinitionsTable.IsNull()
		? nullptr
		: StatusDefinitionsTable.LoadSynchronous();
	return BuildDefinitionsFromTable(DefinitionsTable, StatusDefinitions);
}

TArray<FProjectSurvivalStatusDefinition> UProjectSurvivalStatusSettings::BuildDefinitionsFromTable(
	const UDataTable* DefinitionsTable,
	const TArray<FProjectSurvivalStatusDefinition>& FallbackDefinitions)
{
	TArray<FProjectSurvivalStatusDefinition> ResolvedDefinitions = FallbackDefinitions;
	TMap<FName, int32> DefinitionIndexByName;
	for (int32 Index = 0; Index < ResolvedDefinitions.Num(); ++Index)
	{
		const FName StatusName = ResolvedDefinitions[Index].StatusName;
		if (!StatusName.IsNone() && !DefinitionIndexByName.Contains(StatusName))
		{
			DefinitionIndexByName.Add(StatusName, Index);
		}
	}

	if (!DefinitionsTable)
	{
		return ResolvedDefinitions;
	}

	if (DefinitionsTable->GetRowStruct() != FProjectSurvivalStatusTableRow::StaticStruct())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ProjectSurvivalStatus] DataTable '%s' uses row struct '%s'; expected '%s'. Falling back to configured status definitions."),
			*GetNameSafe(DefinitionsTable),
			*GetNameSafe(DefinitionsTable->GetRowStruct()),
			*GetNameSafe(FProjectSurvivalStatusTableRow::StaticStruct()));
		return ResolvedDefinitions;
	}

	for (const FName RowName : DefinitionsTable->GetRowNames())
	{
		const FProjectSurvivalStatusTableRow* Row = DefinitionsTable->FindRow<FProjectSurvivalStatusTableRow>(
			RowName,
			TEXT("ProjectSurvivalStatusDefinitions"),
			false);
		if (!Row)
		{
			continue;
		}

		FProjectSurvivalStatusDefinition Definition = Row->ToStatusDefinition(RowName);
		if (Definition.StatusName.IsNone())
		{
			continue;
		}

		if (int32* ExistingIndex = DefinitionIndexByName.Find(Definition.StatusName))
		{
			ResolvedDefinitions[*ExistingIndex] = Definition;
		}
		else
		{
			DefinitionIndexByName.Add(Definition.StatusName, ResolvedDefinitions.Num());
			ResolvedDefinitions.Add(Definition);
		}
	}

	return ResolvedDefinitions;
}
