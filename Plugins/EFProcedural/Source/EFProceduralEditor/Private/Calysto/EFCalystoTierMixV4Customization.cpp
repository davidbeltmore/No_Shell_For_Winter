#include "Calysto/EFCalystoTierMixV4Customization.h"

#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "EFCalystoTierMixV4Customization"

TSharedRef<IPropertyTypeCustomization> FEFCalystoTierMixV4Customization::MakeInstance()
{
	return MakeShared<FEFCalystoTierMixV4Customization>();
}

void FEFCalystoTierMixV4Customization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	(void)CustomizationUtils;

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	[
		SNew(STextBlock)
		.Text(this, &FEFCalystoTierMixV4Customization::GetHeaderSummary)
		.ToolTipText(LOCTEXT(
			"HeaderSummaryTooltip",
			"Common, Uncommon, Rare, and Epic share a maximum total of 0.90. "
			"Nothing is calculated automatically from the unassigned probability."))
	];
}

void FEFCalystoTierMixV4Customization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	(void)CustomizationUtils;

	CommonHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Common));
	UncommonHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Uncommon));
	RareHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Rare));
	EpicHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Epic));
	NothingHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Nothing));

	if (!CommonHandle.IsValid()
		|| !UncommonHandle.IsValid()
		|| !RareHandle.IsValid()
		|| !EpicHandle.IsValid()
		|| !NothingHandle.IsValid())
	{
		ChildBuilder.AddCustomRow(LOCTEXT("InvalidSchemaFilter", "Invalid V4 Tier Schema"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"InvalidSchema",
				"The compiled FEFCalystoTierMixV4 schema is incomplete. Editing is disabled."))
			.ColorAndOpacity(FLinearColor(1.0f, 0.2f, 0.2f))
		];
		return;
	}

	ChildBuilder.AddCustomRow(LOCTEXT("ProbabilityGuideFilter", "Probability Guide"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT(
			"ProbabilityGuide",
			"Distribute up to 0.90 across Common, Uncommon, Rare, and Epic. Unused probability mass is converted automatically to Nothing."))
		.AutoWrapText(true)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
	];

	AddEditableTierRow(ChildBuilder, CommonHandle);
	AddEditableTierRow(ChildBuilder, UncommonHandle);
	AddEditableTierRow(ChildBuilder, RareHandle);
	AddEditableTierRow(ChildBuilder, EpicHandle);

	ChildBuilder.AddCustomRow(LOCTEXT("AssignedProbabilityFilter", "Assigned Probability"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AssignedProbability", "Assigned Probability"))
		.ToolTipText(LOCTEXT(
			"AssignedProbabilityTooltip",
			"Read-only sum of Common, Uncommon, Rare, and Epic. It cannot exceed 0.90."))
	]
	.ValueContent()
	.MinDesiredWidth(180.0f)
	[
		SNew(SNumericEntryBox<float>)
		.Value(this, &FEFCalystoTierMixV4Customization::GetAuthoredTotal)
		.IsEnabled(false)
		.MinFractionalDigits(2)
		.MaxFractionalDigits(4)
	];

	ChildBuilder.AddCustomRow(LOCTEXT("NothingFilter", "Nothing"))
	.NameContent()
	[
		NothingHandle->CreatePropertyNameWidget(
			LOCTEXT("NothingLabel", "Nothing (Automatic)"),
			LOCTEXT(
				"NothingTooltip",
				"Read-only probability of selecting no tier. It is exactly 1 minus the four authored tiers and is always at least 0.10."))
	]
	.ValueContent()
	.MinDesiredWidth(180.0f)
	[
		SNew(SNumericEntryBox<float>)
		.Value(this, &FEFCalystoTierMixV4Customization::GetDerivedNothing)
		.IsEnabled(false)
		.MinFractionalDigits(2)
		.MaxFractionalDigits(4)
	];

	ChildBuilder.AddCustomRow(LOCTEXT("ValidationFeedbackFilter", "Probability Validation"))
	.Visibility(TAttribute<EVisibility>::Create(
		TAttribute<EVisibility>::FGetter::CreateSP(
			this,
			&FEFCalystoTierMixV4Customization::GetFeedbackVisibility)))
	.WholeRowContent()
	[
		SAssignNew(FeedbackWidget, STextBlock)
		.Text(FeedbackMessage)
		.AutoWrapText(true)
		.ColorAndOpacity(FLinearColor(1.0f, 0.18f, 0.12f))
	];
}

bool FEFCalystoTierMixV4Customization::ValidateAuthoredMass(
	const float Common,
	const float Uncommon,
	const float Rare,
	const float Epic,
	float& OutNothing,
	FText* OutError)
{
	const float Values[] = {Common, Uncommon, Rare, Epic};
	for (const float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0f || Value > MaximumAuthoredMass)
		{
			OutNothing = 0.0f;
			if (OutError)
			{
				*OutError = LOCTEXT(
					"InvalidIndividualTier",
					"Rejected: each tier probability must be finite and between 0.00 and 0.90.");
			}
			return false;
		}
	}

	const double AuthoredMass = static_cast<double>(Common)
		+ static_cast<double>(Uncommon)
		+ static_cast<double>(Rare)
		+ static_cast<double>(Epic);
	if (!FMath::IsFinite(AuthoredMass)
		|| AuthoredMass > static_cast<double>(MaximumAuthoredMass)
			+ ValidationTolerance)
	{
		OutNothing = FMath::Max(0.0f, 1.0f - static_cast<float>(AuthoredMass));
		if (OutError)
		{
			*OutError = FText::Format(
				LOCTEXT(
					"AuthoredMassExceeded",
					"Rejected: the four tiers total {0}. Their shared maximum is 0.90, reserving at least 0.10 for Nothing."),
				FText::AsNumber(AuthoredMass));
		}
		return false;
	}

	OutNothing = FMath::Clamp(1.0f - static_cast<float>(AuthoredMass), 0.10f, 1.0f);
	if (OutError)
	{
		*OutError = FText::GetEmpty();
	}
	return true;
}

void FEFCalystoTierMixV4Customization::AddEditableTierRow(
	IDetailChildrenBuilder& ChildBuilder,
	const TSharedPtr<IPropertyHandle>& TierHandle)
{
	check(TierHandle.IsValid());

	ChildBuilder.AddCustomRow(TierHandle->GetPropertyDisplayName())
	.NameContent()
	[
		TierHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(180.0f)
	[
		SNew(SNumericEntryBox<float>)
		.Value(this, &FEFCalystoTierMixV4Customization::GetTierValue, TierHandle)
		.OnValueCommitted(
			this,
			&FEFCalystoTierMixV4Customization::CommitTierValue,
			TierHandle)
		.MinValue(0.0f)
		.MaxValue(MaximumAuthoredMass)
		.MinSliderValue(0.0f)
		.MaxSliderValue(MaximumAuthoredMass)
		.AllowSpin(true)
		.MinFractionalDigits(2)
		.MaxFractionalDigits(4)
		.UndeterminedString(LOCTEXT("MultipleValues", "Multiple Values"))
	];
}

TOptional<float> FEFCalystoTierMixV4Customization::GetTierValue(
	TSharedPtr<IPropertyHandle> TierHandle) const
{
	float Value = 0.0f;
	if (TierHandle.IsValid()
		&& TierHandle->GetValue(Value) == FPropertyAccess::Success)
	{
		return Value;
	}
	return TOptional<float>();
}

TOptional<float> FEFCalystoTierMixV4Customization::GetAuthoredTotal() const
{
	float Common = 0.0f;
	float Uncommon = 0.0f;
	float Rare = 0.0f;
	float Epic = 0.0f;
	if (!ReadCurrentValues(Common, Uncommon, Rare, Epic))
	{
		return TOptional<float>();
	}
	return Common + Uncommon + Rare + Epic;
}

TOptional<float> FEFCalystoTierMixV4Customization::GetDerivedNothing() const
{
	float Common = 0.0f;
	float Uncommon = 0.0f;
	float Rare = 0.0f;
	float Epic = 0.0f;
	if (!ReadCurrentValues(Common, Uncommon, Rare, Epic))
	{
		return TOptional<float>();
	}

	float Nothing = 0.0f;
	if (!ValidateAuthoredMass(Common, Uncommon, Rare, Epic, Nothing))
	{
		return TOptional<float>();
	}
	return Nothing;
}

FText FEFCalystoTierMixV4Customization::GetHeaderSummary() const
{
	const TOptional<float> Total = GetAuthoredTotal();
	const TOptional<float> Nothing = GetDerivedNothing();
	if (!Total.IsSet() || !Nothing.IsSet())
	{
		return LOCTEXT("MixedTierSummary", "Multiple Values");
	}

	return FText::Format(
		LOCTEXT("TierSummary", "Assigned {0} / 0.90  |  Nothing {1}"),
		FText::AsNumber(Total.GetValue()),
		FText::AsNumber(Nothing.GetValue()));
}

EVisibility FEFCalystoTierMixV4Customization::GetFeedbackVisibility() const
{
	return FeedbackMessage.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

void FEFCalystoTierMixV4Customization::CommitTierValue(
	const float NewValue,
	const ETextCommit::Type CommitType,
	TSharedPtr<IPropertyHandle> TierHandle)
{
	(void)CommitType;

	float Common = 0.0f;
	float Uncommon = 0.0f;
	float Rare = 0.0f;
	float Epic = 0.0f;
	if (!ReadCurrentValues(Common, Uncommon, Rare, Epic))
	{
		SetFeedback(LOCTEXT(
			"MixedSelectionRejected",
			"Rejected: tier probabilities cannot be edited across mixed selections."));
		return;
	}

	if (TierHandle == CommonHandle)
	{
		Common = NewValue;
	}
	else if (TierHandle == UncommonHandle)
	{
		Uncommon = NewValue;
	}
	else if (TierHandle == RareHandle)
	{
		Rare = NewValue;
	}
	else if (TierHandle == EpicHandle)
	{
		Epic = NewValue;
	}
	else
	{
		SetFeedback(LOCTEXT("UnknownTierRejected", "Rejected: unknown tier field."));
		return;
	}

	float Nothing = 0.0f;
	FText Error;
	if (!ValidateAuthoredMass(
		Common,
		Uncommon,
		Rare,
		Epic,
		Nothing,
		&Error))
	{
		SetFeedback(Error);
		return;
	}

	if (!TierHandle.IsValid()
		|| TierHandle->SetValue(NewValue) != FPropertyAccess::Success)
	{
		SetFeedback(LOCTEXT(
			"WriteFailed",
			"Rejected: Unreal could not write the tier value to the policy."));
		return;
	}

	// The owning V4 policy synchronizes serialized Nothing in its editor change
	// hook.  We deliberately never redistribute or renormalize sibling tiers.
	SetFeedback(FText::GetEmpty());
}

bool FEFCalystoTierMixV4Customization::ReadCurrentValues(
	float& OutCommon,
	float& OutUncommon,
	float& OutRare,
	float& OutEpic) const
{
	return CommonHandle.IsValid()
		&& UncommonHandle.IsValid()
		&& RareHandle.IsValid()
		&& EpicHandle.IsValid()
		&& CommonHandle->GetValue(OutCommon) == FPropertyAccess::Success
		&& UncommonHandle->GetValue(OutUncommon) == FPropertyAccess::Success
		&& RareHandle->GetValue(OutRare) == FPropertyAccess::Success
		&& EpicHandle->GetValue(OutEpic) == FPropertyAccess::Success;
}

void FEFCalystoTierMixV4Customization::SetFeedback(const FText& Message)
{
	FeedbackMessage = Message;
	if (FeedbackWidget.IsValid())
	{
		FeedbackWidget->SetText(FeedbackMessage);
	}
}

#undef LOCTEXT_NAMESPACE
