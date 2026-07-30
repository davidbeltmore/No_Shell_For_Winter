#include "EFCharacterCreationDazBridgeEditor.h"

#include "AnimGraphNode_ControlRig.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/MeshDeformer.h"
#include "Animation/MeshDeformerCollection.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EditorFramework/AssetImportData.h"
#include "EFCharacterCreationSettings.h"
#include "EFCharacterCreationTypes.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/FbxAssetImportData.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "Animation/MorphTarget.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCharacterCreationDazBridge, Log, All);

namespace EFCharacterCreationDazBridgeEditorPrivate
{
	static const FSoftObjectPath DualQuatMorphDeformerPath(
		TEXT("/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth.DG_DualQuatSkin_Morph_Cloth"));
	static const FSoftObjectPath DualQuatMorphCollectionPath(
		TEXT("/Game/DazToUnreal/Deformers/MDC_DazDualQuatMorph.MDC_DazDualQuatMorph"));
	static const TCHAR* JointControlAnimSuffix = TEXT("_JCMAnim");
	static const TCHAR* JointControlRigSuffix = TEXT("_JCM_CR");
	static const TCHAR* AuditReportDirectoryName = TEXT("ProjectMultipleMeshAudit");
	static const TCHAR* FemaleVisibleMeshObjectPath = TEXT("/Game/DazToUnreal/Female/Female.Female");
	static const TCHAR* MultipleCompatibilityMeshObjectPath = TEXT("/Game/DazToUnreal/Multiple/Multiple.Multiple");
	static const TCHAR* MaleMeshObjectPath = TEXT("/Game/DazToUnreal/Male/Male.Male");

	static const TArray<FString> GeograftHintTokens = {
		TEXT("goldenpalace"),
		TEXT("geograft"),
		TEXT("graft"),
		TEXT("genital"),
		TEXT("anatomy"),
		TEXT("shell"),
		TEXT("majora"),
		TEXT("minora"),
		TEXT("vulva"),
		TEXT("penis"),
		TEXT("scrot"),
		TEXT("gp_")
	};

	static const TArray<FString> PelvisCorrectiveHintTokens = {
		TEXT("pelvis"),
		TEXT("hip"),
		TEXT("thigh"),
		TEXT("leg"),
		TEXT("glute"),
		TEXT("groin"),
		TEXT("crotch"),
		TEXT("genital"),
		TEXT("pubic"),
		TEXT("butt"),
		TEXT("anus"),
		TEXT("labia"),
		TEXT("vagina"),
		TEXT("penis"),
		TEXT("upperleg")
	};

	struct FDazMeshCorrectiveAuditRecord
	{
		TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
		FString MeshPath;
		bool bIsGeograftLike = false;
		bool bHasTargetCollection = false;
		bool bHasExpectedDefaultDeformer = false;
		bool bHasPostProcessAnimBlueprint = false;
		FString PostProcessAnimBlueprintPath;
		bool bHasDTUFile = false;
		FString DTUPath;
		bool bHasAutoJCMImportData = false;
		int32 TotalMorphCount = 0;
		int32 TotalDQ2LBMorphCount = 0;
		int32 RelevantDQ2LBMorphCount = 0;
		int32 RelevantJointLinkCount = 0;
		int32 RelevantJointLinksWithDQ2LB = 0;
		TArray<FString> RelevantDQ2LBMorphNames;
		TArray<FString> MissingRelevantDQ2LBMorphNames;
		FString Recommendation;
	};

	static bool ContainsAnyToken(const FString& Source, const TArray<FString>& Tokens)
	{
		for (const FString& Token : Tokens)
		{
			if (!Token.IsEmpty() && Source.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	static FString BuildCompanionAssetObjectPath(USkeletalMesh* SkeletalMesh, const TCHAR* Suffix)
	{
		if (!IsValid(SkeletalMesh))
		{
			return FString();
		}

		const FString PackagePath = SkeletalMesh->GetOutermost()->GetName();
		const FString FolderPath = FPackageName::GetLongPackagePath(PackagePath);
		const FString AssetName = SkeletalMesh->GetName() + Suffix;
		return FolderPath / AssetName + TEXT(".") + AssetName;
	}

	static bool IsGeograftLikeMesh(const FString& MeshPath)
	{
		return ContainsAnyToken(MeshPath, GeograftHintTokens);
	}

	static bool IsRelevantPelvisCorrective(const FString& Name)
	{
		return ContainsAnyToken(Name, PelvisCorrectiveHintTokens);
	}

	static TArray<FName> GatherMorphNames(USkeletalMesh* SkeletalMesh)
	{
		TArray<FName> MorphNames;
		if (!IsValid(SkeletalMesh))
		{
			return MorphNames;
		}

		for (const TPair<FName, int32>& MorphTargetPair : SkeletalMesh->GetMorphTargetIndexMap())
		{
			MorphNames.AddUnique(MorphTargetPair.Key);
		}

		for (const TObjectPtr<UMorphTarget>& MorphTarget : SkeletalMesh->GetMorphTargets())
		{
			if (IsValid(MorphTarget))
			{
				MorphNames.AddUnique(MorphTarget->GetFName());
			}
		}

#if WITH_EDITOR
		if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
		{
			TArray<FName> CurveNames;
			Skeleton->GetCurveMetaDataNames(CurveNames);
			for (const FName CurveName : CurveNames)
			{
				if (Skeleton->GetCurveMetaDataMorphTarget(CurveName))
				{
					MorphNames.AddUnique(CurveName);
				}
			}
		}
#endif

		MorphNames.Sort(FNameLexicalLess());
		return MorphNames;
	}

	static UAnimBlueprint* ResolveAssignedJointControlBlueprint(USkeletalMesh* SkeletalMesh)
	{
		if (!IsValid(SkeletalMesh))
		{
			return nullptr;
		}

		if (UClass* PostProcessClass = SkeletalMesh->GetPostProcessAnimBlueprint())
		{
			return Cast<UAnimBlueprint>(PostProcessClass->ClassGeneratedBy);
		}

		return nullptr;
	}

	static UAnimBlueprint* ResolveExistingJointControlBlueprint(USkeletalMesh* SkeletalMesh)
	{
		if (!IsValid(SkeletalMesh))
		{
			return nullptr;
		}

		if (UAnimBlueprint* AssignedBlueprint = ResolveAssignedJointControlBlueprint(SkeletalMesh))
		{
			return AssignedBlueprint;
		}

		const FString BlueprintObjectPath = BuildCompanionAssetObjectPath(SkeletalMesh, JointControlAnimSuffix);
		return BlueprintObjectPath.IsEmpty()
			? nullptr
			: LoadObject<UAnimBlueprint>(nullptr, *BlueprintObjectPath);
	}

	static bool LoadDtuJsonForMesh(
		USkeletalMesh* SkeletalMesh,
		FString& OutDtuPath,
		TSharedPtr<FJsonObject>& OutJsonObject,
		FString& OutError)
	{
		OutDtuPath.Reset();
		if (IsValid(SkeletalMesh))
		{
			if (UAssetImportData* AssetImportData = SkeletalMesh->GetAssetImportData())
			{
				if (UFbxAssetImportData* FbxAssetImportData = Cast<UFbxAssetImportData>(AssetImportData))
				{
					for (const FAssetImportInfo::FSourceFile& SourceFile : FbxAssetImportData->GetSourceData().SourceFiles)
					{
						const FString SourceFilePath = SourceFile.RelativeFilename;
						TArray<FString> LikelyPaths;
						LikelyPaths.Add(FPaths::ChangeExtension(SourceFilePath, TEXT("dtu")));
						LikelyPaths.Add(
							FPaths::GetPath(SourceFilePath) +
							TEXT("/../") +
							FPaths::ChangeExtension(FPaths::GetCleanFilename(SourceFilePath), TEXT("dtu")));

						for (const FString& PossiblePath : LikelyPaths)
						{
							if (FPaths::FileExists(PossiblePath))
							{
								OutDtuPath = PossiblePath;
								break;
							}
						}

						if (!OutDtuPath.IsEmpty())
						{
							break;
						}
					}
				}
			}
		}

		if (OutDtuPath.IsEmpty())
		{
			OutError = TEXT("No DTU file could be resolved from the mesh import data.");
			return false;
		}

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *OutDtuPath))
		{
			OutError = FString::Printf(TEXT("Could not read DTU file: %s"), *OutDtuPath);
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		OutJsonObject.Reset();
		if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Could not parse DTU JSON: %s"), *OutDtuPath);
			return false;
		}

		return true;
	}

	static bool HasAutoJCMImportData(const TSharedPtr<FJsonObject>& JsonObject)
	{
#if ENGINE_MAJOR_VERSION > 4
		const TArray<TSharedPtr<FJsonValue>>* JointLinks = nullptr;
		return JsonObject.IsValid() && JsonObject->TryGetArrayField(TEXT("JointLinks"), JointLinks);
#else
		return false;
#endif
	}

	static UAnimBlueprint* CreateLocalJointControlBlueprint(
		UObject* InParent,
		const FName BlueprintName,
		USkeleton* Skeleton)
	{
		if (!InParent || !Skeleton)
		{
			return nullptr;
		}

		UClass* ClassToUse = UAnimInstance::StaticClass();
		UAnimBlueprint* NewBlueprint = CastChecked<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			ClassToUse,
			InParent,
			BlueprintName,
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None));
		if (!NewBlueprint)
		{
			return nullptr;
		}

		NewBlueprint->TargetSkeleton = Skeleton;

		if (UAnimBlueprintGeneratedClass* GeneratedClass = Cast<UAnimBlueprintGeneratedClass>(NewBlueprint->GeneratedClass))
		{
			GeneratedClass->TargetSkeleton = Skeleton;
		}
		if (UAnimBlueprintGeneratedClass* SkeletonGeneratedClass = Cast<UAnimBlueprintGeneratedClass>(NewBlueprint->SkeletonGeneratedClass))
		{
			SkeletonGeneratedClass->TargetSkeleton = Skeleton;
		}

#if ENGINE_MAJOR_VERSION > 4
		TArray<UEdGraph*> Graphs;
		NewBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || Graph->GetFName() != UEdGraphSchema_K2::GN_AnimGraph)
			{
				continue;
			}

			UEdGraphNode* OutputNode = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				OutputNode = Node;
			}

			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewBlueprint);
			}

			UAnimGraphNode_LinkedInputPose* InputTemplate = NewObject<UAnimGraphNode_LinkedInputPose>();
			UEdGraphNode* InputNode = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UAnimGraphNode_LinkedInputPose>(
				Graph,
				InputTemplate,
				FVector2D(0.0f, 0.0f),
				false);

			UAnimGraphNode_ControlRig* ControlRigTemplate = NewObject<UAnimGraphNode_ControlRig>();
			UEdGraphNode* ControlRigNode = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UAnimGraphNode_ControlRig>(
				Graph,
				ControlRigTemplate,
				FVector2D(0.0f, 0.0f),
				false);

			if (!InputNode || !OutputNode || !ControlRigNode)
			{
				continue;
			}

			UEdGraphPin* NextPin = InputNode->FindPin(TEXT("Pose"));
			UEdGraphPin* ControlRigSourcePin = ControlRigNode->FindPin(TEXT("Source"));
			UEdGraphPin* ControlRigPosePin = ControlRigNode->FindPin(TEXT("Pose"));
			UEdGraphPin* OutputPin = OutputNode->FindPin(TEXT("Result"));
			if (NextPin && ControlRigSourcePin && ControlRigPosePin && OutputPin)
			{
				NextPin->MakeLinkTo(ControlRigSourcePin);
				ControlRigPosePin->MakeLinkTo(OutputPin);
			}
		}
#endif

		return NewBlueprint;
	}

	static UAnimBlueprint* CreateJointControlAnimationFromDtu(
		const TSharedPtr<FJsonObject>& JsonObject,
		USkeletalMesh* SkeletalMesh)
	{
		if (!HasAutoJCMImportData(JsonObject) || !IsValid(SkeletalMesh))
		{
			return nullptr;
		}

		USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
		if (!IsValid(Skeleton))
		{
			return nullptr;
		}

		const FString AssetName = SkeletalMesh->GetName() + JointControlAnimSuffix;
		const FString PackageName = FPackageName::GetLongPackagePath(SkeletalMesh->GetOutermost()->GetName()) / AssetName;
		UPackage* NewPackage = CreatePackage(*PackageName);
		if (!NewPackage)
		{
			return nullptr;
		}

		UAnimBlueprint* AnimBlueprint = CreateLocalJointControlBlueprint(NewPackage, FName(*AssetName), Skeleton);
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		return AnimBlueprint;
	}

	static void AnalyzeJointLinks(
		const TSharedPtr<FJsonObject>& DtuJsonObject,
		const TSet<FString>& LowerMorphNameSet,
		FDazMeshCorrectiveAuditRecord& InOutRecord)
	{
		if (!DtuJsonObject.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* JointLinks = nullptr;
		if (!DtuJsonObject->TryGetArrayField(TEXT("JointLinks"), JointLinks) || !JointLinks)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& JointLinkValue : *JointLinks)
		{
			const TSharedPtr<FJsonObject> JointLink = JointLinkValue.IsValid() ? JointLinkValue->AsObject() : nullptr;
			if (!JointLink.IsValid())
			{
				continue;
			}

			FString BoneName;
			FString MorphName;
			if (!JointLink->TryGetStringField(TEXT("Bone"), BoneName) ||
				!JointLink->TryGetStringField(TEXT("Morph"), MorphName))
			{
				continue;
			}

			const FString LowerBoneName = BoneName.ToLower();
			const FString LowerMorphName = MorphName.ToLower();
			if (!IsRelevantPelvisCorrective(LowerBoneName) && !IsRelevantPelvisCorrective(LowerMorphName))
			{
				continue;
			}

			++InOutRecord.RelevantJointLinkCount;

			const FString DQ2LBName = MorphName + TEXT("_dq2lb");
			if (LowerMorphNameSet.Contains(DQ2LBName.ToLower()))
			{
				++InOutRecord.RelevantJointLinksWithDQ2LB;
			}
			else
			{
				InOutRecord.MissingRelevantDQ2LBMorphNames.AddUnique(DQ2LBName);
			}
		}
	}

	static void AddConfiguredBodyMeshes(
		const UEFCharacterCreationSettings* Settings,
		TSet<TObjectPtr<USkeletalMesh>>& InOutCandidateMeshes)
	{
		if (!Settings)
		{
			return;
		}

		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: loading %d configured body mesh options."), Settings->BodyMeshOptions.Num());

		for (const FCharacterSkeletalMeshOption& Option : Settings->BodyMeshOptions)
		{
			if (USkeletalMesh* SkeletalMesh = Option.SkeletalMesh.LoadSynchronous())
			{
				InOutCandidateMeshes.Add(SkeletalMesh);
			}
		}

		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: configured body mesh pass produced %d candidates."), InOutCandidateMeshes.Num());
	}

	static void AddScannedDazMeshes(
		const UEFCharacterCreationSettings* Settings,
		TSet<TObjectPtr<USkeletalMesh>>& InOutCandidateMeshes)
	{
		if (!Settings || Settings->DazPathTokens.Num() == 0)
		{
			return;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		FARFilter Filter;
		Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> SkeletalMeshAssets;
		AssetRegistryModule.Get().GetAssets(Filter, SkeletalMeshAssets);
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: scanning %d skeletal mesh assets from Asset Registry."), SkeletalMeshAssets.Num());

		TArray<FAssetData> MatchingSkeletalMeshAssets;
		MatchingSkeletalMeshAssets.Reserve(SkeletalMeshAssets.Num());

		for (const FAssetData& AssetData : SkeletalMeshAssets)
		{
			const FString PackageName = AssetData.PackageName.ToString();
			if (!PackageName.StartsWith(TEXT("/Game/")))
			{
				continue;
			}

			const FString AssetPath = AssetData.GetSoftObjectPath().ToString();
			if (!ContainsAnyToken(PackageName, Settings->DazPathTokens) &&
				!ContainsAnyToken(AssetPath, Settings->DazPathTokens))
			{
				continue;
			}

			MatchingSkeletalMeshAssets.Add(AssetData);
		}

		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: %d skeletal meshes matched Daz path tokens."), MatchingSkeletalMeshAssets.Num());

		for (int32 AssetIndex = 0; AssetIndex < MatchingSkeletalMeshAssets.Num(); ++AssetIndex)
		{
			if ((AssetIndex % 25) == 0)
			{
				UE_LOG(
					LogEFCharacterCreationDazBridge,
					Log,
					TEXT("DQ sync: loading matched skeletal mesh %d/%d."),
					AssetIndex + 1,
					MatchingSkeletalMeshAssets.Num());
			}

			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MatchingSkeletalMeshAssets[AssetIndex].GetAsset()))
			{
				InOutCandidateMeshes.Add(SkeletalMesh);
			}
		}

		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: registry scan finished with %d total unique candidates."), InOutCandidateMeshes.Num());
	}

	static void AddCoreProjectDazMeshes(TSet<TObjectPtr<USkeletalMesh>>& InOutCandidateMeshes)
	{
		static const TCHAR* CoreMeshPaths[] =
		{
			FemaleVisibleMeshObjectPath,
			MultipleCompatibilityMeshObjectPath,
			MaleMeshObjectPath
		};

		for (const TCHAR* CoreMeshPath : CoreMeshPaths)
		{
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(FSoftObjectPath(CoreMeshPath).TryLoad()))
			{
				InOutCandidateMeshes.Add(SkeletalMesh);
			}
			else
			{
				UE_LOG(LogEFCharacterCreationDazBridge, Warning, TEXT("DQ audit: core project Daz mesh could not be loaded: %s"), CoreMeshPath);
			}
		}
	}

	static FString FormatAuditRecordForReport(const FDazMeshCorrectiveAuditRecord& Record)
	{
		return FString::Printf(
			TEXT("Mesh=%s | GeograftLike=%s | TargetCollection=%s | DQDefault=%s | PostProcess=%s | DTU=%s | AutoJCM=%s | Morphs=%d | DQ2LB=%d | RelevantDQ2LB=%d | RelevantJointLinks=%d | RelevantLinksWithDQ2LB=%d | Recommendation=%s"),
			*Record.MeshPath,
			Record.bIsGeograftLike ? TEXT("true") : TEXT("false"),
			Record.bHasTargetCollection ? TEXT("true") : TEXT("false"),
			Record.bHasExpectedDefaultDeformer ? TEXT("true") : TEXT("false"),
			Record.bHasPostProcessAnimBlueprint ? *Record.PostProcessAnimBlueprintPath : TEXT("missing"),
			Record.bHasDTUFile ? *Record.DTUPath : TEXT("missing"),
			Record.bHasAutoJCMImportData ? TEXT("true") : TEXT("false"),
			Record.TotalMorphCount,
			Record.TotalDQ2LBMorphCount,
			Record.RelevantDQ2LBMorphCount,
			Record.RelevantJointLinkCount,
			Record.RelevantJointLinksWithDQ2LB,
			*Record.Recommendation);
	}

	static bool WriteAuditReport(const FString& ReportStem, const TArray<FString>& Lines, FString& OutReportPath)
	{
		OutReportPath.Reset();

		const FString ReportDirectory = FPaths::ProjectSavedDir() / AuditReportDirectoryName;
		IFileManager::Get().MakeDirectory(*ReportDirectory, true);

		OutReportPath = ReportDirectory / FString::Printf(
			TEXT("%s_%s.txt"),
			*ReportStem,
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

		return FFileHelper::SaveStringArrayToFile(Lines, *OutReportPath);
	}

	static UMeshDeformerCollection* ResolveOrCreateDualQuatMorphCollection(bool& bOutCreated, bool& bOutUpdated)
	{
		bOutCreated = false;
		bOutUpdated = false;

		UMeshDeformerCollection* Collection = Cast<UMeshDeformerCollection>(
			DualQuatMorphCollectionPath.TryLoad());
		if (!Collection)
		{
			UPackage* Package = CreatePackage(*DualQuatMorphCollectionPath.GetLongPackageName());
			if (!Package)
			{
				return nullptr;
			}

			Collection = NewObject<UMeshDeformerCollection>(
				Package,
				UMeshDeformerCollection::StaticClass(),
				*DualQuatMorphCollectionPath.GetAssetName(),
				RF_Public | RF_Standalone);
			if (!Collection)
			{
				return nullptr;
			}

#if WITH_EDITORONLY_DATA
			Collection->Description = TEXT("Project-owned collection for the official UE5 Dual Quaternion + morph deformer.");
#endif

			FAssetRegistryModule::AssetCreated(Collection);
			Collection->MarkPackageDirty();
			bOutCreated = true;
			bOutUpdated = true;
		}

		const UMeshDeformer* DualQuatMorphDeformer = Cast<UMeshDeformer>(DualQuatMorphDeformerPath.TryLoad());
		if (!DualQuatMorphDeformer)
		{
			return nullptr;
		}

		const bool bHasOfficialDeformer = Collection->MeshDeformers.ContainsByPredicate(
			[](const TSoftObjectPtr<UMeshDeformer>& ExistingDeformer)
			{
				return ExistingDeformer.ToSoftObjectPath() == DualQuatMorphDeformerPath;
			});

		if (!bHasOfficialDeformer)
		{
			Collection->Modify();
			Collection->MeshDeformers.Add(TSoftObjectPtr<UMeshDeformer>(DualQuatMorphDeformerPath));
			Collection->MarkPackageDirty();
			bOutUpdated = true;
		}

		return Collection;
	}

	static bool SaveAssetIfDirty(UObject* Asset)
	{
		if (!IsValid(Asset))
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package || !Package->IsDirty())
		{
			return true;
		}

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bWarnOfLongFilename = false;
		SaveArgs.bSlowTask = false;
		SaveArgs.Error = GError;
		return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
	}

	static FString JoinLimited(const TArray<FString>& Values, const int32 MaxItems = 6)
	{
		TArray<FString> LimitedValues;
		const int32 SafeMaxItems = FMath::Max(MaxItems, 0);
		for (int32 Index = 0; Index < Values.Num() && Index < SafeMaxItems; ++Index)
		{
			LimitedValues.Add(Values[Index]);
		}

		FString Result = FString::Join(LimitedValues, TEXT(", "));
		if (Values.Num() > SafeMaxItems)
		{
			Result += FString::Printf(TEXT(", ... (+%d more)"), Values.Num() - SafeMaxItems);
		}
		return Result;
	}

	static FDazMeshCorrectiveAuditRecord BuildAuditRecord(
		USkeletalMesh* SkeletalMesh,
		UMeshDeformerCollection* Collection,
		UMeshDeformer* DualQuatMorphDeformer)
	{
		FDazMeshCorrectiveAuditRecord Record;
		if (!IsValid(SkeletalMesh))
		{
			return Record;
		}

		Record.SkeletalMesh = SkeletalMesh;
		Record.MeshPath = SkeletalMesh->GetPathName();
		Record.bIsGeograftLike = IsGeograftLikeMesh(Record.MeshPath);
		Record.bHasTargetCollection = Collection && SkeletalMesh->GetTargetMeshDeformers() == Collection;
		Record.bHasExpectedDefaultDeformer = DualQuatMorphDeformer && SkeletalMesh->GetDefaultMeshDeformer() == DualQuatMorphDeformer;

		if (UAnimBlueprint* AssignedBlueprint = ResolveAssignedJointControlBlueprint(SkeletalMesh))
		{
			Record.bHasPostProcessAnimBlueprint = true;
			Record.PostProcessAnimBlueprintPath = AssignedBlueprint->GetPathName();
		}
		else if (UClass* PostProcessClass = SkeletalMesh->GetPostProcessAnimBlueprint())
		{
			Record.bHasPostProcessAnimBlueprint = true;
			Record.PostProcessAnimBlueprintPath = PostProcessClass->GetPathName();
		}

		const TArray<FName> MorphNames = GatherMorphNames(SkeletalMesh);
		Record.TotalMorphCount = MorphNames.Num();

		TSet<FString> LowerMorphNameSet;
		for (const FName MorphName : MorphNames)
		{
			const FString MorphString = MorphName.ToString();
			const FString LowerMorphString = MorphString.ToLower();
			LowerMorphNameSet.Add(LowerMorphString);

			if (!LowerMorphString.EndsWith(TEXT("_dq2lb")))
			{
				continue;
			}

			++Record.TotalDQ2LBMorphCount;
			if (IsRelevantPelvisCorrective(LowerMorphString))
			{
				++Record.RelevantDQ2LBMorphCount;
				Record.RelevantDQ2LBMorphNames.Add(MorphString);
			}
		}

		FString DtuError;
		TSharedPtr<FJsonObject> DtuJsonObject;
		Record.bHasDTUFile = LoadDtuJsonForMesh(SkeletalMesh, Record.DTUPath, DtuJsonObject, DtuError);
		Record.bHasAutoJCMImportData = Record.bHasDTUFile && HasAutoJCMImportData(DtuJsonObject);
		if (Record.bHasAutoJCMImportData)
		{
			AnalyzeJointLinks(DtuJsonObject, LowerMorphNameSet, Record);
		}

		if (!Record.bHasTargetCollection || !Record.bHasExpectedDefaultDeformer)
		{
			Record.Recommendation = TEXT("Repair can patch the mesh-level Dual Quaternion deformer setup.");
		}
		else if ((Record.RelevantDQ2LBMorphCount > 0 || Record.RelevantJointLinksWithDQ2LB > 0) && !Record.bHasPostProcessAnimBlueprint)
		{
			Record.Recommendation = Record.bHasAutoJCMImportData
				? TEXT("Repair can create or reattach the AutoJCM post process for this mesh.")
				: TEXT("Corrective morphs exist, but AutoJCM import data is missing; manual post-process assignment is required.");
		}
		else if (Record.RelevantJointLinkCount > 0 && Record.RelevantJointLinksWithDQ2LB == 0)
		{
			Record.Recommendation = Record.bIsGeograftLike
				? TEXT("This geograft is missing relevant *_dq2lb correctives; fallback to linear skinning on this mesh is recommended.")
				: TEXT("The body mesh is missing relevant *_dq2lb correctives; a manual corrective morph is required for the pelvis pose.");
		}
		else if (Record.bIsGeograftLike && Record.RelevantDQ2LBMorphCount == 0 && Record.RelevantJointLinksWithDQ2LB == 0)
		{
			Record.Recommendation = TEXT("Geograft has no proven DQ-aware pelvis correctives; keep DQ off on this mesh if deformation is unstable.");
		}
		else if (Record.bHasPostProcessAnimBlueprint)
		{
			Record.Recommendation = TEXT("DQ + JCM wiring looks present; validate the bad pose in editor and compare body vs geograft.");
		}
		else
		{
			Record.Recommendation = TEXT("No relevant pelvis DQ corrective evidence was found; inspect the DTU export and mesh morph content.");
		}

		return Record;
	}

	static void LogAuditRecord(const FDazMeshCorrectiveAuditRecord& Record)
	{
		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Log,
			TEXT("DQ audit: Mesh=%s GeograftLike=%s DQDefault=%s TargetCollection=%s PostProcess=%s DTU=%s AutoJCM=%s TotalMorphs=%d DQ2LB=%d RelevantDQ2LB=%d RelevantJointLinks=%d RelevantJointLinksWithDQ2LB=%d Recommendation=\"%s\""),
			*Record.MeshPath,
			Record.bIsGeograftLike ? TEXT("true") : TEXT("false"),
			Record.bHasExpectedDefaultDeformer ? TEXT("true") : TEXT("false"),
			Record.bHasTargetCollection ? TEXT("true") : TEXT("false"),
			Record.bHasPostProcessAnimBlueprint ? *Record.PostProcessAnimBlueprintPath : TEXT("missing"),
			Record.bHasDTUFile ? *Record.DTUPath : TEXT("missing"),
			Record.bHasAutoJCMImportData ? TEXT("true") : TEXT("false"),
			Record.TotalMorphCount,
			Record.TotalDQ2LBMorphCount,
			Record.RelevantDQ2LBMorphCount,
			Record.RelevantJointLinkCount,
			Record.RelevantJointLinksWithDQ2LB,
			*Record.Recommendation);

		if (Record.RelevantDQ2LBMorphNames.Num() > 0)
		{
			UE_LOG(
				LogEFCharacterCreationDazBridge,
				Log,
				TEXT("DQ audit: relevant present dq2lb morphs for %s -> %s"),
				*Record.MeshPath,
				*JoinLimited(Record.RelevantDQ2LBMorphNames));
		}

		if (Record.MissingRelevantDQ2LBMorphNames.Num() > 0)
		{
			UE_LOG(
				LogEFCharacterCreationDazBridge,
				Warning,
				TEXT("DQ audit: relevant missing dq2lb morphs for %s -> %s"),
				*Record.MeshPath,
				*JoinLimited(Record.MissingRelevantDQ2LBMorphNames));
		}
	}

	static bool EnsureMeshDeformerState(
		USkeletalMesh* SkeletalMesh,
		UMeshDeformerCollection* Collection,
		UMeshDeformer* DualQuatMorphDeformer,
		const bool bEnableDQDefault,
		TArray<UObject*>& InOutAssetsToSave)
	{
		if (!IsValid(SkeletalMesh))
		{
			return false;
		}

		bool bChanged = false;
		SkeletalMesh->Modify();

		if (Collection && SkeletalMesh->GetTargetMeshDeformers() != Collection)
		{
			SkeletalMesh->SetTargetMeshDeformers(Collection);
			bChanged = true;
		}

		if (bEnableDQDefault)
		{
			if (DualQuatMorphDeformer && SkeletalMesh->GetDefaultMeshDeformer() != DualQuatMorphDeformer)
			{
				SkeletalMesh->SetDefaultMeshDeformer(DualQuatMorphDeformer);
				bChanged = true;
			}
		}
		else if (SkeletalMesh->GetDefaultMeshDeformer() != nullptr)
		{
			SkeletalMesh->SetDefaultMeshDeformer(nullptr);
			bChanged = true;
		}

		if (bChanged)
		{
			SkeletalMesh->MarkPackageDirty();
			InOutAssetsToSave.AddUnique(SkeletalMesh);
		}

		return bChanged;
	}

	static bool EnsureAutoJCMPostProcessForMesh(
		USkeletalMesh* SkeletalMesh,
		const FDazMeshCorrectiveAuditRecord& AuditRecord,
		TArray<UObject*>& InOutAssetsToSave,
		FString& OutRepairNote)
	{
		OutRepairNote.Reset();
		if (!IsValid(SkeletalMesh))
		{
			OutRepairNote = TEXT("Skipped invalid skeletal mesh.");
			return false;
		}

		FString DtuPath = AuditRecord.DTUPath;
		TSharedPtr<FJsonObject> DtuJsonObject;
		FString DtuError;
		if (!AuditRecord.bHasAutoJCMImportData &&
			!LoadDtuJsonForMesh(SkeletalMesh, DtuPath, DtuJsonObject, DtuError))
		{
			OutRepairNote = DtuError;
			return false;
		}

		if (!DtuJsonObject.IsValid())
		{
			if (!LoadDtuJsonForMesh(SkeletalMesh, DtuPath, DtuJsonObject, DtuError))
			{
				OutRepairNote = DtuError;
				return false;
			}
		}

		if (!HasAutoJCMImportData(DtuJsonObject))
		{
			OutRepairNote = TEXT("DTU does not contain AutoJCM JointLinks data.");
			return false;
		}

		UAnimBlueprint* JointControlBlueprint = ResolveExistingJointControlBlueprint(SkeletalMesh);
		bool bCreatedBlueprint = false;
		if (!JointControlBlueprint)
		{
			USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
			if (!IsValid(Skeleton))
			{
				OutRepairNote = TEXT("Skeletal mesh has no valid skeleton for AutoJCM setup.");
				return false;
			}

			const FString MeshPackageName = SkeletalMesh->GetOutermost()->GetName();
			const FString FolderPath = FPackageName::GetLongPackagePath(MeshPackageName);
			JointControlBlueprint = CreateJointControlAnimationFromDtu(DtuJsonObject, SkeletalMesh);
			bCreatedBlueprint = IsValid(JointControlBlueprint);
		}

		if (!IsValid(JointControlBlueprint))
		{
			OutRepairNote = TEXT("Could not resolve or create the JCM AnimBlueprint.");
			return false;
		}

		FKismetEditorUtilities::CompileBlueprint(JointControlBlueprint);
		InOutAssetsToSave.AddUnique(JointControlBlueprint);

		const FString ControlRigObjectPath = BuildCompanionAssetObjectPath(SkeletalMesh, JointControlRigSuffix);
		UObject* ExistingControlRig = ControlRigObjectPath.IsEmpty()
			? nullptr
			: LoadObject<UObject>(nullptr, *ControlRigObjectPath);

		if (!ExistingControlRig && GEngine)
		{
			const FString SkeletalMeshPackagePath = SkeletalMesh->GetOutermost()->GetPathName() + TEXT(".") + SkeletalMesh->GetName();
			const FString PostProcessAnimPackagePath = JointControlBlueprint->GetOutermost()->GetPathName() + TEXT(".") + JointControlBlueprint->GetName();
			const FString CreateJCMControlRigCommand = FString::Format(
				TEXT("py CreateAutoJCMControlRig.py --skeletalMesh={0} --animBlueprint={1} --dtuFile=\"{2}\""),
				{ SkeletalMeshPackagePath, PostProcessAnimPackagePath, DtuPath });

			UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ repair: executing AutoJCM repair command: %s"), *CreateJCMControlRigCommand);
			GEngine->Exec(nullptr, *CreateJCMControlRigCommand);
			ExistingControlRig = LoadObject<UObject>(nullptr, *ControlRigObjectPath);
		}

		if (ExistingControlRig)
		{
			InOutAssetsToSave.AddUnique(ExistingControlRig);
		}

		if (UClass* GeneratedClass = JointControlBlueprint->GetAnimBlueprintGeneratedClass())
		{
			if (SkeletalMesh->GetPostProcessAnimBlueprint() != GeneratedClass)
			{
				SkeletalMesh->Modify();
				SkeletalMesh->SetPostProcessAnimBlueprint(GeneratedClass);
				SkeletalMesh->MarkPackageDirty();
				InOutAssetsToSave.AddUnique(SkeletalMesh);
			}
		}

		OutRepairNote = bCreatedBlueprint
			? TEXT("Created or refreshed AutoJCM assets and reattached the post process.")
			: TEXT("Reattached existing AutoJCM post process assets.");
		return true;
	}

	static void CollectCandidateMeshes(TSet<TObjectPtr<USkeletalMesh>>& OutCandidateMeshes)
	{
		OutCandidateMeshes.Reset();
		AddCoreProjectDazMeshes(OutCandidateMeshes);

		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Log,
			TEXT("DQ audit: restricted candidate scope to Female, Male, and Multiple project meshes only. CandidateMeshes=%d"),
			OutCandidateMeshes.Num());
	}

	static bool SyncDazDualQuatMorphAssets(FString& OutSummary)
	{
		OutSummary = TEXT("Dual Quaternion sync did not run.");
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: starting."));

		UMeshDeformer* DualQuatMorphDeformer = Cast<UMeshDeformer>(DualQuatMorphDeformerPath.TryLoad());
		if (!DualQuatMorphDeformer)
		{
			OutSummary = FString::Printf(
				TEXT("Could not load official Dual Quaternion deformer asset: %s"),
				*DualQuatMorphDeformerPath.ToString());
			return false;
		}
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ sync: official deformer asset loaded."));

		bool bCreatedCollection = false;
		bool bUpdatedCollection = false;
		UMeshDeformerCollection* Collection = ResolveOrCreateDualQuatMorphCollection(bCreatedCollection, bUpdatedCollection);
		if (!Collection)
		{
			OutSummary = TEXT("Could not create or load the project mesh deformer collection.");
			return false;
		}
		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Log,
			TEXT("DQ sync: collection ready. Created=%s Updated=%s"),
			bCreatedCollection ? TEXT("true") : TEXT("false"),
			bUpdatedCollection ? TEXT("true") : TEXT("false"));

		TSet<TObjectPtr<USkeletalMesh>> CandidateMeshes;
		CollectCandidateMeshes(CandidateMeshes);

		int32 UpdatedMeshCount = 0;
		int32 SavedAssetCount = 0;
		TArray<UObject*> AssetsToSave;

		if (bUpdatedCollection)
		{
			AssetsToSave.Add(Collection);
		}

		for (USkeletalMesh* SkeletalMesh : CandidateMeshes)
		{
			if (!IsValid(SkeletalMesh))
			{
				continue;
			}

			const bool bNeedsTargetCollection = SkeletalMesh->GetTargetMeshDeformers() != Collection;
			const bool bNeedsDefaultDeformer = SkeletalMesh->GetDefaultMeshDeformer() != DualQuatMorphDeformer;
			if (!bNeedsTargetCollection && !bNeedsDefaultDeformer)
			{
				continue;
			}

			SkeletalMesh->Modify();
			if (bNeedsTargetCollection)
			{
				SkeletalMesh->SetTargetMeshDeformers(Collection);
			}
			if (bNeedsDefaultDeformer)
			{
				SkeletalMesh->SetDefaultMeshDeformer(DualQuatMorphDeformer);
			}
			SkeletalMesh->MarkPackageDirty();
			AssetsToSave.AddUnique(SkeletalMesh);
			UpdatedMeshCount++;
		}

		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Log,
			TEXT("DQ sync: candidate patch pass finished. Candidates=%d UpdatedMeshes=%d AssetsToSave=%d. DefaultMeshDeformer is now patched at mesh asset level."),
			CandidateMeshes.Num(),
			UpdatedMeshCount,
			AssetsToSave.Num());

		for (int32 AssetIndex = 0; AssetIndex < AssetsToSave.Num(); ++AssetIndex)
		{
			if ((AssetIndex % 25) == 0)
			{
				UE_LOG(
					LogEFCharacterCreationDazBridge,
					Log,
					TEXT("DQ sync: saving asset %d/%d."),
					AssetIndex + 1,
					AssetsToSave.Num());
			}

			SavedAssetCount += SaveAssetIfDirty(AssetsToSave[AssetIndex]) ? 1 : 0;
		}

		OutSummary = FString::Printf(
			TEXT("Dual Quaternion sync finished. CandidateMeshes=%d UpdatedMeshes=%d CreatedCollection=%s SavedAssets=%d Collection=%s DefaultMeshDeformerPatched=true"),
			CandidateMeshes.Num(),
			UpdatedMeshCount,
			bCreatedCollection ? TEXT("true") : TEXT("false"),
			SavedAssetCount,
			*DualQuatMorphCollectionPath.ToString());
		return true;
	}

	static bool AuditDazDualQuatCorrectives(FString& OutSummary)
	{
		OutSummary = TEXT("DQ corrective audit did not run.");
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ audit: starting."));

		UMeshDeformer* DualQuatMorphDeformer = Cast<UMeshDeformer>(DualQuatMorphDeformerPath.TryLoad());
		bool bCreatedCollection = false;
		bool bUpdatedCollection = false;
		UMeshDeformerCollection* Collection = ResolveOrCreateDualQuatMorphCollection(bCreatedCollection, bUpdatedCollection);

		TSet<TObjectPtr<USkeletalMesh>> CandidateMeshes;
		CollectCandidateMeshes(CandidateMeshes);

		TArray<FString> ReportLines;
		ReportLines.Add(TEXT("Project Multiple Mesh DQ Corrective Audit"));
		ReportLines.Add(FString::Printf(TEXT("Generated=%s"), *FDateTime::Now().ToIso8601()));
		ReportLines.Add(FString::Printf(TEXT("Scope=Female, Male, and Multiple project meshes only")));
		ReportLines.Add(FString::Printf(TEXT("FemaleVisibleMesh=%s"), FemaleVisibleMeshObjectPath));
		ReportLines.Add(FString::Printf(TEXT("MultipleCompatibilityMesh=%s"), MultipleCompatibilityMeshObjectPath));
		ReportLines.Add(FString::Printf(TEXT("MaleMesh=%s"), MaleMeshObjectPath));
		ReportLines.Add(FString::Printf(TEXT("Collection=%s"), Collection ? *Collection->GetPathName() : TEXT("missing")));
		ReportLines.Add(FString::Printf(TEXT("DefaultDeformer=%s"), DualQuatMorphDeformer ? *DualQuatMorphDeformer->GetPathName() : TEXT("missing")));
		ReportLines.Add(TEXT(""));

		int32 BodyMeshes = 0;
		int32 GeograftLikeMeshes = 0;
		int32 MeshesMissingRelevantDQ2LB = 0;
		int32 MeshesMissingPostProcess = 0;

		for (USkeletalMesh* SkeletalMesh : CandidateMeshes)
		{
			if (!IsValid(SkeletalMesh))
			{
				continue;
			}

			const FDazMeshCorrectiveAuditRecord Record = BuildAuditRecord(SkeletalMesh, Collection, DualQuatMorphDeformer);
			LogAuditRecord(Record);
			ReportLines.Add(FormatAuditRecordForReport(Record));

			if (Record.bIsGeograftLike)
			{
				++GeograftLikeMeshes;
			}
			else
			{
				++BodyMeshes;
			}

			if (Record.RelevantJointLinkCount > 0 && Record.RelevantJointLinksWithDQ2LB == 0)
			{
				++MeshesMissingRelevantDQ2LB;
			}

			if ((Record.RelevantDQ2LBMorphCount > 0 || Record.RelevantJointLinksWithDQ2LB > 0) &&
				!Record.bHasPostProcessAnimBlueprint)
			{
				++MeshesMissingPostProcess;
			}
		}

		FString ReportPath;
		const bool bReportWritten = WriteAuditReport(TEXT("DazDQCorrectivesAudit"), ReportLines, ReportPath);

		OutSummary = FString::Printf(
			TEXT("DQ corrective audit finished. CandidateMeshes=%d BodyMeshes=%d GeograftLikeMeshes=%d MissingRelevantDQ2LB=%d MissingPostProcess=%d Collection=%s Report=%s"),
			CandidateMeshes.Num(),
			BodyMeshes,
			GeograftLikeMeshes,
			MeshesMissingRelevantDQ2LB,
			MeshesMissingPostProcess,
			Collection ? *Collection->GetPathName() : TEXT("missing"),
			bReportWritten ? *ReportPath : TEXT("failed"));
		return true;
	}

	static bool RepairDazDualQuatCorrectives(FString& OutSummary)
	{
		OutSummary = TEXT("DQ corrective repair did not run.");
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("DQ repair: starting."));

		UMeshDeformer* DualQuatMorphDeformer = Cast<UMeshDeformer>(DualQuatMorphDeformerPath.TryLoad());
		if (!DualQuatMorphDeformer)
		{
			OutSummary = FString::Printf(
				TEXT("Could not load official Dual Quaternion deformer asset: %s"),
				*DualQuatMorphDeformerPath.ToString());
			return false;
		}

		bool bCreatedCollection = false;
		bool bUpdatedCollection = false;
		UMeshDeformerCollection* Collection = ResolveOrCreateDualQuatMorphCollection(bCreatedCollection, bUpdatedCollection);
		if (!Collection)
		{
			OutSummary = TEXT("Could not create or load the project mesh deformer collection.");
			return false;
		}

		TSet<TObjectPtr<USkeletalMesh>> CandidateMeshes;
		CollectCandidateMeshes(CandidateMeshes);

		TArray<UObject*> AssetsToSave;
		if (bUpdatedCollection)
		{
			AssetsToSave.Add(Collection);
		}

		TArray<FString> ReportLines;
		ReportLines.Add(TEXT("Project Multiple Mesh DQ Corrective Repair"));
		ReportLines.Add(FString::Printf(TEXT("Generated=%s"), *FDateTime::Now().ToIso8601()));
		ReportLines.Add(FString::Printf(TEXT("Scope=Female, Male, and Multiple project meshes only")));
		ReportLines.Add(FString::Printf(TEXT("FemaleVisibleMesh=%s"), FemaleVisibleMeshObjectPath));
		ReportLines.Add(FString::Printf(TEXT("MultipleCompatibilityMesh=%s"), MultipleCompatibilityMeshObjectPath));
		ReportLines.Add(FString::Printf(TEXT("MaleMesh=%s"), MaleMeshObjectPath));
		ReportLines.Add(FString::Printf(TEXT("Collection=%s"), *Collection->GetPathName()));
		ReportLines.Add(FString::Printf(TEXT("DefaultDeformer=%s"), *DualQuatMorphDeformer->GetPathName()));
		ReportLines.Add(TEXT(""));

		int32 DQPatchedMeshes = 0;
		int32 PostProcessRepairedMeshes = 0;
		int32 GeograftFallbackMeshes = 0;
		int32 ManualAttentionMeshes = 0;

		for (USkeletalMesh* SkeletalMesh : CandidateMeshes)
		{
			if (!IsValid(SkeletalMesh))
			{
				continue;
			}

			const FDazMeshCorrectiveAuditRecord AuditRecord = BuildAuditRecord(SkeletalMesh, Collection, DualQuatMorphDeformer);
			ReportLines.Add(FormatAuditRecordForReport(AuditRecord));
			const bool bGeograftNeedsFallback =
				AuditRecord.bIsGeograftLike &&
				(AuditRecord.RelevantJointLinksWithDQ2LB == 0 && AuditRecord.RelevantDQ2LBMorphCount == 0);

			const bool bPatchedDeformer = EnsureMeshDeformerState(
				SkeletalMesh,
				Collection,
				DualQuatMorphDeformer,
				!bGeograftNeedsFallback,
				AssetsToSave);
			if (bPatchedDeformer)
			{
				if (bGeograftNeedsFallback)
				{
					++GeograftFallbackMeshes;
					ReportLines.Add(FString::Printf(TEXT("Repair=%s | Action=ClearedDefaultDQForGeograftFallback"), *AuditRecord.MeshPath));
					UE_LOG(
						LogEFCharacterCreationDazBridge,
						Warning,
						TEXT("DQ repair: cleared mesh-level DQ default on geograft-like mesh %s because no relevant pelvis *_dq2lb correctives were found."),
						*AuditRecord.MeshPath);
				}
				else
				{
					++DQPatchedMeshes;
					ReportLines.Add(FString::Printf(TEXT("Repair=%s | Action=PatchedTargetCollectionAndDefaultDQ"), *AuditRecord.MeshPath));
				}
			}

			if (!bGeograftNeedsFallback &&
				(AuditRecord.RelevantDQ2LBMorphCount > 0 || AuditRecord.RelevantJointLinksWithDQ2LB > 0) &&
				!AuditRecord.bHasPostProcessAnimBlueprint)
			{
				FString RepairNote;
				if (EnsureAutoJCMPostProcessForMesh(SkeletalMesh, AuditRecord, AssetsToSave, RepairNote))
				{
					++PostProcessRepairedMeshes;
					ReportLines.Add(FString::Printf(TEXT("Repair=%s | Action=RepairedAutoJCM | Note=%s"), *AuditRecord.MeshPath, *RepairNote));
					UE_LOG(
						LogEFCharacterCreationDazBridge,
						Log,
						TEXT("DQ repair: %s -> %s"),
						*AuditRecord.MeshPath,
						*RepairNote);
				}
				else
				{
					++ManualAttentionMeshes;
					ReportLines.Add(FString::Printf(TEXT("Repair=%s | Action=ManualAttention | Note=%s"), *AuditRecord.MeshPath, *RepairNote));
					UE_LOG(
						LogEFCharacterCreationDazBridge,
						Warning,
						TEXT("DQ repair: could not repair post process for %s -> %s"),
						*AuditRecord.MeshPath,
						*RepairNote);
				}
			}
			else if (AuditRecord.RelevantJointLinkCount > 0 &&
				AuditRecord.RelevantJointLinksWithDQ2LB == 0 &&
				!AuditRecord.bIsGeograftLike)
			{
				++ManualAttentionMeshes;
				ReportLines.Add(FString::Printf(TEXT("Repair=%s | Action=ManualCorrectiveMorphNeeded"), *AuditRecord.MeshPath));
				UE_LOG(
					LogEFCharacterCreationDazBridge,
					Warning,
					TEXT("DQ repair: %s still needs a manual corrective morph for the pelvis pose; no relevant *_dq2lb morph was found."),
					*AuditRecord.MeshPath);
			}
		}

		int32 SavedAssetCount = 0;
		for (int32 AssetIndex = 0; AssetIndex < AssetsToSave.Num(); ++AssetIndex)
		{
			if ((AssetIndex % 25) == 0)
			{
				UE_LOG(
					LogEFCharacterCreationDazBridge,
					Log,
					TEXT("DQ repair: saving asset %d/%d."),
					AssetIndex + 1,
					AssetsToSave.Num());
			}

			SavedAssetCount += SaveAssetIfDirty(AssetsToSave[AssetIndex]) ? 1 : 0;
		}

		FString ReportPath;
		const bool bReportWritten = WriteAuditReport(TEXT("DazDQCorrectivesRepair"), ReportLines, ReportPath);

		OutSummary = FString::Printf(
			TEXT("DQ corrective repair finished. CandidateMeshes=%d DQPatchedMeshes=%d PostProcessRepairedMeshes=%d GeograftFallbackMeshes=%d ManualAttentionMeshes=%d SavedAssets=%d Collection=%s Report=%s"),
			CandidateMeshes.Num(),
			DQPatchedMeshes,
			PostProcessRepairedMeshes,
			GeograftFallbackMeshes,
			ManualAttentionMeshes,
			SavedAssetCount,
			*DualQuatMorphCollectionPath.ToString(),
			bReportWritten ? *ReportPath : TEXT("failed"));
		return true;
	}
}

void FEFCharacterCreationDazBridgeEditorModule::StartupModule()
{
	const TSharedPtr<IPlugin> DazPlugin = IPluginManager::Get().FindPlugin(TEXT("DazToUnreal"));
	if (!DazPlugin.IsValid() || !DazPlugin->IsEnabled())
	{
		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Warning,
			TEXT("EFCharacterCreationDazBridge expects the DazToUnreal plugin to be installed and enabled in the editor."));
	}

	const UEFCharacterCreationSettings* Settings = UEFCharacterCreationSettings::Get();
	if (!Settings || Settings->DazPathTokens.Num() == 0 || Settings->DazMorphTokens.Num() == 0)
	{
		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Warning,
			TEXT("EFCharacterCreationSettings is missing Daz compatibility defaults. Review the plugin DefaultGame.ini if you are adopting this plugin in another project."));
	}
	else
	{
		UE_LOG(
			LogEFCharacterCreationDazBridge,
			Log,
			TEXT("EFCharacterCreation Daz bridge is active. Daz compatibility defaults are available."));
	}

	AuditDazDQCorrectivesCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("EFCharacterCreation.AuditDazDQCorrectives"),
		TEXT("Audit Daz skeletal meshes for mesh-level Dual Quaternion setup, AutoJCM post-process wiring, and pelvis-related *_dq2lb correctives."),
		FConsoleCommandDelegate::CreateRaw(this, &FEFCharacterCreationDazBridgeEditorModule::HandleAuditDazDQCorrectivesCommand),
		ECVF_Default);

	RepairDazDQCorrectivesCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("EFCharacterCreation.RepairDazDQCorrectives"),
		TEXT("Repair mesh-level DQ/JCM setup for Daz skeletal meshes and fall back geografts without relevant pelvis *_dq2lb correctives to linear skinning."),
		FConsoleCommandDelegate::CreateRaw(this, &FEFCharacterCreationDazBridgeEditorModule::HandleRepairDazDQCorrectivesCommand),
		ECVF_Default);

	SyncDazDualQuatMorphCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("EFCharacterCreation.SyncDazDualQuatMorph"),
		TEXT("Prepare Daz skeletal meshes to cook the official UE5 Dual Quaternion + morph deformer."),
		FConsoleCommandDelegate::CreateRaw(this, &FEFCharacterCreationDazBridgeEditorModule::HandleSyncDazDualQuatMorphCommand),
		ECVF_Default);
}

void FEFCharacterCreationDazBridgeEditorModule::ShutdownModule()
{
	if (AuditDazDQCorrectivesCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(AuditDazDQCorrectivesCommand);
		AuditDazDQCorrectivesCommand = nullptr;
	}

	if (RepairDazDQCorrectivesCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(RepairDazDQCorrectivesCommand);
		RepairDazDQCorrectivesCommand = nullptr;
	}

	if (SyncDazDualQuatMorphCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(SyncDazDualQuatMorphCommand);
		SyncDazDualQuatMorphCommand = nullptr;
	}
}

void FEFCharacterCreationDazBridgeEditorModule::HandleAuditDazDQCorrectivesCommand()
{
	FString Summary;
	const bool bSucceeded = EFCharacterCreationDazBridgeEditorPrivate::AuditDazDualQuatCorrectives(Summary);

	if (bSucceeded)
	{
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogEFCharacterCreationDazBridge, Error, TEXT("%s"), *Summary);
	}
}

void FEFCharacterCreationDazBridgeEditorModule::HandleRepairDazDQCorrectivesCommand()
{
	FString Summary;
	const bool bSucceeded = EFCharacterCreationDazBridgeEditorPrivate::RepairDazDualQuatCorrectives(Summary);

	if (bSucceeded)
	{
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogEFCharacterCreationDazBridge, Error, TEXT("%s"), *Summary);
	}
}

void FEFCharacterCreationDazBridgeEditorModule::HandleSyncDazDualQuatMorphCommand()
{
	FString Summary;
	const bool bSucceeded = EFCharacterCreationDazBridgeEditorPrivate::SyncDazDualQuatMorphAssets(Summary);

	if (bSucceeded)
	{
		UE_LOG(LogEFCharacterCreationDazBridge, Log, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogEFCharacterCreationDazBridge, Error, TEXT("%s"), *Summary);
	}
}

IMPLEMENT_MODULE(FEFCharacterCreationDazBridgeEditorModule, EFCharacterCreationDazBridgeEditor)
