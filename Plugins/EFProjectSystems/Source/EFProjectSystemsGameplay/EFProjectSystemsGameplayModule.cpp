#include "AbilitySystemComponent.h"
#include "Actors/ACFCharacter.h"
#include "Calysto/ProjectCalystoPopulationBridgeV4.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"
#include "Modules/ModuleManager.h"

namespace ProjectSystemsGameplayModulePrivate
{
	static FGameplayTag ResolveGenderTag(const ECharacterCreationGender Gender, const FGameplayTag SuppliedTag)
	{
		if (SuppliedTag.IsValid())
		{
			return SuppliedTag;
		}

		switch (Gender)
		{
		case ECharacterCreationGender::Male:
			return FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Male"), false);
		case ECharacterCreationGender::Female:
			return FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Female"), false);
		case ECharacterCreationGender::NotApplicable:
		default:
			return FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Unknown"), false);
		}
	}

	static void HandleCharacterCreationIdentityChanged(
		APawn* Pawn,
		const FString& CharacterName,
		const ECharacterCreationGender Gender,
		const FGameplayTag SuppliedGenderTag)
	{
		if (!IsValid(Pawn))
		{
			return;
		}

		const FString ResolvedName = CharacterName.TrimStartAndEnd().IsEmpty() ? TEXT("Player") : CharacterName.TrimStartAndEnd();
		const FGameplayTag GenderTag = ResolveGenderTag(Gender, SuppliedGenderTag);

		if (AACFCharacter* ACFCharacter = Cast<AACFCharacter>(Pawn))
		{
			ACFCharacter->SetCharacterName(FText::FromString(ResolvedName));
		}

		if (UAbilitySystemComponent* AbilitySystem = Pawn->FindComponentByClass<UAbilitySystemComponent>())
		{
			const FGameplayTag GenderTags[] = {
				FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Male"), false),
				FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Female"), false),
				FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Unknown"), false)
			};

			for (const FGameplayTag CandidateTag : GenderTags)
			{
				if (CandidateTag.IsValid())
				{
					AbilitySystem->SetLooseGameplayTagCount(CandidateTag, CandidateTag == GenderTag ? 1 : 0);
				}
			}
		}

		if (UProjectIntimacyPartnerComponent* PartnerComponent = Pawn->FindComponentByClass<UProjectIntimacyPartnerComponent>())
		{
			PartnerComponent->DisplayNameOverride = FText::FromString(ResolvedName);
			PartnerComponent->GenderTag = GenderTag;
		}
	}
}

class FEFProjectSystemsGameplayModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IdentityChangedHandle = EFCharacterCreationGameplayHooks::OnIdentityChanged().AddStatic(&ProjectSystemsGameplayModulePrivate::HandleCharacterCreationIdentityChanged);
		check(!CalystoPopulationBridgeV4.IsValid());
		CalystoPopulationBridgeV4 = MakeUnique<FProjectCalystoPopulationBridgeV4>();
		IModularFeatures::Get().RegisterModularFeature(
			IEFCalystoPopulationBridgeV4::GetModularFeatureName(),
			CalystoPopulationBridgeV4.Get());
	}

	virtual void ShutdownModule() override
	{
		if (CalystoPopulationBridgeV4.IsValid())
		{
			IModularFeatures::Get().UnregisterModularFeature(
				IEFCalystoPopulationBridgeV4::GetModularFeatureName(),
				CalystoPopulationBridgeV4.Get());
			CalystoPopulationBridgeV4.Reset();
		}
		if (IdentityChangedHandle.IsValid())
		{
			EFCharacterCreationGameplayHooks::OnIdentityChanged().Remove(IdentityChangedHandle);
			IdentityChangedHandle.Reset();
		}
	}

private:
	FDelegateHandle IdentityChangedHandle;
	TUniquePtr<FProjectCalystoPopulationBridgeV4> CalystoPopulationBridgeV4;
};

IMPLEMENT_MODULE(FEFProjectSystemsGameplayModule, EFProjectSystemsGameplay)
