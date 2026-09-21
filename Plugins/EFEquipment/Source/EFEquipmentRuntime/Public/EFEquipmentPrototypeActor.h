#pragma once
#include "CoreMinimal.h"
#include "ItemActors/ACFItemActor.h"
#include "EFEquipmentPrototypeActor.generated.h"

class UStaticMeshComponent;
UCLASS(Blueprintable)
class EFEQUIPMENTRUNTIME_API AEFEquipmentPrototypeActor : public AACFItemActor
{
    GENERATED_BODY()
public:
    AEFEquipmentPrototypeActor();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prototype") TObjectPtr<UStaticMeshComponent> PrototypeMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FName AttachmentBone = TEXT("spine_03");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FVector AttachmentOffset = FVector(-18, 0, -5);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prototype") FVector MeshScale = FVector(0.16, 0.30, 0.35);
protected:
    virtual void BeginPlay() override;
};
