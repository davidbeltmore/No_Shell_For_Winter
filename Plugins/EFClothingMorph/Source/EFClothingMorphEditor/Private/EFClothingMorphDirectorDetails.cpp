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
		TEXT("EF Clothing Morph V4"),
		LOCTEXT("OverviewCategory", "EF Clothing Morph V4"),
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
				"Add one entry to Clothes for each Clothing Mesh and Body Mesh pair. Clothing Name is created automatically when both meshes are assigned and remains editable. Skin Gap and Surface Volume update only that clothing at runtime. Several clothes can work at the same time, and unfinished drafts cannot disable ready clothes. Advanced mesh edits happen only when you press their buttons. Fit-data updates never replace a Clothing Mesh or modify the body or shared skeleton."))
		]
	];

	DetailBuilder.EditCategory(
		TEXT("Clothes"),
		LOCTEXT("ClothesCategory", "Clothes"),
		ECategoryPriority::Important);
}

#undef LOCTEXT_NAMESPACE
