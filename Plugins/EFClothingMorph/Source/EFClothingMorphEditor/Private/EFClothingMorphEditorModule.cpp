#include "EFClothingFitCompilerLibrary.h"

#include "AssetCompilingManager.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingSkeletonFingerprint.h"
#include "EFClothingSurfaceBinding.h"
#include "Engine/SkeletalMesh.h"
#include "Features/IModularFeatures.h"
#include "IPIEAuthorizer.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "FEFClothingMorphEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingMorphEditor, Log, All);

namespace EFClothingMorphEditorModulePrivate
{
	constexpr TCHAR CompatibilityReferencePath[] = TEXT("/Game/DazToUnreal/Multiple/Multiple.Multiple");

	static FString GetPackageGuid(const UObject* Object)
	{
		const UPackage* Package = IsValid(Object) ? Object->GetOutermost() : nullptr;
		return Package
			? Package->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens)
			: FString();
	}

	static bool FailFreshness(FString& OutReason, const FString& Reason)
	{
		OutReason = Reason;
		return false;
	}

	static const UEFClothingFitProfile* FindUniqueProfile(
		const UEFClothingFitRegistry* Registry,
		const FSoftObjectPath& SourcePath,
		const FSoftObjectPath& BodyPath,
		int32& OutMatchCount)
	{
		OutMatchCount = 0;
		const UEFClothingFitProfile* Match = nullptr;
		if (!IsValid(Registry))
		{
			return nullptr;
		}

		for (const UEFClothingFitProfile* Profile : Registry->Profiles)
		{
			if (IsValid(Profile)
				&& Profile->SourceGarment.ToSoftObjectPath() == SourcePath
				&& Profile->BodySurface.ToSoftObjectPath() == BodyPath)
			{
				++OutMatchCount;
				Match = Profile;
			}
		}
		return Match;
	}

	/**
	 * Cheap authoring-freshness gate. This deliberately checks immutable build
	 * provenance rather than running the compiler's exhaustive geometry QA on
	 * every Play request.
	 *
	 * FEFClothingGarmentRow::BuildCompileFingerprint intentionally excludes
	 * ShellThicknessCm and the per-garment runtime clearance values. Moving those
	 * sliders therefore never reaches the compiler from this gate.
	 */
	static bool IsCatalogFresh(
		const UEFClothingMorphDirectorPolicy* Director,
		const UEFClothingFitRegistry* Registry,
		const USkeletalMesh* CompatibilityReference,
		FString& OutReason)
	{
		OutReason.Reset();
		if (!IsValid(Director))
		{
			return FailFreshness(OutReason, TEXT("the configured Clothing Director could not be loaded"));
		}
		if (!IsValid(Registry))
		{
			return FailFreshness(OutReason, TEXT("the generated fit registry is missing"));
		}
		if (!IsValid(CompatibilityReference))
		{
			return FailFreshness(OutReason, TEXT("the protected Multiple compatibility reference is missing"));
		}

		int32 EnabledGarmentCount = 0;
		for (const FEFClothingGarmentRow& Row : Director->Garments)
		{
			if (!Row.bEnabled)
			{
				continue;
			}
			++EnabledGarmentCount;

			const FSoftObjectPath SourcePath = Row.SourceGarment.ToSoftObjectPath();
			const FSoftObjectPath BodyPath = Row.BodySurface.ToSoftObjectPath();
			if (!SourcePath.IsValid() || !BodyPath.IsValid())
			{
				return FailFreshness(OutReason, FString::Printf(
					TEXT("enabled garment %s has an empty source or body path"),
					*Row.GarmentId.ToString()));
			}

			USkeletalMesh* Source = Row.SourceGarment.LoadSynchronous();
			USkeletalMesh* Body = Row.BodySurface.LoadSynchronous();
			if (!IsValid(Source) || !IsValid(Body))
			{
				return FailFreshness(OutReason, FString::Printf(
					TEXT("enabled garment %s could not load its editable source mesh or reference body"),
					*Row.GarmentId.ToString()));
			}

			int32 MatchingProfileCount = 0;
			const UEFClothingFitProfile* Profile = FindUniqueProfile(
				Registry,
				SourcePath,
				BodyPath,
				MatchingProfileCount);
			if (!IsValid(Profile) || MatchingProfileCount != 1)
			{
				return FailFreshness(OutReason, FString::Printf(
					TEXT("garment %s has %d generated profiles for its source/body pair (expected exactly one)"),
					*Row.GarmentId.ToString(),
					MatchingProfileCount));
			}

			USkeletalMesh* Fitted = Profile->FittedGarment.LoadSynchronous();
			if (!IsValid(Fitted))
			{
				return FailFreshness(OutReason, FString::Printf(
					TEXT("garment %s is missing its internal fitted mesh"),
					*Row.GarmentId.ToString()));
			}

			const FString CurrentSourceContent = EFClothingSkeleton::BuildContentFingerprint(Source);
			const FString CurrentBodyContent = EFClothingSkeleton::BuildContentFingerprint(Body);
			const FString CurrentCompatibilityContent = EFClothingSkeleton::BuildContentFingerprint(CompatibilityReference);
			const FString CurrentFittedContent = EFClothingSkeleton::BuildContentFingerprint(Fitted);
			const FString CurrentSourceSkeleton = EFClothingSkeleton::BuildFingerprint(Source);
			const FString CurrentBodySkeleton = EFClothingSkeleton::BuildFingerprint(Body);
			const FString CurrentCompatibilitySkeleton = EFClothingSkeleton::BuildFingerprint(CompatibilityReference);
			const FString CurrentFittedSkeleton = EFClothingSkeleton::BuildFingerprint(Fitted);
			const FString CurrentSharedSkeleton = EFClothingSkeleton::BuildSharedSkeletonFingerprint(Source->GetSkeleton());
			const FString CurrentSharedSkeletonEditor = EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(Source->GetSkeleton());

			if (Profile->CompilerVersion != EFClothingMorphV26::CompilerVersion
				|| !Profile->BuildGuid.IsValid()
				|| Profile->GarmentCompileFingerprint != Row.BuildCompileFingerprint()
				|| Profile->SourceGarment.ToSoftObjectPath() != SourcePath
				|| Profile->BodySurface.ToSoftObjectPath() != BodyPath
				|| Profile->CompatibilityReference.ToSoftObjectPath() != FSoftObjectPath(CompatibilityReference)
				|| Profile->SourceContentFingerprint != CurrentSourceContent
				|| Profile->BodyContentFingerprint != CurrentBodyContent
				|| Profile->CompatibilityContentFingerprint != CurrentCompatibilityContent
				|| Profile->FittedContentFingerprint != CurrentFittedContent
				|| Profile->SourceSkeletonFingerprint != CurrentSourceSkeleton
				|| Profile->BodySkeletonFingerprint != CurrentBodySkeleton
				|| Profile->CompatibilitySkeletonFingerprint != CurrentCompatibilitySkeleton
				|| Profile->FittedSkeletonFingerprint != CurrentFittedSkeleton
				|| Profile->SharedSkeletonFingerprint != CurrentSharedSkeleton
				|| Profile->SharedSkeletonEditorFingerprint != CurrentSharedSkeletonEditor
				|| Source->GetSkeleton() != Body->GetSkeleton()
				|| Source->GetSkeleton() != CompatibilityReference->GetSkeleton()
				|| Source->GetSkeleton() != Fitted->GetSkeleton()
				|| Profile->SourcePackageGuid != GetPackageGuid(Source)
				|| Profile->BodyPackageGuid != GetPackageGuid(Body)
				|| Profile->CompatibilityPackageGuid != GetPackageGuid(CompatibilityReference)
				|| Profile->FittedPackageGuid != GetPackageGuid(Fitted))
			{
				return FailFreshness(OutReason, FString::Printf(
					TEXT("garment %s changed since its generated fit was compiled"),
					*Row.GarmentId.ToString()));
			}

			if (Row.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
			{
				continue;
			}

			UEFClothingSurfaceBinding* Binding = Profile->SurfaceBinding.LoadSynchronous();
			if (!IsValid(Binding)
				|| Binding->CompilerVersion != EFClothingMorphV26::CompilerVersion
				|| Binding->SchemaVersion != EFClothingMorphV26::SurfaceBindingSchemaVersion
				|| Binding->BuildGuid != Profile->BuildGuid
				|| Binding->SourceGarment.ToSoftObjectPath() != SourcePath
				|| Binding->FittedGarment.ToSoftObjectPath() != Profile->FittedGarment.ToSoftObjectPath()
				|| Binding->BodySurface.ToSoftObjectPath() != BodyPath
				|| Binding->SourceContentFingerprint != CurrentSourceContent
				|| Binding->FittedContentFingerprint != CurrentFittedContent
				|| Binding->BodyContentFingerprint != CurrentBodyContent
				|| Binding->SourceSkeletonFingerprint != CurrentSourceSkeleton
				|| Binding->FittedSkeletonFingerprint != CurrentFittedSkeleton
				|| Binding->BodySkeletonFingerprint != CurrentBodySkeleton
				|| Binding->SharedSkeletonFingerprint != CurrentSharedSkeleton
				|| Binding->LODPairBindings.IsEmpty())
			{
				return FailFreshness(OutReason, FString::Printf(
					TEXT("garment %s has a missing or stale GPU surface binding"),
					*Row.GarmentId.ToString()));
			}

			for (const FEFClothingSurfaceLODPairBinding& LODPair : Binding->LODPairBindings)
			{
				if (!LODPair.bCertified
					|| LODPair.GarmentTopology.LODIndex < 0
					|| LODPair.BodyTopology.LODIndex < 0
					|| LODPair.GarmentTopology.RenderVertexCount <= 0
					|| LODPair.BodyTopology.RenderVertexCount <= 0
					|| LODPair.GarmentTopology.TopologyFingerprint.IsEmpty()
					|| LODPair.BodyTopology.TopologyFingerprint.IsEmpty()
					|| LODPair.VertexBindings.Num() != LODPair.GarmentTopology.RenderVertexCount
					|| LODPair.Metrics.InvalidAnchorCount != 0)
				{
					return FailFreshness(OutReason, FString::Printf(
						TEXT("garment %s has an uncertified or incomplete GPU LOD binding"),
						*Row.GarmentId.ToString()));
				}
			}
		}

		if (EnabledGarmentCount <= 0)
		{
			return FailFreshness(OutReason, TEXT("the Clothing Director has no enabled garments"));
		}
		if (Registry->Profiles.Num() != EnabledGarmentCount)
		{
			return FailFreshness(OutReason, FString::Printf(
				TEXT("the generated registry contains %d profiles for %d enabled garments"),
				Registry->Profiles.Num(),
				EnabledGarmentCount));
		}

		return true;
	}

	static FEFClothingFitCompileOptions MakeCanonicalCompileOptions()
	{
		FEFClothingFitCompileOptions Options;
		Options.OutputRoot = TEXT("/EFClothingMorph/_Internal/Compiled/V26");
		Options.MinimumClearanceCm = 0.45f;
		Options.MaximumPushCm = 2.5f;
		Options.SmoothingIterations = 4;
		Options.MaximumInfluences = 8;
		Options.bTransferMissingBodyMorphs = false;
		Options.bCompileBodyMorphBindings = false;
		Options.MaximumTransferredMorphs = 0;
		Options.MinimumTransferredMorphDeltaCm = 0.02f;
		Options.MorphClearanceSampleCount = 4;
		Options.MaximumMorphRepairCm = 5.0f;
		Options.MorphPairRequests.Reset();
		Options.MorphPairGridResolution = 4;
		Options.MorphPairProbeCountPerAxis = 3;
		Options.MorphActivationEpsilon = 0.0f;
		Options.bCopyBodyDeformerToDerived = true;
		return Options;
	}

	static FText MakeBlockedPIEError(const FString& StaleReason, const FString& CompilerReport)
	{
		const FString ShortReport = CompilerReport.IsEmpty()
			? TEXT("The compiler did not provide a report.")
			: CompilerReport.Left(1200);
		return FText::Format(
			LOCTEXT(
				"PIEAutoRefreshFailed",
				"EF Clothing Morph could not refresh the stale garment fit before Play. PIE was blocked so an unbound or outdated garment is never shown.\n\nStale reason: {0}\n\nCompiler: {1}\n\nOpen the Clothing Director, correct the reported garment setting, and press Play again."),
			FText::FromString(StaleReason),
			FText::FromString(ShortReport));
	}
}

class FEFClothingMorphEditorModule final : public IModuleInterface, public IPIEAuthorizer
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
	}

protected:
	virtual TValueOrError<bool, FText> IsPIEAuthorizedInternal(bool bIsSimulateInEditor) const override
	{
		// The potentially blocking freshness check and compile belong exclusively in
		// RequestPIEPermissionInternal. Silent authorization queries stay non-blocking.
		return MakeValue(true);
	}

	virtual TValueOrError<bool, FText> RequestPIEPermissionInternal(bool bIsSimulateInEditor) const override
	{
		using namespace EFClothingMorphEditorModulePrivate;

		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		if (!Settings || !Settings->bEnabled)
		{
			return MakeValue(true);
		}

		// Native Skeletal Mesh Deform edits may leave render/DDC work in flight.
		// Complete it before comparing the editable source against its profile.
		FAssetCompilingManager::Get().FinishAllCompilation();

		UEFClothingMorphDirectorPolicy* Director = Settings->DirectorPolicy.LoadSynchronous();
		USkeletalMesh* CompatibilityReference = LoadObject<USkeletalMesh>(nullptr, CompatibilityReferencePath);
		if (!IsValid(Director))
		{
			return MakeError(FText::FromString(
				TEXT("EF Clothing Morph blocked Play because the configured Clothing Director could not be loaded.")));
		}
		if (!IsValid(CompatibilityReference))
		{
			return MakeError(FText::FromString(
				TEXT("EF Clothing Morph blocked Play because /Game/DazToUnreal/Multiple/Multiple is missing. The protected compatibility mesh was not modified.")));
		}

		FString PolicyError;
		if (!Director->ValidatePolicy(PolicyError))
		{
			return MakeError(FText::Format(
				LOCTEXT("InvalidDirectorBlockedPIE", "EF Clothing Morph blocked Play because the Clothing Director is invalid: {0}"),
				FText::FromString(PolicyError)));
		}

		UEFClothingFitRegistry* Registry = Settings->Registry.LoadSynchronous();
		FString StaleReason;
		if (IsCatalogFresh(Director, Registry, CompatibilityReference, StaleReason))
		{
			UE_LOG(LogEFClothingMorphEditor, Verbose, TEXT("PIE freshness gate: EF Clothing Morph catalog is current."));
			return MakeValue(true);
		}

		UE_LOG(
			LogEFClothingMorphEditor,
			Display,
			TEXT("PIE freshness gate is rebuilding the EF Clothing Morph catalog: %s"),
			*StaleReason);

		FScopedSlowTask SlowTask(
			1.0f,
			LOCTEXT("RefreshingGarmentFits", "Refreshing EF Clothing Morph garment fits before Play..."));
		if (!FApp::IsUnattended())
		{
			SlowTask.MakeDialog(false);
		}
		SlowTask.EnterProgressFrame(1.0f);

		const FEFClothingCatalogCompileResult CompileResult =
			UEFClothingFitCompilerLibrary::CompileGarmentCatalog(
				Director,
				CompatibilityReference,
				MakeCanonicalCompileOptions());
		if (!CompileResult.bSuccess)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Error,
				TEXT("Automatic pre-PIE EF Clothing Morph compilation failed: %s"),
				*CompileResult.Report);
			return MakeError(MakeBlockedPIEError(StaleReason, CompileResult.Report));
		}

		FAssetCompilingManager::Get().FinishAllCompilation();
		Registry = CompileResult.Registry;
		FString PostCompileReason;
		if (!IsCatalogFresh(Director, Registry, CompatibilityReference, PostCompileReason))
		{
			return MakeError(MakeBlockedPIEError(
				PostCompileReason,
				TEXT("Compilation reported success, but the published catalog did not pass the post-compile freshness check.")));
		}

		UE_LOG(
			LogEFClothingMorphEditor,
			Display,
			TEXT("Automatic pre-PIE EF Clothing Morph compilation succeeded: %s"),
			*CompileResult.Report);
		return MakeValue(true);
	}
};

IMPLEMENT_MODULE(FEFClothingMorphEditorModule, EFClothingMorphEditor)

#undef LOCTEXT_NAMESPACE
