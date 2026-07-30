#pragma once

#include "CoreMinimal.h"
#include "ContentPolicy/ProjectContentPolicyTypes.h"
#include "UObject/Interface.h"
#include "ProjectOptionalMatureContentProvider.generated.h"

/**
 * C++-only presentation boundary. Blueprint and gameplay callers request
 * presentations through UProjectContentPolicySubsystem so policy cannot be
 * bypassed by invoking a provider event directly.
 */
UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectOptionalMatureContentProvider : public UInterface
{
	GENERATED_BODY()
};

class EFPROJECTSYSTEMSGAMEPLAY_API IProjectOptionalMatureContentProvider
{
	GENERATED_BODY()

public:
	virtual bool SupportsMatureFeature(EProjectOptionalMatureFeature Feature) const = 0;
	virtual bool IsMatureFeatureAvailable(EProjectOptionalMatureFeature Feature) const = 0;
	virtual bool TryBeginMaturePresentation(const FProjectMaturePresentationRequest& Request) = 0;
	virtual void CancelMaturePresentation(const FGuid& RequestId) = 0;
};
