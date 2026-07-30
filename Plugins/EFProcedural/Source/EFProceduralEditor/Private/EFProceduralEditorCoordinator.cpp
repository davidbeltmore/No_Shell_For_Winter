#include "EFProceduralEditorCoordinator.h"

#include "EFProceduralSettings.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
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
	if (!IsManagedEditorWorld(World))
	{
		return;
	}

	if (AActor* DungeonActor = FindDungeonActor(World))
	{
		TryInvokeActorFunction(DungeonActor, UEFProceduralSettings::Get()->DungeonRefreshFunctionNames);
	}
}

void FEFProceduralEditorCoordinator::CleanupEditorDungeon(UWorld* World) const
{
	if (!IsManagedEditorWorld(World))
	{
		return;
	}
}

AActor* FEFProceduralEditorCoordinator::FindDungeonActor(UWorld* World) const
{
	if (!IsValid(World))
	{
		return nullptr;
	}

	UClass* DungeonClass = UEFProceduralSettings::Get()->GetDungeonActorClassResolved().LoadSynchronous();
	if (!IsValid(DungeonClass))
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World, DungeonClass); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

bool FEFProceduralEditorCoordinator::TryInvokeActorFunction(AActor* Actor, const TArray<FName>& CandidateNames) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	for (const FName& CandidateName : CandidateNames)
	{
		if (UFunction* Function = Actor->FindFunction(CandidateName))
		{
			Actor->ProcessEvent(Function, nullptr);
			return true;
		}
	}

	return false;
}
