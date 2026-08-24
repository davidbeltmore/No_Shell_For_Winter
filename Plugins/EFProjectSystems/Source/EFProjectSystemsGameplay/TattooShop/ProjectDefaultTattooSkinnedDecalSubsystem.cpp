#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Canvas.h"
#include "Engine/DataTable.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SkinnedDecalSampler.h"
#include "TattooShop/ProjectAutomaticTattooTypes.h"
#include "TattooShop/ProjectTattooShopInputSubsystem.h"
#include "TattooShop/ProjectTattooShopSettings.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogProjectDefaultTattooSkinnedDecal, Log, All);

namespace ProjectDefaultTattooSkinnedDecalPrivate
{
	constexpr float RetryDelaySeconds = 0.35f;
	constexpr float RuntimePollDelaySeconds = 0.20f;
	constexpr float DefaultTattooDecalSize = 6.0f;
	constexpr float DefaultTattooDecalDepth = 2.0f;
	constexpr int32 MinimumMaxDecals = 128;
	constexpr int32 HardMaximumManualTattooCount = 32;
	constexpr int32 HardMaximumProjectTattooCount = 64;
	constexpr int32 DefaultRuntimeAtlasCellSize = 512;
	constexpr int32 HardMaximumRuntimeAtlasSize = 4096;
	constexpr float RuntimeAtlasCellFill = 0.92f;
	constexpr int32 TattooShopPreviewReservedDecalIndex = 10;
	constexpr float ProjectionDistance = 12.0f;

	const TCHAR* AutomaticTattooTablePath = TEXT("/Game/_Game/AutomaticTattoo/DT_AutomaticTattoos.DT_AutomaticTattoos");
	const TCHAR* OverlayMaterialPath = TEXT("/SkinnedDecalComponent/M_SkinnedDecalOverlay.M_SkinnedDecalOverlay");
	const TCHAR* EmptyNormalTexturePath = TEXT("/SkinnedDecalComponent/Textures/SDC_EmptyNormal.SDC_EmptyNormal");

	const FName BaseColorParameterName(TEXT("BaseColor"));
	const FName CompactParameterName(TEXT("Compact"));
	const FName DecalColorParameterName(TEXT("DecalColor"));
	const FName DecalCompactParameterName(TEXT("DecalCompact"));
	const FName DecalNormalParameterName(TEXT("DecalNormal"));
	const FName DecalDepthParameterName(TEXT("DecalDepth"));
	const FName DecalUseCompactMapParameterName(TEXT("DecalUseCompactMap"));
	const FName DecalUseNormalMapParameterName(TEXT("DecalUseNormalMap"));
	const FName DecalUseOffsetParameterName(TEXT("DecalUseOffset"));
	const FName NormalParameterName(TEXT("Normal"));
	const FName SubImagesXParameterName(TEXT("SubImagesX"));
	const FName SubImagesYParameterName(TEXT("SubImagesY"));
	const FName AnimationFrameRateParameterName(TEXT("AnimationFrameRate"));
	const FName AnimationTotalFramesParameterName(TEXT("AnimationTotalFrames"));
	const FName EmissiveStrengthParameterName(TEXT("DecalEmissive Strength"));
	const FName UseEmissiveParameterName(TEXT("DecaulUseEmissive"));
	const FName TattooShopPreviewAtlasRowName(TEXT("__TattooShopPreview"));

	struct FRuntimeTattooLimits
	{
		int32 MaximumManualTattoos = HardMaximumManualTattooCount;
		int32 MaximumTotalLayers = HardMaximumProjectTattooCount;
		int32 AtlasCellSize = DefaultRuntimeAtlasCellSize;
		int32 MaximumAtlasSize = HardMaximumRuntimeAtlasSize;
	};

	FRuntimeTattooLimits ResolveRuntimeTattooLimits()
	{
		FRuntimeTattooLimits Limits;
		if (const UProjectTattooShopSettings* Settings = UProjectTattooShopSettings::Get())
		{
			Limits.AtlasCellSize = FMath::Clamp(Settings->AtlasCellSize, 64, DefaultRuntimeAtlasCellSize);
			Limits.MaximumAtlasSize = FMath::Clamp(
				Settings->MaxAtlasSize,
				Limits.AtlasCellSize,
				HardMaximumRuntimeAtlasSize);

			const int32 CellsPerAxis = FMath::Max(1, Limits.MaximumAtlasSize / Limits.AtlasCellSize);
			const int32 AtlasCapacity = CellsPerAxis * CellsPerAxis;
			Limits.MaximumTotalLayers = FMath::Clamp(
				Settings->MaxTotalTattooLayers,
				1,
				FMath::Min(HardMaximumProjectTattooCount, AtlasCapacity));
			Limits.MaximumManualTattoos = FMath::Clamp(
				Settings->MaxManualTattoos,
				1,
				FMath::Min(HardMaximumManualTattooCount, Limits.MaximumTotalLayers));
		}
		return Limits;
	}

	FColor AverageEdgeColor(const FColor* Pixels, const int32 Width, const int32 Height)
	{
		if (!Pixels || Width <= 0 || Height <= 0)
		{
			return FColor::White;
		}

		const int32 Step = FMath::Max(1, FMath::Min(Width, Height) / 32);
		FVector4f AccumulatedColor(0.0f, 0.0f, 0.0f, 0.0f);
		int32 SampleCount = 0;

		const auto AddSample = [&AccumulatedColor, &SampleCount, Pixels, Width](const int32 X, const int32 Y)
		{
			const FColor& Sample = Pixels[Y * Width + X];
			AccumulatedColor.X += static_cast<float>(Sample.R);
			AccumulatedColor.Y += static_cast<float>(Sample.G);
			AccumulatedColor.Z += static_cast<float>(Sample.B);
			AccumulatedColor.W += static_cast<float>(Sample.A);
			++SampleCount;
		};

		for (int32 X = 0; X < Width; X += Step)
		{
			AddSample(X, 0);
			AddSample(X, Height - 1);
		}
		for (int32 Y = 0; Y < Height; Y += Step)
		{
			AddSample(0, Y);
			AddSample(Width - 1, Y);
		}

		if (SampleCount <= 0)
		{
			return FColor::White;
		}

		return FColor(
			FMath::Clamp(FMath::RoundToInt(AccumulatedColor.X / SampleCount), 0, 255),
			FMath::Clamp(FMath::RoundToInt(AccumulatedColor.Y / SampleCount), 0, 255),
			FMath::Clamp(FMath::RoundToInt(AccumulatedColor.Z / SampleCount), 0, 255),
			FMath::Clamp(FMath::RoundToInt(AccumulatedColor.W / SampleCount), 0, 255));
	}

	uint8 DeriveTattooAlpha(const FColor& Pixel, const FColor& Background)
	{
		const int32 MaxChannelDelta = FMath::Max3(
			FMath::Abs(static_cast<int32>(Pixel.R) - static_cast<int32>(Background.R)),
			FMath::Abs(static_cast<int32>(Pixel.G) - static_cast<int32>(Background.G)),
			FMath::Abs(static_cast<int32>(Pixel.B) - static_cast<int32>(Background.B)));

		const int32 PixelLuma = (static_cast<int32>(Pixel.R) * 30 + static_cast<int32>(Pixel.G) * 59 + static_cast<int32>(Pixel.B) * 11) / 100;
		const int32 BackgroundLuma = (static_cast<int32>(Background.R) * 30 + static_cast<int32>(Background.G) * 59 + static_cast<int32>(Background.B) * 11) / 100;

		const int32 AlphaFromDifference = FMath::Clamp((MaxChannelDelta - 12) * 255 / 80, 0, 255);
		const int32 AlphaFromDarkness = FMath::Clamp((BackgroundLuma - PixelLuma - 8) * 255 / 96, 0, 255);
		return static_cast<uint8>(FMath::Max(AlphaFromDifference, AlphaFromDarkness));
	}

	UTexture2D* CreateTransientTextureFromPixels(
		UObject* Outer,
		const FName BaseName,
		const int32 Width,
		const int32 Height,
		const TArray<FColor>& Pixels,
		const bool bSRGB)
	{
		if (!Outer || Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
		{
			return nullptr;
		}

		const FName UniqueTextureName = MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), BaseName);
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, UniqueTextureName);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return nullptr;
		}

		Texture->NeverStream = true;
		Texture->SRGB = bSRGB;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
		if (!TextureData)
		{
			Mip.BulkData.Unlock();
			return nullptr;
		}

		FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* CreateSolidTexture(UObject* Outer, const FName BaseName, const FColor Color)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Color, 4);
		return CreateTransientTextureFromPixels(Outer, BaseName, 2, 2, Pixels, false);
	}

	FColor AlphaCompositeOver(const FColor& Back, const FColor& Front)
	{
		const float FrontAlpha = static_cast<float>(Front.A) / 255.0f;
		const float BackAlpha = static_cast<float>(Back.A) / 255.0f;
		const float OutAlpha = FrontAlpha + BackAlpha * (1.0f - FrontAlpha);
		if (OutAlpha <= KINDA_SMALL_NUMBER)
		{
			return FColor(0, 0, 0, 0);
		}

		const auto BlendChannel = [FrontAlpha, BackAlpha, OutAlpha](const uint8 BackChannel, const uint8 FrontChannel) -> uint8
		{
			const float FrontValue = static_cast<float>(FrontChannel) * FrontAlpha;
			const float BackValue = static_cast<float>(BackChannel) * BackAlpha * (1.0f - FrontAlpha);
			return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((FrontValue + BackValue) / OutAlpha), 0, 255));
		};

		return FColor(
			BlendChannel(Back.R, Front.R),
			BlendChannel(Back.G, Front.G),
			BlendChannel(Back.B, Front.B),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(OutAlpha * 255.0f), 0, 255)));
	}

	FString BuildOverlayMaterialSignature(
		USkinnedDecalSampler* Sampler,
		const TArray<FName>& RowNames,
		const TArray<const FProjectAutomaticTattooTableRow*>& TattooRows,
		const TMap<FName, FProjectTattooParameters>* ManualParametersByRow = nullptr)
	{
		FString Signature = FString::Printf(
			TEXT("Sampler=%s|Overlay=%s|Rows=%d"),
			*GetPathNameSafe(Sampler),
			*GetPathNameSafe(Sampler ? Sampler->OverlayBlendMaterialDynamic : nullptr),
			RowNames.Num());

		for (int32 RowIndex = 0; RowIndex < RowNames.Num() && RowIndex < TattooRows.Num(); ++RowIndex)
		{
			const FProjectAutomaticTattooTableRow* TattooRow = TattooRows[RowIndex];
			const FString TexturePath = TattooRow && !TattooRow->TattooTexture.IsNull()
				? TattooRow->TattooTexture.ToSoftObjectPath().ToString()
				: FString();
			Signature += FString::Printf(
				TEXT("|%s:%s:%d:%d:%s:%.4f:%.4f:%.4f:%.4f:%.4f"),
				*RowNames[RowIndex].ToString(),
				*TexturePath,
				TattooRow && TattooRow->bEnabled ? 1 : 0,
				TattooRow ? static_cast<int32>(TattooRow->PlacementPreset) : 0,
				TattooRow ? *TattooRow->AnchorBone.ToString() : TEXT("None"),
				TattooRow ? TattooRow->OffsetX : 0.0f,
				TattooRow ? TattooRow->OffsetY : 0.0f,
				TattooRow ? TattooRow->Size : 0.0f,
				TattooRow ? TattooRow->RotationDegrees : 0.0f,
				TattooRow ? TattooRow->ProjectionDistance : 0.0f);

			if (ManualParametersByRow)
			{
				if (const FProjectTattooParameters* Parameters = ManualParametersByRow->Find(RowNames[RowIndex]))
				{
					Signature += FString::Printf(
						TEXT(":Manual:Enabled=%d:Layer=%d:Preset=%d:Anchor=%s:OffsetX=%.6f:OffsetY=%.6f:Size=%.6f:RotationDegrees=%.6f:ProjectionDistance=%.6f:UseTint=%d:Opacity=%.6f:Color=%.6f,%.6f,%.6f,%.6f:RuntimeMissing=%d:Asset=%s:Runtime=%s"),
						Parameters->bEnabled ? 1 : 0,
						Parameters->LayerOrder,
						static_cast<int32>(Parameters->PlacementPreset),
						*Parameters->AnchorBone.ToString(),
						Parameters->OffsetX,
						Parameters->OffsetY,
						Parameters->Size,
						Parameters->RotationDegrees,
						Parameters->ProjectionDistance,
						Parameters->bUseTint ? 1 : 0,
						FMath::Clamp(Parameters->Opacity, 0.0f, 1.0f),
						Parameters->Color.R,
						Parameters->Color.G,
						Parameters->Color.B,
						Parameters->Color.A,
						Parameters->bRuntimeTextureMissing ? 1 : 0,
						*Parameters->TextureAssetPath.ToString(),
						*Parameters->RuntimeTextureId);
				}
			}
		}

		return Signature;
	}

	const TCHAR* PlacementPresetName(const EProjectAutomaticTattooPlacementPreset Preset)
	{
		switch (Preset)
		{
		case EProjectAutomaticTattooPlacementPreset::ChestFront:
			return TEXT("ChestFront");
		case EProjectAutomaticTattooPlacementPreset::AbdomenFront:
			return TEXT("AbdomenFront");
		case EProjectAutomaticTattooPlacementPreset::PelvisFront:
			return TEXT("PelvisFront");
		case EProjectAutomaticTattooPlacementPreset::UpperBack:
			return TEXT("UpperBack");
		case EProjectAutomaticTattooPlacementPreset::LowerBack:
			return TEXT("LowerBack");
		case EProjectAutomaticTattooPlacementPreset::LeftUpperArm:
			return TEXT("LeftUpperArm");
		case EProjectAutomaticTattooPlacementPreset::RightUpperArm:
			return TEXT("RightUpperArm");
		case EProjectAutomaticTattooPlacementPreset::LeftForearm:
			return TEXT("LeftForearm");
		case EProjectAutomaticTattooPlacementPreset::RightForearm:
			return TEXT("RightForearm");
		case EProjectAutomaticTattooPlacementPreset::LeftThigh:
			return TEXT("LeftThigh");
		case EProjectAutomaticTattooPlacementPreset::RightThigh:
			return TEXT("RightThigh");
		case EProjectAutomaticTattooPlacementPreset::LeftUpperThigh:
			return TEXT("LeftThigh");
		case EProjectAutomaticTattooPlacementPreset::RightUpperThigh:
			return TEXT("RightThigh");
		case EProjectAutomaticTattooPlacementPreset::LeftBackThigh:
			return TEXT("LeftBackThigh");
		case EProjectAutomaticTattooPlacementPreset::RightBackThigh:
			return TEXT("RightBackThigh");
		case EProjectAutomaticTattooPlacementPreset::LeftCalf:
			return TEXT("LeftCalf");
		case EProjectAutomaticTattooPlacementPreset::RightCalf:
			return TEXT("RightCalf");
		case EProjectAutomaticTattooPlacementPreset::LeftBackCalf:
			return TEXT("LeftBackCalf");
		case EProjectAutomaticTattooPlacementPreset::RightBackCalf:
			return TEXT("RightBackCalf");
		case EProjectAutomaticTattooPlacementPreset::LeftHand:
			return TEXT("LeftHand");
		case EProjectAutomaticTattooPlacementPreset::RightHand:
			return TEXT("RightHand");
		case EProjectAutomaticTattooPlacementPreset::LeftFoot:
			return TEXT("LeftFoot");
		case EProjectAutomaticTattooPlacementPreset::RightFoot:
			return TEXT("RightFoot");
		default:
			return TEXT("ChestFront");
		}
	}

	FName ResolvePresetAnchorBone(
		const USkeletalMeshComponent* TargetMesh,
		const EProjectAutomaticTattooPlacementPreset Preset)
	{
		if (!IsValid(TargetMesh))
		{
			return NAME_None;
		}

		const auto ResolveFirstExisting = [TargetMesh](std::initializer_list<const TCHAR*> CandidateNames) -> FName
		{
			for (const TCHAR* CandidateName : CandidateNames)
			{
				const FName CandidateBone(CandidateName);
				if (TargetMesh->GetBoneIndex(CandidateBone) != INDEX_NONE)
				{
					return CandidateBone;
				}
			}
			return NAME_None;
		};

		switch (Preset)
		{
		case EProjectAutomaticTattooPlacementPreset::AbdomenFront:
			return ResolveFirstExisting({ TEXT("spine_02"), TEXT("abdomenUpper"), TEXT("abdomen2"), TEXT("spine2"), TEXT("pelvis") });
		case EProjectAutomaticTattooPlacementPreset::PelvisFront:
			return ResolveFirstExisting({ TEXT("pelvis"), TEXT("hip"), TEXT("spine_01"), TEXT("spine1") });
		case EProjectAutomaticTattooPlacementPreset::UpperBack:
			return ResolveFirstExisting({ TEXT("spine_04"), TEXT("spine_03"), TEXT("chest"), TEXT("upperchest"), TEXT("spine2") });
		case EProjectAutomaticTattooPlacementPreset::LowerBack:
			return ResolveFirstExisting({ TEXT("spine_02"), TEXT("spine_01"), TEXT("pelvis"), TEXT("spine1") });
		case EProjectAutomaticTattooPlacementPreset::LeftUpperArm:
			return ResolveFirstExisting({ TEXT("upperarm_l"), TEXT("upperarm_twist_01_l"), TEXT("clavicle_l"), TEXT("lShldrBend") });
		case EProjectAutomaticTattooPlacementPreset::RightUpperArm:
			return ResolveFirstExisting({ TEXT("upperarm_r"), TEXT("upperarm_twist_01_r"), TEXT("clavicle_r"), TEXT("rShldrBend") });
		case EProjectAutomaticTattooPlacementPreset::LeftForearm:
			return ResolveFirstExisting({ TEXT("lowerarm_l"), TEXT("lowerarm_twist_01_l"), TEXT("hand_l"), TEXT("lForearmBend") });
		case EProjectAutomaticTattooPlacementPreset::RightForearm:
			return ResolveFirstExisting({ TEXT("lowerarm_r"), TEXT("lowerarm_twist_01_r"), TEXT("hand_r"), TEXT("rForearmBend") });
		case EProjectAutomaticTattooPlacementPreset::LeftThigh:
		case EProjectAutomaticTattooPlacementPreset::LeftUpperThigh:
			return ResolveFirstExisting({ TEXT("thigh_l"), TEXT("thigh_twist_01_l"), TEXT("lThighBend"), TEXT("lThighTwist"), TEXT("pelvis") });
		case EProjectAutomaticTattooPlacementPreset::RightThigh:
		case EProjectAutomaticTattooPlacementPreset::RightUpperThigh:
			return ResolveFirstExisting({ TEXT("thigh_r"), TEXT("thigh_twist_01_r"), TEXT("rThighBend"), TEXT("rThighTwist"), TEXT("pelvis") });
		case EProjectAutomaticTattooPlacementPreset::LeftBackThigh:
			return ResolveFirstExisting({ TEXT("thigh_l"), TEXT("thigh_twist_01_l"), TEXT("lThighBend"), TEXT("lThighTwist"), TEXT("pelvis") });
		case EProjectAutomaticTattooPlacementPreset::RightBackThigh:
			return ResolveFirstExisting({ TEXT("thigh_r"), TEXT("thigh_twist_01_r"), TEXT("rThighBend"), TEXT("rThighTwist"), TEXT("pelvis") });
		case EProjectAutomaticTattooPlacementPreset::LeftCalf:
		case EProjectAutomaticTattooPlacementPreset::LeftBackCalf:
			return ResolveFirstExisting({ TEXT("calf_l"), TEXT("calf_twist_01_l"), TEXT("lShin"), TEXT("lShinBend"), TEXT("thigh_l") });
		case EProjectAutomaticTattooPlacementPreset::RightCalf:
		case EProjectAutomaticTattooPlacementPreset::RightBackCalf:
			return ResolveFirstExisting({ TEXT("calf_r"), TEXT("calf_twist_01_r"), TEXT("rShin"), TEXT("rShinBend"), TEXT("thigh_r") });
		case EProjectAutomaticTattooPlacementPreset::LeftHand:
			return ResolveFirstExisting({ TEXT("hand_l"), TEXT("lHand"), TEXT("middle_01_l"), TEXT("index_01_l"), TEXT("lMid1") });
		case EProjectAutomaticTattooPlacementPreset::RightHand:
			return ResolveFirstExisting({ TEXT("hand_r"), TEXT("rHand"), TEXT("middle_01_r"), TEXT("index_01_r"), TEXT("rMid1") });
		case EProjectAutomaticTattooPlacementPreset::LeftFoot:
			return ResolveFirstExisting({ TEXT("foot_l"), TEXT("ball_l"), TEXT("toe_l"), TEXT("lFoot"), TEXT("lToe"), TEXT("calf_l"), TEXT("lShin") });
		case EProjectAutomaticTattooPlacementPreset::RightFoot:
			return ResolveFirstExisting({ TEXT("foot_r"), TEXT("ball_r"), TEXT("toe_r"), TEXT("rFoot"), TEXT("rToe"), TEXT("calf_r"), TEXT("rShin") });
		case EProjectAutomaticTattooPlacementPreset::ChestFront:
		default:
			return ResolveFirstExisting({ TEXT("spine_04"), TEXT("spine_03"), TEXT("spine_05"), TEXT("chest"), TEXT("upperchest"), TEXT("abdomenUpper"), TEXT("spine2") });
		}
	}

	int32 ResolveFirstExistingBoneIndex(
		const USkeletalMeshComponent* TargetMesh,
		std::initializer_list<const TCHAR*> CandidateNames)
	{
		if (!IsValid(TargetMesh))
		{
			return INDEX_NONE;
		}

		for (const TCHAR* CandidateName : CandidateNames)
		{
			const int32 BoneIndex = TargetMesh->GetBoneIndex(FName(CandidateName));
			if (BoneIndex != INDEX_NONE)
			{
				return BoneIndex;
			}
		}

		return INDEX_NONE;
	}

	bool TryGetReferencePoseLocation(
		const USkeletalMeshComponent* TargetMesh,
		const FReferenceSkeleton& RefSkeleton,
		std::initializer_list<const TCHAR*> CandidateNames,
		FVector& OutLocation)
	{
		const int32 BoneIndex = ResolveFirstExistingBoneIndex(TargetMesh, CandidateNames);
		if (BoneIndex == INDEX_NONE || BoneIndex >= RefSkeleton.GetNum())
		{
			return false;
		}

		OutLocation = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, BoneIndex).GetLocation();
		return true;
	}

	void ResolveCharacterReferenceAxes(
		const USkeletalMeshComponent* TargetMesh,
		FVector& OutForward,
		FVector& OutRight,
		FVector& OutUp);

	bool TryResolveHandReferencePoseSurfaceFrame(
		const USkeletalMeshComponent* TargetMesh,
		const FReferenceSkeleton& RefSkeleton,
		const EProjectAutomaticTattooPlacementPreset Preset,
		FVector& InOutSurfaceOrigin,
		FVector& OutNormal,
		FVector& OutRight,
		FVector& OutUp)
	{
		const bool bLeftHand = Preset == EProjectAutomaticTattooPlacementPreset::LeftHand;
		const bool bRightHand = Preset == EProjectAutomaticTattooPlacementPreset::RightHand;
		if (!bLeftHand && !bRightHand)
		{
			return false;
		}

		const int32 HandBoneIndex = bLeftHand
			? ResolveFirstExistingBoneIndex(TargetMesh, { TEXT("hand_l"), TEXT("lHand") })
			: ResolveFirstExistingBoneIndex(TargetMesh, { TEXT("hand_r"), TEXT("rHand") });
		if (HandBoneIndex == INDEX_NONE || HandBoneIndex >= RefSkeleton.GetNum())
		{
			return false;
		}

		FVector CharacterForward;
		FVector CharacterRight;
		FVector CharacterUp;
		ResolveCharacterReferenceAxes(TargetMesh, CharacterForward, CharacterRight, CharacterUp);

		const FVector HandLocation =
			FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, HandBoneIndex).GetLocation();
		FVector FingerBaseLocation = FVector::ZeroVector;
		FVector ForearmLocation = FVector::ZeroVector;
		FVector IndexLocation = FVector::ZeroVector;
		FVector PinkyLocation = FVector::ZeroVector;
		FVector ThumbLocation = FVector::ZeroVector;

		const bool bHasFingerBase = bLeftHand
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("middle_01_l"), TEXT("lMid1"), TEXT("index_01_l"), TEXT("lIndex1") }, FingerBaseLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("middle_01_r"), TEXT("rMid1"), TEXT("index_01_r"), TEXT("rIndex1") }, FingerBaseLocation);
		const bool bHasForearm = bLeftHand
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("lowerarm_l"), TEXT("lowerarm_twist_01_l"), TEXT("lForearmBend"), TEXT("lForearm") }, ForearmLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("lowerarm_r"), TEXT("lowerarm_twist_01_r"), TEXT("rForearmBend"), TEXT("rForearm") }, ForearmLocation);
		const bool bHasIndex = bLeftHand
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("index_01_l"), TEXT("lIndex1") }, IndexLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("index_01_r"), TEXT("rIndex1") }, IndexLocation);
		const bool bHasPinky = bLeftHand
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("pinky_01_l"), TEXT("lPinky1"), TEXT("lSmall1") }, PinkyLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("pinky_01_r"), TEXT("rPinky1"), TEXT("rSmall1") }, PinkyLocation);
		const bool bHasThumb = bLeftHand
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("thumb_01_l"), TEXT("lThumb1") }, ThumbLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("thumb_01_r"), TEXT("rThumb1") }, ThumbLocation);

		FVector TowardFingers = bHasFingerBase ? (FingerBaseLocation - HandLocation).GetSafeNormal() : FVector::ZeroVector;
		FVector TowardWrist = bHasForearm ? (ForearmLocation - HandLocation).GetSafeNormal() : FVector::ZeroVector;
		if (TowardWrist.IsNearlyZero() && !TowardFingers.IsNearlyZero())
		{
			TowardWrist = -TowardFingers;
		}
		if (TowardFingers.IsNearlyZero() && !TowardWrist.IsNearlyZero())
		{
			TowardFingers = -TowardWrist;
		}
		if (TowardWrist.IsNearlyZero())
		{
			TowardWrist = CharacterUp;
		}

		InOutSurfaceOrigin = HandLocation;
		if (bHasFingerBase && !TowardFingers.IsNearlyZero())
		{
			const float FingerDistance = FVector::Distance(HandLocation, FingerBaseLocation);
			InOutSurfaceOrigin += TowardFingers * FMath::Clamp(FingerDistance * 0.30f, 0.75f, 3.5f);
		}

		OutUp = TowardWrist;

		FVector AcrossHand = FVector::ZeroVector;
		if (bHasIndex && bHasPinky)
		{
			AcrossHand = (IndexLocation - PinkyLocation).GetSafeNormal();
		}
		else if (bHasIndex && bHasThumb)
		{
			AcrossHand = (IndexLocation - ThumbLocation).GetSafeNormal();
		}

		const FVector FallbackNormal = bLeftHand ? -CharacterRight : CharacterRight;
		if (!AcrossHand.IsNearlyZero())
		{
			OutRight = (AcrossHand - OutUp * FVector::DotProduct(AcrossHand, OutUp)).GetSafeNormal();
			if (OutRight.IsNearlyZero())
			{
				OutRight = CharacterForward;
			}

			OutNormal = FVector::CrossProduct(OutRight, OutUp).GetSafeNormal();
			if (FVector::DotProduct(OutNormal, FallbackNormal) < 0.0f)
			{
				OutNormal *= -1.0f;
				OutRight *= -1.0f;
			}
		}
		else
		{
			OutNormal = (FallbackNormal - OutUp * FVector::DotProduct(FallbackNormal, OutUp)).GetSafeNormal();
			OutRight = FVector::CrossProduct(OutUp, OutNormal).GetSafeNormal();
		}

		if (OutNormal.IsNearlyZero())
		{
			OutNormal = FallbackNormal;
		}
		if (OutRight.IsNearlyZero())
		{
			OutRight = FVector::CrossProduct(OutUp, OutNormal).GetSafeNormal();
		}
		return true;
	}

	bool TryResolveFootReferencePoseSurfaceFrame(
		const USkeletalMeshComponent* TargetMesh,
		const FReferenceSkeleton& RefSkeleton,
		const EProjectAutomaticTattooPlacementPreset Preset,
		FVector& InOutSurfaceOrigin,
		FVector& OutNormal,
		FVector& OutRight,
		FVector& OutUp)
	{
		const bool bLeftFoot = Preset == EProjectAutomaticTattooPlacementPreset::LeftFoot;
		const bool bRightFoot = Preset == EProjectAutomaticTattooPlacementPreset::RightFoot;
		if (!bLeftFoot && !bRightFoot)
		{
			return false;
		}

		const int32 FootBoneIndex = bLeftFoot
			? ResolveFirstExistingBoneIndex(TargetMesh, { TEXT("foot_l"), TEXT("lFoot") })
			: ResolveFirstExistingBoneIndex(TargetMesh, { TEXT("foot_r"), TEXT("rFoot") });
		if (FootBoneIndex == INDEX_NONE || FootBoneIndex >= RefSkeleton.GetNum())
		{
			return false;
		}

		FVector CharacterForward;
		FVector CharacterRight;
		FVector CharacterUp;
		ResolveCharacterReferenceAxes(TargetMesh, CharacterForward, CharacterRight, CharacterUp);

		const FVector FootLocation =
			FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, FootBoneIndex).GetLocation();
		FVector ToeBaseLocation = FVector::ZeroVector;
		FVector LegLocation = FVector::ZeroVector;

		const bool bHasToeBase = bLeftFoot
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("ball_l"), TEXT("toe_l"), TEXT("lToe"), TEXT("lToe1"), TEXT("lBigToe1") }, ToeBaseLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("ball_r"), TEXT("toe_r"), TEXT("rToe"), TEXT("rToe1"), TEXT("rBigToe1") }, ToeBaseLocation);
		const bool bHasLeg = bLeftFoot
			? TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("calf_l"), TEXT("calf_twist_01_l"), TEXT("lShin"), TEXT("lShinBend"), TEXT("thigh_l") }, LegLocation)
			: TryGetReferencePoseLocation(TargetMesh, RefSkeleton, { TEXT("calf_r"), TEXT("calf_twist_01_r"), TEXT("rShin"), TEXT("rShinBend"), TEXT("thigh_r") }, LegLocation);

		FVector TowardToes = bHasToeBase ? (ToeBaseLocation - FootLocation).GetSafeNormal() : FVector::ZeroVector;
		FVector TowardAnkle = bHasLeg ? (LegLocation - FootLocation).GetSafeNormal() : FVector::ZeroVector;
		if (TowardAnkle.IsNearlyZero() && !TowardToes.IsNearlyZero())
		{
			TowardAnkle = -TowardToes;
		}
		if (TowardToes.IsNearlyZero() && !TowardAnkle.IsNearlyZero())
		{
			TowardToes = -TowardAnkle;
		}
		if (TowardAnkle.IsNearlyZero())
		{
			TowardAnkle = -CharacterForward;
			TowardToes = CharacterForward;
		}

		InOutSurfaceOrigin = FootLocation;
		if (bHasToeBase && !TowardToes.IsNearlyZero())
		{
			const float ToeDistance = FVector::Distance(FootLocation, ToeBaseLocation);
			InOutSurfaceOrigin += TowardToes * FMath::Clamp(ToeDistance * 0.68f, 2.0f, 8.0f);
		}

		OutUp = !TowardToes.IsNearlyZero() ? TowardToes : -TowardAnkle;
		OutNormal = (CharacterUp - OutUp * FVector::DotProduct(CharacterUp, OutUp)).GetSafeNormal();
		if (OutNormal.IsNearlyZero())
		{
			OutNormal = CharacterUp;
		}

		OutRight = FVector::CrossProduct(OutUp, OutNormal).GetSafeNormal();
		if (OutRight.IsNearlyZero())
		{
			OutRight = CharacterRight;
		}
		if (FVector::DotProduct(OutRight, CharacterRight) < 0.0f)
		{
			OutRight *= -1.0f;
		}

		OutNormal = FVector::CrossProduct(OutRight, OutUp).GetSafeNormal();
		if (FVector::DotProduct(OutNormal, CharacterUp) < 0.0f)
		{
			OutNormal *= -1.0f;
			OutRight *= -1.0f;
		}
		return true;
	}

	void ResolveCharacterReferenceAxes(
		const USkeletalMeshComponent* TargetMesh,
		FVector& OutForward,
		FVector& OutRight,
		FVector& OutUp)
	{
		OutForward = FVector::ForwardVector;
		OutRight = FVector::RightVector;
		OutUp = FVector::UpVector;

		if (!IsValid(TargetMesh))
		{
			return;
		}

		const FTransform MeshTransform = TargetMesh->GetComponentTransform();
		const AActor* Owner = TargetMesh->GetOwner();
		const FVector WorldForward = Owner ? Owner->GetActorForwardVector() : TargetMesh->GetForwardVector();
		const FVector WorldRight = Owner ? Owner->GetActorRightVector() : TargetMesh->GetRightVector();
		const FVector WorldUp = Owner ? Owner->GetActorUpVector() : TargetMesh->GetUpVector();

		OutForward = MeshTransform.InverseTransformVectorNoScale(WorldForward).GetSafeNormal();
		OutRight = MeshTransform.InverseTransformVectorNoScale(WorldRight).GetSafeNormal();
		OutUp = MeshTransform.InverseTransformVectorNoScale(WorldUp).GetSafeNormal();

		if (OutForward.IsNearlyZero())
		{
			OutForward = FVector::ForwardVector;
		}
		if (OutRight.IsNearlyZero())
		{
			OutRight = FVector::RightVector;
		}
		if (OutUp.IsNearlyZero())
		{
			OutUp = FVector::UpVector;
		}
	}

	void ResolvePresetSurfaceFrame(
		const USkeletalMeshComponent* TargetMesh,
		const FReferenceSkeleton& RefSkeleton,
		const EProjectAutomaticTattooPlacementPreset Preset,
		FVector& InOutSurfaceOrigin,
		FVector& OutNormal,
		FVector& OutRight,
		FVector& OutUp)
	{
		FVector CharacterForward;
		FVector CharacterRight;
		FVector CharacterUp;
		ResolveCharacterReferenceAxes(TargetMesh, CharacterForward, CharacterRight, CharacterUp);

		OutNormal = CharacterForward;
		OutRight = CharacterRight;
		OutUp = CharacterUp;

		switch (Preset)
		{
		case EProjectAutomaticTattooPlacementPreset::UpperBack:
		case EProjectAutomaticTattooPlacementPreset::LowerBack:
		case EProjectAutomaticTattooPlacementPreset::LeftBackThigh:
		case EProjectAutomaticTattooPlacementPreset::RightBackThigh:
		case EProjectAutomaticTattooPlacementPreset::LeftBackCalf:
		case EProjectAutomaticTattooPlacementPreset::RightBackCalf:
			OutNormal = -CharacterForward;
			OutRight = CharacterRight;
			break;
		case EProjectAutomaticTattooPlacementPreset::LeftUpperArm:
		case EProjectAutomaticTattooPlacementPreset::LeftForearm:
			OutNormal = -CharacterRight;
			OutRight = CharacterForward;
			break;
		case EProjectAutomaticTattooPlacementPreset::RightUpperArm:
		case EProjectAutomaticTattooPlacementPreset::RightForearm:
			OutNormal = CharacterRight;
			OutRight = -CharacterForward;
			break;
		case EProjectAutomaticTattooPlacementPreset::LeftHand:
		case EProjectAutomaticTattooPlacementPreset::RightHand:
			if (TryResolveHandReferencePoseSurfaceFrame(
				TargetMesh,
				RefSkeleton,
				Preset,
				InOutSurfaceOrigin,
				OutNormal,
				OutRight,
				OutUp))
			{
				break;
			}
			OutNormal = Preset == EProjectAutomaticTattooPlacementPreset::LeftHand ? -CharacterRight : CharacterRight;
			OutRight = Preset == EProjectAutomaticTattooPlacementPreset::LeftHand ? CharacterForward : -CharacterForward;
			break;
		case EProjectAutomaticTattooPlacementPreset::LeftFoot:
		case EProjectAutomaticTattooPlacementPreset::RightFoot:
			if (TryResolveFootReferencePoseSurfaceFrame(
				TargetMesh,
				RefSkeleton,
				Preset,
				InOutSurfaceOrigin,
				OutNormal,
				OutRight,
				OutUp))
			{
				break;
			}
			OutNormal = CharacterForward;
			OutRight = CharacterRight;
			break;
		case EProjectAutomaticTattooPlacementPreset::ChestFront:
		case EProjectAutomaticTattooPlacementPreset::AbdomenFront:
		case EProjectAutomaticTattooPlacementPreset::PelvisFront:
		case EProjectAutomaticTattooPlacementPreset::LeftThigh:
		case EProjectAutomaticTattooPlacementPreset::RightThigh:
		case EProjectAutomaticTattooPlacementPreset::LeftUpperThigh:
		case EProjectAutomaticTattooPlacementPreset::RightUpperThigh:
		case EProjectAutomaticTattooPlacementPreset::LeftCalf:
		case EProjectAutomaticTattooPlacementPreset::RightCalf:
		default:
			break;
		}

		OutNormal = OutNormal.GetSafeNormal();
		OutRight = (OutRight - OutNormal * FVector::DotProduct(OutRight, OutNormal)).GetSafeNormal();
		OutUp = (OutUp - OutNormal * FVector::DotProduct(OutUp, OutNormal)).GetSafeNormal();
		if (OutRight.IsNearlyZero())
		{
			OutRight = FVector::CrossProduct(OutUp, OutNormal).GetSafeNormal();
		}
		if (OutUp.IsNearlyZero())
		{
			OutUp = FVector::CrossProduct(OutNormal, OutRight).GetSafeNormal();
		}
	}

	FString BuildTattooPlacementSignature(
		const FName RowName,
		const FProjectAutomaticTattooTableRow* TattooRow,
		const FName AnchorBone,
		const int32 SubUV)
	{
		if (!TattooRow)
		{
			return FString();
		}

		return FString::Printf(
			TEXT("%s|Preset=%s|Bone=%s|SubUV=%d|OffsetX=%.4f|OffsetY=%.4f|Size=%.4f|Rotation=%.4f|Projection=%.4f|Enabled=%d"),
			*RowName.ToString(),
			PlacementPresetName(TattooRow->PlacementPreset),
			*AnchorBone.ToString(),
			SubUV,
			TattooRow->OffsetX,
			TattooRow->OffsetY,
			TattooRow->Size,
			TattooRow->RotationDegrees,
			TattooRow->ProjectionDistance,
			TattooRow->bEnabled ? 1 : 0);
	}

	FString BuildTattooPlacementGroupKey(
		const FProjectAutomaticTattooTableRow* TattooRow,
		const FName AnchorBone)
	{
		if (!TattooRow)
		{
			return FString();
		}

		return FString::Printf(
			TEXT("Preset=%s|Bone=%s|OffsetX=%.4f|OffsetY=%.4f|Size=%.4f|Rotation=%.4f|Projection=%.4f"),
			PlacementPresetName(TattooRow->PlacementPreset),
			*AnchorBone.ToString(),
			TattooRow->OffsetX,
			TattooRow->OffsetY,
			TattooRow->Size,
			TattooRow->RotationDegrees,
			TattooRow->ProjectionDistance);
	}

	int32 ResolveProjectDecalIndex(
		const int32 RequestedDecalIndex,
		const int32 PreviewDecalIndex,
		const int32 MaxDecals,
		const TMap<FName, int32>& AutomaticDecalIndices)
	{
		if (RequestedDecalIndex != INDEX_NONE)
		{
			return RequestedDecalIndex;
		}

		TSet<int32> UsedIndices;
		for (const TPair<FName, int32>& AutomaticLayer : AutomaticDecalIndices)
		{
			if (AutomaticLayer.Value != INDEX_NONE)
			{
				UsedIndices.Add(AutomaticLayer.Value);
			}
		}
		if (PreviewDecalIndex != INDEX_NONE)
		{
			UsedIndices.Add(PreviewDecalIndex);
		}
		UsedIndices.Add(TattooShopPreviewReservedDecalIndex);

		const int32 SafeMaxDecals = FMath::Max(1, MaxDecals);
		for (int32 CandidateIndex = 0; CandidateIndex < SafeMaxDecals; ++CandidateIndex)
		{
			if (!UsedIndices.Contains(CandidateIndex))
			{
				return CandidateIndex;
			}
		}

		return INDEX_NONE;
	}

	bool BuildReferencePoseDecalData(
		const USkeletalMeshComponent* TargetMesh,
		const FName AnchorBone,
		const FProjectAutomaticTattooTableRow* TattooRow,
		const int32 DecalIndex,
		const int32 SubUV,
		FSkinnedDecalData& OutData,
		FVector& OutReferenceLocation,
		FQuat& OutReferenceRotation)
	{
		if (!IsValid(TargetMesh) || !IsValid(TargetMesh->GetSkinnedAsset()) || AnchorBone.IsNone() || !TattooRow)
		{
			return false;
		}

		const int32 BoneIndex = TargetMesh->GetBoneIndex(AnchorBone);
		const FReferenceSkeleton& RefSkeleton = TargetMesh->GetSkinnedAsset()->GetRefSkeleton();
		if (BoneIndex == INDEX_NONE || BoneIndex >= RefSkeleton.GetNum())
		{
			return false;
		}

		const FTransform ReferenceTransform = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, BoneIndex);
		FVector SurfaceNormal;
		FVector SurfaceRight;
		FVector SurfaceUp;
		FVector SurfaceOrigin = ReferenceTransform.GetLocation();
		ResolvePresetSurfaceFrame(
			TargetMesh,
			RefSkeleton,
			TattooRow->PlacementPreset,
			SurfaceOrigin,
			SurfaceNormal,
			SurfaceRight,
			SurfaceUp);

		const float StableProjectionDistance = FMath::Max(0.0f, TattooRow->ProjectionDistance);
		OutReferenceLocation = SurfaceOrigin
			+ SurfaceNormal * StableProjectionDistance
			+ SurfaceRight * TattooRow->OffsetX
			+ SurfaceUp * TattooRow->OffsetY;

		const FQuat BaseRotation = FRotationMatrix::MakeFromXZ(SurfaceNormal, SurfaceUp).ToQuat();
		OutReferenceRotation = FQuat(SurfaceNormal, FMath::DegreesToRadians(TattooRow->RotationDegrees)) * BaseRotation;

		FMatrix DecalMatrix = FTransform(OutReferenceRotation).ToMatrixNoScale();
		OutData = FSkinnedDecalData();
		OutData.Index = DecalIndex;
		OutData.DecalLocation = OutReferenceLocation;
		DecalMatrix.GetUnitAxes(OutData.BasisX, OutData.BasisY, OutData.BasisZ);
		OutData.Info = FVector(FMath::Max(1.0f, TattooRow->Size), SubUV, 0.0f);
		return true;
	}

	int32 ScoreSkeletalMeshComponent(const USkeletalMeshComponent* Component)
	{
		if (!IsValid(Component) || !IsValid(Component->GetSkinnedAsset()))
		{
			return MIN_int32;
		}

		const FString ComponentName = Component->GetName().ToLower();
		const FString AssetPath = Component->GetSkinnedAsset()->GetPathName().ToLower();
		const FString CombinedIdentity = ComponentName + TEXT(" ") + AssetPath;

		int32 Score = 0;
		if (ComponentName == TEXT("female"))
		{
			Score += 1000;
		}
		if (ComponentName.Contains(TEXT("female")))
		{
			Score += 500;
		}
		if (AssetPath.Contains(TEXT("/game/daztounreal/female/female.")))
		{
			Score += 1000;
		}
		else if (AssetPath.Contains(TEXT("/game/daztounreal/female/")))
		{
			Score += 300;
		}
		if (AssetPath.Contains(TEXT("female")))
		{
			Score += 150;
		}

		if (CombinedIdentity.Contains(TEXT("multiple")))
		{
			Score -= 600;
		}

		const TCHAR* NonBodyTokens[] = {
			TEXT("hair"),
			TEXT("eyelash"),
			TEXT("lash"),
			TEXT("brow"),
			TEXT("teeth"),
			TEXT("mouth"),
			TEXT("eye")
		};

		for (const TCHAR* Token : NonBodyTokens)
		{
			if (CombinedIdentity.Contains(Token))
			{
				Score -= 300;
			}
		}

		if (Component->GetNumMaterials() > 0)
		{
			Score += 25;
		}
		if (Component->IsVisible() && !Component->bHiddenInGame)
		{
			Score += 2000;
		}
		else
		{
			Score -= 4000;
		}

		return Score;
	}
}

void UProjectDefaultTattooSkinnedDecalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	AppliedPawn = nullptr;
	AppliedSampler = nullptr;
	AppliedTargetMesh = nullptr;
	AutomaticTattooDecalIndices.Reset();
	ManualTattooDecalIndices.Reset();
	AutomaticTattooSubUVByRow.Reset();
	AutomaticTattooPlacementSignatures.Reset();
	ManualTattooPlacementSignatures.Reset();
	AutomaticTattooEffectiveOffsets.Reset();
	AutomaticTattooCompositeSourceCountByRow.Reset();
	AutomaticTattooCompositeGroupKeyByRow.Reset();
	RuntimeDebugPlacementOverrides.Reset();
	RuntimeDebugForcedActiveRows.Reset();
	ManualTattooRowNames.Reset();
	ManualTattooRows.Reset();
	ManualTattooParametersByRow.Reset();
	ManualTattooTexturesByRow.Reset();
	ManualTattooStateSignature.Reset();
	RuntimeAutomaticTattooAtlasTexture = nullptr;
	RuntimeAutomaticTattooAtlasRenderTarget = nullptr;
	RuntimeAtlasSourceTextures.Reset();
	AutomaticTattooAtlasSubImagesX = 1;
	AutomaticTattooAtlasSubImagesY = 1;
	CachedOverlaySampler = nullptr;
	CachedOverlayMaterialSignature.Reset();
	TattooShopPreviewDecalIndex = ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewReservedDecalIndex;
	RetryCooldownSeconds = 0.0f;
	bTattooShopPreviewForAutomation = false;
	bTattooShopPreviewApplied = false;
}

void UProjectDefaultTattooSkinnedDecalSubsystem::Deinitialize()
{
	ClearProjectTattooLayers(AppliedPawn.Get());
	RuntimeAutomaticTattooAtlasTexture = nullptr;
	RuntimeAutomaticTattooAtlasRenderTarget = nullptr;
	RuntimeDebugPlacementOverrides.Reset();
	RuntimeDebugForcedActiveRows.Reset();
	Super::Deinitialize();
}

void UProjectDefaultTattooSkinnedDecalSubsystem::Tick(const float DeltaTime)
{
	RetryCooldownSeconds = FMath::Max(0.0f, RetryCooldownSeconds - DeltaTime);
	if (RetryCooldownSeconds > 0.0f)
	{
		return;
	}

	APawn* Pawn = ResolveLocalPlayerPawn();
	if (!IsValid(Pawn))
	{
		AppliedPawn = nullptr;
		AppliedSampler = nullptr;
		AppliedTargetMesh = nullptr;
		AutomaticTattooDecalIndices.Reset();
		ManualTattooDecalIndices.Reset();
		AutomaticTattooSubUVByRow.Reset();
		AutomaticTattooPlacementSignatures.Reset();
		ManualTattooPlacementSignatures.Reset();
		AutomaticTattooEffectiveOffsets.Reset();
		AutomaticTattooCompositeSourceCountByRow.Reset();
		AutomaticTattooCompositeGroupKeyByRow.Reset();
		CachedOverlaySampler = nullptr;
		CachedOverlayMaterialSignature.Reset();
		TattooShopPreviewDecalIndex = ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewReservedDecalIndex;
		bTattooShopPreviewApplied = false;
		RetryCooldownSeconds = ProjectDefaultTattooSkinnedDecalPrivate::RetryDelaySeconds;
		return;
	}

	if (AppliedPawn.IsValid() && AppliedPawn.Get() != Pawn)
	{
		ClearProjectTattooLayers(AppliedPawn.Get());
	}

	if (!IsTattooShopOpen() && bTattooShopPreviewForAutomation)
	{
		bTattooShopPreviewForAutomation = false;
		ClearTattooShopPreviewLayer(Pawn);
	}

	if (!IsAutomaticTattooUnlockedForAutomation())
	{
		if (!AutomaticTattooDecalIndices.IsEmpty())
		{
			ClearAutomaticTattoo(Pawn);
		}
	}
	else if (!EnsureAutomaticTattoo(Pawn))
	{
		RetryCooldownSeconds = ProjectDefaultTattooSkinnedDecalPrivate::RetryDelaySeconds;
		return;
	}
	else
	{
		RetryCooldownSeconds = ProjectDefaultTattooSkinnedDecalPrivate::RuntimePollDelaySeconds;
	}

	if (!EnsureManualTattoos(Pawn) && !ManualTattooRowNames.IsEmpty())
	{
		RetryCooldownSeconds = ProjectDefaultTattooSkinnedDecalPrivate::RetryDelaySeconds;
	}

	if (bTattooShopPreviewForAutomation && !bTattooShopPreviewApplied)
	{
		if (!TryApplyTattooShopPreview(Pawn))
		{
			RetryCooldownSeconds = ProjectDefaultTattooSkinnedDecalPrivate::RetryDelaySeconds;
		}
	}
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ApplyTattooShopPreviewForAutomation(APawn* Pawn)
{
	APawn* TargetPawn = IsValid(Pawn) ? Pawn : ResolveLocalPlayerPawn();
	if (!IsValid(TargetPawn))
	{
		return false;
	}

	bTattooShopPreviewForAutomation = true;
	RetryCooldownSeconds = 0.0f;
	const bool bAutomaticReady =
		!IsAutomaticTattooUnlockedForAutomation()
		|| EnsureAutomaticTattoo(TargetPawn);
	const bool bPreviewReady =
		(bTattooShopPreviewApplied && AppliedPawn.Get() == TargetPawn)
		? RestoreSkinnedDecalOverlayIfNeeded()
		: TryApplyTattooShopPreview(TargetPawn);
	return bAutomaticReady && bPreviewReady;
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ClearTattooShopPreviewForAutomation(APawn* Pawn)
{
	bTattooShopPreviewForAutomation = false;
	ClearTattooShopPreviewLayer(IsValid(Pawn) ? Pawn : AppliedPawn.Get());
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::IsTattooShopPreviewForAutomationEnabled() const
{
	return bTattooShopPreviewForAutomation;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::HasActiveAutomaticTattoo(APawn* Pawn) const
{
	if (AutomaticTattooDecalIndices.IsEmpty() || !AppliedSampler.IsValid())
	{
		return false;
	}

	if (IsValid(Pawn) && AppliedPawn.Get() != Pawn)
	{
		return false;
	}

	return AppliedPawn.IsValid();
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::HasActiveManualTattoo(APawn* Pawn) const
{
	return !ManualTattooDecalIndices.IsEmpty()
		&& AppliedSampler.IsValid()
		&& (!IsValid(Pawn) || AppliedPawn.Get() == Pawn);
}

void UProjectDefaultTattooSkinnedDecalSubsystem::SynchronizeManualTattoos(
	APawn* Pawn,
	const TArray<FProjectTattooRecord>& Records,
	const TMap<FGuid, UTexture2D*>& TextureByTattooId)
{
	const ProjectDefaultTattooSkinnedDecalPrivate::FRuntimeTattooLimits Limits =
		ProjectDefaultTattooSkinnedDecalPrivate::ResolveRuntimeTattooLimits();
	TArray<FProjectTattooRecord> SortedRecords = Records;
	SortedRecords.Sort([](const FProjectTattooRecord& Left, const FProjectTattooRecord& Right)
	{
		if (Left.Parameters.LayerOrder != Right.Parameters.LayerOrder)
		{
			return Left.Parameters.LayerOrder < Right.Parameters.LayerOrder;
		}
		return Left.TattooId.ToString(EGuidFormats::Digits) < Right.TattooId.ToString(EGuidFormats::Digits);
	});

	FString Signature;
	for (const FProjectTattooRecord& Record : SortedRecords)
	{
		const UTexture2D* const* Texture = TextureByTattooId.Find(Record.TattooId);
		const UTexture2D* TextureObject = Texture ? *Texture : nullptr;
		Signature += FString::Printf(
			TEXT("%s|TexturePath=%s|Texture=%p|Resource=%p|Enabled=%d|Layer=%d|Preset=%d|Anchor=%s|OffsetX=%.6f|OffsetY=%.6f|Size=%.6f|RotationDegrees=%.6f|ProjectionDistance=%.6f|UseTint=%d|Opacity=%.6f|Color=%.6f,%.6f,%.6f,%.6f|RuntimeMissing=%d|Asset=%s|Runtime=%s;"),
			*Record.TattooId.ToString(EGuidFormats::Digits),
			*GetPathNameSafe(TextureObject),
			static_cast<const void*>(TextureObject),
			static_cast<const void*>(TextureObject ? TextureObject->GetResource() : nullptr),
			Record.Parameters.bEnabled ? 1 : 0,
			Record.Parameters.LayerOrder,
			static_cast<int32>(Record.Parameters.PlacementPreset),
			*Record.Parameters.AnchorBone.ToString(),
			Record.Parameters.OffsetX,
			Record.Parameters.OffsetY,
			Record.Parameters.Size,
			Record.Parameters.RotationDegrees,
			Record.Parameters.ProjectionDistance,
			Record.Parameters.bUseTint ? 1 : 0,
			FMath::Clamp(Record.Parameters.Opacity, 0.0f, 1.0f),
			Record.Parameters.Color.R,
			Record.Parameters.Color.G,
			Record.Parameters.Color.B,
			Record.Parameters.Color.A,
			Record.Parameters.bRuntimeTextureMissing ? 1 : 0,
			*Record.Parameters.TextureAssetPath.ToString(),
			*Record.Parameters.RuntimeTextureId);
	}
	USkeletalMeshComponent* CurrentTargetMesh = ResolveTargetMesh(Pawn);
	if (Signature == ManualTattooStateSignature
		&& AppliedPawn.Get() == Pawn
		&& AppliedTargetMesh.Get() == CurrentTargetMesh)
	{
		RestoreSkinnedDecalOverlayIfNeeded();
		return;
	}

	ClearManualTattoos(IsValid(Pawn) ? Pawn : AppliedPawn.Get());
	CachedOverlayMaterialSignature.Reset();
	ManualTattooRowNames.Reset();
	ManualTattooRows.Reset();
	ManualTattooParametersByRow.Reset();
	ManualTattooTexturesByRow.Reset();

	int32 EligibleManualTattooCount = 0;
	int32 AcceptedManualTattooCount = 0;
	for (const FProjectTattooRecord& Record : SortedRecords)
	{
		UTexture2D* const* Texture = TextureByTattooId.Find(Record.TattooId);
		if (!Record.TattooId.IsValid()
			|| !Record.Parameters.bEnabled
			|| Record.Parameters.bRuntimeTextureMissing
			|| !Texture
			|| !IsValid(*Texture))
		{
			continue;
		}
		++EligibleManualTattooCount;
		if (AcceptedManualTattooCount >= Limits.MaximumManualTattoos)
		{
			continue;
		}

		const FName RowName(*FString::Printf(TEXT("__Manual_%s"), *Record.TattooId.ToString(EGuidFormats::Digits)));
		FProjectAutomaticTattooTableRow& Row = ManualTattooRows.AddDefaulted_GetRef();
		Row.TattooTexture = *Texture;
		Row.PlacementPreset = Record.Parameters.PlacementPreset;
		Row.AnchorBone = Record.Parameters.AnchorBone;
		Row.OffsetX = Record.Parameters.OffsetX;
		Row.OffsetY = Record.Parameters.OffsetY;
		Row.Size = Record.Parameters.Size;
		Row.RotationDegrees = Record.Parameters.RotationDegrees;
		Row.ProjectionDistance = Record.Parameters.ProjectionDistance;
		Row.bEnabled = true;
		ManualTattooRowNames.Add(RowName);
		ManualTattooParametersByRow.Add(RowName, Record.Parameters);
		ManualTattooTexturesByRow.Add(RowName, *Texture);
		++AcceptedManualTattooCount;
	}

	if (EligibleManualTattooCount > Limits.MaximumManualTattoos)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Manual tattoo input was limited to %d valid layers (received %d eligible records)."),
			Limits.MaximumManualTattoos,
			EligibleManualTattooCount);
	}

	ManualTattooStateSignature = Signature;
	if (!ManualTattooRowNames.IsEmpty())
	{
		EnsureManualTattoos(Pawn);
	}
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::IsAutomaticTattooUnlockedForAutomation() const
{
	TArray<FName> RowNames;
	TArray<const FProjectAutomaticTattooTableRow*> TattooRows;
	ResolveAutomaticTattooRows(RowNames, TattooRows, true);
	return !TattooRows.IsEmpty();
}

int32 UProjectDefaultTattooSkinnedDecalSubsystem::GetAutomaticTattooUnlockEncounterCountForAutomation() const
{
	const UWorld* World = GetWorld();
	const UProjectIntimacySubsystem* IntimacySubsystem = World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr;
	return IntimacySubsystem ? IntimacySubsystem->GetTotalIntimacyEncounterCount() : 0;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::RefreshAutomaticTattooForAutomation(APawn* Pawn)
{
	APawn* TargetPawn = IsValid(Pawn) ? Pawn : ResolveLocalPlayerPawn();
	if (!IsValid(TargetPawn) || !IsAutomaticTattooUnlockedForAutomation())
	{
		return false;
	}

	RetryCooldownSeconds = 0.0f;
	return EnsureAutomaticTattoo(TargetPawn);
}

void UProjectDefaultTattooSkinnedDecalSubsystem::GetAutomaticTattooRuntimeDebugSnapshots(
	TArray<FProjectAutomaticTattooRuntimeDebugSnapshot>& OutSnapshots) const
{
	OutSnapshots.Reset();

	TArray<FName> RowNames;
	TArray<const FProjectAutomaticTattooTableRow*> Rows;
	ResolveAutomaticTattooRows(RowNames, Rows, false);
	OutSnapshots.Reserve(RowNames.Num());

	for (int32 RowIndex = 0; RowIndex < RowNames.Num(); ++RowIndex)
	{
		const FName RowName = RowNames[RowIndex];
		const FProjectAutomaticTattooTableRow* Row = Rows.IsValidIndex(RowIndex) ? Rows[RowIndex] : nullptr;
		if (!Row)
		{
			continue;
		}

		FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot = OutSnapshots.AddDefaulted_GetRef();
		Snapshot.RowName = RowName;
		Snapshot.DataTableRow = *Row;
		Snapshot.EffectiveRow = BuildEffectiveTattooRow(RowName, Row);
		Snapshot.bActive = IsAutomaticTattooRowActive(RowName, Row);
		Snapshot.bForcedActiveForDebug = RuntimeDebugForcedActiveRows.Contains(RowName);
		Snapshot.bHasRuntimeOverride = RuntimeDebugPlacementOverrides.Contains(RowName);
		Snapshot.DecalIndex = AutomaticTattooDecalIndices.FindRef(RowName);
		Snapshot.SubUV = AutomaticTattooSubUVByRow.FindRef(RowName);
		Snapshot.TattooTexturePath = Snapshot.EffectiveRow.TattooTexture.IsNull()
			? FString()
			: Snapshot.EffectiveRow.TattooTexture.ToSoftObjectPath().ToString();
	}
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::GetAutomaticTattooRuntimeDebugSnapshot(
	const FName RowName,
	FProjectAutomaticTattooRuntimeDebugSnapshot& OutSnapshot) const
{
	TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> Snapshots;
	GetAutomaticTattooRuntimeDebugSnapshots(Snapshots);
	for (const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot : Snapshots)
	{
		if (Snapshot.RowName == RowName)
		{
			OutSnapshot = Snapshot;
			return true;
		}
	}

	return false;
}

FProjectAutomaticTattooRuntimeDebugState UProjectDefaultTattooSkinnedDecalSubsystem::CaptureAutomaticTattooRuntimeDebugState(
	const FName RowName) const
{
	FProjectAutomaticTattooRuntimeDebugState State;
#if !UE_BUILD_SHIPPING
	if (const FProjectAutomaticTattooRuntimePlacementOverride* RuntimeOverride = RuntimeDebugPlacementOverrides.Find(RowName))
	{
		State.bHasRuntimeOverride = true;
		State.RuntimeOverride = *RuntimeOverride;
	}
	State.bForcedActiveForDebug = RuntimeDebugForcedActiveRows.Contains(RowName);
#endif
	return State;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::RestoreAutomaticTattooRuntimeDebugState(
	APawn* Pawn,
	const FName RowName,
	const FProjectAutomaticTattooRuntimeDebugState& State)
{
#if UE_BUILD_SHIPPING
	(void)Pawn;
	(void)RowName;
	(void)State;
	return false;
#else
	if (!FindAutomaticTattooRow(RowName))
	{
		return false;
	}

	if (State.bHasRuntimeOverride)
	{
		RuntimeDebugPlacementOverrides.FindOrAdd(RowName) = State.RuntimeOverride;
	}
	else
	{
		RuntimeDebugPlacementOverrides.Remove(RowName);
	}

	if (State.bForcedActiveForDebug)
	{
		RuntimeDebugForcedActiveRows.Add(RowName);
	}
	else
	{
		RuntimeDebugForcedActiveRows.Remove(RowName);
	}

	AutomaticTattooPlacementSignatures.Remove(RowName);
	CachedOverlayMaterialSignature.Reset();
	RetryCooldownSeconds = 0.0f;
	return RefreshAutomaticTattooAfterRuntimeDebugChange(Pawn);
#endif
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::SetAutomaticTattooRuntimeDebugPlacement(
	APawn* Pawn,
	const FName RowName,
	const FProjectAutomaticTattooTableRow& TattooRow)
{
#if UE_BUILD_SHIPPING
	(void)Pawn;
	(void)RowName;
	(void)TattooRow;
	return false;
#else
	if (!FindAutomaticTattooRow(RowName))
	{
		return false;
	}

	FProjectAutomaticTattooRuntimePlacementOverride& RuntimeOverride = RuntimeDebugPlacementOverrides.FindOrAdd(RowName);
	RuntimeOverride.PlacementPreset = TattooRow.PlacementPreset;
	RuntimeOverride.AnchorBone = TattooRow.AnchorBone;
	RuntimeOverride.OffsetX = TattooRow.OffsetX;
	RuntimeOverride.OffsetY = TattooRow.OffsetY;
	RuntimeOverride.Size = FMath::Max(1.0f, TattooRow.Size);
	RuntimeOverride.RotationDegrees = TattooRow.RotationDegrees;
	RuntimeOverride.ProjectionDistance = FMath::Max(0.0f, TattooRow.ProjectionDistance);
	RuntimeOverride.bEnabled = TattooRow.bEnabled;

	AutomaticTattooPlacementSignatures.Remove(RowName);
	CachedOverlayMaterialSignature.Reset();
	RetryCooldownSeconds = 0.0f;
	return RefreshAutomaticTattooAfterRuntimeDebugChange(Pawn);
#endif
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::AdjustAutomaticTattooRuntimeDebugPlacement(
	APawn* Pawn,
	const FName RowName,
	const float DeltaOffsetX,
	const float DeltaOffsetY,
	const float DeltaSize,
	const float DeltaRotationDegrees,
	const float DeltaProjectionDistance)
{
#if UE_BUILD_SHIPPING
	(void)Pawn;
	(void)RowName;
	(void)DeltaOffsetX;
	(void)DeltaOffsetY;
	(void)DeltaSize;
	(void)DeltaRotationDegrees;
	(void)DeltaProjectionDistance;
	return false;
#else
	const FProjectAutomaticTattooTableRow* SourceRow = FindAutomaticTattooRow(RowName);
	if (!SourceRow)
	{
		return false;
	}

	const FProjectAutomaticTattooTableRow EffectiveRow = BuildEffectiveTattooRow(RowName, SourceRow);
	FProjectAutomaticTattooRuntimePlacementOverride& RuntimeOverride = RuntimeDebugPlacementOverrides.FindOrAdd(RowName);
	RuntimeOverride.PlacementPreset = EffectiveRow.PlacementPreset;
	RuntimeOverride.AnchorBone = EffectiveRow.AnchorBone;
	RuntimeOverride.OffsetX = EffectiveRow.OffsetX + DeltaOffsetX;
	RuntimeOverride.OffsetY = EffectiveRow.OffsetY + DeltaOffsetY;
	RuntimeOverride.Size = FMath::Max(1.0f, EffectiveRow.Size + DeltaSize);
	RuntimeOverride.RotationDegrees = EffectiveRow.RotationDegrees + DeltaRotationDegrees;
	RuntimeOverride.ProjectionDistance = FMath::Max(0.0f, EffectiveRow.ProjectionDistance + DeltaProjectionDistance);
	RuntimeOverride.bEnabled = EffectiveRow.bEnabled;

	AutomaticTattooPlacementSignatures.Remove(RowName);
	CachedOverlayMaterialSignature.Reset();
	RetryCooldownSeconds = 0.0f;
	return RefreshAutomaticTattooAfterRuntimeDebugChange(Pawn);
#endif
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ResetAutomaticTattooRuntimeDebugPlacement(APawn* Pawn, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)Pawn;
	(void)RowName;
	return false;
#else
	if (!RuntimeDebugPlacementOverrides.Remove(RowName))
	{
		return false;
	}

	AutomaticTattooPlacementSignatures.Remove(RowName);
	CachedOverlayMaterialSignature.Reset();
	RetryCooldownSeconds = 0.0f;
	return RefreshAutomaticTattooAfterRuntimeDebugChange(Pawn);
#endif
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::SetAutomaticTattooRuntimeDebugForcedActive(
	APawn* Pawn,
	const FName RowName,
	const bool bForcedActive)
{
#if UE_BUILD_SHIPPING
	(void)Pawn;
	(void)RowName;
	(void)bForcedActive;
	return false;
#else
	if (!FindAutomaticTattooRow(RowName))
	{
		return false;
	}

	if (bForcedActive)
	{
		RuntimeDebugForcedActiveRows.Add(RowName);
	}
	else
	{
		RuntimeDebugForcedActiveRows.Remove(RowName);
	}

	AutomaticTattooPlacementSignatures.Remove(RowName);
	CachedOverlayMaterialSignature.Reset();
	RetryCooldownSeconds = 0.0f;
	return RefreshAutomaticTattooAfterRuntimeDebugChange(Pawn);
#endif
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ToggleAutomaticTattooRuntimeDebugForcedActive(APawn* Pawn, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)Pawn;
	(void)RowName;
	return false;
#else
	if (!FindAutomaticTattooRow(RowName))
	{
		return false;
	}

	if (RuntimeDebugForcedActiveRows.Contains(RowName))
	{
		RuntimeDebugForcedActiveRows.Remove(RowName);
	}
	else
	{
		RuntimeDebugForcedActiveRows.Add(RowName);
	}

	AutomaticTattooPlacementSignatures.Remove(RowName);
	CachedOverlayMaterialSignature.Reset();
	RetryCooldownSeconds = 0.0f;
	return RefreshAutomaticTattooAfterRuntimeDebugChange(Pawn);
#endif
}

FString UProjectDefaultTattooSkinnedDecalSubsystem::BuildAutomaticTattooRuntimeDebugCopyText(const FName RowName) const
{
	const FProjectAutomaticTattooTableRow* SourceRow = FindAutomaticTattooRow(RowName);
	if (!SourceRow)
	{
		return FString();
	}

	const FProjectAutomaticTattooTableRow EffectiveRow = BuildEffectiveTattooRow(RowName, SourceRow);
	return FString::Printf(
		TEXT("[AT] DT_AutomaticTattoos row '%s': PlacementPreset=%s, AnchorBone=%s, OffsetX=%.3f, OffsetY=%.3f, Size=%.3f, RotationDegrees=%.3f, ProjectionDistance=%.3f, Enabled=%s"),
		*RowName.ToString(),
		ProjectDefaultTattooSkinnedDecalPrivate::PlacementPresetName(EffectiveRow.PlacementPreset),
		*EffectiveRow.AnchorBone.ToString(),
		EffectiveRow.OffsetX,
		EffectiveRow.OffsetY,
		EffectiveRow.Size,
		EffectiveRow.RotationDegrees,
		EffectiveRow.ProjectionDistance,
		EffectiveRow.bEnabled ? TEXT("True") : TEXT("False"));
}

FString UProjectDefaultTattooSkinnedDecalSubsystem::GetTattooLayerReportForAutomation(APawn* Pawn) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	USkinnedDecalSampler* Sampler = AppliedSampler.Get();
	USkeletalMeshComponent* Mesh = Sampler ? Sampler->Mesh : nullptr;
	UMaterialInterface* MeshOverlay = Mesh ? Mesh->GetOverlayMaterial() : nullptr;
	UMaterialInstanceDynamic* SamplerOverlay = Sampler ? Sampler->OverlayBlendMaterialDynamic : nullptr;
	const int32 IntimacyEncounterCount = GetAutomaticTattooUnlockEncounterCountForAutomation();
	TArray<FName> ActiveRowNames;
	TArray<const FProjectAutomaticTattooTableRow*> SourceActiveTattooRows;
	ResolveAutomaticTattooRows(ActiveRowNames, SourceActiveTattooRows, true);
	TArray<FProjectAutomaticTattooTableRow> EffectiveActiveTattooRowValues;
	TArray<const FProjectAutomaticTattooTableRow*> ActiveTattooRows;
	BuildEffectiveTattooRows(ActiveRowNames, SourceActiveTattooRows, EffectiveActiveTattooRowValues, ActiveTattooRows);
	const FProjectAutomaticTattooTableRow* FirstTattooRow = ActiveTattooRows.IsEmpty() ? nullptr : ActiveTattooRows[0];
	const FName FirstTattooRowName = ActiveRowNames.IsEmpty() ? NAME_None : ActiveRowNames[0];
	const UTexture2D* TattooTexture = ResolveTattooTexture(FirstTattooRow);

	Root->SetStringField(TEXT("automatic_tattoos_system_tag"), TEXT("AT"));
	Root->SetStringField(TEXT("automatic_tattoos_system_name"), TEXT("Automatic Tattoos"));
	Root->SetStringField(TEXT("player_tattoos_system_tag"), TEXT("PT"));
	Root->SetStringField(TEXT("player_tattoos_system_name"), TEXT("Player Tattoos"));
	Root->SetStringField(TEXT("player_tattoos_legacy_name"), TEXT("TattooShop"));
	Root->SetBoolField(TEXT("automatic_layer_present"), HasActiveAutomaticTattoo(Pawn));
	Root->SetBoolField(TEXT("automatic_unlocked"), IsAutomaticTattooUnlockedForAutomation());
	Root->SetStringField(TEXT("automatic_reward_id"), FirstTattooRow ? FirstTattooRow->RewardId.ToString() : FString());
	Root->SetStringField(TEXT("automatic_unlock_rule"), TEXT("Rows are runtime-active by UnlockRule; AlwaysOnSpawn rows appear on spawn, RuntimeReward rows appear after this run unlocks their RewardId"));
	Root->SetStringField(TEXT("automatic_data_table"), ProjectDefaultTattooSkinnedDecalPrivate::AutomaticTattooTablePath);
	Root->SetStringField(TEXT("automatic_row_name"), FirstTattooRowName.ToString());
	Root->SetBoolField(TEXT("automatic_row_found"), FirstTattooRow != nullptr);
	Root->SetBoolField(TEXT("automatic_row_enabled"), FirstTattooRow && FirstTattooRow->bEnabled);
	Root->SetStringField(TEXT("automatic_unlock_description"), FirstTattooRow ? FirstTattooRow->UnlockDescription : FString());
	Root->SetStringField(TEXT("automatic_tattoo_texture"), GetPathNameSafe(TattooTexture));
	Root->SetStringField(TEXT("automatic_placement_preset"), FirstTattooRow ? ProjectDefaultTattooSkinnedDecalPrivate::PlacementPresetName(FirstTattooRow->PlacementPreset) : TEXT(""));
	Root->SetNumberField(TEXT("automatic_offset_x"), FirstTattooRow ? FirstTattooRow->OffsetX : 0.0f);
	Root->SetNumberField(TEXT("automatic_offset_y"), FirstTattooRow ? FirstTattooRow->OffsetY : 0.0f);
	Root->SetNumberField(TEXT("automatic_size"), FirstTattooRow ? FirstTattooRow->Size : 0.0f);
	Root->SetNumberField(TEXT("automatic_rotation_degrees"), FirstTattooRow ? FirstTattooRow->RotationDegrees : 0.0f);
	Root->SetNumberField(TEXT("automatic_projection_distance"), FirstTattooRow ? FirstTattooRow->ProjectionDistance : 0.0f);
	Root->SetNumberField(TEXT("intimacy_total_encounters"), IntimacyEncounterCount);
	Root->SetStringField(TEXT("automatic_layer_system_tag"), TEXT("AT"));
	Root->SetStringField(TEXT("tattooshop_preview_system_tag"), TEXT("PT"));
	Root->SetBoolField(TEXT("tattooshop_preview_layer_present"), bTattooShopPreviewApplied && AppliedPawn.IsValid() && (!IsValid(Pawn) || AppliedPawn.Get() == Pawn));
	Root->SetBoolField(TEXT("tattooshop_preview_requested"), bTattooShopPreviewForAutomation);
	Root->SetNumberField(TEXT("automatic_layer_index"), AutomaticTattooDecalIndices.FindRef(FirstTattooRowName));
	Root->SetNumberField(TEXT("tattooshop_preview_layer_index"), TattooShopPreviewDecalIndex);
	Root->SetNumberField(TEXT("automatic_layer_count"), AutomaticTattooDecalIndices.Num());
	Root->SetNumberField(TEXT("automatic_active_row_count"), ActiveRowNames.Num());
	Root->SetNumberField(TEXT("automatic_atlas_sub_images_x"), AutomaticTattooAtlasSubImagesX);
	Root->SetNumberField(TEXT("automatic_atlas_sub_images_y"), AutomaticTattooAtlasSubImagesY);
	Root->SetStringField(TEXT("applied_pawn"), GetPathNameSafe(AppliedPawn.Get()));
	Root->SetStringField(TEXT("sampler"), GetPathNameSafe(Sampler));
	Root->SetStringField(TEXT("sampler_mesh"), GetPathNameSafe(Mesh));
	Root->SetStringField(TEXT("sampler_overlay"), GetPathNameSafe(SamplerOverlay));
	Root->SetStringField(TEXT("mesh_overlay"), GetPathNameSafe(MeshOverlay));
	Root->SetBoolField(TEXT("mesh_overlay_is_skinned_decal"), MeshOverlay && SamplerOverlay && MeshOverlay == SamplerOverlay);

	TArray<TSharedPtr<FJsonValue>> ActiveRowsJson;
	for (int32 RowIndex = 0; RowIndex < ActiveRowNames.Num(); ++RowIndex)
	{
		const FName RowName = ActiveRowNames[RowIndex];
		const FProjectAutomaticTattooTableRow* Row = ActiveTattooRows.IsValidIndex(RowIndex) ? ActiveTattooRows[RowIndex] : nullptr;
		TSharedRef<FJsonObject> RowObject = MakeShared<FJsonObject>();
		RowObject->SetStringField(TEXT("system_tag"), TEXT("AT"));
		RowObject->SetStringField(TEXT("system_name"), TEXT("Automatic Tattoos"));
		RowObject->SetStringField(TEXT("row_name"), RowName.ToString());
		RowObject->SetStringField(TEXT("reward_id"), Row ? Row->RewardId.ToString() : FString());
		RowObject->SetStringField(TEXT("unlock_description"), Row ? Row->UnlockDescription : FString());
		RowObject->SetStringField(TEXT("placement_preset"), Row ? ProjectDefaultTattooSkinnedDecalPrivate::PlacementPresetName(Row->PlacementPreset) : TEXT(""));
		RowObject->SetNumberField(TEXT("offset_x"), Row ? Row->OffsetX : 0.0f);
		RowObject->SetNumberField(TEXT("offset_y"), Row ? Row->OffsetY : 0.0f);
		RowObject->SetNumberField(TEXT("size"), Row ? Row->Size : 0.0f);
		RowObject->SetNumberField(TEXT("rotation_degrees"), Row ? Row->RotationDegrees : 0.0f);
		RowObject->SetNumberField(TEXT("projection_distance"), Row ? Row->ProjectionDistance : 0.0f);
		RowObject->SetBoolField(TEXT("runtime_debug_forced_active"), RuntimeDebugForcedActiveRows.Contains(RowName));
		RowObject->SetBoolField(TEXT("runtime_debug_override_active"), RuntimeDebugPlacementOverrides.Contains(RowName));
		const FVector2D EffectiveOffset = AutomaticTattooEffectiveOffsets.FindRef(RowName);
		RowObject->SetNumberField(TEXT("effective_offset_x"), EffectiveOffset.X);
		RowObject->SetNumberField(TEXT("effective_offset_y"), EffectiveOffset.Y);
		RowObject->SetNumberField(TEXT("decal_index"), AutomaticTattooDecalIndices.FindRef(RowName));
		RowObject->SetNumberField(TEXT("sub_uv"), AutomaticTattooSubUVByRow.FindRef(RowName));
		const int32 CompositeSourceCount = AutomaticTattooCompositeSourceCountByRow.FindRef(RowName);
		RowObject->SetNumberField(TEXT("composite_source_count"), CompositeSourceCount);
		RowObject->SetBoolField(TEXT("uses_composite_sub_uv"), CompositeSourceCount > 1);
		RowObject->SetStringField(TEXT("composite_group_key"), AutomaticTattooCompositeGroupKeyByRow.FindRef(RowName));
		RowObject->SetStringField(TEXT("placement_signature"), AutomaticTattooPlacementSignatures.FindRef(RowName));
		RowObject->SetStringField(TEXT("tattoo_texture"), GetPathNameSafe(ResolveTattooTexture(Row)));
		ActiveRowsJson.Add(MakeShared<FJsonValueObject>(RowObject));
	}
	Root->SetArrayField(TEXT("automatic_active_rows"), ActiveRowsJson);

	FString ReportString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportString);
	FJsonSerializer::Serialize(Root, Writer);
	return ReportString;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::RestoreSkinnedDecalOverlayIfNeeded() const
{
	USkinnedDecalSampler* Sampler = AppliedSampler.Get();
	if (!IsValid(Sampler) || !IsValid(Sampler->Mesh) || !IsValid(Sampler->OverlayBlendMaterialDynamic))
	{
		return false;
	}

	if (Sampler->Mesh->GetOverlayMaterial() == Sampler->OverlayBlendMaterialDynamic)
	{
		return true;
	}

	Sampler->Mesh->SetOverlayMaterial(Sampler->OverlayBlendMaterialDynamic);
	return true;
}

TStatId UProjectDefaultTattooSkinnedDecalSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectDefaultTattooSkinnedDecalSubsystem, STATGROUP_Tickables);
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

APawn* UProjectDefaultTattooSkinnedDecalSubsystem::ResolveLocalPlayerPawn() const
{
	const UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::IsTattooShopOpen() const
{
	const UWorld* World = GetWorld();
	const UProjectTattooShopInputSubsystem* TattooShopInputSubsystem = World ? World->GetSubsystem<UProjectTattooShopInputSubsystem>() : nullptr;
	return TattooShopInputSubsystem && TattooShopInputSubsystem->IsTattooShopOpen();
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::EnsureAutomaticTattoo(APawn* Pawn)
{
	const ProjectDefaultTattooSkinnedDecalPrivate::FRuntimeTattooLimits Limits =
		ProjectDefaultTattooSkinnedDecalPrivate::ResolveRuntimeTattooLimits();
	if (!IsValid(Pawn) || !IsAutomaticTattooUnlockedForAutomation())
	{
		return false;
	}

	if (AppliedPawn.IsValid() && AppliedPawn.Get() != Pawn)
	{
		ClearProjectTattooLayers(AppliedPawn.Get());
	}

	if (!ManualTattooRowNames.IsEmpty())
	{
		// The combined automatic+manual atlas is configured and the two families
		// are spawned together by EnsureManualTattoos. Avoid rebuilding it here.
		return EnsureManualTattoos(Pawn);
	}

	TArray<FName> ActiveRowNames;
	TArray<const FProjectAutomaticTattooTableRow*> SourceActiveRows;
	ResolveAutomaticTattooRows(ActiveRowNames, SourceActiveRows, true);
	if (SourceActiveRows.IsEmpty())
	{
		ClearAutomaticTattoo(Pawn);
		return false;
	}

	TArray<FProjectAutomaticTattooTableRow> EffectiveActiveRowValues;
	TArray<const FProjectAutomaticTattooTableRow*> ActiveRows;
	BuildEffectiveTattooRows(ActiveRowNames, SourceActiveRows, EffectiveActiveRowValues, ActiveRows);
	if (ActiveRowNames.Num() > Limits.MaximumTotalLayers)
	{
		ActiveRowNames.SetNum(Limits.MaximumTotalLayers);
		ActiveRows.SetNum(Limits.MaximumTotalLayers);
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Active automatic tattoo layers were limited to %d."),
			Limits.MaximumTotalLayers);
	}

	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(Pawn);
	if (!IsValid(TargetMesh) || !IsValid(TargetMesh->GetSkinnedAsset()))
	{
		return false;
	}

	TArray<FName> AtlasRowNames = ActiveRowNames;
	TArray<const FProjectAutomaticTattooTableRow*> AtlasRows = ActiveRows;
	const int32 MaximumManualRowsForAtlas = FMath::Min(
		Limits.MaximumManualTattoos,
		FMath::Max(0, Limits.MaximumTotalLayers - AtlasRowNames.Num()));
	for (int32 ManualIndex = 0;
		ManualIndex < ManualTattooRowNames.Num()
			&& ManualIndex < ManualTattooRows.Num()
			&& ManualIndex < MaximumManualRowsForAtlas;
		++ManualIndex)
	{
		AtlasRowNames.Add(ManualTattooRowNames[ManualIndex]);
		AtlasRows.Add(&ManualTattooRows[ManualIndex]);
	}
	if (bTattooShopPreviewForAutomation || bTattooShopPreviewApplied)
	{
		if (AtlasRowNames.Num() < Limits.MaximumTotalLayers)
		{
			AppendTattooShopPreviewAtlasRow(AtlasRowNames, AtlasRows);
		}
	}

	USkinnedDecalSampler* Sampler = ResolveOrCreateSampler(Pawn);
	if (!ConfigureSampler(Sampler, TargetMesh) || !ConfigureOverlayMaterial(Sampler, AtlasRowNames, AtlasRows))
	{
		return false;
	}
	TSet<FName> ActiveRowSet;
	for (const FName ActiveRowName : ActiveRowNames)
	{
		ActiveRowSet.Add(ActiveRowName);
	}
	for (const TPair<FName, int32>& ExistingLayer : AutomaticTattooDecalIndices)
	{
		if (!ActiveRowSet.Contains(ExistingLayer.Key))
		{
			ClearTattooLayer(Pawn, ExistingLayer.Value);
		}
	}
	for (auto It = AutomaticTattooDecalIndices.CreateIterator(); It; ++It)
	{
		if (!ActiveRowSet.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = AutomaticTattooPlacementSignatures.CreateIterator(); It; ++It)
	{
		if (!ActiveRowSet.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = AutomaticTattooEffectiveOffsets.CreateIterator(); It; ++It)
	{
		if (!ActiveRowSet.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	bool bAllActiveRowsReady = true;
	for (int32 RowIndex = 0; RowIndex < ActiveRows.Num(); ++RowIndex)
	{
		const FName RowName = ActiveRowNames[RowIndex];
		const FProjectAutomaticTattooTableRow* ActiveTattooRow = ActiveRows[RowIndex];
		if (!ActiveTattooRow)
		{
			bAllActiveRowsReady = false;
			continue;
		}

		FName AnchorBone = ActiveTattooRow->AnchorBone;
		if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
		{
			AnchorBone = ProjectDefaultTattooSkinnedDecalPrivate::ResolvePresetAnchorBone(
				TargetMesh,
				ActiveTattooRow->PlacementPreset);
		}
		if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
		{
			AnchorBone = ResolveAnchorBone(TargetMesh);
		}

		const int32 SubUV = AutomaticTattooSubUVByRow.FindRef(RowName);
		const FString PlacementSignature = ProjectDefaultTattooSkinnedDecalPrivate::BuildTattooPlacementSignature(
			RowName,
			ActiveTattooRow,
			AnchorBone,
			SubUV);
		if (AutomaticTattooDecalIndices.Contains(RowName)
			&& AutomaticTattooPlacementSignatures.FindRef(RowName) == PlacementSignature)
		{
			AutomaticTattooEffectiveOffsets.Add(RowName, FVector2D(ActiveTattooRow->OffsetX, ActiveTattooRow->OffsetY));
			continue;
		}

		const int32 ExistingDecalIndex = AutomaticTattooDecalIndices.FindRef(RowName);
		if (ExistingDecalIndex != INDEX_NONE)
		{
			ClearTattooLayer(Pawn, ExistingDecalIndex);
			AutomaticTattooDecalIndices.Remove(RowName);
			AutomaticTattooPlacementSignatures.Remove(RowName);
			AutomaticTattooEffectiveOffsets.Remove(RowName);
		}

		bAllActiveRowsReady &= ApplyAutomaticTattooLayer(Pawn, INDEX_NONE, RowName, ActiveTattooRow, SubUV);
	}

	return bAllActiveRowsReady && !AutomaticTattooDecalIndices.IsEmpty();
}

void UProjectDefaultTattooSkinnedDecalSubsystem::GetManualTattooRuntimeDebugSnapshots(
	TArray<FProjectManualTattooRuntimeDebugSnapshot>& OutSnapshots) const
{
	OutSnapshots.Reset();

	TArray<FColor> AtlasPixels;
	int32 AtlasWidth = 0;
	int32 AtlasHeight = 0;
	if (RuntimeAutomaticTattooAtlasRenderTarget)
	{
		AtlasWidth = RuntimeAutomaticTattooAtlasRenderTarget->SizeX;
		AtlasHeight = RuntimeAutomaticTattooAtlasRenderTarget->SizeY;
		if (FTextureRenderTargetResource* Resource = RuntimeAutomaticTattooAtlasRenderTarget->GameThread_GetRenderTargetResource())
		{
			FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
			ReadFlags.SetLinearToGamma(false);
			Resource->ReadPixels(AtlasPixels, ReadFlags);
		}
	}

	const int32 SafeSubImagesX = FMath::Max(1, AutomaticTattooAtlasSubImagesX);
	const int32 SafeSubImagesY = FMath::Max(1, AutomaticTattooAtlasSubImagesY);
	const int32 CellWidth = AtlasWidth > 0 ? AtlasWidth / SafeSubImagesX : 0;
	const int32 CellHeight = AtlasHeight > 0 ? AtlasHeight / SafeSubImagesY : 0;

	for (const FName RowName : ManualTattooRowNames)
	{
		const FString RowString = RowName.ToString();
		constexpr const TCHAR* ManualPrefix = TEXT("__Manual_");
		if (!RowString.StartsWith(ManualPrefix))
		{
			continue;
		}

		FGuid TattooId;
		if (!FGuid::ParseExact(RowString.RightChop(FCString::Strlen(ManualPrefix)), EGuidFormats::Digits, TattooId))
		{
			continue;
		}

		FProjectManualTattooRuntimeDebugSnapshot& Snapshot = OutSnapshots.AddDefaulted_GetRef();
		Snapshot.TattooId = TattooId;
		Snapshot.RowName = RowName;
		Snapshot.DecalIndex = ManualTattooDecalIndices.Contains(RowName)
			? ManualTattooDecalIndices.FindRef(RowName)
			: INDEX_NONE;
		Snapshot.SubUV = AutomaticTattooSubUVByRow.Contains(RowName)
			? AutomaticTattooSubUVByRow.FindRef(RowName)
			: INDEX_NONE;
		Snapshot.AtlasSubImagesX = SafeSubImagesX;
		Snapshot.AtlasSubImagesY = SafeSubImagesY;
		Snapshot.TattooTexturePath = GetPathNameSafe(ManualTattooTexturesByRow.FindRef(RowName));

		if (const FProjectTattooParameters* Parameters = ManualTattooParametersByRow.Find(RowName))
		{
			const FString State = FString::Printf(
				TEXT("%s|%s|%d|%d|%s|%.6f|%.6f|%.6f|%.6f|%.6f|%d|%.6f|%.6f,%.6f,%.6f,%.6f|%s|%s"),
				*TattooId.ToString(EGuidFormats::Digits),
				*Snapshot.TattooTexturePath,
				Parameters->bEnabled ? 1 : 0,
				Parameters->LayerOrder,
				*Parameters->AnchorBone.ToString(),
				Parameters->OffsetX,
				Parameters->OffsetY,
				Parameters->Size,
				Parameters->RotationDegrees,
				Parameters->ProjectionDistance,
				Parameters->bUseTint ? 1 : 0,
				Parameters->Opacity,
				Parameters->Color.R,
				Parameters->Color.G,
				Parameters->Color.B,
				Parameters->Color.A,
				*Parameters->TextureAssetPath.ToString(),
				*Parameters->RuntimeTextureId);
			Snapshot.StateHash = FMD5::HashAnsiString(*State);
		}

		if (CellWidth <= 0 || CellHeight <= 0 || Snapshot.SubUV < 0 || AtlasPixels.Num() != AtlasWidth * AtlasHeight)
		{
			continue;
		}

		const int32 CellX = Snapshot.SubUV % SafeSubImagesX;
		const int32 CellY = Snapshot.SubUV / SafeSubImagesX;
		if (CellX < 0 || CellX >= SafeSubImagesX || CellY < 0 || CellY >= SafeSubImagesY)
		{
			continue;
		}

		const int32 MinX = CellX * CellWidth;
		const int32 MinY = CellY * CellHeight;
		const int32 MaxX = FMath::Min(MinX + CellWidth, AtlasWidth);
		const int32 MaxY = FMath::Min(MinY + CellHeight, AtlasHeight);
		Snapshot.AtlasCellRect = FIntRect(MinX, MinY, MaxX, MaxY);
		Snapshot.MinimumAlpha = 255;
		Snapshot.MaximumAlpha = 0;
		int32 VisibleMinX = CellWidth;
		int32 VisibleMinY = CellHeight;
		int32 VisibleMaxX = -1;
		int32 VisibleMaxY = -1;
		TArray<FColor> CellPixels;
		CellPixels.Reserve((MaxX - MinX) * (MaxY - MinY));
		for (int32 Y = MinY; Y < MaxY; ++Y)
		{
			for (int32 X = MinX; X < MaxX; ++X)
			{
				const FColor Pixel = AtlasPixels[Y * AtlasWidth + X];
				CellPixels.Add(Pixel);
				Snapshot.MinimumAlpha = FMath::Min(Snapshot.MinimumAlpha, Pixel.A);
				Snapshot.MaximumAlpha = FMath::Max(Snapshot.MaximumAlpha, Pixel.A);
				if (Pixel.A > 0)
				{
					++Snapshot.NonTransparentPixelCount;
					const int32 LocalX = X - MinX;
					const int32 LocalY = Y - MinY;
					VisibleMinX = FMath::Min(VisibleMinX, LocalX);
					VisibleMinY = FMath::Min(VisibleMinY, LocalY);
					VisibleMaxX = FMath::Max(VisibleMaxX, LocalX);
					VisibleMaxY = FMath::Max(VisibleMaxY, LocalY);
				}
			}
		}

		if (Snapshot.NonTransparentPixelCount > 0)
		{
			Snapshot.VisiblePixelBounds = FIntRect(VisibleMinX, VisibleMinY, VisibleMaxX + 1, VisibleMaxY + 1);
		}
		if (!CellPixels.IsEmpty())
		{
			Snapshot.AtlasCellPixelHash = FMD5::HashBytes(
				reinterpret_cast<const uint8*>(CellPixels.GetData()),
				CellPixels.Num() * static_cast<int32>(sizeof(FColor)));
		}
	}
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::GetManualTattooRuntimeDebugSnapshot(
	const FGuid& TattooId,
	FProjectManualTattooRuntimeDebugSnapshot& OutSnapshot) const
{
	TArray<FProjectManualTattooRuntimeDebugSnapshot> Snapshots;
	GetManualTattooRuntimeDebugSnapshots(Snapshots);
	if (const FProjectManualTattooRuntimeDebugSnapshot* Snapshot = Snapshots.FindByPredicate(
		[&TattooId](const FProjectManualTattooRuntimeDebugSnapshot& Candidate)
		{
			return Candidate.TattooId == TattooId;
		}))
	{
		OutSnapshot = *Snapshot;
		return true;
	}
	return false;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::EnsureManualTattoos(APawn* Pawn)
{
	const ProjectDefaultTattooSkinnedDecalPrivate::FRuntimeTattooLimits Limits =
		ProjectDefaultTattooSkinnedDecalPrivate::ResolveRuntimeTattooLimits();
	if (!IsValid(Pawn))
	{
		return false;
	}
	if (ManualTattooRowNames.IsEmpty())
	{
		if (!ManualTattooDecalIndices.IsEmpty())
		{
			ClearManualTattoos(Pawn);
		}
		return true;
	}

	TArray<FName> AtlasRowNames;
	TArray<const FProjectAutomaticTattooTableRow*> SourceAutomaticRows;
	ResolveAutomaticTattooRows(AtlasRowNames, SourceAutomaticRows, true);
	TArray<FProjectAutomaticTattooTableRow> EffectiveAutomaticRowValues;
	TArray<const FProjectAutomaticTattooTableRow*> AtlasRows;
	BuildEffectiveTattooRows(
		AtlasRowNames,
		SourceAutomaticRows,
		EffectiveAutomaticRowValues,
		AtlasRows);
	if (AtlasRowNames.Num() > Limits.MaximumTotalLayers)
	{
		AtlasRowNames.SetNum(Limits.MaximumTotalLayers);
		AtlasRows.SetNum(Limits.MaximumTotalLayers);
	}
	const int32 MaximumManualRowsForAtlas = FMath::Min(
		Limits.MaximumManualTattoos,
		FMath::Max(0, Limits.MaximumTotalLayers - AtlasRowNames.Num()));
	const int32 DesiredManualRowCount = FMath::Min3(
		ManualTattooRowNames.Num(),
		ManualTattooRows.Num(),
		MaximumManualRowsForAtlas);
	for (int32 ManualIndex = 0;
		ManualIndex < ManualTattooRowNames.Num()
			&& ManualIndex < ManualTattooRows.Num()
			&& ManualIndex < MaximumManualRowsForAtlas;
		++ManualIndex)
	{
		AtlasRowNames.Add(ManualTattooRowNames[ManualIndex]);
		AtlasRows.Add(&ManualTattooRows[ManualIndex]);
	}
	if (bTattooShopPreviewForAutomation || bTattooShopPreviewApplied)
	{
		if (AtlasRowNames.Num() < Limits.MaximumTotalLayers)
		{
			AppendTattooShopPreviewAtlasRow(AtlasRowNames, AtlasRows);
		}
	}

	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(Pawn);
	USkinnedDecalSampler* Sampler = ResolveOrCreateSampler(Pawn);
	if (!ConfigureSampler(Sampler, TargetMesh) || !ConfigureOverlayMaterial(Sampler, AtlasRowNames, AtlasRows))
	{
		return false;
	}
	TSet<FName> DesiredAutomaticRows;
	for (int32 RowIndex = 0; RowIndex < AtlasRowNames.Num(); ++RowIndex)
	{
		if (RowIndex < AtlasRows.Num()
			&& AtlasRows[RowIndex]
			&& !ManualTattooParametersByRow.Contains(AtlasRowNames[RowIndex])
			&& AtlasRowNames[RowIndex] != ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName)
		{
			DesiredAutomaticRows.Add(AtlasRowNames[RowIndex]);
		}
	}
	for (const TPair<FName, int32>& Existing : AutomaticTattooDecalIndices)
	{
		if (!DesiredAutomaticRows.Contains(Existing.Key))
		{
			ClearTattooLayer(Pawn, Existing.Value);
		}
	}
	for (auto It = AutomaticTattooDecalIndices.CreateIterator(); It; ++It)
	{
		if (!DesiredAutomaticRows.Contains(It.Key()))
		{
			AutomaticTattooPlacementSignatures.Remove(It.Key());
			AutomaticTattooEffectiveOffsets.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	bool bAllReady = true;
	for (int32 RowIndex = 0; RowIndex < AtlasRowNames.Num() && RowIndex < AtlasRows.Num(); ++RowIndex)
	{
		const FName RowName = AtlasRowNames[RowIndex];
		const FProjectAutomaticTattooTableRow* Row = AtlasRows[RowIndex];
		if (!Row
			|| ManualTattooParametersByRow.Contains(RowName)
			|| RowName == ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName)
		{
			continue;
		}

		FName AnchorBone = Row->AnchorBone;
		if (AnchorBone.IsNone() || !TargetMesh || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
		{
			AnchorBone = ProjectDefaultTattooSkinnedDecalPrivate::ResolvePresetAnchorBone(TargetMesh, Row->PlacementPreset);
		}
		if (AnchorBone.IsNone() || !TargetMesh || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
		{
			AnchorBone = ResolveAnchorBone(TargetMesh);
		}

		const int32 SubUV = AutomaticTattooSubUVByRow.FindRef(RowName);
		const FString PlacementSignature = ProjectDefaultTattooSkinnedDecalPrivate::BuildTattooPlacementSignature(
			RowName,
			Row,
			AnchorBone,
			SubUV);
		if (AutomaticTattooDecalIndices.Contains(RowName)
			&& AutomaticTattooPlacementSignatures.FindRef(RowName) == PlacementSignature)
		{
			continue;
		}

		if (const int32* ExistingIndex = AutomaticTattooDecalIndices.Find(RowName))
		{
			ClearTattooLayer(Pawn, *ExistingIndex);
			AutomaticTattooDecalIndices.Remove(RowName);
			AutomaticTattooPlacementSignatures.Remove(RowName);
		}
		bAllReady &= ApplyAutomaticTattooLayer(Pawn, INDEX_NONE, RowName, Row, SubUV);
	}

	TSet<FName> DesiredRows;
	for (int32 ManualIndex = 0;
		ManualIndex < ManualTattooRowNames.Num() && ManualIndex < MaximumManualRowsForAtlas;
		++ManualIndex)
	{
		DesiredRows.Add(ManualTattooRowNames[ManualIndex]);
	}
	for (const TPair<FName, int32>& Existing : ManualTattooDecalIndices)
	{
		if (!DesiredRows.Contains(Existing.Key))
		{
			ClearTattooLayer(Pawn, Existing.Value);
		}
	}
	for (auto It = ManualTattooDecalIndices.CreateIterator(); It; ++It)
	{
		if (!DesiredRows.Contains(It.Key()))
		{
			ManualTattooPlacementSignatures.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	for (int32 ManualIndex = 0;
		ManualIndex < ManualTattooRowNames.Num()
			&& ManualIndex < ManualTattooRows.Num()
			&& ManualIndex < MaximumManualRowsForAtlas;
		++ManualIndex)
	{
		const FName RowName = ManualTattooRowNames[ManualIndex];
		const FProjectAutomaticTattooTableRow& Row = ManualTattooRows[ManualIndex];
		FName AnchorBone = Row.AnchorBone;
		if (AnchorBone.IsNone() || !TargetMesh || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
		{
			AnchorBone = ProjectDefaultTattooSkinnedDecalPrivate::ResolvePresetAnchorBone(TargetMesh, Row.PlacementPreset);
		}
		if (AnchorBone.IsNone() || !TargetMesh || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
		{
			AnchorBone = ResolveAnchorBone(TargetMesh);
		}
		const int32 SubUV = AutomaticTattooSubUVByRow.FindRef(RowName);
		const FString PlacementSignature = ProjectDefaultTattooSkinnedDecalPrivate::BuildTattooPlacementSignature(
			RowName, &Row, AnchorBone, SubUV);
		if (ManualTattooDecalIndices.Contains(RowName)
			&& ManualTattooPlacementSignatures.FindRef(RowName) == PlacementSignature)
		{
			continue;
		}

		if (const int32* ExistingIndex = ManualTattooDecalIndices.Find(RowName))
		{
			ClearTattooLayer(Pawn, *ExistingIndex);
			ManualTattooDecalIndices.Remove(RowName);
			ManualTattooPlacementSignatures.Remove(RowName);
		}

		TMap<FName, int32> UsedLayerIndices = AutomaticTattooDecalIndices;
		for (const TPair<FName, int32>& ManualLayer : ManualTattooDecalIndices)
		{
			UsedLayerIndices.Add(ManualLayer.Key, ManualLayer.Value);
		}
		const int32 SpawnedIndex = ProjectDefaultTattooSkinnedDecalPrivate::ResolveProjectDecalIndex(
			INDEX_NONE,
			TattooShopPreviewDecalIndex,
			Sampler ? Sampler->MaxDecals : ProjectDefaultTattooSkinnedDecalPrivate::MinimumMaxDecals,
			UsedLayerIndices);
		FSkinnedDecalData DecalData;
		FVector ReferenceLocation = FVector::ZeroVector;
		FQuat ReferenceRotation = FQuat::Identity;
		if (SpawnedIndex == INDEX_NONE || !ProjectDefaultTattooSkinnedDecalPrivate::BuildReferencePoseDecalData(
			TargetMesh, AnchorBone, &Row, SpawnedIndex, SubUV, DecalData, ReferenceLocation, ReferenceRotation))
		{
			bAllReady = false;
			continue;
		}

		Sampler->RemoveDecal(SpawnedIndex);
		Sampler->SpawnDecalFromData(DecalData);
		ManualTattooDecalIndices.Add(RowName, SpawnedIndex);
		ManualTattooPlacementSignatures.Add(RowName, PlacementSignature);
		AppliedPawn = Pawn;
		AppliedSampler = Sampler;
	}
	return bAllReady
		&& ManualTattooDecalIndices.Num() == DesiredManualRowCount
		&& AutomaticTattooDecalIndices.Num() == DesiredAutomaticRows.Num();
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::TryApplyTattooShopPreview(APawn* Pawn)
{
	TArray<FName> RowNames;
	TArray<const FProjectAutomaticTattooTableRow*> SourceRows;
	ResolveAutomaticTattooRows(RowNames, SourceRows, true);
	TArray<FProjectAutomaticTattooTableRow> EffectiveRowValues;
	TArray<const FProjectAutomaticTattooTableRow*> Rows;
	BuildEffectiveTattooRows(RowNames, SourceRows, EffectiveRowValues, Rows);

	FName PreviewRowName = NAME_None;
	const FProjectAutomaticTattooTableRow* PreviewRow = nullptr;
	if (!AppendTattooShopPreviewAtlasRow(RowNames, Rows, &PreviewRowName, &PreviewRow) || !PreviewRow)
	{
		return false;
	}

	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(Pawn);
	USkinnedDecalSampler* Sampler = ResolveOrCreateSampler(Pawn);
	if (!ConfigureSampler(Sampler, TargetMesh) || !ConfigureOverlayMaterial(Sampler, RowNames, Rows))
	{
		return false;
	}

	const int32 SubUV = AutomaticTattooSubUVByRow.FindRef(ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName);
	return ApplyTattooLayer(Pawn, TattooShopPreviewDecalIndex, TEXT("TattooShop preview"), PreviewRowName, PreviewRow, SubUV);
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ApplyTattooLayer(
	APawn* Pawn,
	const int32 DecalIndex,
	const TCHAR* LayerName,
	const FName RowName,
	const FProjectAutomaticTattooTableRow* TattooRow,
	const int32 SubUV)
{
	if (!IsValid(Pawn))
	{
		return false;
	}

	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(Pawn);
	if (!IsValid(TargetMesh) || !IsValid(TargetMesh->GetSkinnedAsset()))
	{
		return false;
	}

	if (!TattooRow || !TattooRow->bEnabled)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] No enabled unlocked automatic tattoo row was found in %s."),
			ProjectDefaultTattooSkinnedDecalPrivate::AutomaticTattooTablePath);
		return false;
	}

	FName AnchorBone = TattooRow->AnchorBone;
	if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
	{
		AnchorBone = ResolveAnchorBone(TargetMesh);
	}
	if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Could not find a valid torso bone on %s mesh=%s."),
			*GetNameSafe(Pawn),
			*GetNameSafe(TargetMesh));
		return false;
	}

	USkinnedDecalSampler* Sampler = ResolveOrCreateSampler(Pawn);
	if (!ConfigureSampler(Sampler, TargetMesh))
	{
		return false;
	}

	const FVector TattooLocation = ComputeTattooLocation(Pawn, TargetMesh, AnchorBone, TattooRow);
	const FQuat TattooRotation = ComputeTattooRotation(Pawn, TattooRow);
	const float TattooSize = TattooRow ? TattooRow->Size : ProjectDefaultTattooSkinnedDecalPrivate::DefaultTattooDecalSize;
	const int32 SpawnedDecalIndex = Sampler->SpawnDecal(
		TattooLocation,
		TattooRotation,
		AnchorBone,
		FMath::Max(1.0f, TattooSize),
		SubUV,
		DecalIndex);

	AppliedPawn = Pawn;
	AppliedSampler = Sampler;
	if (LayerName && FCString::Stricmp(LayerName, TEXT("automatic")) == 0)
	{
		AutomaticTattooDecalIndices.Add(RowName, SpawnedDecalIndex);
	}
	else if (LayerName && FCString::Stricmp(LayerName, TEXT("TattooShop preview")) == 0)
	{
		bTattooShopPreviewApplied = true;
		TattooShopPreviewDecalIndex = SpawnedDecalIndex;
	}

	UE_LOG(
		LogProjectDefaultTattooSkinnedDecal,
		Display,
		TEXT("[TattooSkinnedDecal] Applied %s tattoo layer via SkinnedDecal row=%s pawn=%s mesh=%s bone=%s requestedIndex=%d spawnedIndex=%d subUV=%d location=%s offset=(%.2f, %.2f) size=%.2f rotation=%.2f."),
		LayerName ? LayerName : TEXT("project"),
		*RowName.ToString(),
		*GetNameSafe(Pawn),
		*GetNameSafe(TargetMesh),
		*AnchorBone.ToString(),
		DecalIndex,
		SpawnedDecalIndex,
		SubUV,
		*TattooLocation.ToCompactString(),
		TattooRow ? TattooRow->OffsetX : 0.0f,
		TattooRow ? TattooRow->OffsetY : 0.0f,
		FMath::Max(1.0f, TattooSize),
		TattooRow ? TattooRow->RotationDegrees : 0.0f);

	return true;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ApplyAutomaticTattooLayer(
	APawn* Pawn,
	const int32 DecalIndex,
	const FName RowName,
	const FProjectAutomaticTattooTableRow* TattooRow,
	const int32 SubUV)
{
	if (!IsValid(Pawn))
	{
		return false;
	}

	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(Pawn);
	if (!IsValid(TargetMesh) || !IsValid(TargetMesh->GetSkinnedAsset()))
	{
		return false;
	}

	if (!TattooRow || !TattooRow->bEnabled)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] No enabled automatic tattoo row was found for %s."),
			*RowName.ToString());
		return false;
	}

	FName AnchorBone = TattooRow->AnchorBone;
	if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
	{
		AnchorBone = ProjectDefaultTattooSkinnedDecalPrivate::ResolvePresetAnchorBone(TargetMesh, TattooRow->PlacementPreset);
	}
	if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
	{
		AnchorBone = ResolveAnchorBone(TargetMesh);
	}
	if (AnchorBone.IsNone() || TargetMesh->GetBoneIndex(AnchorBone) == INDEX_NONE)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Could not resolve a reference-pose anchor bone for automatic row=%s pawn=%s mesh=%s preset=%s."),
			*RowName.ToString(),
			*GetNameSafe(Pawn),
			*GetNameSafe(TargetMesh),
			ProjectDefaultTattooSkinnedDecalPrivate::PlacementPresetName(TattooRow->PlacementPreset));
		return false;
	}

	USkinnedDecalSampler* Sampler = ResolveOrCreateSampler(Pawn);
	if (!ConfigureSampler(Sampler, TargetMesh))
	{
		return false;
	}

	TMap<FName, int32> UsedLayerIndices = AutomaticTattooDecalIndices;
	for (const TPair<FName, int32>& ManualLayer : ManualTattooDecalIndices)
	{
		UsedLayerIndices.Add(ManualLayer.Key, ManualLayer.Value);
	}
	const int32 SpawnedDecalIndex = ProjectDefaultTattooSkinnedDecalPrivate::ResolveProjectDecalIndex(
		DecalIndex,
		TattooShopPreviewDecalIndex,
		Sampler ? Sampler->MaxDecals : ProjectDefaultTattooSkinnedDecalPrivate::MinimumMaxDecals,
		UsedLayerIndices);
	if (SpawnedDecalIndex == INDEX_NONE)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Could not allocate an automatic decal index for row=%s."),
			*RowName.ToString());
		return false;
	}

	FSkinnedDecalData StableDecalData;
	FVector ReferenceLocation = FVector::ZeroVector;
	FQuat ReferenceRotation = FQuat::Identity;
	if (!ProjectDefaultTattooSkinnedDecalPrivate::BuildReferencePoseDecalData(
		TargetMesh,
		AnchorBone,
		TattooRow,
		SpawnedDecalIndex,
		SubUV,
		StableDecalData,
		ReferenceLocation,
		ReferenceRotation))
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Could not build reference-pose decal data for row=%s bone=%s."),
			*RowName.ToString(),
			*AnchorBone.ToString());
		return false;
	}

	Sampler->RemoveDecal(SpawnedDecalIndex);
	Sampler->SpawnDecalFromData(StableDecalData);

	AppliedPawn = Pawn;
	AppliedSampler = Sampler;
	AutomaticTattooDecalIndices.Add(RowName, SpawnedDecalIndex);
	AutomaticTattooPlacementSignatures.Add(
		RowName,
		ProjectDefaultTattooSkinnedDecalPrivate::BuildTattooPlacementSignature(RowName, TattooRow, AnchorBone, SubUV));
	AutomaticTattooEffectiveOffsets.Add(RowName, FVector2D(TattooRow->OffsetX, TattooRow->OffsetY));

	UE_LOG(
		LogProjectDefaultTattooSkinnedDecal,
		Display,
		TEXT("[TattooSkinnedDecal] Applied automatic tattoo from reference-pose data row=%s pawn=%s mesh=%s preset=%s bone=%s decalIndex=%d subUV=%d refLocation=%s offset=(%.2f, %.2f) size=%.2f rotation=%.2f projection=%.2f."),
		*RowName.ToString(),
		*GetNameSafe(Pawn),
		*GetNameSafe(TargetMesh),
		ProjectDefaultTattooSkinnedDecalPrivate::PlacementPresetName(TattooRow->PlacementPreset),
		*AnchorBone.ToString(),
		SpawnedDecalIndex,
		SubUV,
		*ReferenceLocation.ToCompactString(),
		TattooRow->OffsetX,
		TattooRow->OffsetY,
		FMath::Max(1.0f, TattooRow->Size),
		TattooRow->RotationDegrees,
		TattooRow->ProjectionDistance);

	return true;
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ClearAutomaticTattoo(APawn* Pawn)
{
	TArray<int32> LayersToClear;
	for (const TPair<FName, int32>& AutomaticLayer : AutomaticTattooDecalIndices)
	{
		LayersToClear.Add(AutomaticLayer.Value);
	}
	AutomaticTattooDecalIndices.Reset();
	AutomaticTattooPlacementSignatures.Reset();
	AutomaticTattooEffectiveOffsets.Reset();
	AutomaticTattooCompositeSourceCountByRow.Reset();
	AutomaticTattooCompositeGroupKeyByRow.Reset();

	for (const int32 DecalIndex : LayersToClear)
	{
		ClearTattooLayer(Pawn, DecalIndex);
	}

	USkinnedDecalSampler* Sampler = AppliedSampler.Get();
	if (!Sampler && IsValid(Pawn))
	{
		Sampler = Pawn->FindComponentByClass<USkinnedDecalSampler>();
	}
	if (!bTattooShopPreviewApplied
		&& ManualTattooDecalIndices.IsEmpty()
		&& IsValid(Sampler)
		&& IsValid(Sampler->Mesh)
		&& Sampler->Mesh->GetOverlayMaterial() == Sampler->OverlayBlendMaterialDynamic)
	{
		Sampler->Mesh->SetOverlayMaterial(nullptr);
	}
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ClearManualTattoos(APawn* Pawn)
{
	TArray<int32> LayersToClear;
	ManualTattooDecalIndices.GenerateValueArray(LayersToClear);
	ManualTattooDecalIndices.Reset();
	ManualTattooPlacementSignatures.Reset();
	for (const int32 DecalIndex : LayersToClear)
	{
		ClearTattooLayer(Pawn, DecalIndex);
	}
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ClearTattooShopPreviewLayer(APawn* Pawn)
{
	const int32 PreviewLayerToClear = TattooShopPreviewDecalIndex;
	TattooShopPreviewDecalIndex = ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewReservedDecalIndex;
	bTattooShopPreviewApplied = false;

	ClearTattooLayer(Pawn, PreviewLayerToClear);

	USkinnedDecalSampler* Sampler = AppliedSampler.Get();
	if (!Sampler && IsValid(Pawn))
	{
		Sampler = Pawn->FindComponentByClass<USkinnedDecalSampler>();
	}
	if (AutomaticTattooDecalIndices.IsEmpty()
		&& ManualTattooDecalIndices.IsEmpty()
		&& IsValid(Sampler)
		&& IsValid(Sampler->Mesh)
		&& Sampler->Mesh->GetOverlayMaterial() == Sampler->OverlayBlendMaterialDynamic)
	{
		Sampler->Mesh->SetOverlayMaterial(nullptr);
	}
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ClearProjectTattooLayers(APawn* Pawn)
{
	ClearTattooShopPreviewLayer(Pawn);
	ClearManualTattoos(Pawn);
	ClearAutomaticTattoo(Pawn);
	AppliedPawn = nullptr;
	AppliedSampler = nullptr;
	AppliedTargetMesh = nullptr;
	AutomaticTattooPlacementSignatures.Reset();
	AutomaticTattooEffectiveOffsets.Reset();
	AutomaticTattooCompositeSourceCountByRow.Reset();
	AutomaticTattooCompositeGroupKeyByRow.Reset();
	CachedOverlaySampler = nullptr;
	CachedOverlayMaterialSignature.Reset();
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ClearTattooLayer(APawn* Pawn, const int32 DecalIndex)
{
	if (DecalIndex == INDEX_NONE)
	{
		return;
	}

	USkinnedDecalSampler* Sampler = AppliedSampler.Get();
	if (!Sampler && IsValid(Pawn))
	{
		Sampler = Pawn->FindComponentByClass<USkinnedDecalSampler>();
	}

	if (IsValid(Sampler))
	{
		Sampler->RemoveDecal(DecalIndex);
	}
}

USkeletalMeshComponent* UProjectDefaultTattooSkinnedDecalSubsystem::ResolveTargetMesh(APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Pawn->GetComponents<USkeletalMeshComponent>(MeshComponents, true);

	USkeletalMeshComponent* BestMesh = nullptr;
	int32 BestScore = MIN_int32;
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		const int32 Score = ProjectDefaultTattooSkinnedDecalPrivate::ScoreSkeletalMeshComponent(MeshComponent);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestMesh = MeshComponent;
		}
	}

	if (BestMesh && BestScore > 0)
	{
		return BestMesh;
	}

	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			return CharacterMesh;
		}
	}

	return BestMesh;
}

USkinnedDecalSampler* UProjectDefaultTattooSkinnedDecalSubsystem::ResolveOrCreateSampler(APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	TArray<USkinnedDecalSampler*> Samplers;
	Pawn->GetComponents<USkinnedDecalSampler>(Samplers);
	for (USkinnedDecalSampler* Sampler : Samplers)
	{
		if (IsValid(Sampler))
		{
			return Sampler;
		}
	}

	USkinnedDecalSampler* Sampler = NewObject<USkinnedDecalSampler>(
		Pawn,
		USkinnedDecalSampler::StaticClass(),
		TEXT("ProjectDefaultTattooSkinnedDecalSampler"));
	if (!Sampler)
	{
		return nullptr;
	}

	Pawn->AddInstanceComponent(Sampler);
	Sampler->RegisterComponent();
	return Sampler;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ConfigureSampler(USkinnedDecalSampler* Sampler, USkeletalMeshComponent* TargetMesh)
{
	if (!IsValid(Sampler) || !IsValid(TargetMesh))
	{
		return false;
	}

	Sampler->BlendMode = ESkinnedDecalBlendMode::Overlay;
	Sampler->AdditionalData = ESkinnedDecalAdditionalData::NoAdditionalData;
	Sampler->MinDecalDistance = 0.0f;
	Sampler->MaxDecals = FMath::Max(Sampler->MaxDecals, ProjectDefaultTattooSkinnedDecalPrivate::MinimumMaxDecals);
	Sampler->EnableSaveGame = false;

	if (!Sampler->OverlayBlendMaterial.IsValid())
	{
		UMaterialInterface* OverlayMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			ProjectDefaultTattooSkinnedDecalPrivate::OverlayMaterialPath);
		if (OverlayMaterial)
		{
			Sampler->OverlayBlendMaterial = OverlayMaterial;
		}
	}

	if (!Sampler->OverlayBlendMaterial.IsValid())
	{
		UE_LOG(LogProjectDefaultTattooSkinnedDecal, Warning, TEXT("[TattooSkinnedDecal] Missing SkinnedDecal overlay material."));
		return false;
	}

	const bool bAlreadyConfiguredForTarget = Sampler->Mesh == TargetMesh && Sampler->OverlayBlendMaterialDynamic != nullptr;
	const bool bTargetChanged = AppliedTargetMesh.IsValid() && AppliedTargetMesh.Get() != TargetMesh;

	if (Sampler->Mesh && Sampler->Mesh != TargetMesh)
	{
		Sampler->Mesh->SetOverlayMaterial(nullptr);
	}
	for (USkeletalMeshComponent* RenderMesh : Sampler->RenderMeshes)
	{
		if (IsValid(RenderMesh) && RenderMesh != TargetMesh)
		{
			RenderMesh->SetOverlayMaterial(nullptr);
		}
	}

	if (!bAlreadyConfiguredForTarget)
	{
		Sampler->RenderMeshes.Reset();
		Sampler->Materials.Reset();
		Sampler->OverlayBlendMaterialDynamic = nullptr;
		CachedOverlaySampler = nullptr;
		CachedOverlayMaterialSignature.Reset();
		Sampler->SetMeshComponent(TargetMesh, false);
		if (bTargetChanged)
		{
			// Decal indices/signatures describe data owned by the old sampler mesh.
			// Force every layer to respawn against the replacement visible body.
			AutomaticTattooDecalIndices.Reset();
			ManualTattooDecalIndices.Reset();
			AutomaticTattooPlacementSignatures.Reset();
			ManualTattooPlacementSignatures.Reset();
			bTattooShopPreviewApplied = false;
		}
	}
	else if (TargetMesh->GetOverlayMaterial() != Sampler->OverlayBlendMaterialDynamic)
	{
		TargetMesh->SetOverlayMaterial(Sampler->OverlayBlendMaterialDynamic);
	}

	AppliedTargetMesh = TargetMesh;
	return Sampler->OverlayBlendMaterialDynamic != nullptr;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::ConfigureOverlayMaterial(
	USkinnedDecalSampler* Sampler,
	const TArray<FName>& RowNames,
	const TArray<const FProjectAutomaticTattooTableRow*>& TattooRows)
{
	if (!IsValid(Sampler) || !IsValid(Sampler->OverlayBlendMaterialDynamic))
	{
		return false;
	}

	if (RowNames.IsEmpty() || TattooRows.IsEmpty() || RowNames.Num() != TattooRows.Num())
	{
		return false;
	}

	const FString OverlayMaterialSignature =
		ProjectDefaultTattooSkinnedDecalPrivate::BuildOverlayMaterialSignature(
			Sampler,
			RowNames,
			TattooRows,
			&ManualTattooParametersByRow);
	if (CachedOverlaySampler.Get() == Sampler
		&& CachedOverlayMaterialSignature == OverlayMaterialSignature
		&& (RuntimeAutomaticTattooAtlasRenderTarget || RuntimeAutomaticTattooAtlasTexture)
		&& RuntimeNeutralCompactTexture
		&& !AutomaticTattooSubUVByRow.IsEmpty())
	{
		return true;
	}

	int32 SubImagesX = 1;
	int32 SubImagesY = 1;
	TMap<FName, int32> SubUVByRow;
	TMap<FName, int32> CompositeSourceCountByRow;
	TMap<FName, FString> CompositeGroupKeyByRow;
	UTexture* TattooAtlasTexture = CreateAutomaticTattooAtlas(
		RowNames,
		TattooRows,
		SubImagesX,
		SubImagesY,
		SubUVByRow,
		CompositeSourceCountByRow,
		CompositeGroupKeyByRow);
	if (!TattooAtlasTexture)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Could not build an automatic tattoo atlas from %d rows."),
			TattooRows.Num());
		return false;
	}

	AutomaticTattooSubUVByRow = SubUVByRow;
	AutomaticTattooCompositeSourceCountByRow = CompositeSourceCountByRow;
	AutomaticTattooCompositeGroupKeyByRow = CompositeGroupKeyByRow;
	AutomaticTattooAtlasSubImagesX = FMath::Max(1, SubImagesX);
	AutomaticTattooAtlasSubImagesY = FMath::Max(1, SubImagesY);

	if (!RuntimeNeutralCompactTexture)
	{
		RuntimeNeutralCompactTexture = ProjectDefaultTattooSkinnedDecalPrivate::CreateSolidTexture(
			this,
			TEXT("ProjectDefaultTattooNeutralCompact"),
			FColor(128, 128, 128, 255));
	}

	const auto SetOverlayTextureParameter = [Sampler](const FName ParameterName, UTexture* Texture)
	{
		if (!Texture)
		{
			return;
		}

		Sampler->OverlayBlendMaterialDynamic->SetTextureParameterValue(ParameterName, Texture);
		Sampler->OverlayBlendMaterialDynamic->SetTextureParameterValueByInfo(
			FMaterialParameterInfo(ParameterName, Sampler->Association, Sampler->LayerIndex),
			Texture);
	};

	const auto SetOverlayScalarParameter = [Sampler](const FName ParameterName, const float Value)
	{
		Sampler->OverlayBlendMaterialDynamic->SetScalarParameterValue(ParameterName, Value);
		Sampler->OverlayBlendMaterialDynamic->SetScalarParameterValueByInfo(
			FMaterialParameterInfo(ParameterName, Sampler->Association, Sampler->LayerIndex),
			Value);
	};

	SetOverlayTextureParameter(ProjectDefaultTattooSkinnedDecalPrivate::BaseColorParameterName, TattooAtlasTexture);
	SetOverlayTextureParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalColorParameterName, TattooAtlasTexture);
	SetOverlayTextureParameter(ProjectDefaultTattooSkinnedDecalPrivate::CompactParameterName, RuntimeNeutralCompactTexture);
	SetOverlayTextureParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalCompactParameterName, RuntimeNeutralCompactTexture);

	if (UTexture* EmptyNormalTexture = LoadObject<UTexture>(nullptr, ProjectDefaultTattooSkinnedDecalPrivate::EmptyNormalTexturePath))
	{
		SetOverlayTextureParameter(ProjectDefaultTattooSkinnedDecalPrivate::NormalParameterName, EmptyNormalTexture);
		SetOverlayTextureParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalNormalParameterName, EmptyNormalTexture);
	}

	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::EmissiveStrengthParameterName, 0.0f);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::UseEmissiveParameterName, 0.0f);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalDepthParameterName, ProjectDefaultTattooSkinnedDecalPrivate::DefaultTattooDecalDepth);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalUseCompactMapParameterName, 0.0f);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalUseNormalMapParameterName, 0.0f);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::DecalUseOffsetParameterName, 0.0f);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::SubImagesXParameterName, static_cast<float>(AutomaticTattooAtlasSubImagesX));
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::SubImagesYParameterName, static_cast<float>(AutomaticTattooAtlasSubImagesY));
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::AnimationFrameRateParameterName, 0.0f);
	SetOverlayScalarParameter(ProjectDefaultTattooSkinnedDecalPrivate::AnimationTotalFramesParameterName, 1.0f);

	CachedOverlaySampler = Sampler;
	CachedOverlayMaterialSignature = OverlayMaterialSignature;
	return true;
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::IsAutomaticTattooRowActive(
	const FName RowName,
	const FProjectAutomaticTattooTableRow* TattooRow) const
{
	if (!TattooRow || !TattooRow->bEnabled)
	{
		return false;
	}

	const bool bIntimacyRuntimeRow =
		RowName == UProjectIntimacySubsystem::TestTattooIntimacyRewardId
		|| TattooRow->RewardId == UProjectIntimacySubsystem::TestTattooIntimacyRewardId;
	const bool bRequiresRuntimeReward =
		TattooRow->UnlockRule == EProjectAutomaticTattooUnlockRule::RuntimeReward
		|| bIntimacyRuntimeRow;

	if (!bRequiresRuntimeReward)
	{
		return true;
	}

#if !UE_BUILD_SHIPPING
	if (RuntimeDebugForcedActiveRows.Contains(RowName))
	{
		return true;
	}
#endif

	if (TattooRow->RewardId.IsNone())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UProjectIntimacySubsystem* IntimacySubsystem = World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr;
	return IntimacySubsystem && IntimacySubsystem->IsAutomaticTattooRewardUnlocked(TattooRow->RewardId);
}

void UProjectDefaultTattooSkinnedDecalSubsystem::ResolveAutomaticTattooRows(
	TArray<FName>& OutRowNames,
	TArray<const FProjectAutomaticTattooTableRow*>& OutRows,
	const bool bOnlyActive) const
{
	OutRowNames.Reset();
	OutRows.Reset();

	const UDataTable* AutomaticTattooTable = LoadObject<UDataTable>(
		nullptr,
		ProjectDefaultTattooSkinnedDecalPrivate::AutomaticTattooTablePath);
	if (!AutomaticTattooTable)
	{
		return;
	}

	TArray<FName> RowNames = AutomaticTattooTable->GetRowNames();
	RowNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	for (const FName RowName : RowNames)
	{
		const FProjectAutomaticTattooTableRow* CandidateRow = AutomaticTattooTable->FindRow<FProjectAutomaticTattooTableRow>(
			RowName,
			TEXT("ProjectAutomaticTattoo"),
			false);
		if (!CandidateRow)
		{
			continue;
		}

		FProjectAutomaticTattooTableRow EffectiveCandidateRow = BuildEffectiveTattooRow(RowName, CandidateRow);
		if (bOnlyActive && !IsAutomaticTattooRowActive(RowName, &EffectiveCandidateRow))
		{
			continue;
		}

		OutRowNames.Add(RowName);
		OutRows.Add(CandidateRow);
	}
}

const FProjectAutomaticTattooTableRow* UProjectDefaultTattooSkinnedDecalSubsystem::FindAutomaticTattooRow(const FName RowName) const
{
	if (RowName.IsNone())
	{
		return nullptr;
	}

	const UDataTable* AutomaticTattooTable = LoadObject<UDataTable>(
		nullptr,
		ProjectDefaultTattooSkinnedDecalPrivate::AutomaticTattooTablePath);
	if (!AutomaticTattooTable)
	{
		return nullptr;
	}

	return AutomaticTattooTable->FindRow<FProjectAutomaticTattooTableRow>(
		RowName,
		TEXT("ProjectAutomaticTattooRuntimeDebug"),
		false);
}

FProjectAutomaticTattooTableRow UProjectDefaultTattooSkinnedDecalSubsystem::BuildEffectiveTattooRow(
	const FName RowName,
	const FProjectAutomaticTattooTableRow* TattooRow) const
{
	FProjectAutomaticTattooTableRow EffectiveRow;
	if (TattooRow)
	{
		EffectiveRow = *TattooRow;
	}

#if !UE_BUILD_SHIPPING
	if (const FProjectAutomaticTattooRuntimePlacementOverride* RuntimeOverride = RuntimeDebugPlacementOverrides.Find(RowName))
	{
		EffectiveRow.PlacementPreset = RuntimeOverride->PlacementPreset;
		EffectiveRow.AnchorBone = RuntimeOverride->AnchorBone;
		EffectiveRow.OffsetX = RuntimeOverride->OffsetX;
		EffectiveRow.OffsetY = RuntimeOverride->OffsetY;
		EffectiveRow.Size = FMath::Max(1.0f, RuntimeOverride->Size);
		EffectiveRow.RotationDegrees = RuntimeOverride->RotationDegrees;
		EffectiveRow.ProjectionDistance = FMath::Max(0.0f, RuntimeOverride->ProjectionDistance);
		EffectiveRow.bEnabled = RuntimeOverride->bEnabled;
	}
#endif

	return EffectiveRow;
}

void UProjectDefaultTattooSkinnedDecalSubsystem::BuildEffectiveTattooRows(
	const TArray<FName>& RowNames,
	const TArray<const FProjectAutomaticTattooTableRow*>& SourceRows,
	TArray<FProjectAutomaticTattooTableRow>& OutEffectiveRows,
	TArray<const FProjectAutomaticTattooTableRow*>& OutEffectiveRowPtrs) const
{
	OutEffectiveRows.Reset();
	OutEffectiveRowPtrs.Reset();
	OutEffectiveRows.Reserve(SourceRows.Num());
	OutEffectiveRowPtrs.Reserve(SourceRows.Num());

	for (int32 RowIndex = 0; RowIndex < SourceRows.Num(); ++RowIndex)
	{
		const FName RowName = RowNames.IsValidIndex(RowIndex) ? RowNames[RowIndex] : NAME_None;
		OutEffectiveRows.Add(BuildEffectiveTattooRow(RowName, SourceRows[RowIndex]));
	}

	for (FProjectAutomaticTattooTableRow& EffectiveRow : OutEffectiveRows)
	{
		OutEffectiveRowPtrs.Add(&EffectiveRow);
	}
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::RefreshAutomaticTattooAfterRuntimeDebugChange(APawn* Pawn)
{
	APawn* TargetPawn = IsValid(Pawn) ? Pawn : ResolveLocalPlayerPawn();
	if (!IsValid(TargetPawn))
	{
		return false;
	}

	RetryCooldownSeconds = 0.0f;
	if (!IsAutomaticTattooUnlockedForAutomation())
	{
		ClearAutomaticTattoo(TargetPawn);
		return true;
	}

	return EnsureAutomaticTattoo(TargetPawn);
}

bool UProjectDefaultTattooSkinnedDecalSubsystem::AppendTattooShopPreviewAtlasRow(
	TArray<FName>& InOutRowNames,
	TArray<const FProjectAutomaticTattooTableRow*>& InOutRows,
	FName* OutPreviewRowName,
	const FProjectAutomaticTattooTableRow** OutPreviewRow) const
{
	TArray<FName> AllRowNames;
	TArray<const FProjectAutomaticTattooTableRow*> AllRows;
	ResolveAutomaticTattooRows(AllRowNames, AllRows, false);
	if (AllRows.IsEmpty())
	{
		return false;
	}

	FName PreviewRowName = AllRowNames[0];
	const FProjectAutomaticTattooTableRow* PreviewRow = AllRows[0];
	for (int32 RowIndex = 0; RowIndex < AllRows.Num(); ++RowIndex)
	{
		const UTexture2D* Texture = ResolveTattooTexture(AllRows[RowIndex]);
		if (Texture && Texture->GetPathName().Contains(TEXT("/Game/TattooShop/Texture/T_Heart.")))
		{
			PreviewRowName = AllRowNames[RowIndex];
			PreviewRow = AllRows[RowIndex];
			break;
		}
	}

	if (!PreviewRow)
	{
		return false;
	}

	if (OutPreviewRowName)
	{
		*OutPreviewRowName = PreviewRowName;
	}
	if (OutPreviewRow)
	{
		*OutPreviewRow = PreviewRow;
	}

	if (!InOutRowNames.Contains(ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName))
	{
		InOutRowNames.Add(ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName);
		InOutRows.Add(PreviewRow);
	}

	return true;
}

UTexture* UProjectDefaultTattooSkinnedDecalSubsystem::CreateAutomaticTattooAtlas(
	const TArray<FName>& RowNames,
	const TArray<const FProjectAutomaticTattooTableRow*>& TattooRows,
	int32& OutSubImagesX,
	int32& OutSubImagesY,
	TMap<FName, int32>& OutSubUVByRow,
	TMap<FName, int32>& OutCompositeSourceCountByRow,
	TMap<FName, FString>& OutCompositeGroupKeyByRow)
{
	const ProjectDefaultTattooSkinnedDecalPrivate::FRuntimeTattooLimits Limits =
		ProjectDefaultTattooSkinnedDecalPrivate::ResolveRuntimeTattooLimits();
	OutSubImagesX = 1;
	OutSubImagesY = 1;
	OutSubUVByRow.Reset();
	OutCompositeSourceCountByRow.Reset();
	OutCompositeGroupKeyByRow.Reset();

	if (RowNames.IsEmpty() || TattooRows.IsEmpty())
	{
		return nullptr;
	}

	struct FRuntimeAtlasSource
	{
		FName RowName = NAME_None;
		UTexture2D* Texture = nullptr;
		FLinearColor DrawColor = FLinearColor::White;
		int32 Width = 1;
		int32 Height = 1;
	};

	TArray<FRuntimeAtlasSource> RuntimeSources;
	RuntimeAtlasSourceTextures.Reset();
	RuntimeSources.Reserve(FMath::Min(RowNames.Num(), Limits.MaximumTotalLayers));
	for (int32 RowIndex = 0;
		RowIndex < TattooRows.Num()
			&& RowIndex < RowNames.Num()
			&& RuntimeSources.Num() < Limits.MaximumTotalLayers;
		++RowIndex)
	{
		const FName RowName = RowNames[RowIndex];
		UTexture2D* SourceTexture = ResolveTattooTexture(TattooRows[RowIndex]);
		if (RowName.IsNone() || !IsValid(SourceTexture))
		{
			continue;
		}

		// T_Heart and legacy TattooShop textures are colour-keyed images rather
		// than transparent PNGs.  Feeding their raw RGB to the atlas makes the
		// entire rectangular card opaque on the initial spawn.  Reuse the
		// established mask conversion before the canvas copies the source.
		UTexture2D* MaskedTexture = CreateMaskedTattooTexture(SourceTexture);
		if (!IsValid(MaskedTexture))
		{
			continue;
		}
		RuntimeAtlasSourceTextures.Add(MaskedTexture);

		FRuntimeAtlasSource& RuntimeSource = RuntimeSources.AddDefaulted_GetRef();
		RuntimeSource.RowName = RowName;
		RuntimeSource.Texture = MaskedTexture;
		RuntimeSource.Width = FMath::Max(1, MaskedTexture->GetSizeX());
		RuntimeSource.Height = FMath::Max(1, MaskedTexture->GetSizeY());
		if (const FProjectTattooParameters* Parameters = ManualTattooParametersByRow.Find(RowName))
		{
			const FLinearColor EffectiveTint = Parameters->bUseTint
				? Parameters->Color
				: FLinearColor::White;
			RuntimeSource.DrawColor = FLinearColor(
				FMath::Max(0.0f, EffectiveTint.R),
				FMath::Max(0.0f, EffectiveTint.G),
				FMath::Max(0.0f, EffectiveTint.B),
				FMath::Clamp(Parameters->Opacity, 0.0f, 1.0f));
		}
	}

	if (RuntimeSources.IsEmpty())
	{
		return nullptr;
	}

	// One atlas cell per logical row is intentional: two GUIDs must never own
	// the same color image or SubUV, even when they share placement values.
	OutSubImagesX = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(RuntimeSources.Num()))));
	OutSubImagesY = FMath::Max(1, FMath::CeilToInt(static_cast<float>(RuntimeSources.Num()) / static_cast<float>(OutSubImagesX)));
	const int32 AtlasWidth = Limits.AtlasCellSize * OutSubImagesX;
	const int32 AtlasHeight = Limits.AtlasCellSize * OutSubImagesY;
	if (AtlasWidth > Limits.MaximumAtlasSize || AtlasHeight > Limits.MaximumAtlasSize)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Error,
			TEXT("[TattooSkinnedDecal] Refusing atlas %dx%d: configured maximum is %d (cell=%d layers=%d)."),
			AtlasWidth,
			AtlasHeight,
			Limits.MaximumAtlasSize,
			Limits.AtlasCellSize,
			RuntimeSources.Num());
		return nullptr;
	}

	RuntimeAutomaticTattooAtlasRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		this,
		AtlasWidth,
		AtlasHeight,
		RTF_RGBA8_SRGB,
		FLinearColor::Transparent,
		false,
		false);
	if (!RuntimeAutomaticTattooAtlasRenderTarget)
	{
		return nullptr;
	}

	RuntimeAutomaticTattooAtlasRenderTarget->TargetGamma = 2.2f;
	RuntimeAutomaticTattooAtlasRenderTarget->AddressX = TA_Clamp;
	RuntimeAutomaticTattooAtlasRenderTarget->AddressY = TA_Clamp;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RuntimeAutomaticTattooAtlasRenderTarget, FLinearColor::Transparent);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext RenderTargetContext;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(
		this,
		RuntimeAutomaticTattooAtlasRenderTarget,
		Canvas,
		CanvasSize,
		RenderTargetContext);
	if (!IsValid(Canvas))
	{
		RuntimeAutomaticTattooAtlasRenderTarget = nullptr;
		return nullptr;
	}

	for (int32 AtlasIndex = 0; AtlasIndex < RuntimeSources.Num(); ++AtlasIndex)
	{
		const FRuntimeAtlasSource& RuntimeSource = RuntimeSources[AtlasIndex];
		const int32 CellX = AtlasIndex % OutSubImagesX;
		const int32 CellY = AtlasIndex / OutSubImagesX;
		const float CellSize = static_cast<float>(Limits.AtlasCellSize);
		const float FitSize = CellSize * ProjectDefaultTattooSkinnedDecalPrivate::RuntimeAtlasCellFill;
		const float SourceAspect = static_cast<float>(RuntimeSource.Width) / static_cast<float>(RuntimeSource.Height);
		const FVector2D DrawSize = SourceAspect >= 1.0f
			? FVector2D(FitSize, FitSize / SourceAspect)
			: FVector2D(FitSize * SourceAspect, FitSize);
		const FVector2D DrawPosition(
			static_cast<float>(CellX) * CellSize + (CellSize - DrawSize.X) * 0.5f,
			static_cast<float>(CellY) * CellSize + (CellSize - DrawSize.Y) * 0.5f);

		Canvas->K2_DrawTexture(
			RuntimeSource.Texture,
			DrawPosition,
			DrawSize,
			FVector2D::ZeroVector,
			FVector2D::UnitVector,
			RuntimeSource.DrawColor,
			// Every tattoo owns its cell, so overwrite RGBA verbatim instead of
			// premultiplying RGB through a translucent canvas blend.
			BLEND_Opaque,
			0.0f,
			FVector2D::ZeroVector);

		OutSubUVByRow.Add(RuntimeSource.RowName, AtlasIndex);
		OutCompositeSourceCountByRow.Add(RuntimeSource.RowName, 1);
		OutCompositeGroupKeyByRow.Add(RuntimeSource.RowName, RuntimeSource.RowName.ToString());
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, RenderTargetContext);
	RuntimeAutomaticTattooAtlasTexture = nullptr;
	return RuntimeAutomaticTattooAtlasRenderTarget.Get();

#if 0

	struct FAtlasSource
	{
		FName RowName = NAME_None;
		FString CompositeGroupKey;
		UTexture2D* Texture = nullptr;
		TArray<FColor> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		int32 VisibleMinX = 0;
		int32 VisibleMinY = 0;
		int32 VisibleMaxX = 0;
		int32 VisibleMaxY = 0;
		int32 DrawPriority = 0;
		int32 SourceOrder = 0;
		bool bSRGB = true;
	};

	TArray<FAtlasSource> AtlasSources;
	int32 CellWidth = 0;
	int32 CellHeight = 0;
	bool bAtlasSRGB = true;

#if WITH_EDITOR
	for (int32 RowIndex = 0; RowIndex < TattooRows.Num(); ++RowIndex)
	{
		const FName RowName = RowNames.IsValidIndex(RowIndex) ? RowNames[RowIndex] : NAME_None;
		const FProjectAutomaticTattooTableRow* TattooRow = TattooRows[RowIndex];
		UTexture2D* SourceTexture = ResolveTattooTexture(TattooRow);
		if (!IsValid(SourceTexture) || !SourceTexture->Source.IsValid() || SourceTexture->Source.GetFormat() != TSF_BGRA8)
		{
			continue;
		}

		TArray64<uint8> RawMipData;
		if (!SourceTexture->Source.GetMipData(RawMipData, 0) || RawMipData.Num() <= 0)
		{
			continue;
		}

		const int32 Width = static_cast<int32>(SourceTexture->Source.GetSizeX());
		const int32 Height = static_cast<int32>(SourceTexture->Source.GetSizeY());
		const int64 PixelCount = static_cast<int64>(Width) * static_cast<int64>(Height);
		if (Width <= 0 || Height <= 0 || RawMipData.Num() < PixelCount * static_cast<int64>(sizeof(FColor)))
		{
			continue;
		}

		const FColor* SourcePixels = reinterpret_cast<const FColor*>(RawMipData.GetData());
		const FColor BackgroundColor = ProjectDefaultTattooSkinnedDecalPrivate::AverageEdgeColor(SourcePixels, Width, Height);
		FColor MinColor(255, 255, 255, 255);
		FColor MaxColor(0, 0, 0, 0);
		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			const FColor& Pixel = SourcePixels[PixelIndex];
			MinColor.A = FMath::Min(MinColor.A, Pixel.A);
			MaxColor.A = FMath::Max(MaxColor.A, Pixel.A);
		}
		const bool bSourceAlphaHasMask = MinColor.A < 250 && MaxColor.A > 8;

		FAtlasSource AtlasSource;
		AtlasSource.RowName = RowName;
		AtlasSource.CompositeGroupKey = ProjectDefaultTattooSkinnedDecalPrivate::BuildTattooPlacementGroupKey(
			TattooRow,
			TattooRow ? TattooRow->AnchorBone : NAME_None);
		if (RowName == ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName)
		{
			AtlasSource.CompositeGroupKey = RowName.ToString();
		}
		if (AtlasSource.CompositeGroupKey.IsEmpty())
		{
			AtlasSource.CompositeGroupKey = RowName.ToString();
		}
		AtlasSource.Texture = SourceTexture;
		AtlasSource.Width = Width;
		AtlasSource.Height = Height;
		AtlasSource.DrawPriority = TattooRow && TattooRow->UnlockRule == EProjectAutomaticTattooUnlockRule::RuntimeReward ? 100 : 0;
		AtlasSource.SourceOrder = RowIndex;
		AtlasSource.bSRGB = SourceTexture->SRGB;
		AtlasSource.Pixels.SetNumUninitialized(Width * Height);
		AtlasSource.VisibleMinX = Width;
		AtlasSource.VisibleMinY = Height;
		AtlasSource.VisibleMaxX = -1;
		AtlasSource.VisibleMaxY = -1;

		int32 VisiblePixelCount = 0;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 PixelIndex = Y * Width + X;
				FColor Pixel = SourcePixels[PixelIndex];
				if (const FProjectTattooParameters* ManualParameters = ManualTattooParametersByRow.Find(RowName))
				{
					const uint8 OriginalAlpha = Pixel.A;
					if (ManualParameters->bUseTint)
					{
						const FLinearColor SourceLinear = Pixel.ReinterpretAsLinear();
						FLinearColor Tinted = SourceLinear * ManualParameters->Color;
						Tinted.A = static_cast<float>(OriginalAlpha) / 255.0f;
						Pixel = Tinted.ToFColor(true);
					}
				}
				const uint8 DerivedAlpha = ProjectDefaultTattooSkinnedDecalPrivate::DeriveTattooAlpha(Pixel, BackgroundColor);
				const float ManualOpacity = ManualTattooParametersByRow.Contains(RowName)
					? FMath::Clamp(ManualTattooParametersByRow.FindChecked(RowName).Opacity, 0.0f, 1.0f)
					: 1.0f;
				Pixel.A = static_cast<uint8>(FMath::RoundToInt(
					static_cast<float>(bSourceAlphaHasMask ? Pixel.A : DerivedAlpha) * ManualOpacity));
				if (Pixel.A > 8)
				{
					++VisiblePixelCount;
					AtlasSource.VisibleMinX = FMath::Min(AtlasSource.VisibleMinX, X);
					AtlasSource.VisibleMinY = FMath::Min(AtlasSource.VisibleMinY, Y);
					AtlasSource.VisibleMaxX = FMath::Max(AtlasSource.VisibleMaxX, X);
					AtlasSource.VisibleMaxY = FMath::Max(AtlasSource.VisibleMaxY, Y);
				}
				AtlasSource.Pixels[PixelIndex] = Pixel;
			}
		}

		if (VisiblePixelCount <= 0)
		{
			continue;
		}

		const int32 VisibleWidth = FMath::Max(1, AtlasSource.VisibleMaxX - AtlasSource.VisibleMinX + 1);
		const int32 VisibleHeight = FMath::Max(1, AtlasSource.VisibleMaxY - AtlasSource.VisibleMinY + 1);
		CellWidth = FMath::Max(CellWidth, VisibleWidth);
		CellHeight = FMath::Max(CellHeight, VisibleHeight);
		bAtlasSRGB = bAtlasSRGB || SourceTexture->SRGB;
		AtlasSources.Add(MoveTemp(AtlasSource));
	}
#endif

	if (AtlasSources.IsEmpty())
	{
		for (int32 RowIndex = 0; RowIndex < TattooRows.Num(); ++RowIndex)
		{
			if (UTexture2D* SourceTexture = ResolveTattooTexture(TattooRows[RowIndex]))
			{
				const FName RowName = RowNames.IsValidIndex(RowIndex) ? RowNames[RowIndex] : NAME_None;
				OutSubUVByRow.Add(RowName, 0);
				OutCompositeSourceCountByRow.Add(RowName, 1);
				FString CompositeGroupKey = ProjectDefaultTattooSkinnedDecalPrivate::BuildTattooPlacementGroupKey(
					TattooRows[RowIndex],
					TattooRows[RowIndex] ? TattooRows[RowIndex]->AnchorBone : NAME_None);
				if (RowName == ProjectDefaultTattooSkinnedDecalPrivate::TattooShopPreviewAtlasRowName || CompositeGroupKey.IsEmpty())
				{
					CompositeGroupKey = RowName.ToString();
				}
				OutCompositeGroupKeyByRow.Add(RowName, CompositeGroupKey);
				OutSubImagesX = 1;
				OutSubImagesY = 1;
				return CreateMaskedTattooTexture(SourceTexture);
			}
		}
		return nullptr;
	}

	CellWidth = FMath::Max(64, FMath::Max(CellWidth, CellHeight));
	CellHeight = CellWidth;

	struct FAtlasCell
	{
		FString CompositeGroupKey;
		TArray<int32> SourceIndices;
	};

	TArray<FAtlasCell> AtlasCells;
	TMap<FString, int32> AtlasCellIndexByGroupKey;
	for (int32 SourceIndex = 0; SourceIndex < AtlasSources.Num(); ++SourceIndex)
	{
		const FString& CompositeGroupKey = AtlasSources[SourceIndex].CompositeGroupKey;
		int32* ExistingCellIndex = AtlasCellIndexByGroupKey.Find(CompositeGroupKey);
		if (!ExistingCellIndex)
		{
			ExistingCellIndex = &AtlasCellIndexByGroupKey.Add(CompositeGroupKey, AtlasCells.Num());
			FAtlasCell& NewCell = AtlasCells.AddDefaulted_GetRef();
			NewCell.CompositeGroupKey = CompositeGroupKey;
		}
		AtlasCells[*ExistingCellIndex].SourceIndices.Add(SourceIndex);
	}

	for (FAtlasCell& AtlasCell : AtlasCells)
	{
		AtlasCell.SourceIndices.Sort([&AtlasSources](const int32 LeftIndex, const int32 RightIndex)
		{
			const FAtlasSource& Left = AtlasSources[LeftIndex];
			const FAtlasSource& Right = AtlasSources[RightIndex];
			if (Left.DrawPriority != Right.DrawPriority)
			{
				return Left.DrawPriority < Right.DrawPriority;
			}
			return Left.SourceOrder < Right.SourceOrder;
		});
	}

	OutSubImagesX = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(AtlasCells.Num()))));
	OutSubImagesY = FMath::Max(1, FMath::CeilToInt(static_cast<float>(AtlasCells.Num()) / static_cast<float>(OutSubImagesX)));
	const int32 AtlasWidth = CellWidth * OutSubImagesX;
	const int32 AtlasHeight = CellHeight * OutSubImagesY;

	TArray<FColor> AtlasPixels;
	AtlasPixels.Init(FColor(0, 0, 0, 0), AtlasWidth * AtlasHeight);

	for (int32 AtlasIndex = 0; AtlasIndex < AtlasCells.Num(); ++AtlasIndex)
	{
		const FAtlasCell& AtlasCell = AtlasCells[AtlasIndex];
		const int32 CellX = AtlasIndex % OutSubImagesX;
		const int32 CellY = AtlasIndex / OutSubImagesX;
		for (const int32 SourceIndex : AtlasCell.SourceIndices)
		{
			const FAtlasSource& AtlasSource = AtlasSources[SourceIndex];
			const int32 VisibleWidth = FMath::Max(1, AtlasSource.VisibleMaxX - AtlasSource.VisibleMinX + 1);
			const int32 VisibleHeight = FMath::Max(1, AtlasSource.VisibleMaxY - AtlasSource.VisibleMinY + 1);
			float FitScale = static_cast<float>(CellHeight) * 0.86f / static_cast<float>(VisibleHeight);
			if (static_cast<float>(VisibleWidth) * FitScale > static_cast<float>(CellWidth) * 0.86f)
			{
				FitScale = static_cast<float>(CellWidth) * 0.86f / static_cast<float>(VisibleWidth);
			}
			const int32 TargetWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(VisibleWidth) * FitScale));
			const int32 TargetHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(VisibleHeight) * FitScale));
			const int32 StartX = CellX * CellWidth + (CellWidth - TargetWidth) / 2;
			const int32 StartY = CellY * CellHeight + (CellHeight - TargetHeight) / 2;

			for (int32 Y = 0; Y < TargetHeight; ++Y)
			{
				const float SourceYFloat = static_cast<float>(AtlasSource.VisibleMinY)
					+ ((static_cast<float>(Y) + 0.5f) / static_cast<float>(TargetHeight)) * static_cast<float>(VisibleHeight);
				const int32 SourceY = FMath::Clamp(FMath::FloorToInt(SourceYFloat), AtlasSource.VisibleMinY, AtlasSource.VisibleMaxY);
				for (int32 X = 0; X < TargetWidth; ++X)
				{
					const float SourceXFloat = static_cast<float>(AtlasSource.VisibleMinX)
						+ ((static_cast<float>(X) + 0.5f) / static_cast<float>(TargetWidth)) * static_cast<float>(VisibleWidth);
					const int32 SourceX = FMath::Clamp(FMath::FloorToInt(SourceXFloat), AtlasSource.VisibleMinX, AtlasSource.VisibleMaxX);
					const int32 SourcePixelIndex = SourceY * AtlasSource.Width + SourceX;
					const int32 AtlasPixelIndex = (StartY + Y) * AtlasWidth + StartX + X;
					if (AtlasPixels.IsValidIndex(AtlasPixelIndex) && AtlasSource.Pixels.IsValidIndex(SourcePixelIndex))
					{
						AtlasPixels[AtlasPixelIndex] = ProjectDefaultTattooSkinnedDecalPrivate::AlphaCompositeOver(
							AtlasPixels[AtlasPixelIndex],
							AtlasSource.Pixels[SourcePixelIndex]);
					}
				}
			}
		}

		for (const int32 SourceIndex : AtlasCell.SourceIndices)
		{
			const FAtlasSource& AtlasSource = AtlasSources[SourceIndex];
			OutSubUVByRow.Add(AtlasSource.RowName, AtlasIndex);
			OutCompositeSourceCountByRow.Add(AtlasSource.RowName, AtlasCell.SourceIndices.Num());
			OutCompositeGroupKeyByRow.Add(AtlasSource.RowName, AtlasCell.CompositeGroupKey);
		}
	}

	RuntimeAutomaticTattooAtlasTexture = ProjectDefaultTattooSkinnedDecalPrivate::CreateTransientTextureFromPixels(
		this,
		TEXT("ProjectAutomaticTattooAtlas"),
		AtlasWidth,
		AtlasHeight,
		AtlasPixels,
		bAtlasSRGB);

	return RuntimeAutomaticTattooAtlasTexture.Get();
#endif
}

UTexture2D* UProjectDefaultTattooSkinnedDecalSubsystem::ResolveTattooTexture(
	const FProjectAutomaticTattooTableRow* TattooRow) const
{
	if (TattooRow && !TattooRow->TattooTexture.IsNull())
	{
		if (UTexture2D* RowTexture = TattooRow->TattooTexture.LoadSynchronous())
		{
			return RowTexture;
		}
	}

	return nullptr;
}

UTexture2D* UProjectDefaultTattooSkinnedDecalSubsystem::CreateMaskedTattooTexture(UTexture2D* SourceTexture)
{
	if (!IsValid(SourceTexture))
	{
		return nullptr;
	}

#if WITH_EDITOR
	if (!SourceTexture->Source.IsValid() || SourceTexture->Source.GetFormat() != TSF_BGRA8)
	{
		return SourceTexture;
	}

	TArray64<uint8> RawMipData;
	if (!SourceTexture->Source.GetMipData(RawMipData, 0) || RawMipData.Num() <= 0)
	{
		return SourceTexture;
	}

	const int32 Width = static_cast<int32>(SourceTexture->Source.GetSizeX());
	const int32 Height = static_cast<int32>(SourceTexture->Source.GetSizeY());
	const int64 PixelCount = static_cast<int64>(Width) * static_cast<int64>(Height);
	if (Width <= 0 || Height <= 0 || RawMipData.Num() < PixelCount * static_cast<int64>(sizeof(FColor)))
	{
		return SourceTexture;
	}

	const FColor* SourcePixels = reinterpret_cast<const FColor*>(RawMipData.GetData());
	const FColor BackgroundColor = ProjectDefaultTattooSkinnedDecalPrivate::AverageEdgeColor(SourcePixels, Width, Height);
	const bool bTextureReportsAlpha = SourceTexture->HasAlphaChannel();
	FColor MinColor(255, 255, 255, 255);
	FColor MaxColor(0, 0, 0, 0);
	int32 MaxObservedDelta = 0;
	for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		const FColor& Pixel = SourcePixels[PixelIndex];
		MaxObservedDelta = FMath::Max(
			MaxObservedDelta,
			FMath::Max3(
				FMath::Abs(static_cast<int32>(Pixel.R) - static_cast<int32>(BackgroundColor.R)),
				FMath::Abs(static_cast<int32>(Pixel.G) - static_cast<int32>(BackgroundColor.G)),
				FMath::Abs(static_cast<int32>(Pixel.B) - static_cast<int32>(BackgroundColor.B))));
		MinColor.R = FMath::Min(MinColor.R, Pixel.R);
		MinColor.G = FMath::Min(MinColor.G, Pixel.G);
		MinColor.B = FMath::Min(MinColor.B, Pixel.B);
		MinColor.A = FMath::Min(MinColor.A, Pixel.A);
		MaxColor.R = FMath::Max(MaxColor.R, Pixel.R);
		MaxColor.G = FMath::Max(MaxColor.G, Pixel.G);
		MaxColor.B = FMath::Max(MaxColor.B, Pixel.B);
		MaxColor.A = FMath::Max(MaxColor.A, Pixel.A);
	}

	const bool bSourceAlphaHasMask = MinColor.A < 250 && MaxColor.A > 8;

	TArray<FColor> MaskedPixels;
	MaskedPixels.SetNumUninitialized(Width * Height);

	int32 VisiblePixelCount = 0;
	for (int32 PixelIndex = 0; PixelIndex < MaskedPixels.Num(); ++PixelIndex)
	{
		FColor Pixel = SourcePixels[PixelIndex];
		const uint8 DerivedAlpha = ProjectDefaultTattooSkinnedDecalPrivate::DeriveTattooAlpha(Pixel, BackgroundColor);
		Pixel.A = bSourceAlphaHasMask ? Pixel.A : DerivedAlpha;
		if (Pixel.A > 8)
		{
			++VisiblePixelCount;
		}
		MaskedPixels[PixelIndex] = Pixel;
	}

	if (VisiblePixelCount <= 0)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Warning,
			TEXT("[TattooSkinnedDecal] Could not derive visible alpha from %s; using original texture. background=%s min=%s max=%s maxDelta=%d textureReportsAlpha=%d sourceAlphaMask=%d sourceFormat=%d rawBytes=%lld."),
			*GetNameSafe(SourceTexture),
			*BackgroundColor.ToString(),
			*MinColor.ToString(),
			*MaxColor.ToString(),
			MaxObservedDelta,
			bTextureReportsAlpha ? 1 : 0,
			bSourceAlphaHasMask ? 1 : 0,
			static_cast<int32>(SourceTexture->Source.GetFormat()),
			RawMipData.Num());
		return SourceTexture;
	}

	RuntimeMaskedTattooTexture = ProjectDefaultTattooSkinnedDecalPrivate::CreateTransientTextureFromPixels(
		this,
		TEXT("ProjectDefaultTattooMaskedTexture"),
		Width,
		Height,
		MaskedPixels,
		SourceTexture->SRGB);

	if (RuntimeMaskedTattooTexture)
	{
		UE_LOG(
			LogProjectDefaultTattooSkinnedDecal,
			Display,
			TEXT("[TattooSkinnedDecal] Built masked tattoo texture %s %dx%d visibleCoverage=%.1f%% background=%s."),
			*GetNameSafe(SourceTexture),
			Width,
			Height,
			100.0f * static_cast<float>(VisiblePixelCount) / static_cast<float>(MaskedPixels.Num()),
			*BackgroundColor.ToString());
	}

	return RuntimeMaskedTattooTexture ? RuntimeMaskedTattooTexture.Get() : SourceTexture;
#else
	return SourceTexture;
#endif
}

FName UProjectDefaultTattooSkinnedDecalSubsystem::ResolveAnchorBone(const USkeletalMeshComponent* TargetMesh) const
{
	if (!IsValid(TargetMesh))
	{
		return NAME_None;
	}

	const FName CandidateBones[] = {
		TEXT("spine_04"),
		TEXT("spine_03"),
		TEXT("spine_05"),
		TEXT("spine_02"),
		TEXT("chest"),
		TEXT("upperchest"),
		TEXT("abdomenUpper"),
		TEXT("abdomen2"),
		TEXT("spine2"),
		TEXT("spine1"),
		TEXT("pelvis")
	};

	for (const FName CandidateBone : CandidateBones)
	{
		if (TargetMesh->GetBoneIndex(CandidateBone) != INDEX_NONE)
		{
			return CandidateBone;
		}
	}

	return NAME_None;
}

FVector UProjectDefaultTattooSkinnedDecalSubsystem::ComputeTattooLocation(
	const APawn* Pawn,
	const USkeletalMeshComponent* TargetMesh,
	const FName AnchorBone,
	const FProjectAutomaticTattooTableRow* TattooRow) const
{
	if (IsValid(TargetMesh) && !AnchorBone.IsNone() && TargetMesh->GetBoneIndex(AnchorBone) != INDEX_NONE)
	{
		const FVector FrontNormal = IsValid(Pawn) ? Pawn->GetActorForwardVector().GetSafeNormal() : TargetMesh->GetForwardVector().GetSafeNormal();
		const FVector UpVector = IsValid(Pawn) ? Pawn->GetActorUpVector().GetSafeNormal() : TargetMesh->GetUpVector().GetSafeNormal();
		const FVector RightVector = IsValid(Pawn) ? Pawn->GetActorRightVector().GetSafeNormal() : TargetMesh->GetRightVector().GetSafeNormal();
		const float OffsetX = TattooRow ? TattooRow->OffsetX : 0.0f;
		const float OffsetY = TattooRow ? TattooRow->OffsetY : 8.0f;
		const float ProjectionDistance = TattooRow ? TattooRow->ProjectionDistance : ProjectDefaultTattooSkinnedDecalPrivate::ProjectionDistance;
		return TargetMesh->GetSocketLocation(AnchorBone)
			+ FrontNormal * ProjectionDistance
			+ RightVector * OffsetX
			+ UpVector * OffsetY;
	}

	if (IsValid(TargetMesh) && IsValid(TargetMesh->GetSkinnedAsset()))
	{
		const FBoxSphereBounds LocalBounds = TargetMesh->GetSkinnedAsset()->GetBounds();
		const FTransform MeshTransform = TargetMesh->GetComponentTransform();
		const FVector WorldFrontNormal = IsValid(Pawn) ? Pawn->GetActorForwardVector().GetSafeNormal() : TargetMesh->GetForwardVector().GetSafeNormal();
		const FVector LocalFrontNormal = MeshTransform.InverseTransformVectorNoScale(WorldFrontNormal).GetSafeNormal();
		const FVector LocalUp = MeshTransform.InverseTransformVectorNoScale(IsValid(Pawn) ? Pawn->GetActorUpVector() : TargetMesh->GetUpVector()).GetSafeNormal();
		const FVector LocalLeft = -MeshTransform.InverseTransformVectorNoScale(IsValid(Pawn) ? Pawn->GetActorRightVector() : TargetMesh->GetRightVector()).GetSafeNormal();
		const FVector LocalAbsFront = LocalFrontNormal.GetAbs();
		const FVector LocalAbsUp = LocalUp.GetAbs();
		const FVector LocalAbsLeft = LocalLeft.GetAbs();
		const float FrontExtent = LocalAbsFront.X * LocalBounds.BoxExtent.X + LocalAbsFront.Y * LocalBounds.BoxExtent.Y + LocalAbsFront.Z * LocalBounds.BoxExtent.Z;
		const float UpExtent = LocalAbsUp.X * LocalBounds.BoxExtent.X + LocalAbsUp.Y * LocalBounds.BoxExtent.Y + LocalAbsUp.Z * LocalBounds.BoxExtent.Z;
		const float LeftExtent = LocalAbsLeft.X * LocalBounds.BoxExtent.X + LocalAbsLeft.Y * LocalBounds.BoxExtent.Y + LocalAbsLeft.Z * LocalBounds.BoxExtent.Z;
		return MeshTransform.TransformPosition(
			LocalBounds.Origin
			+ LocalFrontNormal * (FrontExtent * 0.82f)
			+ LocalLeft * (LeftExtent * 0.20f)
			+ LocalUp * (UpExtent * 0.22f));
	}

	return IsValid(Pawn) ? Pawn->GetActorLocation() : FVector::ZeroVector;
}

FQuat UProjectDefaultTattooSkinnedDecalSubsystem::ComputeTattooRotation(
	const APawn* Pawn,
	const FProjectAutomaticTattooTableRow* TattooRow) const
{
	const FVector FrontNormal = IsValid(Pawn) ? Pawn->GetActorForwardVector().GetSafeNormal() : FVector::ForwardVector;
	const FVector UpVector = IsValid(Pawn) ? Pawn->GetActorUpVector().GetSafeNormal() : FVector::UpVector;
	const FQuat BaseRotation = FRotationMatrix::MakeFromXZ(FrontNormal, UpVector).ToQuat();
	const float RotationDegrees = TattooRow ? TattooRow->RotationDegrees : 0.0f;
	return FQuat(FrontNormal, FMath::DegreesToRadians(RotationDegrees)) * BaseRotation;
}
