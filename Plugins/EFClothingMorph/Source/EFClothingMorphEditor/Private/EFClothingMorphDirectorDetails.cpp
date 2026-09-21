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
		TEXT("EF Clothing Morph V5.1"),
		LOCTEXT("OverviewCategory", "EF Clothing Morph V5.1 - Single Clothing Table"),
		ECategoryPriority::Important);
	Overview.AddCustomRow(LOCTEXT("OverviewSearch", "Clothes clothing help mesh live fit"))
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
				"This is the only clothing table you maintain, using the same workflow as V4.5. Add each clothing mesh once with its reference Body Mesh, then register all supported bodies in Bodies. Every garment is unisex; body-specific fits and anatomy coverage are generated automatically. Force Opaque prevents skin and tattoos from appearing through cloth. Optional layering and material exceptions stay inside the same row; no generated Data Asset is an authoring surface. Fit-data updates never replace a Clothing Mesh or modify the body or shared skeleton, and V4 remains available as rollback."))
		]
	];

	DetailBuilder.EditCategory(
		TEXT("Clothes"),
		LOCTEXT("ClothesCategory", "Clothes"),
		ECategoryPriority::Important);
}

#undef LOCTEXT_NAMESPACE
