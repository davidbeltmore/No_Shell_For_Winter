#include "EFClothingGarmentRowCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EFClothingFitCompilerLibrary.h"
#include "EFClothingFitProfile.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingNativeMeshAuthoringLibrary.h"
#include "EFClothingNativeSourceEditorGate.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "Misc/MessageDialog.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "EFClothingGarmentRowCustomization"

namespace EFClothingGarmentRowCustomizationPrivate
{
	constexpr TCHAR CompatibilityReferencePath[] = TEXT("/Game/DazToUnreal/Multiple/Multiple.Multiple");

	static UEFClothingMorphDirectorPolicy* ResolveDirector(const TSharedPtr<IPropertyHandle>& StructHandle)
	{
		if (!StructHandle.IsValid())
		{
			return nullptr;
		}

		TArray<UObject*> OuterObjects;
		StructHandle->GetOuterObjects(OuterObjects);
		for (UObject* OuterObject : OuterObjects)
		{
			if (UEFClothingMorphDirectorPolicy* Director = Cast<UEFClothingMorphDirectorPolicy>(OuterObject))
			{
				return Director;
			}
		}
		return nullptr;
	}

	static FName ResolveGarmentId(const TSharedPtr<IPropertyHandle>& StructHandle)
	{
		FName GarmentId = NAME_None;
		if (StructHandle.IsValid())
		{
			if (const TSharedPtr<IPropertyHandle> IdHandle = StructHandle->GetChildHandle(
				GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, GarmentId)))
			{
				IdHandle->GetValue(GarmentId);
			}
		}
		return GarmentId;
	}

	static void ShowResult(const bool bSuccess, const FString& Report)
	{
		const FString Message = Report.IsEmpty()
			? (bSuccess ? TEXT("Clothing action completed.") : TEXT("Clothing action failed."))
			: Report;
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = bSuccess ? 5.0f : 10.0f;
		Info.bUseSuccessFailIcons = true;
		Info.bUseLargeFont = false;
		if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(
				bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	static bool ResolveActionTarget(
		const TSharedPtr<IPropertyHandle>& StructHandle,
		UEFClothingMorphDirectorPolicy*& OutDirector,
		FName& OutGarmentId)
	{
		OutDirector = ResolveDirector(StructHandle);
		OutGarmentId = ResolveGarmentId(StructHandle);
		if (!IsValid(OutDirector) || OutGarmentId.IsNone())
		{
			ShowResult(false, TEXT("Assign both meshes so a unique Clothing Name can be created before using this action."));
			return false;
		}
		return true;
	}

	static FReply OpenEditableMesh(const TSharedPtr<IPropertyHandle> StructHandle)
	{
		UEFClothingMorphDirectorPolicy* Director = nullptr;
		FName GarmentId = NAME_None;
		if (!ResolveActionTarget(StructHandle, Director, GarmentId))
		{
			return FReply::Handled();
		}
		FString Report;
		const bool bSuccess = UEFClothingNativeMeshAuthoringLibrary::OpenEditableMesh(
			Director,
			GarmentId,
			Report);
		if (!bSuccess)
		{
			ShowResult(false, Report);
		}
		return FReply::Handled();
	}

	static FReply RefreshBinding(const TSharedPtr<IPropertyHandle> StructHandle)
	{
		UEFClothingMorphDirectorPolicy* Director = nullptr;
		FName GarmentId = NAME_None;
		if (!ResolveActionTarget(StructHandle, Director, GarmentId))
		{
			return FReply::Handled();
		}

		USkeletalMesh* CompatibilityReference = LoadObject<USkeletalMesh>(nullptr, CompatibilityReferencePath);
		if (!IsValid(CompatibilityReference))
		{
			ShowResult(false, TEXT("Fit-data update could not load the protected Multiple compatibility reference."));
			return FReply::Handled();
		}

		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		UEFClothingFitRegistry* Registry = Settings
			? Settings->Registry.LoadSynchronous()
			: nullptr;
		const FEFClothingNativeSourceEditorGateResult Result =
			FEFClothingNativeSourceEditorGate::ValidateOrRefresh(
				Director,
				Registry,
				CompatibilityReference,
				true,
				GarmentId,
				false);
		ShowResult(Result.bSuccess, Result.Report);
		return FReply::Handled();
	}

	static FReply ApplyNativeOffset(const TSharedPtr<IPropertyHandle> StructHandle)
	{
		UEFClothingMorphDirectorPolicy* Director = nullptr;
		FName GarmentId = NAME_None;
		if (!ResolveActionTarget(StructHandle, Director, GarmentId))
		{
			return FReply::Handled();
		}

		const EAppReturnType::Type Answer = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT(
				"ConfirmNativeOffset",
				"Apply Native UE Offset to the Clothing Mesh?\n\nThis is an explicit Unreal Engine mesh edit. Changing the settings alone does nothing. The action does not auto-save and can be undone before saving. Review morph targets, skin weights, and Chaos Cloth afterward. The body and shared skeleton are never modified."),
			LOCTEXT("ConfirmNativeOffsetTitle", "Apply Native Offset"));
		if (Answer != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}

		FString Report;
		const bool bSuccess = UEFClothingNativeMeshAuthoringLibrary::ApplyNativeOffsetToEditableMesh(
			Director,
			GarmentId,
			Report);
		ShowResult(bSuccess, Report);
		return FReply::Handled();
	}

	static FReply CreateShell(const TSharedPtr<IPropertyHandle> StructHandle)
	{
		UEFClothingMorphDirectorPolicy* Director = nullptr;
		FName GarmentId = NAME_None;
		if (!ResolveActionTarget(StructHandle, Director, GarmentId))
		{
			return FReply::Handled();
		}

		const EAppReturnType::Type Answer = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT(
				"ConfirmCreateShell",
				"Create real shell geometry on the Clothing Mesh?\n\nWARNING: this changes topology. Morph targets, skin-weight profiles, and Chaos Cloth data may need repair. The action does not auto-save. Use Surface Volume when visible runtime fullness is enough."),
			LOCTEXT("ConfirmCreateShellTitle", "Create Shell on Clothing Mesh"));
		if (Answer != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}

		FString Report;
		const bool bSuccess = UEFClothingNativeMeshAuthoringLibrary::CreateShellOnEditableMesh(
			Director,
			GarmentId,
			Report);
		ShowResult(bSuccess, Report);
		return FReply::Handled();
	}

	static void AddPropertyIfValid(
		IDetailGroup& Group,
		const TSharedPtr<IPropertyHandle>& PropertyHandle)
	{
		if (PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
		{
			Group.AddPropertyRow(PropertyHandle.ToSharedRef());
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FEFClothingGarmentRowCustomization::MakeInstance()
{
	return MakeShared<FEFClothingGarmentRowCustomization>();
}

void FEFClothingGarmentRowCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> IdHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, GarmentId));
	if (!IdHandle.IsValid() || !IdHandle->IsValidHandle())
	{
		HeaderRow.NameContent()[StructPropertyHandle->CreatePropertyNameWidget()];
		return;
	}

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget(LOCTEXT("ClothingEntry", "Clothing"))
	]
	.ValueContent()
	.MinDesiredWidth(280.0f)
	[
		IdHandle->CreatePropertyValueWidget()
	];
}

void FEFClothingGarmentRowCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace EFClothingGarmentRowCustomizationPrivate;
	const TSharedPtr<IPropertyHandle> StructHandle = StructPropertyHandle;
	auto Child = [&StructPropertyHandle](const FName PropertyName)
	{
		return StructPropertyHandle->GetChildHandle(PropertyName);
	};

	IDetailGroup& ClothingSetupGroup = StructBuilder.AddGroup(
		TEXT("ClothingSetup"),
		LOCTEXT("ClothingSetupGroup", "Clothing Setup"));
	AddPropertyIfValid(ClothingSetupGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, bEnabled)));
	AddPropertyIfValid(ClothingSetupGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, GarmentId)));
	AddPropertyIfValid(ClothingSetupGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, SourceGarment)));
	AddPropertyIfValid(ClothingSetupGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, BodySurface)));
	ClothingSetupGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenClothingMeshButton", "Open Clothing Mesh"))
			.ToolTipText(LOCTEXT("OpenClothingMeshTooltip", "Opens this Clothing Mesh in Unreal Engine's Skeletal Mesh Editor."))
			.OnClicked_Lambda([StructHandle]() { return OpenEditableMesh(StructHandle); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RefreshFitDataButton", "Update This Clothing"))
			.ToolTipText(LOCTEXT("RefreshFitDataTooltip", "Updates fit data only for this clothing. Other clothes remain active. It never replaces a Clothing Mesh or modifies a body, skin weights, morph targets, or shared skeleton."))
			.OnClicked_Lambda([StructHandle]() { return RefreshBinding(StructHandle); })
		]
	];

	IDetailGroup& LiveFitGroup = StructBuilder.AddGroup(
		TEXT("LiveFit"),
		LOCTEXT("LiveFitGroup", "Live Fit"));
	AddPropertyIfValid(LiveFitGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, AdditionalClearanceCm)));
	AddPropertyIfValid(LiveFitGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, ShellThicknessCm)));

	IDetailGroup& AdvancedMeshEditGroup = StructBuilder.AddGroup(
		TEXT("AdvancedMeshEdit"),
		LOCTEXT("AdvancedMeshEditGroup", "Advanced Mesh Edit"));
	if (const TSharedPtr<IPropertyHandle> NativeOffsetHandle = Child(
		GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, NativeUEOffset)))
	{
		AddPropertyIfValid(AdvancedMeshEditGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, OffsetType)));
		AddPropertyIfValid(AdvancedMeshEditGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, DistanceCm)));
		AddPropertyIfValid(AdvancedMeshEditGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, Steps)));
		AddPropertyIfValid(AdvancedMeshEditGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, bOffsetBoundaries)));
		AddPropertyIfValid(AdvancedMeshEditGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, SmoothingPerStep)));
		AddPropertyIfValid(AdvancedMeshEditGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, bReprojectAfterSmoothing)));
	}
	AdvancedMeshEditGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("ApplyNativeOffsetButton", "Apply Native Offset to Clothing Mesh"))
		.ToolTipText(LOCTEXT("ApplyNativeOffsetTooltip", "Edits the Clothing Mesh with the settings above. The action supports Undo and never auto-saves. Changing the settings alone has no effect."))
		.OnClicked_Lambda([StructHandle]() { return ApplyNativeOffset(StructHandle); })
	];

	IDetailGroup& BodyHidingGroup = StructBuilder.AddGroup(
		TEXT("BodyHiding"),
		LOCTEXT("BodyHidingGroup", "Body Hiding"));
	AddPropertyIfValid(BodyHidingGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, BodySectionsToExclude)));
	AddPropertyIfValid(BodyHidingGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, CoverageTags)));

	AdvancedMeshEditGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text(LOCTEXT(
			"ShellWarning",
			"Create Shell changes the Clothing Mesh topology. It uses Surface Volume as the requested thickness and may require repairs to morph targets, skin weights, or Chaos Cloth data. Use it only when real inner and outer geometry is required."))
	];
	AdvancedMeshEditGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateShellButton", "Create Shell on Clothing Mesh..."))
		.ToolTipText(LOCTEXT("CreateShellTooltip", "Creates real thickness after a warning. Unsafe assets are refused, and the action does not auto-save."))
		.OnClicked_Lambda([StructHandle]() { return CreateShell(StructHandle); })
	];

	IDetailGroup& NotesGroup = StructBuilder.AddGroup(
		TEXT("Notes"),
		LOCTEXT("NotesGroup", "Notes"));
	AddPropertyIfValid(NotesGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, Notes)));
}

#undef LOCTEXT_NAMESPACE
