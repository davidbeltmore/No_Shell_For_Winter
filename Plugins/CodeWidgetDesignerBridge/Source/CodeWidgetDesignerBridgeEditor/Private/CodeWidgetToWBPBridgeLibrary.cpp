#include "CodeWidgetToWBPBridgeLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetBlueprintFactory.h"

DEFINE_LOG_CATEGORY_STATIC(LogCodeWidgetToWBPBridge, Log, All);

namespace CodeWidgetToWBPBridge
{
	static constexpr TCHAR DefaultWidgetBlueprintFolder[] = TEXT("/Game/_Game/Widgets");
	static TMap<FString, TMap<FName, UClass*>> GNamedWidgetClassOverridesByPackage;
	static TMap<FString, FCodeWidgetDesignerBuildContext> GBuildContextByPackage;

	struct FNormalizedAssetPath
	{
		FString PackageName;
		FString PackagePath;
		FString AssetName;
		FString ObjectPath;
	};

	static FString ExtractQuotedObjectPath(const FString& InPath)
	{
		FString CleanPath = InPath.TrimStartAndEnd();
		int32 FirstQuote = INDEX_NONE;
		int32 LastQuote = INDEX_NONE;
		if (CleanPath.FindChar(TEXT('\''), FirstQuote) && CleanPath.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
		{
			CleanPath = CleanPath.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}
		return CleanPath;
	}

	static bool NormalizeAssetPath(const FString& InPath, FNormalizedAssetPath& OutPath, FString& OutError)
	{
		FString CleanPath = ExtractQuotedObjectPath(InPath);
		CleanPath.RemoveFromEnd(TEXT("_C"));

		if (CleanPath.IsEmpty())
		{
			OutError = TEXT("Target asset path is empty.");
			return false;
		}

		if (!CleanPath.StartsWith(TEXT("/")))
		{
			OutError = FString::Printf(TEXT("Target asset path '%s' must be a long package path under /Game or /PluginName."), *CleanPath);
			return false;
		}

		FString PackageName = CleanPath;
		FString ObjectPath;
		if (CleanPath.Contains(TEXT(".")))
		{
			PackageName = FPackageName::ObjectPathToPackageName(CleanPath);
			ObjectPath = CleanPath;
		}
		else
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(CleanPath);
			ObjectPath = FString::Printf(TEXT("%s.%s"), *CleanPath, *AssetName);
		}

		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(PackageName, true, &PackageNameError))
		{
			OutError = PackageNameError.ToString();
			return false;
		}

		OutPath.PackageName = PackageName;
		OutPath.PackagePath = FPackageName::GetLongPackagePath(PackageName);
		OutPath.AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		OutPath.ObjectPath = ObjectPath;
		return true;
	}

	static bool IsUnderDefaultWidgetBlueprintFolder(const FString& PackageName)
	{
		return PackageName == DefaultWidgetBlueprintFolder || PackageName.StartsWith(FString::Printf(TEXT("%s/"), DefaultWidgetBlueprintFolder));
	}

	static FString MakeWidgetBlueprintAssetName(const UClass* NativeWidgetClass)
	{
		FString AssetBaseName = NativeWidgetClass ? NativeWidgetClass->GetName() : TEXT("CodeWidget");
		AssetBaseName.RemoveFromStart(TEXT("U"));
		AssetBaseName.RemoveFromEnd(TEXT("Widget"));

		if (!AssetBaseName.StartsWith(TEXT("WBP_")))
		{
			AssetBaseName = FString::Printf(TEXT("WBP_%s"), *AssetBaseName);
		}

		return AssetBaseName;
	}

	static FString MakeChildWidgetBlueprintAssetName(
		const FCodeWidgetDesignerChildWidgetSpec& ChildWidgetSpec,
		const UClass* NativeWidgetClass)
	{
		FString OverrideName = ChildWidgetSpec.AssetNameOverride.TrimStartAndEnd();
		OverrideName.RemoveFromStart(TEXT("/"));
		OverrideName.RemoveFromEnd(TEXT("."));
		if (!OverrideName.IsEmpty() && !OverrideName.Contains(TEXT("/")) && !OverrideName.Contains(TEXT(".")))
		{
			return OverrideName;
		}

		return MakeWidgetBlueprintAssetName(NativeWidgetClass);
	}

	static FString MakeWidgetFolderRootPath(const FNormalizedAssetPath& ParentPath)
	{
		FString RootPath = ParentPath.PackagePath;
		if (RootPath.EndsWith(TEXT("/Main")))
		{
			int32 LastSlashIndex = INDEX_NONE;
			if (RootPath.FindLastChar(TEXT('/'), LastSlashIndex) && LastSlashIndex > 0)
			{
				RootPath.LeftInline(LastSlashIndex, EAllowShrinking::No);
			}
		}
		return RootPath;
	}

	static FString SanitizeRelativeFolder(const FString& RelativeFolder)
	{
		FString CleanFolder = RelativeFolder.TrimStartAndEnd();
		CleanFolder.RemoveFromStart(TEXT("/"));
		CleanFolder.RemoveFromEnd(TEXT("/"));
		// Preserve the folder declared by the native manifest so validation matches the current WBP layout.
		return CleanFolder;
	}

	static FString MakeChildWidgetBlueprintPath(
		const FNormalizedAssetPath& ParentPath,
		const FCodeWidgetDesignerChildWidgetSpec& ChildWidgetSpec,
		const UClass* NativeWidgetClass)
	{
		const FString RelativeFolder = SanitizeRelativeFolder(ChildWidgetSpec.RelativeFolder);
		const FString ChildFolder = RelativeFolder.IsEmpty()
			? ParentPath.PackagePath
			: FString::Printf(TEXT("%s/%s"), *MakeWidgetFolderRootPath(ParentPath), *RelativeFolder);
		return FString::Printf(TEXT("%s/%s"), *ChildFolder, *MakeChildWidgetBlueprintAssetName(ChildWidgetSpec, NativeWidgetClass));
	}

	static UWidgetBlueprint* LoadExistingWidgetBlueprint(const FNormalizedAssetPath& Path)
	{
		return LoadObject<UWidgetBlueprint>(nullptr, *Path.ObjectPath);
	}

	static UWidgetBlueprint* CreateWidgetBlueprint(const FNormalizedAssetPath& Path, UClass* NativeWidgetClass, FString& OutReport)
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = NativeWidgetClass;

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* CreatedAsset = AssetTools.CreateAsset(Path.AssetName, Path.PackagePath, UWidgetBlueprint::StaticClass(), Factory);
		UWidgetBlueprint* CreatedWidgetBlueprint = Cast<UWidgetBlueprint>(CreatedAsset);
		if (CreatedWidgetBlueprint)
		{
			OutReport += FString::Printf(TEXT("Created Widget Blueprint '%s'.\n"), *Path.ObjectPath);
		}
		return CreatedWidgetBlueprint;
	}

	static TSet<FName> CollectBindWidgetNames(UClass* NativeWidgetClass)
	{
		TSet<FName> BindWidgetNames;
		if (!NativeWidgetClass)
		{
			return BindWidgetNames;
		}

		for (TFieldIterator<FProperty> PropertyIt(NativeWidgetClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			bool bIsOptional = false;
			if (!FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property, bIsOptional))
			{
				continue;
			}

			const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			if (!ObjectProperty || !ObjectProperty->PropertyClass || !ObjectProperty->PropertyClass->IsChildOf(UWidget::StaticClass()))
			{
				continue;
			}

			BindWidgetNames.Add(Property->GetFName());
		}

		return BindWidgetNames;
	}

	static const UObject* GetDesignerSafeDefaultFontObject()
	{
		static TWeakObjectPtr<const UObject> CachedFontObject;
		if (!CachedFontObject.IsValid())
		{
			CachedFontObject = LoadObject<UObject>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}
		return CachedFontObject.Get();
	}

	static int32 NormalizeDesignerPreviewTextFonts(const TSet<UWidget*>& Widgets)
	{
		const UObject* DefaultFontObject = GetDesignerSafeDefaultFontObject();
		if (!DefaultFontObject)
		{
			return 0;
		}

		int32 NormalizedCount = 0;
		for (UWidget* Widget : Widgets)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
			if (!TextBlock)
			{
				continue;
			}

			FSlateFontInfo FontInfo = TextBlock->GetFont();
			if (FontInfo.FontObject == DefaultFontObject && !FontInfo.CompositeFont.IsValid())
			{
				continue;
			}

			FontInfo.FontObject = DefaultFontObject;
			FontInfo.CompositeFont.Reset();
			TextBlock->SetFont(FontInfo);
			++NormalizedCount;
		}

		return NormalizedCount;
	}

	static FCodeWidgetDesignerBuildContext MakeBuildContextFromSpec(
		const FCodeWidgetDesignerWidgetAssetSpec& Spec,
		const FString& TargetAssetPath)
	{
		FCodeWidgetDesignerBuildContext Context;
		Context.TargetAssetPath = TargetAssetPath;
		Context.RelativeFolder = Spec.RelativeFolder;
		Context.AssetNameOverride = Spec.AssetNameOverride;
		Context.Role = Spec.Role;
		Context.PriorityGroup = Spec.PriorityGroup;
		Context.PriorityRank = Spec.PriorityRank;
		Context.bRuntimeDefault = Spec.bRuntimeDefault;
		Context.ExpectedPreviewTexts = Spec.ExpectedPreviewTexts;
		return Context;
	}

	static UWidgetTree* BuildSourceTreeFromNativeClass(
		UClass* NativeWidgetClass,
		TArray<UObject*>& KeepAlive,
		FString& OutReport,
		const FCodeWidgetDesignerBuildContext* BuildContext = nullptr)
	{
		UUserWidget* SourceWidget = NewObject<UUserWidget>(GetTransientPackage(), NativeWidgetClass, NAME_None, RF_Transient);
		if (!SourceWidget)
		{
			OutReport += TEXT("Failed to create native source widget instance.\n");
			return nullptr;
		}

		KeepAlive.Add(SourceWidget);

		if (ICodeWidgetDesignerTreeProvider* DesignerTreeProvider = Cast<ICodeWidgetDesignerTreeProvider>(SourceWidget))
		{
			if (BuildContext)
			{
				DesignerTreeProvider->SetCodeWidgetDesignerBuildContext(*BuildContext);
				OutReport += FString::Printf(
					TEXT("Applied designer build context for '%s'.\n"),
					*BuildContext->TargetAssetPath);
			}

			UWidgetTree* ProvidedTree = NewObject<UWidgetTree>(SourceWidget, TEXT("CodeWidgetDesignerProvidedTree"), RF_Transient);
			if (ProvidedTree && DesignerTreeProvider->BuildCodeWidgetDesignerTree(ProvidedTree) && ProvidedTree->RootWidget)
			{
				OutReport += TEXT("Used ICodeWidgetDesignerTreeProvider explicit designer tree.\n");
				return ProvidedTree;
			}

			OutReport += TEXT("ICodeWidgetDesignerTreeProvider did not produce a root; falling back to widget lifecycle capture.\n");
		}

		SourceWidget->Initialize();
		SourceWidget->TakeWidget();

		if (SourceWidget->WidgetTree && SourceWidget->WidgetTree->RootWidget)
		{
			OutReport += TEXT("Captured native widget tree through UUserWidget lifecycle.\n");
			return SourceWidget->WidgetTree;
		}

		OutReport += TEXT("Native widget lifecycle produced no WidgetTree root.\n");
		return nullptr;
	}

	static void GatherDesignerChildWidgetSpecs(UClass* NativeWidgetClass, TArray<FCodeWidgetDesignerChildWidgetSpec>& OutChildWidgetSpecs, FString& OutReport)
	{
		if (!NativeWidgetClass)
		{
			return;
		}

		UUserWidget* SourceWidget = NewObject<UUserWidget>(GetTransientPackage(), NativeWidgetClass, NAME_None, RF_Transient);
		const ICodeWidgetDesignerTreeProvider* DesignerTreeProvider = Cast<ICodeWidgetDesignerTreeProvider>(SourceWidget);
		if (!DesignerTreeProvider)
		{
			return;
		}

		TArray<FCodeWidgetDesignerChildWidgetSpec> GatheredSpecs;
		DesignerTreeProvider->GatherCodeWidgetDesignerChildWidgetSpecs(GatheredSpecs);

		TSet<UClass*> SeenClasses;
		for (const FCodeWidgetDesignerChildWidgetSpec& GatheredSpec : GatheredSpecs)
		{
			UClass* ChildClass = GatheredSpec.WidgetClass.Get();
			if (!ChildClass || ChildClass == NativeWidgetClass || SeenClasses.Contains(ChildClass))
			{
				continue;
			}

			SeenClasses.Add(ChildClass);
			OutChildWidgetSpecs.Add(GatheredSpec);
		}

		if (!OutChildWidgetSpecs.IsEmpty())
		{
			OutReport += FString::Printf(
				TEXT("Designer provider declared %d child Widget Blueprint class(es) for 1:1 editable assets.\n"),
				OutChildWidgetSpecs.Num());
		}
	}

	static bool ExportSourceTreeToText(UWidgetTree* SourceTree, FString& OutExportedText, FString& OutReport)
	{
		if (!SourceTree || !SourceTree->RootWidget)
		{
			OutReport += TEXT("Source tree has no root widget.\n");
			return false;
		}

		TArray<UWidget*> WidgetsToExport;
		WidgetsToExport.Add(SourceTree->RootWidget);
		UWidgetTree::GetChildWidgets(SourceTree->RootWidget, WidgetsToExport);

		FWidgetBlueprintEditorUtils::ExportWidgetsToText(WidgetsToExport, OutExportedText);
		OutReport += FString::Printf(TEXT("Exported %d widgets from code tree.\n"), WidgetsToExport.Num());
		return !OutExportedText.IsEmpty();
	}

	static int32 ApplyNamedWidgetClassOverrides(
		UWidgetTree* SourceTree,
		const TMap<FName, UClass*>& NamedWidgetClassOverrides,
		FString& OutReport)
	{
		if (!SourceTree || NamedWidgetClassOverrides.IsEmpty())
		{
			return 0;
		}

		int32 ReplacementCount = 0;
		for (const TPair<FName, UClass*>& OverridePair : NamedWidgetClassOverrides)
		{
			const FName WidgetName = OverridePair.Key;
			UClass* ReplacementClass = OverridePair.Value;
			if (WidgetName.IsNone() || !ReplacementClass || !ReplacementClass->IsChildOf(UWidget::StaticClass()))
			{
				OutReport += FString::Printf(
					TEXT("Skipped invalid named widget class override '%s'.\n"),
					*WidgetName.ToString());
				continue;
			}

			UWidget* ExistingWidget = SourceTree->FindWidget(WidgetName);
			if (!ExistingWidget)
			{
				OutReport += FString::Printf(
					TEXT("Named widget class override '%s' did not match a widget in the source tree.\n"),
					*WidgetName.ToString());
				continue;
			}

			if (ExistingWidget->GetClass() == ReplacementClass)
			{
				continue;
			}

			const FName PlaceholderName = MakeUniqueObjectName(
				SourceTree,
				ExistingWidget->GetClass(),
				FName(*FString::Printf(TEXT("%s_NativePlaceholder"), *WidgetName.ToString())));
			ExistingWidget->Rename(
				*PlaceholderName.ToString(),
				SourceTree,
				REN_DontCreateRedirectors | REN_NonTransactional);

			UWidget* ReplacementWidget = SourceTree->ConstructWidget<UWidget>(ReplacementClass, WidgetName);
			if (!ReplacementWidget)
			{
				OutReport += FString::Printf(
					TEXT("Failed creating replacement widget '%s' with class '%s'.\n"),
					*WidgetName.ToString(),
					*ReplacementClass->GetName());
				continue;
			}

			ReplacementWidget->bIsVariable = ExistingWidget->bIsVariable;

			int32 ChildIndex = INDEX_NONE;
			if (UPanelWidget* ParentWidget = UWidgetTree::FindWidgetParent(ExistingWidget, ChildIndex))
			{
				if (!ParentWidget->ReplaceChildAt(ChildIndex, ReplacementWidget))
				{
					OutReport += FString::Printf(
						TEXT("Failed replacing named widget '%s' in parent '%s'.\n"),
						*WidgetName.ToString(),
						*ParentWidget->GetName());
					continue;
				}
			}
			else if (SourceTree->RootWidget == ExistingWidget)
			{
				SourceTree->RootWidget = ReplacementWidget;
			}
			else
			{
				OutReport += FString::Printf(
					TEXT("Named widget class override '%s' had no replaceable parent.\n"),
					*WidgetName.ToString());
				continue;
			}

			++ReplacementCount;
			OutReport += FString::Printf(
				TEXT("Replaced source widget '%s' with class '%s' before Designer import.\n"),
				*WidgetName.ToString(),
				*ReplacementClass->GetName());
		}

		return ReplacementCount;
	}

	static bool ReplaceWidgetBlueprintTreeFromText(
		UWidgetBlueprint* WidgetBlueprint,
		UClass* NativeWidgetClass,
		const FString& ExportedText,
		FString& OutReport)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			OutReport += TEXT("Target Widget Blueprint has no WidgetTree.\n");
			return false;
		}

		if (WidgetBlueprint->WidgetTree->RootWidget)
		{
			TSet<UWidget*> WidgetsToDelete;
			WidgetsToDelete.Add(WidgetBlueprint->WidgetTree->RootWidget);
			FWidgetBlueprintEditorUtils::DeleteWidgets(
				WidgetBlueprint,
				WidgetsToDelete,
				FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
			WidgetBlueprint->WidgetTree->RootWidget = nullptr;
		}

		TSet<UWidget*> ImportedWidgets;
		TMap<FName, UWidgetSlotPair*> PastedExtraSlotData;
		FWidgetBlueprintEditorUtils::ImportWidgetsFromText(WidgetBlueprint, ExportedText, ImportedWidgets, PastedExtraSlotData);
		const int32 NormalizedFontCount = NormalizeDesignerPreviewTextFonts(ImportedWidgets);

		UWidget* ImportedRoot = nullptr;
		for (UWidget* ImportedWidget : ImportedWidgets)
		{
			if (!ImportedWidget)
			{
				continue;
			}

			if (!ImportedWidget->GetParent())
			{
				if (ImportedRoot)
				{
					OutReport += FString::Printf(
						TEXT("Multiple root widgets were imported ('%s' and '%s'); cannot choose a Designer root.\n"),
						*ImportedRoot->GetName(),
						*ImportedWidget->GetName());
					return false;
				}
				ImportedRoot = ImportedWidget;
			}
		}

		if (!ImportedRoot)
		{
			OutReport += TEXT("Imported widget text produced no root widget.\n");
			return false;
		}

		const TSet<FName> BindWidgetNames = CollectBindWidgetNames(NativeWidgetClass);
		for (UWidget* ImportedWidget : ImportedWidgets)
		{
			if (!ImportedWidget)
			{
				continue;
			}

			ImportedWidget->SetFlags(RF_Transactional);
			ImportedWidget->WidgetGeneratedBy = WidgetBlueprint;
			ImportedWidget->WidgetGeneratedByClass = WidgetBlueprint->GeneratedClass;

			if (BindWidgetNames.Contains(ImportedWidget->GetFName()))
			{
				ImportedWidget->bIsVariable = true;
			}

			WidgetBlueprint->OnVariableAdded(ImportedWidget->GetFName());
		}

		WidgetBlueprint->WidgetTree->Modify();
		WidgetBlueprint->WidgetTree->RootWidget = ImportedRoot;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		OutReport += FString::Printf(TEXT("Imported %d widgets into Designer tree. Root: '%s'.\n"), ImportedWidgets.Num(), *ImportedRoot->GetName());
		if (NormalizedFontCount > 0)
		{
			OutReport += FString::Printf(TEXT("Normalized %d TextBlock fonts to /Engine/EngineFonts/Roboto for Designer-safe preview.\n"), NormalizedFontCount);
		}
		return true;
	}

	static FString MakeWidgetFolderName(const UClass* NativeWidgetClass)
	{
		FString FolderName = MakeWidgetBlueprintAssetName(NativeWidgetClass);
		FolderName.RemoveFromStart(TEXT("WBP_"));
		return FolderName;
	}

	static FString MakeDefaultTargetAssetPath(const UClass* NativeWidgetClass)
	{
		return FString::Printf(
			TEXT("%s/%s/Main/%s"),
			DefaultWidgetBlueprintFolder,
			*MakeWidgetFolderName(NativeWidgetClass),
			*MakeWidgetBlueprintAssetName(NativeWidgetClass));
	}

	static FString MakeManifestSystemName(const FString& RootPath, const UClass* NativeWidgetClass)
	{
		FString CleanRoot = RootPath.TrimStartAndEnd();
		CleanRoot.RemoveFromEnd(TEXT("/"));
		FString SystemName = FPackageName::GetLongPackageAssetName(CleanRoot);
		if (SystemName.IsEmpty())
		{
			SystemName = MakeWidgetFolderName(NativeWidgetClass);
		}
		return SystemName;
	}

	static FCodeWidgetDesignerWidgetAssetSpec MakeWidgetAssetSpecFromChildSpec(
		const FNormalizedAssetPath& HostPath,
		const FCodeWidgetDesignerChildWidgetSpec& ChildWidgetSpec)
	{
		FCodeWidgetDesignerWidgetAssetSpec AssetSpec;
		AssetSpec.WidgetClass = ChildWidgetSpec.WidgetClass;
		AssetSpec.RelativeFolder = ChildWidgetSpec.RelativeFolder;
		AssetSpec.AssetNameOverride = ChildWidgetSpec.AssetNameOverride;
		AssetSpec.Role = ChildWidgetSpec.Role;
		AssetSpec.PriorityGroup = ChildWidgetSpec.PriorityGroup;
		AssetSpec.PriorityRank = ChildWidgetSpec.PriorityRank;
		AssetSpec.bRuntimeDefault = ChildWidgetSpec.bRuntimeDefault;
		AssetSpec.bDesignerOnly = ChildWidgetSpec.bDesignerOnly;
		AssetSpec.bRequiresStableRootWrapper = ChildWidgetSpec.bRequiresStableRootWrapper;
		AssetSpec.ExpectedWidgetNames = ChildWidgetSpec.ExpectedWidgetNames;
		AssetSpec.ExpectedBlueprintEvents = ChildWidgetSpec.ExpectedBlueprintEvents;
		AssetSpec.ExpectedVisualStates = ChildWidgetSpec.ExpectedVisualStates;
		AssetSpec.ExpectedPreviewTexts = ChildWidgetSpec.ExpectedPreviewTexts;
		AssetSpec.TargetAssetPath = MakeChildWidgetBlueprintPath(HostPath, ChildWidgetSpec, ChildWidgetSpec.WidgetClass.Get());
		return AssetSpec;
	}

	static FString ResolveWidgetAssetSpecPath(
		const FNormalizedAssetPath& HostPath,
		const FCodeWidgetDesignerWidgetAssetSpec& AssetSpec)
	{
		if (!AssetSpec.TargetAssetPath.TrimStartAndEnd().IsEmpty())
		{
			return AssetSpec.TargetAssetPath;
		}

		FCodeWidgetDesignerChildWidgetSpec ChildWidgetSpec;
		ChildWidgetSpec.WidgetClass = AssetSpec.WidgetClass;
		ChildWidgetSpec.RelativeFolder = AssetSpec.RelativeFolder;
		ChildWidgetSpec.AssetNameOverride = AssetSpec.AssetNameOverride;
		return MakeChildWidgetBlueprintPath(HostPath, ChildWidgetSpec, AssetSpec.WidgetClass.Get());
	}

	static void AddUniqueManifestSpec(
		TArray<FCodeWidgetDesignerWidgetAssetSpec>& Specs,
		const FCodeWidgetDesignerWidgetAssetSpec& Spec)
	{
		if (!Spec.WidgetClass)
		{
			return;
		}

		const FString Key = Spec.TargetAssetPath.TrimStartAndEnd();
		for (const FCodeWidgetDesignerWidgetAssetSpec& ExistingSpec : Specs)
		{
			if (!Key.IsEmpty() && ExistingSpec.TargetAssetPath.Equals(Key, ESearchCase::IgnoreCase))
			{
				return;
			}
			if (Key.IsEmpty() && ExistingSpec.WidgetClass == Spec.WidgetClass)
			{
				return;
			}
		}

		Specs.Add(Spec);
	}

	static bool BuildConversionManifestFromNativeClass(
		UClass* NativeWidgetClass,
		const FString& TargetAssetPath,
		FCodeWidgetDesignerConversionManifest& OutManifest,
		FString& OutReport)
	{
		if (!NativeWidgetClass)
		{
			OutReport += TEXT("NativeWidgetClass is null.\n");
			return false;
		}

		FCodeWidgetDesignerConversionManifest ProviderManifest;
		bool bUsedProviderManifest = false;
		if (UUserWidget* SourceWidget = NewObject<UUserWidget>(GetTransientPackage(), NativeWidgetClass, NAME_None, RF_Transient))
		{
			if (const ICodeWidgetDesignerTreeProvider* DesignerTreeProvider = Cast<ICodeWidgetDesignerTreeProvider>(SourceWidget))
			{
				bUsedProviderManifest = DesignerTreeProvider->GatherCodeWidgetDesignerConversionManifest(ProviderManifest);
			}
		}

		OutManifest = bUsedProviderManifest ? ProviderManifest : FCodeWidgetDesignerConversionManifest();

		FString HostTargetAssetPath = TargetAssetPath.TrimStartAndEnd();
		if (HostTargetAssetPath.IsEmpty() && !OutManifest.HostWidget.TargetAssetPath.IsEmpty())
		{
			HostTargetAssetPath = OutManifest.HostWidget.TargetAssetPath;
		}
		if (HostTargetAssetPath.IsEmpty())
		{
			HostTargetAssetPath = MakeDefaultTargetAssetPath(NativeWidgetClass);
		}

		FString PathError;
		FNormalizedAssetPath HostPath;
		if (!NormalizeAssetPath(HostTargetAssetPath, HostPath, PathError))
		{
			OutReport += PathError + TEXT("\n");
			return false;
		}

		if (OutManifest.RootPath.TrimStartAndEnd().IsEmpty() || OutManifest.RootPath == DefaultWidgetBlueprintFolder)
		{
			OutManifest.RootPath = MakeWidgetFolderRootPath(HostPath);
		}
		if (OutManifest.MainFolder.TrimStartAndEnd().IsEmpty())
		{
			OutManifest.MainFolder = TEXT("Main");
		}
		OutManifest.GlobalFolder = SanitizeRelativeFolder(OutManifest.GlobalFolder);
		if (OutManifest.GlobalFolder.IsEmpty())
		{
			OutManifest.GlobalFolder = TEXT("Global");
		}
		if (OutManifest.SystemName.TrimStartAndEnd().IsEmpty())
		{
			OutManifest.SystemName = MakeManifestSystemName(OutManifest.RootPath, NativeWidgetClass);
		}

		OutManifest.HostWidget.WidgetClass = NativeWidgetClass;
		OutManifest.HostWidget.TargetAssetPath = HostPath.PackageName;
		OutManifest.HostWidget.Role = ECodeWidgetDesignerAssetRole::Host;
		OutManifest.HostWidget.PriorityRank = FMath::Max(OutManifest.HostWidget.PriorityRank, 10000);

		TArray<FCodeWidgetDesignerChildWidgetSpec> ChildWidgetSpecs;
		GatherDesignerChildWidgetSpecs(NativeWidgetClass, ChildWidgetSpecs, OutReport);
		for (const FCodeWidgetDesignerChildWidgetSpec& ChildWidgetSpec : ChildWidgetSpecs)
		{
			FCodeWidgetDesignerWidgetAssetSpec AssetSpec = MakeWidgetAssetSpecFromChildSpec(HostPath, ChildWidgetSpec);
			AddUniqueManifestSpec(OutManifest.WidgetAssets, AssetSpec);
		}

		for (FCodeWidgetDesignerWidgetAssetSpec& AssetSpec : OutManifest.WidgetAssets)
		{
			if (AssetSpec.WidgetClass && AssetSpec.TargetAssetPath.TrimStartAndEnd().IsEmpty())
			{
				AssetSpec.TargetAssetPath = ResolveWidgetAssetSpecPath(HostPath, AssetSpec);
			}
		}

		OutReport += FString::Printf(
			TEXT("Built conversion manifest for '%s' at '%s' with %d child asset spec(s).\n"),
			*OutManifest.SystemName,
			*OutManifest.RootPath,
			OutManifest.WidgetAssets.Num());
		return true;
	}

	static void CollectManifestSpecs(
		const FCodeWidgetDesignerConversionManifest& Manifest,
		TArray<FCodeWidgetDesignerWidgetAssetSpec>& OutSpecs,
		const bool bIncludeHost)
	{
		OutSpecs.Reset();
		for (const FCodeWidgetDesignerWidgetAssetSpec& AssetSpec : Manifest.WidgetAssets)
		{
			AddUniqueManifestSpec(OutSpecs, AssetSpec);
		}
		if (bIncludeHost)
		{
			AddUniqueManifestSpec(OutSpecs, Manifest.HostWidget);
		}
	}

	static bool NormalizeSpecPath(
		const FCodeWidgetDesignerWidgetAssetSpec& AssetSpec,
		FNormalizedAssetPath& OutPath,
		FString& OutReport)
	{
		FString PathError;
		if (!NormalizeAssetPath(AssetSpec.TargetAssetPath, OutPath, PathError))
		{
			OutReport += FString::Printf(
				TEXT("Invalid asset path for class '%s': %s\n"),
				*GetNameSafe(AssetSpec.WidgetClass.Get()),
				*PathError);
			return false;
		}
		return true;
	}

	static void EnsurePackagePathDirectory(const FString& LongPackagePath)
	{
		if (LongPackagePath.IsEmpty())
		{
			return;
		}

		const FString PackageDirectory = FPackageName::LongPackageNameToFilename(LongPackagePath);
		if (!PackageDirectory.IsEmpty())
		{
			FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*PackageDirectory);
		}
	}

	static void AppendValidationError(FCodeWidgetDesignerValidationReport& Report, const FString& Message)
	{
		Report.bPassed = false;
		Report.Errors.Add(Message);
	}

	static void AppendValidationWarning(FCodeWidgetDesignerValidationReport& Report, const FString& Message)
	{
		Report.Warnings.Add(Message);
	}

	static void AppendValidationInfo(FCodeWidgetDesignerValidationReport& Report, const FString& Message)
	{
		Report.Info.Add(Message);
	}

	static void AppendHighRiskWarning(FCodeWidgetDesignerValidationReport& Report, const FString& Message)
	{
		AppendValidationWarning(Report, FString::Printf(TEXT("High risk: %s"), *Message));
	}

	static void AddUniqueReportString(TArray<FString>& Values, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Values.AddUnique(Value);
		}
	}

	static int32 CountLineNumberAtIndex(const FString& Contents, const int32 CharacterIndex)
	{
		int32 LineNumber = 1;
		const int32 ClampedIndex = FMath::Clamp(CharacterIndex, 0, Contents.Len());
		for (int32 Index = 0; Index < ClampedIndex; ++Index)
		{
			if (Contents[Index] == TEXT('\n'))
			{
				++LineNumber;
			}
		}
		return LineNumber;
	}

	static FString ExtractSingleLineSnippet(const FString& Contents, const int32 CharacterIndex)
	{
		if (!Contents.IsValidIndex(CharacterIndex))
		{
			return FString();
		}

		int32 LineStart = CharacterIndex;
		while (LineStart > 0 && Contents[LineStart - 1] != TEXT('\n') && Contents[LineStart - 1] != TEXT('\r'))
		{
			--LineStart;
		}

		int32 LineEnd = CharacterIndex;
		while (Contents.IsValidIndex(LineEnd) && Contents[LineEnd] != TEXT('\n') && Contents[LineEnd] != TEXT('\r'))
		{
			++LineEnd;
		}

		FString Snippet = Contents.Mid(LineStart, LineEnd - LineStart);
		Snippet.TrimStartAndEndInline();
		if (Snippet.Len() > 220)
		{
			Snippet = Snippet.Left(217) + TEXT("...");
		}
		return Snippet;
	}

	static void PromoteHighRiskWarningsForStrictMode(FCodeWidgetDesignerValidationReport& Report)
	{
		for (const FString& Warning : Report.Warnings)
		{
			if (Warning.StartsWith(TEXT("High risk:")))
			{
				AppendValidationError(Report, FString::Printf(TEXT("Strict mode failure: %s"), *Warning));
			}
		}
	}

	static FString EscapeJsonStringForReport(const FString& Input)
	{
		FString Output = Input.Replace(TEXT("\\"), TEXT("\\\\"));
		Output.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Output.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Output.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Output.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Output;
	}

	static void AppendJsonStringArray(FString& Json, const TCHAR* Name, const TArray<FString>& Values)
	{
		Json += FString::Printf(TEXT("\n  \"%s\": ["), Name);
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0)
			{
				Json += TEXT(", ");
			}
			Json += FString::Printf(TEXT("\"%s\""), *EscapeJsonStringForReport(Values[Index]));
		}
		Json += TEXT("]");
	}

	static bool SaveValidationReportJson(const FCodeWidgetDesignerValidationReport& Report, FString& OutReport)
	{
		const FString ReportDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeWidgetDesignerBridge"), TEXT("Reports"));
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ReportDirectory);

		FString FileName = FPaths::MakeValidFileName(Report.SystemName.IsEmpty() ? TEXT("WidgetConversion") : Report.SystemName);
		if (FileName.IsEmpty())
		{
			FileName = TEXT("WidgetConversion");
		}

		const FString ReportPath = FPaths::Combine(ReportDirectory, FileName + TEXT(".json"));
		FString Json = TEXT("{");
		Json += FString::Printf(TEXT("\n  \"systemName\": \"%s\","), *EscapeJsonStringForReport(Report.SystemName));
		Json += FString::Printf(TEXT("\n  \"rootPath\": \"%s\","), *EscapeJsonStringForReport(Report.RootPath));
		Json += FString::Printf(TEXT("\n  \"passed\": %s,"), Report.bPassed ? TEXT("true") : TEXT("false"));
		AppendJsonStringArray(Json, TEXT("errors"), Report.Errors);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("warnings"), Report.Warnings);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("info"), Report.Info);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("expectedAssets"), Report.ExpectedAssets);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("existingAssets"), Report.ExistingAssets);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("missingAssets"), Report.MissingAssets);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("staleDuplicateAssets"), Report.StaleDuplicateAssets);
		Json += TEXT(",");
		AppendJsonStringArray(Json, TEXT("runtimeWinners"), Report.RuntimeWinners);
		Json += TEXT("\n}\n");

		const bool bSaved = FFileHelper::SaveStringToFile(Json, *ReportPath);
		OutReport += FString::Printf(TEXT("Validation report JSON %s: %s\n"), bSaved ? TEXT("saved") : TEXT("failed"), *ReportPath);
		return bSaved;
	}

	static bool IsSuspiciousPlaceholderText(const FString& Text)
	{
		const FString CleanText = Text.TrimStartAndEnd();
		if (CleanText.Len() < 4)
		{
			return false;
		}

		int32 LetterACount = 0;
		int32 OtherVisibleCount = 0;
		for (const TCHAR Character : CleanText)
		{
			if (FChar::IsWhitespace(Character))
			{
				continue;
			}
			if (Character == TEXT('A') || Character == TEXT('a'))
			{
				++LetterACount;
			}
			else
			{
				++OtherVisibleCount;
			}
		}

		return LetterACount >= 4 && OtherVisibleCount == 0;
	}

	static bool ContainsExpectedPreviewText(const TArray<FString>& TextValues, const FString& ExpectedPreviewText)
	{
		const FString CleanExpected = ExpectedPreviewText.TrimStartAndEnd();
		if (CleanExpected.IsEmpty())
		{
			return true;
		}

		for (const FString& TextValue : TextValues)
		{
			if (TextValue.Contains(CleanExpected, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static void CollectWidgetsFromBlueprint(UWidgetBlueprint* WidgetBlueprint, TArray<UWidget*>& OutWidgets)
	{
		OutWidgets.Reset();
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !WidgetBlueprint->WidgetTree->RootWidget)
		{
			return;
		}

		OutWidgets.Add(WidgetBlueprint->WidgetTree->RootWidget);
		UWidgetTree::GetChildWidgets(WidgetBlueprint->WidgetTree->RootWidget, OutWidgets);
	}

	static FString MakeReportSummary(const FCodeWidgetDesignerValidationReport& Report)
	{
		FString Summary;
		Summary += FString::Printf(
			TEXT("Validation %s for '%s'. errors=%d warnings=%d info=%d\n"),
			Report.bPassed ? TEXT("passed") : TEXT("failed"),
			*Report.SystemName,
			Report.Errors.Num(),
			Report.Warnings.Num(),
			Report.Info.Num());

		if (!Report.ExpectedAssets.IsEmpty() || !Report.ExistingAssets.IsEmpty() || !Report.MissingAssets.IsEmpty() || !Report.StaleDuplicateAssets.IsEmpty())
		{
			Summary += FString::Printf(
				TEXT("Assets expected=%d existing=%d missing=%d staleOrDuplicate=%d runtimeWinners=%d\n"),
				Report.ExpectedAssets.Num(),
				Report.ExistingAssets.Num(),
				Report.MissingAssets.Num(),
				Report.StaleDuplicateAssets.Num(),
				Report.RuntimeWinners.Num());
		}

		for (const FString& Error : Report.Errors)
		{
			Summary += FString::Printf(TEXT("ERROR: %s\n"), *Error);
		}
		for (const FString& Warning : Report.Warnings)
		{
			Summary += FString::Printf(TEXT("WARNING: %s\n"), *Warning);
		}
		for (const FString& Info : Report.Info)
		{
			Summary += FString::Printf(TEXT("INFO: %s\n"), *Info);
		}
		return Summary;
	}

	static bool GetGeneratedClassObjectPath(const FAssetData& AssetData, FString& OutClassObjectPath)
	{
		FString GeneratedClassExportPath;
		if (!AssetData.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassExportPath)
			&& !AssetData.GetTagValue(FName(TEXT("GeneratedClassPath")), GeneratedClassExportPath))
		{
			return false;
		}

		OutClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassExportPath);
		return !OutClassObjectPath.IsEmpty();
	}

	static void AuditDuplicateWidgetAssets(
		const FCodeWidgetDesignerConversionManifest& Manifest,
		const TArray<FCodeWidgetDesignerWidgetAssetSpec>& Specs,
		FCodeWidgetDesignerValidationReport& ValidationReport)
	{
		TSet<FString> ExpectedPackageNames;
		TArray<UClass*> ExpectedNativeClasses;
		for (const FCodeWidgetDesignerWidgetAssetSpec& Spec : Specs)
		{
			FNormalizedAssetPath NormalizedPath;
			FString PathReport;
			if (NormalizeSpecPath(Spec, NormalizedPath, PathReport))
			{
				ExpectedPackageNames.Add(NormalizedPath.PackageName);
			}
			if (Spec.WidgetClass)
			{
				ExpectedNativeClasses.AddUnique(Spec.WidgetClass.Get());
			}
		}

		if (Manifest.RootPath.IsEmpty() || ExpectedNativeClasses.IsEmpty())
		{
			return;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		TArray<FString> RootPaths = { Manifest.RootPath };
		AssetRegistry.ScanPathsSynchronous(RootPaths, true);

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*Manifest.RootPath));
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		for (const FAssetData& AssetData : Assets)
		{
			const FString PackageName = AssetData.PackageName.ToString();
			if (ExpectedPackageNames.Contains(PackageName))
			{
				continue;
			}

			FString GeneratedClassObjectPath;
			if (!GetGeneratedClassObjectPath(AssetData, GeneratedClassObjectPath))
			{
				continue;
			}

			UClass* GeneratedClass = LoadObject<UClass>(nullptr, *GeneratedClassObjectPath);
			if (!GeneratedClass)
			{
				continue;
			}

			for (UClass* NativeClass : ExpectedNativeClasses)
			{
				if (NativeClass && GeneratedClass->IsChildOf(NativeClass) && GeneratedClass != NativeClass)
				{
					AddUniqueReportString(ValidationReport.StaleDuplicateAssets, PackageName);
					AppendHighRiskWarning(
						ValidationReport,
						FString::Printf(
							TEXT("stale or duplicate widget candidate under manifest root may affect resolver priority: %s derives from %s."),
							*PackageName,
							*NativeClass->GetName()));
					break;
				}
			}
		}
	}

	static bool ContainsUnsafeExplicitCreateWidgetName(const FString& Contents, FString& OutSnippet, int32& OutLineNumber)
	{
		int32 SearchIndex = 0;
		while (SearchIndex < Contents.Len())
		{
			const int32 CreateWidgetIndex = Contents.Find(TEXT("CreateWidget"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchIndex);
			if (CreateWidgetIndex == INDEX_NONE)
			{
				return false;
			}

			const int32 OpenParenIndex = Contents.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, CreateWidgetIndex);
			if (OpenParenIndex == INDEX_NONE)
			{
				return false;
			}

			int32 Depth = 0;
			int32 TopLevelCommaCount = 0;
			int32 CloseParenIndex = INDEX_NONE;
			for (int32 Index = OpenParenIndex; Index < Contents.Len(); ++Index)
			{
				const TCHAR Char = Contents[Index];
				if (Char == TEXT('('))
				{
					++Depth;
				}
				else if (Char == TEXT(')'))
				{
					--Depth;
					if (Depth == 0)
					{
						CloseParenIndex = Index;
						break;
					}
				}
				else if (Char == TEXT(',') && Depth == 1)
				{
					++TopLevelCommaCount;
				}
			}

			if (CloseParenIndex == INDEX_NONE)
			{
				return false;
			}

			if (TopLevelCommaCount >= 2)
			{
				FString Statement = Contents.Mid(CreateWidgetIndex, FMath::Min(CloseParenIndex - CreateWidgetIndex + 1, 220));
				Statement.ReplaceInline(TEXT("\r"), TEXT(" "));
				Statement.ReplaceInline(TEXT("\n"), TEXT(" "));
				Statement.TrimStartAndEndInline();

				const bool bLooksLikeDynamicChild =
					Statement.Contains(TEXT("Row"), ESearchCase::IgnoreCase)
					|| Statement.Contains(TEXT("Option"), ESearchCase::IgnoreCase)
					|| Statement.Contains(TEXT("Entry"), ESearchCase::IgnoreCase)
					|| Statement.Contains(TEXT("Card"), ESearchCase::IgnoreCase)
					|| Statement.Contains(TEXT("Item"), ESearchCase::IgnoreCase);
				if (bLooksLikeDynamicChild)
				{
					OutSnippet = Statement;
					OutLineNumber = CountLineNumberAtIndex(Contents, CreateWidgetIndex);
					return true;
				}
			}

			SearchIndex = CloseParenIndex + 1;
		}

		return false;
	}

	static void ScanSourceForHardcodedVisualOverrides(
		UClass* NativeWidgetClass,
		FCodeWidgetDesignerValidationReport& ValidationReport)
	{
		if (!NativeWidgetClass)
		{
			return;
		}

		TArray<UClass*> ClassesToScan;
		for (UClass* ClassToScan = NativeWidgetClass;
			ClassToScan && ClassToScan->IsChildOf(UUserWidget::StaticClass()) && ClassToScan != UUserWidget::StaticClass();
			ClassToScan = ClassToScan->GetSuperClass())
		{
			ClassesToScan.AddUnique(ClassToScan);
		}

		const TArray<FString> RootsToScan = {
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")),
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins"))
		};
		const TArray<FString> FileExtensions = { TEXT("*.cpp"), TEXT("*.h") };
		const TArray<FString> DangerousTokens = {
			TEXT("SetBrush"),
			TEXT("SetColorAndOpacity"),
			TEXT("SetFont"),
			TEXT("SetPadding"),
			TEXT("SetWidthOverride"),
			TEXT("SetHeightOverride"),
			TEXT("SetRenderTransform"),
			TEXT("SetOpacity"),
			TEXT("/Game/UI/"),
			TEXT(".WBP_")
		};

		int32 WarningCount = 0;
		TSet<FString> ScannedFiles;
		for (UClass* ClassToScan : ClassesToScan)
		{
			const FString ClassName = ClassToScan->GetName();
			FString ClassBaseName = ClassName;
			ClassBaseName.RemoveFromStart(TEXT("U"));

			for (const FString& Root : RootsToScan)
			{
				for (const FString& Extension : FileExtensions)
				{
					TArray<FString> Files;
					IFileManager::Get().FindFilesRecursive(Files, *Root, *Extension, true, false);
					for (const FString& File : Files)
					{
						if (ScannedFiles.Contains(File) || !File.Contains(ClassBaseName))
						{
							continue;
						}

						FString Contents;
						if (!FFileHelper::LoadFileToString(Contents, *File) || !Contents.Contains(ClassName))
						{
							continue;
						}

						ScannedFiles.Add(File);

						FString UnsafeCreateWidgetSnippet;
						int32 UnsafeCreateWidgetLine = INDEX_NONE;
						if (ContainsUnsafeExplicitCreateWidgetName(Contents, UnsafeCreateWidgetSnippet, UnsafeCreateWidgetLine))
						{
							AppendHighRiskWarning(
								ValidationReport,
								FString::Printf(
									TEXT("'%s:%d' creates a dynamic row/option/card widget with an explicit UObject name: %s. Converted menus should usually let UMG generate runtime child names; deterministic names can crash when another WBP class with the same name is still alive."),
									*FPaths::ConvertRelativePathToFull(File),
									UnsafeCreateWidgetLine,
									*UnsafeCreateWidgetSnippet));
							++WarningCount;
						}

						for (const FString& Token : DangerousTokens)
						{
							const int32 TokenIndex = Contents.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart);
							if (TokenIndex != INDEX_NONE)
							{
								AppendValidationWarning(
									ValidationReport,
									FString::Printf(
										TEXT("Manual review: '%s:%d' contains visual/code-path token '%s'. Keep this inside fallback or data-only updates. Snippet: %s"),
										*FPaths::ConvertRelativePathToFull(File),
										CountLineNumberAtIndex(Contents, TokenIndex),
										*Token,
										*ExtractSingleLineSnippet(Contents, TokenIndex)));
								++WarningCount;
								break;
							}
						}

						if (WarningCount >= 50)
						{
							AppendValidationWarning(ValidationReport, TEXT("Hardcoded visual scanner stopped after 50 warnings."));
							return;
						}
					}
				}
			}
		}
	}

	static bool DeleteManifestAssets(
		const FCodeWidgetDesignerConversionManifest& Manifest,
		FString& OutReport)
	{
		TArray<FCodeWidgetDesignerWidgetAssetSpec> Specs;
		CollectManifestSpecs(Manifest, Specs, true);

		TSet<FName> ManifestPackages;
		TArray<FNormalizedAssetPath> NormalizedPaths;
		for (const FCodeWidgetDesignerWidgetAssetSpec& Spec : Specs)
		{
			FNormalizedAssetPath NormalizedPath;
			if (NormalizeSpecPath(Spec, NormalizedPath, OutReport))
			{
				ManifestPackages.Add(FName(*NormalizedPath.PackageName));
				NormalizedPaths.Add(NormalizedPath);
			}
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		TArray<UObject*> ObjectsToDelete;
		for (const FNormalizedAssetPath& NormalizedPath : NormalizedPaths)
		{
			UObject* Asset = LoadObject<UObject>(nullptr, *NormalizedPath.ObjectPath);
			if (!Asset)
			{
				continue;
			}

			TArray<FName> Referencers;
			AssetRegistry.GetReferencers(FName(*NormalizedPath.PackageName), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			TArray<FName> ExternalReferencers;
			for (const FName& Referencer : Referencers)
			{
				if (!ManifestPackages.Contains(Referencer))
				{
					ExternalReferencers.Add(Referencer);
				}
			}

			if (!ExternalReferencers.IsEmpty())
			{
				OutReport += FString::Printf(
					TEXT("Skipped deleting '%s'; external referencers: %s\n"),
					*NormalizedPath.PackageName,
					*FString::JoinBy(ExternalReferencers, TEXT(", "), [](const FName& Name) { return Name.ToString(); }));
				continue;
			}

			ObjectsToDelete.Add(Asset);
		}

		if (ObjectsToDelete.IsEmpty())
		{
			OutReport += TEXT("CleanRebuild found no existing manifest assets to delete.\n");
			return true;
		}

		const int32 DeletedCount = ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		OutReport += FString::Printf(TEXT("CleanRebuild deleted %d manifest asset(s).\n"), DeletedCount);
		return DeletedCount == ObjectsToDelete.Num();
	}

	static FString AssetRoleToString(const ECodeWidgetDesignerAssetRole Role)
	{
		const UEnum* RoleEnum = StaticEnum<ECodeWidgetDesignerAssetRole>();
		return RoleEnum ? RoleEnum->GetNameStringByValue(static_cast<int64>(Role)) : TEXT("Unknown");
	}

	static FString GenerationModeToString(const ECodeWidgetDesignerGenerationMode GenerationMode)
	{
		const UEnum* GenerationModeEnum = StaticEnum<ECodeWidgetDesignerGenerationMode>();
		return GenerationModeEnum ? GenerationModeEnum->GetNameStringByValue(static_cast<int64>(GenerationMode)) : TEXT("Unknown");
	}

	static FString MakeSemanticDuplicateKey(const FCodeWidgetDesignerWidgetAssetSpec& Spec)
	{
		return FString::Printf(
			TEXT("%s|%s|%s|runtime=%s|designerOnly=%s"),
			*GetNameSafe(Spec.WidgetClass.Get()),
			*AssetRoleToString(Spec.Role),
			*Spec.PriorityGroup.ToString(),
			Spec.bRuntimeDefault ? TEXT("true") : TEXT("false"),
			Spec.bDesignerOnly ? TEXT("true") : TEXT("false"));
	}

	static FString MakeSpecLabel(const FCodeWidgetDesignerWidgetAssetSpec& Spec, const FString& PackageName)
	{
		return FString::Printf(
			TEXT("%s (%s, class=%s, rank=%d)"),
			*PackageName,
			*AssetRoleToString(Spec.Role),
			*GetNameSafe(Spec.WidgetClass.Get()),
			Spec.PriorityRank);
	}

	static void AppendCleanRebuildPreview(
		const FCodeWidgetDesignerConversionManifest& Manifest,
		const TArray<FCodeWidgetDesignerWidgetAssetSpec>& Specs,
		FCodeWidgetDesignerValidationReport& ValidationReport,
		FString& OutReport)
	{
		TSet<FName> ManifestPackages;
		TArray<FNormalizedAssetPath> NormalizedPaths;
		for (const FCodeWidgetDesignerWidgetAssetSpec& Spec : Specs)
		{
			FNormalizedAssetPath NormalizedPath;
			FString PathReport;
			if (NormalizeSpecPath(Spec, NormalizedPath, PathReport))
			{
				ManifestPackages.Add(FName(*NormalizedPath.PackageName));
				NormalizedPaths.Add(NormalizedPath);
			}
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		if (!Manifest.RootPath.IsEmpty())
		{
			TArray<FString> RootPaths = { Manifest.RootPath };
			AssetRegistry.ScanPathsSynchronous(RootPaths, true);
		}

		AppendValidationInfo(
			ValidationReport,
			FString::Printf(TEXT("CleanRebuild preview only: %d manifest asset target(s) inspected; no assets were deleted."), NormalizedPaths.Num()));

		for (const FNormalizedAssetPath& NormalizedPath : NormalizedPaths)
		{
			UObject* Asset = LoadObject<UObject>(nullptr, *NormalizedPath.ObjectPath);
			if (!Asset)
			{
				AppendValidationInfo(
					ValidationReport,
					FString::Printf(TEXT("CleanRebuild preview: '%s' does not exist yet."), *NormalizedPath.PackageName));
				continue;
			}

			TArray<FName> Referencers;
			AssetRegistry.GetReferencers(FName(*NormalizedPath.PackageName), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			TArray<FName> ExternalReferencers;
			for (const FName& Referencer : Referencers)
			{
				if (!ManifestPackages.Contains(Referencer))
				{
					ExternalReferencers.Add(Referencer);
				}
			}

			if (ExternalReferencers.IsEmpty())
			{
				AppendValidationInfo(
					ValidationReport,
					FString::Printf(TEXT("CleanRebuild preview: '%s' has no external package referencers."), *NormalizedPath.PackageName));
			}
			else
			{
				AppendHighRiskWarning(
					ValidationReport,
					FString::Printf(
						TEXT("CleanRebuild preview: '%s' has external referencers: %s"),
						*NormalizedPath.PackageName,
						*FString::JoinBy(ExternalReferencers, TEXT(", "), [](const FName& Name) { return Name.ToString(); })));
			}
		}

		OutReport += TEXT("CleanRebuild preflight completed without deleting assets.\n");
	}
}

bool UCodeWidgetToWBPBridgeLibrary::PreflightWidgetBlueprintConversion(
	TSubclassOf<UUserWidget> NativeWidgetClass,
	const FString& TargetAssetPath,
	const ECodeWidgetDesignerGenerationMode GenerationMode,
	const bool bStrict,
	FCodeWidgetDesignerValidationReport& OutValidationReport,
	FString& OutReport)
{
	OutReport.Reset();
	OutValidationReport = FCodeWidgetDesignerValidationReport();

	UClass* NativeClass = NativeWidgetClass.Get();
	if (!NativeClass)
	{
		CodeWidgetToWBPBridge::AppendValidationError(OutValidationReport, TEXT("NativeWidgetClass is null."));
		OutReport = CodeWidgetToWBPBridge::MakeReportSummary(OutValidationReport);
		return false;
	}

	FCodeWidgetDesignerConversionManifest Manifest;
	if (!CodeWidgetToWBPBridge::BuildConversionManifestFromNativeClass(NativeClass, TargetAssetPath, Manifest, OutReport))
	{
		CodeWidgetToWBPBridge::AppendValidationError(OutValidationReport, TEXT("Could not build conversion manifest."));
		OutReport += CodeWidgetToWBPBridge::MakeReportSummary(OutValidationReport);
		return false;
	}

	OutValidationReport.SystemName = Manifest.SystemName.IsEmpty()
		? FString::Printf(TEXT("%s_Preflight"), *NativeClass->GetName())
		: FString::Printf(TEXT("%s_Preflight"), *Manifest.SystemName);
	OutValidationReport.RootPath = Manifest.RootPath;

	CodeWidgetToWBPBridge::AppendValidationInfo(
		OutValidationReport,
		FString::Printf(
			TEXT("Preflight for native widget '%s' with generationMode=%s strict=%s. No assets are created, replaced, deleted, or saved by this command."),
			*NativeClass->GetName(),
			*CodeWidgetToWBPBridge::GenerationModeToString(GenerationMode),
			bStrict ? TEXT("true") : TEXT("false")));

	if (!Manifest.RootPath.StartsWith(CodeWidgetToWBPBridge::DefaultWidgetBlueprintFolder))
	{
		CodeWidgetToWBPBridge::AppendValidationError(
			OutValidationReport,
			FString::Printf(
				TEXT("Manifest root '%s' is outside the project widget root '%s'."),
				*Manifest.RootPath,
				CodeWidgetToWBPBridge::DefaultWidgetBlueprintFolder));
	}

	TArray<FCodeWidgetDesignerWidgetAssetSpec> RawSpecs = Manifest.WidgetAssets;
	RawSpecs.Add(Manifest.HostWidget);

	TArray<FCodeWidgetDesignerWidgetAssetSpec> Specs;
	CodeWidgetToWBPBridge::CollectManifestSpecs(Manifest, Specs, true);

	TMap<FString, TArray<FString>> TargetAssetLabelsByPackage;
	TMap<FString, TArray<FString>> SemanticLabelsByKey;

	for (const FCodeWidgetDesignerWidgetAssetSpec& Spec : RawSpecs)
	{
		if (!Spec.WidgetClass || Spec.Role == ECodeWidgetDesignerAssetRole::Asset)
		{
			continue;
		}

		CodeWidgetToWBPBridge::FNormalizedAssetPath NormalizedPath;
		if (!CodeWidgetToWBPBridge::NormalizeSpecPath(Spec, NormalizedPath, OutReport))
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(TEXT("Invalid WBP target for '%s'."), *GetNameSafe(Spec.WidgetClass.Get())));
			continue;
		}

		const FString SpecLabel = CodeWidgetToWBPBridge::MakeSpecLabel(Spec, NormalizedPath.PackageName);
		CodeWidgetToWBPBridge::AddUniqueReportString(OutValidationReport.ExpectedAssets, NormalizedPath.PackageName);
		TargetAssetLabelsByPackage.FindOrAdd(NormalizedPath.PackageName).Add(SpecLabel);
		SemanticLabelsByKey.FindOrAdd(CodeWidgetToWBPBridge::MakeSemanticDuplicateKey(Spec)).Add(SpecLabel);

		if (!NormalizedPath.PackageName.StartsWith(Manifest.RootPath))
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(
					TEXT("Widget Blueprint '%s' is outside manifest root '%s'."),
					*NormalizedPath.PackageName,
					*Manifest.RootPath));
		}

		UWidgetBlueprint* WidgetBlueprint = CodeWidgetToWBPBridge::LoadExistingWidgetBlueprint(NormalizedPath);
		if (WidgetBlueprint)
		{
			CodeWidgetToWBPBridge::AddUniqueReportString(OutValidationReport.ExistingAssets, NormalizedPath.PackageName);
			if (WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget)
			{
				if (GenerationMode == ECodeWidgetDesignerGenerationMode::ReplaceDesigner)
				{
					CodeWidgetToWBPBridge::AppendValidationWarning(
						OutValidationReport,
						FString::Printf(
							TEXT("Replace preflight: '%s' already has Designer root '%s'. Manual edits would be overwritten by a real Replace run."),
							*NormalizedPath.PackageName,
							*WidgetBlueprint->WidgetTree->RootWidget->GetName()));
				}
				else
				{
					CodeWidgetToWBPBridge::AppendValidationInfo(
						OutValidationReport,
						FString::Printf(
							TEXT("Existing Designer root will be preserved for '%s': '%s'."),
							*NormalizedPath.PackageName,
							*WidgetBlueprint->WidgetTree->RootWidget->GetName()));
				}
			}
		}
		else
		{
			CodeWidgetToWBPBridge::AddUniqueReportString(OutValidationReport.MissingAssets, NormalizedPath.PackageName);
			CodeWidgetToWBPBridge::AppendValidationInfo(
				OutValidationReport,
				FString::Printf(TEXT("Expected WBP does not exist yet and would be created by manifest generation: %s."), *NormalizedPath.PackageName));
		}
	}

	for (const TPair<FString, TArray<FString>>& TargetPair : TargetAssetLabelsByPackage)
	{
		if (TargetPair.Value.Num() > 1)
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(
					TEXT("Duplicate manifest target '%s' is used by: %s"),
					*TargetPair.Key,
					*FString::Join(TargetPair.Value, TEXT("; "))));
		}
	}

	for (const TPair<FString, TArray<FString>>& SemanticPair : SemanticLabelsByKey)
	{
		if (SemanticPair.Value.Num() > 1)
		{
			CodeWidgetToWBPBridge::AppendValidationWarning(
				OutValidationReport,
				FString::Printf(
					TEXT("Possible duplicate semantic edit point '%s': %s"),
					*SemanticPair.Key,
					*FString::Join(SemanticPair.Value, TEXT("; "))));
		}
	}

	CodeWidgetToWBPBridge::AuditDuplicateWidgetAssets(Manifest, Specs, OutValidationReport);
	if (GenerationMode == ECodeWidgetDesignerGenerationMode::CleanRebuild)
	{
		CodeWidgetToWBPBridge::AppendCleanRebuildPreview(Manifest, Specs, OutValidationReport, OutReport);
	}
	CodeWidgetToWBPBridge::ScanSourceForHardcodedVisualOverrides(NativeClass, OutValidationReport);

	if (bStrict)
	{
		CodeWidgetToWBPBridge::PromoteHighRiskWarningsForStrictMode(OutValidationReport);
	}

	CodeWidgetToWBPBridge::SaveValidationReportJson(OutValidationReport, OutReport);
	OutReport += CodeWidgetToWBPBridge::MakeReportSummary(OutValidationReport);
	if (OutValidationReport.bPassed)
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Display, TEXT("%s"), *OutReport);
	}
	else
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Warning, TEXT("%s"), *OutReport);
	}
	return OutValidationReport.bPassed;
}

UWidgetBlueprint* UCodeWidgetToWBPBridgeLibrary::CreateOrUpdateWidgetBlueprintFromCode(
	TSubclassOf<UUserWidget> NativeWidgetClass,
	const FString& TargetAssetPath,
	const bool bReplaceExistingDesignerRoot,
	const bool bSaveAsset,
	FString& OutReport)
{
	OutReport.Reset();

	if (!NativeWidgetClass)
	{
		OutReport = TEXT("NativeWidgetClass is null.");
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return nullptr;
	}

	FString PathError;
	CodeWidgetToWBPBridge::FNormalizedAssetPath NormalizedPath;
	if (!CodeWidgetToWBPBridge::NormalizeAssetPath(TargetAssetPath, NormalizedPath, PathError))
	{
		OutReport = PathError;
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return nullptr;
	}

	if (!CodeWidgetToWBPBridge::IsUnderDefaultWidgetBlueprintFolder(NormalizedPath.PackageName))
	{
		OutReport += FString::Printf(
			TEXT("Project convention warning: generated Widget Blueprints should live under %s unless explicitly overridden. Current target: %s.\n"),
			CodeWidgetToWBPBridge::DefaultWidgetBlueprintFolder,
			*NormalizedPath.PackageName);
	}

	static TSet<UClass*> ActiveWidgetBlueprintExports;
	if (!ActiveWidgetBlueprintExports.Contains(NativeWidgetClass.Get()))
	{
		ActiveWidgetBlueprintExports.Add(NativeWidgetClass.Get());
		ON_SCOPE_EXIT
		{
			ActiveWidgetBlueprintExports.Remove(NativeWidgetClass.Get());
		};

		TArray<FCodeWidgetDesignerChildWidgetSpec> ChildWidgetSpecs;
		CodeWidgetToWBPBridge::GatherDesignerChildWidgetSpecs(NativeWidgetClass.Get(), ChildWidgetSpecs, OutReport);

		for (const FCodeWidgetDesignerChildWidgetSpec& ChildWidgetSpec : ChildWidgetSpecs)
		{
			UClass* ChildClass = ChildWidgetSpec.WidgetClass.Get();
			if (!ChildClass || ActiveWidgetBlueprintExports.Contains(ChildClass))
			{
				continue;
			}

			const FString ChildTargetAssetPath = CodeWidgetToWBPBridge::MakeChildWidgetBlueprintPath(NormalizedPath, ChildWidgetSpec, ChildClass);
			FString ChildReport;
			UWidgetBlueprint* ChildWidgetBlueprint = UCodeWidgetToWBPBridgeLibrary::CreateOrUpdateWidgetBlueprintFromCode(
				ChildWidgetSpec.WidgetClass,
				ChildTargetAssetPath,
				false,
				bSaveAsset,
				ChildReport);

			OutReport += FString::Printf(
				TEXT("Child Widget Blueprint '%s' export %s. Existing child Designer roots are preserved; run that child class directly with Replace to refresh it.\n%s"),
				*ChildTargetAssetPath,
				ChildWidgetBlueprint ? TEXT("succeeded") : TEXT("failed"),
				*ChildReport);
		}
	}

	UWidgetBlueprint* WidgetBlueprint = CodeWidgetToWBPBridge::LoadExistingWidgetBlueprint(NormalizedPath);
	const bool bCreatedNewAsset = WidgetBlueprint == nullptr;
	if (!WidgetBlueprint)
	{
		WidgetBlueprint = CodeWidgetToWBPBridge::CreateWidgetBlueprint(NormalizedPath, NativeWidgetClass.Get(), OutReport);
	}

	if (!WidgetBlueprint)
	{
		OutReport += FString::Printf(TEXT("Failed to create or load Widget Blueprint '%s'."), *NormalizedPath.ObjectPath);
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return nullptr;
	}

	if (WidgetBlueprint->GeneratedClass && !WidgetBlueprint->GeneratedClass->IsChildOf(NativeWidgetClass.Get()))
	{
		OutReport += FString::Printf(
			TEXT("Target parent '%s' does not inherit from native widget '%s'. Reparent it manually or create a new WBP.\n"),
			*GetNameSafe(WidgetBlueprint->GeneratedClass->GetSuperClass()),
			*NativeWidgetClass->GetName());
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return WidgetBlueprint;
	}

	if (!bReplaceExistingDesignerRoot && WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget)
	{
		OutReport += FString::Printf(
			TEXT("Preserved existing Designer root '%s'. Pass bReplaceExistingDesignerRoot=true only when intentionally refreshing from code.\n"),
			*WidgetBlueprint->WidgetTree->RootWidget->GetName());
		UE_LOG(LogCodeWidgetToWBPBridge, Display, TEXT("%s"), *OutReport);
		return WidgetBlueprint;
	}

	TArray<UObject*> SourceKeepAlive;
	const FCodeWidgetDesignerBuildContext* BuildContext = CodeWidgetToWBPBridge::GBuildContextByPackage.Find(NormalizedPath.PackageName);
	UWidgetTree* SourceTree = CodeWidgetToWBPBridge::BuildSourceTreeFromNativeClass(NativeWidgetClass.Get(), SourceKeepAlive, OutReport, BuildContext);
	if (!SourceTree || !SourceTree->RootWidget)
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return WidgetBlueprint;
	}

	if (const TMap<FName, UClass*>* NamedWidgetClassOverrides = CodeWidgetToWBPBridge::GNamedWidgetClassOverridesByPackage.Find(NormalizedPath.PackageName))
	{
		const int32 ReplacementCount = CodeWidgetToWBPBridge::ApplyNamedWidgetClassOverrides(SourceTree, *NamedWidgetClassOverrides, OutReport);
		if (ReplacementCount > 0)
		{
			OutReport += FString::Printf(TEXT("Applied %d named widget class override(s).\n"), ReplacementCount);
		}
	}

	FString ExportedText;
	if (!CodeWidgetToWBPBridge::ExportSourceTreeToText(SourceTree, ExportedText, OutReport))
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return WidgetBlueprint;
	}

	if (!CodeWidgetToWBPBridge::ReplaceWidgetBlueprintTreeFromText(WidgetBlueprint, NativeWidgetClass.Get(), ExportedText, OutReport))
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return WidgetBlueprint;
	}

	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	if (bSaveAsset)
	{
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(WidgetBlueprint->GetOutermost());
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
		OutReport += bSaved ? TEXT("Saved Widget Blueprint asset.\n") : TEXT("SavePackages reported failure.\n");
	}
	else
	{
		WidgetBlueprint->GetOutermost()->MarkPackageDirty();
	}

	if (bCreatedNewAsset)
	{
		OutReport += TEXT("Created asset uses native class as parent and stores the code UI as an editable Designer tree.\n");
	}

	UE_LOG(LogCodeWidgetToWBPBridge, Display, TEXT("%s"), *OutReport);
	return WidgetBlueprint;
}

UWidgetBlueprint* UCodeWidgetToWBPBridgeLibrary::CreateOrUpdateWidgetBlueprintFromCodeWithNamedWidgetClassOverrides(
	TSubclassOf<UUserWidget> NativeWidgetClass,
	const FString& TargetAssetPath,
	const bool bReplaceExistingDesignerRoot,
	const bool bSaveAsset,
	const TArray<FName>& WidgetNames,
	const TArray<TSubclassOf<UWidget>>& WidgetClasses,
	FString& OutReport)
{
	OutReport.Reset();

	if (WidgetNames.Num() != WidgetClasses.Num())
	{
		OutReport = FString::Printf(
			TEXT("WidgetNames and WidgetClasses length mismatch: %d vs %d."),
			WidgetNames.Num(),
			WidgetClasses.Num());
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return nullptr;
	}

	FString PathError;
	CodeWidgetToWBPBridge::FNormalizedAssetPath NormalizedPath;
	if (!CodeWidgetToWBPBridge::NormalizeAssetPath(TargetAssetPath, NormalizedPath, PathError))
	{
		OutReport = PathError;
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return nullptr;
	}

	TMap<FName, UClass*> Overrides;
	for (int32 Index = 0; Index < WidgetNames.Num(); ++Index)
	{
		UClass* WidgetClass = WidgetClasses[Index].Get();
		if (!WidgetNames[Index].IsNone() && WidgetClass)
		{
			Overrides.Add(WidgetNames[Index], WidgetClass);
		}
	}

	if (!Overrides.IsEmpty())
	{
		CodeWidgetToWBPBridge::GNamedWidgetClassOverridesByPackage.Add(NormalizedPath.PackageName, Overrides);
	}
	ON_SCOPE_EXIT
	{
		CodeWidgetToWBPBridge::GNamedWidgetClassOverridesByPackage.Remove(NormalizedPath.PackageName);
	};

	return CreateOrUpdateWidgetBlueprintFromCode(
		NativeWidgetClass,
		TargetAssetPath,
		bReplaceExistingDesignerRoot,
		bSaveAsset,
		OutReport);
}

bool UCodeWidgetToWBPBridgeLibrary::CreateOrUpdateWidgetBlueprintsFromManifest(
	TSubclassOf<UUserWidget> NativeWidgetClass,
	const FString& TargetAssetPath,
	const ECodeWidgetDesignerGenerationMode GenerationMode,
	const bool bSaveAssets,
	FString& OutReport)
{
	OutReport.Reset();

	UClass* NativeClass = NativeWidgetClass.Get();
	if (!NativeClass)
	{
		OutReport = TEXT("NativeWidgetClass is null.");
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return false;
	}

	FCodeWidgetDesignerConversionManifest Manifest;
	if (!CodeWidgetToWBPBridge::BuildConversionManifestFromNativeClass(NativeClass, TargetAssetPath, Manifest, OutReport))
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Error, TEXT("%s"), *OutReport);
		return false;
	}
	Manifest.GenerationMode = GenerationMode;

	if (GenerationMode == ECodeWidgetDesignerGenerationMode::ValidateOnly)
	{
		FCodeWidgetDesignerValidationReport ValidationReport;
		FString ValidationText;
		const bool bValid = ValidateWidgetBlueprintConversion(NativeWidgetClass, TargetAssetPath, ValidationReport, ValidationText);
		OutReport += ValidationText;
		return bValid;
	}

	if (GenerationMode == ECodeWidgetDesignerGenerationMode::CleanRebuild)
	{
		CodeWidgetToWBPBridge::DeleteManifestAssets(Manifest, OutReport);
	}

	TArray<FCodeWidgetDesignerWidgetAssetSpec> Specs;
	CodeWidgetToWBPBridge::CollectManifestSpecs(Manifest, Specs, true);

	const bool bReplaceDesignerRoot =
		GenerationMode == ECodeWidgetDesignerGenerationMode::ReplaceDesigner
		|| GenerationMode == ECodeWidgetDesignerGenerationMode::CleanRebuild;

	int32 SuccessCount = 0;
	int32 SkippedCount = 0;
	for (const FCodeWidgetDesignerWidgetAssetSpec& Spec : Specs)
	{
		if (!Spec.WidgetClass)
		{
			++SkippedCount;
			OutReport += FString::Printf(TEXT("Skipped manifest asset spec with no widget class: %s\n"), *Spec.TargetAssetPath);
			continue;
		}
		if (Spec.Role == ECodeWidgetDesignerAssetRole::Asset)
		{
			++SkippedCount;
			OutReport += FString::Printf(TEXT("Skipped non-WBP asset spec: %s\n"), *Spec.TargetAssetPath);
			continue;
		}

		CodeWidgetToWBPBridge::FNormalizedAssetPath NormalizedPath;
		if (!CodeWidgetToWBPBridge::NormalizeSpecPath(Spec, NormalizedPath, OutReport))
		{
			continue;
		}

		CodeWidgetToWBPBridge::EnsurePackagePathDirectory(NormalizedPath.PackagePath);

		FString AssetReport;
		const FCodeWidgetDesignerBuildContext BuildContext = CodeWidgetToWBPBridge::MakeBuildContextFromSpec(Spec, NormalizedPath.PackageName);
		CodeWidgetToWBPBridge::GBuildContextByPackage.Add(NormalizedPath.PackageName, BuildContext);
		UWidgetBlueprint* WidgetBlueprint = CreateOrUpdateWidgetBlueprintFromCode(
			Spec.WidgetClass,
			NormalizedPath.PackageName,
			bReplaceDesignerRoot,
			bSaveAssets,
			AssetReport);
		CodeWidgetToWBPBridge::GBuildContextByPackage.Remove(NormalizedPath.PackageName);

		OutReport += FString::Printf(
			TEXT("Manifest asset '%s' export %s.\n%s"),
			*NormalizedPath.PackageName,
			WidgetBlueprint ? TEXT("succeeded") : TEXT("failed"),
			*AssetReport);

		if (WidgetBlueprint)
		{
			++SuccessCount;
		}
	}

	FCodeWidgetDesignerValidationReport ValidationReport;
	FString ValidationText;
	const bool bValid = ValidateWidgetBlueprintConversion(NativeWidgetClass, TargetAssetPath, ValidationReport, ValidationText);
	OutReport += FString::Printf(
		TEXT("Manifest generation finished. success=%d skipped=%d total=%d\n"),
		SuccessCount,
		SkippedCount,
		Specs.Num());
	OutReport += ValidationText;

	const bool bSucceeded = SuccessCount > 0 && bValid;
	if (bSucceeded)
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Display, TEXT("%s"), *OutReport);
	}
	else
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Warning, TEXT("%s"), *OutReport);
	}
	return bSucceeded;
}

bool UCodeWidgetToWBPBridgeLibrary::ValidateWidgetBlueprintConversion(
	TSubclassOf<UUserWidget> NativeWidgetClass,
	const FString& TargetAssetPath,
	FCodeWidgetDesignerValidationReport& OutValidationReport,
	FString& OutReport)
{
	OutReport.Reset();
	OutValidationReport = FCodeWidgetDesignerValidationReport();

	UClass* NativeClass = NativeWidgetClass.Get();
	if (!NativeClass)
	{
		CodeWidgetToWBPBridge::AppendValidationError(OutValidationReport, TEXT("NativeWidgetClass is null."));
		OutReport = CodeWidgetToWBPBridge::MakeReportSummary(OutValidationReport);
		return false;
	}

	FCodeWidgetDesignerConversionManifest Manifest;
	if (!CodeWidgetToWBPBridge::BuildConversionManifestFromNativeClass(NativeClass, TargetAssetPath, Manifest, OutReport))
	{
		CodeWidgetToWBPBridge::AppendValidationError(OutValidationReport, TEXT("Could not build conversion manifest."));
		OutReport += CodeWidgetToWBPBridge::MakeReportSummary(OutValidationReport);
		return false;
	}

	OutValidationReport.SystemName = Manifest.SystemName;
	OutValidationReport.RootPath = Manifest.RootPath;

	if (!Manifest.RootPath.StartsWith(CodeWidgetToWBPBridge::DefaultWidgetBlueprintFolder))
	{
		CodeWidgetToWBPBridge::AppendValidationError(
			OutValidationReport,
			FString::Printf(
				TEXT("Manifest root '%s' is outside the project widget root '%s'."),
				*Manifest.RootPath,
				CodeWidgetToWBPBridge::DefaultWidgetBlueprintFolder));
	}

	TArray<FCodeWidgetDesignerWidgetAssetSpec> Specs;
	CodeWidgetToWBPBridge::CollectManifestSpecs(Manifest, Specs, true);

	for (const FCodeWidgetDesignerWidgetAssetSpec& Spec : Specs)
	{
		if (!Spec.WidgetClass || Spec.Role == ECodeWidgetDesignerAssetRole::Asset)
		{
			continue;
		}

		CodeWidgetToWBPBridge::FNormalizedAssetPath NormalizedPath;
		if (!CodeWidgetToWBPBridge::NormalizeSpecPath(Spec, NormalizedPath, OutReport))
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(TEXT("Invalid WBP target for '%s'."), *GetNameSafe(Spec.WidgetClass.Get())));
			continue;
		}

		CodeWidgetToWBPBridge::AddUniqueReportString(OutValidationReport.ExpectedAssets, NormalizedPath.PackageName);

		if (!NormalizedPath.PackageName.StartsWith(Manifest.RootPath))
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(
					TEXT("Widget Blueprint '%s' is outside manifest root '%s'."),
					*NormalizedPath.PackageName,
					*Manifest.RootPath));
		}

		UWidgetBlueprint* WidgetBlueprint = CodeWidgetToWBPBridge::LoadExistingWidgetBlueprint(NormalizedPath);
		if (!WidgetBlueprint)
		{
			CodeWidgetToWBPBridge::AddUniqueReportString(OutValidationReport.MissingAssets, NormalizedPath.PackageName);
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(TEXT("Missing expected Widget Blueprint: %s."), *NormalizedPath.PackageName));
			continue;
		}

		CodeWidgetToWBPBridge::AddUniqueReportString(OutValidationReport.ExistingAssets, NormalizedPath.PackageName);

		if (!WidgetBlueprint->GeneratedClass || !WidgetBlueprint->GeneratedClass->IsChildOf(Spec.WidgetClass.Get()))
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(
					TEXT("Widget Blueprint '%s' does not derive from expected native class '%s'."),
					*NormalizedPath.PackageName,
					*GetNameSafe(Spec.WidgetClass.Get())));
		}

		TArray<UWidget*> Widgets;
		CodeWidgetToWBPBridge::CollectWidgetsFromBlueprint(WidgetBlueprint, Widgets);
		if (Widgets.IsEmpty())
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(TEXT("Widget Blueprint '%s' has no Designer root."), *NormalizedPath.PackageName));
			continue;
		}

		if ((Spec.Role == ECodeWidgetDesignerAssetRole::Individual || Spec.Role == ECodeWidgetDesignerAssetRole::GlobalTemplate)
			&& Widgets.Num() <= 2)
		{
			CodeWidgetToWBPBridge::AppendValidationError(
				OutValidationReport,
				FString::Printf(
					TEXT("Widget Blueprint '%s' looks like an incomplete placeholder: only %d Designer widget(s)."),
					*NormalizedPath.PackageName,
					Widgets.Num()));
		}

		TSet<FName> WidgetNames;
		TArray<FString> PreviewTextValues;
		for (UWidget* Widget : Widgets)
		{
			if (!Widget)
			{
				continue;
			}

			WidgetNames.Add(Widget->GetFName());
			if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				const FString TextValue = TextBlock->GetText().ToString();
				PreviewTextValues.Add(TextValue);
				if (CodeWidgetToWBPBridge::IsSuspiciousPlaceholderText(TextValue))
				{
					CodeWidgetToWBPBridge::AppendValidationError(
						OutValidationReport,
						FString::Printf(
							TEXT("Widget Blueprint '%s' has suspicious placeholder text on '%s': '%s'."),
							*NormalizedPath.PackageName,
							*Widget->GetName(),
							*TextValue));
				}
			}
		}

		for (const FString& ExpectedPreviewText : Spec.ExpectedPreviewTexts)
		{
			if (!CodeWidgetToWBPBridge::ContainsExpectedPreviewText(PreviewTextValues, ExpectedPreviewText))
			{
				CodeWidgetToWBPBridge::AppendValidationError(
					OutValidationReport,
					FString::Printf(
						TEXT("Widget Blueprint '%s' is missing expected preview text '%s'."),
						*NormalizedPath.PackageName,
						*ExpectedPreviewText));
			}
		}

		TArray<FName> ExpectedNames = Spec.ExpectedWidgetNames;
		if (Spec.bRequiresStableRootWrapper)
		{
			ExpectedNames.AddUnique(TEXT("DesignerRootOverlay"));
			ExpectedNames.AddUnique(TEXT("RootSizeBox"));
			ExpectedNames.AddUnique(TEXT("RootOverlay"));
		}

		for (const FName ExpectedName : ExpectedNames)
		{
			if (!WidgetNames.Contains(ExpectedName))
			{
				CodeWidgetToWBPBridge::AppendValidationError(
					OutValidationReport,
					FString::Printf(
						TEXT("Widget Blueprint '%s' is missing expected Designer widget '%s'."),
						*NormalizedPath.PackageName,
						*ExpectedName.ToString()));
			}
		}

		const TSet<FName> BindWidgetNames = CodeWidgetToWBPBridge::CollectBindWidgetNames(Spec.WidgetClass.Get());
		for (const FName BindWidgetName : BindWidgetNames)
		{
			if (!WidgetNames.Contains(BindWidgetName))
			{
				CodeWidgetToWBPBridge::AppendValidationWarning(
					OutValidationReport,
					FString::Printf(
						TEXT("Widget Blueprint '%s' does not expose BindWidgetOptional '%s'. Confirm this is an intentional fallback path."),
						*NormalizedPath.PackageName,
						*BindWidgetName.ToString()));
			}
		}

		CodeWidgetToWBPBridge::AppendValidationInfo(
			OutValidationReport,
			FString::Printf(
				TEXT("Validated '%s' with %d Designer widget(s)."),
				*NormalizedPath.PackageName,
				Widgets.Num()));
	}

	CodeWidgetToWBPBridge::AuditDuplicateWidgetAssets(Manifest, Specs, OutValidationReport);
	CodeWidgetToWBPBridge::ScanSourceForHardcodedVisualOverrides(NativeClass, OutValidationReport);
	CodeWidgetToWBPBridge::SaveValidationReportJson(OutValidationReport, OutReport);

	OutReport += CodeWidgetToWBPBridge::MakeReportSummary(OutValidationReport);
	if (OutValidationReport.bPassed)
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Display, TEXT("%s"), *OutReport);
	}
	else
	{
		UE_LOG(LogCodeWidgetToWBPBridge, Warning, TEXT("%s"), *OutReport);
	}
	return OutValidationReport.bPassed;
}
