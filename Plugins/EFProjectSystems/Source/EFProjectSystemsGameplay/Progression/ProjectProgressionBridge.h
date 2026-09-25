#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"

class UACFGASStatisticsComponent;

// Server-side pilot entry points. No XP, level or currency is stored here.
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectProgressionBridge
{
 static bool Purchase(UACFGASStatisticsComponent* Stats, FGameplayAttribute Attribute, int32 Amount = 1);
 static bool GrantExperience(UACFGASStatisticsComponent* Stats, int32 Amount, int32 MaxLevel = 100);
 static TArray<FGameplayAttribute> GetPurchasableAttributes();
};
