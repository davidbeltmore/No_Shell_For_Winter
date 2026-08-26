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
	constexpr TCHAR GraphSchemaVersion[] = TEXT("26.1");
	constexpr TCHAR PrimarySemantic[] = TEXT("Garment");

	constexpr TCHAR CustomKernelClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_CustomComputeKernel");
	constexpr TCHAR ReadSkinnedMeshClassPath[] = TEXT("/Script/OptimusCore.OptimusSkinnedMeshReadDataInterface");
	constexpr TCHAR WriteSkinnedMeshClassPath[] = TEXT("/Script/OptimusCore.OptimusSkinnedMeshWriteDataInterface");
	constexpr TCHAR SkeletalMeshComponentSourceClassPath[] = TEXT("/Script/OptimusCore.OptimusSkeletalMeshComponentSource");
	constexpr TCHAR ComponentSourceNodeClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_ComponentSource");
	constexpr TCHAR VariableGetNodeClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_GetVariable");

	constexpr TCHAR KernelName[] = TEXT("EF_GarmentSurfaceConstraint");
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
		Int4Array,
		Float,
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
			{ TEXT("EF_GarmentLODIndex"), EVariableType::Int, true },
			{ TEXT("EF_BodyLODIndex"), EVariableType::Int, true },
			{ TEXT("EF_BodyTriangleAndMode"), EVariableType::Int4Array },
			{ TEXT("EF_BarycentricsAndFollowWeight"), EVariableType::Float4Array },
			{ TEXT("EF_RestOffsetAndClearanceCm"), EVariableType::Float4Array },
			{ TEXT("EF_MaximumCorrectionAndRestGapCm"), EVariableType::Float2Array },
			{ TEXT("EF_GlobalClearanceOffsetCm"), EVariableType::Float },
			{ TEXT("EF_GarmentClearanceOffsetCm"), EVariableType::Float },
			{ TEXT("EF_MaximumCorrectionOverrideCm"), EVariableType::Float },
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
		case EVariableType::Int4Array:
			Handle = Registry.FindArrayType(TBaseStructure<FIntVector4>::Get());
			break;
		case EVariableType::Float:
			// Optimus variables use UE double storage and convert it to HLSL float.
			Handle = Registry.FindType(*FDoubleProperty::StaticClass());
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

	const FString& GetKernelSource()
	{
		static const FString Source = TEXT(R"EFHLSL(
KERNEL
{
	float3 GarmentPosition = ReadGarmentPosition(Index);
	float4 GarmentTangentX = ReadGarmentTangentX(Index);
	float4 GarmentTangentZ = ReadGarmentTangentZ(Index);

	float3 GarmentNormal = GarmentTangentZ.xyz;
	float GarmentNormalLengthSquared = dot(GarmentNormal, GarmentNormal);
	GarmentNormal = GarmentNormalLengthSquared > 1.0e-12f
		? GarmentNormal * rsqrt(GarmentNormalLengthSquared)
		: float3(0.0f, 0.0f, 1.0f);

	float RuntimeOffsetCm = ReadEF_GlobalClearanceOffsetCm()
		+ ReadEF_GarmentClearanceOffsetCm();
	RuntimeOffsetCm = isfinite(RuntimeOffsetCm) ? RuntimeOffsetCm : 0.0f;
	float CorrectionOverrideCm = ReadEF_MaximumCorrectionOverrideCm();
	CorrectionOverrideCm = isfinite(CorrectionOverrideCm)
		? max(CorrectionOverrideCm, 0.0f)
		: 0.0f;
	float ConservativePushCm = CorrectionOverrideCm > 0.0f
		? CorrectionOverrideCm
		: max(2.0f, 0.55f + RuntimeOffsetCm);
	float3 ConservativePosition = GarmentPosition + GarmentNormal * ConservativePushCm;

	if (!all(isfinite(GarmentPosition))
		|| !all(isfinite(GarmentTangentX))
		|| !all(isfinite(GarmentTangentZ)))
	{
		WriteCorrectedPosition(Index, float3(0.0f, 0.0f, 0.0f));
		WritePreservedTangentX(Index, float4(1.0f, 0.0f, 0.0f, 1.0f));
		WritePreservedTangentZ(Index, float4(0.0f, 0.0f, 1.0f, 1.0f));
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

	StructuredBuffer<int4> BodyTriangleAndModeBuffer = ReadEF_BodyTriangleAndMode();
	StructuredBuffer<float4> BarycentricsAndFollowWeightBuffer = ReadEF_BarycentricsAndFollowWeight();
	StructuredBuffer<float4> RestOffsetAndClearanceCmBuffer = ReadEF_RestOffsetAndClearanceCm();
	StructuredBuffer<float2> MaximumCorrectionAndRestGapCmBuffer = ReadEF_MaximumCorrectionAndRestGapCm();

	uint TriangleBufferCount = 0;
	uint BarycentricBufferCount = 0;
	uint RestOffsetBufferCount = 0;
	uint LimitBufferCount = 0;
	uint IgnoredStride = 0;
	BodyTriangleAndModeBuffer.GetDimensions(TriangleBufferCount, IgnoredStride);
	BarycentricsAndFollowWeightBuffer.GetDimensions(BarycentricBufferCount, IgnoredStride);
	RestOffsetAndClearanceCmBuffer.GetDimensions(RestOffsetBufferCount, IgnoredStride);
	MaximumCorrectionAndRestGapCmBuffer.GetDimensions(LimitBufferCount, IgnoredStride);
	if (Index >= TriangleBufferCount
		|| Index >= BarycentricBufferCount
		|| Index >= RestOffsetBufferCount
		|| Index >= LimitBufferCount)
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

	uint BodyVertexCount = Body::NumVertex();
	if (any(BodyTriangleAndMode.xyz < 0)
		|| any((uint3)BodyTriangleAndMode.xyz >= BodyVertexCount)
		|| BodyTriangleAndMode.w < 0
		|| BodyTriangleAndMode.w > 2
		|| !all(isfinite(BarycentricsAndFollowWeight))
		|| !all(isfinite(RestOffsetAndClearanceCm))
		|| !all(isfinite(MaximumCorrectionAndRestGapCm)))
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
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
	float Edge01LengthSquared = dot(Edge01, Edge01);
	float3 UnnormalizedNormal = cross(Edge01, Edge02);
	float NormalLengthSquared = dot(UnnormalizedNormal, UnnormalizedNormal);
	if (Edge01LengthSquared <= 1.0e-12f || NormalLengthSquared <= 1.0e-12f)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	float3 SurfaceTangent = Edge01 * rsqrt(Edge01LengthSquared);
	float3 SurfaceNormal = UnnormalizedNormal * rsqrt(NormalLengthSquared);
	float3 SurfaceBitangent = normalize(cross(SurfaceNormal, SurfaceTangent));
	// Match the compiler's final Gram-Schmidt step exactly: T = (N x T) x N.
	SurfaceTangent = normalize(cross(SurfaceBitangent, SurfaceNormal));
	float3 SurfaceAnchor = BodyP0 * Barycentrics.x
		+ BodyP1 * Barycentrics.y
		+ BodyP2 * Barycentrics.z;
	float3 SurfaceTarget = SurfaceAnchor
		+ SurfaceTangent * RestOffsetAndClearanceCm.x
		+ SurfaceBitangent * RestOffsetAndClearanceCm.y
		+ SurfaceNormal * RestOffsetAndClearanceCm.z;

	int SurfaceMode = BodyTriangleAndMode.w;
	float FollowWeight = SurfaceMode == 2
		? 0.0f
		: saturate(BarycentricsAndFollowWeight.w);
	float3 FollowDelta = (SurfaceTarget - GarmentPosition) * FollowWeight;
	// Surface following may transport tangentially or outward, never toward skin.
	FollowDelta -= SurfaceNormal * min(dot(FollowDelta, SurfaceNormal), 0.0f);
	float3 CandidatePosition = GarmentPosition + FollowDelta;

	float BaseTargetGapCm = max(RestOffsetAndClearanceCm.w, 0.0f);
	// Only SurfaceFollow preserves a larger compiled rest gap.
	if (SurfaceMode == 0)
	{
		BaseTargetGapCm = max(BaseTargetGapCm, MaximumCorrectionAndRestGapCm.y);
	}
	float TargetGapCm = max(BaseTargetGapCm + RuntimeOffsetCm, 0.0f);
	float SignedGapCm = dot(CandidatePosition - SurfaceAnchor, SurfaceNormal);
	float RequiredPushCm = max(TargetGapCm - SignedGapCm, 0.0f);
	float CompiledMaximumCorrectionCm = max(MaximumCorrectionAndRestGapCm.x, 0.0f);
	float MaximumCorrectionCm = CorrectionOverrideCm > 0.0f
		? CorrectionOverrideCm
		: CompiledMaximumCorrectionCm;
	if (MaximumCorrectionCm <= 0.0f)
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}

	float AppliedPushCm = min(RequiredPushCm, MaximumCorrectionCm);
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

		UOptimusNode* PrimaryBindingNode = Graph->AddComponentBindingGetNode(PrimaryBinding, FVector2D(-1320.0, -220.0));
		UOptimusNode* BodyBindingNode = Graph->AddComponentBindingGetNode(BodyBinding, FVector2D(-1320.0, 520.0));
		UOptimusNode* GarmentReadNode = Graph->AddDataInterfaceNode(ReadClass, FVector2D(-980.0, -260.0));
		UOptimusNode* BodyReadNode = Graph->AddDataInterfaceNode(ReadClass, FVector2D(-980.0, 520.0));
		UOptimusNode* WriteNode = Graph->AddDataInterfaceNode(WriteClass, FVector2D(860.0, -120.0));
		UOptimusNode* KernelNode = Graph->AddNode(KernelClass, FVector2D(300.0, -100.0));
		if (!PrimaryBindingNode || !BodyBindingNode || !GarmentReadNode || !BodyReadNode || !WriteNode || !KernelNode)
		{
			OutError = TEXT("Failed to create one or more required Optimus nodes.");
			return false;
		}

		if (!SetValidatedNameProperty(KernelNode, TEXT("KernelName"), KernelName, OutError)
			|| !SetExecutionDomain(KernelNode, OutError)
			|| !SetGroupSize(KernelNode, OutError))
		{
			return false;
		}

		const FOptimusDataDomain VertexDomain(TArray<FName>{ Optimus::DomainName::Vertex });
		const FOptimusDataDomain SingletonDomain;
		TArray<FOptimusParameterBinding> PrimaryInputs;
		PrimaryInputs.Add(MakeBinding(TEXT("GarmentPosition"), ResolveVector3Type(), VertexDomain));
		PrimaryInputs.Add(MakeBinding(TEXT("GarmentTangentX"), ResolveVector4Type(), VertexDomain));
		PrimaryInputs.Add(MakeBinding(TEXT("GarmentTangentZ"), ResolveVector4Type(), VertexDomain));
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			PrimaryInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
		}

		const TArray<FOptimusParameterBinding> Outputs =
		{
			MakeBinding(TEXT("CorrectedPosition"), ResolveVector3Type(), VertexDomain),
			MakeBinding(TEXT("PreservedTangentX"), ResolveVector4Type(), VertexDomain),
			MakeBinding(TEXT("PreservedTangentZ"), ResolveVector4Type(), VertexDomain)
		};
		const TArray<FOptimusParameterBinding> BodyInputs =
		{
			MakeBinding(TEXT("BodyPosition"), ResolveVector3Type(), VertexDomain)
		};
		if (!SetParameterBindingArray(KernelNode, TEXT("InputBindingArray"), PrimaryInputs, OutError)
			|| !SetParameterBindingArray(KernelNode, TEXT("OutputBindingArray"), Outputs, OutError)
			|| !SetSecondaryBodyBindings(KernelNode, BodyInputs, OutError))
		{
			return false;
		}

		IOptimusShaderTextProvider* ShaderTextProvider = Cast<IOptimusShaderTextProvider>(KernelNode);
		if (!ShaderTextProvider)
		{
			OutError = TEXT("Reflected custom kernel does not implement IOptimusShaderTextProvider.");
			return false;
		}
		ShaderTextProvider->SetShaderText(GetKernelSource());

		if (!AddRequiredLink(Graph, PrimaryBindingNode, SkeletalComponentPin, GarmentReadNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, PrimaryBindingNode, SkeletalComponentPin, WriteNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, BodyBindingNode, SkeletalComponentPin, BodyReadNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("Position"), KernelNode, TEXT("Primary Group.GarmentPosition"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("TangentX"), KernelNode, TEXT("Primary Group.GarmentTangentX"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("TangentZ"), KernelNode, TEXT("Primary Group.GarmentTangentZ"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), KernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, KernelNode, TEXT("CorrectedPosition"), WriteNode, TEXT("Position"), OutError)
			|| !AddRequiredLink(Graph, KernelNode, TEXT("PreservedTangentX"), WriteNode, TEXT("TangentX"), OutError)
			|| !AddRequiredLink(Graph, KernelNode, TEXT("PreservedTangentZ"), WriteNode, TEXT("TangentZ"), OutError))
		{
			return false;
		}

		int32 VariableNodeIndex = 0;
		for (const FVariableSpec& Spec : GetVariableSpecs())
		{
			UOptimusVariableDescription* const* Variable = Variables.Find(Spec.Name);
			UOptimusNode* VariableNode = Variable
				? Graph->AddVariableGetNode(*Variable, FVector2D(-640.0, -520.0 + VariableNodeIndex * 90.0))
				: nullptr;
			if (!VariableNode
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					KernelNode,
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
					TEXT("Kernel binding %s[%d] does not match V26 schema (%s)."),
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
			OutError = TEXT("Secondary group must be Body with one BodyPosition float3/Vertex binding.");
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
			const int32 ExpectedNodeCount = 2 + 3 + 1 + GetVariableSpecs().Num();
			if (Nodes.Num() != ExpectedNodeCount)
			{
				Errors.Add(FString::Printf(TEXT("Node count is %d, expected %d."), Nodes.Num(), ExpectedNodeCount));
			}

			TArray<UOptimusNode*> KernelNodes;
			TArray<UOptimusNode*> ReadNodes;
			TArray<UOptimusNode*> WriteNodes;
			TArray<UOptimusNode*> ComponentNodes;
			TArray<UOptimusNode*> VariableNodes;
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
			}

			if (KernelNodes.Num() != 1 || ReadNodes.Num() != 2 || WriteNodes.Num() != 1
				|| ComponentNodes.Num() != 2 || VariableNodes.Num() != GetVariableSpecs().Num())
			{
				Errors.Add(TEXT("Required kernel/read/write/component/variable node cardinality does not match V26."));
			}
			else
			{
				UOptimusNode* KernelNode = KernelNodes[0];
				UOptimusNode* WriteNode = WriteNodes[0];
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

				if (!PrimaryBindingNode || !BodyBindingNode || !GarmentReadNode || !BodyReadNode)
				{
					Errors.Add(TEXT("Explicit Garment/Body component routing is incomplete."));
				}
				else
				{
					const bool bCoreLinksValid =
						ArePinsDirectlyLinked(PrimaryBindingNode, SkeletalComponentPin, WriteNode, SkinnedComponentPin)
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("Position"), KernelNode, TEXT("Primary Group.GarmentPosition"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("TangentX"), KernelNode, TEXT("Primary Group.GarmentTangentX"))
						&& ArePinsDirectlyLinked(GarmentReadNode, TEXT("TangentZ"), KernelNode, TEXT("Primary Group.GarmentTangentZ"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), KernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(KernelNode, TEXT("CorrectedPosition"), WriteNode, TEXT("Position"))
						&& ArePinsDirectlyLinked(KernelNode, TEXT("PreservedTangentX"), WriteNode, TEXT("TangentX"))
						&& ArePinsDirectlyLinked(KernelNode, TEXT("PreservedTangentZ"), WriteNode, TEXT("TangentZ"));
					if (!bCoreLinksValid)
					{
						Errors.Add(TEXT("Core Garment/Body/kernel/write links do not match V26."));
					}
				}

				const FOptimusDataDomain VertexDomain(TArray<FName>{ Optimus::DomainName::Vertex });
				const FOptimusDataDomain SingletonDomain;
				TArray<FOptimusParameterBinding> ExpectedInputs;
				ExpectedInputs.Add(MakeBinding(TEXT("GarmentPosition"), ResolveVector3Type(), VertexDomain));
				ExpectedInputs.Add(MakeBinding(TEXT("GarmentTangentX"), ResolveVector4Type(), VertexDomain));
				ExpectedInputs.Add(MakeBinding(TEXT("GarmentTangentZ"), ResolveVector4Type(), VertexDomain));
				for (const FVariableSpec& Spec : GetVariableSpecs())
				{
					ExpectedInputs.Add(MakeBinding(Spec.Name, ResolveVariableType(Spec.Type), SingletonDomain));
				}
				const TArray<FOptimusParameterBinding> ExpectedOutputs =
				{
					MakeBinding(TEXT("CorrectedPosition"), ResolveVector3Type(), VertexDomain),
					MakeBinding(TEXT("PreservedTangentX"), ResolveVector4Type(), VertexDomain),
					MakeBinding(TEXT("PreservedTangentZ"), ResolveVector4Type(), VertexDomain)
				};
				if (!ValidateBindingArray(KernelNode, TEXT("InputBindingArray"), ExpectedInputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateBindingArray(KernelNode, TEXT("OutputBindingArray"), ExpectedOutputs, Error))
				{
					Errors.Add(Error);
				}
				if (!ValidateSecondaryBodyBindings(KernelNode, Error))
				{
					Errors.Add(Error);
				}

				const IOptimusShaderTextProvider* ShaderTextProvider = Cast<IOptimusShaderTextProvider>(KernelNode);
				if (!ShaderTextProvider || ShaderTextProvider->GetShaderText() != GetKernelSource())
				{
					Errors.Add(TEXT("Kernel source differs from the generated V26 source."));
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
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), KernelNode, KernelPinPath))
					{
						Errors.Add(FString::Printf(TEXT("Variable node/link %s is missing."), *Spec.Name.ToString()));
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
			TEXT("PASS: %s; Primary=Garment, Secondary=Body, %d variables, %d nodes, BodyToGarment transform, unilateral constraint and tangent passthrough validated."),
			AssetObjectPath,
			GetVariableSpecs().Num(),
			2 + 3 + 1 + GetVariableSpecs().Num());
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
