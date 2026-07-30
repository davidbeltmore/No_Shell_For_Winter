#include "DirtyPawnWorldSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "DirtyPawnComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"

void UDirtyPawnWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UWorld* World = GetWorld())
	{
		if (!ActorSpawnedHandle.IsValid())
		{
			ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UDirtyPawnWorldSubsystem::HandleActorSpawned));
		}
	}
}

void UDirtyPawnWorldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
			ActorSpawnedHandle.Reset();
		}
	}

	Super::Deinitialize();
}

void UDirtyPawnWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		AttachOrPrewarmActor(*It);
	}
}

void UDirtyPawnWorldSubsystem::HandleActorSpawned(AActor* Actor)
{
	AttachOrPrewarmActor(Actor);
}

void UDirtyPawnWorldSubsystem::AttachOrPrewarmActor(AActor* Actor)
{
	if (!ShouldConsiderActor(Actor))
	{
		return;
	}

	UDirtyPawnComponent* UsableComponent = nullptr;
	UsableComponent = UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(Actor);

	if (!UsableComponent)
	{
		UsableComponent = NewObject<UDirtyPawnComponent>(Actor, UDirtyPawnComponent::StaticClass(), TEXT("DirtyPawnRuntime"));
		if (!UsableComponent)
		{
			return;
		}

		Actor->AddInstanceComponent(UsableComponent);
		UsableComponent->RegisterComponent();
	}

	UsableComponent->bUseMaterialWrapper = true;
	UsableComponent->bAutoInitializeOnBeginPlay = true;
	UsableComponent->PreinitializeDirtyPawn();
}

bool UDirtyPawnWorldSubsystem::ShouldConsiderActor(AActor* Actor) const
{
	if (!IsValid(Actor) || Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	UWorld* World = Actor->GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return false;
	}

	return Actor->IsA<APawn>() && HasDazLikeMesh(Actor);
}

bool UDirtyPawnWorldSubsystem::HasDazLikeMesh(AActor* Actor) const
{
	TArray<USkeletalMeshComponent*> MeshComponents;
	Actor->GetComponents<USkeletalMeshComponent>(MeshComponents, true);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || !MeshComponent->GetSkeletalMeshAsset())
		{
			continue;
		}

		FString Tokens = GetNameSafe(MeshComponent).ToLower();
		Tokens += TEXT(" ");
		Tokens += GetNameSafe(MeshComponent->GetSkeletalMeshAsset()).ToLower();
		Tokens += TEXT(" ");
		Tokens += MeshComponent->GetSkeletalMeshAsset()->GetPathName().ToLower();

		const int32 MaterialCount = MeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			if (UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex))
			{
				Tokens += TEXT(" ");
				Tokens += GetNameSafe(Material).ToLower();
				Tokens += TEXT(" ");
				Tokens += Material->GetPathName().ToLower();
			}
		}

		if (Tokens.Contains(TEXT("daztounreal"))
			|| Tokens.Contains(TEXT("genesis"))
			|| Tokens.Contains(TEXT("genesis8"))
			|| Tokens.Contains(TEXT("genesis9"))
			|| Tokens.Contains(TEXT("daz")))
		{
			return true;
		}
	}

	return false;
}

bool UDirtyPawnWorldSubsystem::IsUsableDirtyPawnComponent(const UDirtyPawnComponent* Component) const
{
	return IsValid(Component)
		&& !Component->GetName().StartsWith(TEXT("TRASH_"))
		&& !Component->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
}
