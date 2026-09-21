#include "Calysto/EFCalystoTraitBindingDetails.h"

#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

namespace
{
	FGuid ReadIdentity(const TSharedPtr<IPropertyHandle>& Handle)
	{
		TArray<void*> Raw; Handle->AccessRawData(Raw);
		return Raw.Num()==1 && Raw[0] ? *static_cast<const FGuid*>(Raw[0]) : FGuid();
	}
	TArray<TPair<FGuid,FText>> Entries(const TSharedRef<IPropertyHandle>& Property)
	{
		TArray<TPair<FGuid,FText>> Result; TSet<FGuid> Seen;
		uint8 Role=0; Property->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEFCalystoTraitBinding,Role))->GetValue(Role);
		TArray<UObject*> Objects; Property->GetOuterObjects(Objects);
		if (Objects.Num()!=1) return Result;
		const auto* Asset=Cast<UEFCalystoDungeonDirectorAsset>(Objects[0]); if (!Asset) return Result;
		const auto Add=[&](const auto& Profiles)
		{
			for (const auto& Profile:Profiles) for (const auto& Group:Profile.Content)
				if (uint8(Group.Role)==Role) for (const auto& Entry:Group.Entries)
					if (Entry.Selection.Id.IsValid() && !Seen.Contains(Entry.Selection.Id))
					{
						Seen.Add(Entry.Selection.Id);
						Result.Emplace(Entry.Selection.Id,FText::FromString(Profile.Selection.DisplayName+TEXT(" / ")+Entry.Selection.DisplayName));
					}
		};
		Add(Asset->Styles); Add(Asset->RoomThemes);
		Result.Sort([](const auto& A,const auto& B){return A.Value.ToString()<B.Value.ToString();});
		return Result;
	}
}

TSharedRef<IPropertyTypeCustomization> FEFCalystoTraitBindingDetails::MakeInstance()
{ return MakeShared<FEFCalystoTraitBindingDetails>(); }

void FEFCalystoTraitBindingDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Property,
	FDetailWidgetRow& Header,IPropertyTypeCustomizationUtils&)
{ Header.NameContent()[Property->CreatePropertyNameWidget()].ValueContent()[Property->CreatePropertyValueWidget()]; }

void FEFCalystoTraitBindingDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Property,
	IDetailChildrenBuilder& Children,IPropertyTypeCustomizationUtils&)
{
	uint32 Count=0; Property->GetNumChildren(Count);
	for (uint32 Index=0;Index<Count;++Index)
	{
		const auto Child=Property->GetChildHandle(Index); if (!Child || !Child->GetProperty()) continue;
		if (Child->GetProperty()->GetFName()!=GET_MEMBER_NAME_CHECKED(FEFCalystoTraitBinding,EntryId))
		{ Children.AddProperty(Child.ToSharedRef()); continue; }
		const auto Control=Property->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEFCalystoTraitBinding,Control));
		const TWeakPtr<IPropertyHandle> WeakControl=Control, WeakEntry=Child;
		Control->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakControl,WeakEntry]
		{
			const auto ChangedControl=WeakControl.Pin(), ChangedEntry=WeakEntry.Pin();
			if (!ChangedControl || !ChangedEntry) return;
			uint8 Value=0;
			if (ChangedControl->GetValue(Value)==FPropertyAccess::Success && Value==uint8(EEFCalystoTraitControl::Chance)
				&& ReadIdentity(ChangedEntry).IsValid())
			{
				const FGuid Empty; FString Text;
				TBaseStructure<FGuid>::Get()->ExportText(Text,&Empty,nullptr,nullptr,PPF_None,nullptr);
				// The originating native Control edit already owns this asset transaction.
				ChangedEntry->SetValueFromFormattedString(Text,EPropertyValueSetFlags::NotTransactable);
			}
		}));
		auto& Row=Children.AddProperty(Child.ToSharedRef());
		Row.Visibility(TAttribute<EVisibility>::CreateLambda([Control]
		{ uint8 Value=0; Control->GetValue(Value); return Value==uint8(EEFCalystoTraitControl::EntryWeight) ? EVisibility::Visible : EVisibility::Collapsed; }));
		Row.CustomWidget(false)
		.NameContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Entry")))]
		.ValueContent().MinDesiredWidth(220)
		[
			SNew(SComboButton)
			.ToolTipText(FText::FromString(TEXT("Select an entry in the chosen content role. Its persisted identity survives renaming and reordering.")))
			.OnGetMenuContent_Lambda([Property,Child]
			{
				FMenuBuilder Menu(true,nullptr);
				const auto Options=Entries(Property);
				if (Options.IsEmpty()) Menu.AddMenuEntry(FText::FromString(TEXT("No entries in this role")),FText(),FSlateIcon(),FUIAction());
				for (const auto& Option:Options)
				{
					const FGuid Id=Option.Key;
					Menu.AddMenuEntry(Option.Value,FText(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([Child,Id]
					{
						FString Value;
						TBaseStructure<FGuid>::Get()->ExportText(Value,&Id,nullptr,nullptr,PPF_None,nullptr);
						Child->SetValueFromFormattedString(Value);
					})));
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()[SNew(STextBlock).Text_Lambda([Property,Child]
			{
				const FGuid Current=ReadIdentity(Child);
				for (const auto& Option:Entries(Property)) if (Option.Key==Current) return Option.Value;
				return FText::FromString(Current.IsValid() ? TEXT("Missing entry in this role") : TEXT("Select an entry"));
			})]
		];
	}
}
