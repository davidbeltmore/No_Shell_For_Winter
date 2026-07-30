#pragma once

#include "CoreMinimal.h"
#include "Characters/ProjectEnemyCombatStatTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Intimacy/ProjectIntimacyTypes.h"
#include "ProjectIntimacyDialogueLibrary.generated.h"

class UDataTable;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectIntimacyDialogueLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	static FGameplayTag GetPersonalityTag(EProjectIntimacyPersonality Personality);

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	static FGameplayTag GetTalkStyleTag(FName StyleName);

	static void BuildPreferredTalkTags(
		EProjectIntimacyPersonality Personality,
		const FGameplayTagContainer& RelationshipTags,
		FGameplayTagContainer& OutPreferredTags);
	static int32 ScoreTalkOptionForPreferredTags(
		const FProjectIntimacyTalkOptionRow& Option,
		const FGameplayTagContainer& PreferredTags);
	static void BuildRelationshipTagsFromCounters(
		int32 Encounters,
		FGameplayTag GenderTag,
		bool bHasHusbandRing,
		FGameplayTagContainer& OutRelationshipTags);
	static bool RelationshipTagsForceChill(const FGameplayTagContainer& RelationshipTags);
	static void BuildFallbackTalkOptions(TArray<FProjectIntimacyTalkOptionRow>& OutOptions);
	static void BuildFallbackMediaCues(TArray<FProjectIntimacyMediaCueRow>& OutRows);
	static void BuildFallbackSocialCardRows(TArray<FProjectSocialCardRow>& OutRows);
	static FText ResolvePartnerResponse(
		UDataTable* ResponseTable,
		FName OptionId,
		EProjectIntimacyPersonality Personality,
		bool bAccepted);
};
