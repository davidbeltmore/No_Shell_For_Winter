#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "ProjectEmoteMenuDataAsset.generated.h"

UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteMenuDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Project|Emote|Runtime")
	FProjectEmoteSceneRuntimeSettings RuntimeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Project|Emote|Menu", meta = (TitleProperty = "NodeId"))
	TArray<FProjectEmoteMenuNodeDefinition> Nodes;
};
