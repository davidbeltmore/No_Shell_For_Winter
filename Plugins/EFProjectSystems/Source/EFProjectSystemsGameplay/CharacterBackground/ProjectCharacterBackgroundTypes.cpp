#include "CharacterBackground/ProjectCharacterBackgroundTypes.h"

#define LOCTEXT_NAMESPACE "ProjectCharacterBackgroundTypes"

namespace ProjectCharacterBackground
{
	bool TryResolveDoctrineAttribute(const FName AttributeID, EProjectDoctrineAttribute& OutAttribute)
	{
		const FString AttributeString = AttributeID.ToString();

		if (AttributeString.Equals(TEXT("Willpower"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Willpower;
			return true;
		}
		if (AttributeString.Equals(TEXT("Offensive"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Offensive;
			return true;
		}
		if (AttributeString.Equals(TEXT("Defensive"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Defensive;
			return true;
		}
		if (AttributeString.Equals(TEXT("Faith"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Faith;
			return true;
		}
		if (AttributeString.Equals(TEXT("Cunning"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Cunning;
			return true;
		}
		if (AttributeString.Equals(TEXT("Celerity"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Celerity;
			return true;
		}
		if (AttributeString.Equals(TEXT("Charisma"), ESearchCase::IgnoreCase))
		{
			OutAttribute = EProjectDoctrineAttribute::Charisma;
			return true;
		}

		return false;
	}

	FName GetDoctrineAttributeID(const EProjectDoctrineAttribute Attribute)
	{
		switch (Attribute)
		{
		case EProjectDoctrineAttribute::Willpower:
			return TEXT("Willpower");
		case EProjectDoctrineAttribute::Offensive:
			return TEXT("Offensive");
		case EProjectDoctrineAttribute::Defensive:
			return TEXT("Defensive");
		case EProjectDoctrineAttribute::Faith:
			return TEXT("Faith");
		case EProjectDoctrineAttribute::Cunning:
			return TEXT("Cunning");
		case EProjectDoctrineAttribute::Celerity:
			return TEXT("Celerity");
		case EProjectDoctrineAttribute::Charisma:
			return TEXT("Charisma");
		default:
			return NAME_None;
		}
	}

	FText GetDoctrineAttributeDisplayText(const EProjectDoctrineAttribute Attribute)
	{
		switch (Attribute)
		{
		case EProjectDoctrineAttribute::Willpower:
			return LOCTEXT("Willpower", "Willpower");
		case EProjectDoctrineAttribute::Offensive:
			return LOCTEXT("Offensive", "Offensive");
		case EProjectDoctrineAttribute::Defensive:
			return LOCTEXT("Defensive", "Defensive");
		case EProjectDoctrineAttribute::Faith:
			return LOCTEXT("Faith", "Faith");
		case EProjectDoctrineAttribute::Cunning:
			return LOCTEXT("Cunning", "Cunning");
		case EProjectDoctrineAttribute::Celerity:
			return LOCTEXT("Celerity", "Celerity");
		case EProjectDoctrineAttribute::Charisma:
			return LOCTEXT("Charisma", "Charisma");
		default:
			return LOCTEXT("UnknownAttribute", "Unknown");
		}
	}
}

#undef LOCTEXT_NAMESPACE
