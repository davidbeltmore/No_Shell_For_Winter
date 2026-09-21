#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Calysto/EFCalystoDeferredMeshBoundsProvider.h"
#include "EFCalystoDeferredBoundsTestActor.generated.h"

class UStaticMesh;

UCLASS(NotBlueprintable, Transient)
class AEFCalystoDeferredBoundsTestActor : public AActor, public IEFCalystoDeferredMeshBoundsProvider
{
	GENERATED_BODY()
public:
	AEFCalystoDeferredBoundsTestActor()
	{
		RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("BoundsRoot"));
		auto* Attachment = CreateDefaultSubobject<USceneComponent>(TEXT("VisualAttachment"));
		Attachment->SetupAttachment(RootComponent);
	}
	UPROPERTY() TSoftObjectPtr<UStaticMesh> VisualMesh;
	UPROPERTY() int32 WrongType = 0;
	TArray<FEFCalystoDeferredMeshBinding> Bindings;
	virtual void GetDeferredMeshBoundsBindings(TArray<FEFCalystoDeferredMeshBinding>& Out) const override { Out.Append(Bindings); }
};
