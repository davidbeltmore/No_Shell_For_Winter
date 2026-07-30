#include "EFProceduralRuntimeSubsystem.h"

#include "Interfaces/LevelReadinessProvider.h"
#include "Interfaces/PlayerStartResolver.h"
#include "Interfaces/SpawnPostProcessor.h"

namespace EFProceduralRuntimePrivate
{
	static bool AddUniqueProvider(UObject* Provider, TArray<TWeakObjectPtr<UObject>>& Providers)
	{
		if (!IsValid(Provider))
		{
			return false;
		}

		const int32 ExistingIndex = Providers.IndexOfByPredicate([Provider](const TWeakObjectPtr<UObject>& Entry)
		{
			return Entry.Get() == Provider;
		});

		if (ExistingIndex == INDEX_NONE)
		{
			Providers.Add(Provider);
			return true;
		}

		return false;
	}

	static void RemoveProvider(UObject* Provider, TArray<TWeakObjectPtr<UObject>>& Providers)
	{
		Providers.RemoveAllSwap([Provider](const TWeakObjectPtr<UObject>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Provider;
		});
	}
}

void UEFProceduralRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UEFProceduralRuntimeSubsystem::Deinitialize()
{
	LevelReadinessProviders.Reset();
	PlayerStartResolvers.Reset();
	SpawnPostProcessors.Reset();
	Super::Deinitialize();
}

void UEFProceduralRuntimeSubsystem::RegisterProvider(UObject* Provider)
{
	if (!IsValid(Provider))
	{
		return;
	}

	if (Provider->GetClass()->ImplementsInterface(ULevelReadinessProvider::StaticClass()))
	{
		EFProceduralRuntimePrivate::AddUniqueProvider(Provider, LevelReadinessProviders);
	}

	if (Provider->GetClass()->ImplementsInterface(UPlayerStartResolver::StaticClass()))
	{
		EFProceduralRuntimePrivate::AddUniqueProvider(Provider, PlayerStartResolvers);
	}

	if (Provider->GetClass()->ImplementsInterface(USpawnPostProcessor::StaticClass()))
	{
		EFProceduralRuntimePrivate::AddUniqueProvider(Provider, SpawnPostProcessors);
	}
}

void UEFProceduralRuntimeSubsystem::UnregisterProvider(UObject* Provider)
{
	EFProceduralRuntimePrivate::RemoveProvider(Provider, LevelReadinessProviders);
	EFProceduralRuntimePrivate::RemoveProvider(Provider, PlayerStartResolvers);
	EFProceduralRuntimePrivate::RemoveProvider(Provider, SpawnPostProcessors);
}

bool UEFProceduralRuntimeSubsystem::IsDungeonRuntimeReady(UWorld* World)
{
	return IsLevelRuntimeReady(World);
}

bool UEFProceduralRuntimeSubsystem::IsLevelRuntimeReady(UWorld* World)
{
	CompactProviders(LevelReadinessProviders);

	for (const TWeakObjectPtr<UObject>& ProviderPtr : LevelReadinessProviders)
	{
		if (ILevelReadinessProvider* Provider = Cast<ILevelReadinessProvider>(ProviderPtr.Get()))
		{
			if (!Provider->IsLevelRuntimeReady(World))
			{
				return false;
			}
		}
	}

	return true;
}

bool UEFProceduralRuntimeSubsystem::ResolvePlayerStartTransform(UWorld* World, FTransform& OutStartTransform) const
{
	TArray<TWeakObjectPtr<UObject>> MutableResolvers = PlayerStartResolvers;
	CompactProviders(MutableResolvers);

	for (const TWeakObjectPtr<UObject>& ProviderPtr : MutableResolvers)
	{
		if (const IPlayerStartResolver* Provider = Cast<IPlayerStartResolver>(ProviderPtr.Get()))
		{
			if (Provider->ResolvePlayerStartTransform(World, OutStartTransform))
			{
				return true;
			}
		}
	}

	return false;
}

bool UEFProceduralRuntimeSubsystem::ShouldPostProcessSpawnedActor(const AActor* SpawnedActor) const
{
	TArray<TWeakObjectPtr<UObject>> MutableProcessors = SpawnPostProcessors;
	CompactProviders(MutableProcessors);

	for (const TWeakObjectPtr<UObject>& ProviderPtr : MutableProcessors)
	{
		if (const ISpawnPostProcessor* Provider = Cast<ISpawnPostProcessor>(ProviderPtr.Get()))
		{
			if (Provider->ShouldPostProcessSpawnedActor(SpawnedActor))
			{
				return true;
			}
		}
	}

	return false;
}

void UEFProceduralRuntimeSubsystem::PostProcessSpawnedActor(AActor* SpawnedActor)
{
	CompactProviders(SpawnPostProcessors);

	for (const TWeakObjectPtr<UObject>& ProviderPtr : SpawnPostProcessors)
	{
		if (ISpawnPostProcessor* Provider = Cast<ISpawnPostProcessor>(ProviderPtr.Get()))
		{
			if (Provider->ShouldPostProcessSpawnedActor(SpawnedActor))
			{
				Provider->PostProcessSpawnedActor(SpawnedActor);
			}
		}
	}
}

void UEFProceduralRuntimeSubsystem::CompactProviders(TArray<TWeakObjectPtr<UObject>>& Providers)
{
	Providers.RemoveAllSwap([](const TWeakObjectPtr<UObject>& ProviderPtr)
	{
		return !ProviderPtr.IsValid();
	});
}
