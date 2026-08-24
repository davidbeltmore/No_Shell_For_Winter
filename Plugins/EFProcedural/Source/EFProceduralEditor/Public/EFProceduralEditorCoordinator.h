#pragma once

#include "CoreMinimal.h"

class UWorld;

class EFPROCEDURALEDITOR_API FEFProceduralEditorCoordinator
{
public:
	bool IsManagedEditorWorld(const UWorld* World) const;
	void PrepareEditorDungeon(UWorld* World) const;
	void CleanupEditorDungeon(UWorld* World) const;

private:
	static FString NormalizeMapName(const FString& PackageName);
	static bool MatchesManagedMapName(const FString& ShortMapName, const FString& ManagedMapName);
};
