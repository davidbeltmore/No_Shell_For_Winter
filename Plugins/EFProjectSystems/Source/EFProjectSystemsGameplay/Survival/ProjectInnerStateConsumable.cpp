#include "Survival/ProjectInnerStateConsumable.h"
#include "Survival/ProjectSurvivalConsumableBlueprintLibrary.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "GameFramework/Pawn.h"

UProjectInnerStateConsumable::UProjectInnerStateConsumable()
{
    ItemInfo.ItemType=EItemType::Consumable;
    ItemInfo.MaxInventoryStack=10;
    ItemInfo.ItemWeight=.2f;
    bConsumeOnUse=true;
}
bool UProjectInnerStateConsumable::CanBeUsed_Implementation(const APawn* Pawn) const
{
    const auto* Inner=Pawn?Pawn->FindComponentByClass<UProjectInnerStateComponent>():nullptr;
    return Inner && !Inner->State.bUnconscious && !Inner->State.bFatalAsphyxia;
}
void UProjectInnerStateConsumable::OnItemUsed_Implementation(APawn* Target)
{
    if (!Target || !Target->HasAuthority() || !CanBeUsed(Target)) return;
    auto* Inner=Target->FindComponentByClass<UProjectInnerStateComponent>();
    if (bAntidote) Inner->UseAntidote(Toxin);
    if (bPartialWash) Inner->Wash(false);
    if (SicknessTreatmentHours>0) Inner->TreatSickness(SicknessTreatmentHours);
    if (PoisonPoints>0) Inner->AddPoison(PoisonPoints,PoisonSpeed,Toxin,FGuid::NewGuid());
    for (const auto& Effect:Influences) Inner->SetInfluence(Effect);
    if (HealthRestoreFraction>0)
        if (auto* Resources=Target->FindComponentByClass<UProjectRealtimeSnapshotComponent>())
        {
            float Current=0,Maximum=0;
            if (Resources->TryReadOwnerResource(TEXT("Health"),Current,Maximum))
                Resources->ApplyOwnerResourceDelta(TEXT("Health"),static_cast<float>(Maximum*FMath::Clamp(HealthRestoreFraction,0.0,1.0)));
        }
    if (!SurvivalProfile.NeedDeltas.IsEmpty() || !SurvivalProfile.SensationDeltas.IsEmpty()
        || !SurvivalProfile.TimedNeedDeltas.IsEmpty() || !SurvivalProfile.TimedSensationDeltas.IsEmpty())
        UProjectSurvivalConsumableBlueprintLibrary::ApplySurvivalConsumableProfile(Target,this,SurvivalProfile);
}
