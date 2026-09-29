#include "Progression/ProjectSplineAIController.h"
#include "Components/ACFSplineFollowerComponent.h"

FPathFollowingRequestResult AProjectSplineAIController::MoveTo(const FAIMoveRequest& MoveRequest,
    FNavPathSharedPtr* OutPath)
{
    const TWeakObjectPtr<UACFSplineFollowerComponent> Follower = FindComponentByClass<UACFSplineFollowerComponent>();
    const bool WasFollowing = Follower.IsValid() && Follower->IsFollowing();
    const int32 PreviousSegment = WasFollowing ? Follower->GetCurrentSegmentIndex() : INDEX_NONE;
    auto Result = Super::MoveTo(MoveRequest, OutPath);

    // AAIController broadcasts a successful completion synchronously for AlreadyAtGoal.
    // ACF consumes that callback, advances the segment and may start another MoveTo
    // before Super returns. Its caller must not advance again for the old request.
    // Preserve AlreadyAtGoal when no spline callback actually consumed the arrival.
    if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal && WasFollowing &&
        (!Follower.IsValid() || !Follower->IsFollowing() || Follower->GetCurrentSegmentIndex() != PreviousSegment))
    {
        Result.Code = EPathFollowingRequestResult::RequestSuccessful;
    }
    return Result;
}
