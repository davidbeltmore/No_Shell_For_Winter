#include "UI/EFBodyPresetCheckBox.h"
#include "UI/EFCharacterCreationRootWidget.h"

void UEFBodyPresetCheckBox::InitializeChoice(UEFCharacterCreationRootWidget* InOwner, FName InPresetId, bool bSelected)
{
	Owner=InOwner;
	PresetId=InPresetId;
	SetIsChecked(bSelected);
	OnCheckStateChanged.AddUniqueDynamic(this, &UEFBodyPresetCheckBox::HandleChoiceChanged);
}

void UEFBodyPresetCheckBox::HandleChoiceChanged(bool bSelected)
{
	if (!bSelected) { SetIsChecked(true); return; }
	if (Owner.IsValid()) Owner->SelectBodyPresetFromUI(PresetId);
}
