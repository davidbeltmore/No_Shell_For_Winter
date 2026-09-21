#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "EFCalystoDirectorPortal.generated.h"

class UStaticMesh;

UINTERFACE(MinimalAPI)
class UEFCalystoDirectorPortal : public UInterface { GENERATED_BODY() };

/** Travel owns progression identity and the supported door approach point. */
class EFPROCEDURALRUNTIME_API IEFCalystoDirectorPortal
{
	GENERATED_BODY()
public:
	virtual FVector GetDirectorApproachWorld() const = 0;
	virtual void SetDirectorInteractionEnabled(bool bEnabled) = 0;
	virtual bool SetDirectorAppearance(UStaticMesh* Mesh) = 0;
};
