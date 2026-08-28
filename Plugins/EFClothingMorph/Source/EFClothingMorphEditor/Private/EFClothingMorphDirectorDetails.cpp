#include "EFClothingMorphDirectorDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "EFClothingMorphDirectorDetails"

TSharedRef<IDetailCustomization> FEFClothingMorphDirectorDetails::MakeInstance()
{
	return MakeShared<FEFClothingMorphDirectorDetails>();
}

void FEFClothingMorphDirectorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UEFClothingMorphDirectorPolicy, AuthoringGuide));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UEFClothingMorphDirectorPolicy, SchemaVersion));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UEFClothingMorphDirectorPolicy, DirectorId));

	IDetailCategoryBuilder& Overview = DetailBuilder.EditCategory(
		TEXT("EF Clothing Morph V3"),
		LOCTEXT("OverviewCategory", "EF Clothing Morph V3"),
		ECategoryPriority::Important);
	Overview.AddCustomRow(LOCTEXT("OverviewSearch", "Garment clothing help source mesh runtime offset"))
	.WholeRowContent()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(10.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT(
				"OverviewText",
				"Add one entry below for each garment and reference-body pair. Editable Garment Mesh is the authoritative source: native Unreal edits are allowed. Skin Clearance and Surface Inflate are immediate per-garment runtime controls. Native UE Offset and Create Shell change nothing until their explicit buttons are pressed. Refresh Binding rebuilds project-owned binding data without replacing the source mesh or modifying the shared skeleton."))
		]
	];

	DetailBuilder.EditCategory(
		TEXT("Garments"),
		LOCTEXT("GarmentsCategory", "Garments"),
		ECategoryPriority::Important);
}

#undef LOCTEXT_NAMESPACE
