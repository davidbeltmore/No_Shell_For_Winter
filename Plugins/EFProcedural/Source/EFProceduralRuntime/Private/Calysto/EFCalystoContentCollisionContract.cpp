#include "Calysto/EFCalystoContentCollisionContract.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/MovementComponent.h"
#include "Misc/SecureHash.h"

namespace EFCalystoContentCollisionPrivate
{
	bool Finite(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool QueryEnabled(const ECollisionEnabled::Type Value)
	{
		return Value == ECollisionEnabled::QueryOnly || Value == ECollisionEnabled::QueryAndPhysics;
	}

	FString TransformKey(const FTransform& Transform)
	{
		const FVector Location = Transform.GetLocation(), Scale = Transform.GetScale3D();
		const FQuat Rotation = Transform.GetRotation();
		return FString::Printf(TEXT("%.17g,%.17g,%.17g|%.17g,%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
			Location.X, Location.Y, Location.Z, Rotation.X, Rotation.Y, Rotation.Z, Rotation.W, Scale.X, Scale.Y, Scale.Z);
	}

	bool ComponentToRoot(const USceneComponent* Component, const USceneComponent* Root, FTransform& OutTransform)
	{
		OutTransform = FTransform::Identity;
		const USceneComponent* Current = Component;
		for (int32 Depth = 0; Current && Current != Root && Depth < 256; ++Depth)
		{
			OutTransform = OutTransform * Current->GetRelativeTransform();
			Current = Current->GetAttachParent();
		}
		return Current == Root && !OutTransform.ContainsNaN();
	}

	FBox BoundsBox(const UPrimitiveComponent& Component, const FTransform& LocalToActor)
	{
		const FBoxSphereBounds Bounds = Component.CalcBounds(LocalToActor);
		return FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
	}

	/**
	 * Project actors may assign a mesh to an existing default scene component
	 * while deferred materialization is in progress. That mesh has no CDO
	 * primitive bounds yet, so it must opt into this finite, exact contract with
	 * EFCalystoDeferredMeshBounds and name its pre-existing attachment component.
	 */
	bool AddDeferredMeshBounds(AActor* CDO, USceneComponent* RootScene,
		FEFCalystoContentCollisionContract& OutContract, FString& OutError)
	{
		int32 DeclaredMeshCount = 0;
		for (TFieldIterator<FProperty> It(CDO->GetClass()); It; ++It)
		{
			FProperty* const Property = *It;
			if (!Property->GetBoolMetaData(TEXT("EFCalystoDeferredMeshBounds"))) continue;
			if (++DeclaredMeshCount > 16 || Property->ArrayDim != 1)
			{
				OutError = TEXT("Deferred visual bounds exceed the finite property contract.");
				return false;
			}
			const FSoftObjectProperty* const SoftMeshProperty = CastField<FSoftObjectProperty>(Property);
			if (!SoftMeshProperty || !SoftMeshProperty->PropertyClass
				|| (!SoftMeshProperty->PropertyClass->IsChildOf(USkeletalMesh::StaticClass())
					&& !SoftMeshProperty->PropertyClass->IsChildOf(UStaticMesh::StaticClass())))
			{
				OutError = TEXT("Deferred visual bounds require one scalar soft skeletal or static mesh property.");
				return false;
			}
			const FSoftObjectPtr MeshReference = SoftMeshProperty->GetPropertyValue_InContainer(CDO);
			const FSoftObjectPath MeshPath = MeshReference.ToSoftObjectPath();
			UObject* const LoadedMesh = MeshReference.Get();
			if (MeshPath.IsNull() || !IsValid(LoadedMesh))
			{
				OutError = FString::Printf(TEXT("Deferred visual mesh is not resident before placement reservation: %s"),
					*MeshPath.ToString());
				return false;
			}
			FBoxSphereBounds MeshBounds;
			if (const USkeletalMesh* const SkeletalMesh = Cast<USkeletalMesh>(LoadedMesh))
			{
				MeshBounds = SkeletalMesh->GetBounds();
			}
			else if (const UStaticMesh* const StaticMesh = Cast<UStaticMesh>(LoadedMesh))
			{
				MeshBounds = StaticMesh->GetBounds();
			}
			else
			{
				OutError = TEXT("Deferred visual mesh property resolved an unsupported resource type.");
				return false;
			}
			const FString ComponentName = Property->GetMetaData(TEXT("EFCalystoDeferredMeshComponent"));
			USceneComponent* const MeshComponent = ComponentName.IsEmpty() ? nullptr
				: Cast<USceneComponent>(CDO->GetDefaultSubobjectByName(FName(*ComponentName)));
			FTransform ToRoot;
			if (!MeshComponent || !RootScene || !ComponentToRoot(MeshComponent, RootScene, ToRoot))
			{
				OutError = TEXT("Deferred visual mesh lacks its declared finite CDO attachment path.");
				return false;
			}
			const FBox LocalBounds = FBox(MeshBounds.Origin - MeshBounds.BoxExtent, MeshBounds.Origin + MeshBounds.BoxExtent)
				.TransformBy(ToRoot * OutContract.RootRelativeTransform);
			if (!LocalBounds.IsValid || !Finite(LocalBounds.Min) || !Finite(LocalBounds.Max) || LocalBounds.GetExtent().IsNearlyZero())
			{
				OutError = TEXT("Deferred visual mesh has no finite nonzero local bounds.");
				return false;
			}
			OutContract.LocalPrimitiveBounds += LocalBounds;
		}
		return true;
	}

	FString CanonicalHash(const FEFCalystoContentCollisionContract& Contract)
	{
		FString Canonical = FString::Printf(TEXT("%s|%s|%s|%s|%u|%u|%u|%u"),
			*Contract.ActorClassPath.ToString(), *Contract.RootComponentClassPath, *Contract.CollisionProfile.ToString(),
			*TransformKey(Contract.RootRelativeTransform), uint8(Contract.ObjectType), Contract.CollisionEnabled,
			Contract.PawnResponse, uint8(Contract.bHasQueryCollision));
		if (Contract.bHasPrimitiveBounds)
		{
			Canonical += FString::Printf(TEXT("|primitive=%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
				Contract.LocalPrimitiveBounds.Min.X, Contract.LocalPrimitiveBounds.Min.Y, Contract.LocalPrimitiveBounds.Min.Z,
				Contract.LocalPrimitiveBounds.Max.X, Contract.LocalPrimitiveBounds.Max.Y, Contract.LocalPrimitiveBounds.Max.Z);
		}
		else
		{
			Canonical += TEXT("|primitive=none");
		}
		if (Contract.bHasQueryCollision)
		{
			Canonical += FString::Printf(TEXT("|%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
				Contract.LocalRootBounds.Min.X, Contract.LocalRootBounds.Min.Y, Contract.LocalRootBounds.Min.Z,
				Contract.LocalRootBounds.Max.X, Contract.LocalRootBounds.Max.Y, Contract.LocalRootBounds.Max.Z);
		}
		return FMD5::HashAnsiString(*(Canonical + TEXT("|") + Contract.QueryParticipantsHash));
	}
}

bool FEFCalystoContentCollisionContracts::Build(UClass* ActorClass,
	FEFCalystoContentCollisionContract& OutContract, FString& OutError)
{
	using namespace EFCalystoContentCollisionPrivate;
	OutContract = {};
	OutError.Reset();
	if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass())
		|| ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
	{
		OutError = TEXT("Content collision preflight requires a loaded concrete AActor class.");
		return false;
	}
	AActor* const CDO = Cast<AActor>(ActorClass->GetDefaultObject());
	if (!IsValid(CDO))
	{
		OutError = TEXT("The loaded content actor class has no valid default object for collision preflight.");
		return false;
	}
	OutContract.ActorClassPath = FSoftObjectPath(ActorClass);
	USceneComponent* const RootScene = CDO->GetRootComponent();
	if (RootScene)
	{
		OutContract.RootComponentClassPath = RootScene->GetClass()->GetPathName();
		OutContract.RootRelativeTransform = FTransform(RootScene->GetRelativeRotation(), RootScene->GetRelativeLocation(), RootScene->GetRelativeScale3D());
		if (OutContract.RootRelativeTransform.ContainsNaN())
		{
			OutError = TEXT("The content actor CDO root has a nonfinite relative transform.");
			return false;
		}
	}
	UPrimitiveComponent* const RootPrimitive = Cast<UPrimitiveComponent>(RootScene);
	if (RootPrimitive)
	{
		OutContract.CollisionProfile = RootPrimitive->GetCollisionProfileName();
		OutContract.ObjectType = RootPrimitive->GetCollisionObjectType();
		OutContract.CollisionEnabled = uint8(RootPrimitive->GetCollisionEnabled());
		OutContract.PawnResponse = uint8(RootPrimitive->GetCollisionResponseToChannel(ECC_Pawn));
	}

	TArray<UPrimitiveComponent*> Participants;
	TSet<UPrimitiveComponent*> Seen;
	const auto AddParticipant = [&Participants, &Seen](UPrimitiveComponent* Component)
	{
		if (IsValid(Component) && Component->IsQueryCollisionEnabled() && !Seen.Contains(Component))
		{
			Seen.Add(Component);
			Participants.Add(Component);
		}
	};
	if (UMovementComponent* const Movement = CDO->FindComponentByClass<UMovementComponent>(); Movement && Movement->UpdatedPrimitive)
	{
		AddParticipant(Movement->UpdatedPrimitive);
	}
	else if (RootScene)
	{
		AddParticipant(RootPrimitive);
		TArray<USceneComponent*> Children;
		RootScene->GetChildrenComponents(true, Children);
		if (Children.Num() > 255)
		{
			OutError = TEXT("The content actor CDO exceeds the 255-child collision contract bound.");
			return false;
		}
		for (USceneComponent* Child : Children) AddParticipant(Cast<UPrimitiveComponent>(Child));
	}

	TArray<FString> ParticipantLines;
	for (UPrimitiveComponent* const Component : Participants)
	{
		FTransform ToRoot;
		if (!RootScene || !ComponentToRoot(Component, RootScene, ToRoot))
		{
			OutError = TEXT("A CDO spawn-collision participant has no finite bounded attachment path to the root.");
			return false;
		}
		const FTransform ToActor = ToRoot * OutContract.RootRelativeTransform;
		const FBox Bounds = BoundsBox(*Component, ToActor);
		if (!Bounds.IsValid || !Finite(Bounds.Min) || !Finite(Bounds.Max) || Bounds.GetExtent().IsNearlyZero())
		{
			OutError = TEXT("A CDO spawn-collision participant has no finite nonzero local bounds.");
			return false;
		}
		OutContract.LocalRootBounds += Bounds;
		FString Responses;
		for (uint8 Channel = 0; Channel < uint8(ECC_MAX); ++Channel)
			Responses += FString::FromInt(uint8(Component->GetCollisionResponseToChannel(ECollisionChannel(Channel)))) + TEXT(",");
		ParticipantLines.Add(FString::Printf(TEXT("%s|%s|%s|%s|%u|%u|%s|%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
			*Component->GetClass()->GetPathName(), *Component->GetFName().ToString(), *Component->GetCollisionProfileName().ToString(),
			*TransformKey(ToRoot), uint8(Component->GetCollisionObjectType()), uint8(Component->GetCollisionEnabled()), *Responses,
			Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z));
	}

	// Collision alone is insufficient for a strict transaction: visual or other
	// non-query primitives can extend farther than the capsule used by UE's spawn
	// test. Freeze their CDO envelope before Chance/Amount/Weight so the surface
	// reservation encloses the same component class later verified after spawn.
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	CDO->GetComponents(PrimitiveComponents);
	if (PrimitiveComponents.Num() > 255)
	{
		OutError = TEXT("The content actor CDO exceeds the 255-primitive reservation-envelope bound.");
		return false;
	}
	for (UPrimitiveComponent* const Component : PrimitiveComponents)
	{
		if (!IsValid(Component))
		{
			OutError = TEXT("The content actor CDO has an invalid primitive component.");
			return false;
		}
		FTransform ToRoot;
		if (!RootScene || !ComponentToRoot(Component, RootScene, ToRoot))
		{
			OutError = TEXT("A content primitive has no finite bounded attachment path to the actor root.");
			return false;
		}
		const FBox Bounds = BoundsBox(*Component, ToRoot * OutContract.RootRelativeTransform);
		if (!Bounds.IsValid || !Finite(Bounds.Min) || !Finite(Bounds.Max))
		{
			OutError = TEXT("A content primitive has nonfinite CDO bounds.");
			return false;
		}
		if (!Bounds.GetExtent().IsNearlyZero())
		{
			OutContract.LocalPrimitiveBounds += Bounds;
		}
	}
	if (!AddDeferredMeshBounds(CDO, RootScene, OutContract, OutError))
	{
		return false;
	}
	OutContract.bHasPrimitiveBounds = OutContract.LocalPrimitiveBounds.IsValid != 0;
	ParticipantLines.Sort();
	OutContract.bHasQueryCollision = !Participants.IsEmpty();
	OutContract.QueryParticipantsHash = FMD5::HashAnsiString(*FString::Join(ParticipantLines, TEXT("\n")));
	if (OutContract.bHasQueryCollision)
	{
		if (!OutContract.LocalRootBounds.IsValid || !Finite(OutContract.LocalRootBounds.Min)
			|| !Finite(OutContract.LocalRootBounds.Max) || OutContract.LocalRootBounds.GetExtent().IsNearlyZero())
		{
			OutError = TEXT("The content actor CDO collision participant union has no finite nonzero bounds.");
			return false;
		}
	}
	OutContract.Hash = CanonicalHash(OutContract);
	if (!OutContract.IsValid())
	{
		OutError = TEXT("The content actor collision contract could not be canonicalized.");
		return false;
	}
	return true;
}
