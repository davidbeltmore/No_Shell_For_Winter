#include "EFClothingSurfaceDeformerBuilderLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "IOptimusShaderTextProvider.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "OptimusBindingTypes.h"
#include "OptimusComponentSource.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusDataDomain.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformer.h"
#include "OptimusDiagnostic.h"
#include "OptimusExecutionDomain.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "OptimusResourceDescription.h"
#include "OptimusValidatedName.h"
#include "OptimusVariableDescription.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingSurfaceDeformerBuilder, Log, All);

namespace EFClothingSurfaceDeformerBuilder
{
	constexpr TCHAR AssetPackagePath[] = TEXT("/EFClothingMorph/Deformers/DG_EFGarmentSurfaceConstraint");
	constexpr TCHAR AssetName[] = TEXT("DG_EFGarmentSurfaceConstraint");
	constexpr TCHAR AssetObjectPath[] = TEXT("/EFClothingMorph/Deformers/DG_EFGarmentSurfaceConstraint.DG_EFGarmentSurfaceConstraint");
	constexpr TCHAR GraphSchemaMetadataKey[] = TEXT("EFClothingMorph.SurfaceGraphSchema");
	constexpr TCHAR PrimarySemanticMetadataKey[] = TEXT("EFClothingMorph.PrimaryBindingSemantic");
	constexpr TCHAR GraphSchemaVersion[] = TEXT("35.0");
	constexpr TCHAR PrimarySemantic[] = TEXT("Garment");

	constexpr TCHAR CustomKernelClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_CustomComputeKernel");
	constexpr TCHAR ReadSkinnedMeshClassPath[] = TEXT("/Script/OptimusCore.OptimusSkinnedMeshReadDataInterface");
	constexpr TCHAR WriteSkinnedMeshClassPath[] = TEXT("/Script/OptimusCore.OptimusSkinnedMeshWriteDataInterface");
	constexpr TCHAR SkeletalMeshComponentSourceClassPath[] = TEXT("/Script/OptimusCore.OptimusSkeletalMeshComponentSource");
	constexpr TCHAR ComponentSourceNodeClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_ComponentSource");
	constexpr TCHAR VariableGetNodeClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_GetVariable");

	constexpr TCHAR BaseKernelName[] = TEXT("EF_GarmentSurfaceBase");
	constexpr TCHAR CohesionAKernelName[] = TEXT("EF_GarmentShapeCohesionA");
	constexpr TCHAR CohesionBKernelName[] = TEXT("EF_GarmentShapeCohesionB");
	constexpr TCHAR WitnessKernelName[] = TEXT("EF_GarmentWitness");
	constexpr TCHAR FinalizeKernelName[] = TEXT("EF_GarmentSeamAndTangentFinalize");
	constexpr TCHAR BasePositionResourceName[] = TEXT("BaseCorrectedPosition");
	constexpr TCHAR CohesionAPositionResourceName[] = TEXT("CohesionACorrectedPosition");
	constexpr TCHAR CohesionBPositionResourceName[] = TEXT("CohesionBCorrectedPosition");
	constexpr TCHAR WitnessPositionResourceName[] = TEXT("WitnessCorrectedPosition");
	constexpr TCHAR PrimaryGroupPin[] = TEXT("Primary Group");
	constexpr TCHAR BodyGroupPin[] = TEXT("Body");
	// UE 5.8's explicit component-source node is the SkeletalMesh subtype, while
	// the read/write data interfaces request the more general SkinnedMesh source.
	// They carry the same component-binding type but intentionally expose
	// different pin names.
	constexpr TCHAR SkeletalComponentPin[] = TEXT("SkeletalMesh");
	constexpr TCHAR SkinnedComponentPin[] = TEXT("SkinnedMesh");

	enum class EVariableType : uint8
	{
		Int,
		IntArray,
		Int2Array,
		Int4Array,
		Float,
		FloatArray,
		Float2Array,
		Float4Array,
		Transform
	};

	struct FVariableSpec
	{
		FName Name;
		EVariableType Type;
		bool bDefaultToInvalidLOD = false;
	};

	const TArray<FVariableSpec>& GetVariableSpecs()
	{
		static const TArray<FVariableSpec> Specs =
		{
			{ TEXT("EF_BindingVertexCount"), EVariableType::Int },
			{ TEXT("EF_BodyVertexCount"), EVariableType::Int },
			{ TEXT("EF_GarmentLODIndex"), EVariableType::Int, true },
			{ TEXT("EF_BodyLODIndex"), EVariableType::Int, true },
			{ TEXT("EF_BodyTriangleAndMode"), EVariableType::Int4Array },
			{ TEXT("EF_BarycentricsAndFollowWeight"), EVariableType::Float4Array },
			{ TEXT("EF_RestOffsetAndClearanceCm"), EVariableType::Float4Array },
			{ TEXT("EF_MaximumCorrectionAndRestGapCm"), EVariableType::Float2Array },
			{ TEXT("EF_ThicknessReferenceAndLayer"), EVariableType::Int2Array },
			{ TEXT("EF_NeighborReferenceCount"), EVariableType::Int },
			{ TEXT("EF_NeighborRanges"), EVariableType::Int2Array },
			{ TEXT("EF_NeighborIndices"), EVariableType::IntArray },
			{ TEXT("EF_WeldReferenceCount"), EVariableType::Int },
			{ TEXT("EF_WeldRanges"), EVariableType::Int2Array },
			{ TEXT("EF_WeldIndices"), EVariableType::IntArray },
			{ TEXT("EF_LocalFeatureSizeCm"), EVariableType::FloatArray },
			{ TEXT("EF_UnderBreastGuardWeights"), EVariableType::FloatArray },
			{ TEXT("EF_LowerBodyMorphGuardWeights"), EVariableType::FloatArray },
			{ TEXT("EF_WitnessCount"), EVariableType::Int },
			{ TEXT("EF_WitnessReferenceCount"), EVariableType::Int },
			{ TEXT("EF_WitnessRanges"), EVariableType::Int2Array },
			{ TEXT("EF_WitnessIndices"), EVariableType::IntArray },
			{ TEXT("EF_WitnessGarmentVertices"), EVariableType::Int4Array },
			{ TEXT("EF_WitnessGarmentBarycentricsAndClearanceCm"), EVariableType::Float4Array },
			{ TEXT("EF_WitnessBodyVertices"), EVariableType::Int4Array },
			{ TEXT("EF_WitnessBodyBarycentricsAndMaximumCorrectionCm"), EVariableType::Float4Array },
			{ TEXT("EF_GlobalClearanceOffsetCm"), EVariableType::Float },
			{ TEXT("EF_GarmentClearanceOffsetCm"), EVariableType::Float },
			{ TEXT("EF_GarmentInflateCm"), EVariableType::Float },
			{ TEXT("EF_MaximumCorrectionOverrideCm"), EVariableType::Float },
			{ TEXT("EF_MaximumAutomaticBodyShapeTravelCm"), EVariableType::Float },
			{ TEXT("EF_BodyMorphActivity"), EVariableType::Float },
			{ TEXT("EF_BreastMorphActivity"), EVariableType::Float },
			{ TEXT("EF_UnderBreastClearanceMaxCm"), EVariableType::Float },
			{ TEXT("EF_LowerBodyMorphActivity"), EVariableType::Float },
			{ TEXT("EF_LowerBodyMorphClearanceMaxCm"), EVariableType::Float },
			{ TEXT("EF_DeltaTimeSeconds"), EVariableType::Float },
			{ TEXT("EF_BodyToGarmentTransform"), EVariableType::Transform }
		};
		return Specs;
	}

	FOptimusDataTypeRef ResolveVariableType(const EVariableType Type)
	{
		const FOptimusDataTypeRegistry& Registry = FOptimusDataTypeRegistry::Get();
		FOptimusDataTypeHandle Handle;
		switch (Type)
		{
		case EVariableType::Int:
			Handle = Registry.FindType(*FIntProperty::StaticClass());
			break;
		case EVariableType::IntArray:
			Handle = Registry.FindArrayType(*FIntProperty::StaticClass());
			break;
		case EVariableType::Int2Array:
			Handle = Registry.FindArrayType(TBaseStructure<FIntPoint>::Get());
			break;
		case EVariableType::Int4Array:
			Handle = Registry.FindArrayType(TBaseStructure<FIntVector4>::Get());
			break;
		case EVariableType::Float:
			// Optimus variables use UE double storage and convert it to HLSL float.
			Handle = Registry.FindType(*FDoubleProperty::StaticClass());
			break;
		case EVariableType::FloatArray:
			Handle = Registry.FindArrayType(*FDoubleProperty::StaticClass());
			break;
		case EVariableType::Float2Array:
			Handle = Registry.FindArrayType(TBaseStructure<FVector2D>::Get());
			break;
		case EVariableType::Float4Array:
			Handle = Registry.FindArrayType(TBaseStructure<FVector4>::Get());
			break;
		case EVariableType::Transform:
			Handle = Registry.FindType(TBaseStructure<FTransform>::Get());
			break;
		default:
			break;
		}
		return FOptimusDataTypeRef(Handle);
	}

	FOptimusDataTypeRef ResolveVector3Type()
	{
		return FOptimusDataTypeRef(FOptimusDataTypeRegistry::Get().FindType(TBaseStructure<FVector>::Get()));
	}

	FOptimusDataTypeRef ResolveVector4Type()
	{
		return FOptimusDataTypeRef(FOptimusDataTypeRegistry::Get().FindType(TBaseStructure<FVector4>::Get()));
	}

	UClass* ResolvePrivateClass(const TCHAR* ClassPath, UClass* RequiredBase, FString& OutError)
	{
		UClass* Class = LoadObject<UClass>(nullptr, ClassPath);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("Unable to resolve required Optimus class %s."), ClassPath);
			return nullptr;
		}
		if (!Class->IsChildOf(RequiredBase))
		{
			OutError = FString::Printf(
				TEXT("Reflected class %s is not derived from %s."),
				ClassPath,
				*RequiredBase->GetPathName());
			return nullptr;
		}
		return Class;
	}

	bool ResolveOptimusClasses(
		UClass*& OutKernelClass,
		UClass*& OutReadClass,
		UClass*& OutWriteClass,
		UClass*& OutSkeletalSourceClass,
		UClass*& OutComponentNodeClass,
		UClass*& OutVariableNodeClass,
		FString& OutError)
	{
		OutKernelClass = ResolvePrivateClass(CustomKernelClassPath, UOptimusNode::StaticClass(), OutError);
		if (!OutKernelClass)
		{
			return false;
		}
		OutReadClass = ResolvePrivateClass(ReadSkinnedMeshClassPath, UOptimusComputeDataInterface::StaticClass(), OutError);
		if (!OutReadClass)
		{
			return false;
		}
		OutWriteClass = ResolvePrivateClass(WriteSkinnedMeshClassPath, UOptimusComputeDataInterface::StaticClass(), OutError);
		if (!OutWriteClass)
		{
			return false;
		}
		OutSkeletalSourceClass = ResolvePrivateClass(
			SkeletalMeshComponentSourceClassPath,
			UOptimusComponentSource::StaticClass(),
			OutError);
		if (!OutSkeletalSourceClass)
		{
			return false;
		}
		OutComponentNodeClass = ResolvePrivateClass(ComponentSourceNodeClassPath, UOptimusNode::StaticClass(), OutError);
		if (!OutComponentNodeClass)
		{
			return false;
		}
		OutVariableNodeClass = ResolvePrivateClass(VariableGetNodeClassPath, UOptimusNode::StaticClass(), OutError);
		return OutVariableNodeClass != nullptr;
	}

	FOptimusParameterBinding MakeBinding(
		const FName Name,
		const FOptimusDataTypeRef& Type,
		const FOptimusDataDomain& Domain)
	{
		FOptimusParameterBinding Binding;
		Binding.Name = Name;
		Binding.DataType = Type;
		Binding.DataDomain = Domain;
		return Binding;
	}

	bool SetValidatedNameProperty(UObject* Object, const FName PropertyName, const FName Value, FString& OutError)
	{
		FStructProperty* MemberProperty = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName);
		if (!MemberProperty || MemberProperty->Struct != FOptimusValidatedName::StaticStruct())
		{
			OutError = FString::Printf(TEXT("Optimus reflected property %s has an unexpected type."), *PropertyName.ToString());
			return false;
		}
		FProperty* NameProperty = FindFProperty<FProperty>(FOptimusValidatedName::StaticStruct(), TEXT("Name"));
		if (!NameProperty)
		{
			OutError = TEXT("FOptimusValidatedName.Name reflection is unavailable.");
			return false;
		}

		Object->Modify();
		MemberProperty->ContainerPtrToValuePtr<FOptimusValidatedName>(Object)->Name = Value;
		FPropertyChangedEvent Event(NameProperty, EPropertyChangeType::ValueSet);
		Event.SetActiveMemberProperty(MemberProperty);
		Object->PostEditChangeProperty(Event);
		return true;
	}

	bool SetExecutionDomain(UObject* Object, FString& OutError)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("ExecutionDomain"));
		if (!Property || Property->Struct != FOptimusExecutionDomain::StaticStruct())
		{
			OutError = TEXT("Optimus ExecutionDomain reflection is unavailable or incompatible.");
			return false;
		}

		Object->Modify();
		*Property->ContainerPtrToValuePtr<FOptimusExecutionDomain>(Object) =
			FOptimusExecutionDomain(Optimus::DomainName::Vertex);
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(Event);
		return true;
	}

	bool SetGroupSize(UObject* Object, FString& OutError)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), TEXT("GroupSize"));
		if (!Property || Property->Struct != TBaseStructure<FIntVector>::Get())
		{
			OutError = TEXT("Optimus GroupSize reflection is unavailable or incompatible.");
			return false;
		}
		*Property->ContainerPtrToValuePtr<FIntVector>(Object) = FIntVector(64, 1, 1);
		return true;
	}

	bool SetParameterBindingArray(
		UObject* Object,
		const FName MemberPropertyName,
		const TArray<FOptimusParameterBinding>& Bindings,
		FString& OutError)
	{
		FStructProperty* MemberProperty = FindFProperty<FStructProperty>(Object->GetClass(), MemberPropertyName);
		if (!MemberProperty || MemberProperty->Struct != FOptimusParameterBindingArray::StaticStruct())
		{
			OutError = FString::Printf(
				TEXT("Optimus reflected binding member %s has an unexpected type."),
				*MemberPropertyName.ToString());
			return false;
		}
		FArrayProperty* InnerArrayProperty = FindFProperty<FArrayProperty>(
			FOptimusParameterBindingArray::StaticStruct(),
			TEXT("InnerArray"));
		if (!InnerArrayProperty)
		{
			OutError = TEXT("FOptimusParameterBindingArray.InnerArray reflection is unavailable.");
			return false;
		}

		Object->Modify();
		MemberProperty->ContainerPtrToValuePtr<FOptimusParameterBindingArray>(Object)->InnerArray = Bindings;
		FPropertyChangedEvent Event(InnerArrayProperty, EPropertyChangeType::ValueSet);
		Event.SetActiveMemberProperty(MemberProperty);
		Object->PostEditChangeProperty(Event);
		return true;
	}

	bool SetSecondaryBodyBindings(
		UObject* Object,
		const TArray<FOptimusParameterBinding>& Bindings,
		FString& OutError)
	{
		FArrayProperty* GroupsProperty = FindFProperty<FArrayProperty>(
			Object->GetClass(),
			TEXT("SecondaryInputBindingGroups"));
		FStructProperty* GroupStructProperty = GroupsProperty
			? CastField<FStructProperty>(GroupsProperty->Inner)
			: nullptr;
		if (!GroupsProperty || !GroupStructProperty)
		{
			OutError = TEXT("Optimus secondary input group reflection is unavailable.");
			return false;
		}

		FStructProperty* GroupNameProperty = FindFProperty<FStructProperty>(
			GroupStructProperty->Struct,
			TEXT("GroupName"));
		FStructProperty* BindingArrayProperty = FindFProperty<FStructProperty>(
			GroupStructProperty->Struct,
			TEXT("BindingArray"));
		if (!GroupNameProperty
			|| GroupNameProperty->Struct != FOptimusValidatedName::StaticStruct()
			|| !BindingArrayProperty
			|| BindingArrayProperty->Struct != FOptimusParameterBindingArray::StaticStruct())
		{
			OutError = TEXT("Optimus secondary input group layout changed incompatibly.");
			return false;
		}

		Object->Modify();
		FScriptArrayHelper ArrayHelper(GroupsProperty, GroupsProperty->ContainerPtrToValuePtr<void>(Object));
		ArrayHelper.EmptyValues();
		const int32 GroupIndex = ArrayHelper.AddValue();
		void* GroupMemory = ArrayHelper.GetRawPtr(GroupIndex);
		GroupNameProperty->ContainerPtrToValuePtr<FOptimusValidatedName>(GroupMemory)->Name = BodyGroupPin;
		BindingArrayProperty->ContainerPtrToValuePtr<FOptimusParameterBindingArray>(GroupMemory)->InnerArray = Bindings;

		FPropertyChangedEvent Event(GroupsProperty, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(Event);
		return true;
	}

	const FString& GetBaseKernelSource()
	{
		static const FString Source = FString(TEXT(R"EFHLSL(
KERNEL
{
	float3 GarmentPosition = ReadGarmentPosition(Index);
	float4 GarmentTangentX = ReadGarmentTangentX(Index);
	float4 GarmentTangentZ = ReadGarmentTangentZ(Index);

	float RuntimeOffsetCm = ReadEF_GlobalClearanceOffsetCm()
		+ ReadEF_GarmentClearanceOffsetCm();
	RuntimeOffsetCm = isfinite(RuntimeOffsetCm) ? RuntimeOffsetCm : 0.0f;
	// V3 keeps the authored source mesh as the runtime mesh. For a legacy paired
	// shell this value scales the existing outer layer. For a native single-layer
	// garment it is a non-destructive surface inflate: no topology is invented and
	// a value of zero is an exact pass-through.
	float RuntimeInflateCm = ReadEF_GarmentInflateCm();
	RuntimeInflateCm = isfinite(RuntimeInflateCm)
		? clamp(RuntimeInflateCm, 0.0f, 2.0f)
		: 0.0f;
	float BodyMorphActivity = ReadEF_BodyMorphActivity();
	// SurfaceTarget already contains the exact animated morph magnitude. Activity
	// is only a gate; multiplying by the curve value here would apply the morph a
	// second time and make sub-unit weights follow quadratically.
	float BodyMorphGate = isfinite(BodyMorphActivity)
		&& abs(BodyMorphActivity) > 1.0e-4f
		? 1.0f
		: 0.0f;
	float BreastMorphActivity = ReadEF_BreastMorphActivity();
	float BreastMorphStrength = isfinite(BreastMorphActivity)
		? saturate(abs(BreastMorphActivity))
		: 0.0f;
	float CorrectionOverrideCm = ReadEF_MaximumCorrectionOverrideCm();
	CorrectionOverrideCm = isfinite(CorrectionOverrideCm)
		? max(CorrectionOverrideCm, 0.0f)
		: 0.0f;
	float MaximumAutomaticBodyShapeTravelCm =
		ReadEF_MaximumAutomaticBodyShapeTravelCm();
	MaximumAutomaticBodyShapeTravelCm =
		isfinite(MaximumAutomaticBodyShapeTravelCm)
		? clamp(MaximumAutomaticBodyShapeTravelCm, 0.0f, 32.0f)
		: 0.0f;
	// This reserve is consumed only through a compiler-generated inframammary
	// weight. It cannot affect the upper breast, torso, back, or unrelated clothes.
	float UnderBreastClearanceMaxCm =
		ReadEF_UnderBreastClearanceMaxCm();
	UnderBreastClearanceMaxCm =
		isfinite(UnderBreastClearanceMaxCm)
		? clamp(UnderBreastClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float LowerBodyMorphActivity = ReadEF_LowerBodyMorphActivity();
	float LowerBodyMorphStrength = isfinite(LowerBodyMorphActivity)
		? saturate(max(LowerBodyMorphActivity, 0.0f))
		: 0.0f;
	float LowerBodyMorphClearanceMaxCm =
		ReadEF_LowerBodyMorphClearanceMaxCm();
	LowerBodyMorphClearanceMaxCm =
		isfinite(LowerBodyMorphClearanceMaxCm)
		? clamp(LowerBodyMorphClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	// Imported DAZ garment tangents are not a certified exterior direction at
	// every seam. V3 never hides or replaces the garment on invalid/stale data;
	// the safe fallback is the exact upstream UE-deformed source position.
	bool HasFinitePosition = all(isfinite(GarmentPosition));
	float3 ConservativePosition = HasFinitePosition
		? GarmentPosition
		: float3(0.0f, 0.0f, 0.0f);
	bool HasUsableTangentX = all(isfinite(GarmentTangentX.xyz))
		&& dot(GarmentTangentX.xyz, GarmentTangentX.xyz) > 1.0e-12f;
	bool HasUsableTangentZ = all(isfinite(GarmentTangentZ.xyz))
		&& dot(GarmentTangentZ.xyz, GarmentTangentZ.xyz) > 1.0e-12f;
	float3 SafeTangentDirection = HasUsableTangentX
		? normalize(GarmentTangentX.xyz)
		: float3(1.0f, 0.0f, 0.0f);
	float3 SafeNormalDirection = HasUsableTangentZ
		? normalize(GarmentTangentZ.xyz)
		: float3(0.0f, 0.0f, 1.0f);
	if (!HasUsableTangentZ)
	{
		float3 ReferenceAxis = abs(SafeTangentDirection.z) < 0.999f
			? float3(0.0f, 0.0f, 1.0f)
			: float3(0.0f, 1.0f, 0.0f);
		SafeNormalDirection = normalize(cross(SafeTangentDirection, ReferenceAxis));
	}
	if (!HasUsableTangentX)
	{
		float3 ReferenceAxis = abs(SafeNormalDirection.z) < 0.999f
			? float3(0.0f, 0.0f, 1.0f)
			: float3(0.0f, 1.0f, 0.0f);
		SafeTangentDirection = normalize(cross(ReferenceAxis, SafeNormalDirection));
	}
	GarmentTangentX = float4(
		HasUsableTangentX ? GarmentTangentX.xyz : SafeTangentDirection,
		isfinite(GarmentTangentX.w) ? GarmentTangentX.w : 1.0f);
	GarmentTangentZ = float4(
		HasUsableTangentZ ? GarmentTangentZ.xyz : SafeNormalDirection,
		isfinite(GarmentTangentZ.w) ? GarmentTangentZ.w : 1.0f);

	if (!HasFinitePosition)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	int BindingVertexCount = ReadEF_BindingVertexCount();
	int GarmentLODIndex = ReadEF_GarmentLODIndex();
	int BodyLODIndex = ReadEF_BodyLODIndex();
	if (BindingVertexCount <= 0
		|| Index >= (uint)BindingVertexCount
		|| GarmentLODIndex < 0
		|| BodyLODIndex < 0)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

)EFHLSL")) + FString(TEXT(R"EFHLSL(

	StructuredBuffer<int4> BodyTriangleAndModeBuffer = ReadEF_BodyTriangleAndMode();
	StructuredBuffer<float4> BarycentricsAndFollowWeightBuffer = ReadEF_BarycentricsAndFollowWeight();
	StructuredBuffer<float4> RestOffsetAndClearanceCmBuffer = ReadEF_RestOffsetAndClearanceCm();
	StructuredBuffer<float2> MaximumCorrectionAndRestGapCmBuffer = ReadEF_MaximumCorrectionAndRestGapCm();
	StructuredBuffer<int2> ThicknessReferenceAndLayerBuffer = ReadEF_ThicknessReferenceAndLayer();
	StructuredBuffer<float> UnderBreastGuardWeightsBuffer = ReadEF_UnderBreastGuardWeights();
	StructuredBuffer<float> LowerBodyMorphGuardWeightsBuffer =
		ReadEF_LowerBodyMorphGuardWeights();

	uint TriangleBufferCount = 0;
	uint BarycentricBufferCount = 0;
	uint RestOffsetBufferCount = 0;
	uint LimitBufferCount = 0;
	uint ThicknessBufferCount = 0;
	uint UnderBreastGuardBufferCount = 0;
	uint LowerBodyMorphGuardBufferCount = 0;
	uint IgnoredStride = 0;
	BodyTriangleAndModeBuffer.GetDimensions(TriangleBufferCount, IgnoredStride);
	BarycentricsAndFollowWeightBuffer.GetDimensions(BarycentricBufferCount, IgnoredStride);
	RestOffsetAndClearanceCmBuffer.GetDimensions(RestOffsetBufferCount, IgnoredStride);
	MaximumCorrectionAndRestGapCmBuffer.GetDimensions(LimitBufferCount, IgnoredStride);
	ThicknessReferenceAndLayerBuffer.GetDimensions(ThicknessBufferCount, IgnoredStride);
	UnderBreastGuardWeightsBuffer.GetDimensions(UnderBreastGuardBufferCount, IgnoredStride);
	LowerBodyMorphGuardWeightsBuffer.GetDimensions(
		LowerBodyMorphGuardBufferCount,
		IgnoredStride);
	if (Index >= TriangleBufferCount
		|| Index >= BarycentricBufferCount
		|| Index >= RestOffsetBufferCount
		|| Index >= LimitBufferCount
		|| Index >= ThicknessBufferCount
		|| Index >= UnderBreastGuardBufferCount
		|| Index >= LowerBodyMorphGuardBufferCount)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	int4 BodyTriangleAndMode = BodyTriangleAndModeBuffer[Index];
	float4 BarycentricsAndFollowWeight = BarycentricsAndFollowWeightBuffer[Index];
	float4 RestOffsetAndClearanceCm = RestOffsetAndClearanceCmBuffer[Index];
	float2 MaximumCorrectionAndRestGapCm = MaximumCorrectionAndRestGapCmBuffer[Index];
	int2 ThicknessReferenceAndLayer = ThicknessReferenceAndLayerBuffer[Index];
	float UnderBreastGuardWeight = UnderBreastGuardWeightsBuffer[Index];
	float LowerBodyMorphGuardWeight = LowerBodyMorphGuardWeightsBuffer[Index];

	int BodyVertexCountValue = ReadEF_BodyVertexCount();
	uint BodyVertexCount = (uint)max(BodyVertexCountValue, 0);
	if (any(BodyTriangleAndMode.xyz < 0)
		|| BodyVertexCountValue <= 0
		|| any((uint3)BodyTriangleAndMode.xyz >= BodyVertexCount)
		|| BodyTriangleAndMode.w < 0
		|| BodyTriangleAndMode.w > 3
		|| !all(isfinite(BarycentricsAndFollowWeight))
		|| !all(isfinite(RestOffsetAndClearanceCm))
		|| !all(isfinite(MaximumCorrectionAndRestGapCm))
		|| !isfinite(UnderBreastGuardWeight)
		|| UnderBreastGuardWeight < 0.0f
		|| UnderBreastGuardWeight > 1.0f
		|| !isfinite(LowerBodyMorphGuardWeight)
		|| LowerBodyMorphGuardWeight < 0.0f
		|| LowerBodyMorphGuardWeight > 1.0f
		|| ThicknessReferenceAndLayer.y < 0
		|| ThicknessReferenceAndLayer.y > 1)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}
	if (BodyTriangleAndMode.w == 3)
	{
		// Explicit catalog-derived anatomy exclusion: never sample or anchor to the
		// excluded anatomical surface. Preserve the complete upstream
		// DAZ/skinning/morph/Chaos result and apply only the bounded, compiler-masked
		// lower-body morph reserve along the garment's safe upstream normal.
		float PreserveUpstreamLowerBodyReserveCm = LowerBodyMorphStrength
			* LowerBodyMorphGuardWeight
			* LowerBodyMorphClearanceMaxCm;
		float3 PreserveUpstreamPosition = PreserveUpstreamLowerBodyReserveCm > 0.0f
			? GarmentPosition
				+ SafeNormalDirection * PreserveUpstreamLowerBodyReserveCm
			: GarmentPosition;
		WriteCorrectedPosition(Index, PreserveUpstreamPosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	float3 RuntimeGarmentPosition = GarmentPosition;
	float3 RuntimeRestOffsetCm = RestOffsetAndClearanceCm.xyz;
	float RuntimeTargetClearanceCm = RestOffsetAndClearanceCm.w;
	if (ThicknessReferenceAndLayer.y == 1)
	{
		int InnerReference = ThicknessReferenceAndLayer.x;
		if (InnerReference < 0
			|| InnerReference >= BindingVertexCount
			|| (uint)InnerReference >= RestOffsetBufferCount
			|| (uint)InnerReference >= ThicknessBufferCount)
		{
			WriteCorrectedPosition(Index, ConservativePosition);
			WritePreservedTangentX(Index, GarmentTangentX);
			WritePreservedTangentZ(Index, GarmentTangentZ);
			return;
		}
		float4 InnerRestOffsetAndClearanceCm =
			RestOffsetAndClearanceCmBuffer[InnerReference];
		float3 CompiledLayerVectorCm =
			RestOffsetAndClearanceCm.xyz - InnerRestOffsetAndClearanceCm.xyz;
		float CompiledLayerLengthCm = length(CompiledLayerVectorCm);
		float3 InnerGarmentPosition = ReadGarmentPosition((uint)InnerReference);
		if (!all(isfinite(InnerRestOffsetAndClearanceCm))
			|| !all(isfinite(InnerGarmentPosition))
			|| CompiledLayerLengthCm <= 1.0e-6f)
		{
			WriteCorrectedPosition(Index, ConservativePosition);
			WritePreservedTangentX(Index, GarmentTangentX);
			WritePreservedTangentZ(Index, GarmentTangentZ);
			return;
		}
		float ThicknessScale = RuntimeInflateCm / CompiledLayerLengthCm;
		RuntimeGarmentPosition = InnerGarmentPosition
			+ (GarmentPosition - InnerGarmentPosition) * ThicknessScale;
		RuntimeRestOffsetCm = InnerRestOffsetAndClearanceCm.xyz
			+ CompiledLayerVectorCm * ThicknessScale;
		RuntimeTargetClearanceCm = InnerRestOffsetAndClearanceCm.w
			+ max(RuntimeRestOffsetCm.z - InnerRestOffsetAndClearanceCm.z, 0.0f);
	}

	float BarycentricSum = BarycentricsAndFollowWeight.x
		+ BarycentricsAndFollowWeight.y
		+ BarycentricsAndFollowWeight.z;
	if (abs(BarycentricSum) <= 1.0e-8f)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}
	float3 Barycentrics = BarycentricsAndFollowWeight.xyz / BarycentricSum;

)EFHLSL")) + TEXT(R"EFHLSL(

	float3 BodyP0 = Body::ReadBodyPosition((uint)BodyTriangleAndMode.x);
	float3 BodyP1 = Body::ReadBodyPosition((uint)BodyTriangleAndMode.y);
	float3 BodyP2 = Body::ReadBodyPosition((uint)BodyTriangleAndMode.z);
	float4x4 BodyToGarment = ReadEF_BodyToGarmentTransform();
	BodyP0 = mul(float4(BodyP0, 1.0f), BodyToGarment).xyz;
	BodyP1 = mul(float4(BodyP1, 1.0f), BodyToGarment).xyz;
	BodyP2 = mul(float4(BodyP2, 1.0f), BodyToGarment).xyz;
	if (!all(isfinite(BodyP0)) || !all(isfinite(BodyP1)) || !all(isfinite(BodyP2)))
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	float3 Edge01 = BodyP1 - BodyP0;
	float3 Edge02 = BodyP2 - BodyP0;
	float3 UnnormalizedNormal = cross(Edge01, Edge02);
	float GeometricNormalLengthSquared = dot(UnnormalizedNormal, UnnormalizedNormal);
	if (GeometricNormalLengthSquared <= 1.0e-12f)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	float3 GeometricNormal = UnnormalizedNormal * rsqrt(GeometricNormalLengthSquared);
	// Schema 4 uses only the explicitly oriented triangle built from the final
	// animated body positions. A secondary Optimus read may expose deformed
	// Position while independently falling back to static TangentZ; excluding
	// TangentZ makes compile, runtime and readback QA mathematically identical.
	float3 SurfaceNormal = GeometricNormal;
	float3 SurfaceTangent = Edge01 - SurfaceNormal * dot(Edge01, SurfaceNormal);
	float SurfaceTangentLengthSquared = dot(SurfaceTangent, SurfaceTangent);
	if (SurfaceTangentLengthSquared <= 1.0e-12f)
	{
		SurfaceTangent = Edge02 - SurfaceNormal * dot(Edge02, SurfaceNormal);
		SurfaceTangentLengthSquared = dot(SurfaceTangent, SurfaceTangent);
	}
	if (SurfaceTangentLengthSquared <= 1.0e-12f)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}
	SurfaceTangent *= rsqrt(SurfaceTangentLengthSquared);
	float3 SurfaceBitangent = normalize(cross(SurfaceNormal, SurfaceTangent));
	// Match the compiler's final Gram-Schmidt step exactly: T = (N x T) x N.
	SurfaceTangent = normalize(cross(SurfaceBitangent, SurfaceNormal));
	float3 SurfaceAnchor = BodyP0 * Barycentrics.x
		+ BodyP1 * Barycentrics.y
		+ BodyP2 * Barycentrics.z;
	float3 SurfaceTarget = SurfaceAnchor
		+ SurfaceTangent * RuntimeRestOffsetCm.x
		+ SurfaceBitangent * RuntimeRestOffsetCm.y
		+ SurfaceNormal * RuntimeRestOffsetCm.z;

	int SurfaceMode = BodyTriangleAndMode.w;
	float3 SurfaceDelta = SurfaceTarget - RuntimeGarmentPosition;
	float SurfaceDeltaLengthCm = length(SurfaceDelta);
	float SurfaceDeltaScale = SurfaceDeltaLengthCm > 1.0e-8f
		? min(1.0f, MaximumAutomaticBodyShapeTravelCm / SurfaceDeltaLengthCm)
		: 0.0f;
	float3 BoundedSurfaceDelta = SurfaceDelta * SurfaceDeltaScale;
	float OutwardSurfaceTravelCm = clamp(
		max(dot(BoundedSurfaceDelta, SurfaceNormal), 0.0f),
		0.0f,
		MaximumAutomaticBodyShapeTravelCm);
	// Drive a compact clearance reserve from the actual final surface travel. This
	// includes tangential/downward motion at the inframammary fold that an
	// outward-only dot product misses. The compiler-authored mask keeps this out of
	// the upper breast, chest and every unrelated clothing region.
	float UnderBreastTravelGate = smoothstep(
		0.02f,
		0.75f,
		min(SurfaceDeltaLengthCm, MaximumAutomaticBodyShapeTravelCm));
	float UnderBreastReserveCm = BreastMorphStrength
		* UnderBreastGuardWeight
		* UnderBreastClearanceMaxCm
		* UnderBreastTravelGate;
	float LowerBodyMorphReserveCm = LowerBodyMorphStrength
		* LowerBodyMorphGuardWeight
		* LowerBodyMorphClearanceMaxCm;
	float CompiledFollowWeight = saturate(BarycentricsAndFollowWeight.w);
	float FollowWeight = SurfaceMode == 2
		? CompiledFollowWeight * BodyMorphGate
		: CompiledFollowWeight;
	// V4 CollisionOnly remains an exact pass-through while no body morph is active.
	// During a morph it uses the compiler's continuous geometry confidence and can
	// transport in both directions, so reductions follow the skin as faithfully as
	// expansions. The final clearance projection below remains strictly unilateral.
	float3 FollowDelta = (SurfaceMode == 2 ? BoundedSurfaceDelta : SurfaceDelta)
		* FollowWeight;
	if (SurfaceMode != 2)
	{
		// Preserve the established V26 behavior for legacy SurfaceFollow/Hybrid data.
		FollowDelta -= SurfaceNormal * min(dot(FollowDelta, SurfaceNormal), 0.0f);
	}
	float3 CandidatePosition = RuntimeGarmentPosition + FollowDelta;

	float BaseTargetGapCm = max(RuntimeTargetClearanceCm, 0.0f);
	// Only SurfaceFollow preserves a larger compiled rest gap.
	if (SurfaceMode == 0)
	{
		BaseTargetGapCm = max(BaseTargetGapCm, RuntimeRestOffsetCm.z);
	}
	// A source-authored single layer has no generated inner/outer pairing. Its
	// runtime inflate is therefore expressed along the already-certified body
	// surface normal. This is deliberately distinct from a native Create Shell
	// edit, which must author real side walls/topology on the source asset.
	float SourceSurfaceInflateCm =
		(ThicknessReferenceAndLayer.x < 0 && ThicknessReferenceAndLayer.y == 0)
			? RuntimeInflateCm
			: 0.0f;
	float TargetGapCm = max(
		BaseTargetGapCm + RuntimeOffsetCm + SourceSurfaceInflateCm
			+ UnderBreastReserveCm + LowerBodyMorphReserveCm,
		0.0f);
	float SignedGapCm = dot(CandidatePosition - SurfaceAnchor, SurfaceNormal);
	float RequiredPushCm = max(TargetGapCm - SignedGapCm, 0.0f);
	float CompiledMaximumCorrectionCm = max(MaximumCorrectionAndRestGapCm.x, 0.0f);
	float MaximumCorrectionCm = CorrectionOverrideCm > 0.0f
		? CorrectionOverrideCm
		: CompiledMaximumCorrectionCm;
	// Runtime clearance and inflate are bounded independently by the V3 public
	// contract. Add their active budgets to the certified/override correction so
	// manual tuning remains useful without allowing a malformed gap to explode.
	float RuntimeCorrectionBudgetCm = clamp(max(RuntimeOffsetCm, 0.0f), 0.0f, 2.0f)
		+ clamp(RuntimeInflateCm, 0.0f, 2.0f);
	// The compiled correction remains the normal animation budget. When a body
	// shape itself travels farther (for example an extreme DAZ full-body morph),
	// extend only the available unilateral budget by that measured surface
	// advance. No correction is applied unless the current signed gap requires it.
	float MaximumAppliedPushCm = MaximumCorrectionCm
		+ RuntimeCorrectionBudgetCm
		+ OutwardSurfaceTravelCm * BodyMorphGate
		+ UnderBreastReserveCm
		+ LowerBodyMorphReserveCm;
	if (MaximumAppliedPushCm <= 0.0f)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	// The correction remains unilateral: it can only move outward, and is capped
	// by the certified/override correction plus the active runtime tuning budget.
	float AppliedPushCm = clamp(RequiredPushCm, 0.0f, MaximumAppliedPushCm);
	float3 CorrectedPosition = CandidatePosition + SurfaceNormal * AppliedPushCm;
	if (!all(isfinite(CorrectedPosition)))
	{
		CorrectedPosition = ConservativePosition;
	}

	WriteCorrectedPosition(Index, CorrectedPosition);
	// Position-only correction preserves the upstream DAZ/Chaos tangent basis exactly.
	WritePreservedTangentX(Index, GarmentTangentX);
	WritePreservedTangentZ(Index, GarmentTangentZ);
}
)EFHLSL");
		return Source;
	}

	const FString& GetCohesionKernelSource()
	{
		static const FString Source = FString(TEXT(R"EFHLSL(
KERNEL
{
	float3 RawPosition = ReadRawCorrectedPosition(Index);
	float3 UpstreamPosition = ReadUpstreamGarmentPosition(Index);
	float BodyMorphActivity = ReadEF_BodyMorphActivity();
	float LowerBodyMorphActivity = ReadEF_LowerBodyMorphActivity();
	float LowerBodyMorphStrength = isfinite(LowerBodyMorphActivity)
		? saturate(max(LowerBodyMorphActivity, 0.0f))
		: 0.0f;
	bool MorphActive = (isfinite(BodyMorphActivity)
		&& abs(BodyMorphActivity) > 1.0e-4f)
		|| LowerBodyMorphStrength > 1.0e-4f;
	float BreastMorphActivity = ReadEF_BreastMorphActivity();
	float BreastMorphStrength = isfinite(BreastMorphActivity)
		? saturate(abs(BreastMorphActivity))
		: 0.0f;
	bool HasFiniteInput = all(isfinite(RawPosition))
		&& all(isfinite(UpstreamPosition));
	float3 ConservativePosition = all(isfinite(RawPosition))
		? RawPosition
		: (all(isfinite(UpstreamPosition))
			? UpstreamPosition
			: float3(0.0f, 0.0f, 0.0f));
	int BindingVertexCount = ReadEF_BindingVertexCount();
	int BodyVertexCountValue = ReadEF_BodyVertexCount();
	int NeighborReferenceCountValue = ReadEF_NeighborReferenceCount();
	if (!HasFiniteInput
		|| !MorphActive
		|| BindingVertexCount <= 0
		|| Index >= (uint)BindingVertexCount
		|| BodyVertexCountValue <= 0
		|| NeighborReferenceCountValue < 0
		|| ReadEF_GarmentLODIndex() < 0
		|| ReadEF_BodyLODIndex() < 0)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}

	StructuredBuffer<int4> BodyTriangleAndModeBuffer = ReadEF_BodyTriangleAndMode();
	StructuredBuffer<float4> BarycentricsAndFollowWeightBuffer =
		ReadEF_BarycentricsAndFollowWeight();
	StructuredBuffer<float4> RestOffsetAndClearanceCmBuffer =
		ReadEF_RestOffsetAndClearanceCm();
	StructuredBuffer<float2> MaximumCorrectionAndRestGapCmBuffer =
		ReadEF_MaximumCorrectionAndRestGapCm();
	StructuredBuffer<int2> ThicknessReferenceAndLayerBuffer =
		ReadEF_ThicknessReferenceAndLayer();
	StructuredBuffer<float> UnderBreastGuardWeightsBuffer =
		ReadEF_UnderBreastGuardWeights();
	StructuredBuffer<float> LowerBodyMorphGuardWeightsBuffer =
		ReadEF_LowerBodyMorphGuardWeights();
	StructuredBuffer<int2> NeighborRangesBuffer = ReadEF_NeighborRanges();
	StructuredBuffer<int> NeighborIndicesBuffer = ReadEF_NeighborIndices();
	StructuredBuffer<float> LocalFeatureSizeCmBuffer = ReadEF_LocalFeatureSizeCm();

	uint TriangleBufferCount = 0;
	uint BarycentricBufferCount = 0;
	uint RestOffsetBufferCount = 0;
	uint LimitBufferCount = 0;
	uint ThicknessBufferCount = 0;
	uint UnderBreastGuardBufferCount = 0;
	uint LowerBodyMorphGuardBufferCount = 0;
	uint NeighborRangeBufferCount = 0;
	uint NeighborIndexBufferCount = 0;
	uint LocalFeatureBufferCount = 0;
	uint IgnoredStride = 0;
	BodyTriangleAndModeBuffer.GetDimensions(TriangleBufferCount, IgnoredStride);
	BarycentricsAndFollowWeightBuffer.GetDimensions(BarycentricBufferCount, IgnoredStride);
	RestOffsetAndClearanceCmBuffer.GetDimensions(RestOffsetBufferCount, IgnoredStride);
	MaximumCorrectionAndRestGapCmBuffer.GetDimensions(LimitBufferCount, IgnoredStride);
	ThicknessReferenceAndLayerBuffer.GetDimensions(ThicknessBufferCount, IgnoredStride);
	UnderBreastGuardWeightsBuffer.GetDimensions(UnderBreastGuardBufferCount, IgnoredStride);
	LowerBodyMorphGuardWeightsBuffer.GetDimensions(
		LowerBodyMorphGuardBufferCount,
		IgnoredStride);
	NeighborRangesBuffer.GetDimensions(NeighborRangeBufferCount, IgnoredStride);
	NeighborIndicesBuffer.GetDimensions(NeighborIndexBufferCount, IgnoredStride);
	LocalFeatureSizeCmBuffer.GetDimensions(LocalFeatureBufferCount, IgnoredStride);
	if (TriangleBufferCount < (uint)BindingVertexCount
		|| BarycentricBufferCount < (uint)BindingVertexCount
		|| RestOffsetBufferCount < (uint)BindingVertexCount
		|| LimitBufferCount < (uint)BindingVertexCount
		|| ThicknessBufferCount < (uint)BindingVertexCount
		|| NeighborRangeBufferCount < (uint)BindingVertexCount
		|| NeighborIndexBufferCount < (uint)NeighborReferenceCountValue
		|| LocalFeatureBufferCount < (uint)BindingVertexCount
		|| UnderBreastGuardBufferCount < (uint)BindingVertexCount
		|| LowerBodyMorphGuardBufferCount < (uint)BindingVertexCount)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}

	int4 PrimaryTriangleAndMode = BodyTriangleAndModeBuffer[Index];
	float4 PrimaryBarycentricsAndFollowWeight =
		BarycentricsAndFollowWeightBuffer[Index];
	float4 PrimaryRestOffsetAndClearanceCm =
		RestOffsetAndClearanceCmBuffer[Index];
	float2 PrimaryMaximumCorrectionAndRestGapCm =
		MaximumCorrectionAndRestGapCmBuffer[Index];
	int2 PrimaryThicknessReferenceAndLayer =
		ThicknessReferenceAndLayerBuffer[Index];
	float OwnFollowWeight = saturate(PrimaryBarycentricsAndFollowWeight.w);
	float OwnLocalFeatureSizeCm = LocalFeatureSizeCmBuffer[Index];
	float UnderBreastGuardWeight = UnderBreastGuardWeightsBuffer[Index];
	float LowerBodyMorphGuardWeight = LowerBodyMorphGuardWeightsBuffer[Index];
	if (PrimaryTriangleAndMode.w == 3
		|| OwnFollowWeight <= 1.0e-4f
		|| !isfinite(OwnLocalFeatureSizeCm)
		|| OwnLocalFeatureSizeCm <= 1.0e-5f
		|| !isfinite(UnderBreastGuardWeight)
		|| UnderBreastGuardWeight < 0.0f
		|| UnderBreastGuardWeight > 1.0f
		|| !isfinite(LowerBodyMorphGuardWeight)
		|| LowerBodyMorphGuardWeight < 0.0f
		|| LowerBodyMorphGuardWeight > 1.0f)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}

	int2 NeighborRange = NeighborRangesBuffer[Index];
	if (NeighborRange.x < 0
		|| NeighborRange.y < 0
		|| NeighborRange.y > 256
		|| NeighborRange.x > NeighborReferenceCountValue
		|| NeighborRange.y > NeighborReferenceCountValue - NeighborRange.x)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}
)EFHLSL")) + FString(TEXT(R"EFHLSL(

	float RuntimeInflateCm = ReadEF_GarmentInflateCm();
	RuntimeInflateCm = isfinite(RuntimeInflateCm)
		? clamp(RuntimeInflateCm, 0.0f, 2.0f)
		: 0.0f;
	float3 EffectiveUpstreamPosition = UpstreamPosition;
	float RuntimeTargetClearanceCm = PrimaryRestOffsetAndClearanceCm.w;
	if (PrimaryThicknessReferenceAndLayer.y == 1)
	{
		int InnerReference = PrimaryThicknessReferenceAndLayer.x;
		if (InnerReference < 0
			|| InnerReference >= BindingVertexCount
			|| (uint)InnerReference >= RestOffsetBufferCount
			|| (uint)InnerReference >= ThicknessBufferCount)
		{
			WriteCohesivePosition(Index, ConservativePosition);
			return;
		}
		float4 InnerRest = RestOffsetAndClearanceCmBuffer[InnerReference];
		float3 InnerUpstreamPosition =
			ReadUpstreamGarmentPosition((uint)InnerReference);
		float3 LayerVectorCm = PrimaryRestOffsetAndClearanceCm.xyz
			- InnerRest.xyz;
		float LayerLengthCm = length(LayerVectorCm);
		if (!all(isfinite(InnerRest))
			|| !all(isfinite(InnerUpstreamPosition))
			|| LayerLengthCm <= 1.0e-6f)
		{
			WriteCohesivePosition(Index, ConservativePosition);
			return;
		}
		float ThicknessScale = RuntimeInflateCm / LayerLengthCm;
		EffectiveUpstreamPosition = InnerUpstreamPosition
			+ (UpstreamPosition - InnerUpstreamPosition) * ThicknessScale;
		float3 EffectiveRest = InnerRest.xyz + LayerVectorCm * ThicknessScale;
		RuntimeTargetClearanceCm = InnerRest.w
			+ max(EffectiveRest.z - InnerRest.z, 0.0f);
	}

	float3 RawCorrection = RawPosition - EffectiveUpstreamPosition;
	float3 ConstraintCorrectionSum = RawCorrection;
	float ConstraintWeightSum = 1.0f;
	[loop]
	for (int LocalNeighborIndex = 0;
		LocalNeighborIndex < NeighborRange.y;
		++LocalNeighborIndex)
	{
		int NeighborVertexIndex =
			NeighborIndicesBuffer[NeighborRange.x + LocalNeighborIndex];
		if (NeighborVertexIndex < 0
			|| NeighborVertexIndex >= BindingVertexCount
			|| NeighborVertexIndex == (int)Index)
		{
			continue;
		}
		int4 NeighborTriangleAndMode =
			BodyTriangleAndModeBuffer[NeighborVertexIndex];
		if (NeighborTriangleAndMode.w == 3)
		{
			continue;
		}
		float3 NeighborRawPosition =
			ReadRawCorrectedPosition((uint)NeighborVertexIndex);
		float3 NeighborUpstreamPosition =
			ReadUpstreamGarmentPosition((uint)NeighborVertexIndex);
		float NeighborLocalFeatureSizeCm =
			LocalFeatureSizeCmBuffer[NeighborVertexIndex];
		if (!all(isfinite(NeighborRawPosition))
			|| !all(isfinite(NeighborUpstreamPosition))
			|| !isfinite(NeighborLocalFeatureSizeCm)
			|| NeighborLocalFeatureSizeCm <= 1.0e-5f)
		{
			continue;
		}
		float NeighborFollowWeight = saturate(
			BarycentricsAndFollowWeightBuffer[NeighborVertexIndex].w);
		int2 NeighborThicknessReferenceAndLayer =
			ThicknessReferenceAndLayerBuffer[NeighborVertexIndex];
		if (NeighborThicknessReferenceAndLayer.y == 1)
		{
			int NeighborInnerReference =
				NeighborThicknessReferenceAndLayer.x;
			if (NeighborInnerReference < 0
				|| NeighborInnerReference >= BindingVertexCount
				|| (uint)NeighborInnerReference >= RestOffsetBufferCount)
			{
				continue;
			}
			float4 NeighborRest =
				RestOffsetAndClearanceCmBuffer[NeighborVertexIndex];
			float4 NeighborInnerRest =
				RestOffsetAndClearanceCmBuffer[NeighborInnerReference];
			float3 NeighborInnerUpstream =
				ReadUpstreamGarmentPosition((uint)NeighborInnerReference);
			float3 NeighborLayerVectorCm = NeighborRest.xyz
				- NeighborInnerRest.xyz;
			float NeighborLayerLengthCm = length(NeighborLayerVectorCm);
			if (!all(isfinite(NeighborRest))
				|| !all(isfinite(NeighborInnerRest))
				|| !all(isfinite(NeighborInnerUpstream))
				|| NeighborLayerLengthCm <= 1.0e-6f)
			{
				continue;
			}
			NeighborUpstreamPosition = NeighborInnerUpstream
				+ (NeighborUpstreamPosition - NeighborInnerUpstream)
					* (RuntimeInflateCm / NeighborLayerLengthCm);
		}

		float3 NeighborCorrection = NeighborRawPosition
			- NeighborUpstreamPosition;
		float3 CorrectionDifference = RawCorrection - NeighborCorrection;
		float CorrectionDifferenceLength = length(CorrectionDifference);
		float UpstreamEdgeLengthCm = length(
			EffectiveUpstreamPosition - NeighborUpstreamPosition);
		// True topology adjacency is distinct from import-vertex weld groups. A
		// symmetric midpoint proposal cannot make the two endpoints cross, while the
		// local feature bound protects small or slender triangles.
		float MaximumCorrectionGradientCm = min(
			UpstreamEdgeLengthCm * 0.45f,
			min(OwnLocalFeatureSizeCm, NeighborLocalFeatureSizeCm) * 0.40f);
		if (!isfinite(MaximumCorrectionGradientCm)
			|| MaximumCorrectionGradientCm <= 1.0e-6f)
		{
			continue;
		}
		float DifferenceScale = CorrectionDifferenceLength > 1.0e-8f
			? min(1.0f,
				MaximumCorrectionGradientCm / CorrectionDifferenceLength)
			: 0.0f;
		float3 PairMidpoint = (RawCorrection + NeighborCorrection) * 0.5f;
		float3 PairProposalForOwn = PairMidpoint
			+ CorrectionDifference * (0.5f * DifferenceScale);
		float PairConfidence = 0.25f
			+ 0.75f * min(OwnFollowWeight, NeighborFollowWeight);
		ConstraintCorrectionSum += PairProposalForOwn * PairConfidence;
		ConstraintWeightSum += PairConfidence;
	}

	float3 ConsensusCorrection = ConstraintCorrectionSum
		/ max(ConstraintWeightSum, 1.0e-6f);
	float CohesionBlend = saturate(OwnFollowWeight * 4.0f);
	float3 CohesivePosition = EffectiveUpstreamPosition
		+ lerp(RawCorrection, ConsensusCorrection, CohesionBlend);
)EFHLSL")) + FString(TEXT(R"EFHLSL(

	float PrimaryBarycentricSum = dot(
		PrimaryBarycentricsAndFollowWeight.xyz,
		float3(1.0f, 1.0f, 1.0f));
	uint BodyVertexCount = (uint)BodyVertexCountValue;
	if (any(PrimaryTriangleAndMode.xyz < 0)
		|| any((uint3)PrimaryTriangleAndMode.xyz >= BodyVertexCount)
		|| !all(isfinite(PrimaryBarycentricsAndFollowWeight))
		|| !all(isfinite(PrimaryRestOffsetAndClearanceCm))
		|| !all(isfinite(PrimaryMaximumCorrectionAndRestGapCm))
		|| abs(PrimaryBarycentricSum) <= 1.0e-8f)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}
	float3 PrimaryBarycentrics =
		PrimaryBarycentricsAndFollowWeight.xyz / PrimaryBarycentricSum;
	float3 BodyP0 = Body::ReadBodyPosition((uint)PrimaryTriangleAndMode.x);
	float3 BodyP1 = Body::ReadBodyPosition((uint)PrimaryTriangleAndMode.y);
	float3 BodyP2 = Body::ReadBodyPosition((uint)PrimaryTriangleAndMode.z);
	float4x4 BodyToGarment = ReadEF_BodyToGarmentTransform();
	BodyP0 = mul(float4(BodyP0, 1.0f), BodyToGarment).xyz;
	BodyP1 = mul(float4(BodyP1, 1.0f), BodyToGarment).xyz;
	BodyP2 = mul(float4(BodyP2, 1.0f), BodyToGarment).xyz;
	float3 Edge01 = BodyP1 - BodyP0;
	float3 Edge02 = BodyP2 - BodyP0;
	float3 UnnormalizedNormal = cross(Edge01, Edge02);
	float NormalLengthSquared = dot(UnnormalizedNormal, UnnormalizedNormal);
	if (!all(isfinite(BodyP0))
		|| !all(isfinite(BodyP1))
		|| !all(isfinite(BodyP2))
		|| NormalLengthSquared <= 1.0e-12f)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}
	float3 SurfaceNormal = UnnormalizedNormal * rsqrt(NormalLengthSquared);
	float3 SurfaceTangent = Edge01
		- SurfaceNormal * dot(Edge01, SurfaceNormal);
	float SurfaceTangentLengthSquared = dot(SurfaceTangent, SurfaceTangent);
	if (SurfaceTangentLengthSquared <= 1.0e-12f)
	{
		SurfaceTangent = Edge02
			- SurfaceNormal * dot(Edge02, SurfaceNormal);
		SurfaceTangentLengthSquared = dot(SurfaceTangent, SurfaceTangent);
	}
	if (SurfaceTangentLengthSquared <= 1.0e-12f)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}
	SurfaceTangent *= rsqrt(SurfaceTangentLengthSquared);
	float3 SurfaceBitangent = normalize(cross(SurfaceNormal, SurfaceTangent));
	SurfaceTangent = normalize(cross(SurfaceBitangent, SurfaceNormal));
	float3 SurfaceAnchor = BodyP0 * PrimaryBarycentrics.x
		+ BodyP1 * PrimaryBarycentrics.y
		+ BodyP2 * PrimaryBarycentrics.z;
	float MaximumAutomaticBodyShapeTravelCm =
		ReadEF_MaximumAutomaticBodyShapeTravelCm();
	MaximumAutomaticBodyShapeTravelCm =
		isfinite(MaximumAutomaticBodyShapeTravelCm)
		? clamp(MaximumAutomaticBodyShapeTravelCm, 0.0f, 32.0f)
		: 0.0f;
	float3 SurfaceTarget = SurfaceAnchor
		+ SurfaceTangent * PrimaryRestOffsetAndClearanceCm.x
		+ SurfaceBitangent * PrimaryRestOffsetAndClearanceCm.y
		+ SurfaceNormal * PrimaryRestOffsetAndClearanceCm.z;
	float3 SurfaceDelta = SurfaceTarget - EffectiveUpstreamPosition;
	float SurfaceDeltaLengthCm = length(SurfaceDelta);
	float SurfaceDeltaScale = SurfaceDeltaLengthCm > 1.0e-8f
		? min(1.0f, MaximumAutomaticBodyShapeTravelCm / SurfaceDeltaLengthCm)
		: 0.0f;
	float OutwardSurfaceTravelCm = clamp(
		max(dot(SurfaceDelta * SurfaceDeltaScale, SurfaceNormal), 0.0f),
		0.0f,
		MaximumAutomaticBodyShapeTravelCm);
	float UnderBreastClearanceMaxCm = ReadEF_UnderBreastClearanceMaxCm();
	UnderBreastClearanceMaxCm = isfinite(UnderBreastClearanceMaxCm)
		? clamp(UnderBreastClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float UnderBreastTravelGate = smoothstep(
		0.02f,
		0.75f,
		min(SurfaceDeltaLengthCm, MaximumAutomaticBodyShapeTravelCm));
	float UnderBreastReserveCm = BreastMorphStrength
		* UnderBreastGuardWeight
		* UnderBreastClearanceMaxCm
		* UnderBreastTravelGate;
	float LowerBodyMorphClearanceMaxCm =
		ReadEF_LowerBodyMorphClearanceMaxCm();
	LowerBodyMorphClearanceMaxCm = isfinite(LowerBodyMorphClearanceMaxCm)
		? clamp(LowerBodyMorphClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float LowerBodyMorphReserveCm = LowerBodyMorphStrength
		* LowerBodyMorphGuardWeight
		* LowerBodyMorphClearanceMaxCm;
	float BaseTargetGapCm = max(RuntimeTargetClearanceCm, 0.0f);
	if (PrimaryTriangleAndMode.w == 0)
	{
		BaseTargetGapCm = max(
			BaseTargetGapCm,
			PrimaryRestOffsetAndClearanceCm.z);
	}
	float SourceSurfaceInflateCm =
		(PrimaryThicknessReferenceAndLayer.x < 0
			&& PrimaryThicknessReferenceAndLayer.y == 0)
			? RuntimeInflateCm
			: 0.0f;
	float RuntimeOffsetCm = ReadEF_GlobalClearanceOffsetCm()
		+ ReadEF_GarmentClearanceOffsetCm();
	RuntimeOffsetCm = isfinite(RuntimeOffsetCm) ? RuntimeOffsetCm : 0.0f;
	float TargetGapCm = max(
		BaseTargetGapCm + RuntimeOffsetCm + SourceSurfaceInflateCm
			+ UnderBreastReserveCm + LowerBodyMorphReserveCm,
		0.0f);
	float RequiredPushCm = max(
		TargetGapCm
			- dot(CohesivePosition - SurfaceAnchor, SurfaceNormal),
		0.0f);
	float CorrectionOverrideCm = ReadEF_MaximumCorrectionOverrideCm();
	CorrectionOverrideCm = isfinite(CorrectionOverrideCm)
		? max(CorrectionOverrideCm, 0.0f)
		: 0.0f;
	float MaximumCorrectionCm = CorrectionOverrideCm > 0.0f
		? CorrectionOverrideCm
		: max(PrimaryMaximumCorrectionAndRestGapCm.x, 0.0f);
	float RuntimeCorrectionBudgetCm =
		clamp(max(RuntimeOffsetCm, 0.0f), 0.0f, 2.0f)
		+ RuntimeInflateCm;
	float AllowedAdditionalPushCm = MaximumCorrectionCm
		+ RuntimeCorrectionBudgetCm
		+ UnderBreastReserveCm
		+ LowerBodyMorphReserveCm;
	// The input position has already passed the unilateral Base constraint. A
	// regularization step that cannot be re-opened inside the same certified
	// budget is rejected instead of creating an unbounded outward spike.
	if (!isfinite(RequiredPushCm)
		|| RequiredPushCm > AllowedAdditionalPushCm + 1.0e-5f)
	{
		WriteCohesivePosition(Index, ConservativePosition);
		return;
	}
	float3 ProjectedPosition = CohesivePosition
		+ SurfaceNormal * RequiredPushCm;
	float MaximumTotalTravelCm = MaximumAutomaticBodyShapeTravelCm
		+ AllowedAdditionalPushCm;
	if (!all(isfinite(ProjectedPosition))
		|| length(ProjectedPosition - EffectiveUpstreamPosition)
			> MaximumTotalTravelCm + 1.0e-4f)
	{
		ProjectedPosition = ConservativePosition;
	}
	WriteCohesivePosition(Index, ProjectedPosition);
}
)EFHLSL"));
		return Source;
	}

	const FString& GetWitnessKernelSource()
	{
		static const FString Source = FString(TEXT(R"EFHLSL(
KERNEL
{
	float3 BasePosition = ReadBaseCorrectedPosition(Index);
	float4 BaseTangentX = ReadBaseTangentX(Index);
	float4 BaseTangentZ = ReadBaseTangentZ(Index);

	float RuntimeOffsetCm = ReadEF_GlobalClearanceOffsetCm()
		+ ReadEF_GarmentClearanceOffsetCm();
	RuntimeOffsetCm = isfinite(RuntimeOffsetCm) ? RuntimeOffsetCm : 0.0f;
	float RuntimeInflateCm = ReadEF_GarmentInflateCm();
	RuntimeInflateCm = isfinite(RuntimeInflateCm)
		? clamp(RuntimeInflateCm, 0.0f, 2.0f)
		: 0.0f;
	float BreastMorphActivity = ReadEF_BreastMorphActivity();
	float BreastMorphStrength = isfinite(BreastMorphActivity)
		? saturate(abs(BreastMorphActivity))
		: 0.0f;
	float UnderBreastClearanceMaxCm = ReadEF_UnderBreastClearanceMaxCm();
	UnderBreastClearanceMaxCm = isfinite(UnderBreastClearanceMaxCm)
		? clamp(UnderBreastClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float LowerBodyMorphActivity = ReadEF_LowerBodyMorphActivity();
	float LowerBodyMorphStrength = isfinite(LowerBodyMorphActivity)
		? saturate(max(LowerBodyMorphActivity, 0.0f))
		: 0.0f;
	float LowerBodyMorphClearanceMaxCm =
		ReadEF_LowerBodyMorphClearanceMaxCm();
	LowerBodyMorphClearanceMaxCm = isfinite(LowerBodyMorphClearanceMaxCm)
		? clamp(LowerBodyMorphClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float UnderBreastWitnessReserveBudgetCm =
		BreastMorphStrength * UnderBreastClearanceMaxCm;
	float LowerBodyMorphWitnessReserveBudgetCm =
		LowerBodyMorphStrength * LowerBodyMorphClearanceMaxCm;
	float AutomaticWitnessReserveBudgetCm =
		UnderBreastWitnessReserveBudgetCm
		+ LowerBodyMorphWitnessReserveBudgetCm;
	// The base pass is already vertex-safe against an explicitly oriented body
	// triangle. Never use the imported garment tangent normal as an emergency
	// direction here: DAZ garments can expose an inward tangent basis at seams,
	// which would turn a fail-safe into a 2.5 cm inward displacement. Until a GPU
	// failure flag hides the component, preserving the certified base result is
	// the only non-regressive fallback for malformed/conflicting witness cones.
	bool HasFinitePosition = all(isfinite(BasePosition));
	float3 ConservativePosition = HasFinitePosition
		? BasePosition
		: float3(0.0f, 0.0f, 0.0f);
	bool HasUsableTangentX = all(isfinite(BaseTangentX.xyz))
		&& dot(BaseTangentX.xyz, BaseTangentX.xyz) > 1.0e-12f;
	bool HasUsableTangentZ = all(isfinite(BaseTangentZ.xyz))
		&& dot(BaseTangentZ.xyz, BaseTangentZ.xyz) > 1.0e-12f;
	float3 SafeTangentDirection = HasUsableTangentX
		? normalize(BaseTangentX.xyz)
		: float3(1.0f, 0.0f, 0.0f);
	float3 SafeNormalDirection = HasUsableTangentZ
		? normalize(BaseTangentZ.xyz)
		: float3(0.0f, 0.0f, 1.0f);
	if (!HasUsableTangentZ)
	{
		float3 ReferenceAxis = abs(SafeTangentDirection.z) < 0.999f
			? float3(0.0f, 0.0f, 1.0f)
			: float3(0.0f, 1.0f, 0.0f);
		SafeNormalDirection = normalize(cross(SafeTangentDirection, ReferenceAxis));
	}
	if (!HasUsableTangentX)
	{
		float3 ReferenceAxis = abs(SafeNormalDirection.z) < 0.999f
			? float3(0.0f, 0.0f, 1.0f)
			: float3(0.0f, 1.0f, 0.0f);
		SafeTangentDirection = normalize(cross(ReferenceAxis, SafeNormalDirection));
	}
	BaseTangentX = float4(
		HasUsableTangentX ? BaseTangentX.xyz : SafeTangentDirection,
		isfinite(BaseTangentX.w) ? BaseTangentX.w : 1.0f);
	BaseTangentZ = float4(
		HasUsableTangentZ ? BaseTangentZ.xyz : SafeNormalDirection,
		isfinite(BaseTangentZ.w) ? BaseTangentZ.w : 1.0f);

	if (!HasFinitePosition)
	{
		WriteFinalPosition(Index, ConservativePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}

	int BindingVertexCount = ReadEF_BindingVertexCount();
	int BodyVertexCountValue = ReadEF_BodyVertexCount();
	int WitnessCountValue = ReadEF_WitnessCount();
	int WitnessReferenceCountValue = ReadEF_WitnessReferenceCount();
	if (BindingVertexCount <= 0
		|| Index >= (uint)BindingVertexCount
		|| BodyVertexCountValue <= 0
		|| WitnessCountValue < 0
		|| WitnessReferenceCountValue < 0
		|| ReadEF_GarmentLODIndex() < 0
		|| ReadEF_BodyLODIndex() < 0)
	{
		WriteFinalPosition(Index, ConservativePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}

)EFHLSL")) + TEXT(R"EFHLSL(

	StructuredBuffer<int2> WitnessRangesBuffer = ReadEF_WitnessRanges();
	StructuredBuffer<int> WitnessIndicesBuffer = ReadEF_WitnessIndices();
	StructuredBuffer<int4> PrimaryBodyTriangleAndModeBuffer = ReadEF_BodyTriangleAndMode();
	StructuredBuffer<float4> PrimaryBarycentricsAndFollowWeightBuffer =
		ReadEF_BarycentricsAndFollowWeight();
	StructuredBuffer<float4> RestOffsetAndClearanceCmBuffer =
		ReadEF_RestOffsetAndClearanceCm();
	StructuredBuffer<int2> ThicknessReferenceAndLayerBuffer =
		ReadEF_ThicknessReferenceAndLayer();
	StructuredBuffer<float> LocalFeatureSizeCmBuffer = ReadEF_LocalFeatureSizeCm();
	StructuredBuffer<float> UnderBreastGuardWeightsBuffer =
		ReadEF_UnderBreastGuardWeights();
	StructuredBuffer<float> LowerBodyMorphGuardWeightsBuffer =
		ReadEF_LowerBodyMorphGuardWeights();
	StructuredBuffer<int4> WitnessGarmentVerticesBuffer = ReadEF_WitnessGarmentVertices();
	StructuredBuffer<float4> WitnessGarmentBarycentricsAndClearanceCmBuffer =
		ReadEF_WitnessGarmentBarycentricsAndClearanceCm();
	StructuredBuffer<int4> WitnessBodyVerticesBuffer = ReadEF_WitnessBodyVertices();
	StructuredBuffer<float4> WitnessBodyBarycentricsAndMaximumCorrectionCmBuffer =
		ReadEF_WitnessBodyBarycentricsAndMaximumCorrectionCm();

	uint WitnessRangeBufferCount = 0;
	uint WitnessIndexBufferCount = 0;
	uint PrimaryBodyTriangleBufferCount = 0;
	uint PrimaryBarycentricBufferCount = 0;
	uint RestOffsetBufferCount = 0;
	uint ThicknessBufferCount = 0;
	uint LocalFeatureBufferCount = 0;
	uint UnderBreastGuardBufferCount = 0;
	uint LowerBodyMorphGuardBufferCount = 0;
	uint WitnessGarmentVertexBufferCount = 0;
	uint WitnessGarmentBarycentricBufferCount = 0;
	uint WitnessBodyVertexBufferCount = 0;
	uint WitnessBodyBarycentricBufferCount = 0;
	uint IgnoredStride = 0;
	WitnessRangesBuffer.GetDimensions(WitnessRangeBufferCount, IgnoredStride);
	WitnessIndicesBuffer.GetDimensions(WitnessIndexBufferCount, IgnoredStride);
	PrimaryBodyTriangleAndModeBuffer.GetDimensions(PrimaryBodyTriangleBufferCount, IgnoredStride);
	PrimaryBarycentricsAndFollowWeightBuffer.GetDimensions(
		PrimaryBarycentricBufferCount,
		IgnoredStride);
	RestOffsetAndClearanceCmBuffer.GetDimensions(RestOffsetBufferCount, IgnoredStride);
	ThicknessReferenceAndLayerBuffer.GetDimensions(ThicknessBufferCount, IgnoredStride);
	LocalFeatureSizeCmBuffer.GetDimensions(LocalFeatureBufferCount, IgnoredStride);
	UnderBreastGuardWeightsBuffer.GetDimensions(
		UnderBreastGuardBufferCount,
		IgnoredStride);
	LowerBodyMorphGuardWeightsBuffer.GetDimensions(
		LowerBodyMorphGuardBufferCount,
		IgnoredStride);
	WitnessGarmentVerticesBuffer.GetDimensions(WitnessGarmentVertexBufferCount, IgnoredStride);
	WitnessGarmentBarycentricsAndClearanceCmBuffer.GetDimensions(
		WitnessGarmentBarycentricBufferCount,
		IgnoredStride);
	WitnessBodyVerticesBuffer.GetDimensions(WitnessBodyVertexBufferCount, IgnoredStride);
	WitnessBodyBarycentricsAndMaximumCorrectionCmBuffer.GetDimensions(
		WitnessBodyBarycentricBufferCount,
		IgnoredStride);
	// Optimus variable arrays may expose padded GPU allocation capacity. The scalar
	// values retain the logical uploaded lengths; buffer dimensions therefore need
	// to cover those lengths, not equal them byte-for-byte. Every subsequent range
	// is checked against both the logical count and the real allocation.
	const bool bInsufficientBufferCapacity = Index >= WitnessRangeBufferCount
		|| WitnessIndexBufferCount < (uint)WitnessReferenceCountValue
		|| PrimaryBodyTriangleBufferCount < (uint)BindingVertexCount
		|| PrimaryBarycentricBufferCount < (uint)BindingVertexCount
		|| RestOffsetBufferCount < (uint)BindingVertexCount
		|| ThicknessBufferCount < (uint)BindingVertexCount
		|| LocalFeatureBufferCount < (uint)BindingVertexCount
		|| UnderBreastGuardBufferCount < (uint)BindingVertexCount
		|| LowerBodyMorphGuardBufferCount < (uint)BindingVertexCount
		|| WitnessGarmentVertexBufferCount < (uint)WitnessCountValue
		|| WitnessGarmentBarycentricBufferCount < (uint)WitnessCountValue
		|| WitnessBodyVertexBufferCount < (uint)WitnessCountValue
		|| WitnessBodyBarycentricBufferCount < (uint)WitnessCountValue;
	if (bInsufficientBufferCapacity)
	{
		WriteFinalPosition(Index, ConservativePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}

	int2 WitnessRange = WitnessRangesBuffer[Index];
	if (WitnessRange.x < 0
		|| WitnessRange.y < 0
		|| WitnessRange.y > 256
		|| WitnessRange.x > WitnessReferenceCountValue
		|| WitnessRange.y > WitnessReferenceCountValue - WitnessRange.x)
	{
		WriteFinalPosition(Index, ConservativePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}
	if (WitnessRange.y == 0)
	{
		WriteFinalPosition(Index, BasePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}
)EFHLSL") + TEXT(R"EFHLSL(

	uint BodyVertexCount = (uint)BodyVertexCountValue;
	float4x4 BodyToGarment = ReadEF_BodyToGarmentTransform();
	int4 PrimaryBodyTriangleAndMode = PrimaryBodyTriangleAndModeBuffer[Index];
	if (PrimaryBodyTriangleAndMode.w == 3)
	{
		WriteFinalPosition(Index, BasePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}
	bool InvalidWitnessData = false;
	if (any(PrimaryBodyTriangleAndMode.xyz < 0)
		|| any((uint3)PrimaryBodyTriangleAndMode.xyz >= BodyVertexCount))
	{
		InvalidWitnessData = true;
	}
	float3 PrimaryBarycentrics =
		PrimaryBarycentricsAndFollowWeightBuffer[Index].xyz;
	float PrimaryBarycentricSum = dot(
		PrimaryBarycentrics,
		float3(1.0f, 1.0f, 1.0f));
	if (!all(isfinite(PrimaryBarycentrics))
		|| abs(PrimaryBarycentricSum) <= 1.0e-8f)
	{
		InvalidWitnessData = true;
	}
	if (InvalidWitnessData)
	{
		WriteFinalPosition(Index, ConservativePosition);
		WriteFinalTangentX(Index, BaseTangentX);
		WriteFinalTangentZ(Index, BaseTangentZ);
		return;
	}
	PrimaryBarycentrics /= PrimaryBarycentricSum;
	float3 PrimaryBodyP0 = Body::ReadBodyPosition((uint)PrimaryBodyTriangleAndMode.x);
	float3 PrimaryBodyP1 = Body::ReadBodyPosition((uint)PrimaryBodyTriangleAndMode.y);
	float3 PrimaryBodyP2 = Body::ReadBodyPosition((uint)PrimaryBodyTriangleAndMode.z);
	PrimaryBodyP0 = mul(float4(PrimaryBodyP0, 1.0f), BodyToGarment).xyz;
	PrimaryBodyP1 = mul(float4(PrimaryBodyP1, 1.0f), BodyToGarment).xyz;
	PrimaryBodyP2 = mul(float4(PrimaryBodyP2, 1.0f), BodyToGarment).xyz;
	float3 UnnormalizedPrimaryNormal = cross(
		PrimaryBodyP1 - PrimaryBodyP0,
		PrimaryBodyP2 - PrimaryBodyP0);
	float PrimaryGeometricNormalLengthSquared = dot(
		UnnormalizedPrimaryNormal,
		UnnormalizedPrimaryNormal);
	if (!all(isfinite(PrimaryBodyP0))
		|| !all(isfinite(PrimaryBodyP1))
		|| !all(isfinite(PrimaryBodyP2))
		|| PrimaryGeometricNormalLengthSquared <= 1.0e-12f)
	{
		InvalidWitnessData = true;
	}
	float3 PrimaryGeometricNormal = UnnormalizedPrimaryNormal
		* rsqrt(max(PrimaryGeometricNormalLengthSquared, 1.0e-12f));
	float3 PrimaryNormal = PrimaryGeometricNormal;
	float3 ExtraCorrection = float3(0.0f, 0.0f, 0.0f);
	float LocalFeatureSizeCm = LocalFeatureSizeCmBuffer[Index];
	if (!isfinite(LocalFeatureSizeCm) || LocalFeatureSizeCm <= 1.0e-5f)
	{
		InvalidWitnessData = true;
	}
	float MaximumWitnessCorrectionCm = min(
		0.70f,
		max(LocalFeatureSizeCm * 0.25f, 0.0f)
			+ AutomaticWitnessReserveBudgetCm);

	// Solve every incident face/edge constraint as a small convex half-space
	// problem local to this render vertex. Requiring every participating corner
	// to contribute the complete witness deficit is deliberately conservative:
	// barycentric weights sum to one, therefore the reconstructed face sample is
	// guaranteed to receive at least that same deficit. Six cyclic projections
	// make later constraints repair any earlier one that they partially close.
	// Each projection direction is clipped to the primary vertex-safe half-space,
	// so this pass can never undo the base pass' skin clearance even when a cloth
	// triangle spans a concave DAZ region with opposing nearest-surface normals.
	[unroll]
	for (int ProjectionIteration = 0;
		ProjectionIteration < 6 && !InvalidWitnessData;
		++ProjectionIteration)
	{
		[loop]
		for (int LocalReferenceIndex = 0;
			LocalReferenceIndex < WitnessRange.y && !InvalidWitnessData;
			++LocalReferenceIndex)
		{
			int WitnessIndex = WitnessIndicesBuffer[WitnessRange.x + LocalReferenceIndex];
			if (WitnessIndex < 0 || WitnessIndex >= WitnessCountValue)
			{
				InvalidWitnessData = true;
				break;
			}
			int4 GarmentVertices = WitnessGarmentVerticesBuffer[WitnessIndex];
			float4 GarmentBarycentricsAndClearanceCm =
				WitnessGarmentBarycentricsAndClearanceCmBuffer[WitnessIndex];
			int4 BodyVertices = WitnessBodyVerticesBuffer[WitnessIndex];
			float4 BodyBarycentricsAndMaximumCorrectionCm =
				WitnessBodyBarycentricsAndMaximumCorrectionCmBuffer[WitnessIndex];
			if (any(GarmentVertices.xyz < 0)
				|| any((uint3)GarmentVertices.xyz >= (uint)BindingVertexCount)
				|| any(BodyVertices.xyz < 0)
				|| any((uint3)BodyVertices.xyz >= BodyVertexCount)
				|| !all(isfinite(GarmentBarycentricsAndClearanceCm))
				|| !all(isfinite(BodyBarycentricsAndMaximumCorrectionCm)))
			{
				InvalidWitnessData = true;
				break;
			}
			// The authored witness budget and local topology feature size both bound
			// this vertex. A small triangle can no longer receive the old fixed
			// 0.35cm differential push that visually tore dense DAZ garments.
			MaximumWitnessCorrectionCm = min(
				MaximumWitnessCorrectionCm,
				max(BodyBarycentricsAndMaximumCorrectionCm.w, 0.0f)
					+ AutomaticWitnessReserveBudgetCm);
			float GarmentBarycentricSum = dot(
				GarmentBarycentricsAndClearanceCm.xyz,
				float3(1.0f, 1.0f, 1.0f));
			float BodyBarycentricSum = dot(
				BodyBarycentricsAndMaximumCorrectionCm.xyz,
				float3(1.0f, 1.0f, 1.0f));
			if (abs(GarmentBarycentricSum) <= 1.0e-8f
				|| abs(BodyBarycentricSum) <= 1.0e-8f)
			{
				InvalidWitnessData = true;
				break;
			}
			float3 GarmentBarycentrics =
				GarmentBarycentricsAndClearanceCm.xyz / GarmentBarycentricSum;
			float WitnessUnderBreastGuardWeight =
				UnderBreastGuardWeightsBuffer[GarmentVertices.x] * GarmentBarycentrics.x
				+ UnderBreastGuardWeightsBuffer[GarmentVertices.y] * GarmentBarycentrics.y
				+ UnderBreastGuardWeightsBuffer[GarmentVertices.z] * GarmentBarycentrics.z;
			float WitnessLowerBodyMorphGuardWeight =
				LowerBodyMorphGuardWeightsBuffer[GarmentVertices.x] * GarmentBarycentrics.x
				+ LowerBodyMorphGuardWeightsBuffer[GarmentVertices.y] * GarmentBarycentrics.y
				+ LowerBodyMorphGuardWeightsBuffer[GarmentVertices.z] * GarmentBarycentrics.z;
			if (!isfinite(WitnessUnderBreastGuardWeight)
				|| WitnessUnderBreastGuardWeight < 0.0f
				|| WitnessUnderBreastGuardWeight > 1.0f
				|| !isfinite(WitnessLowerBodyMorphGuardWeight)
				|| WitnessLowerBodyMorphGuardWeight < 0.0f
				|| WitnessLowerBodyMorphGuardWeight > 1.0f)
			{
				InvalidWitnessData = true;
				break;
			}
			float IncidenceWeight = GarmentVertices.x == (int)Index
				? GarmentBarycentrics.x
				: (GarmentVertices.y == (int)Index
					? GarmentBarycentrics.y
					: (GarmentVertices.z == (int)Index ? GarmentBarycentrics.z : 0.0f));
			if (IncidenceWeight <= 1.0e-6f)
			{
				InvalidWitnessData = true;
				break;
			}
			float3 BodyBarycentrics =
				BodyBarycentricsAndMaximumCorrectionCm.xyz / BodyBarycentricSum;
			float3 SamplePosition =
				ReadBaseCorrectedPosition((uint)GarmentVertices.x) * GarmentBarycentrics.x
				+ ReadBaseCorrectedPosition((uint)GarmentVertices.y) * GarmentBarycentrics.y
				+ ReadBaseCorrectedPosition((uint)GarmentVertices.z) * GarmentBarycentrics.z;

)EFHLSL") + TEXT(R"EFHLSL(
			float3 BodyP0 = Body::ReadBodyPosition((uint)BodyVertices.x);
			float3 BodyP1 = Body::ReadBodyPosition((uint)BodyVertices.y);
			float3 BodyP2 = Body::ReadBodyPosition((uint)BodyVertices.z);
			BodyP0 = mul(float4(BodyP0, 1.0f), BodyToGarment).xyz;
			BodyP1 = mul(float4(BodyP1, 1.0f), BodyToGarment).xyz;
			BodyP2 = mul(float4(BodyP2, 1.0f), BodyToGarment).xyz;
			float3 UnnormalizedWitnessNormal = cross(BodyP1 - BodyP0, BodyP2 - BodyP0);
			float WitnessGeometricNormalLengthSquared = dot(
				UnnormalizedWitnessNormal,
				UnnormalizedWitnessNormal);
			if (!all(isfinite(SamplePosition))
				|| !all(isfinite(BodyP0))
				|| !all(isfinite(BodyP1))
				|| !all(isfinite(BodyP2))
				|| WitnessGeometricNormalLengthSquared <= 1.0e-12f)
			{
				InvalidWitnessData = true;
				break;
			}
			float3 WitnessNormal = UnnormalizedWitnessNormal
				* rsqrt(WitnessGeometricNormalLengthSquared);
			float3 BodyAnchor = BodyP0 * BodyBarycentrics.x
				+ BodyP1 * BodyBarycentrics.y
				+ BodyP2 * BodyBarycentrics.z;
			float DynamicVertexClearanceCm[3];
			int GarmentVertexArray[3] =
			{
				GarmentVertices.x,
				GarmentVertices.y,
				GarmentVertices.z
			};
			[unroll]
			for (int CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				int GarmentVertexIndex = GarmentVertexArray[CornerIndex];
				float4 VertexRest = RestOffsetAndClearanceCmBuffer[GarmentVertexIndex];
				int2 ThicknessReferenceAndLayer =
					ThicknessReferenceAndLayerBuffer[GarmentVertexIndex];
				float EffectiveClearanceCm = VertexRest.w;
				if (ThicknessReferenceAndLayer.y == 1)
				{
					int InnerReference = ThicknessReferenceAndLayer.x;
					if (InnerReference < 0 || InnerReference >= BindingVertexCount)
					{
						InvalidWitnessData = true;
						break;
					}
					float4 InnerRest = RestOffsetAndClearanceCmBuffer[InnerReference];
					float3 LayerVectorCm = VertexRest.xyz - InnerRest.xyz;
					float LayerLengthCm = length(LayerVectorCm);
					if (!all(isfinite(VertexRest))
						|| !all(isfinite(InnerRest))
						|| LayerLengthCm <= 1.0e-6f)
					{
						InvalidWitnessData = true;
						break;
					}
					float3 EffectiveRest = InnerRest.xyz
						+ LayerVectorCm * (RuntimeInflateCm / LayerLengthCm);
					EffectiveClearanceCm = InnerRest.w
						+ max(EffectiveRest.z - InnerRest.z, 0.0f);
				}
				else if (ThicknessReferenceAndLayer.y == 0
					&& ThicknessReferenceAndLayer.x < 0)
				{
					// V3 single-layer source mesh: use the same certified body-normal
					// inflate as the vertex pass so face/edge witnesses remain coherent.
					EffectiveClearanceCm += RuntimeInflateCm;
				}
				else if (ThicknessReferenceAndLayer.y != 0)
				{
					InvalidWitnessData = true;
					break;
				}
				DynamicVertexClearanceCm[CornerIndex] = EffectiveClearanceCm;
			}
			if (InvalidWitnessData)
			{
				break;
			}
			float DynamicWitnessClearanceCm =
				DynamicVertexClearanceCm[0] * GarmentBarycentrics.x
				+ DynamicVertexClearanceCm[1] * GarmentBarycentrics.y
				+ DynamicVertexClearanceCm[2] * GarmentBarycentrics.z;
			float WitnessUnderBreastReserveCm =
				UnderBreastWitnessReserveBudgetCm * WitnessUnderBreastGuardWeight;
			float WitnessLowerBodyMorphReserveCm =
				LowerBodyMorphWitnessReserveBudgetCm
				* WitnessLowerBodyMorphGuardWeight;
			float TargetClearanceCm =
				max(
					DynamicWitnessClearanceCm + RuntimeOffsetCm
						+ WitnessUnderBreastReserveCm
						+ WitnessLowerBodyMorphReserveCm,
					0.0f);
			float RequiredPushCm = max(
				TargetClearanceCm - dot(SamplePosition - BodyAnchor, WitnessNormal),
				0.0f);
			float RemainingPushCm = RequiredPushCm - dot(ExtraCorrection, WitnessNormal);
			if (RemainingPushCm <= 1.0e-5f)
			{
				continue;
			}

			float PrimaryAlignment = dot(WitnessNormal, PrimaryNormal);
			float3 SafeDirection = WitnessNormal
				- PrimaryNormal * min(PrimaryAlignment, 0.0f);
			float SafeOpeningRate = dot(SafeDirection, WitnessNormal);
			if (!all(isfinite(SafeDirection)) || SafeOpeningRate <= 1.0e-6f)
			{
				// Exactly opposing primary and witness half-spaces have no finite
				// unilateral solution for this vertex. Leave the strongest safe
				// result intact; final readback/saturation keeps the garment closed.
				continue;
			}
			ExtraCorrection += SafeDirection * (RemainingPushCm / SafeOpeningRate);
		}
	}
	if (!all(isfinite(ExtraCorrection))
		|| dot(ExtraCorrection, PrimaryNormal) < -1.0e-5f)
	{
		InvalidWitnessData = true;
	}
)EFHLSL") + TEXT(R"EFHLSL(

	// Keep the edge/face correction subordinate to the vertex-safe base pass. This
	// visual stability limit is topology-agnostic; excluded Director sections have
	// already taken the PreserveUpstream path before witness evaluation.
	float ExtraCorrectionLength = length(ExtraCorrection);
	float BoundedCorrectionScale = ExtraCorrectionLength > 1.0e-8f
		? min(1.0f, MaximumWitnessCorrectionCm / ExtraCorrectionLength)
		: 0.0f;
	float3 BoundedExtraCorrection = InvalidWitnessData
		? float3(0.0f, 0.0f, 0.0f)
		: ExtraCorrection * BoundedCorrectionScale;
	float3 FinalPosition = BasePosition + BoundedExtraCorrection;
	if (!all(isfinite(FinalPosition)))
	{
		FinalPosition = ConservativePosition;
	}
	WriteFinalPosition(Index, FinalPosition);
	WriteFinalTangentX(Index, BaseTangentX);
	WriteFinalTangentZ(Index, BaseTangentZ);
}
)EFHLSL");
		return Source;
	}

	const FString& GetFinalizeKernelSource()
	{
		static const FString Source = FString(TEXT(R"EFHLSL(
KERNEL
{
	float3 WitnessPosition = ReadWitnessPosition(Index);
	float3 UpstreamPosition = ReadUpstreamGarmentPosition(Index);
	float4 WitnessTangentX = ReadWitnessTangentX(Index);
	float4 WitnessTangentZ = ReadWitnessTangentZ(Index);
	float BodyMorphActivity = ReadEF_BodyMorphActivity();
	float LowerBodyMorphActivity = ReadEF_LowerBodyMorphActivity();
	float LowerBodyMorphStrength = isfinite(LowerBodyMorphActivity)
		? saturate(max(LowerBodyMorphActivity, 0.0f))
		: 0.0f;
	bool MorphActive = (isfinite(BodyMorphActivity)
		&& abs(BodyMorphActivity) > 1.0e-4f)
		|| LowerBodyMorphStrength > 1.0e-4f;
	float BreastMorphActivity = ReadEF_BreastMorphActivity();
	float BreastMorphStrength = isfinite(BreastMorphActivity)
		? saturate(abs(BreastMorphActivity))
		: 0.0f;

	// Seam welding and tangent rebuilding are body-morph stability operations.
	// Outside that narrow gate this pass is bit-for-bit transparent to the
	// already certified witness result.
	if (!MorphActive)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	bool FiniteInputs = all(isfinite(WitnessPosition))
		&& all(isfinite(UpstreamPosition))
		&& all(isfinite(WitnessTangentX))
		&& all(isfinite(WitnessTangentZ));
	int BindingVertexCount = ReadEF_BindingVertexCount();
	int BodyVertexCountValue = ReadEF_BodyVertexCount();
	int WeldReferenceCountValue = ReadEF_WeldReferenceCount();
	if (!FiniteInputs
		|| BindingVertexCount <= 0
		|| Index >= (uint)BindingVertexCount
		|| BodyVertexCountValue <= 0
		|| WeldReferenceCountValue < 0
		|| ReadEF_GarmentLODIndex() < 0
		|| ReadEF_BodyLODIndex() < 0)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	StructuredBuffer<int2> WeldRangesBuffer = ReadEF_WeldRanges();
	StructuredBuffer<int> WeldIndicesBuffer = ReadEF_WeldIndices();
	StructuredBuffer<int4> BodyTriangleAndModeBuffer = ReadEF_BodyTriangleAndMode();
	StructuredBuffer<float4> BarycentricsAndFollowWeightBuffer =
		ReadEF_BarycentricsAndFollowWeight();
	StructuredBuffer<float4> RestOffsetAndClearanceCmBuffer =
		ReadEF_RestOffsetAndClearanceCm();
	StructuredBuffer<float2> MaximumCorrectionAndRestGapCmBuffer =
		ReadEF_MaximumCorrectionAndRestGapCm();
	StructuredBuffer<int2> ThicknessReferenceAndLayerBuffer =
		ReadEF_ThicknessReferenceAndLayer();
	StructuredBuffer<float> UnderBreastGuardWeightsBuffer =
		ReadEF_UnderBreastGuardWeights();
	StructuredBuffer<float> LowerBodyMorphGuardWeightsBuffer =
		ReadEF_LowerBodyMorphGuardWeights();

	uint WeldRangeBufferCount = 0;
	uint WeldIndexBufferCount = 0;
	uint TriangleBufferCount = 0;
	uint BarycentricBufferCount = 0;
	uint RestOffsetBufferCount = 0;
	uint LimitBufferCount = 0;
	uint ThicknessBufferCount = 0;
	uint UnderBreastGuardBufferCount = 0;
	uint LowerBodyMorphGuardBufferCount = 0;
	uint IgnoredStride = 0;
	WeldRangesBuffer.GetDimensions(WeldRangeBufferCount, IgnoredStride);
	WeldIndicesBuffer.GetDimensions(WeldIndexBufferCount, IgnoredStride);
	BodyTriangleAndModeBuffer.GetDimensions(TriangleBufferCount, IgnoredStride);
	BarycentricsAndFollowWeightBuffer.GetDimensions(BarycentricBufferCount, IgnoredStride);
	RestOffsetAndClearanceCmBuffer.GetDimensions(RestOffsetBufferCount, IgnoredStride);
	MaximumCorrectionAndRestGapCmBuffer.GetDimensions(LimitBufferCount, IgnoredStride);
	ThicknessReferenceAndLayerBuffer.GetDimensions(ThicknessBufferCount, IgnoredStride);
	UnderBreastGuardWeightsBuffer.GetDimensions(UnderBreastGuardBufferCount, IgnoredStride);
	LowerBodyMorphGuardWeightsBuffer.GetDimensions(
		LowerBodyMorphGuardBufferCount,
		IgnoredStride);
	if (WeldRangeBufferCount < (uint)BindingVertexCount
		|| WeldIndexBufferCount < (uint)WeldReferenceCountValue
		|| TriangleBufferCount < (uint)BindingVertexCount
		|| BarycentricBufferCount < (uint)BindingVertexCount
		|| RestOffsetBufferCount < (uint)BindingVertexCount
		|| LimitBufferCount < (uint)BindingVertexCount
		|| ThicknessBufferCount < (uint)BindingVertexCount
		|| UnderBreastGuardBufferCount < (uint)BindingVertexCount
		|| LowerBodyMorphGuardBufferCount < (uint)BindingVertexCount)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	int4 CurrentTriangleAndMode = BodyTriangleAndModeBuffer[Index];
	float CurrentFollowWeight = saturate(
		BarycentricsAndFollowWeightBuffer[Index].w);
	if (CurrentTriangleAndMode.w == 3)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	int2 WeldRange = WeldRangesBuffer[Index];
	if (WeldRange.x < 0
		|| WeldRange.y < 0
		|| WeldRange.y > 256
		|| (WeldRange.y == 0 && WeldRange.x != 0)
		|| WeldRange.x > WeldReferenceCountValue
		|| WeldRange.y > WeldReferenceCountValue - WeldRange.x)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	// Singleton render vertices still need the same body-aligned tangent rebuild as
	// split vertices during large morphs.  A zero range therefore means a valid
	// one-member logical group whose canonical vertex and position are this vertex.
	float3 WeldPositionSum = WitnessPosition;
	int CanonicalVertexIndex = (int)Index;
	int PreviousVertexIndex = -1;
	bool ContainsCurrentVertex = WeldRange.y == 0;
	bool InvalidWeldGroup = false;
	[loop]
	for (int LocalWeldIndex = 0; LocalWeldIndex < WeldRange.y; ++LocalWeldIndex)
	{
		int WeldVertexIndex = WeldIndicesBuffer[WeldRange.x + LocalWeldIndex];
		// The compiler sorts every group. Enforcing that invariant makes the first
		// member a deterministic canonical surface for every duplicated render split.
		if (WeldVertexIndex < 0
			|| WeldVertexIndex >= BindingVertexCount
			|| WeldVertexIndex <= PreviousVertexIndex)
		{
			InvalidWeldGroup = true;
			break;
		}
		float3 WeldPosition = ReadWitnessPosition((uint)WeldVertexIndex);
		if (!all(isfinite(WeldPosition)))
		{
			InvalidWeldGroup = true;
			break;
		}
		CanonicalVertexIndex = LocalWeldIndex == 0
			? WeldVertexIndex
			: CanonicalVertexIndex;
		ContainsCurrentVertex = ContainsCurrentVertex
			|| WeldVertexIndex == (int)Index;
		PreviousVertexIndex = WeldVertexIndex;
		WeldPositionSum = LocalWeldIndex == 0
			? WeldPosition
			: WeldPositionSum + WeldPosition;
	}
	if (InvalidWeldGroup || !ContainsCurrentVertex || CanonicalVertexIndex < 0)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}
	float3 SharedPosition = WeldRange.y == 0
		? WitnessPosition
		: WeldPositionSum / (float)WeldRange.y;
	// Low-confidence vertices do not inherit the body's tangent frame, but exact
	// render splits must still close to one position.  Returning the shared result
	// here prevents a witness pass from reopening UV/material seams.
	if (CurrentFollowWeight <= 1.0e-4f)
	{
		WriteFinalPosition(Index, SharedPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}
)EFHLSL")) + FString(TEXT(R"EFHLSL(

	int4 CanonicalTriangleAndMode =
		BodyTriangleAndModeBuffer[CanonicalVertexIndex];
	float4 CanonicalBarycentricsAndFollowWeight =
		BarycentricsAndFollowWeightBuffer[CanonicalVertexIndex];
	float4 CanonicalRestOffsetAndClearanceCm =
		RestOffsetAndClearanceCmBuffer[CanonicalVertexIndex];
	float2 CanonicalMaximumCorrectionAndRestGapCm =
		MaximumCorrectionAndRestGapCmBuffer[CanonicalVertexIndex];
	int2 CanonicalThicknessReferenceAndLayer =
		ThicknessReferenceAndLayerBuffer[CanonicalVertexIndex];
	float CanonicalUnderBreastGuardWeight =
		UnderBreastGuardWeightsBuffer[CanonicalVertexIndex];
	float CanonicalLowerBodyMorphGuardWeight =
		LowerBodyMorphGuardWeightsBuffer[CanonicalVertexIndex];
	float CanonicalFollowWeight = saturate(
		CanonicalBarycentricsAndFollowWeight.w);
	uint BodyVertexCount = (uint)BodyVertexCountValue;
	float CanonicalBarycentricSum = dot(
		CanonicalBarycentricsAndFollowWeight.xyz,
		float3(1.0f, 1.0f, 1.0f));
	bool InvalidCanonicalSurface = CanonicalTriangleAndMode.w == 3
		|| CanonicalFollowWeight <= 1.0e-4f
		|| any(CanonicalTriangleAndMode.xyz < 0)
		|| any((uint3)CanonicalTriangleAndMode.xyz >= BodyVertexCount)
		|| !all(isfinite(CanonicalBarycentricsAndFollowWeight))
		|| !all(isfinite(CanonicalRestOffsetAndClearanceCm))
		|| !all(isfinite(CanonicalMaximumCorrectionAndRestGapCm))
		|| !isfinite(CanonicalUnderBreastGuardWeight)
		|| CanonicalUnderBreastGuardWeight < 0.0f
		|| CanonicalUnderBreastGuardWeight > 1.0f
		|| !isfinite(CanonicalLowerBodyMorphGuardWeight)
		|| CanonicalLowerBodyMorphGuardWeight < 0.0f
		|| CanonicalLowerBodyMorphGuardWeight > 1.0f
		|| abs(CanonicalBarycentricSum) <= 1.0e-8f
		|| CanonicalThicknessReferenceAndLayer.y < 0
		|| CanonicalThicknessReferenceAndLayer.y > 1;
	if (InvalidCanonicalSurface)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	float3 CanonicalBarycentrics =
		CanonicalBarycentricsAndFollowWeight.xyz / CanonicalBarycentricSum;
	float4x4 BodyToGarment = ReadEF_BodyToGarmentTransform();
	float3 BodyP0 = Body::ReadBodyPosition((uint)CanonicalTriangleAndMode.x);
	float3 BodyP1 = Body::ReadBodyPosition((uint)CanonicalTriangleAndMode.y);
	float3 BodyP2 = Body::ReadBodyPosition((uint)CanonicalTriangleAndMode.z);
	BodyP0 = mul(float4(BodyP0, 1.0f), BodyToGarment).xyz;
	BodyP1 = mul(float4(BodyP1, 1.0f), BodyToGarment).xyz;
	BodyP2 = mul(float4(BodyP2, 1.0f), BodyToGarment).xyz;
	float3 Edge01 = BodyP1 - BodyP0;
	float3 Edge02 = BodyP2 - BodyP0;
	float3 UnnormalizedSurfaceNormal = cross(Edge01, Edge02);
	float SurfaceNormalLengthSquared = dot(
		UnnormalizedSurfaceNormal,
		UnnormalizedSurfaceNormal);
	float3 SurfaceTangent = Edge01;
	if (SurfaceNormalLengthSquared > 1.0e-12f)
	{
		float3 PreliminaryNormal = UnnormalizedSurfaceNormal
			* rsqrt(SurfaceNormalLengthSquared);
		SurfaceTangent -= PreliminaryNormal * dot(SurfaceTangent, PreliminaryNormal);
		if (dot(SurfaceTangent, SurfaceTangent) <= 1.0e-12f)
		{
			SurfaceTangent = Edge02
				- PreliminaryNormal * dot(Edge02, PreliminaryNormal);
		}
	}
	float SurfaceTangentLengthSquared = dot(SurfaceTangent, SurfaceTangent);
	if (!all(isfinite(BodyP0))
		|| !all(isfinite(BodyP1))
		|| !all(isfinite(BodyP2))
		|| SurfaceNormalLengthSquared <= 1.0e-12f
		|| SurfaceTangentLengthSquared <= 1.0e-12f)
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	float3 SurfaceNormal = UnnormalizedSurfaceNormal
		* rsqrt(SurfaceNormalLengthSquared);
	SurfaceTangent *= rsqrt(SurfaceTangentLengthSquared);
	float3 SurfaceBitangent = normalize(cross(SurfaceNormal, SurfaceTangent));
	SurfaceTangent = normalize(cross(SurfaceBitangent, SurfaceNormal));
	float3 SurfaceAnchor = BodyP0 * CanonicalBarycentrics.x
		+ BodyP1 * CanonicalBarycentrics.y
		+ BodyP2 * CanonicalBarycentrics.z;

	float RuntimeInflateCm = ReadEF_GarmentInflateCm();
	RuntimeInflateCm = isfinite(RuntimeInflateCm)
		? clamp(RuntimeInflateCm, 0.0f, 2.0f)
		: 0.0f;
	float RuntimeOffsetCm = ReadEF_GlobalClearanceOffsetCm()
		+ ReadEF_GarmentClearanceOffsetCm();
	RuntimeOffsetCm = isfinite(RuntimeOffsetCm) ? RuntimeOffsetCm : 0.0f;
	float3 RuntimeRestOffsetCm = CanonicalRestOffsetAndClearanceCm.xyz;
	float RuntimeTargetClearanceCm = CanonicalRestOffsetAndClearanceCm.w;
	float3 CanonicalUpstreamPosition =
		ReadUpstreamGarmentPosition((uint)CanonicalVertexIndex);
	if (!all(isfinite(CanonicalUpstreamPosition)))
	{
		WriteFinalPosition(Index, WitnessPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}

	if (CanonicalThicknessReferenceAndLayer.y == 1)
	{
		int InnerReference = CanonicalThicknessReferenceAndLayer.x;
		if (InnerReference < 0
			|| InnerReference >= BindingVertexCount
			|| (uint)InnerReference >= RestOffsetBufferCount)
		{
			WriteFinalPosition(Index, WitnessPosition);
			WriteFinalTangentX(Index, WitnessTangentX);
			WriteFinalTangentZ(Index, WitnessTangentZ);
			return;
		}
		float4 InnerRest = RestOffsetAndClearanceCmBuffer[InnerReference];
		float3 InnerUpstream = ReadUpstreamGarmentPosition((uint)InnerReference);
		float3 LayerVectorCm = CanonicalRestOffsetAndClearanceCm.xyz
			- InnerRest.xyz;
		float LayerLengthCm = length(LayerVectorCm);
		if (!all(isfinite(InnerRest))
			|| !all(isfinite(InnerUpstream))
			|| LayerLengthCm <= 1.0e-6f)
		{
			WriteFinalPosition(Index, WitnessPosition);
			WriteFinalTangentX(Index, WitnessTangentX);
			WriteFinalTangentZ(Index, WitnessTangentZ);
			return;
		}
		float ThicknessScale = RuntimeInflateCm / LayerLengthCm;
		CanonicalUpstreamPosition = InnerUpstream
			+ (CanonicalUpstreamPosition - InnerUpstream) * ThicknessScale;
		RuntimeRestOffsetCm = InnerRest.xyz + LayerVectorCm * ThicknessScale;
		RuntimeTargetClearanceCm = InnerRest.w
			+ max(RuntimeRestOffsetCm.z - InnerRest.z, 0.0f);
	}
)EFHLSL")) + TEXT(R"EFHLSL(

	float BaseTargetGapCm = max(RuntimeTargetClearanceCm, 0.0f);
	if (CanonicalTriangleAndMode.w == 0)
	{
		BaseTargetGapCm = max(BaseTargetGapCm, RuntimeRestOffsetCm.z);
	}
	float SourceSurfaceInflateCm =
		(CanonicalThicknessReferenceAndLayer.x < 0
			&& CanonicalThicknessReferenceAndLayer.y == 0)
			? RuntimeInflateCm
			: 0.0f;
	float CorrectionOverrideCm = ReadEF_MaximumCorrectionOverrideCm();
	CorrectionOverrideCm = isfinite(CorrectionOverrideCm)
		? max(CorrectionOverrideCm, 0.0f)
		: 0.0f;
	float MaximumCorrectionCm = CorrectionOverrideCm > 0.0f
		? CorrectionOverrideCm
		: max(CanonicalMaximumCorrectionAndRestGapCm.x, 0.0f);
	float RuntimeCorrectionBudgetCm =
		clamp(max(RuntimeOffsetCm, 0.0f), 0.0f, 2.0f)
		+ RuntimeInflateCm;
	float MaximumAutomaticBodyShapeTravelCm =
		ReadEF_MaximumAutomaticBodyShapeTravelCm();
	MaximumAutomaticBodyShapeTravelCm =
		isfinite(MaximumAutomaticBodyShapeTravelCm)
		? clamp(MaximumAutomaticBodyShapeTravelCm, 0.0f, 32.0f)
		: 0.0f;
	float3 SurfaceTarget = SurfaceAnchor
		+ SurfaceTangent * RuntimeRestOffsetCm.x
		+ SurfaceBitangent * RuntimeRestOffsetCm.y
		+ SurfaceNormal * RuntimeRestOffsetCm.z;
	float3 SurfaceDelta = SurfaceTarget - CanonicalUpstreamPosition;
	float SurfaceDeltaLengthCm = length(SurfaceDelta);
	float SurfaceDeltaScale = SurfaceDeltaLengthCm > 1.0e-8f
		? min(1.0f, MaximumAutomaticBodyShapeTravelCm / SurfaceDeltaLengthCm)
		: 0.0f;
	float OutwardSurfaceTravelCm = clamp(
		max(dot(SurfaceDelta * SurfaceDeltaScale, SurfaceNormal), 0.0f),
		0.0f,
		MaximumAutomaticBodyShapeTravelCm);
	float UnderBreastClearanceMaxCm = ReadEF_UnderBreastClearanceMaxCm();
	UnderBreastClearanceMaxCm = isfinite(UnderBreastClearanceMaxCm)
		? clamp(UnderBreastClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float UnderBreastTravelGate = smoothstep(
		0.02f,
		0.75f,
		min(SurfaceDeltaLengthCm, MaximumAutomaticBodyShapeTravelCm));
	float UnderBreastReserveCm = BreastMorphStrength
		* CanonicalUnderBreastGuardWeight
		* UnderBreastClearanceMaxCm
		* UnderBreastTravelGate;
	float LowerBodyMorphClearanceMaxCm =
		ReadEF_LowerBodyMorphClearanceMaxCm();
	LowerBodyMorphClearanceMaxCm = isfinite(LowerBodyMorphClearanceMaxCm)
		? clamp(LowerBodyMorphClearanceMaxCm, 0.0f, 0.35f)
		: 0.0f;
	float LowerBodyMorphReserveCm = LowerBodyMorphStrength
		* CanonicalLowerBodyMorphGuardWeight
		* LowerBodyMorphClearanceMaxCm;
	float TargetGapCm = max(
		BaseTargetGapCm + RuntimeOffsetCm + SourceSurfaceInflateCm
			+ UnderBreastReserveCm + LowerBodyMorphReserveCm,
		0.0f);
	float SignedGapCm = dot(SharedPosition - SurfaceAnchor, SurfaceNormal);
	float RequiredPushCm = max(TargetGapCm - SignedGapCm, 0.0f);
	float AllowedAdditionalPushCm = MaximumCorrectionCm
		+ RuntimeCorrectionBudgetCm
		+ OutwardSurfaceTravelCm
		+ UnderBreastReserveCm
		+ LowerBodyMorphReserveCm;
	float MaximumTotalTravelCm = MaximumAutomaticBodyShapeTravelCm
		+ MaximumCorrectionCm
		+ RuntimeCorrectionBudgetCm
		+ UnderBreastReserveCm
		+ LowerBodyMorphReserveCm;
	float3 ProjectedSharedPosition = SharedPosition
		+ SurfaceNormal * RequiredPushCm;
	bool ProjectionWithinBudget = isfinite(RequiredPushCm)
		&& RequiredPushCm <= AllowedAdditionalPushCm + 1.0e-5f
		&& all(isfinite(ProjectedSharedPosition))
		&& length(ProjectedSharedPosition - CanonicalUpstreamPosition)
			<= MaximumTotalTravelCm + 1.0e-4f;
	if (!ProjectionWithinBudget)
	{
		// All members use the same canonical witness fallback, so even a saturated
		// morph cannot split a render seam into the visible triangular tears seen in V4.
		ProjectedSharedPosition =
			ReadWitnessPosition((uint)CanonicalVertexIndex);
	}
	if (!all(isfinite(ProjectedSharedPosition)))
	{
		ProjectedSharedPosition = WitnessPosition;
	}

	float UpstreamNormalLengthSquared = dot(
		WitnessTangentZ.xyz,
		WitnessTangentZ.xyz);
	if (!isfinite(UpstreamNormalLengthSquared)
		|| UpstreamNormalLengthSquared <= 1.0e-12f)
	{
		WriteFinalPosition(Index, ProjectedSharedPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}
	float3 UpstreamNormal = WitnessTangentZ.xyz
		* rsqrt(UpstreamNormalLengthSquared);
	float3 UpstreamTangent = WitnessTangentX.xyz
		- UpstreamNormal * dot(WitnessTangentX.xyz, UpstreamNormal);
	float UpstreamTangentLengthSquared = dot(UpstreamTangent, UpstreamTangent);
	if (!isfinite(UpstreamTangentLengthSquared)
		|| UpstreamTangentLengthSquared <= 1.0e-12f)
	{
		WriteFinalPosition(Index, ProjectedSharedPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}
	UpstreamTangent *= rsqrt(UpstreamTangentLengthSquared);
	if (dot(SurfaceNormal, UpstreamNormal) < 0.0f)
	{
		SurfaceNormal = -SurfaceNormal;
		SurfaceTangent = -SurfaceTangent;
	}
	if (dot(SurfaceTangent, UpstreamTangent) < 0.0f)
	{
		SurfaceTangent = -SurfaceTangent;
	}
	float FrameBlend = saturate(min(CurrentFollowWeight, CanonicalFollowWeight));
	float3 FinalNormal = lerp(UpstreamNormal, SurfaceNormal, FrameBlend);
	float FinalNormalLengthSquared = dot(FinalNormal, FinalNormal);
	if (!isfinite(FinalNormalLengthSquared)
		|| FinalNormalLengthSquared <= 1.0e-12f)
	{
		WriteFinalPosition(Index, ProjectedSharedPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}
	FinalNormal *= rsqrt(FinalNormalLengthSquared);
	float3 ProjectedUpstreamTangent = UpstreamTangent
		- FinalNormal * dot(UpstreamTangent, FinalNormal);
	float3 ProjectedBodyTangent = SurfaceTangent
		- FinalNormal * dot(SurfaceTangent, FinalNormal);
	float3 FinalTangent = lerp(
		ProjectedUpstreamTangent,
		ProjectedBodyTangent,
		FrameBlend);
	FinalTangent -= FinalNormal * dot(FinalTangent, FinalNormal);
	float FinalTangentLengthSquared = dot(FinalTangent, FinalTangent);
	if (!isfinite(FinalTangentLengthSquared)
		|| FinalTangentLengthSquared <= 1.0e-12f)
	{
		WriteFinalPosition(Index, ProjectedSharedPosition);
		WriteFinalTangentX(Index, WitnessTangentX);
		WriteFinalTangentZ(Index, WitnessTangentZ);
		return;
	}
	FinalTangent *= rsqrt(FinalTangentLengthSquared);
	WriteFinalPosition(Index, ProjectedSharedPosition);
	WriteFinalTangentX(Index, float4(FinalTangent, WitnessTangentX.w));
	WriteFinalTangentZ(Index, float4(FinalNormal, WitnessTangentZ.w));
}
)EFHLSL");
		return Source;
	}

	UOptimusNodePin* RequirePin(UOptimusNode* Node, const FString& PinPath, FString& OutError)
	{
		if (!Node)
		{
			OutError = FString::Printf(TEXT("Null node while resolving pin %s."), *PinPath);
			return nullptr;
		}
		UOptimusNodePin* Pin = Node->FindPin(PinPath);
		if (!Pin)
		{
			OutError = FString::Printf(
				TEXT("Node %s does not expose required pin %s."),
				*Node->GetPathName(),
				*PinPath);
		}
		return Pin;
	}

	bool AddRequiredLink(
		UOptimusNodeGraph* Graph,
		UOptimusNode* OutputNode,
		const FString& OutputPinPath,
		UOptimusNode* InputNode,
		const FString& InputPinPath,
		FString& OutError)
	{
		UOptimusNodePin* OutputPin = RequirePin(OutputNode, OutputPinPath, OutError);
		UOptimusNodePin* InputPin = RequirePin(InputNode, InputPinPath, OutError);
		if (!OutputPin || !InputPin)
		{
			return false;
		}
		if (!Graph->AddLink(OutputPin, InputPin))
		{
			FString Reason;
			InputPin->CanCannect(OutputPin, &Reason);
			OutError = FString::Printf(
				TEXT("Failed to link %s.%s -> %s.%s%s%s."),
				*OutputNode->GetName(),
				*OutputPinPath,
				*InputNode->GetName(),
				*InputPinPath,
				Reason.IsEmpty() ? TEXT("") : TEXT(": "),
				*Reason);
			return false;
		}
		return true;
	}

	bool SetInvalidLODDefault(UOptimusVariableDescription* Variable, FString& OutError)
	{
		if (!Variable)
		{
			OutError = TEXT("Cannot initialize a null Optimus variable.");
			return false;
		}
		const int32 InvalidLOD = INDEX_NONE;
		const TArrayView<const uint8> RawValue(
			reinterpret_cast<const uint8*>(&InvalidLOD),
			sizeof(InvalidLOD));
		Variable->DefaultValueStruct.SetValue(Variable->DataType, RawValue);
		return true;
	}

	bool RebuildGraph(UOptimusDeformer* Deformer, FString& OutError)
	{
		UClass* KernelClass = nullptr;
		UClass* ReadClass = nullptr;
		UClass* WriteClass = nullptr;
		UClass* SkeletalSourceClass = nullptr;
		UClass* ComponentNodeClass = nullptr;
		UClass* VariableNodeClass = nullptr;
		if (!ResolveOptimusClasses(
			KernelClass,
			ReadClass,
			WriteClass,
			SkeletalSourceClass,
			ComponentNodeClass,
			VariableNodeClass,
			OutError))
		{
			return false;
		}

		const UOptimusComponentSource* SkeletalSource = Cast<UOptimusComponentSource>(
			SkeletalSourceClass->GetDefaultObject());
		if (!SkeletalSource)
		{
			OutError = TEXT("Unable to obtain the Optimus Skeletal Mesh component source CDO.");
			return false;
		}

		UOptimusNodeGraph* Graph = Deformer->GetUpdateGraph();
		if (!Graph)
		{
			OutError = TEXT("The Optimus deformer has no Update graph.");
			return false;
		}
		const TArray<UOptimusNode*> ExistingNodes = Graph->GetAllNodes();
		if (!ExistingNodes.IsEmpty() && !Graph->RemoveNodes(ExistingNodes))
		{
			OutError = TEXT("Failed to clear the generated Optimus Update graph.");
			return false;
		}
		const TArray<UOptimusResourceDescription*> ExistingResources = Deformer->GetResources();
		for (UOptimusResourceDescription* Resource : ExistingResources)
		{
			if (!Deformer->RemoveResource(Resource))
			{
				OutError = FString::Printf(TEXT("Failed to remove stale resource %s."), *GetNameSafe(Resource));
				return false;
			}
		}

		const TArray<UOptimusVariableDescription*> ExistingVariables = Deformer->GetVariables();
		for (UOptimusVariableDescription* Variable : ExistingVariables)
		{
			if (!Deformer->RemoveVariable(Variable))
			{
				OutError = FString::Printf(TEXT("Failed to remove stale variable %s."), *GetNameSafe(Variable));
				return false;
			}
		}

		UOptimusComponentSourceBinding* PrimaryBinding = Deformer->GetPrimaryComponentBinding();
		const TArray<UOptimusComponentSourceBinding*> ExistingBindings = Deformer->GetComponentBindings();
		for (UOptimusComponentSourceBinding* Binding : ExistingBindings)
		{
			if (Binding != PrimaryBinding && !Deformer->RemoveComponentBinding(Binding))
			{
				OutError = FString::Printf(TEXT("Failed to remove stale component binding %s."), *GetNameSafe(Binding));
				return false;
			}
		}
		if (!PrimaryBinding)
		{
			PrimaryBinding = Deformer->AddComponentBinding(
				SkeletalSource,
				UOptimusComponentSourceBinding::GetPrimaryBindingName());
		}
		if (!PrimaryBinding)
		{
			OutError = TEXT("Failed to create the primary Garment component binding.");
			return false;
		}
		if (PrimaryBinding->BindingName != UOptimusComponentSourceBinding::GetPrimaryBindingName()
			&& !Deformer->RenameComponentBinding(
				PrimaryBinding,
				UOptimusComponentSourceBinding::GetPrimaryBindingName(),
				true))
		{
			OutError = TEXT("Failed to restore the Optimus primary binding name.");
			return false;
		}
		if (!Deformer->SetComponentBindingSource(PrimaryBinding, SkeletalSource, true))
		{
			OutError = TEXT("Failed to configure the primary Garment binding as Skeletal Mesh.");
			return false;
		}

		UOptimusComponentSourceBinding* BodyBinding = Deformer->AddComponentBinding(SkeletalSource, TEXT("Body"));
		if (!BodyBinding || BodyBinding->BindingName != TEXT("Body"))
		{
			OutError = TEXT("Failed to create the exact secondary Body component binding.");
			return false;
		}

		TMap<FName, UOptimusVariableDescription*> Variables;
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			const FOptimusDataTypeRef Type = ResolveVariableType(Spec.Type);
			if (!Type.IsValid())
			{
				OutError = FString::Printf(TEXT("Optimus type for %s is unavailable."), *Spec.Name.ToString());
				return false;
			}
			UOptimusVariableDescription* Variable = Deformer->AddVariable(Type, Spec.Name);
			if (!Variable || Variable->VariableName != Spec.Name)
			{
				OutError = FString::Printf(TEXT("Failed to create exact variable %s."), *Spec.Name.ToString());
				return false;
			}
			if (Spec.bDefaultToInvalidLOD && !SetInvalidLODDefault(Variable, OutError))
			{
				return false;
			}
			Variables.Add(Spec.Name, Variable);
		}

		const FOptimusDataDomain VertexDomain(TArray<FName>{ Optimus::DomainName::Vertex });
		const FOptimusDataDomain SingletonDomain;
		auto AddPositionResource = [Deformer, PrimaryBinding, &VertexDomain, &OutError](
			const FName ResourceName,
			UOptimusResourceDescription*& OutResource) -> bool
		{
			OutResource = Deformer->AddResource(ResolveVector3Type(), ResourceName);
			if (!OutResource)
			{
				OutError = FString::Printf(
					TEXT("Failed to create the %s Optimus resource."),
					*ResourceName.ToString());
				return false;
			}
			OutResource->ComponentBinding = PrimaryBinding;
			OutResource->Modify();
			if (!Deformer->SetResourceDataDomain(OutResource, VertexDomain, true))
			{
				OutError = FString::Printf(
					TEXT("Failed to bind %s to the Garment vertex domain."),
					*ResourceName.ToString());
				return false;
			}
			return true;
		};

		UOptimusResourceDescription* BasePositionResource = nullptr;
		UOptimusResourceDescription* CohesionAPositionResource = nullptr;
		UOptimusResourceDescription* CohesionBPositionResource = nullptr;
		UOptimusResourceDescription* WitnessPositionResource = nullptr;
		if (!AddPositionResource(BasePositionResourceName, BasePositionResource)
			|| !AddPositionResource(CohesionAPositionResourceName, CohesionAPositionResource)
			|| !AddPositionResource(CohesionBPositionResourceName, CohesionBPositionResource)
			|| !AddPositionResource(WitnessPositionResourceName, WitnessPositionResource))
		{
			return false;
		}

		UOptimusNode* PrimaryBindingNode = Graph->AddComponentBindingGetNode(PrimaryBinding, FVector2D(-2100.0, -300.0));
		UOptimusNode* BodyBindingNode = Graph->AddComponentBindingGetNode(BodyBinding, FVector2D(-2100.0, 760.0));
		UOptimusNode* GarmentReadNode = Graph->AddDataInterfaceNode(ReadClass, FVector2D(-1760.0, -340.0));
		UOptimusNode* BodyReadNode = Graph->AddDataInterfaceNode(ReadClass, FVector2D(-1760.0, 760.0));
		UOptimusNode* BaseKernelNode = Graph->AddNode(KernelClass, FVector2D(-660.0, -220.0));
		UOptimusNode* BasePositionResourceNode = Graph->AddResourceNode(
			BasePositionResource,
			FVector2D(-20.0, -360.0));
		UOptimusNode* CohesionAKernelNode = Graph->AddNode(KernelClass, FVector2D(360.0, -220.0));
		UOptimusNode* CohesionAPositionResourceNode = Graph->AddResourceNode(
			CohesionAPositionResource,
			FVector2D(1000.0, -360.0));
		UOptimusNode* CohesionBKernelNode = Graph->AddNode(KernelClass, FVector2D(1380.0, -220.0));
		UOptimusNode* CohesionBPositionResourceNode = Graph->AddResourceNode(
			CohesionBPositionResource,
			FVector2D(2020.0, -360.0));
		UOptimusNode* WitnessKernelNode = Graph->AddNode(KernelClass, FVector2D(2400.0, -120.0));
		UOptimusNode* WitnessPositionResourceNode = Graph->AddResourceNode(
			WitnessPositionResource,
			FVector2D(3040.0, -360.0));
		UOptimusNode* FinalizeKernelNode = Graph->AddNode(KernelClass, FVector2D(3420.0, -120.0));
		UOptimusNode* WriteNode = Graph->AddDataInterfaceNode(WriteClass, FVector2D(4080.0, -100.0));
		if (!PrimaryBindingNode
			|| !BodyBindingNode
			|| !GarmentReadNode
			|| !BodyReadNode
			|| !BaseKernelNode
			|| !BasePositionResourceNode
			|| !CohesionAKernelNode
			|| !CohesionAPositionResourceNode
			|| !CohesionBKernelNode
			|| !CohesionBPositionResourceNode
			|| !WitnessKernelNode
			|| !WitnessPositionResourceNode
			|| !FinalizeKernelNode
			|| !WriteNode)
		{
			OutError = TEXT("Failed to create one or more required Optimus nodes.");
			return false;
		}

		if (!SetValidatedNameProperty(BaseKernelNode, TEXT("KernelName"), BaseKernelName, OutError)
			|| !SetExecutionDomain(BaseKernelNode, OutError)
			|| !SetGroupSize(BaseKernelNode, OutError)
			|| !SetValidatedNameProperty(CohesionAKernelNode, TEXT("KernelName"), CohesionAKernelName, OutError)
			|| !SetExecutionDomain(CohesionAKernelNode, OutError)
			|| !SetGroupSize(CohesionAKernelNode, OutError)
			|| !SetValidatedNameProperty(CohesionBKernelNode, TEXT("KernelName"), CohesionBKernelName, OutError)
			|| !SetExecutionDomain(CohesionBKernelNode, OutError)
			|| !SetGroupSize(CohesionBKernelNode, OutError)
			|| !SetValidatedNameProperty(WitnessKernelNode, TEXT("KernelName"), WitnessKernelName, OutError)
			|| !SetExecutionDomain(WitnessKernelNode, OutError)
			|| !SetGroupSize(WitnessKernelNode, OutError)
			|| !SetValidatedNameProperty(FinalizeKernelNode, TEXT("KernelName"), FinalizeKernelName, OutError)
			|| !SetExecutionDomain(FinalizeKernelNode, OutError)
			|| !SetGroupSize(FinalizeKernelNode, OutError))
		{
			return false;
		}

		TArray<FOptimusParameterBinding> BaseInputs;
		BaseInputs.Add(MakeBinding(TEXT("GarmentPosition"), ResolveVector3Type(), VertexDomain));
		BaseInputs.Add(MakeBinding(TEXT("GarmentTangentX"), ResolveVector4Type(), VertexDomain));
		BaseInputs.Add(MakeBinding(TEXT("GarmentTangentZ"), ResolveVector4Type(), VertexDomain));
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			BaseInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
		}

		const TArray<FOptimusParameterBinding> BaseOutputs =
		{
			MakeBinding(TEXT("CorrectedPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("PreservedTangentX"), ResolveVector4Type(), VertexDomain),
			MakeBinding(TEXT("PreservedTangentZ"), ResolveVector4Type(), VertexDomain)
		};
		TArray<FOptimusParameterBinding> CohesionInputs =
		{
			MakeBinding(TEXT("RawCorrectedPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("UpstreamGarmentPosition"), ResolveVector3Type(), VertexDomain)
		};
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			CohesionInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
		}
		const TArray<FOptimusParameterBinding> CohesionOutputs =
		{
			MakeBinding(TEXT("CohesivePosition"), ResolveVector3Type(), VertexDomain)
		};
		TArray<FOptimusParameterBinding> WitnessInputs =
		{
			MakeBinding(TEXT("BaseCorrectedPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("BaseTangentX"), ResolveVector4Type(), VertexDomain),
			MakeBinding(TEXT("BaseTangentZ"), ResolveVector4Type(), VertexDomain)
		};
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			WitnessInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
		}
		const TArray<FOptimusParameterBinding> WitnessOutputs =
		{
			MakeBinding(TEXT("FinalPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("FinalTangentX"), ResolveVector4Type(), VertexDomain),
			MakeBinding(TEXT("FinalTangentZ"), ResolveVector4Type(), VertexDomain)
		};
		TArray<FOptimusParameterBinding> FinalizeInputs =
		{
			MakeBinding(TEXT("WitnessPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("UpstreamGarmentPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("WitnessTangentX"), ResolveVector4Type(), VertexDomain),
			MakeBinding(TEXT("WitnessTangentZ"), ResolveVector4Type(), VertexDomain)
		};
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			FinalizeInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
		}
		const TArray<FOptimusParameterBinding> FinalizeOutputs =
		{
			MakeBinding(TEXT("FinalPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("FinalTangentX"), ResolveVector4Type(), VertexDomain),
			MakeBinding(TEXT("FinalTangentZ"), ResolveVector4Type(), VertexDomain)
		};
		const TArray<FOptimusParameterBinding> BodyInputs =
		{
			MakeBinding(TEXT("BodyPosition"), ResolveVector3Type(), VertexDomain)
		};
		if (!SetParameterBindingArray(BaseKernelNode, TEXT("InputBindingArray"), BaseInputs, OutError)
			|| !SetParameterBindingArray(BaseKernelNode, TEXT("OutputBindingArray"), BaseOutputs, OutError)
			|| !SetSecondaryBodyBindings(BaseKernelNode, BodyInputs, OutError)
			|| !SetParameterBindingArray(CohesionAKernelNode, TEXT("InputBindingArray"), CohesionInputs, OutError)
			|| !SetParameterBindingArray(CohesionAKernelNode, TEXT("OutputBindingArray"), CohesionOutputs, OutError)
			|| !SetSecondaryBodyBindings(CohesionAKernelNode, BodyInputs, OutError)
			|| !SetParameterBindingArray(CohesionBKernelNode, TEXT("InputBindingArray"), CohesionInputs, OutError)
			|| !SetParameterBindingArray(CohesionBKernelNode, TEXT("OutputBindingArray"), CohesionOutputs, OutError)
			|| !SetSecondaryBodyBindings(CohesionBKernelNode, BodyInputs, OutError)
			|| !SetParameterBindingArray(WitnessKernelNode, TEXT("InputBindingArray"), WitnessInputs, OutError)
			|| !SetParameterBindingArray(WitnessKernelNode, TEXT("OutputBindingArray"), WitnessOutputs, OutError)
			|| !SetSecondaryBodyBindings(WitnessKernelNode, BodyInputs, OutError)
			|| !SetParameterBindingArray(FinalizeKernelNode, TEXT("InputBindingArray"), FinalizeInputs, OutError)
			|| !SetParameterBindingArray(FinalizeKernelNode, TEXT("OutputBindingArray"), FinalizeOutputs, OutError)
			|| !SetSecondaryBodyBindings(FinalizeKernelNode, BodyInputs, OutError))
		{
			return false;
		}

		IOptimusShaderTextProvider* BaseShaderTextProvider = Cast<IOptimusShaderTextProvider>(BaseKernelNode);
		IOptimusShaderTextProvider* CohesionAShaderTextProvider = Cast<IOptimusShaderTextProvider>(CohesionAKernelNode);
		IOptimusShaderTextProvider* CohesionBShaderTextProvider = Cast<IOptimusShaderTextProvider>(CohesionBKernelNode);
		IOptimusShaderTextProvider* WitnessShaderTextProvider = Cast<IOptimusShaderTextProvider>(WitnessKernelNode);
		IOptimusShaderTextProvider* FinalizeShaderTextProvider = Cast<IOptimusShaderTextProvider>(FinalizeKernelNode);
		if (!BaseShaderTextProvider
			|| !CohesionAShaderTextProvider
			|| !CohesionBShaderTextProvider
			|| !WitnessShaderTextProvider
			|| !FinalizeShaderTextProvider)
		{
			OutError = TEXT("One or more reflected custom kernels do not implement IOptimusShaderTextProvider.");
			return false;
		}
		BaseShaderTextProvider->SetShaderText(GetBaseKernelSource());
		CohesionAShaderTextProvider->SetShaderText(GetCohesionKernelSource());
		CohesionBShaderTextProvider->SetShaderText(GetCohesionKernelSource());
		WitnessShaderTextProvider->SetShaderText(GetWitnessKernelSource());
		FinalizeShaderTextProvider->SetShaderText(GetFinalizeKernelSource());

		if (!AddRequiredLink(Graph, PrimaryBindingNode, SkeletalComponentPin, GarmentReadNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, PrimaryBindingNode, SkeletalComponentPin, WriteNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, BodyBindingNode, SkeletalComponentPin, BodyReadNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("Position"), BaseKernelNode, TEXT("Primary Group.GarmentPosition"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("TangentX"), BaseKernelNode, TEXT("Primary Group.GarmentTangentX"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("TangentZ"), BaseKernelNode, TEXT("Primary Group.GarmentTangentZ"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), BaseKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, BaseKernelNode, TEXT("CorrectedPosition"), BasePositionResourceNode, TEXT("SetBaseCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, BasePositionResourceNode, TEXT("GetBaseCorrectedPosition"), CohesionAKernelNode, TEXT("Primary Group.RawCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("Position"), CohesionAKernelNode, TEXT("Primary Group.UpstreamGarmentPosition"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), CohesionAKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, CohesionAKernelNode, TEXT("CohesivePosition"), CohesionAPositionResourceNode, TEXT("SetCohesionACorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, CohesionAPositionResourceNode, TEXT("GetCohesionACorrectedPosition"), CohesionBKernelNode, TEXT("Primary Group.RawCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("Position"), CohesionBKernelNode, TEXT("Primary Group.UpstreamGarmentPosition"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), CohesionBKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, CohesionBKernelNode, TEXT("CohesivePosition"), CohesionBPositionResourceNode, TEXT("SetCohesionBCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, CohesionBPositionResourceNode, TEXT("GetCohesionBCorrectedPosition"), WitnessKernelNode, TEXT("Primary Group.BaseCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, BaseKernelNode, TEXT("PreservedTangentX"), WitnessKernelNode, TEXT("Primary Group.BaseTangentX"), OutError)
			|| !AddRequiredLink(Graph, BaseKernelNode, TEXT("PreservedTangentZ"), WitnessKernelNode, TEXT("Primary Group.BaseTangentZ"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), WitnessKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, WitnessKernelNode, TEXT("FinalPosition"), WitnessPositionResourceNode, TEXT("SetWitnessCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, WitnessPositionResourceNode, TEXT("GetWitnessCorrectedPosition"), FinalizeKernelNode, TEXT("Primary Group.WitnessPosition"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("Position"), FinalizeKernelNode, TEXT("Primary Group.UpstreamGarmentPosition"), OutError)
			|| !AddRequiredLink(Graph, WitnessKernelNode, TEXT("FinalTangentX"), FinalizeKernelNode, TEXT("Primary Group.WitnessTangentX"), OutError)
			|| !AddRequiredLink(Graph, WitnessKernelNode, TEXT("FinalTangentZ"), FinalizeKernelNode, TEXT("Primary Group.WitnessTangentZ"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), FinalizeKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, FinalizeKernelNode, TEXT("FinalPosition"), WriteNode, TEXT("Position"), OutError)
			|| !AddRequiredLink(Graph, FinalizeKernelNode, TEXT("FinalTangentX"), WriteNode, TEXT("TangentX"), OutError)
			|| !AddRequiredLink(Graph, FinalizeKernelNode, TEXT("FinalTangentZ"), WriteNode, TEXT("TangentZ"), OutError))
		{
			return false;
		}

		int32 VariableNodeIndex = 0;
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			UOptimusVariableDescription* const* Variable = Variables.Find(Spec.Name);
			UOptimusNode* VariableNode = Variable
				? Graph->AddVariableGetNode(*Variable, FVector2D(-1040.0, -520.0 + VariableNodeIndex * 90.0))
				: nullptr;
			if (!VariableNode
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					BaseKernelNode,
					FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString()),
					OutError)
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					CohesionAKernelNode,
					FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString()),
					OutError)
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					CohesionBKernelNode,
					FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString()),
					OutError)
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					WitnessKernelNode,
					FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString()),
					OutError)
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					FinalizeKernelNode,
					FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString()),
					OutError))
			{
				return false;
			}
			++VariableNodeIndex;
		}

		return true;
	}

	UClass* GetDataInterfaceClass(const UOptimusNode* Node)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("DataInterfaceClass"));
		return Property ? Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Node)) : nullptr;
	}

	UOptimusComponentSourceBinding* GetNodeComponentBinding(const UOptimusNode* Node)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("Binding"));
		return Property
			? Cast<UOptimusComponentSourceBinding>(Property->GetObjectPropertyValue_InContainer(Node))
			: nullptr;
	}

	bool ArePinsDirectlyLinked(UOptimusNode* OutputNode, const FString& OutputPath, UOptimusNode* InputNode, const FString& InputPath)
	{
		UOptimusNodePin* OutputPin = OutputNode ? OutputNode->FindPin(OutputPath) : nullptr;
		UOptimusNodePin* InputPin = InputNode ? InputNode->FindPin(InputPath) : nullptr;
		return OutputPin && InputPin && InputPin->GetConnectedPins().Contains(OutputPin);
	}

	bool ValidateBindingArray(
		const UObject* KernelNode,
		const FName PropertyName,
		const TArray<FOptimusParameterBinding>& Expected,
		FString& OutError)
	{
		if (!KernelNode)
		{
			OutError = FString::Printf(TEXT("Missing kernel while validating binding array %s."), *PropertyName.ToString());
			return false;
		}
		const FStructProperty* Property = FindFProperty<FStructProperty>(KernelNode->GetClass(), PropertyName);
		if (!Property || Property->Struct != FOptimusParameterBindingArray::StaticStruct())
		{
			OutError = FString::Printf(TEXT("Missing reflected kernel binding array %s."), *PropertyName.ToString());
			return false;
		}
		const FOptimusParameterBindingArray* Actual =
			Property->ContainerPtrToValuePtr<FOptimusParameterBindingArray>(KernelNode);
		if (!Actual || Actual->Num() != Expected.Num())
		{
			OutError = FString::Printf(
				TEXT("Kernel binding array %s has %d entries; expected %d."),
				*PropertyName.ToString(),
				Actual ? Actual->Num() : 0,
				Expected.Num());
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			const FOptimusParameterBinding& A = (*Actual)[Index];
			const FOptimusParameterBinding& E = Expected[Index];
			if (A.Name != E.Name || A.DataType != E.DataType || A.DataDomain != E.DataDomain)
			{
				OutError = FString::Printf(
					TEXT("Kernel binding %s[%d] does not match schema 35 (%s)."),
					*PropertyName.ToString(),
					Index,
					*E.Name.ToString());
				return false;
			}
		}
		return true;
	}

	bool ValidateSecondaryBodyBindings(const UObject* KernelNode, FString& OutError)
	{
		if (!KernelNode)
		{
			OutError = TEXT("Missing kernel while validating the secondary Body group.");
			return false;
		}
		const FArrayProperty* GroupsProperty = FindFProperty<FArrayProperty>(
			KernelNode->GetClass(),
			TEXT("SecondaryInputBindingGroups"));
		const FStructProperty* GroupStructProperty = GroupsProperty
			? CastField<FStructProperty>(GroupsProperty->Inner)
			: nullptr;
		if (!GroupsProperty || !GroupStructProperty)
		{
			OutError = TEXT("Missing reflected secondary Body group.");
			return false;
		}
		FScriptArrayHelper ArrayHelper(
			GroupsProperty,
			GroupsProperty->ContainerPtrToValuePtr<void>(KernelNode));
		if (ArrayHelper.Num() != 1)
		{
			OutError = FString::Printf(TEXT("Kernel has %d secondary groups; expected exactly Body."), ArrayHelper.Num());
			return false;
		}

		const FStructProperty* GroupNameProperty = FindFProperty<FStructProperty>(
			GroupStructProperty->Struct,
			TEXT("GroupName"));
		const FStructProperty* BindingArrayProperty = FindFProperty<FStructProperty>(
			GroupStructProperty->Struct,
			TEXT("BindingArray"));
		const void* GroupMemory = ArrayHelper.GetRawPtr(0);
		if (!GroupNameProperty || !BindingArrayProperty || !GroupMemory)
		{
			OutError = TEXT("Secondary Body group reflection is incomplete.");
			return false;
		}
		const FOptimusValidatedName* GroupName =
			GroupNameProperty->ContainerPtrToValuePtr<FOptimusValidatedName>(GroupMemory);
		const FOptimusParameterBindingArray* Bindings =
			BindingArrayProperty->ContainerPtrToValuePtr<FOptimusParameterBindingArray>(GroupMemory);
		const FOptimusDataDomain VertexDomain(TArray<FName>{ Optimus::DomainName::Vertex });
		if (!GroupName || GroupName->Name != BodyGroupPin
			|| !Bindings
			|| Bindings->Num() != 1
			|| (*Bindings)[0].Name != TEXT("BodyPosition")
			|| (*Bindings)[0].DataType != ResolveVector3Type()
			|| (*Bindings)[0].DataDomain != VertexDomain)
		{
			OutError = TEXT("Secondary group must be Body with a single BodyPosition float3 Vertex binding.");
			return false;
		}
		return true;
	}

	bool ValidateGraph(UOptimusDeformer* Deformer, bool bRequireMetadata, FString& OutReport)
	{
		TArray<FString> Errors;
		UClass* KernelClass = nullptr;
		UClass* ReadClass = nullptr;
		UClass* WriteClass = nullptr;
		UClass* SkeletalSourceClass = nullptr;
		UClass* ComponentNodeClass = nullptr;
		UClass* VariableNodeClass = nullptr;
		FString Error;
		if (!ResolveOptimusClasses(
			KernelClass,
			ReadClass,
			WriteClass,
			SkeletalSourceClass,
			ComponentNodeClass,
			VariableNodeClass,
			Error))
		{
			OutReport = Error;
			return false;
		}

		if (!Deformer || Deformer->GetPathName() != AssetObjectPath)
		{
			OutReport = FString::Printf(TEXT("Expected canonical asset %s."), AssetObjectPath);
			return false;
		}

		const TArray<UOptimusComponentSourceBinding*>& Bindings = Deformer->GetComponentBindings();
		UOptimusComponentSourceBinding* PrimaryBinding = Deformer->GetPrimaryComponentBinding();
		UOptimusComponentSourceBinding* BodyBinding = Deformer->ResolveComponentBinding(TEXT("Body"));
		if (Bindings.Num() != 2)
		{
			Errors.Add(FString::Printf(TEXT("Component binding count is %d, expected 2."), Bindings.Num()));
		}
		if (!PrimaryBinding
			|| PrimaryBinding->BindingName != UOptimusComponentSourceBinding::GetPrimaryBindingName()
			|| PrimaryBinding->ComponentType != SkeletalSourceClass)
		{
			Errors.Add(TEXT("Primary binding is not the explicit Skeletal Mesh Garment binding."));
		}
		if (!BodyBinding || BodyBinding->IsPrimaryBinding() || BodyBinding->ComponentType != SkeletalSourceClass)
		{
			Errors.Add(TEXT("Secondary Body binding is missing or incompatible."));
		}
		const FOptimusDataDomain VertexDomain(TArray<FName>{ Optimus::DomainName::Vertex });
		const TArray<UOptimusResourceDescription*>& Resources = Deformer->GetResources();
		UOptimusResourceDescription* BasePositionResource = nullptr;
		UOptimusResourceDescription* CohesionAPositionResource = nullptr;
		UOptimusResourceDescription* CohesionBPositionResource = nullptr;
		UOptimusResourceDescription* WitnessPositionResource = nullptr;
		for (UOptimusResourceDescription* Resource : Resources)
		{
			if (Resource && Resource->ResourceName == BasePositionResourceName)
			{
				BasePositionResource = Resource;
			}
			else if (Resource && Resource->ResourceName == CohesionAPositionResourceName)
			{
				CohesionAPositionResource = Resource;
			}
			else if (Resource && Resource->ResourceName == CohesionBPositionResourceName)
			{
				CohesionBPositionResource = Resource;
			}
			else if (Resource && Resource->ResourceName == WitnessPositionResourceName)
			{
				WitnessPositionResource = Resource;
			}
		}
		if (Resources.Num() != 4)
		{
			Errors.Add(FString::Printf(TEXT("Resource count is %d, expected 4."), Resources.Num()));
		}
		auto IsValidPositionResource = [PrimaryBinding, &VertexDomain](
			const UOptimusResourceDescription* Resource,
			const FName ExpectedName) -> bool
		{
			return Resource
				&& Resource->ResourceName == ExpectedName
				&& Resource->DataType == ResolveVector3Type()
				&& Resource->ComponentBinding.Get() == PrimaryBinding
				&& Resource->DataDomain == VertexDomain;
		};
		if (!IsValidPositionResource(BasePositionResource, BasePositionResourceName))
		{
			Errors.Add(TEXT("BaseCorrectedPosition resource is missing or not bound to Garment float3/Vertex."));
		}
		if (!IsValidPositionResource(CohesionAPositionResource, CohesionAPositionResourceName))
		{
			Errors.Add(TEXT("CohesionACorrectedPosition resource is missing or not bound to Garment float3/Vertex."));
		}
		if (!IsValidPositionResource(CohesionBPositionResource, CohesionBPositionResourceName))
		{
			Errors.Add(TEXT("CohesionBCorrectedPosition resource is missing or not bound to Garment float3/Vertex."));
		}
		if (!IsValidPositionResource(WitnessPositionResource, WitnessPositionResourceName))
		{
			Errors.Add(TEXT("WitnessCorrectedPosition resource is missing or not bound to Garment float3/Vertex."));
		}

		const TArray<UOptimusVariableDescription*>& Variables = Deformer->GetVariables();
		if (Variables.Num() != GetVariableSpecs().Num())
		{
			Errors.Add(FString::Printf(
				TEXT("Variable count is %d, expected %d."),
				Variables.Num(),
				GetVariableSpecs().Num()));
		}
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			UOptimusVariableDescription* const* Match = Variables.FindByPredicate(
				[&Spec](const UOptimusVariableDescription* Variable)
				{
					return Variable && Variable->VariableName == Spec.Name;
				});
			if (!Match || !*Match || (*Match)->DataType != ResolveVariableType(Spec.Type))
			{
				Errors.Add(FString::Printf(TEXT("Variable %s is missing or has the wrong type."), *Spec.Name.ToString()));
			}
		}

		UOptimusNodeGraph* Graph = Deformer->GetUpdateGraph();
		if (!Graph)
		{
			Errors.Add(TEXT("Update graph is missing."));
		}
		else
		{
			const TArray<UOptimusNode*>& Nodes = Graph->GetAllNodes();
			const int32 ExpectedNodeCount = 2 + 3 + 5 + 4 + GetVariableSpecs().Num();
			if (Nodes.Num() != ExpectedNodeCount)
			{
				Errors.Add(FString::Printf(TEXT("Node count is %d, expected %d."), Nodes.Num(), ExpectedNodeCount));
			}

			TArray<UOptimusNode*> KernelNodes;
			TArray<UOptimusNode*> ReadNodes;
			TArray<UOptimusNode*> WriteNodes;
			TArray<UOptimusNode*> ComponentNodes;
			TArray<UOptimusNode*> VariableNodes;
			TArray<UOptimusNode*> ResourceNodes;
			for (UOptimusNode* Node : Nodes)
			{
				if (Node->GetClass() == KernelClass)
				{
					KernelNodes.Add(Node);
				}
				else if (Node->GetClass() == ComponentNodeClass)
				{
					ComponentNodes.Add(Node);
				}
				else if (Node->GetClass() == VariableNodeClass)
				{
					VariableNodes.Add(Node);
				}
				else if (GetDataInterfaceClass(Node) == ReadClass)
				{
					ReadNodes.Add(Node);
				}
				else if (GetDataInterfaceClass(Node) == WriteClass)
				{
					WriteNodes.Add(Node);
				}
				else if (Node
					&& ((Node->FindPin(TEXT("SetBaseCorrectedPosition"))
						&& Node->FindPin(TEXT("GetBaseCorrectedPosition")))
						|| (Node->FindPin(TEXT("SetCohesionACorrectedPosition"))
							&& Node->FindPin(TEXT("GetCohesionACorrectedPosition")))
						|| (Node->FindPin(TEXT("SetCohesionBCorrectedPosition"))
							&& Node->FindPin(TEXT("GetCohesionBCorrectedPosition")))
						|| (Node->FindPin(TEXT("SetWitnessCorrectedPosition"))
							&& Node->FindPin(TEXT("GetWitnessCorrectedPosition")))))
				{
					ResourceNodes.Add(Node);
				}
			}

			if (KernelNodes.Num() != 5
				|| ReadNodes.Num() != 2
				|| WriteNodes.Num() != 1
				|| ComponentNodes.Num() != 2
				|| VariableNodes.Num() != GetVariableSpecs().Num()
				|| ResourceNodes.Num() != 4)
			{
				Errors.Add(TEXT("Required five-kernel/read/write/component/resource/variable node cardinality does not match schema 35."));
			}
			else
			{
				auto GetKernelName = [](const UOptimusNode* Node) -> FName
				{
					const FStructProperty* Property = Node
						? FindFProperty<FStructProperty>(Node->GetClass(), TEXT("KernelName"))
						: nullptr;
					const FOptimusValidatedName* Value = Property
						? Property->ContainerPtrToValuePtr<FOptimusValidatedName>(Node)
						: nullptr;
					return Value ? Value->Name : NAME_None;
				};
				UOptimusNode* BaseKernelNode = nullptr;
				UOptimusNode* CohesionAKernelNode = nullptr;
				UOptimusNode* CohesionBKernelNode = nullptr;
				UOptimusNode* WitnessKernelNode = nullptr;
				UOptimusNode* FinalizeKernelNode = nullptr;
				for (UOptimusNode* KernelNode : KernelNodes)
				{
					const FName Name = GetKernelName(KernelNode);
					if (Name == BaseKernelName)
					{
						BaseKernelNode = KernelNode;
					}
					else if (Name == CohesionAKernelName)
					{
						CohesionAKernelNode = KernelNode;
					}
					else if (Name == CohesionBKernelName)
					{
						CohesionBKernelNode = KernelNode;
					}
					else if (Name == WitnessKernelName)
					{
						WitnessKernelNode = KernelNode;
					}
					else if (Name == FinalizeKernelName)
					{
						FinalizeKernelNode = KernelNode;
					}
				}
				UOptimusNode* WriteNode = WriteNodes[0];
				UOptimusNode* BasePositionResourceNode = nullptr;
				UOptimusNode* CohesionAPositionResourceNode = nullptr;
				UOptimusNode* CohesionBPositionResourceNode = nullptr;
				UOptimusNode* WitnessPositionResourceNode = nullptr;
				for (UOptimusNode* ResourceNode : ResourceNodes)
				{
					if (ResourceNode && ResourceNode->FindPin(TEXT("GetBaseCorrectedPosition")))
					{
						BasePositionResourceNode = ResourceNode;
					}
					else if (ResourceNode && ResourceNode->FindPin(TEXT("GetCohesionACorrectedPosition")))
					{
						CohesionAPositionResourceNode = ResourceNode;
					}
					else if (ResourceNode && ResourceNode->FindPin(TEXT("GetCohesionBCorrectedPosition")))
					{
						CohesionBPositionResourceNode = ResourceNode;
					}
					else if (ResourceNode && ResourceNode->FindPin(TEXT("GetWitnessCorrectedPosition")))
					{
						WitnessPositionResourceNode = ResourceNode;
					}
				}
				UOptimusNode* PrimaryBindingNode = nullptr;
				UOptimusNode* BodyBindingNode = nullptr;
				for (UOptimusNode* ComponentNode : ComponentNodes)
				{
					if (GetNodeComponentBinding(ComponentNode) == PrimaryBinding)
					{
						PrimaryBindingNode = ComponentNode;
					}
					else if (GetNodeComponentBinding(ComponentNode) == BodyBinding)
					{
						BodyBindingNode = ComponentNode;
					}
				}
				UOptimusNode* GarmentReadNode = nullptr;
				UOptimusNode* BodyReadNode = nullptr;
				for (UOptimusNode* ReadNode : ReadNodes)
				{
					if (PrimaryBindingNode && ArePinsDirectlyLinked(PrimaryBindingNode, SkeletalComponentPin, ReadNode, SkinnedComponentPin))
					{
						GarmentReadNode = ReadNode;
					}
					else if (BodyBindingNode && ArePinsDirectlyLinked(BodyBindingNode, SkeletalComponentPin, ReadNode, SkinnedComponentPin))
					{
						BodyReadNode = ReadNode;
					}
				}

				if (!PrimaryBindingNode
					|| !BodyBindingNode
					|| !GarmentReadNode
					|| !BodyReadNode
					|| !BaseKernelNode
					|| !BasePositionResourceNode
					|| !CohesionAKernelNode
					|| !CohesionAPositionResourceNode
					|| !CohesionBKernelNode
					|| !CohesionBPositionResourceNode
					|| !WitnessKernelNode
					|| !WitnessPositionResourceNode
					|| !FinalizeKernelNode)
				{
					Errors.Add(TEXT("Explicit Garment/Body routing or named five-kernel chain is incomplete."));
				}
				else
				{
					const bool bCoreLinksValid =
						ArePinsDirectlyLinked(PrimaryBindingNode, SkeletalComponentPin, WriteNode, SkinnedComponentPin)
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("Position"), BaseKernelNode, TEXT("Primary Group.GarmentPosition"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("TangentX"), BaseKernelNode, TEXT("Primary Group.GarmentTangentX"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("TangentZ"), BaseKernelNode, TEXT("Primary Group.GarmentTangentZ"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), BaseKernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(BaseKernelNode, TEXT("CorrectedPosition"), BasePositionResourceNode, TEXT("SetBaseCorrectedPosition"))
						&& ArePinsDirectlyLinked(BasePositionResourceNode, TEXT("GetBaseCorrectedPosition"), CohesionAKernelNode, TEXT("Primary Group.RawCorrectedPosition"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("Position"), CohesionAKernelNode, TEXT("Primary Group.UpstreamGarmentPosition"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), CohesionAKernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(CohesionAKernelNode, TEXT("CohesivePosition"), CohesionAPositionResourceNode, TEXT("SetCohesionACorrectedPosition"))
						&& ArePinsDirectlyLinked(CohesionAPositionResourceNode, TEXT("GetCohesionACorrectedPosition"), CohesionBKernelNode, TEXT("Primary Group.RawCorrectedPosition"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("Position"), CohesionBKernelNode, TEXT("Primary Group.UpstreamGarmentPosition"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), CohesionBKernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(CohesionBKernelNode, TEXT("CohesivePosition"), CohesionBPositionResourceNode, TEXT("SetCohesionBCorrectedPosition"))
						&& ArePinsDirectlyLinked(CohesionBPositionResourceNode, TEXT("GetCohesionBCorrectedPosition"), WitnessKernelNode, TEXT("Primary Group.BaseCorrectedPosition"))
						&& ArePinsDirectlyLinked(BaseKernelNode, TEXT("PreservedTangentX"), WitnessKernelNode, TEXT("Primary Group.BaseTangentX"))
						&& ArePinsDirectlyLinked(BaseKernelNode, TEXT("PreservedTangentZ"), WitnessKernelNode, TEXT("Primary Group.BaseTangentZ"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), WitnessKernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(WitnessKernelNode, TEXT("FinalPosition"), WitnessPositionResourceNode, TEXT("SetWitnessCorrectedPosition"))
						&& ArePinsDirectlyLinked(WitnessPositionResourceNode, TEXT("GetWitnessCorrectedPosition"), FinalizeKernelNode, TEXT("Primary Group.WitnessPosition"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("Position"), FinalizeKernelNode, TEXT("Primary Group.UpstreamGarmentPosition"))
						&& ArePinsDirectlyLinked(WitnessKernelNode, TEXT("FinalTangentX"), FinalizeKernelNode, TEXT("Primary Group.WitnessTangentX"))
						&& ArePinsDirectlyLinked(WitnessKernelNode, TEXT("FinalTangentZ"), FinalizeKernelNode, TEXT("Primary Group.WitnessTangentZ"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), FinalizeKernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(FinalizeKernelNode, TEXT("FinalPosition"), WriteNode, TEXT("Position"))
						&& ArePinsDirectlyLinked(FinalizeKernelNode, TEXT("FinalTangentX"), WriteNode, TEXT("TangentX"))
						&& ArePinsDirectlyLinked(FinalizeKernelNode, TEXT("FinalTangentZ"), WriteNode, TEXT("TangentZ"));
					if (!bCoreLinksValid)
					{
						Errors.Add(TEXT("Core Garment/Body/base/cohesion-A/cohesion-B/witness/finalize/write links do not match schema 35."));
					}
				}

				const FOptimusDataDomain SingletonDomain;
				TArray<FOptimusParameterBinding> ExpectedBaseInputs;
				ExpectedBaseInputs.Add(MakeBinding(TEXT("GarmentPosition"), ResolveVector3Type(), VertexDomain));
				ExpectedBaseInputs.Add(MakeBinding(TEXT("GarmentTangentX"), ResolveVector4Type(), VertexDomain));
				ExpectedBaseInputs.Add(MakeBinding(TEXT("GarmentTangentZ"), ResolveVector4Type(), VertexDomain));
				for (const FVariableSpec& Spec : GetVariableSpecs())
				{
					ExpectedBaseInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
				}
				const TArray<FOptimusParameterBinding> ExpectedBaseOutputs =
				{
					MakeBinding(TEXT("CorrectedPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("PreservedTangentX"), ResolveVector4Type(), VertexDomain),
					MakeBinding(TEXT("PreservedTangentZ"), ResolveVector4Type(), VertexDomain)
				};
				TArray<FOptimusParameterBinding> ExpectedCohesionInputs =
				{
					MakeBinding(TEXT("RawCorrectedPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("UpstreamGarmentPosition"), ResolveVector3Type(), VertexDomain)
				};
				for (const FVariableSpec& Spec : GetVariableSpecs())
				{
					ExpectedCohesionInputs.Add(
						MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
				}
				const TArray<FOptimusParameterBinding> ExpectedCohesionOutputs =
				{
					MakeBinding(TEXT("CohesivePosition"), ResolveVector3Type(), VertexDomain)
				};
				TArray<FOptimusParameterBinding> ExpectedWitnessInputs =
				{
					MakeBinding(TEXT("BaseCorrectedPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("BaseTangentX"), ResolveVector4Type(), VertexDomain),
					MakeBinding(TEXT("BaseTangentZ"), ResolveVector4Type(), VertexDomain)
				};
				for (const FVariableSpec& Spec : GetVariableSpecs())
				{
					ExpectedWitnessInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
				}
				const TArray<FOptimusParameterBinding> ExpectedWitnessOutputs =
				{
					MakeBinding(TEXT("FinalPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("FinalTangentX"), ResolveVector4Type(), VertexDomain),
					MakeBinding(TEXT("FinalTangentZ"), ResolveVector4Type(), VertexDomain)
				};
				TArray<FOptimusParameterBinding> ExpectedFinalizeInputs =
				{
					MakeBinding(TEXT("WitnessPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("UpstreamGarmentPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("WitnessTangentX"), ResolveVector4Type(), VertexDomain),
					MakeBinding(TEXT("WitnessTangentZ"), ResolveVector4Type(), VertexDomain)
				};
				for (const FVariableSpec& Spec : GetVariableSpecs())
				{
					ExpectedFinalizeInputs.Add(
						MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
				}
				const TArray<FOptimusParameterBinding> ExpectedFinalizeOutputs =
				{
					MakeBinding(TEXT("FinalPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("FinalTangentX"), ResolveVector4Type(), VertexDomain),
					MakeBinding(TEXT("FinalTangentZ"), ResolveVector4Type(), VertexDomain)
				};
				if (!ValidateBindingArray(BaseKernelNode, TEXT("InputBindingArray"), ExpectedBaseInputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(BaseKernelNode, TEXT("OutputBindingArray"), ExpectedBaseOutputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateSecondaryBodyBindings(BaseKernelNode, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(CohesionAKernelNode, TEXT("InputBindingArray"), ExpectedCohesionInputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(CohesionAKernelNode, TEXT("OutputBindingArray"), ExpectedCohesionOutputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateSecondaryBodyBindings(CohesionAKernelNode, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(CohesionBKernelNode, TEXT("InputBindingArray"), ExpectedCohesionInputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(CohesionBKernelNode, TEXT("OutputBindingArray"), ExpectedCohesionOutputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateSecondaryBodyBindings(CohesionBKernelNode, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(WitnessKernelNode, TEXT("InputBindingArray"), ExpectedWitnessInputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(WitnessKernelNode, TEXT("OutputBindingArray"), ExpectedWitnessOutputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateSecondaryBodyBindings(WitnessKernelNode, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(FinalizeKernelNode, TEXT("InputBindingArray"), ExpectedFinalizeInputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(FinalizeKernelNode, TEXT("OutputBindingArray"), ExpectedFinalizeOutputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateSecondaryBodyBindings(FinalizeKernelNode, Error))
				{
					Errors.Add(Error);
				}

				const IOptimusShaderTextProvider* BaseShaderTextProvider = Cast<IOptimusShaderTextProvider>(BaseKernelNode);
				const IOptimusShaderTextProvider* CohesionAShaderTextProvider = Cast<IOptimusShaderTextProvider>(CohesionAKernelNode);
				const IOptimusShaderTextProvider* CohesionBShaderTextProvider = Cast<IOptimusShaderTextProvider>(CohesionBKernelNode);
				const IOptimusShaderTextProvider* WitnessShaderTextProvider = Cast<IOptimusShaderTextProvider>(WitnessKernelNode);
				const IOptimusShaderTextProvider* FinalizeShaderTextProvider = Cast<IOptimusShaderTextProvider>(FinalizeKernelNode);
				if (!BaseShaderTextProvider || BaseShaderTextProvider->GetShaderText() != GetBaseKernelSource())
				{
						Errors.Add(TEXT("Base kernel source differs from the generated schema-35 source."));
				}
				if (!CohesionAShaderTextProvider
					|| CohesionAShaderTextProvider->GetShaderText() != GetCohesionKernelSource())
				{
						Errors.Add(TEXT("Cohesion-A kernel source differs from the generated schema-35 source."));
				}
				if (!CohesionBShaderTextProvider
					|| CohesionBShaderTextProvider->GetShaderText() != GetCohesionKernelSource())
				{
						Errors.Add(TEXT("Cohesion-B kernel source differs from the generated schema-35 source."));
				}
				if (!WitnessShaderTextProvider || WitnessShaderTextProvider->GetShaderText() != GetWitnessKernelSource())
				{
						Errors.Add(TEXT("Witness kernel source differs from the generated schema-35 source."));
				}
				if (!FinalizeShaderTextProvider || FinalizeShaderTextProvider->GetShaderText() != GetFinalizeKernelSource())
				{
						Errors.Add(TEXT("Seam/tangent finalize kernel source differs from the generated schema-35 source."));
				}

				for (const FVariableSpec& Spec : GetVariableSpecs())
				{
					UOptimusNode** VariableNode = VariableNodes.FindByPredicate(
						[&Spec](UOptimusNode* Node)
						{
							return Node && Node->FindPin(Spec.Name.ToString()) != nullptr;
						});
					const FString KernelPinPath = FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString());
					if (!VariableNode || !*VariableNode
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), BaseKernelNode, KernelPinPath)
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), CohesionAKernelNode, KernelPinPath)
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), CohesionBKernelNode, KernelPinPath)
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), WitnessKernelNode, KernelPinPath)
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), FinalizeKernelNode, KernelPinPath))
					{
						Errors.Add(FString::Printf(TEXT("Variable node/five-kernel links %s are missing."), *Spec.Name.ToString()));
					}
				}
			}
		}

		if (bRequireMetadata)
		{
			FMetaData& MetaData = Deformer->GetOutermost()->GetMetaData();
			const FString SchemaValue = MetaData.GetValue(Deformer, GraphSchemaMetadataKey);
			const FString SemanticValue = MetaData.GetValue(Deformer, PrimarySemanticMetadataKey);
			if (SchemaValue != GraphSchemaVersion || SemanticValue != PrimarySemantic)
			{
				Errors.Add(TEXT("Generated graph metadata/schema marker is missing or stale."));
			}
		}

		if (!Errors.IsEmpty())
		{
			OutReport = FString::Join(Errors, TEXT("\n"));
			return false;
		}

		OutReport = FString::Printf(
			TEXT("PASS: %s; Primary=Garment, Secondary=Body, %d variables, %d nodes, BodyToGarment transform, five-pass base/two-stage cohesion/witness/seam-tangent constraint validated."),
			AssetObjectPath,
			GetVariableSpecs().Num(),
			2 + 3 + 5 + 4 + GetVariableSpecs().Num());
		return true;
	}

	FString FormatDiagnostic(const FOptimusCompilerDiagnostic& Diagnostic)
	{
		const TCHAR* Level = TEXT("Info");
		switch (Diagnostic.Level)
		{
		case EOptimusDiagnosticLevel::Warning:
			Level = TEXT("Warning");
			break;
		case EOptimusDiagnosticLevel::Error:
			Level = TEXT("Error");
			break;
		default:
			break;
		}
		return FString::Printf(
			TEXT("[%s] %s%s%s"),
			Level,
			*Diagnostic.Message.ToString(),
			Diagnostic.Object.IsValid() ? TEXT(" @ ") : TEXT(""),
			Diagnostic.Object.IsValid() ? *Diagnostic.Object->GetPathName() : TEXT(""));
	}

	bool CompileDeformer(UOptimusDeformer* Deformer, FString& OutDiagnostics)
	{
		TArray<FString> Diagnostics;
		const FDelegateHandle DiagnosticHandle = Deformer->GetCompileMessageDelegate().AddLambda(
			[&Diagnostics](const FOptimusCompilerDiagnostic& Diagnostic)
			{
				Diagnostics.Add(FormatDiagnostic(Diagnostic));
			});
		const bool bCompiled = Deformer->Compile();
		Deformer->GetCompileMessageDelegate().Remove(DiagnosticHandle);

		const TCHAR* Status = TEXT("Modified");
		switch (Deformer->GetStatus())
		{
		case EOptimusDeformerStatus::Compiled:
			Status = TEXT("Compiled");
			break;
		case EOptimusDeformerStatus::CompiledWithWarnings:
			Status = TEXT("CompiledWithWarnings");
			break;
		case EOptimusDeformerStatus::HasErrors:
			Status = TEXT("HasErrors");
			break;
		default:
			break;
		}
		Diagnostics.Insert(FString::Printf(TEXT("Optimus status: %s."), Status), 0);
		OutDiagnostics = FString::Join(Diagnostics, TEXT("\n"));
		return bCompiled && Deformer->GetStatus() != EOptimusDeformerStatus::HasErrors;
	}

	bool SaveDeformer(UOptimusDeformer* Deformer, FString& OutError)
	{
		UPackage* Package = Deformer ? Deformer->GetOutermost() : nullptr;
		if (!Package)
		{
			OutError = TEXT("Cannot save a null deformer package.");
			return false;
		}

		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			AssetPackagePath,
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Package, Deformer, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Failed to save %s."), *Filename);
			return false;
		}
		return true;
	}
}

FEFClothingSurfaceDeformerBuildResult
UEFClothingSurfaceDeformerBuilderLibrary::BuildOrUpdateSurfaceConstraintDeformer(const bool bForceRebuild)
{
	using namespace EFClothingSurfaceDeformerBuilder;
	FEFClothingSurfaceDeformerBuildResult Result;
	Result.DeformerAsset = FSoftObjectPath(AssetObjectPath);

	UObject* ExistingObject = LoadObject<UObject>(nullptr, AssetObjectPath);
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(ExistingObject);
	if (ExistingObject && !Deformer)
	{
		Result.Report = FString::Printf(
			TEXT("FAIL: %s exists but is %s, not UOptimusDeformer. No asset was overwritten."),
			AssetObjectPath,
			*ExistingObject->GetClass()->GetPathName());
		return Result;
	}

	bool bNewAsset = false;
	if (!Deformer)
	{
		UPackage* Package = CreatePackage(AssetPackagePath);
		Deformer = NewObject<UOptimusDeformer>(
			Package,
			UOptimusDeformer::StaticClass(),
			AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		bNewAsset = Deformer != nullptr;
		if (!Deformer)
		{
			Result.Report = FString::Printf(TEXT("FAIL: could not create %s."), AssetObjectPath);
			return Result;
		}
	}

	FString StructuralReport;
	const bool bAlreadyCurrent = !bForceRebuild && ValidateGraph(Deformer, true, StructuralReport);
	if (!bAlreadyCurrent)
	{
		FString RebuildError;
		if (!RebuildGraph(Deformer, RebuildError))
		{
			Result.Report = FString::Printf(
				TEXT("FAIL: graph rebuild aborted before save. %s"),
				*RebuildError);
			return Result;
		}
		Result.bRebuilt = true;
	}

	FString PreCompileValidation;
	if (!ValidateGraph(Deformer, false, PreCompileValidation))
	{
		Result.Report = FString::Printf(TEXT("FAIL: generated graph is structurally invalid.\n%s"), *PreCompileValidation);
		return Result;
	}

	FString CompileDiagnostics;
	if (!CompileDeformer(Deformer, CompileDiagnostics))
	{
		Result.Report = FString::Printf(TEXT("FAIL: Optimus compile failed.\n%s"), *CompileDiagnostics);
		return Result;
	}

	FMetaData& MetaData = Deformer->GetOutermost()->GetMetaData();
	MetaData.SetValue(Deformer, GraphSchemaMetadataKey, GraphSchemaVersion);
	MetaData.SetValue(Deformer, PrimarySemanticMetadataKey, PrimarySemantic);

	FString FinalValidation;
	if (!ValidateGraph(Deformer, true, FinalValidation))
	{
		Result.Report = FString::Printf(TEXT("FAIL: post-compile graph validation failed.\n%s"), *FinalValidation);
		return Result;
	}

	if (bNewAsset)
	{
		FAssetRegistryModule::AssetCreated(Deformer);
	}
	FString SaveError;
	if (!SaveDeformer(Deformer, SaveError))
	{
		Result.Report = FString::Printf(TEXT("FAIL: compiled graph was not saved. %s"), *SaveError);
		return Result;
	}

	Result.bSuccess = true;
	Result.Report = FString::Printf(
		TEXT("PASS: %s\n%s\n%s\nRebuilt=%s; saved atomically after successful validation/compile."),
		AssetObjectPath,
		*FinalValidation,
		*CompileDiagnostics,
		Result.bRebuilt ? TEXT("true") : TEXT("false"));
	UE_LOG(LogEFClothingSurfaceDeformerBuilder, Display, TEXT("%s"), *Result.Report);
	return Result;
}

FEFClothingSurfaceDeformerBuildResult
UEFClothingSurfaceDeformerBuilderLibrary::ValidateSurfaceConstraintDeformer()
{
	using namespace EFClothingSurfaceDeformerBuilder;
	FEFClothingSurfaceDeformerBuildResult Result;
	Result.DeformerAsset = FSoftObjectPath(AssetObjectPath);
	UOptimusDeformer* Deformer = LoadObject<UOptimusDeformer>(nullptr, AssetObjectPath);
	if (!Deformer)
	{
		Result.Report = FString::Printf(TEXT("FAIL: %s does not exist or is not a UOptimusDeformer."), AssetObjectPath);
		return Result;
	}

	FString ValidationReport;
	Result.bSuccess = ValidateGraph(Deformer, true, ValidationReport);
	const FString StatusReport = FString::Printf(TEXT("Optimus serialized status: %d."), static_cast<int32>(Deformer->GetStatus()));
	Result.Report = FString::Printf(TEXT("%s\n%s"), *ValidationReport, *StatusReport);
	return Result;
}
#include "EFClothingBodyCoverageBuilder.inl"
