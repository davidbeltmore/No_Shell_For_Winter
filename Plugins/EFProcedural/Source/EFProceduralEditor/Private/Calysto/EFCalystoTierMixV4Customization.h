#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Types/SlateEnums.h"

class IDetailChildrenBuilder;
class IPropertyHandle;
class IPropertyTypeCustomizationUtils;
class STextBlock;
class FDetailWidgetRow;

/**
 * Authoring guard for FEFCalystoTierMixV4.
 *
 * The four authored tiers share at most 0.90 probability mass.  The remaining
 * mass is always the read-only Nothing probability, so every valid mix keeps
 * at least a 10% chance of selecting no tier. Winter is authored in its
 * separate post-100 pool and is deliberately not part of this pre-101 mix.
 */
class FEFCalystoTierMixV4Customization final : public IPropertyTypeCustomization
{
public:
	static constexpr float MaximumAuthoredMass = 0.90f;
	static constexpr float ValidationTolerance = 1.0e-6f;

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	/** Pure validation shared by the details surface and focused automation. */
	static bool ValidateAuthoredMass(
		float Common,
		float Uncommon,
		float Rare,
		float Epic,
		float& OutNothing,
		FText* OutError = nullptr);

private:
	void AddEditableTierRow(
		IDetailChildrenBuilder& ChildBuilder,
		const TSharedPtr<IPropertyHandle>& TierHandle);

	TOptional<float> GetTierValue(
		TSharedPtr<IPropertyHandle> TierHandle) const;

	TOptional<float> GetAuthoredTotal() const;
	TOptional<float> GetDerivedNothing() const;
	FText GetHeaderSummary() const;
	EVisibility GetFeedbackVisibility() const;

	void CommitTierValue(
		float NewValue,
		ETextCommit::Type CommitType,
		TSharedPtr<IPropertyHandle> TierHandle);

	bool ReadCurrentValues(
		float& OutCommon,
		float& OutUncommon,
		float& OutRare,
		float& OutEpic) const;

	void SetFeedback(const FText& Message);

	TSharedPtr<IPropertyHandle> CommonHandle;
	TSharedPtr<IPropertyHandle> UncommonHandle;
	TSharedPtr<IPropertyHandle> RareHandle;
	TSharedPtr<IPropertyHandle> EpicHandle;
	TSharedPtr<IPropertyHandle> NothingHandle;
	TSharedPtr<STextBlock> FeedbackWidget;
	FText FeedbackMessage;
};
