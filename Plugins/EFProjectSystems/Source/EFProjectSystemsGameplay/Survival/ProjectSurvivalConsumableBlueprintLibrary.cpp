#include "Survival/ProjectSurvivalConsumableBlueprintLibrary.h"

#include "EFProjectSurvivalSettings.h"
#include "Survival/ProjectSurvivalConsumableEffectComponent.h"
#include "Survival/ProjectSurvivalConsumableRegistry.h"
#include "Survival/ProjectSurvivalLog.h"
#include "GameFramework/Pawn.h"

namespace
{
	FName NormalizeRegistryIdCandidate(const FString& RawName)
	{
		FString NormalizedName = RawName;
		NormalizedName.RemoveFromStart(TEXT("Default__"));
		NormalizedName.RemoveFromEnd(TEXT("_C"));
		return FName(*NormalizedName);
	}

	TArray<FName> BuildRegistryCandidates(const UObject* SourceAsset)
	{
		TArray<FName> Candidates;
		if (!SourceAsset)
		{
			return Candidates;
		}

		Candidates.AddUnique(NormalizeRegistryIdCandidate(SourceAsset->GetName()));

		if (const UClass* SourceClass = SourceAsset->GetClass())
		{
			Candidates.AddUnique(NormalizeRegistryIdCandidate(SourceClass->GetName()));
		}

		return Candidates;
	}

	UProjectSurvivalConsumableRegistry* LoadConsumableRegistryFromSettings()
	{
		const UEFProjectSurvivalSettings* Settings = UEFProjectSurvivalSettings::Get();
		if (!Settings)
		{
			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumable] Missing EFProjectSurvivalSettings while resolving the consumable registry"));
			return nullptr;
		}

		const FString RegistryPath = Settings->ConsumableRegistry.ToString();
		if (RegistryPath.IsEmpty())
		{
			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumable] Consumable registry path is empty in EFProjectSurvivalSettings"));
			return nullptr;
		}

		UObject* RegistryObject = Settings->ConsumableRegistry.TryLoad();
		UProjectSurvivalConsumableRegistry* Registry = Cast<UProjectSurvivalConsumableRegistry>(RegistryObject);
		if (!Registry)
		{
			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumable] Failed to load consumable registry from settings path %s"),
				*RegistryPath);
			return nullptr;
		}

		return Registry;
	}
}

bool UProjectSurvivalConsumableBlueprintLibrary::ApplySurvivalConsumableProfile(
	APawn* Consumer,
	UObject* SourceAsset,
	const FProjectSurvivalConsumableProfile& Profile)
{
	if (!Consumer)
	{
		UE_LOG(
			LogProjectSurvival,
			Warning,
			TEXT("[ProjectSurvivalConsumable] ApplySurvivalConsumableProfile failed because Consumer was null for %s"),
			*GetNameSafe(SourceAsset));
		return false;
	}

	UProjectSurvivalConsumableEffectComponent* EffectComponent = Consumer->FindComponentByClass<UProjectSurvivalConsumableEffectComponent>();
	if (!EffectComponent)
	{
		EffectComponent = NewObject<UProjectSurvivalConsumableEffectComponent>(
			Consumer,
			UProjectSurvivalConsumableEffectComponent::StaticClass(),
			TEXT("ProjectSurvivalConsumableEffectComponent"));
		if (!EffectComponent)
		{
			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumable] Failed to create consumable effect component for %s"),
				*GetNameSafe(Consumer));
			return false;
		}

		Consumer->AddInstanceComponent(EffectComponent);
		EffectComponent->OnComponentCreated();
		EffectComponent->RegisterComponent();
		EffectComponent->Activate(true);
	}

	return EffectComponent->ApplySurvivalConsumableProfile(Profile, SourceAsset);
}

bool UProjectSurvivalConsumableBlueprintLibrary::ApplySurvivalConsumableFromSource(APawn* Consumer, UObject* SourceAsset)
{
	if (!Consumer || !SourceAsset)
	{
		UE_LOG(
			LogProjectSurvival,
			Warning,
			TEXT("[ProjectSurvivalConsumable] ApplySurvivalConsumableFromSource failed Consumer=%s Source=%s"),
			*GetNameSafe(Consumer),
			*GetNameSafe(SourceAsset));
		return false;
	}

	UProjectSurvivalConsumableRegistry* Registry = LoadConsumableRegistryFromSettings();
	if (!Registry)
	{
		return false;
	}

	FProjectSurvivalConsumableProfile ResolvedProfile;
	for (const FName CandidateId : BuildRegistryCandidates(SourceAsset))
	{
		if (Registry->FindProfileByRegistryId(CandidateId, ResolvedProfile))
		{
			UE_LOG(
				LogProjectSurvival,
				Log,
				TEXT("[ProjectSurvivalConsumable] Resolved profile RegistryId=%s for Source=%s"),
				*CandidateId.ToString(),
				*GetNameSafe(SourceAsset));
			return ApplySurvivalConsumableProfile(Consumer, SourceAsset, ResolvedProfile);
		}
	}

	UE_LOG(
		LogProjectSurvival,
		Warning,
		TEXT("[ProjectSurvivalConsumable] No registry entry found for Source=%s Class=%s"),
		*GetNameSafe(SourceAsset),
		*GetNameSafe(SourceAsset->GetClass()));
	return false;
}
