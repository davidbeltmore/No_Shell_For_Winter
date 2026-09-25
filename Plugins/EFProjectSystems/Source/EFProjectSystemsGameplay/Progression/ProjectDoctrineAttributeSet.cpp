#include "Progression/ProjectDoctrineAttributeSet.h"
#include "Net/UnrealNetwork.h"
void UProjectDoctrineAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Willpower, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Offensive, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Defensive, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Faith, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Cunning, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Celerity, COND_None, REPNOTIFY_Always);
 DOREPLIFETIME_CONDITION_NOTIFY(UProjectDoctrineAttributeSet, Charisma, COND_None, REPNOTIFY_Always);
}
