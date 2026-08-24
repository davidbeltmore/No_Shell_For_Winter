#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace ProjectTattooSkinnedDecalRendererTestsPrivate
{
	constexpr double StepTimeoutSeconds = 30.0;
	constexpr int32 TextureWidth = 64;
	constexpr int32 TextureHeight = 32;

	UTexture2D* CreateTestTexture(UObject* Outer, const TCHAR* Name, const bool bTransparentPattern)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(
			TextureWidth,
			TextureHeight,
			PF_B8G8R8A8,
			MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), FName(Name)));
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
		{
			return nullptr;
		}

		Texture->NeverStream = true;
		Texture->SRGB = true;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(TextureWidth * TextureHeight);
		for (int32 Y = 0; Y < TextureHeight; ++Y)
		{
			for (int32 X = 0; X < TextureWidth; ++X)
			{
				const bool bVisible = !bTransparentPattern || (X >= 8 && X < 32 && Y >= 4 && Y < 28);
				Pixels[Y * TextureWidth + X] = bTransparentPattern
					? FColor(45, 190, 235, bVisible ? 255 : 0)
					: FColor(230, 70, 35, 255);
			}
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
		if (!Destination)
		{
			Mip.BulkData.Unlock();
			return nullptr;
		}
		FMemory::Memcpy(Destination, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	FProjectTattooRecord MakeRecord(
		const FGuid& TattooId,
		const int32 LayerOrder,
		const float OffsetX,
		const float OffsetY)
	{
		FProjectTattooRecord Record;
		Record.TattooId = TattooId;
		Record.Parameters.PlacementPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;
		Record.Parameters.OffsetX = OffsetX;
		Record.Parameters.OffsetY = OffsetY;
		Record.Parameters.Size = 12.0f;
		Record.Parameters.ProjectionDistance = 12.0f;
		Record.Parameters.LayerOrder = LayerOrder;
		Record.Parameters.bUseTint = false;
		Record.Parameters.Color = FLinearColor::Black; // Must be ignored while tint is off.
		Record.Parameters.Opacity = 1.0f;
		Record.Parameters.bEnabled = true;
		return Record;
	}

	enum class ERendererTestStep : uint8
	{
		WaitForPIE,
		Setup,
		WaitForInitialComposite,
		EditSecond,
		WaitForEditedComposite,
		Done
	};

	class FRunManualTattooRendererIsolationCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FRunManualTattooRendererIsolationCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
		}

		virtual bool Update() override
		{
			switch (Step)
			{
			case ERendererTestStep::WaitForPIE:
				return WaitForPIE();
			case ERendererTestStep::Setup:
				return Setup();
			case ERendererTestStep::WaitForInitialComposite:
				return WaitForInitialComposite();
			case ERendererTestStep::EditSecond:
				return EditSecond();
			case ERendererTestStep::WaitForEditedComposite:
				return WaitForEditedComposite();
			case ERendererTestStep::Done:
			default:
				RestoreAutomaticTattooDebugState();
				return true;
			}
		}

	private:
		void Advance(const ERendererTestStep NewStep)
		{
			Step = NewStep;
			StepStartedAt = FPlatformTime::Seconds();
		}

		bool FailIfTimedOut(const TCHAR* Message)
		{
			if (FPlatformTime::Seconds() - StepStartedAt < StepTimeoutSeconds)
			{
				return false;
			}
			Test->AddError(Message);
			Advance(ERendererTestStep::Done);
			return false;
		}

		bool WaitForPIE()
		{
			if (GEditor && GEditor->PlayWorld)
			{
				World = GEditor->PlayWorld;
				Advance(ERendererTestStep::Setup);
				return false;
			}
			return FailIfTimedOut(TEXT("PIE world did not start for the tattoo renderer test."));
		}

		bool Setup()
		{
			UWorld* TestWorld = World.Get();
			APlayerController* Controller = TestWorld ? TestWorld->GetFirstPlayerController() : nullptr;
			USkeletalMesh* FemaleMesh = LoadObject<USkeletalMesh>(
				nullptr,
				TEXT("/Game/DazToUnreal/Female/Female.Female"));
			Renderer = TestWorld ? TestWorld->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
			if (!TestWorld || !Controller || !FemaleMesh || !Renderer.IsValid())
			{
				return FailIfTimedOut(TEXT("Tattoo renderer fixture could not resolve its world, controller, Female mesh, or subsystem."));
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = TestWorld->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform::Identity, SpawnParameters);
			if (!Character.IsValid() || !Character->GetMesh())
			{
				Test->AddError(TEXT("Tattoo renderer fixture could not spawn its transient character."));
				Advance(ERendererTestStep::Done);
				return false;
			}

			Character->GetMesh()->SetSkeletalMeshAsset(FemaleMesh);
			Character->GetMesh()->SetVisibility(true, true);
			Character->GetMesh()->SetHiddenInGame(false, true);
			Controller->Possess(Character.Get());

			OpaqueTexture.Reset(CreateTestTexture(Renderer.Get(), TEXT("TattooOpaqueCard"), false));
			TransparentTexture.Reset(CreateTestTexture(Renderer.Get(), TEXT("TattooTransparentArt"), true));
			if (!OpaqueTexture.IsValid() || !TransparentTexture.IsValid())
			{
				Test->AddError(TEXT("Tattoo renderer fixture could not create runtime textures."));
				Advance(ERendererTestStep::Done);
				return false;
			}

			FirstId = FGuid::NewGuid();
			SecondId = FGuid::NewGuid();
			Records = {
				MakeRecord(FirstId, 10, -3.0f, 12.0f),
				MakeRecord(SecondId, 20, 3.0f, 4.0f)
			};
			Textures.Add(FirstId, OpaqueTexture.Get());
			Textures.Add(SecondId, TransparentTexture.Get());

			TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> AutomaticSnapshots;
			Renderer->GetAutomaticTattooRuntimeDebugSnapshots(AutomaticSnapshots);
			if (!AutomaticSnapshots.IsEmpty())
			{
				ForcedAutomaticRow = AutomaticSnapshots[0].RowName;
				AutomaticStateBeforeTest = Renderer->CaptureAutomaticTattooRuntimeDebugState(ForcedAutomaticRow);
				bHasAutomaticState = true;
				Renderer->SetAutomaticTattooRuntimeDebugForcedActive(Character.Get(), ForcedAutomaticRow, true);
			}

			FlushRenderingCommands();
			Renderer->SynchronizeManualTattoos(Character.Get(), Records, Textures);
			Advance(ERendererTestStep::WaitForInitialComposite);
			return false;
		}

		bool WaitForInitialComposite()
		{
			FlushRenderingCommands();
			FProjectManualTattooRuntimeDebugSnapshot First;
			FProjectManualTattooRuntimeDebugSnapshot Second;
			if (!Renderer.IsValid()
				|| !Renderer->GetManualTattooRuntimeDebugSnapshot(FirstId, First)
				|| !Renderer->GetManualTattooRuntimeDebugSnapshot(SecondId, Second)
				|| First.DecalIndex == INDEX_NONE
				|| Second.DecalIndex == INDEX_NONE
				|| First.AtlasCellPixelHash.IsEmpty()
				|| Second.AtlasCellPixelHash.IsEmpty())
			{
				if (Renderer.IsValid())
				{
					Renderer->SynchronizeManualTattoos(Character.Get(), Records, Textures);
				}
				return FailIfTimedOut(TEXT("Two manual tattoos never reached the SkinnedDecal atlas and sampler."));
			}

			Test->TestNotEqual(TEXT("Two GUIDs own distinct SkinnedDecal indices"), First.DecalIndex, Second.DecalIndex);
			Test->TestNotEqual(TEXT("Two GUIDs own distinct atlas SubUV cells"), First.SubUV, Second.SubUV);
			Test->TestTrue(TEXT("First GUID owns a valid atlas cell"), First.AtlasCellRect.Area() > 0);
			Test->TestTrue(TEXT("Second GUID owns a valid atlas cell"), Second.AtlasCellRect.Area() > 0);
			Test->TestFalse(TEXT("GUID atlas cells do not overlap"), First.AtlasCellRect.Intersect(Second.AtlasCellRect));
			Test->TestNotEqual(TEXT("Different source images produce different cell hashes"), First.AtlasCellPixelHash, Second.AtlasCellPixelHash);
			Test->TestTrue(TEXT("Opaque RGB image is drawn as a bounded rectangular card"),
				First.NonTransparentPixelCount > 0
				&& First.MinimumAlpha == 0
				&& First.MaximumAlpha == 255
				&& First.VisiblePixelBounds.Area() < First.AtlasCellRect.Area());
			Test->TestTrue(TEXT("Transparent PNG alpha survives the runtime compositor"),
				Second.NonTransparentPixelCount > 0
				&& Second.NonTransparentPixelCount < First.NonTransparentPixelCount
				&& Second.MinimumAlpha == 0
				&& Second.MaximumAlpha == 255);
			Test->TestTrue(TEXT("Manual layers coexist while an automatic layer is active"),
				ForcedAutomaticRow.IsNone() || Renderer->HasActiveAutomaticTattoo(Character.Get()));

			InitialFirst = First;
			InitialSecond = Second;
			Advance(ERendererTestStep::EditSecond);
			return false;
		}

		bool EditSecond()
		{
			Records[1].Parameters.bUseTint = true;
			Records[1].Parameters.Color = FLinearColor(0.1f, 0.9f, 0.25f, 1.0f);
			Records[1].Parameters.Opacity = 0.25f;
			Renderer->SynchronizeManualTattoos(Character.Get(), Records, Textures);
			Advance(ERendererTestStep::WaitForEditedComposite);
			return false;
		}

		bool WaitForEditedComposite()
		{
			FlushRenderingCommands();
			FProjectManualTattooRuntimeDebugSnapshot First;
			FProjectManualTattooRuntimeDebugSnapshot Second;
			if (!Renderer.IsValid()
				|| !Renderer->GetManualTattooRuntimeDebugSnapshot(FirstId, First)
				|| !Renderer->GetManualTattooRuntimeDebugSnapshot(SecondId, Second)
				|| Second.StateHash == InitialSecond.StateHash)
			{
				return FailIfTimedOut(TEXT("The edited second tattoo never produced a new compositor state."));
			}

			Test->TestEqual(TEXT("Editing second preserves first state hash"), First.StateHash, InitialFirst.StateHash);
			Test->TestEqual(TEXT("Editing second preserves first atlas cell pixels"), First.AtlasCellPixelHash, InitialFirst.AtlasCellPixelHash);
			Test->TestNotEqual(TEXT("Editing tint/opacity changes second state hash"), Second.StateHash, InitialSecond.StateHash);
			Test->TestNotEqual(TEXT("Editing tint/opacity changes only second cell pixels"), Second.AtlasCellPixelHash, InitialSecond.AtlasCellPixelHash);
			Test->TestEqual(TEXT("First decal index is stable across second edit"), First.DecalIndex, InitialFirst.DecalIndex);
			Test->TestEqual(TEXT("Second decal index is stable across its edit"), Second.DecalIndex, InitialSecond.DecalIndex);
			Test->TestEqual(TEXT("First SubUV is stable across second edit"), First.SubUV, InitialFirst.SubUV);
			Test->TestEqual(TEXT("Second SubUV is stable across its edit"), Second.SubUV, InitialSecond.SubUV);
			Advance(ERendererTestStep::Done);
			return false;
		}

		void RestoreAutomaticTattooDebugState()
		{
			if (bHasAutomaticState && Renderer.IsValid())
			{
				Renderer->RestoreAutomaticTattooRuntimeDebugState(
					Character.Get(),
					ForcedAutomaticRow,
					AutomaticStateBeforeTest);
				bHasAutomaticState = false;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		ERendererTestStep Step = ERendererTestStep::WaitForPIE;
		double StepStartedAt = FPlatformTime::Seconds();
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ACharacter> Character;
		TWeakObjectPtr<UProjectDefaultTattooSkinnedDecalSubsystem> Renderer;
		TStrongObjectPtr<UTexture2D> OpaqueTexture;
		TStrongObjectPtr<UTexture2D> TransparentTexture;
		FGuid FirstId;
		FGuid SecondId;
		TArray<FProjectTattooRecord> Records;
		TMap<FGuid, UTexture2D*> Textures;
		FProjectManualTattooRuntimeDebugSnapshot InitialFirst;
		FProjectManualTattooRuntimeDebugSnapshot InitialSecond;
		FName ForcedAutomaticRow = NAME_None;
		FProjectAutomaticTattooRuntimeDebugState AutomaticStateBeforeTest;
		bool bHasAutomaticState = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectTattooSkinnedDecalRendererIsolationPIETest,
	"NoShellForWinter.TattooShop.SkinnedDecal.PIE.ManualLayerIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectTattooSkinnedDecalRendererIsolationPIETest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAutomationEditorCommonUtils::CreateNewMap();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(
		ProjectTattooSkinnedDecalRendererTestsPrivate::FRunManualTattooRendererIsolationCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
