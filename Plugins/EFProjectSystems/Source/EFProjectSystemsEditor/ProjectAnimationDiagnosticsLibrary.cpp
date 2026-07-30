#include "ProjectAnimationDiagnosticsLibrary.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "AnimationBlueprintLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/Factory.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

bool UProjectAnimationDiagnosticsLibrary::AssignMissingAnimationSkeleton(UAnimationAsset* AnimationAsset, USkeleton* Skeleton)
{
	if (!AnimationAsset || !Skeleton)
	{
		return false;
	}

	if (USkeleton* ExistingSkeleton = AnimationAsset->GetSkeleton())
	{
		return ExistingSkeleton == Skeleton;
	}

	AnimationAsset->Modify();
	AnimationAsset->SetSkeleton(Skeleton);
	AnimationAsset->ValidateSkeleton();
	AnimationAsset->MarkPackageDirty();
	return AnimationAsset->GetSkeleton() == Skeleton;
}

namespace ProjectAnimationDiagnostics
{
	static FString WriteJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	static TSharedRef<FJsonObject> MakeRoot()
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), true);
		return Root;
	}

	static FString ErrorJson(const FString& Message)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("error"), Message);
		return WriteJson(Root);
	}

	static FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : TEXT("");
	}

	static FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT("");
	}

	static FString EnumToString(const TCHAR* EnumPath, int64 Value)
	{
		if (const UEnum* Enum = FindObject<UEnum>(nullptr, EnumPath))
		{
			return Enum->GetNameStringByValue(Value);
		}

		return FString::FromInt(static_cast<int32>(Value));
	}

	static FString RetargetModeToString(const EBoneTranslationRetargetingMode::Type Mode)
	{
		switch (Mode)
		{
		case EBoneTranslationRetargetingMode::Animation:
			return TEXT("Animation");
		case EBoneTranslationRetargetingMode::Skeleton:
			return TEXT("Skeleton");
		case EBoneTranslationRetargetingMode::AnimationScaled:
			return TEXT("AnimationScaled");
		case EBoneTranslationRetargetingMode::AnimationRelative:
			return TEXT("AnimationRelative");
		case EBoneTranslationRetargetingMode::OrientAndScale:
			return TEXT("OrientAndScale");
		default:
			return TEXT("Unknown");
		}
	}

	static TArray<FString> ParseTokens(const FString& RawTokens)
	{
		TArray<FString> Tokens;
		RawTokens.ParseIntoArray(Tokens, TEXT(","), true);

		for (FString& Token : Tokens)
		{
			Token.TrimStartAndEndInline();
			Token = Token.ToLower();
		}

		Tokens.RemoveAll([](const FString& Token)
		{
			return Token.IsEmpty();
		});

		return Tokens;
	}

	static bool MatchesTokens(const FString& Candidate, const TArray<FString>& Tokens)
	{
		if (Tokens.IsEmpty())
		{
			return true;
		}

		const FString LowerCandidate = Candidate.ToLower();
		for (const FString& Token : Tokens)
		{
			if (LowerCandidate.Contains(Token))
			{
				return true;
			}
		}

		return false;
	}

	static TSharedRef<FJsonObject> TransformToJson(const FTransform& Transform)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();

		const FVector Location = Transform.GetLocation();
		TArray<TSharedPtr<FJsonValue>> LocationArray;
		LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
		LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
		LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
		Object->SetArrayField(TEXT("location"), LocationArray);

		const FRotator Rotation = Transform.GetRotation().Rotator();
		TArray<TSharedPtr<FJsonValue>> RotationArray;
		RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
		RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
		RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
		Object->SetArrayField(TEXT("rotation_pyr"), RotationArray);

		const FVector Scale = Transform.GetScale3D();
		TArray<TSharedPtr<FJsonValue>> ScaleArray;
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
		Object->SetArrayField(TEXT("scale"), ScaleArray);

		return Object;
	}

	static void AddPathField(const TSharedRef<FJsonObject>& Object, const TCHAR* Name, const UObject* Value)
	{
		Object->SetStringField(Name, ObjectPath(Value));
	}

	static FString ExportPropertyValue(UObject* Target, FProperty* Property)
	{
		if (!Target || !Property)
		{
			return TEXT("");
		}

		FString Value;
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Target));
			return FString::Printf(TEXT("<array num=%d>"), Helper.Num());
		}

		Property->ExportText_InContainer(0, Value, Target, Target, Target, PPF_None);
		if (Value.Len() > 512)
		{
			Value = Value.Left(512) + TEXT("...");
		}
		return Value;
	}

	static TArray<TSharedPtr<FJsonValue>> SnapshotProperties(UObject* Target, const FString& NameContainsCsv)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Target)
		{
			return Values;
		}

		const TArray<FString> Tokens = ParseTokens(NameContainsCsv);
		for (TFieldIterator<FProperty> It(Target->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !MatchesTokens(Property->GetName(), Tokens))
			{
				continue;
			}

			TSharedRef<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
			PropertyObject->SetStringField(TEXT("name"), Property->GetName());
			PropertyObject->SetStringField(TEXT("type"), Property->GetCPPType());
			PropertyObject->SetStringField(TEXT("value"), ExportPropertyValue(Target, Property));
			PropertyObject->SetStringField(TEXT("owner_class"), Property->GetOwnerClass() ? Property->GetOwnerClass()->GetPathName() : TEXT(""));
			Values.Add(MakeShared<FJsonValueObject>(PropertyObject));
		}

		return Values;
	}

	static void AddBoneRetargetCounts(const TSharedRef<FJsonObject>& Root, const USkeleton* Skeleton)
	{
		TMap<FString, int32> Counts;
		TArray<TSharedPtr<FJsonValue>> NonSkeletonModes;

		if (!Skeleton)
		{
			Root->SetObjectField(TEXT("translation_retarget_mode_counts"), MakeShared<FJsonObject>());
			Root->SetArrayField(TEXT("non_skeleton_retarget_bones"), NonSkeletonModes);
			return;
		}

		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			const EBoneTranslationRetargetingMode::Type Mode = Skeleton->GetBoneTranslationRetargetingMode(BoneIndex);
			const FString ModeName = RetargetModeToString(Mode);
			Counts.FindOrAdd(ModeName)++;

			if (Mode != EBoneTranslationRetargetingMode::Skeleton)
			{
				TSharedRef<FJsonObject> BoneObject = MakeShared<FJsonObject>();
				BoneObject->SetNumberField(TEXT("index"), BoneIndex);
				BoneObject->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(BoneIndex).ToString());
				BoneObject->SetStringField(TEXT("mode"), ModeName);
				NonSkeletonModes.Add(MakeShared<FJsonValueObject>(BoneObject));
			}
		}

		TSharedRef<FJsonObject> CountsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pair : Counts)
		{
			CountsObject->SetNumberField(Pair.Key, Pair.Value);
		}

		Root->SetObjectField(TEXT("translation_retarget_mode_counts"), CountsObject);
		Root->SetArrayField(TEXT("non_skeleton_retarget_bones"), NonSkeletonModes);
	}

	static UAnimBlueprint* ResolveAnimBlueprintFromClass(UClass* AnimClass)
	{
		return AnimClass ? Cast<UAnimBlueprint>(AnimClass->ClassGeneratedBy) : nullptr;
	}

	static void CollectGraphNodes(UEdGraph* Graph, const FString& GraphPath, const TArray<FString>& Tokens, TSet<const UEdGraph*>& VisitedGraphs, TArray<TSharedPtr<FJsonValue>>& OutNodes)
	{
		if (!Graph || VisitedGraphs.Contains(Graph))
		{
			return;
		}

		VisitedGraphs.Add(Graph);

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString NodeClass = Node->GetClass() ? Node->GetClass()->GetName() : TEXT("");
			const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			const FString Combined = NodeClass + TEXT(" ") + NodeTitle + TEXT(" ") + GraphPath;
			if (!MatchesTokens(Combined, Tokens))
			{
				continue;
			}

			TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
			NodeObject->SetStringField(TEXT("graph"), GraphPath);
			NodeObject->SetStringField(TEXT("node_class"), NodeClass);
			NodeObject->SetStringField(TEXT("node_title"), NodeTitle);
			NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
			NodeObject->SetNumberField(TEXT("pos_x"), Node->NodePosX);
			NodeObject->SetNumberField(TEXT("pos_y"), Node->NodePosY);
			NodeObject->SetArrayField(TEXT("properties"), SnapshotProperties(Node, TEXT("Node,IK,Hand,Foot,Retarget,Aim,Lean,Additive,ControlRig,Pose,Offset,Alpha,Slot,Layer")));

			TArray<TSharedPtr<FJsonValue>> Pins;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					continue;
				}

				TSharedRef<FJsonObject> PinObject = MakeShared<FJsonObject>();
				PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinObject->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
				PinObject->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

				TArray<TSharedPtr<FJsonValue>> LinkedNodes;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin && LinkedPin->GetOwningNode())
					{
						LinkedNodes.Add(MakeShared<FJsonValueString>(LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
					}
				}
				PinObject->SetArrayField(TEXT("linked_nodes"), LinkedNodes);
				Pins.Add(MakeShared<FJsonValueObject>(PinObject));
			}
			NodeObject->SetArrayField(TEXT("pins"), Pins);
			OutNodes.Add(MakeShared<FJsonValueObject>(NodeObject));
		}

		for (UEdGraph* SubGraph : Graph->SubGraphs)
		{
			CollectGraphNodes(SubGraph, GraphPath + TEXT("/") + (SubGraph ? SubGraph->GetName() : TEXT("None")), Tokens, VisitedGraphs, OutNodes);
		}
	}

	static TSharedRef<FJsonObject> SnapshotAnimBlueprint(UAnimBlueprint* AnimBP, const FString& NodeNameContainsCsv)
	{
		TSharedRef<FJsonObject> BlueprintObject = MakeShared<FJsonObject>();
		if (!AnimBP)
		{
			BlueprintObject->SetBoolField(TEXT("available"), false);
			return BlueprintObject;
		}

		BlueprintObject->SetBoolField(TEXT("available"), true);
		AddPathField(BlueprintObject, TEXT("asset"), AnimBP);
		AddPathField(BlueprintObject, TEXT("target_skeleton"), AnimBP->TargetSkeleton);

		TArray<TSharedPtr<FJsonValue>> Variables;
		const TArray<FString> Tokens = ParseTokens(NodeNameContainsCsv);
		for (const FBPVariableDescription& Variable : AnimBP->NewVariables)
		{
			const FString VariableName = Variable.VarName.ToString();
			if (!MatchesTokens(VariableName, Tokens))
			{
				continue;
			}

			TSharedRef<FJsonObject> VariableObject = MakeShared<FJsonObject>();
			VariableObject->SetStringField(TEXT("name"), VariableName);
			VariableObject->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
			VariableObject->SetStringField(TEXT("sub_type"), Variable.VarType.PinSubCategoryObject.IsValid() ? Variable.VarType.PinSubCategoryObject->GetName() : TEXT(""));
			Variables.Add(MakeShared<FJsonValueObject>(VariableObject));
		}
		BlueprintObject->SetArrayField(TEXT("variables"), Variables);

		TArray<TSharedPtr<FJsonValue>> Nodes;
		TSet<const UEdGraph*> VisitedGraphs;
		for (UEdGraph* Graph : AnimBP->FunctionGraphs)
		{
			CollectGraphNodes(Graph, Graph ? Graph->GetName() : TEXT("None"), Tokens, VisitedGraphs, Nodes);
		}
		for (UEdGraph* Graph : AnimBP->UbergraphPages)
		{
			CollectGraphNodes(Graph, Graph ? Graph->GetName() : TEXT("None"), Tokens, VisitedGraphs, Nodes);
		}
		BlueprintObject->SetArrayField(TEXT("nodes"), Nodes);
		return BlueprintObject;
	}

	struct FBonePoseDelta
	{
		int32 BoneIndex = INDEX_NONE;
		FName BoneName = NAME_None;
		int32 ParentIndex = INDEX_NONE;
		FString RetargetMode;
		bool bHasAnimationTrack = false;
		double LocalTranslationDelta = 0.0;
		double LocalRotationDeltaDegrees = 0.0;
		double LocalScaleDelta = 0.0;
		double ComponentTranslationDelta = 0.0;
		double ComponentRotationDeltaDegrees = 0.0;
		FTransform RuntimeLocal;
		FTransform RuntimeComponent;
		FTransform SourceLocal;
		FTransform SourceComponent;
	};

	static TSharedRef<FJsonObject> BonePoseDeltaToJson(const FBonePoseDelta& Delta, const bool bIncludeTransforms)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("bone_index"), Delta.BoneIndex);
		Object->SetStringField(TEXT("bone_name"), Delta.BoneName.ToString());
		Object->SetNumberField(TEXT("parent_index"), Delta.ParentIndex);
		Object->SetStringField(TEXT("translation_retarget_mode"), Delta.RetargetMode);
		Object->SetBoolField(TEXT("has_animation_track"), Delta.bHasAnimationTrack);
		Object->SetNumberField(TEXT("local_translation_delta"), Delta.LocalTranslationDelta);
		Object->SetNumberField(TEXT("local_rotation_delta_degrees"), Delta.LocalRotationDeltaDegrees);
		Object->SetNumberField(TEXT("local_scale_delta"), Delta.LocalScaleDelta);
		Object->SetNumberField(TEXT("component_translation_delta"), Delta.ComponentTranslationDelta);
		Object->SetNumberField(TEXT("component_rotation_delta_degrees"), Delta.ComponentRotationDeltaDegrees);

		if (bIncludeTransforms)
		{
			Object->SetObjectField(TEXT("runtime_local"), TransformToJson(Delta.RuntimeLocal));
			Object->SetObjectField(TEXT("source_local"), TransformToJson(Delta.SourceLocal));
			Object->SetObjectField(TEXT("runtime_component"), TransformToJson(Delta.RuntimeComponent));
			Object->SetObjectField(TEXT("source_component"), TransformToJson(Delta.SourceComponent));
		}

		return Object;
	}

	static FString CompareMeshPoseToAnimationAtTimeInternal(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* AnimationAsset, const float TimeSeconds, const FString& BoneNameContainsCsv, const int32 MaxBones)
	{
		if (!MeshComponent)
		{
			return ErrorJson(TEXT("MeshComponent is null."));
		}

		USkeletalMesh* MeshAsset = MeshComponent->GetSkeletalMeshAsset();
		if (!MeshAsset)
		{
			return ErrorJson(TEXT("MeshComponent has no skeletal mesh asset."));
		}

		if (!AnimationAsset)
		{
			return ErrorJson(TEXT("AnimationAsset is null."));
		}

		MeshComponent->RefreshBoneTransforms();

		const FReferenceSkeleton& RefSkeleton = MeshAsset->GetRefSkeleton();
		const int32 NumBones = RefSkeleton.GetNum();
		if (NumBones <= 0)
		{
			return ErrorJson(TEXT("Skeletal mesh has no reference bones."));
		}

		TArray<FName> BoneNames;
		BoneNames.Reserve(NumBones);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			BoneNames.Add(RefSkeleton.GetBoneName(BoneIndex));
		}

		TArray<FTransform> SourceLocalTransforms;
		UAnimationBlueprintLibrary::GetBonePosesForTime(AnimationAsset, BoneNames, TimeSeconds, false, SourceLocalTransforms, MeshAsset);
		if (SourceLocalTransforms.Num() != NumBones)
		{
			return ErrorJson(FString::Printf(TEXT("AnimationBlueprintLibrary returned %d poses for %d bones."), SourceLocalTransforms.Num(), NumBones));
		}

		TArray<FTransform> SourceComponentTransforms;
		SourceComponentTransforms.SetNum(NumBones);
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, MakeArrayView(SourceLocalTransforms), SourceComponentTransforms);

		TArray<FTransform> RuntimeLocalTransforms = MeshComponent->GetBoneSpaceTransforms();
		const TArray<FTransform>& RuntimeComponentTransforms = MeshComponent->GetComponentSpaceTransforms();
		if (RuntimeLocalTransforms.Num() < NumBones || RuntimeComponentTransforms.Num() < NumBones)
		{
			return ErrorJson(FString::Printf(TEXT("Runtime pose has too few transforms. local=%d component=%d required=%d"), RuntimeLocalTransforms.Num(), RuntimeComponentTransforms.Num(), NumBones));
		}

		TSet<FName> TrackNames;
		if (const IAnimationDataModel* DataModel = AnimationAsset->GetDataModel())
		{
			TArray<FName> TrackNameArray;
			DataModel->GetBoneTrackNames(TrackNameArray);
			for (const FName& TrackName : TrackNameArray)
			{
				TrackNames.Add(TrackName);
			}
		}

		const TArray<FString> Tokens = ParseTokens(BoneNameContainsCsv);
		TArray<FBonePoseDelta> Deltas;
		Deltas.Reserve(NumBones);
		USkeleton* MeshSkeleton = MeshAsset->GetSkeleton();

		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
			if (!MatchesTokens(BoneName.ToString(), Tokens))
			{
				continue;
			}

			FBonePoseDelta Delta;
			Delta.BoneIndex = BoneIndex;
			Delta.BoneName = BoneName;
			Delta.ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			Delta.RuntimeLocal = RuntimeLocalTransforms[BoneIndex];
			Delta.RuntimeComponent = RuntimeComponentTransforms[BoneIndex];
			Delta.SourceLocal = SourceLocalTransforms[BoneIndex];
			Delta.SourceComponent = SourceComponentTransforms[BoneIndex];
			Delta.bHasAnimationTrack = TrackNames.Contains(BoneName);
			Delta.RetargetMode = MeshSkeleton ? RetargetModeToString(MeshSkeleton->GetBoneTranslationRetargetingMode(BoneIndex)) : TEXT("");
			Delta.LocalTranslationDelta = FVector::Distance(Delta.RuntimeLocal.GetLocation(), Delta.SourceLocal.GetLocation());
			Delta.LocalRotationDeltaDegrees = FMath::RadiansToDegrees(Delta.RuntimeLocal.GetRotation().AngularDistance(Delta.SourceLocal.GetRotation()));
			Delta.LocalScaleDelta = FVector::Distance(Delta.RuntimeLocal.GetScale3D(), Delta.SourceLocal.GetScale3D());
			Delta.ComponentTranslationDelta = FVector::Distance(Delta.RuntimeComponent.GetLocation(), Delta.SourceComponent.GetLocation());
			Delta.ComponentRotationDeltaDegrees = FMath::RadiansToDegrees(Delta.RuntimeComponent.GetRotation().AngularDistance(Delta.SourceComponent.GetRotation()));
			Deltas.Add(Delta);
		}

		TArray<FBonePoseDelta> SortedDeltas = Deltas;
		SortedDeltas.Sort([](const FBonePoseDelta& A, const FBonePoseDelta& B)
		{
			const double ScoreA = A.ComponentTranslationDelta + A.LocalTranslationDelta + A.ComponentRotationDeltaDegrees + A.LocalRotationDeltaDegrees;
			const double ScoreB = B.ComponentTranslationDelta + B.LocalTranslationDelta + B.ComponentRotationDeltaDegrees + B.LocalRotationDeltaDegrees;
			return ScoreA > ScoreB;
		});

		const int32 OutputLimit = MaxBones > 0 ? FMath::Min(MaxBones, Deltas.Num()) : Deltas.Num();
		const int32 TopLimit = FMath::Min(40, SortedDeltas.Num());

		TSharedRef<FJsonObject> Root = MakeRoot();
		AddPathField(Root, TEXT("mesh_component"), MeshComponent);
		AddPathField(Root, TEXT("mesh_asset"), MeshAsset);
		AddPathField(Root, TEXT("mesh_skeleton"), MeshSkeleton);
		AddPathField(Root, TEXT("animation_asset"), AnimationAsset);
		AddPathField(Root, TEXT("animation_skeleton"), AnimationAsset->GetSkeleton());
		Root->SetNumberField(TEXT("time_seconds"), TimeSeconds);
		Root->SetNumberField(TEXT("runtime_bone_count"), NumBones);
		Root->SetNumberField(TEXT("matched_bone_count"), Deltas.Num());
		Root->SetNumberField(TEXT("output_bone_count"), OutputLimit);

		TArray<TSharedPtr<FJsonValue>> TopDeltas;
		for (int32 Index = 0; Index < TopLimit; ++Index)
		{
			TopDeltas.Add(MakeShared<FJsonValueObject>(BonePoseDeltaToJson(SortedDeltas[Index], false)));
		}
		Root->SetArrayField(TEXT("top_deltas"), TopDeltas);

		TArray<TSharedPtr<FJsonValue>> BoneDeltas;
		for (int32 Index = 0; Index < OutputLimit; ++Index)
		{
			BoneDeltas.Add(MakeShared<FJsonValueObject>(BonePoseDeltaToJson(Deltas[Index], true)));
		}
		Root->SetArrayField(TEXT("bone_deltas"), BoneDeltas);
		return WriteJson(Root);
	}
}

FString UProjectAnimationDiagnosticsLibrary::BuildMaleWalkRetargetAssets()
{
	static const FString FemaleMeshPath = TEXT("/Game/DazToUnreal/Female/Female");
	static const FString MaleMeshPath = TEXT("/Game/DazToUnreal/Male/Male");
	static const FString RetargetRoot = TEXT("/Game/_Game/Animations/Retarget/Male");
	static const FString OutputRoot = RetargetRoot + TEXT("/WalkN");
	static const FString SourceRigPath = RetargetRoot + TEXT("/IK_FemaleWalkN_Source");
	static const FString TargetRigPath = RetargetRoot + TEXT("/IK_MaleWalkN_Target");
	static const FString RetargeterPath = RetargetRoot + TEXT("/RTG_FemaleToMale_WalkN");
	static const TArray<FString> SourceAnimationPaths =
	{
		TEXT("/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04"),
		TEXT("/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04_Pivot"),
		TEXT("/Game/_Game/Animations/Emotes/General/Anim_KA_Idle01_breathing")
	};

	auto MakeFailure = [](const FString& Error)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("error"), Error);
		return ProjectAnimationDiagnostics::WriteJson(Root);
	};

	USkeletalMesh* FemaleMesh = LoadObject<USkeletalMesh>(nullptr, *(FemaleMeshPath + TEXT(".Female")));
	USkeletalMesh* MaleMesh = LoadObject<USkeletalMesh>(nullptr, *(MaleMeshPath + TEXT(".Male")));
	if (!FemaleMesh || !MaleMesh)
	{
		return MakeFailure(TEXT("Female or Male skeletal mesh could not be loaded."));
	}
	if (!FemaleMesh->GetSkeleton() || FemaleMesh->GetSkeleton() != MaleMesh->GetSkeleton())
	{
		return MakeFailure(TEXT("Female and Male must share the authoritative UEFN Skeleton."));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	auto LoadOrCreateRig = [&AssetTools](const FString& AssetPath, USkeletalMesh* PreviewMesh) -> UIKRigDefinition*
	{
		UIKRigDefinition* Rig = LoadObject<UIKRigDefinition>(nullptr, *(AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath)));
		if (!Rig)
		{
			const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
			const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
			UIKRigDefinitionFactory* Factory = NewObject<UIKRigDefinitionFactory>();
			Rig = Cast<UIKRigDefinition>(AssetTools.CreateAsset(AssetName, PackagePath, UIKRigDefinition::StaticClass(), Factory));
		}
		if (!Rig)
		{
			return nullptr;
		}

		UIKRigController* Controller = UIKRigController::GetController(Rig);
		if (!Controller || !Controller->SetSkeletalMesh(PreviewMesh))
		{
			return nullptr;
		}

		Controller->SetRetargetRoot(TEXT("pelvis"));
		Controller->SetRootMotionBone(TEXT("root"));

		struct FChainSpec
		{
			FName Name;
			FName Start;
			FName End;
		};
		static const FChainSpec ChainSpecs[] =
		{
			{ TEXT("Root"), TEXT("root"), TEXT("root") },
			{ TEXT("Spine"), TEXT("pelvis"), TEXT("spine_05") },
			{ TEXT("Neck"), TEXT("neck_01"), TEXT("head") },
			{ TEXT("LeftArm"), TEXT("clavicle_l"), TEXT("hand_l") },
			{ TEXT("RightArm"), TEXT("clavicle_r"), TEXT("hand_r") },
			{ TEXT("LeftLeg"), TEXT("thigh_l"), TEXT("ball_l") },
			{ TEXT("RightLeg"), TEXT("thigh_r"), TEXT("ball_r") }
		};
		for (const FChainSpec& Spec : ChainSpecs)
		{
			const bool bChainExists = Controller->GetRetargetChains().ContainsByPredicate(
				[&Spec](const FBoneChain& Chain)
				{
					return Chain.ChainName == Spec.Name;
				});
			if (!bChainExists)
			{
				Controller->AddRetargetChain(Spec.Name, Spec.Start, Spec.End, NAME_None);
			}
			else
			{
				Controller->SetRetargetChainStartBone(Spec.Name, Spec.Start);
				Controller->SetRetargetChainEndBone(Spec.Name, Spec.End);
			}
		}

		Rig->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
		return Rig;
	};

	UIKRigDefinition* SourceRig = LoadOrCreateRig(SourceRigPath, FemaleMesh);
	UIKRigDefinition* TargetRig = LoadOrCreateRig(TargetRigPath, MaleMesh);
	if (!SourceRig || !TargetRig)
	{
		return MakeFailure(TEXT("Unable to create or configure the project-owned IK Rigs."));
	}

	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(
		nullptr,
		*(RetargeterPath + TEXT(".") + FPackageName::GetLongPackageAssetName(RetargeterPath)));
	if (!Retargeter)
	{
		UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
		Retargeter = Cast<UIKRetargeter>(AssetTools.CreateAsset(
			FPackageName::GetLongPackageAssetName(RetargeterPath),
			FPackageName::GetLongPackagePath(RetargeterPath),
			UIKRetargeter::StaticClass(),
			Factory));
	}
	if (!Retargeter)
	{
		return MakeFailure(TEXT("Unable to create the project-owned Female-to-Male IK Retargeter."));
	}

	UIKRetargeterController* RetargetController = UIKRetargeterController::GetController(Retargeter);
	if (!RetargetController)
	{
		return MakeFailure(TEXT("Unable to obtain the IK Retargeter controller."));
	}
	RetargetController->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
	RetargetController->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);
	RetargetController->SetPreviewMesh(ERetargetSourceOrTarget::Source, FemaleMesh);
	RetargetController->SetPreviewMesh(ERetargetSourceOrTarget::Target, MaleMesh);
	RetargetController->AddDefaultOps();
	RetargetController->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
	RetargetController->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
	RetargetController->AutoMapChains(EAutoMapChainType::Exact, true);
	RetargetController->CleanAsset();
	Retargeter->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	FIKRetargetBatchOperationInputs Inputs;
	for (const FString& SourcePath : SourceAnimationPaths)
	{
		UAnimationAsset* AnimationAsset = LoadObject<UAnimationAsset>(
			nullptr,
			*(SourcePath + TEXT(".") + FPackageName::GetLongPackageAssetName(SourcePath)));
		if (!AnimationAsset)
		{
			return MakeFailure(FString::Printf(TEXT("Unable to load source animation: %s"), *SourcePath));
		}
		Inputs.AssetsToRetarget.Add(FAssetData(AnimationAsset));
	}
	Inputs.SourceMesh = FemaleMesh;
	Inputs.TargetMesh = MaleMesh;
	Inputs.IKRetargetAsset = Retargeter;
	Inputs.Suffix = TEXT("_Male");
	Inputs.TargetPath = OutputRoot;
	Inputs.bUseSourcePath = false;
	Inputs.bIncludeReferencedAssets = false;
	Inputs.bOverwriteExistingFiles = true;
	Inputs.bRetainAdditiveFlags = true;

	const TArray<FAssetData> CreatedAssets = UIKRetargetBatchOperation::RunBatchRetarget(Inputs);
	if (CreatedAssets.Num() != SourceAnimationPaths.Num())
	{
		return MakeFailure(FString::Printf(
			TEXT("Expected %d Male retargeted animations but created %d."),
			SourceAnimationPaths.Num(),
			CreatedAssets.Num()));
	}

	TArray<TSharedPtr<FJsonValue>> CreatedJson;
	for (const FAssetData& AssetData : CreatedAssets)
	{
		UAnimationAsset* CreatedAnimation = Cast<UAnimationAsset>(AssetData.GetAsset());
		if (!CreatedAnimation)
		{
			return MakeFailure(TEXT("A retarget output did not load as an AnimationAsset."));
		}
		CreatedAnimation->SetPreviewMesh(MaleMesh, true);
		CreatedAnimation->MarkPackageDirty();
		const FString CreatedPath = AssetData.GetObjectPathString();
		if (!UEditorAssetLibrary::SaveAsset(CreatedPath, false))
		{
			return MakeFailure(TEXT("Unable to save Male retarget output: ") + CreatedPath);
		}
		CreatedJson.Add(MakeShared<FJsonValueString>(CreatedPath));
	}
	FAssetCompilingManager::Get().FinishAllCompilation();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("female_mesh"), FemaleMesh->GetPathName());
	Root->SetStringField(TEXT("male_mesh"), MaleMesh->GetPathName());
	Root->SetStringField(TEXT("shared_skeleton"), FemaleMesh->GetSkeleton()->GetPathName());
	Root->SetStringField(TEXT("source_rig"), SourceRig->GetPathName());
	Root->SetStringField(TEXT("target_rig"), TargetRig->GetPathName());
	Root->SetStringField(TEXT("retargeter"), Retargeter->GetPathName());
	Root->SetStringField(TEXT("output_root"), OutputRoot);
	Root->SetArrayField(TEXT("created_assets"), CreatedJson);
	return ProjectAnimationDiagnostics::WriteJson(Root);
}

FString UProjectAnimationDiagnosticsLibrary::SnapshotObjectProperties(UObject* Target, const FString& NameContainsCsv)
{
	if (!Target)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("Target is null."));
	}

	TSharedRef<FJsonObject> Root = ProjectAnimationDiagnostics::MakeRoot();
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("target"), Target);
	Root->SetStringField(TEXT("class"), ProjectAnimationDiagnostics::ClassPath(Target));
	Root->SetArrayField(TEXT("properties"), ProjectAnimationDiagnostics::SnapshotProperties(Target, NameContainsCsv));
	return ProjectAnimationDiagnostics::WriteJson(Root);
}

FString UProjectAnimationDiagnosticsLibrary::SnapshotSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, const FString& BoneNameContainsCsv, const int32 MaxBones)
{
	if (!MeshComponent)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("MeshComponent is null."));
	}

	USkeletalMesh* MeshAsset = MeshComponent->GetSkeletalMeshAsset();
	TSharedRef<FJsonObject> Root = ProjectAnimationDiagnostics::MakeRoot();
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("component"), MeshComponent);
	Root->SetStringField(TEXT("component_name"), MeshComponent->GetName());
	Root->SetStringField(TEXT("owner"), MeshComponent->GetOwner() ? MeshComponent->GetOwner()->GetPathName() : TEXT(""));
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("mesh_asset"), MeshAsset);
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("mesh_skeleton"), MeshAsset ? MeshAsset->GetSkeleton() : nullptr);
	Root->SetStringField(TEXT("animation_mode"), ProjectAnimationDiagnostics::EnumToString(TEXT("/Script/Engine.EAnimationMode"), MeshComponent->GetAnimationMode()));
	Root->SetStringField(TEXT("anim_class"), MeshComponent->GetAnimClass() ? MeshComponent->GetAnimClass()->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("anim_instance_class"), MeshComponent->GetAnimInstance() ? MeshComponent->GetAnimInstance()->GetClass()->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("post_process_instance_class"), MeshComponent->GetPostProcessInstance() ? MeshComponent->GetPostProcessInstance()->GetClass()->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("mesh_post_process_anim_blueprint"), MeshAsset && MeshAsset->GetPostProcessAnimBlueprint() ? MeshAsset->GetPostProcessAnimBlueprint()->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("leader_pose_component"), MeshComponent->LeaderPoseComponent.IsValid() ? MeshComponent->LeaderPoseComponent.Get()->GetPathName() : TEXT(""));
	Root->SetBoolField(TEXT("visible"), MeshComponent->IsVisible());
	Root->SetBoolField(TEXT("component_tick_enabled"), MeshComponent->IsComponentTickEnabled());

	if (MeshAsset)
	{
		ProjectAnimationDiagnostics::AddBoneRetargetCounts(Root, MeshAsset->GetSkeleton());

		const FReferenceSkeleton& RefSkeleton = MeshAsset->GetRefSkeleton();
		const TArray<FString> Tokens = ProjectAnimationDiagnostics::ParseTokens(BoneNameContainsCsv);
		const int32 Limit = MaxBones > 0 ? MaxBones : RefSkeleton.GetNum();
		TArray<TSharedPtr<FJsonValue>> Bones;
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum() && Bones.Num() < Limit; ++BoneIndex)
		{
			const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
			if (!ProjectAnimationDiagnostics::MatchesTokens(BoneName.ToString(), Tokens))
			{
				continue;
			}

			TSharedRef<FJsonObject> BoneObject = MakeShared<FJsonObject>();
			BoneObject->SetNumberField(TEXT("index"), BoneIndex);
			BoneObject->SetStringField(TEXT("name"), BoneName.ToString());
			BoneObject->SetNumberField(TEXT("parent_index"), RefSkeleton.GetParentIndex(BoneIndex));
			if (MeshAsset->GetSkeleton())
			{
				BoneObject->SetStringField(TEXT("translation_retarget_mode"), ProjectAnimationDiagnostics::RetargetModeToString(MeshAsset->GetSkeleton()->GetBoneTranslationRetargetingMode(BoneIndex)));
			}
			BoneObject->SetObjectField(TEXT("ref_local"), ProjectAnimationDiagnostics::TransformToJson(RefSkeleton.GetRefBonePose()[BoneIndex]));
			Bones.Add(MakeShared<FJsonValueObject>(BoneObject));
		}
		Root->SetArrayField(TEXT("bones"), Bones);
		Root->SetNumberField(TEXT("bone_count"), RefSkeleton.GetNum());
	}

	return ProjectAnimationDiagnostics::WriteJson(Root);
}

FString UProjectAnimationDiagnosticsLibrary::SnapshotAnimSequenceAsset(UAnimSequenceBase* AnimationAsset)
{
	if (!AnimationAsset)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("AnimationAsset is null."));
	}

	TSharedRef<FJsonObject> Root = ProjectAnimationDiagnostics::MakeRoot();
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("asset"), AnimationAsset);
	Root->SetStringField(TEXT("class"), AnimationAsset->GetClass()->GetPathName());
	Root->SetNumberField(TEXT("play_length"), AnimationAsset->GetPlayLength());
	Root->SetNumberField(TEXT("rate_scale"), AnimationAsset->RateScale);
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("skeleton"), AnimationAsset->GetSkeleton());

	if (USkeleton* Skeleton = AnimationAsset->GetSkeleton())
	{
		ProjectAnimationDiagnostics::AddPathField(Root, TEXT("preview_mesh_for_asset"), Skeleton->GetAssetPreviewMesh(AnimationAsset));
		ProjectAnimationDiagnostics::AddPathField(Root, TEXT("preview_mesh_default"), Skeleton->GetPreviewMesh(false));
		ProjectAnimationDiagnostics::AddBoneRetargetCounts(Root, Skeleton);
	}

	TArray<TSharedPtr<FJsonValue>> TrackNames;
	if (const IAnimationDataModel* DataModel = AnimationAsset->GetDataModel())
	{
		TArray<FName> Names;
		DataModel->GetBoneTrackNames(Names);
		for (const FName& Name : Names)
		{
			TrackNames.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}

		TArray<TSharedPtr<FJsonValue>> FloatCurves;
		for (const FFloatCurve& Curve : DataModel->GetFloatCurves())
		{
			FloatCurves.Add(MakeShared<FJsonValueString>(Curve.GetName().ToString()));
		}

		TArray<TSharedPtr<FJsonValue>> TransformCurves;
		for (const FTransformCurve& Curve : DataModel->GetTransformCurves())
		{
			TransformCurves.Add(MakeShared<FJsonValueString>(Curve.GetName().ToString()));
		}

		Root->SetNumberField(TEXT("data_model_frame_count"), DataModel->GetNumberOfFrames());
		Root->SetNumberField(TEXT("data_model_key_count"), DataModel->GetNumberOfKeys());
		Root->SetNumberField(TEXT("track_count"), Names.Num());
		Root->SetArrayField(TEXT("track_names"), TrackNames);
		Root->SetArrayField(TEXT("float_curves"), FloatCurves);
		Root->SetArrayField(TEXT("transform_curves"), TransformCurves);
	}
	else
	{
		Root->SetNumberField(TEXT("track_count"), 0);
		Root->SetArrayField(TEXT("track_names"), TrackNames);
	}

	if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimationAsset))
	{
		Root->SetNumberField(TEXT("sampled_keys"), AnimSequence->GetNumberOfSampledKeys());
		Root->SetBoolField(TEXT("enable_root_motion"), AnimSequence->bEnableRootMotion);
		Root->SetBoolField(TEXT("force_root_lock"), AnimSequence->bForceRootLock);
		Root->SetBoolField(TEXT("use_normalized_root_motion_scale"), AnimSequence->bUseNormalizedRootMotionScale);
		Root->SetStringField(TEXT("root_motion_root_lock"), ProjectAnimationDiagnostics::EnumToString(TEXT("/Script/Engine.ERootMotionRootLock"), AnimSequence->RootMotionRootLock));
		Root->SetStringField(TEXT("bone_compression_settings"), AnimSequence->BoneCompressionSettings ? AnimSequence->BoneCompressionSettings.Get()->GetPathName() : TEXT(""));
		Root->SetStringField(TEXT("curve_compression_settings"), AnimSequence->CurveCompressionSettings ? AnimSequence->CurveCompressionSettings.Get()->GetPathName() : TEXT(""));
	}

	return ProjectAnimationDiagnostics::WriteJson(Root);
}

FString UProjectAnimationDiagnosticsLibrary::SnapshotAnimBlueprintForMesh(USkeletalMeshComponent* MeshComponent, const FString& NodeNameContainsCsv)
{
	if (!MeshComponent)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("MeshComponent is null."));
	}

	TSharedRef<FJsonObject> Root = ProjectAnimationDiagnostics::MakeRoot();
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("mesh_component"), MeshComponent);
	Root->SetStringField(TEXT("anim_class"), MeshComponent->GetAnimClass() ? MeshComponent->GetAnimClass()->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("post_process_instance_class"), MeshComponent->GetPostProcessInstance() ? MeshComponent->GetPostProcessInstance()->GetClass()->GetPathName() : TEXT(""));
	Root->SetObjectField(TEXT("anim_blueprint"), ProjectAnimationDiagnostics::SnapshotAnimBlueprint(ProjectAnimationDiagnostics::ResolveAnimBlueprintFromClass(MeshComponent->GetAnimClass()), NodeNameContainsCsv));
	Root->SetObjectField(TEXT("post_process_anim_blueprint"), ProjectAnimationDiagnostics::SnapshotAnimBlueprint(ProjectAnimationDiagnostics::ResolveAnimBlueprintFromClass(MeshComponent->GetPostProcessInstance() ? MeshComponent->GetPostProcessInstance()->GetClass() : nullptr), NodeNameContainsCsv));
	return ProjectAnimationDiagnostics::WriteJson(Root);
}

FString UProjectAnimationDiagnosticsLibrary::CompareMeshPoseToCurrentMontage(USkeletalMeshComponent* MeshComponent, const FString& BoneNameContainsCsv, const int32 MaxBones)
{
	if (!MeshComponent || !MeshComponent->GetAnimInstance())
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("MeshComponent or AnimInstance is null."));
	}

	UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance();
	UAnimMontage* Montage = AnimInstance->GetCurrentActiveMontage();
	if (!Montage)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("No current montage is active."));
	}

	const float MontagePosition = AnimInstance->Montage_GetPosition(Montage);
	UAnimSequenceBase* AnimationAsset = nullptr;
	float AnimationTime = MontagePosition;
	FName SlotName = NAME_None;

	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			if (!Segment.GetAnimReference())
			{
				continue;
			}

			if (Segment.IsInRange(MontagePosition) || !AnimationAsset)
			{
				AnimationAsset = Segment.GetAnimReference();
				AnimationTime = Segment.ConvertTrackPosToAnimPos(MontagePosition);
				SlotName = SlotTrack.SlotName;
				if (Segment.IsInRange(MontagePosition))
				{
					break;
				}
			}
		}

		if (AnimationAsset && !SlotName.IsNone())
		{
			break;
		}
	}

	FString Json = ProjectAnimationDiagnostics::CompareMeshPoseToAnimationAtTimeInternal(MeshComponent, AnimationAsset, AnimationTime, BoneNameContainsCsv, MaxBones);

	TSharedPtr<FJsonObject> ParsedRoot;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (FJsonSerializer::Deserialize(Reader, ParsedRoot) && ParsedRoot.IsValid())
	{
		ProjectAnimationDiagnostics::AddPathField(ParsedRoot.ToSharedRef(), TEXT("montage"), Montage);
		ParsedRoot->SetStringField(TEXT("slot_name"), SlotName.ToString());
		ParsedRoot->SetNumberField(TEXT("montage_position"), MontagePosition);
		ParsedRoot->SetNumberField(TEXT("animation_time_from_montage"), AnimationTime);
		return ProjectAnimationDiagnostics::WriteJson(ParsedRoot.ToSharedRef());
	}

	return Json;
}

FString UProjectAnimationDiagnosticsLibrary::CompareMeshPoseToAnimationAtTime(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* AnimationAsset, const float TimeSeconds, const FString& BoneNameContainsCsv, const int32 MaxBones)
{
	return ProjectAnimationDiagnostics::CompareMeshPoseToAnimationAtTimeInternal(MeshComponent, AnimationAsset, TimeSeconds, BoneNameContainsCsv, MaxBones);
}

FString UProjectAnimationDiagnosticsLibrary::RedistributeAdjacentBoneRotation(
	UAnimSequence* AnimationAsset,
	const FName LowerBoneName,
	const FName UpperBoneName,
	const float LowerBoneReferenceBlend)
{
	if (!AnimationAsset)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("AnimationAsset is null."));
	}

	if (LowerBoneName.IsNone() || UpperBoneName.IsNone() || LowerBoneName == UpperBoneName)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("Two distinct bone names are required."));
	}

	if (LowerBoneReferenceBlend <= 0.f || LowerBoneReferenceBlend >= 1.f)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("LowerBoneReferenceBlend must be between zero and one."));
	}

	USkeleton* Skeleton = AnimationAsset->GetSkeleton();
	const IAnimationDataModel* DataModel = AnimationAsset->GetDataModel();
	if (!Skeleton || !DataModel)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("Animation has no skeleton or data model."));
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const int32 LowerBoneIndex = RefSkeleton.FindBoneIndex(LowerBoneName);
	const int32 UpperBoneIndex = RefSkeleton.FindBoneIndex(UpperBoneName);
	if (LowerBoneIndex == INDEX_NONE || UpperBoneIndex == INDEX_NONE)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("One or both correction bones are absent from the animation skeleton."));
	}

	if (RefSkeleton.GetParentIndex(UpperBoneIndex) != LowerBoneIndex)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("UpperBoneName must be a direct child of LowerBoneName."));
	}

	TArray<FName> TrackNames;
	DataModel->GetBoneTrackNames(TrackNames);
	if (!TrackNames.Contains(LowerBoneName) || !TrackNames.Contains(UpperBoneName))
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("Both correction bones must have animation tracks."));
	}

	TArray<FTransform> LowerTransforms;
	TArray<FTransform> UpperTransforms;
	DataModel->GetBoneTrackTransforms(LowerBoneName, LowerTransforms);
	DataModel->GetBoneTrackTransforms(UpperBoneName, UpperTransforms);
	if (LowerTransforms.IsEmpty() || LowerTransforms.Num() != UpperTransforms.Num())
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("Correction bone track key counts are empty or mismatched."));
	}

	const FQuat LowerReferenceRotation = RefSkeleton.GetRefBonePose()[LowerBoneIndex].GetRotation().GetNormalized();
	double MaximumLowerRotationChangeDegrees = 0.0;
	double MaximumPreservedPairRotationErrorDegrees = 0.0;

	for (int32 KeyIndex = 0; KeyIndex < LowerTransforms.Num(); ++KeyIndex)
	{
		const FQuat OldLowerRotation = LowerTransforms[KeyIndex].GetRotation().GetNormalized();
		const FQuat OldUpperRotation = UpperTransforms[KeyIndex].GetRotation().GetNormalized();
		const FQuat OldPairRotation = (OldUpperRotation * OldLowerRotation).GetNormalized();

		const FQuat NewLowerRotation = FQuat::Slerp(
			OldLowerRotation,
			LowerReferenceRotation,
			LowerBoneReferenceBlend).GetNormalized();
		const FQuat NewUpperRotation = (
			OldPairRotation * NewLowerRotation.Inverse()).GetNormalized();
		const FQuat NewPairRotation = (NewUpperRotation * NewLowerRotation).GetNormalized();

		MaximumLowerRotationChangeDegrees = FMath::Max(
			MaximumLowerRotationChangeDegrees,
			FMath::RadiansToDegrees(OldLowerRotation.AngularDistance(NewLowerRotation)));
		MaximumPreservedPairRotationErrorDegrees = FMath::Max(
			MaximumPreservedPairRotationErrorDegrees,
			FMath::RadiansToDegrees(OldPairRotation.AngularDistance(NewPairRotation)));

		LowerTransforms[KeyIndex].SetRotation(NewLowerRotation);
		UpperTransforms[KeyIndex].SetRotation(NewUpperRotation);
	}

	auto SplitTransformKeys = [](
		const TArray<FTransform>& Transforms,
		TArray<FVector>& OutPositions,
		TArray<FQuat>& OutRotations,
		TArray<FVector>& OutScales)
	{
		OutPositions.Reserve(Transforms.Num());
		OutRotations.Reserve(Transforms.Num());
		OutScales.Reserve(Transforms.Num());
		for (const FTransform& Transform : Transforms)
		{
			OutPositions.Add(Transform.GetLocation());
			OutRotations.Add(Transform.GetRotation().GetNormalized());
			OutScales.Add(Transform.GetScale3D());
		}
	};

	TArray<FVector> LowerPositions;
	TArray<FQuat> LowerRotations;
	TArray<FVector> LowerScales;
	TArray<FVector> UpperPositions;
	TArray<FQuat> UpperRotations;
	TArray<FVector> UpperScales;
	SplitTransformKeys(LowerTransforms, LowerPositions, LowerRotations, LowerScales);
	SplitTransformKeys(UpperTransforms, UpperPositions, UpperRotations, UpperScales);

	AnimationAsset->Modify();
	IAnimationDataController& Controller = AnimationAsset->GetController();
	bool bLowerTrackUpdated = false;
	bool bUpperTrackUpdated = false;
	{
		IAnimationDataController::FScopedBracket Bracket(
			Controller,
			NSLOCTEXT("ProjectAnimationMigration", "RedistributeAdjacentBoneRotation", "Redistribute adjacent spine rotation"),
			false);
		bLowerTrackUpdated = Controller.SetBoneTrackKeys(
			LowerBoneName,
			LowerPositions,
			LowerRotations,
			LowerScales,
			false);
		bUpperTrackUpdated = Controller.SetBoneTrackKeys(
			UpperBoneName,
			UpperPositions,
			UpperRotations,
			UpperScales,
			false);
	}

	if (!bLowerTrackUpdated || !bUpperTrackUpdated)
	{
		return ProjectAnimationDiagnostics::ErrorJson(TEXT("Animation data controller rejected one or both corrected tracks."));
	}

	AnimationAsset->MarkPackageDirty();

	TSharedRef<FJsonObject> Root = ProjectAnimationDiagnostics::MakeRoot();
	Root->SetBoolField(TEXT("success"), true);
	ProjectAnimationDiagnostics::AddPathField(Root, TEXT("animation_asset"), AnimationAsset);
	Root->SetStringField(TEXT("lower_bone"), LowerBoneName.ToString());
	Root->SetStringField(TEXT("upper_bone"), UpperBoneName.ToString());
	Root->SetNumberField(TEXT("lower_bone_reference_blend"), LowerBoneReferenceBlend);
	Root->SetNumberField(TEXT("key_count"), LowerTransforms.Num());
	Root->SetNumberField(TEXT("maximum_lower_rotation_change_degrees"), MaximumLowerRotationChangeDegrees);
	Root->SetNumberField(TEXT("maximum_preserved_pair_rotation_error_degrees"), MaximumPreservedPairRotationErrorDegrees);
	return ProjectAnimationDiagnostics::WriteJson(Root);
}
