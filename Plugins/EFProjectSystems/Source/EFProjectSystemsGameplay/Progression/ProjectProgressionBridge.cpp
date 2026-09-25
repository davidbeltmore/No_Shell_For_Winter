#include "Progression/ProjectProgressionBridge.h"
#include "Progression/ProjectDoctrineAttributeSet.h"
#include "ACFGASStatisticsComponent.h"
#include "ACFPrimaryAttributeSet.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

TArray<FGameplayAttribute> FProjectProgressionBridge::GetPurchasableAttributes()
{
 return {
 FGameplayAttribute(FindFProperty<FProperty>(UACFPrimaryAttributeSet::StaticClass(), TEXT("Strength"))),
 FGameplayAttribute(FindFProperty<FProperty>(UACFPrimaryAttributeSet::StaticClass(), TEXT("Intelligence"))),
 FGameplayAttribute(FindFProperty<FProperty>(UACFPrimaryAttributeSet::StaticClass(), TEXT("Endurance"))),
 FGameplayAttribute(FindFProperty<FProperty>(UACFPrimaryAttributeSet::StaticClass(), TEXT("Constitution"))),
 FGameplayAttribute(FindFProperty<FProperty>(UACFPrimaryAttributeSet::StaticClass(), TEXT("Dexterity"))),
 UProjectDoctrineAttributeSet::GetWillpowerAttribute(), UProjectDoctrineAttributeSet::GetOffensiveAttribute(),
 UProjectDoctrineAttributeSet::GetDefensiveAttribute(), UProjectDoctrineAttributeSet::GetFaithAttribute(),
 UProjectDoctrineAttributeSet::GetCunningAttribute(), UProjectDoctrineAttributeSet::GetCelerityAttribute(),
 UProjectDoctrineAttributeSet::GetCharismaAttribute() };
}

bool FProjectProgressionBridge::Purchase(UACFGASStatisticsComponent* Stats, FGameplayAttribute Attribute, int32 Amount)
{
 if (!IsValid(Stats) || !Stats->GetOwner() || !Stats->GetOwner()->HasAuthority() ||
     Amount <= 0 || Amount > 100 || !Attribute.IsValid() || !GetPurchasableAttributes().Contains(Attribute) ||
     Stats->GetLevelingType() != ELevelingType::EAssignPerksManually) return false;
 UAbilitySystemComponent* ASC = Stats->GetOwnerAbilityComponent();
 if (!ASC || !ASC->HasAttributeSetForAttribute(Attribute) || Stats->GetAvailablePerks() < Amount) return false;
 const float Base = ASC->GetNumericAttributeBase(Attribute);
 if (!FMath::IsFinite(Base) || Base < 0.f || Base > 100.f - Amount || !FMath::IsNearlyEqual(Base, FMath::RoundToFloat(Base))) return false;
 Stats->AssignPerkToAttribute(Attribute, Amount);
 return FMath::IsNearlyEqual(ASC->GetNumericAttributeBase(Attribute), Base + Amount);
}

bool FProjectProgressionBridge::GrantExperience(UACFGASStatisticsComponent* Stats, int32 Amount, int32 MaxLevel)
{
 if (!IsValid(Stats) || !Stats->GetOwner() || !Stats->GetOwner()->HasAuthority() || Amount <= 0 ||
     MaxLevel < 2 || MaxLevel > 255 || !Stats->CanLevelUp() || !Stats->GetExpForNextLevelCurve()) return false;
 const int32 Level = Stats->GetCurrentLevel();
 const int32 XP = Stats->GetCurrentExp();
 if (Level < 1 || Level >= MaxLevel || XP < 0 || XP >= Stats->GetTotaleExpToNextLevel() ||
     int64(XP) + Amount > MAX_int32 || Stats->GetAvailablePerks() < 0) return false;
 // Validate every threshold reachable by native recursive AddExp, including the cap.
 int64 BudgetToCap = -int64(XP);
 for (int32 L = Level; L <= MaxLevel; ++L)
 {
  const float Threshold = Stats->GetExpForNextLevelCurve()->GetFloatValue(L);
  if (!FMath::IsFinite(Threshold) || Threshold < 1.f || double(Threshold) > double(MAX_int32)) return false;
  if (L < MaxLevel) BudgetToCap += int64(Threshold);
 }
 if (Stats->GetTotaleExpToNextLevel() != Stats->GetTotalExpsForLevel(Level) || BudgetToCap <= 0) return false;
 // Discard excess at cap. Do not create another level counter.
 Stats->AddExp(int32(FMath::Min(int64(Amount), BudgetToCap)));
 return true;
}
