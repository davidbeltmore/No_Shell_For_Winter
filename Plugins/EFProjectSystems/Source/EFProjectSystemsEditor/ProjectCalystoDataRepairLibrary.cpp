#include "ProjectCalystoDataRepairLibrary.h"

#include "EdGraphSchema_K2.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace
{
	constexpr TCHAR SmartScatterPath[] = TEXT("/Game/Calysto/Shared/Data/Structure/ST_SmartScatter.ST_SmartScatter");
	constexpr TCHAR ScatterClassPath[] = TEXT("/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter.PDA_CalystoScatter_C");
	constexpr TCHAR ScatterDefaultPath[] = TEXT("/Game/Calysto/Shared/Data/DA_ScatterDummy.DA_ScatterDummy");
	constexpr TCHAR ScatterModeEnumPath[] = TEXT("/Game/Calysto/Shared/Data/Structure/Enum_ScatterMode.Enum_ScatterMode");
	constexpr TCHAR DataGuidDigits[] = TEXT("DDA7FF704A715D8FC2E50BAE84265C16");
	constexpr TCHAR ScatterModeGuidDigits[] = TEXT("6B2E03C64E344657A1D970B9D9C985C8");
}

bool UProjectCalystoDataRepairLibrary::RepairSmartScatterDataMember(
	UUserDefinedStruct* SmartScatterStruct,
	UClass* ScatterDataClass,
	UObject* ScatterDefaultObject,
	UEnum* ScatterModeEnum,
	FString& OutDetails)
{
	OutDetails.Reset();
	if (!IsValid(SmartScatterStruct) || !IsValid(ScatterDataClass)
		|| !IsValid(ScatterDefaultObject) || !IsValid(ScatterModeEnum))
	{
		OutDetails = TEXT("One or more required objects are invalid");
		return false;
	}

	if (SmartScatterStruct->GetPathName() != SmartScatterPath
		|| ScatterDataClass->GetPathName() != ScatterClassPath
		|| ScatterDefaultObject->GetPathName() != ScatterDefaultPath
		|| ScatterModeEnum->GetPathName() != ScatterModeEnumPath)
	{
		OutDetails = FString::Printf(
			TEXT("Exact-path guard failed: struct=%s class=%s default=%s enum=%s"),
			*SmartScatterStruct->GetPathName(),
			*ScatterDataClass->GetPathName(),
			*ScatterDefaultObject->GetPathName(),
			*ScatterModeEnum->GetPathName());
		return false;
	}

	if (!ScatterDefaultObject->IsA(ScatterDataClass))
	{
		OutDetails = TEXT("DA_ScatterDummy is not an instance of PDA_CalystoScatter_C");
		return false;
	}

	TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(SmartScatterStruct);
	FStructVariableDescription* DataVariable = nullptr;
	FStructVariableDescription* ScatterModeVariable = nullptr;
	int32 DataMatchCount = 0;
	int32 ScatterModeMatchCount = 0;
	for (FStructVariableDescription& Variable : Variables)
	{
		const FString VariableName = Variable.VarName.ToString();
		if (VariableName.Contains(DataGuidDigits, ESearchCase::IgnoreCase)
			|| Variable.FriendlyName.Equals(TEXT("Data"), ESearchCase::IgnoreCase))
		{
			DataVariable = &Variable;
			++DataMatchCount;
		}
		if (VariableName.Contains(ScatterModeGuidDigits, ESearchCase::IgnoreCase)
			|| Variable.FriendlyName.Equals(TEXT("ScatterMode"), ESearchCase::IgnoreCase)
			|| Variable.FriendlyName.Equals(TEXT("Scatter Mode"), ESearchCase::IgnoreCase))
		{
			ScatterModeVariable = &Variable;
			++ScatterModeMatchCount;
		}
	}

	if (DataMatchCount != 1 || DataVariable == nullptr
		|| ScatterModeMatchCount != 1 || ScatterModeVariable == nullptr)
	{
		OutDetails = FString::Printf(
			TEXT("Expected exactly one Data and ScatterMode member; found data=%d scatter_mode=%d"),
			DataMatchCount,
			ScatterModeMatchCount);
		return false;
	}

	const FGuid DataGuid = DataVariable->VarGuid;
	const FGuid ScatterModeGuid = ScatterModeVariable->VarGuid;
	const FEdGraphPinType BeforeType = DataVariable->ToPinType();
	const FString BeforeClass = GetPathNameSafe(BeforeType.PinSubCategoryObject.Get());
	const FString BeforeDefault = DataVariable->DefaultValue;
	const FString BeforeScatterModeDefault = ScatterModeVariable->DefaultValue;

	FEdGraphPinType CorrectType;
	CorrectType.PinCategory = UEdGraphSchema_K2::PC_Object;
	CorrectType.PinSubCategory = NAME_None;
	CorrectType.PinSubCategoryObject = ScatterDataClass;
	CorrectType.ContainerType = EPinContainerType::None;
	CorrectType.bIsReference = false;
	CorrectType.bIsConst = false;
	CorrectType.bIsWeakPointer = false;
	CorrectType.bIsUObjectWrapper = false;

	const FString CorrectDefault = ScatterDefaultObject->GetPathName();
	const FString CorrectScatterModeDefault = ScatterModeEnum->GetNameByIndex(0).ToString();
	if (CorrectScatterModeDefault.IsEmpty())
	{
		OutDetails = TEXT("Enum_ScatterMode has no first enumerator");
		return false;
	}

	// Change the editor descriptions as one atomic structural update. Calling the
	// public one-field helpers here would compile between changes, while the old
	// object class and legacy numeric enum default are both invalid in UE 5.8.
	FStructureEditorUtils::ModifyStructData(SmartScatterStruct);
	const bool bTypeChanged = BeforeType != CorrectType;
	const bool bDefaultChanged = DataVariable->DefaultValue != CorrectDefault;
	const bool bScatterModeDefaultChanged = ScatterModeVariable->DefaultValue != CorrectScatterModeDefault;
	DataVariable->SetPinType(CorrectType);
	DataVariable->DefaultValue = CorrectDefault;
	DataVariable->CurrentDefaultValue = CorrectDefault;
	ScatterModeVariable->DefaultValue = CorrectScatterModeDefault;
	ScatterModeVariable->CurrentDefaultValue = CorrectScatterModeDefault;
	FStructureEditorUtils::OnStructureChanged(
		SmartScatterStruct,
		FStructureEditorUtils::EStructureEditorChangeInfo::VariableTypeChanged);

	DataVariable = FStructureEditorUtils::GetVarDescByGuid(SmartScatterStruct, DataGuid);
	ScatterModeVariable = FStructureEditorUtils::GetVarDescByGuid(SmartScatterStruct, ScatterModeGuid);
	const FEdGraphPinType AfterType = DataVariable ? DataVariable->ToPinType() : FEdGraphPinType();
	const FString AfterClass = GetPathNameSafe(AfterType.PinSubCategoryObject.Get());
	const FString AfterDefault = DataVariable ? DataVariable->DefaultValue : FString();
	const FObjectPropertyBase* DataProperty = CastField<FObjectPropertyBase>(
		FStructureEditorUtils::GetPropertyByGuid(SmartScatterStruct, DataGuid));
	UObject* ActualDefaultObject = nullptr;
	if (DataProperty != nullptr && SmartScatterStruct->GetDefaultInstance() != nullptr)
	{
		ActualDefaultObject = DataProperty->GetObjectPropertyValue_InContainer(
			SmartScatterStruct->GetDefaultInstance());
	}
	const bool bValidAfter = DataVariable != nullptr
		&& ScatterModeVariable != nullptr
		&& AfterType.PinCategory == UEdGraphSchema_K2::PC_Object
		&& AfterType.PinSubCategoryObject.Get() == ScatterDataClass
		&& AfterType.ContainerType == EPinContainerType::None
		&& AfterDefault.Contains(CorrectDefault, ESearchCase::CaseSensitive)
		&& DataProperty != nullptr
		&& DataProperty->PropertyClass == ScatterDataClass
		&& ActualDefaultObject == ScatterDefaultObject
		&& SmartScatterStruct->Status == UDSS_UpToDate;

	OutDetails = FString::Printf(
		TEXT("member=%s guid=%s before_class=%s before_default=%s after_class=%s after_default=%s actual_default=%s scatter_mode_before=%s scatter_mode_after=%s type_changed=%s default_changed=%s scatter_mode_changed=%s status=%d valid=%s"),
		DataVariable ? *DataVariable->VarName.ToString() : TEXT("<missing>"),
		*DataGuid.ToString(EGuidFormats::Digits),
		*BeforeClass,
		*BeforeDefault,
		*AfterClass,
		*AfterDefault,
		*GetPathNameSafe(ActualDefaultObject),
		*BeforeScatterModeDefault,
		*CorrectScatterModeDefault,
		bTypeChanged ? TEXT("true") : TEXT("false"),
		bDefaultChanged ? TEXT("true") : TEXT("false"),
		bScatterModeDefaultChanged ? TEXT("true") : TEXT("false"),
		static_cast<int32>(SmartScatterStruct->Status.GetValue()),
		bValidAfter ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Display, TEXT("CODEX_CALYSTO_SMART_SCATTER_NATIVE: %s"), *OutDetails);

	if (bValidAfter)
	{
		SmartScatterStruct->MarkPackageDirty();
	}
	return bValidAfter;
}
