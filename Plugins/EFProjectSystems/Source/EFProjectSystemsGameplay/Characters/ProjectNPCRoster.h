#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Pawn.h"
#include "ProjectNPCRoster.generated.h"

USTRUCT(BlueprintType)
struct FProjectNPCArchetype
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Role;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftClassPtr<APawn> Male;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftClassPtr<APawn> Female;
};

/** Project-owned spawn choices. Source/vendor paths live only in the authoring manifest. */
UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectNPCRoster : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(TitleProperty="Id")) TArray<FProjectNPCArchetype> Archetypes;
};
