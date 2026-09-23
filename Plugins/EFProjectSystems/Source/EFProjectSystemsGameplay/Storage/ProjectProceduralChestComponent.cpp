#include "Storage/ProjectProceduralChestComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "ALSLoadAndSaveComponent.h"
#include "UObject/UnrealType.h"

UProjectProceduralChestComponent::UProjectProceduralChestComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bEditableWhenInherited = true;
}
void UProjectProceduralChestComponent::OnRegister()
{
    Super::OnRegister();
    if (GetOwner() && GetWorld() && GetWorld()->IsGameWorld())
    {
        // Explicit whole-world load remains supported; implicit actor-name
        // reload must not override the stable chest identity snapshot.
        auto* Flag = FindFProperty<FBoolProperty>(UALSLoadAndSaveComponent::StaticClass(), TEXT("bAutoReload"));
        TArray<UALSLoadAndSaveComponent*> Components; GetOwner()->GetComponents(Components);
        if (Flag) for (auto* Component : Components) Flag->SetPropertyValue_InContainer(Component, false);
    }
#if WITH_EDITOR
    if (GetOwner() && GetWorld() && !GetWorld()->IsGameWorld() && !GetOwner()->IsTemplate())
        StableIdentity = GetOwner()->GetActorGuid();
#endif
}
bool UProjectProceduralChestComponent::PreviewContents(TArray<FBaseItem>& Contents, FString& Error) const
{
    Contents.Reset();
    if (!bOverrideContent && !Profile) { Error = TEXT("Select a content Profile or enable Override Content."); return false; }
    return FProjectChestSelector::Generate(bOverrideContent ? ContentOverride : Profile->Content, StableIdentity, Seed, Contents, Error);
}
void UProjectProceduralChestComponent::ValidateConfiguration()
{
    TArray<FBaseItem> Preview; FString Error;
    if (!bOverrideContent && !Profile) { ValidationResult = TEXT("Select a Profile or enable Override Content."); return; }
    const bool Valid = FProjectChestSelector::Generate(bOverrideContent ? ContentOverride : Profile->Content,
        StableIdentity.IsValid() ? StableIdentity : FGuid(1,0,0,0), Seed, Preview, Error);
    ValidationResult = Valid ? TEXT("Valid: percentages total 100%; all counts and tiers supported. Instances require a stable identity. Inventory unchanged.") : Error;
}
