#include "EFProceduralEditorCoordinator.h"

#include "EFProceduralSettings.h"
#include "Misc/PackageName.h"

FString FEFProceduralEditorCoordinator::NormalizeMapName(const FString& PackageName)
{
	return FPackageName::GetShortName(PackageName);
}

bool FEFProceduralEditorCoordinator::MatchesManagedMapName(const FString& ShortMapName, const FString& ManagedMapName)
{
	return ShortMapName.Equals(ManagedMapName, ESearchCase::IgnoreCase)
		|| ShortMapName.StartsWith(ManagedMapName + TEXT("_"), ESearchCase::IgnoreCase);
}

bool FEFProceduralEditorCoordinator::IsManagedEditorWorld(const UWorld* World) const
{
	if (!IsValid(World) || World->WorldType != EWorldType::Editor)
	{
		return false;
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	const FString ShortMapName = NormalizeMapName(World->GetPackage()->GetName());
	for (const FString& MapName : Settings->GetManagedMapNamesResolved())
	{
		if (MatchesManagedMapName(ShortMapName, MapName))
		{
			return true;
		}
	}

	return false;
}

void FEFProceduralEditorCoordinator::PrepareEditorDungeon(UWorld* World) const
{
	// Phase 2 never invokes editor refresh/randomize functions on Calysto. The
	// editor coordinator remains as a lifecycle seam, but preparation is a
	// deliberate no-op; all adaptation happens on runtime transient clones.
	(void)World;
}

void FEFProceduralEditorCoordinator::CleanupEditorDungeon(UWorld* World) const
{
	(void)World;
}
