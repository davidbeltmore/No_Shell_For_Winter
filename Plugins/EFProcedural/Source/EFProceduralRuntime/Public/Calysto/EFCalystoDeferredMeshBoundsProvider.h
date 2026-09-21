#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "EFCalystoDeferredMeshBoundsProvider.generated.h"

/** Runtime declaration of a soft visual mesh and its existing CDO attachment. */
struct FEFCalystoDeferredMeshBinding
{
	FName MeshProperty;
	FName AttachmentComponent;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UEFCalystoDeferredMeshBoundsProvider : public UInterface
{
	GENERATED_BODY()
};

/** Cooked builds retain these declarations; editor metadata is not runtime authority. */
class EFPROCEDURALRUNTIME_API IEFCalystoDeferredMeshBoundsProvider
{
	GENERATED_BODY()
public:
	virtual void GetDeferredMeshBoundsBindings(TArray<FEFCalystoDeferredMeshBinding>& OutBindings) const = 0;
};
