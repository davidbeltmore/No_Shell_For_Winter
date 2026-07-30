#include "AbilitySystemComponent.h"
#include "Actors/ACFCharacter.h"
#include "EFCharacterCreationGameplayHooks.h"
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
	}

	virtual void ShutdownModule() override
	{
		if (IdentityChangedHandle.IsValid())
		{
			EFCharacterCreationGameplayHooks::OnIdentityChanged().Remove(IdentityChangedHandle);
			IdentityChangedHandle.Reset();
		}
	}

private:
	FDelegateHandle IdentityChangedHandle;
};

IMPLEMENT_MODULE(FEFProjectSystemsGameplayModule, EFProjectSystemsGameplay)
