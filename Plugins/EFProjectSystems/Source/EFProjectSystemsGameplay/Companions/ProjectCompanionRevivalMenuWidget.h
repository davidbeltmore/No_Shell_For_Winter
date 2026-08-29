#pragma once

#include "CoreMinimal.h"
#include "UI/ProjectEmoteMenuWidget.h"
#include "Companions/ProjectRunCompanionTypes.h"
#include "ProjectCompanionRevivalMenuWidget.generated.h"

/**
 * Native, project-owned selection surface for Winter's Recall. It deliberately
 * reuses the established Emote Menu navigation and visual language. The widget
 * owns presentation only; the GameInstance subsystem owns the transaction.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCompanionRevivalMenuWidget : public UProjectEmoteMenuWidget
{
	GENERATED_BODY()

public:
	bool ConfigureCandidates(const TArray<FProjectCompanionRevivalCandidate>& InCandidates);
	bool TryResolveCandidate(FName OptionId, FGuid& OutStableCompanionId) const;
	static FName GetCancelOptionId();

private:
	TMap<FName, FGuid> CandidateByOptionId;
};

