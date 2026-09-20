#include "UI/EFCharacterCreationSectionButton.h"

void UEFCharacterCreationSectionButton::InitializeSection(const FString& InSection)
{
	Section = InSection;
	OnClicked.AddUniqueDynamic(this, &ThisClass::DispatchSection);
}

void UEFCharacterCreationSectionButton::DispatchSection()
{
	OnSectionSelected.ExecuteIfBound(Section);
}
