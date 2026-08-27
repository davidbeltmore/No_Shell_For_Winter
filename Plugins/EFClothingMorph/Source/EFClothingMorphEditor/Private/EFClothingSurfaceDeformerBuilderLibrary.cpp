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
	constexpr TCHAR GraphSchemaVersion[] = TEXT("26.5");
	constexpr TCHAR PrimarySemantic[] = TEXT("Garment");

	constexpr TCHAR CustomKernelClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_CustomComputeKernel");
	constexpr TCHAR ReadSkinnedMeshClassPath[] = TEXT("/Script/OptimusCore.OptimusSkinnedMeshReadDataInterface");
	constexpr TCHAR WriteSkinnedMeshClassPath[] = TEXT("/Script/OptimusCore.OptimusSkinnedMeshWriteDataInterface");
	constexpr TCHAR SkeletalMeshComponentSourceClassPath[] = TEXT("/Script/OptimusCore.OptimusSkeletalMeshComponentSource");
	constexpr TCHAR ComponentSourceNodeClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_ComponentSource");
	constexpr TCHAR VariableGetNodeClassPath[] = TEXT("/Script/OptimusCore.OptimusNode_GetVariable");

	constexpr TCHAR BaseKernelName[] = TEXT("EF_GarmentSurfaceBase");
	constexpr TCHAR WitnessKernelName[] = TEXT("EF_GarmentWitnessFinalize");
	constexpr TCHAR BasePositionResourceName[] = TEXT("BaseCorrectedPosition");
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
	float CorrectionOverrideCm = ReadEF_MaximumCorrectionOverrideCm();
	CorrectionOverrideCm = isfinite(CorrectionOverrideCm)
		? max(CorrectionOverrideCm, 0.0f)
		: 0.0f;
	// Imported DAZ garment tangents are not a certified exterior direction at
	// every seam. Invalid binding/buffer data is handled fail-closed by component
	// visibility; this shader fallback must therefore preserve upstream geometry
	// rather than risk applying an inward displacement.
	float3 ConservativePosition = GarmentPosition;

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

	int BodyVertexCountValue = ReadEF_BodyVertexCount();
	uint BodyVertexCount = (uint)max(BodyVertexCountValue, 0);
	if (any(BodyTriangleAndMode.xyz < 0)
		|| BodyVertexCountValue <= 0
		|| any((uint3)BodyTriangleAndMode.xyz >= BodyVertexCount)
		|| BodyTriangleAndMode.w < 0
		|| BodyTriangleAndMode.w > 3
		|| !all(isfinite(BarycentricsAndFollowWeight))
		|| !all(isfinite(RestOffsetAndClearanceCm))
		|| !all(isfinite(MaximumCorrectionAndRestGapCm)))
	{
		WriteCorrectedPosition(Index, ConservativePosition);
		WritePreservedTangentX(Index, GarmentTangentX);
		WritePreservedTangentZ(Index, GarmentTangentZ);
		return;
	}
	if (BodyTriangleAndMode.w == 3)
	{
		// Explicit catalog-derived anatomy exclusion: preserve the complete
		// upstream DAZ/skinning/morph/Chaos result without a late surface push.
		WriteCorrectedPosition(Index, GarmentPosition);
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

	// MaximumCorrectionCm is a certification/diagnostic threshold, never a
	// visual clamp. Clamping here would deliberately leave a penetrated vertex
	// inside the body in the exact frame that needs the strongest correction.
	// The exterior-safe output therefore applies the complete unilateral push;
	// QA/readback reports any threshold excess so the binding can be rebuilt.
	float AppliedPushCm = RequiredPushCm;
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
	// The base pass is already vertex-safe against an explicitly oriented body
	// triangle. Never use the imported garment tangent normal as an emergency
	// direction here: DAZ garments can expose an inward tangent basis at seams,
	// which would turn a fail-safe into a 2.5 cm inward displacement. Until a GPU
	// failure flag hides the component, preserving the certified base result is
	// the only non-regressive fallback for malformed/conflicting witness cones.
	float3 ConservativePosition = BasePosition;

	if (!all(isfinite(BasePosition))
		|| !all(isfinite(BaseTangentX))
		|| !all(isfinite(BaseTangentZ)))
	{
		WriteFinalPosition(Index, float3(0.0f, 0.0f, 0.0f));
		WriteFinalTangentX(Index, float4(1.0f, 0.0f, 0.0f, 1.0f));
		WriteFinalTangentZ(Index, float4(0.0f, 0.0f, 1.0f, 1.0f));
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

	StructuredBuffer<int2> WitnessRangesBuffer = ReadEF_WitnessRanges();
	StructuredBuffer<int> WitnessIndicesBuffer = ReadEF_WitnessIndices();
	StructuredBuffer<int4> PrimaryBodyTriangleAndModeBuffer = ReadEF_BodyTriangleAndMode();
	StructuredBuffer<float4> PrimaryBarycentricsAndFollowWeightBuffer =
		ReadEF_BarycentricsAndFollowWeight();
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
)EFHLSL")) + TEXT(R"EFHLSL(

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
			float TargetClearanceCm =
				max(GarmentBarycentricsAndClearanceCm.w + RuntimeOffsetCm, 0.0f);
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

	// The earlier unrestricted local projection could displace individual vertices
	// by more than 1.6 cm and visibly tear the garment in locomotion. Keep the same
	// unilateral edge/face direction, but expose at most 3.5 millimeters per frame.
	// This is enough to close the small moving border leak while the vertex-safe base
	// pass remains the dominant shape and the excluded crotch stays untouched.
	static const float MaximumVisualEdgeCorrectionCm = 0.35f;
	float ExtraCorrectionLength = length(ExtraCorrection);
	float BoundedCorrectionScale = ExtraCorrectionLength > 1.0e-8f
		? min(1.0f, MaximumVisualEdgeCorrectionCm / ExtraCorrectionLength)
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
		UOptimusResourceDescription* BasePositionResource = Deformer->AddResource(
			ResolveVector3Type(),
			BasePositionResourceName);
		if (!BasePositionResource)
		{
			OutError = TEXT("Failed to create the BaseCorrectedPosition Optimus resource.");
			return false;
		}
		BasePositionResource->ComponentBinding = PrimaryBinding;
		BasePositionResource->Modify();
		if (!Deformer->SetResourceDataDomain(BasePositionResource, VertexDomain, true))
		{
			OutError = TEXT("Failed to bind BaseCorrectedPosition to the Garment vertex domain.");
			return false;
		}

		UOptimusNode* PrimaryBindingNode = Graph->AddComponentBindingGetNode(PrimaryBinding, FVector2D(-1700.0, -300.0));
		UOptimusNode* BodyBindingNode = Graph->AddComponentBindingGetNode(BodyBinding, FVector2D(-1700.0, 620.0));
		UOptimusNode* GarmentReadNode = Graph->AddDataInterfaceNode(ReadClass, FVector2D(-1360.0, -340.0));
		UOptimusNode* BodyReadNode = Graph->AddDataInterfaceNode(ReadClass, FVector2D(-1360.0, 620.0));
		UOptimusNode* BaseKernelNode = Graph->AddNode(KernelClass, FVector2D(-260.0, -220.0));
		UOptimusNode* BasePositionResourceNode = Graph->AddResourceNode(
			BasePositionResource,
			FVector2D(380.0, -360.0));
		UOptimusNode* WitnessKernelNode = Graph->AddNode(KernelClass, FVector2D(760.0, -120.0));
		UOptimusNode* WriteNode = Graph->AddDataInterfaceNode(WriteClass, FVector2D(1420.0, -100.0));
		if (!PrimaryBindingNode
			|| !BodyBindingNode
			|| !GarmentReadNode
			|| !BodyReadNode
			|| !BaseKernelNode
			|| !BasePositionResourceNode
			|| !WitnessKernelNode
			|| !WriteNode)
		{
			OutError = TEXT("Failed to create one or more required Optimus nodes.");
			return false;
		}

		if (!SetValidatedNameProperty(BaseKernelNode, TEXT("KernelName"), BaseKernelName, OutError)
			|| !SetExecutionDomain(BaseKernelNode, OutError)
			|| !SetGroupSize(BaseKernelNode, OutError)
			|| !SetValidatedNameProperty(WitnessKernelNode, TEXT("KernelName"), WitnessKernelName, OutError)
			|| !SetExecutionDomain(WitnessKernelNode, OutError)
			|| !SetGroupSize(WitnessKernelNode, OutError))
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
		const TArray<FOptimusParameterBinding> BodyInputs =
		{
			MakeBinding(TEXT("BodyPosition"), ResolveVector3Type(), VertexDomain)
		};
		if (!SetParameterBindingArray(BaseKernelNode, TEXT("InputBindingArray"), BaseInputs, OutError)
			|| !SetParameterBindingArray(BaseKernelNode, TEXT("OutputBindingArray"), BaseOutputs, OutError)
			|| !SetSecondaryBodyBindings(BaseKernelNode, BodyInputs, OutError)
			|| !SetParameterBindingArray(WitnessKernelNode, TEXT("InputBindingArray"), WitnessInputs, OutError)
			|| !SetParameterBindingArray(WitnessKernelNode, TEXT("OutputBindingArray"), WitnessOutputs, OutError)
			|| !SetSecondaryBodyBindings(WitnessKernelNode, BodyInputs, OutError))
		{
			return false;
		}

		IOptimusShaderTextProvider* BaseShaderTextProvider = Cast<IOptimusShaderTextProvider>(BaseKernelNode);
		IOptimusShaderTextProvider* WitnessShaderTextProvider = Cast<IOptimusShaderTextProvider>(WitnessKernelNode);
		if (!BaseShaderTextProvider || !WitnessShaderTextProvider)
		{
			OutError = TEXT("One or more reflected custom kernels do not implement IOptimusShaderTextProvider.");
			return false;
		}
		BaseShaderTextProvider->SetShaderText(GetBaseKernelSource());
		WitnessShaderTextProvider->SetShaderText(GetWitnessKernelSource());

		if (!AddRequiredLink(Graph, PrimaryBindingNode, SkeletalComponentPin, GarmentReadNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, PrimaryBindingNode, SkeletalComponentPin, WriteNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, BodyBindingNode, SkeletalComponentPin, BodyReadNode, SkinnedComponentPin, OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("Position"), BaseKernelNode, TEXT("Primary Group.GarmentPosition"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("TangentX"), BaseKernelNode, TEXT("Primary Group.GarmentTangentX"), OutError)
			|| !AddRequiredLink(Graph, GarmentReadNode, TEXT("TangentZ"), BaseKernelNode, TEXT("Primary Group.GarmentTangentZ"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), BaseKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, BaseKernelNode, TEXT("CorrectedPosition"), BasePositionResourceNode, TEXT("SetBaseCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, BasePositionResourceNode, TEXT("GetBaseCorrectedPosition"), WitnessKernelNode, TEXT("Primary Group.BaseCorrectedPosition"), OutError)
			|| !AddRequiredLink(Graph, BaseKernelNode, TEXT("PreservedTangentX"), WitnessKernelNode, TEXT("Primary Group.BaseTangentX"), OutError)
			|| !AddRequiredLink(Graph, BaseKernelNode, TEXT("PreservedTangentZ"), WitnessKernelNode, TEXT("Primary Group.BaseTangentZ"), OutError)
			|| !AddRequiredLink(Graph, BodyReadNode, TEXT("Position"), WitnessKernelNode, TEXT("Body.BodyPosition"), OutError)
			|| !AddRequiredLink(Graph, WitnessKernelNode, TEXT("FinalPosition"), WriteNode, TEXT("Position"), OutError)
			|| !AddRequiredLink(Graph, WitnessKernelNode, TEXT("FinalTangentX"), WriteNode, TEXT("TangentX"), OutError)
			|| !AddRequiredLink(Graph, WitnessKernelNode, TEXT("FinalTangentZ"), WriteNode, TEXT("TangentZ"), OutError))
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
					BaseKernelNode,
					FString::Printf(TEXT("Primary Group.%s"), *Spec.Name.ToString()),
					OutError)
				|| !AddRequiredLink(
					Graph,
					VariableNode,
					Spec.Name.ToString(),
					WitnessKernelNode,
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
					TEXT("Kernel binding %s[%d] does not match V26.5 schema (%s)."),
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
		UOptimusResourceDescription* BasePositionResource = Resources.Num() == 1 ? Resources[0] : nullptr;
		if (!BasePositionResource
			|| BasePositionResource->ResourceName != BasePositionResourceName
			|| BasePositionResource->DataType != ResolveVector3Type()
			|| BasePositionResource->ComponentBinding.Get() != PrimaryBinding
			|| BasePositionResource->DataDomain != VertexDomain)
		{
			Errors.Add(TEXT("BaseCorrectedPosition resource is missing or not bound to Garment float3/Vertex."));
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
			const int32 ExpectedNodeCount = 2 + 3 + 2 + 1 + GetVariableSpecs().Num();
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
					&& Node->FindPin(TEXT("SetBaseCorrectedPosition"))
					&& Node->FindPin(TEXT("GetBaseCorrectedPosition")))
				{
					ResourceNodes.Add(Node);
				}
			}

			if (KernelNodes.Num() != 2
				|| ReadNodes.Num() != 2
				|| WriteNodes.Num() != 1
				|| ComponentNodes.Num() != 2
				|| VariableNodes.Num() != GetVariableSpecs().Num()
				|| ResourceNodes.Num() != 1)
			{
				Errors.Add(TEXT("Required two-kernel/read/write/component/resource/variable node cardinality does not match V26.5."));
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
				UOptimusNode* WitnessKernelNode = nullptr;
				for (UOptimusNode* KernelNode : KernelNodes)
				{
					const FName Name = GetKernelName(KernelNode);
					if (Name == BaseKernelName)
					{
						BaseKernelNode = KernelNode;
					}
					else if (Name == WitnessKernelName)
					{
						WitnessKernelNode = KernelNode;
					}
				}
				UOptimusNode* WriteNode = WriteNodes[0];
				UOptimusNode* BasePositionResourceNode = ResourceNodes[0];
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
					|| !WitnessKernelNode)
				{
					Errors.Add(TEXT("Explicit Garment/Body routing or named two-kernel chain is incomplete."));
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
						&& ArePinsDirectlyLinked(BasePositionResourceNode, TEXT("GetBaseCorrectedPosition"), WitnessKernelNode, TEXT("Primary Group.BaseCorrectedPosition"))
						&& ArePinsDirectlyLinked(BaseKernelNode, TEXT("PreservedTangentX"), WitnessKernelNode, TEXT("Primary Group.BaseTangentX"))
						&& ArePinsDirectlyLinked(BaseKernelNode, TEXT("PreservedTangentZ"), WitnessKernelNode, TEXT("Primary Group.BaseTangentZ"))
						&& ArePinsDirectlyLinked(BodyReadNode, TEXT("Position"), WitnessKernelNode, TEXT("Body.BodyPosition"))
						&& ArePinsDirectlyLinked(WitnessKernelNode, TEXT("FinalPosition"), WriteNode, TEXT("Position"))
						&& ArePinsDirectlyLinked(WitnessKernelNode, TEXT("FinalTangentX"), WriteNode, TEXT("TangentX"))
						&& ArePinsDirectlyLinked(WitnessKernelNode, TEXT("FinalTangentZ"), WriteNode, TEXT("TangentZ"));
					if (!bCoreLinksValid)
					{
						Errors.Add(TEXT("Core Garment/Body/base/resource/witness/write links do not match V26.5."));
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

				const IOptimusShaderTextProvider* BaseShaderTextProvider = Cast<IOptimusShaderTextProvider>(BaseKernelNode);
				const IOptimusShaderTextProvider* WitnessShaderTextProvider = Cast<IOptimusShaderTextProvider>(WitnessKernelNode);
				if (!BaseShaderTextProvider || BaseShaderTextProvider->GetShaderText() != GetBaseKernelSource())
				{
					Errors.Add(TEXT("Base kernel source differs from the generated V26.5 source."));
				}
				if (!WitnessShaderTextProvider || WitnessShaderTextProvider->GetShaderText() != GetWitnessKernelSource())
				{
					Errors.Add(TEXT("Witness kernel source differs from the generated V26.5 source."));
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
						|| !ArePinsDirectlyLinked(*VariableNode, Spec.Name.ToString(), WitnessKernelNode, KernelPinPath))
					{
						Errors.Add(FString::Printf(TEXT("Variable node/dual-kernel links %s are missing."), *Spec.Name.ToString()));
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
			TEXT("PASS: %s; Primary=Garment, Secondary=Body, %d variables, %d nodes, BodyToGarment transform, two-pass unilateral vertex/witness constraint and tangent passthrough validated."),
			AssetObjectPath,
			GetVariableSpecs().Num(),
			2 + 3 + 2 + 1 + GetVariableSpecs().Num());
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
