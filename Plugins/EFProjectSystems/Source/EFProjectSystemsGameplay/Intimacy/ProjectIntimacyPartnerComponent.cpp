#include "Intimacy/ProjectIntimacyPartnerComponent.h"

#include "Characters/ProjectEnemyLevelComponent.h"
#include "GameFramework/Actor.h"
#include "Intimacy/ProjectIntimacySettings.h"

UProjectIntimacyPartnerComponent::UProjectIntimacyPartnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProjectIntimacyPartnerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Personality == EProjectIntimacyPersonality::Auto)
	{
		Personality = ResolveAutoPersonality();
	}
}

UProjectIntimacyPartnerComponent* UProjectIntimacyPartnerComponent::FindOrCreateForActor(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (UProjectIntimacyPartnerComponent* ExistingComponent = Actor->FindComponentByClass<UProjectIntimacyPartnerComponent>())
	{
		return ExistingComponent;
	}

	UProjectIntimacyPartnerComponent* NewComponent = NewObject<UProjectIntimacyPartnerComponent>(
		Actor,
		UProjectIntimacyPartnerComponent::StaticClass(),
		TEXT("ProjectIntimacyPartnerComponent"));
	if (!NewComponent)
	{
		return nullptr;
	}

	Actor->AddInstanceComponent(NewComponent);
	NewComponent->RegisterComponent();
	return NewComponent;
}

FString UProjectIntimacyPartnerComponent::GetResolvedPartnerId() const
{
	if (!PartnerId.IsEmpty())
	{
		return PartnerId;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return TEXT("UnknownPartner");
	}

	const FString ClassPath = Owner->GetClass() ? Owner->GetClass()->GetPathName() : FString(TEXT("UnknownClass"));
	return FString::Printf(TEXT("%s:%s"), *ClassPath, *Owner->GetFName().ToString());
}

FText UProjectIntimacyPartnerComponent::GetPartnerDisplayName() const
{
	if (!DisplayNameOverride.IsEmpty())
	{
		return DisplayNameOverride;
	}

	const AActor* Owner = GetOwner();
	return Owner ? FText::FromString(Owner->GetName()) : FText::FromString(TEXT("Partner"));
}

EProjectIntimacyPersonality UProjectIntimacyPartnerComponent::GetResolvedPersonality() const
{
	return Personality == EProjectIntimacyPersonality::Auto ? ResolveAutoPersonality() : Personality;
}

FGameplayTag UProjectIntimacyPartnerComponent::GetResolvedGenderTag() const
{
	if (GenderTag.IsValid())
	{
		return GenderTag;
	}

	return FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Male"), false);
}

int32 UProjectIntimacyPartnerComponent::GetPartnerLevel() const
{
	const AActor* Owner = GetOwner();
	if (const UProjectEnemyLevelComponent* LevelComponent = Owner ? Owner->FindComponentByClass<UProjectEnemyLevelComponent>() : nullptr)
	{
		if (LevelComponent->HasAssignedLevel())
		{
			return FMath::Max(1, LevelComponent->GetAssignedLevel());
		}
	}

	return 1;
}

float UProjectIntimacyPartnerComponent::GetDefaultAnimationRate() const
{
	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	switch (GetResolvedPersonality())
	{
	case EProjectIntimacyPersonality::Chill:
		return Settings ? Settings->ChillAnimationRate : 0.75f;
	case EProjectIntimacyPersonality::Stallion:
		return Settings ? Settings->StallionAnimationRate : 1.35f;
	case EProjectIntimacyPersonality::Nice:
	case EProjectIntimacyPersonality::Auto:
	default:
		return Settings ? Settings->NiceAnimationRate : 1.0f;
	}
}

FText UProjectIntimacyPartnerComponent::PersonalityToText(const EProjectIntimacyPersonality InPersonality)
{
	switch (InPersonality)
	{
	case EProjectIntimacyPersonality::Chill:
		return FText::FromString(TEXT("Chill"));
	case EProjectIntimacyPersonality::Nice:
		return FText::FromString(TEXT("Nice"));
	case EProjectIntimacyPersonality::Stallion:
		return FText::FromString(TEXT("Stallion"));
	case EProjectIntimacyPersonality::Auto:
	default:
		return FText::FromString(TEXT("Auto"));
	}
}

FText UProjectIntimacyPartnerComponent::RelationshipToText(const EProjectIntimacyRelationship InRelationship)
{
	switch (InRelationship)
	{
	case EProjectIntimacyRelationship::Familiar:
		return FText::FromString(TEXT("Familiar"));
	case EProjectIntimacyRelationship::Close:
		return FText::FromString(TEXT("Close"));
	case EProjectIntimacyRelationship::Ally:
		return FText::FromString(TEXT("Ally"));
	case EProjectIntimacyRelationship::Unknown:
	default:
		return FText::FromString(TEXT("Unknown"));
	}
}

FText UProjectIntimacyPartnerComponent::GenderTagToText(const FGameplayTag InGenderTag)
{
	if (InGenderTag.MatchesTagExact(FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Male"), false)))
	{
		return FText::FromString(TEXT("Male"));
	}
	if (InGenderTag.MatchesTagExact(FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Female"), false)))
	{
		return FText::FromString(TEXT("Female"));
	}
	return FText::FromString(TEXT("Unknown"));
}

FText UProjectIntimacyPartnerComponent::RelationshipTagsToText(const FGameplayTagContainer& InRelationshipTags)
{
	struct FRelationshipLabel
	{
		const TCHAR* Tag;
		const TCHAR* Label;
	};

	static const FRelationshipLabel Labels[] =
	{
		{ TEXT("Project.Intimacy.Relationship.Partner"), TEXT("Partner") },
		{ TEXT("Project.Intimacy.Relationship.Devoted"), TEXT("Devoted") },
		{ TEXT("Project.Intimacy.Relationship.Attached"), TEXT("Attached") },
		{ TEXT("Project.Intimacy.Relationship.Interested"), TEXT("Interested") },
		{ TEXT("Project.Intimacy.Relationship.Unknown"), TEXT("Unknown") },
		{ TEXT("Project.Intimacy.Relationship.Husband"), TEXT("Husband") },
	};

	TArray<FString> Parts;
	for (const FRelationshipLabel& Label : Labels)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Label.Tag, false);
		if (Tag.IsValid() && InRelationshipTags.HasTagExact(Tag))
		{
			Parts.Add(Label.Label);
		}
	}

	if (Parts.Num() <= 0)
	{
		return FText::FromString(TEXT("Unknown"));
	}

	return FText::FromString(FString::Join(Parts, TEXT(", ")));
}

EProjectIntimacyPersonality UProjectIntimacyPartnerComponent::ResolveAutoPersonality() const
{
	const uint32 Hash = GetTypeHash(GetResolvedPartnerId());
	switch (Hash % 3)
	{
	case 0:
		return EProjectIntimacyPersonality::Chill;
	case 1:
		return EProjectIntimacyPersonality::Nice;
	default:
		return EProjectIntimacyPersonality::Stallion;
	}
}
