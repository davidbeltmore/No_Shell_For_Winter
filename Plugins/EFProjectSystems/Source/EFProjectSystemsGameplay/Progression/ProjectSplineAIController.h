#pragma once

#include "ACFAIController.h"
#include "ProjectSplineAIController.generated.h"

/** Keeps ACF's synchronous arrival callback from advancing the same spline twice. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectSplineAIController : public AACFAIController
{
    GENERATED_BODY()
public:
    virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest,
        FNavPathSharedPtr* OutPath = nullptr) override;
};
