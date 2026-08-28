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
			? (bSuccess ? TEXT("EF Clothing Morph action completed.") : TEXT("EF Clothing Morph action failed."))
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
			ShowResult(false, TEXT("Assign a unique Garment ID before using this action."));
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
		(void)GarmentId;

		USkeletalMesh* CompatibilityReference = LoadObject<USkeletalMesh>(nullptr, CompatibilityReferencePath);
		if (!IsValid(CompatibilityReference))
		{
			ShowResult(false, TEXT("Refresh Binding could not load the protected Multiple compatibility reference."));
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
				true);
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
				"Apply Native UE Offset to the authoritative Editable Garment Mesh?\n\nThis is an explicit transactional mesh edit. It is not applied by changing the values above, it will not auto-save, and it can be undone before saving. Review morph targets, skin weights, and Chaos Cloth after the edit. The body and shared skeleton are never modified."),
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
				"Create real shell geometry on the authoritative Editable Garment Mesh?\n\nWARNING: this changes topology. Morph targets, native skin-weight profiles, and Chaos Cloth mappings can require deliberate repair. The operation is transactional and will not auto-save; EF Clothing Morph will refuse unsafe assets instead of guessing. Use Surface Inflate when visible runtime fullness is sufficient."),
			LOCTEXT("ConfirmCreateShellTitle", "Create Shell on Editable Mesh"));
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

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget(LOCTEXT("GarmentEntry", "Garment"))
	]
	.ValueContent()
	.MinDesiredWidth(280.0f)
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFontBold())
		.Text_Lambda([IdHandle]()
		{
			FName GarmentId = NAME_None;
			if (IdHandle.IsValid() && IdHandle->GetValue(GarmentId) == FPropertyAccess::Success && !GarmentId.IsNone())
			{
				return FText::FromName(GarmentId);
			}
			return LOCTEXT("NewGarment", "New Garment");
		})
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

	IDetailGroup& GarmentGroup = StructBuilder.AddGroup(
		TEXT("GarmentDefinition"),
		LOCTEXT("GarmentDefinitionGroup", "Garment"));
	AddPropertyIfValid(GarmentGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, bEnabled)));
	AddPropertyIfValid(GarmentGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, GarmentId)));
	AddPropertyIfValid(GarmentGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, SourceGarment)));
	AddPropertyIfValid(GarmentGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, BodySurface)));
	GarmentGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenEditableMeshButton", "Open Editable Mesh"))
			.ToolTipText(LOCTEXT("OpenEditableMeshTooltip", "Opens this entry's authoritative source mesh in Unreal Engine's native Skeletal Mesh Editor."))
			.OnClicked_Lambda([StructHandle]() { return OpenEditableMesh(StructHandle); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RefreshBindingButton", "Refresh Binding"))
			.ToolTipText(LOCTEXT("RefreshBindingTooltip", "Rebuilds only stale V3 source-surface bindings. It never replaces the garment mesh or modifies any body, skin weights, morph targets, or shared skeleton."))
			.OnClicked_Lambda([StructHandle]() { return RefreshBinding(StructHandle); })
		]
	];

	IDetailGroup& RuntimeFitGroup = StructBuilder.AddGroup(
		TEXT("RuntimeFit"),
		LOCTEXT("RuntimeFitGroup", "Runtime Fit (Immediate, Per Garment)"));
	AddPropertyIfValid(RuntimeFitGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, AdditionalClearanceCm)));
	AddPropertyIfValid(RuntimeFitGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, ShellThicknessCm)));

	IDetailGroup& NativeOffsetGroup = StructBuilder.AddGroup(
		TEXT("NativeUEOffset"),
		LOCTEXT("NativeUEOffsetGroup", "Native UE Offset (Explicit Mesh Edit)"));
	if (const TSharedPtr<IPropertyHandle> NativeOffsetHandle = Child(
		GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, NativeUEOffset)))
	{
		AddPropertyIfValid(NativeOffsetGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, OffsetType)));
		AddPropertyIfValid(NativeOffsetGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, DistanceCm)));
		AddPropertyIfValid(NativeOffsetGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, Steps)));
		AddPropertyIfValid(NativeOffsetGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, bOffsetBoundaries)));
		AddPropertyIfValid(NativeOffsetGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, SmoothingPerStep)));
		AddPropertyIfValid(NativeOffsetGroup, NativeOffsetHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FEFClothingNativeUEOffsetSettings, bReprojectAfterSmoothing)));
	}
	NativeOffsetGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("ApplyNativeOffsetButton", "Apply Native Offset to Editable Mesh"))
		.ToolTipText(LOCTEXT("ApplyNativeOffsetTooltip", "Explicitly edits the authoritative garment mesh using the values above. The action is transactional, supports Undo, and never auto-saves. Changing the values alone has no effect."))
		.OnClicked_Lambda([StructHandle]() { return ApplyNativeOffset(StructHandle); })
	];

	IDetailGroup& CoverageGroup = StructBuilder.AddGroup(
		TEXT("BodyCoverage"),
		LOCTEXT("BodyCoverageGroup", "Body Coverage"));
	AddPropertyIfValid(CoverageGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, BodySectionsToExclude)));
	AddPropertyIfValid(CoverageGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, CoverageTags)));

	IDetailGroup& ShellGroup = StructBuilder.AddGroup(
		TEXT("RealGeometry"),
		LOCTEXT("RealGeometryGroup", "Real Geometry (Advanced)"));
	ShellGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text(LOCTEXT(
			"ShellWarning",
			"Create Shell is an explicit topology-changing edit of the source mesh. It uses Surface Inflate as the requested thickness, is never required for anti-clipping, and can invalidate morph targets, skin-weight profiles, or Chaos Cloth mappings. Use it only when real inner/outer geometry and boundary walls are necessary."))
	];
	ShellGroup.AddWidgetRow()
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateShellButton", "Create Shell on Editable Mesh..."))
		.ToolTipText(LOCTEXT("CreateShellTooltip", "Requests a transactional shell operation after an explicit warning. Unsafe assets are refused; the action does not auto-save."))
		.OnClicked_Lambda([StructHandle]() { return CreateShell(StructHandle); })
	];

	IDetailGroup& NotesGroup = StructBuilder.AddGroup(
		TEXT("Notes"),
		LOCTEXT("NotesGroup", "Notes"));
	AddPropertyIfValid(NotesGroup, Child(GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, Notes)));
}

#undef LOCTEXT_NAMESPACE
