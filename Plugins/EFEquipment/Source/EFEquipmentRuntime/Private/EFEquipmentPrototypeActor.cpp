#include "EFEquipmentPrototypeActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AEFEquipmentPrototypeActor::AEFEquipmentPrototypeActor()
{
    PrototypeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeMesh")); SetRootComponent(PrototypeMesh);
    PrototypeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) PrototypeMesh->SetStaticMesh(Cube.Object);
}
void AEFEquipmentPrototypeActor::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Character = Cast<ACharacter>(GetItemOwner()); Character && Character->GetMesh())
    {
        AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale,
            Character->GetMesh()->DoesSocketExist(AttachmentBone) ? AttachmentBone : NAME_None);
        SetActorRelativeLocation(AttachmentOffset); SetActorRelativeScale3D(MeshScale);
    }
}
