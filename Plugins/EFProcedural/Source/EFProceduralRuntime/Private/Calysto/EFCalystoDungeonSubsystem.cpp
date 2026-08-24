#include "Calysto/EFCalystoDungeonSubsystem.h"

#include "Calysto/EFCalystoDungeonDirectorPolicy.h"
#include "Calysto/EFCalystoDungeonDirectorMathV4.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Calysto/EFCalystoPopulationBridgeV4.h"
#include "Containers/StringConv.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProperties.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Templates/UnrealTemplate.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoDungeon, Log, All);

#if !UE_BUILD_SHIPPING
namespace EFCalystoDungeonAutomationPrivate
{
	const FName PopulationScenarioZero(TEXT("Zero"));
	const FName PopulationScenarioEnemyCap25(TEXT("EnemyCap25"));
	const FName PopulationScenarioResourceMax(TEXT("ResourceMax"));
	const FName PopulationScenarioResourceMin(TEXT("ResourceMin"));
	const FName PopulationScenarioNPCGeneralistFemale(TEXT("NPCGeneralistFemale"));
	const FName PopulationScenarioNPCGeneralistMale(TEXT("NPCGeneralistMale"));
	const FName PopulationScenarioNPCMeleeFemale(TEXT("NPCMeleeFemale"));
	const FName PopulationScenarioNPCMeleeMale(TEXT("NPCMeleeMale"));
	const FName PopulationScenarioNPCRangedFemale(TEXT("NPCRangedFemale"));
	const FName PopulationScenarioNPCRangedMale(TEXT("NPCRangedMale"));
	const FName PopulationScenarioNPCTotal4(TEXT("NPCTotal4"));
	const FName PopulationScenarioSpecialEvents6(TEXT("SpecialEvents6"));
	const FName PopulationScenarioCompanionRecallLifecycle(TEXT("CompanionRecallLifecycle"));

	static bool TryGetNPCVariantCatalogId(const FName Scenario, FName& OutCatalogId)
	{
		OutCatalogId = NAME_None;
		if (Scenario == PopulationScenarioNPCGeneralistFemale)
		{
			OutCatalogId = TEXT("NPC.Companion.Generalist.Female");
		}
		else if (Scenario == PopulationScenarioNPCGeneralistMale)
		{
			OutCatalogId = TEXT("NPC.Companion.Generalist.Male");
		}
		else if (Scenario == PopulationScenarioNPCMeleeFemale)
		{
			OutCatalogId = TEXT("NPC.Companion.Melee.Female");
		}
		else if (Scenario == PopulationScenarioNPCMeleeMale)
		{
			OutCatalogId = TEXT("NPC.Companion.Melee.Male");
		}
		else if (Scenario == PopulationScenarioNPCRangedFemale)
		{
			OutCatalogId = TEXT("NPC.Companion.Ranged.Female");
		}
		else if (Scenario == PopulationScenarioNPCRangedMale)
		{
			OutCatalogId = TEXT("NPC.Companion.Ranged.Male");
		}
		return !OutCatalogId.IsNone();
	}

	static bool IsKnownPopulationScenario(const FName Scenario)
	{
		FName NPCVariantId;
		return Scenario == PopulationScenarioZero
			|| Scenario == PopulationScenarioEnemyCap25
			|| Scenario == PopulationScenarioResourceMax
			|| Scenario == PopulationScenarioResourceMin
			|| Scenario == PopulationScenarioNPCTotal4
			|| Scenario == PopulationScenarioSpecialEvents6
			|| Scenario == PopulationScenarioCompanionRecallLifecycle
			|| TryGetNPCVariantCatalogId(Scenario, NPCVariantId);
	}

	static bool IsPackagedGameAcceptanceWorld(const UWorld* World)
	{
		return FApp::IsUnattended()
			&& FPlatformProperties::RequiresCookedData()
			&& IsValid(World)
			&& World->WorldType == EWorldType::Game
			&& World->GetNetMode() == NM_Standalone
			&& FParse::Param(FCommandLine::Get(), TEXT("CalystoV4PackagedSmoke"));
	}

	static FString JoinDungeonSizes(const TArray<int32>& Sizes)
	{
		TArray<FString> Values;
		Values.Reserve(Sizes.Num());
		for (const int32 Size : Sizes)
		{
			Values.Add(LexToString(Size));
		}
		return FString::Join(Values, TEXT(","));
	}

	static void SetCandidateValidatedSizes(const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
		UEFCalystoDungeonSubsystem* Subsystem = GameInstance
			? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
			: nullptr;
		if (!Subsystem || Args.IsEmpty())
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("EF.Calysto.Automation.SetCandidateValidatedSizes requires sizes and a live PIE GameInstance."));
			return;
		}

		if (Args.Num() == 1 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			FString ClearError;
			if (!Subsystem->ClearDevelopmentCandidateValidatedDungeonSizesForAutomation(ClearError))
			{
				UE_LOG(LogEFCalystoDungeon, Error,
					TEXT("Development automation could not clear the candidate policy: %s"), *ClearError);
			}
			return;
		}

		TArray<int32> CandidateSizes;
		for (const FString& Argument : Args)
		{
			TArray<FString> Tokens;
			Argument.ParseIntoArray(Tokens, TEXT(","), true);
			for (FString& Token : Tokens)
			{
				Token.TrimStartAndEndInline();
				int32 Size = 0;
				if (!LexTryParseString(Size, *Token))
				{
					UE_LOG(LogEFCalystoDungeon, Error,
						TEXT("Development automation rejected candidate dungeon size '%s'."), *Token);
					return;
				}
				CandidateSizes.Add(Size);
			}
		}

		FString CandidatePolicyHash;
		FString CandidateError;
		if (!Subsystem->SetDevelopmentCandidateValidatedDungeonSizesForAutomation(
				CandidateSizes, CandidatePolicyHash, CandidateError))
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("Development automation rejected candidate policy sizes: %s"), *CandidateError);
			return;
		}
		CandidateSizes.Sort();
		UE_LOG(LogEFCalystoDungeon, Log,
			TEXT("CALYSTO_CERTIFICATION_POLICY_CANDIDATE sizes=%s hash=%s transient=true"),
			*JoinDungeonSizes(CandidateSizes), *CandidatePolicyHash);
	}

	static FAutoConsoleCommandWithWorldAndArgs SetCandidateValidatedSizesCommand(
		TEXT("EF.Calysto.Automation.SetCandidateValidatedSizes"),
		TEXT("Unattended PIE certification only. Apply comma-separated 18..30 candidate sizes to a transient policy clone, or pass clear."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetCandidateValidatedSizes));

	static void ForceDungeonEdge(const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
		UEFCalystoDungeonSubsystem* Subsystem = GameInstance
			? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
			: nullptr;
		if (!Subsystem || Args.Num() != 1)
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("EF.Calysto.Automation.ForceDungeonEdge requires one edge argument and a live PIE GameInstance."));
			return;
		}

		if (Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase) || Args[0] == TEXT("0"))
		{
			Subsystem->ClearDevelopmentForcedDungeonEdgeForAutomation();
			return;
		}

		int32 DungeonEdge = 0;
		if (!LexTryParseString(DungeonEdge, *Args[0])
			|| !Subsystem->SetDevelopmentForcedDungeonEdgeForAutomation(DungeonEdge))
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("Development automation rejected exact dungeon edge '%s'."), *Args[0]);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs ForceDungeonEdgeCommand(
		TEXT("EF.Calysto.Automation.ForceDungeonEdge"),
		TEXT("Unattended PIE certification only. Force an exact 18..30 edge, or pass clear/0."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ForceDungeonEdge));

	static void SetPopulationScenario(const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
		UEFCalystoDungeonSubsystem* Subsystem = GameInstance
			? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
			: nullptr;
		if (!Subsystem || Args.Num() != 1)
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("EF.Calysto.Automation.SetPopulationScenario requires exactly one scenario and a live PIE GameInstance."));
			return;
		}

		if (Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			FString ClearError;
			if (!Subsystem->ClearDevelopmentPopulationScenarioForAutomation(ClearError))
			{
				UE_LOG(LogEFCalystoDungeon, Error,
					TEXT("Development automation could not clear the population scenario: %s"), *ClearError);
			}
			return;
		}

		FName Scenario = NAME_None;
		if (Args[0].Equals(TEXT("Zero"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioZero;
		}
		else if (Args[0].Equals(TEXT("EnemyCap25"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioEnemyCap25;
		}
		else if (Args[0].Equals(TEXT("ResourceMax"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioResourceMax;
		}
		else if (Args[0].Equals(TEXT("ResourceMin"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioResourceMin;
		}
		else if (Args[0].Equals(TEXT("NPCGeneralistFemale"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCGeneralistFemale;
		}
		else if (Args[0].Equals(TEXT("NPCGeneralistMale"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCGeneralistMale;
		}
		else if (Args[0].Equals(TEXT("NPCMeleeFemale"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCMeleeFemale;
		}
		else if (Args[0].Equals(TEXT("NPCMeleeMale"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCMeleeMale;
		}
		else if (Args[0].Equals(TEXT("NPCRangedFemale"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCRangedFemale;
		}
		else if (Args[0].Equals(TEXT("NPCRangedMale"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCRangedMale;
		}
		else if (Args[0].Equals(TEXT("NPCTotal4"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioNPCTotal4;
		}
		else if (Args[0].Equals(TEXT("SpecialEvents6"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioSpecialEvents6;
		}
		else if (Args[0].Equals(TEXT("CompanionRecallLifecycle"), ESearchCase::IgnoreCase))
		{
			Scenario = PopulationScenarioCompanionRecallLifecycle;
		}
		else
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("Development automation rejected unknown population scenario '%s'."), *Args[0]);
			return;
		}

		FString ScenarioPolicyHash;
		FString ScenarioError;
		if (!Subsystem->SetDevelopmentPopulationScenarioForAutomation(
				Scenario, ScenarioPolicyHash, ScenarioError))
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("Development automation rejected population scenario %s: %s"),
				*Scenario.ToString(), *ScenarioError);
			return;
		}
		UE_LOG(LogEFCalystoDungeon, Log,
			TEXT("CALYSTO_POPULATION_SCENARIO scenario=%s hash=%s transient=true"),
			*Scenario.ToString(), *ScenarioPolicyHash);
	}

	static FAutoConsoleCommandWithWorldAndArgs SetPopulationScenarioCommand(
		TEXT("EF.Calysto.Automation.SetPopulationScenario"),
		TEXT("Unattended Development acceptance only. Arm an exact V4 population, NPC-variant or companion Recall lifecycle fixture, or clear its transient policy clone."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetPopulationScenario));
}
#endif

namespace EFCalystoDungeonPrivate
{
	constexpr int64 PCGSeedModulus = 2147483647LL;
	constexpr uint64 EcologyScaleDomain = 0x45434F5F5343414CULL;
	constexpr uint64 EcologyBranchDomain = 0x45434F5F4252414EULL;
	constexpr uint64 EcologyThreatDomain = 0x45434F5F54485245ULL;
	constexpr uint64 EcologyAbundanceDomain = 0x45434F5F4142554EULL;
	constexpr uint64 EcologyMysteryDomain = 0x45434F5F4D595354ULL;
	constexpr uint64 StyleDomain = 0x5354594C455F5633ULL;
	constexpr uint64 ScaleTraitDomain = 0x54524149545F5343ULL;
	constexpr uint64 BranchTraitDomain = 0x54524149545F4252ULL;
	constexpr uint64 ThreatTraitDomain = 0x54524149545F5448ULL;
	constexpr uint64 AbundanceTraitDomain = 0x54524149545F4142ULL;
	constexpr uint64 MysteryTraitDomain = 0x54524149545F4D59ULL;
	constexpr uint64 SizeDomain = 0x53495A455F585F33ULL;
	constexpr uint64 AnchorDensityDomain = 0x414E43484F525F33ULL;
	constexpr uint64 SidePathDomain = 0x534944455F504154ULL;
	constexpr uint64 EnemyPresenceDomain = 0x454E5F5052455353ULL;
	constexpr uint64 EnemyCountDomain = 0x454E5F434F554E54ULL;
	constexpr uint64 EnemyCompositionDomain = 0x454E5F434F4D5033ULL;
	constexpr uint64 ThemeDomain = 0x5448454D455F5633ULL;
	constexpr uint64 FoodPresenceDomain = 0x464F4F445F505233ULL;
	constexpr uint64 FoodCountDomain = 0x464F4F445F434E33ULL;
	constexpr uint64 FoodCompositionDomain = 0x464F4F445F434F33ULL;
	constexpr uint64 ChestPresenceDomain = 0x434853545F505233ULL;
	constexpr uint64 ChestCountDomain = 0x434853545F434E33ULL;
	constexpr uint64 ChestCompositionDomain = 0x434853545F434F33ULL;
	constexpr uint64 LootPresenceDomain = 0x4C4F4F545F505233ULL;
	constexpr uint64 LootCountDomain = 0x4C4F4F545F434E33ULL;
	constexpr uint64 LootCompositionDomain = 0x4C4F4F545F434F33ULL;
	constexpr uint64 SpecialPresenceDomain = 0x535045435F505233ULL;
	constexpr uint64 SpecialCountDomain = 0x535045435F434E33ULL;
	constexpr uint64 SpecialCompositionDomain = 0x535045435F434F33ULL;

	static uint64 Mix64(uint64 Value)
	{
		Value += 0x9E3779B97F4A7C15ULL;
		Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ULL;
		Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBULL;
		return Value ^ (Value >> 31);
	}

	static uint64 HashString64(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		uint64 Hash = 1469598103934665603ULL;
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
			Hash *= 1099511628211ULL;
		}
		return Hash;
	}

	static FString HashText(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		const int32 ByteLength = Utf8.Length();
		const int32 PaddedLength = ((ByteLength + 9 + 63) / 64) * 64;
		TArray<uint8> Message;
		Message.SetNumZeroed(PaddedLength);
		if (ByteLength > 0)
		{
			FMemory::Memcpy(Message.GetData(), Utf8.Get(), ByteLength);
		}
		Message[ByteLength] = 0x80;

		const uint64 BitLength = static_cast<uint64>(ByteLength) * 8ULL;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Message[PaddedLength - 1 - Index] = static_cast<uint8>(BitLength >> (Index * 8));
		}

		static constexpr uint32 RoundConstants[64] = {
			0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
			0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
			0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
			0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
			0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
			0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
			0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
			0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
			0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
			0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
			0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
			0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
			0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
			0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
			0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
			0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
		};

		auto RotateRight = [](const uint32 Value, const uint32 Shift)
		{
			return (Value >> Shift) | (Value << (32U - Shift));
		};

		uint32 State[8] = {
			0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
			0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
		};

		for (int32 BlockOffset = 0; BlockOffset < PaddedLength; BlockOffset += 64)
		{
			uint32 Words[64] = {};
			for (int32 Index = 0; Index < 16; ++Index)
			{
				const int32 Offset = BlockOffset + Index * 4;
				Words[Index] =
					(static_cast<uint32>(Message[Offset]) << 24U) |
					(static_cast<uint32>(Message[Offset + 1]) << 16U) |
					(static_cast<uint32>(Message[Offset + 2]) << 8U) |
					static_cast<uint32>(Message[Offset + 3]);
			}
			for (int32 Index = 16; Index < 64; ++Index)
			{
				const uint32 Sigma0 = RotateRight(Words[Index - 15], 7U)
					^ RotateRight(Words[Index - 15], 18U)
					^ (Words[Index - 15] >> 3U);
				const uint32 Sigma1 = RotateRight(Words[Index - 2], 17U)
					^ RotateRight(Words[Index - 2], 19U)
					^ (Words[Index - 2] >> 10U);
				Words[Index] = Words[Index - 16] + Sigma0 + Words[Index - 7] + Sigma1;
			}

			uint32 A = State[0];
			uint32 B = State[1];
			uint32 C = State[2];
			uint32 D = State[3];
			uint32 E = State[4];
			uint32 F = State[5];
			uint32 G = State[6];
			uint32 H = State[7];

			for (int32 Index = 0; Index < 64; ++Index)
			{
				const uint32 Sum1 = RotateRight(E, 6U) ^ RotateRight(E, 11U) ^ RotateRight(E, 25U);
				const uint32 Choice = (E & F) ^ ((~E) & G);
				const uint32 Temp1 = H + Sum1 + Choice + RoundConstants[Index] + Words[Index];
				const uint32 Sum0 = RotateRight(A, 2U) ^ RotateRight(A, 13U) ^ RotateRight(A, 22U);
				const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Temp2 = Sum0 + Majority;

				H = G;
				G = F;
				F = E;
				E = D + Temp1;
				D = C;
				C = B;
				B = A;
				A = Temp1 + Temp2;
			}

			State[0] += A;
			State[1] += B;
			State[2] += C;
			State[3] += D;
			State[4] += E;
			State[5] += F;
			State[6] += G;
			State[7] += H;
		}

		FString Result;
		Result.Reserve(64);
		for (const uint32 Word : State)
		{
			Result += FString::Printf(TEXT("%08X"), Word);
		}
		return Result;
	}

	static FString FloatBits(const float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return FString::Printf(TEXT("%08X"), Bits);
	}

	static FString NormalizeMapPackageName(const FString& PackageName)
	{
		FString ShortMapName = FPackageName::GetShortName(PackageName);
		if (ShortMapName.StartsWith(TEXT("UEDPIE_")))
		{
			TArray<FString> Parts;
			ShortMapName.ParseIntoArray(Parts, TEXT("_"), true);
			if (Parts.Num() >= 3)
			{
				Parts.RemoveAt(0, 2);
				ShortMapName = FString::Join(Parts, TEXT("_"));
			}
		}
		const FString LongPackagePath = FPackageName::GetLongPackagePath(PackageName);
		return LongPackagePath.IsEmpty() ? ShortMapName : LongPackagePath + TEXT("/") + ShortMapName;
	}

	static bool TryReadInt64Option(const UWorld* World, const TCHAR* Match, int64& OutValue)
	{
		if (!World)
		{
			return false;
		}
		const TCHAR* RawValue = World->URL.GetOption(Match, nullptr);
		return RawValue && RawValue[0] != TCHAR('\0') && LexTryParseString(OutValue, RawValue);
	}

	static int64 CreatePositiveRunSeed()
	{
		const FGuid Guid = FGuid::NewGuid();
		const uint64 Upper = (static_cast<uint64>(Guid.A) << 32) | static_cast<uint64>(Guid.B);
		const uint64 Lower = (static_cast<uint64>(Guid.C) << 32) | static_cast<uint64>(Guid.D);
		int64 Result = static_cast<int64>(Mix64(Upper ^ Mix64(Lower)) & static_cast<uint64>(MAX_int64));
		return Result > 0 ? Result : 1;
	}

	static bool IsValidContextNumbers(const int64 RunSeed, const int64 FloorNumber, const int64 GenerationSerial)
	{
		return RunSeed > 0 && FloorNumber > 0 && GenerationSerial > 0 && GenerationSerial <= PCGSeedModulus;
	}

	static bool IsNormalizedFinite(const float Value)
	{
		return FMath::IsFinite(Value) && Value >= -1.0f && Value <= 1.0f;
	}

	static bool IsValidDirectorIntent(const FEFCalystoDirectorIntent& Intent)
	{
		const bool bValidStyle = Intent.PreferredStyle == EEFCalystoDungeonStyle::Auto
			|| Intent.PreferredStyle == EEFCalystoDungeonStyle::Standard
			|| Intent.PreferredStyle == EEFCalystoDungeonStyle::Compact
			|| Intent.PreferredStyle == EEFCalystoDungeonStyle::Branching;
		return bValidStyle && IsNormalizedFinite(Intent.ScaleBias)
			&& IsNormalizedFinite(Intent.BranchingBias)
			&& IsNormalizedFinite(Intent.ThreatBias)
			&& IsNormalizedFinite(Intent.ResourceBias)
			&& IsNormalizedFinite(Intent.ThemeBias)
			&& FMath::IsFinite(Intent.Volatility)
			&& Intent.Volatility >= 0.0f
			&& Intent.Volatility <= 1.0f;
	}

	static bool IsValidOutcome(const FEFCalystoFloorOutcome& Outcome)
	{
		return FMath::IsFinite(Outcome.Combat) && Outcome.Combat >= 0.0f && Outcome.Combat <= 1.0f
			&& FMath::IsFinite(Outcome.Survival) && Outcome.Survival >= 0.0f && Outcome.Survival <= 1.0f
			&& FMath::IsFinite(Outcome.Resources) && Outcome.Resources >= 0.0f && Outcome.Resources <= 1.0f
			&& FMath::IsFinite(Outcome.Pace) && Outcome.Pace >= 0.0f && Outcome.Pace <= 1.0f
			&& Outcome.Deaths >= 0 && Outcome.Failures >= 0;
	}

	static bool IsValidDirectorIntent(const FEFCalystoDirectorIntentV4& Intent)
	{
		return IsNormalizedFinite(Intent.Scale)
			&& IsNormalizedFinite(Intent.Branching)
			&& IsNormalizedFinite(Intent.Danger)
			&& IsNormalizedFinite(Intent.Safe)
			&& IsNormalizedFinite(Intent.Abundance)
			&& IsNormalizedFinite(Intent.Mystery)
			&& IsNormalizedFinite(Intent.ClothingInfluence)
			&& IsNormalizedFinite(Intent.Volatility);
	}

	static bool IsValidOutcome(const FEFCalystoFloorOutcomeV4& Outcome)
	{
		return FMath::IsFinite(Outcome.Combat) && Outcome.Combat >= 0.0f && Outcome.Combat <= 1.0f
			&& FMath::IsFinite(Outcome.Survival) && Outcome.Survival >= 0.0f && Outcome.Survival <= 1.0f
			&& FMath::IsFinite(Outcome.Resources) && Outcome.Resources >= 0.0f && Outcome.Resources <= 1.0f
			&& FMath::IsFinite(Outcome.Pace) && Outcome.Pace >= 0.0f && Outcome.Pace <= 1.0f
			&& FMath::IsFinite(Outcome.DeathsAndFailures)
			&& Outcome.DeathsAndFailures >= 0.0f && Outcome.DeathsAndFailures <= 1.0f;
	}

	static FString BuildOutcomeHash(const FEFCalystoFloorOutcome& Outcome)
	{
		return HashText(FString::Printf(
			TEXT("EFCalystoOutcomeV3|%d|%s|%s|%s|%s|%d|%d"),
			Outcome.bIsValid ? 1 : 0,
			*FloatBits(Outcome.Combat),
			*FloatBits(Outcome.Survival),
			*FloatBits(Outcome.Resources),
			*FloatBits(Outcome.Pace),
			Outcome.Deaths,
			Outcome.Failures));
	}

	static FString BuildEcologyHash(const FEFCalystoRunEcologyState& Ecology)
	{
		FString Canonical = FString::Printf(
			TEXT("EFCalystoEcologyV3|%d|%d|%s|%s|%s|%s|%s|%s|%lld|%lld|%d|%d|"),
			Ecology.bInitialized ? 1 : 0,
			Ecology.bSyntheticHistory ? 1 : 0,
			*FloatBits(Ecology.Scale),
			*FloatBits(Ecology.Branching),
			*FloatBits(Ecology.Threat),
			*FloatBits(Ecology.Abundance),
			*FloatBits(Ecology.Mystery),
			*FloatBits(Ecology.PerformanceEMA),
			Ecology.LastCommittedFloor,
			Ecology.Revision,
			Ecology.ConsecutiveFloorsWithoutFood,
			Ecology.ConsecutiveFloorsWithoutChest);
		for (const EEFCalystoDungeonStyle Style : Ecology.RecentStyles)
		{
			Canonical += FString::Printf(TEXT("S:%d|"), static_cast<int32>(Style));
		}
		for (const FName Theme : Ecology.RecentDominantThemes)
		{
			Canonical += FString::Printf(TEXT("T:%s|"), *Theme.ToString());
		}
		TArray<FEFCalystoPopulationCooldownState> Cooldowns = Ecology.PopulationCooldowns;
		Cooldowns.Sort([](const FEFCalystoPopulationCooldownState& A, const FEFCalystoPopulationCooldownState& B)
		{
			return A.StableId.LexicalLess(B.StableId);
		});
		for (const FEFCalystoPopulationCooldownState& Cooldown : Cooldowns)
		{
			Canonical += FString::Printf(TEXT("C:%s:%lld|"), *Cooldown.StableId.ToString(), Cooldown.LastSelectedFloor);
		}
		Canonical += FString::Printf(TEXT("DNA:%s|"), *Ecology.RunDNAHash);
		return HashText(Canonical);
	}

	static double UnitFromCounter(const uint64 Value)
	{
		return static_cast<double>(Value >> 11U) * (1.0 / 9007199254740992.0);
	}

	static float SignedUnitFromCounter(const uint64 Value)
	{
		return static_cast<float>(UnitFromCounter(Value) * 2.0 - 1.0);
	}

	static FString BuildEcologyHashV4(const FEFCalystoRunEcologyStateV4& Ecology)
	{
		const FEFCalystoContextTraitsV4& DNA = Ecology.RunDNATraits;
		const FString CompanionHash =
			FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Ecology.CompanionSnapshot);
		if (!Ecology.bInitialized || Ecology.RunDNAHash.IsEmpty() || CompanionHash.IsEmpty()
			|| !IsNormalizedFinite(DNA.Scale) || !IsNormalizedFinite(DNA.Branching)
			|| !IsNormalizedFinite(DNA.Danger) || !IsNormalizedFinite(DNA.Safe)
			|| !IsNormalizedFinite(DNA.Abundance) || !IsNormalizedFinite(DNA.Mystery)
			|| !IsNormalizedFinite(DNA.ClothingInfluence)
			|| !FMath::IsFinite(DNA.Volatility) || DNA.Volatility < 0.0f || DNA.Volatility > 1.0f
			|| !FMath::IsFinite(Ecology.PerformanceEMA)
			|| Ecology.PerformanceEMA < 0.0f || Ecology.PerformanceEMA > 1.0f
			|| Ecology.LastCommittedFloor < 0 || Ecology.EcologyRevision < 0
			|| Ecology.ConsecutiveFloorsWithoutFood < 0
			|| Ecology.ConsecutiveFloorsWithoutChest < 0)
		{
			return FString();
		}
		FString Canonical = FString::Printf(
			TEXT("EFCalystoEcologyV4|INIT:1|SYN:%d|DNA_HASH:%s|DNA:%s:%s:%s:%s:%s:%s:%s:%s|EMA:%s|LAST:%lld|REV:%lld|EMPTY:%d:%d|COMPANIONS:%s|"),
			Ecology.bDevelopmentSyntheticHistory ? 1 : 0,
			*Ecology.RunDNAHash,
			*FloatBits(Ecology.RunDNATraits.Scale),
			*FloatBits(Ecology.RunDNATraits.Branching),
			*FloatBits(Ecology.RunDNATraits.Danger),
			*FloatBits(Ecology.RunDNATraits.Safe),
			*FloatBits(Ecology.RunDNATraits.Abundance),
			*FloatBits(Ecology.RunDNATraits.Mystery),
			*FloatBits(Ecology.RunDNATraits.ClothingInfluence),
			*FloatBits(Ecology.RunDNATraits.Volatility),
			*FloatBits(Ecology.PerformanceEMA),
			Ecology.LastCommittedFloor,
			Ecology.EcologyRevision,
			Ecology.ConsecutiveFloorsWithoutFood,
			Ecology.ConsecutiveFloorsWithoutChest,
			*CompanionHash);
		for (const EEFCalystoStyleV4 Style : Ecology.RecentStyles)
		{
			Canonical += FString::Printf(TEXT("STYLE:%d|"), static_cast<int32>(Style));
		}
		for (const EEFCalystoThemeV4 Theme : Ecology.RecentThemes)
		{
			Canonical += FString::Printf(TEXT("THEME:%d|"), static_cast<int32>(Theme));
		}
		TArray<FEFCalystoCooldownStateV4> Cooldowns = Ecology.Cooldowns;
		Cooldowns.Sort([](const FEFCalystoCooldownStateV4& Left, const FEFCalystoCooldownStateV4& Right)
		{
			return Left.StableId.LexicalLess(Right.StableId);
		});
		TSet<FName> SeenCooldownIds;
		for (const FEFCalystoCooldownStateV4& Cooldown : Cooldowns)
		{
			if (Cooldown.StableId.IsNone() || SeenCooldownIds.Contains(Cooldown.StableId)
				|| Cooldown.LastSelectedFloor <= 0 || Cooldown.CooldownFloors <= 0)
			{
				return FString();
			}
			SeenCooldownIds.Add(Cooldown.StableId);
			Canonical += FString::Printf(
				TEXT("COOLDOWN:%s:%lld:%d|"),
				*Cooldown.StableId.ToString(),
				Cooldown.LastSelectedFloor,
				Cooldown.CooldownFloors);
		}
		return HashText(Canonical);
	}

	static double SampleNormal(
		const FEFCalystoDungeonGenerationContext& Context,
		const int32 GeneratorVersion,
		const FString& EcologyHash,
		const uint64 Domain,
		const uint64 StableId,
		uint32& DrawIndex)
	{
		const double U1 = FMath::Max(
			EFCalystoDungeonDeterminism::Uniform01(Context, GeneratorVersion, EcologyHash, Domain, StableId, DrawIndex++),
			1.0e-12);
		const double U2 = EFCalystoDungeonDeterminism::Uniform01(
			Context, GeneratorVersion, EcologyHash, Domain, StableId, DrawIndex++);
		return FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * UE_DOUBLE_PI * U2);
	}

	static double SampleGamma(
		const double Shape,
		const FEFCalystoDungeonGenerationContext& Context,
		const int32 GeneratorVersion,
		const FString& EcologyHash,
		const uint64 Domain,
		const uint64 StableId,
		uint32& DrawIndex)
	{
		if (Shape <= 0.0)
		{
			return 0.0;
		}
		if (Shape < 1.0)
		{
			const double Uniform = FMath::Max(
				EFCalystoDungeonDeterminism::Uniform01(Context, GeneratorVersion, EcologyHash, Domain, StableId, DrawIndex++),
				1.0e-12);
			return SampleGamma(Shape + 1.0, Context, GeneratorVersion, EcologyHash, Domain, StableId, DrawIndex)
				* FMath::Pow(Uniform, 1.0 / Shape);
		}

		const double D = Shape - (1.0 / 3.0);
		const double C = 1.0 / FMath::Sqrt(9.0 * D);
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const double X = SampleNormal(Context, GeneratorVersion, EcologyHash, Domain, StableId, DrawIndex);
			const double Base = 1.0 + C * X;
			if (Base <= 0.0)
			{
				continue;
			}
			const double V = Base * Base * Base;
			const double U = EFCalystoDungeonDeterminism::Uniform01(
				Context, GeneratorVersion, EcologyHash, Domain, StableId, DrawIndex++);
			if (U < 1.0 - 0.0331 * X * X * X * X
				|| FMath::Loge(FMath::Max(U, 1.0e-12)) < 0.5 * X * X + D * (1.0 - V + FMath::Loge(V)))
			{
				return D * V;
			}
		}
		return Shape;
	}

	static int32 NearestValidatedSize(const TArray<int32>& ValidatedSizes, const int32 Requested)
	{
		int32 Best = 0;
		int32 BestDistance = MAX_int32;
		for (const int32 Candidate : ValidatedSizes)
		{
			const int32 Distance = FMath::Abs(Candidate - Requested);
			if (Distance < BestDistance || (Distance == BestDistance && Candidate < Best))
			{
				Best = Candidate;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	static float SmoothFloorNoise(
		const FEFCalystoDungeonGenerationContext& Context,
		const int32 GeneratorVersion,
		const FString& EcologyHash,
		const uint64 Domain,
		const int32 Period)
	{
		const int64 SafePeriod = FMath::Max(2, Period);
		const int64 Coordinate = Context.FloorNumber - 1;
		const int64 Lattice = Coordinate / SafePeriod;
		const double T = static_cast<double>(Coordinate % SafePeriod) / static_cast<double>(SafePeriod);
		const double SmoothT = T * T * (3.0 - 2.0 * T);
		FEFCalystoDungeonGenerationContext FloorStableContext = Context;
		FloorStableContext.FloorNumber = 1;
		FloorStableContext.GenerationSerial = 1;
		const double A = EFCalystoDungeonDeterminism::Uniform01(
			FloorStableContext, GeneratorVersion, EcologyHash, Domain, static_cast<uint64>(Lattice), 0) * 2.0 - 1.0;
		const double B = EFCalystoDungeonDeterminism::Uniform01(
			FloorStableContext, GeneratorVersion, EcologyHash, Domain, static_cast<uint64>(Lattice + 1), 0) * 2.0 - 1.0;
		return static_cast<float>(FMath::Lerp(A, B, SmoothT));
	}

	static float ResolveTrait(
		const FEFCalystoDungeonGenerationContext& Context,
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FEFCalystoRunEcologyState& Ecology,
		const float RunDNA,
		const uint64 Domain)
	{
		const float Smooth = SmoothFloorNoise(
			Context, Policy->GeneratorVersion, Ecology.RunDNAHash, Domain, Policy->Ecology.SmoothNoisePeriod);
		const float Jitter = static_cast<float>(EFCalystoDungeonDeterminism::Uniform01(
			Context, Policy->GeneratorVersion, Ecology.EcologyHash, Domain, 0, 17) * 2.0 - 1.0);
		const float WeightSum = Policy->Ecology.RunDNAWeight + Policy->Ecology.SmoothNoiseWeight + Policy->Ecology.JitterWeight;
		return WeightSum > UE_SMALL_NUMBER
			? FMath::Clamp((Policy->Ecology.RunDNAWeight * RunDNA
				+ Policy->Ecology.SmoothNoiseWeight * Smooth
				+ Policy->Ecology.JitterWeight * Jitter) / WeightSum, -1.0f, 1.0f)
			: 0.0f;
	}

	static bool HasConsecutiveStyle(
		const FEFCalystoRunEcologyState& Ecology,
		const EEFCalystoDungeonStyle Style,
		const int32 Required)
	{
		if (Required <= 0 || Ecology.RecentStyles.Num() < Required)
		{
			return false;
		}
		for (int32 Offset = 0; Offset < Required; ++Offset)
		{
			if (Ecology.RecentStyles[Ecology.RecentStyles.Num() - 1 - Offset] != Style)
			{
				return false;
			}
		}
		return true;
	}

	static bool HasConsecutiveTheme(
		const FEFCalystoRunEcologyState& Ecology,
		const FName Theme,
		const int32 Required)
	{
		if (Required <= 0 || Ecology.RecentDominantThemes.Num() < Required)
		{
			return false;
		}
		for (int32 Offset = 0; Offset < Required; ++Offset)
		{
			if (Ecology.RecentDominantThemes[Ecology.RecentDominantThemes.Num() - 1 - Offset] != Theme)
			{
				return false;
			}
		}
		return true;
	}

	static const FEFCalystoPopulationCatalogEntry* FindPopulationCatalogEntry(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FName StableId)
	{
		if (!Policy || StableId.IsNone())
		{
			return nullptr;
		}
		const TArray<FEFCalystoPopulationCatalogEntry>* Catalogs[] = {
			&Policy->EnemyCatalog,
			&Policy->FoodCatalog,
			&Policy->ChestCatalog,
			&Policy->LootCatalog,
			&Policy->SpecialEventCatalog
		};
		for (const TArray<FEFCalystoPopulationCatalogEntry>* Catalog : Catalogs)
		{
			if (const FEFCalystoPopulationCatalogEntry* Entry = Catalog->FindByPredicate(
				[StableId](const FEFCalystoPopulationCatalogEntry& Candidate)
				{
					return Candidate.StableId == StableId;
				}))
			{
				return Entry;
			}
		}
		return nullptr;
	}

	static bool ValidateCooldownMemory(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FEFCalystoRunEcologyState& Ecology,
		FString& OutError)
	{
		if (!Ecology.bInitialized || Ecology.LastCommittedFloor < 0 || Ecology.Revision < 0
			|| Ecology.ConsecutiveFloorsWithoutFood < 0 || Ecology.ConsecutiveFloorsWithoutChest < 0
			|| !FMath::IsFinite(Ecology.Scale) || Ecology.Scale < -1.0f || Ecology.Scale > 1.0f
			|| !FMath::IsFinite(Ecology.Branching) || Ecology.Branching < -1.0f || Ecology.Branching > 1.0f
			|| !FMath::IsFinite(Ecology.Threat) || Ecology.Threat < -1.0f || Ecology.Threat > 1.0f
			|| !FMath::IsFinite(Ecology.Abundance) || Ecology.Abundance < -1.0f || Ecology.Abundance > 1.0f
			|| !FMath::IsFinite(Ecology.Mystery) || Ecology.Mystery < -1.0f || Ecology.Mystery > 1.0f
			|| !FMath::IsFinite(Ecology.PerformanceEMA) || Ecology.PerformanceEMA < 0.0f || Ecology.PerformanceEMA > 1.0f)
		{
			OutError = TEXT("Run ecology contains invalid scalar state.");
			return false;
		}

		TSet<FName> SeenCooldownIds;
		for (const FEFCalystoPopulationCooldownState& Cooldown : Ecology.PopulationCooldowns)
		{
			const FEFCalystoPopulationCatalogEntry* Entry = FindPopulationCatalogEntry(Policy, Cooldown.StableId);
			if (Cooldown.StableId.IsNone() || SeenCooldownIds.Contains(Cooldown.StableId)
				|| Cooldown.LastSelectedFloor <= 0 || Cooldown.LastSelectedFloor > Ecology.LastCommittedFloor
				|| !Entry || Entry->CooldownFloors <= 0)
			{
				OutError = FString::Printf(TEXT("Run ecology cooldown %s is invalid, duplicated, or not policy-backed."),
					*Cooldown.StableId.ToString());
				return false;
			}
			SeenCooldownIds.Add(Cooldown.StableId);
		}
		return true;
	}

	static bool IsPopulationEntryAvailable(
		const FEFCalystoPopulationCatalogEntry& Entry,
		const int64 FloorNumber,
		const FEFCalystoRunEcologyState& Ecology)
	{
		if (!Entry.bEnabled || Entry.BaseWeight <= 0 || Entry.MinimumFloor > FloorNumber)
		{
			return false;
		}
		if (Entry.CooldownFloors <= 0)
		{
			return true;
		}
		const FEFCalystoPopulationCooldownState* Memory = Ecology.PopulationCooldowns.FindByPredicate(
			[&Entry](const FEFCalystoPopulationCooldownState& Candidate)
			{
				return Candidate.StableId == Entry.StableId;
			});
		return !Memory || (FloorNumber > Memory->LastSelectedFloor
			&& FloorNumber - Memory->LastSelectedFloor > static_cast<int64>(Entry.CooldownFloors));
	}

	static FString BuildIntentHash(const FEFCalystoResolvedFloorIntent& Intent)
	{
		FString Canonical = FString::Printf(
			TEXT("EFCalystoFloorIntentV3|%d|%lld|%lld|%lld|%d|%s|%s|%s|%s|%d|%s|%s|%s|%s|%s|%d,%d,%d|%s|%s|%d|%d|%d|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%s|"),
			Intent.GeneratorVersion,
			Intent.RunSeed,
			Intent.FloorNumber,
			Intent.GenerationSerial,
			Intent.PCGSeed,
			*Intent.PolicyHash,
			*Intent.EcologyHash,
			*Intent.OutcomeHash,
			*BuildOutcomeHash(Intent.FrozenOutcome),
			static_cast<int32>(Intent.Style),
			*FloatBits(Intent.Scale),
			*FloatBits(Intent.Branching),
			*FloatBits(Intent.Threat),
			*FloatBits(Intent.Abundance),
			*FloatBits(Intent.Mystery),
			Intent.DungeonSize.X,
			Intent.DungeonSize.Y,
			Intent.DungeonSize.Z,
			*FloatBits(Intent.CandidateAnchorDensity),
			*FloatBits(Intent.SidePathChance),
			Intent.RoomMinSize,
			Intent.RoomMaxSize,
			Intent.DifficultyTier,
			*FloatBits(Intent.ThreatBudget),
			*FloatBits(Intent.ResourceBudget),
			*FloatBits(Intent.EnemyPresenceChance),
			*FloatBits(Intent.FoodPresenceChance),
			*FloatBits(Intent.ChestPresenceChance),
			*FloatBits(Intent.LootPresenceChance),
			*FloatBits(Intent.SpecialEventPresenceChance),
			Intent.EnemyCount,
			Intent.FoodCount,
			Intent.ChestCount,
			Intent.LootCount,
			Intent.SpecialEventCount,
			*Intent.DominantTheme.ToString());
		Canonical += FString::Printf(TEXT("PACING:%s|"), *FloatBits(Intent.Pacing));
		for (const FEFCalystoThemeWeight& Theme : Intent.ThemeWeights)
		{
			Canonical += FString::Printf(TEXT("T:%s:%s:%d|"), *Theme.ThemeId.ToString(),
				*Theme.RoomType.ToSoftObjectPath().ToString(), Theme.Weight);
		}
		for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
		{
			Canonical += FString::Printf(TEXT("D:%s:%d:%s:%d:%s:%d|"), *Directive.StableId.ToString(),
				static_cast<int32>(Directive.Category), *Directive.ActorClass.ToSoftObjectPath().ToString(), Directive.Count,
				*FloatBits(Directive.CostPerActor), Directive.RelativeWeight);
		}
		return HashText(Canonical);
	}

	static bool BuildPopulationDirectives(
		const TArray<FEFCalystoPopulationCatalogEntry>& SourceCatalog,
		const EEFCalystoSpawnCategory Category,
		const int64 FloorNumber,
		const int32 DesiredCount,
		const float Budget,
		const FEFCalystoDungeonGenerationContext& Context,
		const int32 GeneratorVersion,
		const FString& EcologyHash,
		const FEFCalystoRunEcologyState& Ecology,
		const FEFCalystoEcologyPolicy& EcologyPolicy,
		const float RarityBias,
		const uint64 CompositionDomain,
		TArray<FEFCalystoSpawnDirective>& OutDirectives,
		float& OutSpent,
		FString& OutError)
	{
		OutSpent = 0.0f;
		if (DesiredCount <= 0)
		{
			return true;
		}

		TArray<FEFCalystoPopulationCatalogEntry> Catalog;
		for (const FEFCalystoPopulationCatalogEntry& Entry : SourceCatalog)
		{
			if (IsPopulationEntryAvailable(Entry, FloorNumber, Ecology))
			{
				Catalog.Add(Entry);
			}
		}
		Catalog.Sort([](const FEFCalystoPopulationCatalogEntry& A, const FEFCalystoPopulationCatalogEntry& B)
		{
			return A.StableId.LexicalLess(B.StableId);
		});
		if (Catalog.IsEmpty())
		{
			OutError = FString::Printf(TEXT("No enabled catalog entries can satisfy category %d at floor %lld."),
				static_cast<int32>(Category), FloorNumber);
			return false;
		}

		TArray<int32> Counts;
		Counts.SetNumZeroed(Catalog.Num());
		for (int32 Draw = 0; Draw < DesiredCount; ++Draw)
		{
			const float RarityTarget = FMath::Clamp(
				static_cast<float>(EFCalystoDungeonDeterminism::Uniform01(
					Context,
					GeneratorVersion,
					EcologyHash,
					EFCalystoDungeonDomains::Rarity,
					static_cast<uint64>(Category) + 1ULL,
					static_cast<uint32>(Draw))) + 0.20f * RarityBias,
				0.0f,
				1.0f);
			TArray<int32> Weights;
			Weights.Reserve(Catalog.Num());
			for (int32 Index = 0; Index < Catalog.Num(); ++Index)
			{
				const bool bWithinCap = Counts[Index] < Catalog[Index].MaxPerFloor;
				float MinimumRemainingCost = 0.0f;
				bool bCanFillRemainingSlots = bWithinCap;
				if (bWithinCap)
				{
					const int32 RemainingSlots = DesiredCount - Draw - 1;
					TArray<float> RemainingCosts;
					for (int32 CandidateIndex = 0; CandidateIndex < Catalog.Num(); ++CandidateIndex)
					{
						const int32 CountAfterChoice = Counts[CandidateIndex] + (CandidateIndex == Index ? 1 : 0);
						const int32 Capacity = FMath::Max(0, Catalog[CandidateIndex].MaxPerFloor - CountAfterChoice);
						for (int32 Slot = 0; Slot < Capacity; ++Slot)
						{
							RemainingCosts.Add(Catalog[CandidateIndex].Cost);
						}
					}
					RemainingCosts.Sort();
					bCanFillRemainingSlots = RemainingCosts.Num() >= RemainingSlots;
					for (int32 Slot = 0; Slot < RemainingSlots && RemainingCosts.IsValidIndex(Slot); ++Slot)
					{
						MinimumRemainingCost += RemainingCosts[Slot];
					}
				}
				const bool bWithinBudget = bCanFillRemainingSlots
					&& OutSpent + Catalog[Index].Cost + MinimumRemainingCost <= Budget + KINDA_SMALL_NUMBER;
				const float RarityAffinity = 1.0f - FMath::Abs(RarityTarget - Catalog[Index].Rarity);
				const float RarityFactor = FMath::Lerp(
					1.0f,
					0.10f + 0.90f * RarityAffinity,
					EcologyPolicy.RaritySelectionStrength);
				const int32 RarityWeighted = FMath::Clamp(
					FMath::RoundToInt(static_cast<float>(Catalog[Index].BaseWeight) * RarityFactor * 100.0f),
					1,
					10000);
				Weights.Add(bWithinCap && bWithinBudget ? RarityWeighted : 0);
			}
			const int32 Chosen = EFCalystoDungeonDeterminism::SelectWeightedIndex(
				Weights,
				EFCalystoDungeonDeterminism::DeriveDomainValue(
					Context, GeneratorVersion, EcologyHash, CompositionDomain, 0, static_cast<uint32>(Draw)));
			if (!Catalog.IsValidIndex(Chosen))
			{
				OutError = FString::Printf(TEXT("Category %d cannot satisfy count %d within budget %.3f and per-entry caps."),
					static_cast<int32>(Category), DesiredCount, Budget);
				return false;
			}
			++Counts[Chosen];
			OutSpent += Catalog[Chosen].Cost;
		}

		for (int32 Index = 0; Index < Catalog.Num(); ++Index)
		{
			if (Counts[Index] <= 0)
			{
				continue;
			}
			FEFCalystoSpawnDirective Directive;
			Directive.StableId = Catalog[Index].StableId;
			Directive.Category = Category;
			Directive.ActorClass = Catalog[Index].ActorClass;
			Directive.Count = Counts[Index];
			Directive.CostPerActor = Catalog[Index].Cost;
			Directive.RelativeWeight = Catalog[Index].BaseWeight;
			OutDirectives.Add(MoveTemp(Directive));
		}
		return true;
	}
}

int32 EFCalystoDungeonDeterminism::DerivePCGSeed(
	const int64 RunSeed,
	const int64 FloorNumber,
	const int64 GenerationSerial,
	const int32 GeneratorVersion,
	const FString& PolicyHash,
	const FString& EcologyHash)
{
	using namespace EFCalystoDungeonPrivate;
	if (!IsValidContextNumbers(RunSeed, FloorNumber, GenerationSerial) || GeneratorVersion <= 0
		|| PolicyHash.IsEmpty() || EcologyHash.IsEmpty())
	{
		return 0;
	}
	uint64 Base = Mix64(static_cast<uint64>(RunSeed));
	Base = Mix64(Base ^ static_cast<uint64>(FloorNumber));
	Base = Mix64(Base ^ static_cast<uint64>(GeneratorVersion));
	Base = Mix64(Base ^ HashString64(PolicyHash));
	Base = Mix64(Base ^ HashString64(EcologyHash));
	Base = Mix64(Base ^ EFCalystoDungeonDomains::PCGSeed);
	const uint64 MultiplierDraw = Mix64(Mix64(Base ^ 1ULL) ^ 0ULL);
	const uint64 OffsetDraw = Mix64(Mix64(Base ^ 2ULL) ^ 0ULL);
	const uint64 A = 1ULL + (MultiplierDraw % static_cast<uint64>(PCGSeedModulus - 1));
	const uint64 B = OffsetDraw % static_cast<uint64>(PCGSeedModulus);
	const uint64 Counter = static_cast<uint64>(GenerationSerial - 1);
	return static_cast<int32>(1ULL + ((A * Counter + B) % static_cast<uint64>(PCGSeedModulus)));
}

float EFCalystoDungeonDeterminism::EffectivePERTConcentration(
	const float AuthoredConcentration,
	const float Volatility)
{
	if (!FMath::IsFinite(AuthoredConcentration) || !FMath::IsFinite(Volatility)
		|| AuthoredConcentration < 2.0f || AuthoredConcentration > 8.0f)
	{
		return 0.0f;
	}
	const float V = FMath::Clamp(Volatility, 0.0f, 1.0f);
	return V <= 0.5f
		? FMath::Lerp(8.0f, AuthoredConcentration, V * 2.0f)
		: FMath::Lerp(AuthoredConcentration, 2.0f, (V - 0.5f) * 2.0f);
}

uint64 EFCalystoDungeonDeterminism::DeriveDomainValue(
	const FEFCalystoDungeonGenerationContext& Context,
	const int32 GeneratorVersion,
	const FString& EcologyHash,
	const uint64 Domain,
	const uint64 StableEntityId,
	const uint32 DrawIndex)
{
	using namespace EFCalystoDungeonPrivate;
	if (!Context.IsValid() || GeneratorVersion <= 0)
	{
		return 0;
	}
	uint64 Value = Mix64(static_cast<uint64>(Context.RunSeed));
	Value = Mix64(Value ^ static_cast<uint64>(Context.FloorNumber));
	Value = Mix64(Value ^ static_cast<uint64>(Context.GenerationSerial));
	Value = Mix64(Value ^ static_cast<uint64>(GeneratorVersion));
	Value = Mix64(Value ^ HashString64(Context.PolicyHash));
	Value = Mix64(Value ^ HashString64(EcologyHash));
	Value = Mix64(Value ^ Domain);
	Value = Mix64(Value ^ StableEntityId);
	return Mix64(Value ^ static_cast<uint64>(DrawIndex));
}

double EFCalystoDungeonDeterminism::Uniform01(
	const FEFCalystoDungeonGenerationContext& Context,
	const int32 GeneratorVersion,
	const FString& EcologyHash,
	const uint64 Domain,
	const uint64 StableEntityId,
	const uint32 DrawIndex)
{
	const uint64 Value = DeriveDomainValue(Context, GeneratorVersion, EcologyHash, Domain, StableEntityId, DrawIndex);
	return static_cast<double>(Value >> 11) * (1.0 / 9007199254740992.0);
}

bool EFCalystoDungeonDeterminism::Bernoulli(
	const double Probability,
	const FEFCalystoDungeonGenerationContext& Context,
	const int32 GeneratorVersion,
	const FString& EcologyHash,
	const uint64 Domain,
	const uint64 StableEntityId,
	const uint32 DrawIndex)
{
	if (!FMath::IsFinite(Probability))
	{
		return false;
	}
	return Uniform01(Context, GeneratorVersion, EcologyHash, Domain, StableEntityId, DrawIndex)
		< FMath::Clamp(Probability, 0.0, 1.0);
}

float EFCalystoDungeonDeterminism::SamplePERT(
	const FEFCalystoFloatDistribution& Distribution,
	const float Volatility,
	const FEFCalystoDungeonGenerationContext& Context,
	const int32 GeneratorVersion,
	const FString& EcologyHash,
	const uint64 Domain,
	const uint64 StableEntityId)
{
	using namespace EFCalystoDungeonPrivate;
	if (!FMath::IsFinite(Distribution.Min) || !FMath::IsFinite(Distribution.Mode)
		|| !FMath::IsFinite(Distribution.Max) || Distribution.Min > Distribution.Mode
		|| Distribution.Mode > Distribution.Max || Distribution.Min == Distribution.Max)
	{
		return Distribution.Min;
	}
	const double Range = static_cast<double>(Distribution.Max - Distribution.Min);
	const double Concentration = EFCalystoDungeonDeterminism::EffectivePERTConcentration(
		Distribution.Concentration,
		Volatility);
	if (Concentration < 2.0 || Concentration > 8.0)
	{
		return Distribution.Min;
	}
	const double Alpha = 1.0 + Concentration * static_cast<double>(Distribution.Mode - Distribution.Min) / Range;
	const double Beta = 1.0 + Concentration * static_cast<double>(Distribution.Max - Distribution.Mode) / Range;
	uint32 DrawIndex = 0;
	const double X = SampleGamma(Alpha, Context, GeneratorVersion, EcologyHash, Domain, StableEntityId, DrawIndex);
	const double Y = SampleGamma(Beta, Context, GeneratorVersion, EcologyHash, Domain, StableEntityId, DrawIndex);
	const double Unit = X + Y > UE_DOUBLE_SMALL_NUMBER ? X / (X + Y) : Alpha / (Alpha + Beta);
	return static_cast<float>(static_cast<double>(Distribution.Min) + Unit * Range);
}

int32 EFCalystoDungeonDeterminism::SamplePERT(
	const FEFCalystoIntDistribution& Distribution,
	const float Volatility,
	const FEFCalystoDungeonGenerationContext& Context,
	const int32 GeneratorVersion,
	const FString& EcologyHash,
	const uint64 Domain,
	const uint64 StableEntityId)
{
	FEFCalystoFloatDistribution FloatDistribution;
	FloatDistribution.Min = static_cast<float>(Distribution.Min);
	FloatDistribution.Mode = static_cast<float>(Distribution.Mode);
	FloatDistribution.Max = static_cast<float>(Distribution.Max);
	FloatDistribution.Concentration = Distribution.Concentration;
	return FMath::Clamp(
		FMath::RoundToInt(SamplePERT(FloatDistribution, Volatility, Context, GeneratorVersion, EcologyHash, Domain, StableEntityId)),
		Distribution.Min,
		Distribution.Max);
}

double EFCalystoDungeonDeterminism::Progression(const int64 FloorNumber, const double Tau)
{
	if (FloorNumber <= 0 || !FMath::IsFinite(Tau) || Tau <= 0.0)
	{
		return 0.0;
	}
	return 1.0 - FMath::Exp(-static_cast<double>(FloorNumber - 1) / Tau);
}

int32 EFCalystoDungeonDeterminism::SelectWeightedIndex(const TArray<int32>& Weights, const uint64 RandomValue)
{
	int64 TotalWeight = 0;
	for (const int32 Weight : Weights)
	{
		if (Weight < 0 || TotalWeight > MAX_int32 - Weight)
		{
			return INDEX_NONE;
		}
		TotalWeight += Weight;
	}
	if (Weights.IsEmpty() || TotalWeight <= 0)
	{
		return INDEX_NONE;
	}
	int64 Roll = static_cast<int64>(RandomValue % static_cast<uint64>(TotalWeight));
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		if (Roll < Weights[Index])
		{
			return Index;
		}
		Roll -= Weights[Index];
	}
	return INDEX_NONE;
}

void UEFCalystoDungeonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bHasActiveRun = false;
	RunEpoch = 0;
	PendingRunEpoch = 0;
	ActiveContext = FEFCalystoDungeonGenerationContext();
	ActiveIntent = FEFCalystoResolvedFloorIntent();
	ActiveManifest = FEFCalystoRealizedFloorManifest();
	ActiveIntentV4 = FEFCalystoResolvedFloorIntentV4();
	ActiveManifestV4 = FEFCalystoRealizedFloorManifestV4();
	bCompanionRosterReady = false;
	ReadyCompanionSnapshotHash.Reset();
	RunEcology = FEFCalystoRunEcologyState();
	RunEcologyV4 = FEFCalystoRunEcologyStateV4();
	bHasQueuedDirectorIntent = false;
	QueuedDirectorIntent = FEFCalystoDirectorIntent();
	QueuedDirectorIntentV4 = FEFCalystoDirectorIntentV4();
	bHasSubmittedOutcome = false;
	SubmittedOutcome = FEFCalystoFloorOutcome();
	SubmittedOutcomeV4 = FEFCalystoFloorOutcomeV4();
	bHasSubmittedCompanionSnapshot = false;
	SubmittedCompanionSnapshot = FEFCalystoCompanionRunSnapshot();
	SubmittedCompanionSnapshotV4 = FEFCalystoCompanionSnapshotV4();
	bTravelRequestPending = false;
	bOpenLevelIssued = false;
	bTravelPreparationInProgress = false;
	bExternalTravelPreparationFailed = false;
	ExternalTravelPreparationFailureCode = NAME_None;
	ExternalTravelPreparationFailureMessage.Reset();
	NextTravelRequestId = 0;
	PendingTravelRequestId = 0;
	PendingContext = FEFCalystoDungeonGenerationContext();
	PendingIntent = FEFCalystoResolvedFloorIntent();
	PendingExpectedManifest = FEFCalystoRealizedFloorManifest();
	PendingIntentV4 = FEFCalystoResolvedFloorIntentV4();
	PendingExpectedManifestV4 = FEFCalystoRealizedFloorManifestV4();
	ExpectedManifestForRealization = FEFCalystoRealizedFloorManifest();
	ExpectedManifestForRealizationV4 = FEFCalystoRealizedFloorManifestV4();
	PendingEcology = FEFCalystoRunEcologyState();
	PendingEcologyV4 = FEFCalystoRunEcologyStateV4();
	PendingSourceWorld.Reset();
	bPendingSourceWasDungeon = false;
	PendingSourceTravelState = EEFCalystoDungeonTravelState::Idle;
	PendingSourceGenerationState = EEFCalystoGenerationState::Idle;
	PendingReturnMapPackage.Reset();
	PendingGenerationAttempt = 0;
	bPendingConsumesSubmittedOutcome = false;
	bPendingConsumesQueuedDirectorIntent = false;
	PendingTravelKind = EEFCalystoDungeonTravelKind::None;
	LastTravelKind = EEFCalystoDungeonTravelKind::None;
	TravelState = EEFCalystoDungeonTravelState::Idle;
	GenerationState = EEFCalystoGenerationState::Idle;
	LastFailureCode = NAME_None;
	LastFailureMessage.Reset();
	CurrentGenerationAttempt = 0;
	ReturnMapPackage.Reset();
	bRecoveryTravelPending = false;
	PendingCompletionTransitionId = NAME_None;
	AwaitingCompletionTransitionId = NAME_None;
#if !UE_BUILD_SHIPPING
	bUsingDevelopmentCandidatePolicyForAutomation = false;
	DevelopmentCandidateValidatedDungeonSizesForAutomation.Reset();
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	DevelopmentPopulationScenarioForAutomation = NAME_None;
#endif

	CompilePolicy();
	PreloadCoreRuntimeAssets();
	PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &UEFCalystoDungeonSubsystem::HandlePostWorldInitialization);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(
			this, &UEFCalystoDungeonSubsystem::HandleTravelFailure);
	}
}

void UEFCalystoDungeonSubsystem::Deinitialize()
{
	CancelTravelWatchdog();
	if (ResolvedFloorPreloadHandle.IsValid())
	{
		ResolvedFloorPreloadHandle->CancelHandle();
		ResolvedFloorPreloadHandle.Reset();
	}
	if (PendingResolvedFloorPreloadHandle.IsValid())
	{
		PendingResolvedFloorPreloadHandle->CancelHandle();
		PendingResolvedFloorPreloadHandle.Reset();
	}
	PendingResolvedFloorAssetPaths.Reset();
	FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
	if (GEngine && TravelFailureHandle.IsValid())
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	CachedDirectorPolicy = nullptr;
	CachedDirectorPolicyV4 = nullptr;
#if !UE_BUILD_SHIPPING
	bUsingDevelopmentCandidatePolicyForAutomation = false;
	DevelopmentCandidateValidatedDungeonSizesForAutomation.Reset();
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	DevelopmentPopulationScenarioForAutomation = NAME_None;
#endif
	PreloadedCoreAssets.Reset();
	Super::Deinitialize();
}

FEFCalystoDungeonSnapshotV4 UEFCalystoDungeonSubsystem::GetSnapshot() const
{
	FEFCalystoDungeonSnapshotV4 Snapshot;
	Snapshot.bHasActiveRun = HasActiveRun();
	Snapshot.RunEpoch = Snapshot.bHasActiveRun ? RunEpoch : 0;
	Snapshot.bPolicyValid = bPolicyValid;
	Snapshot.PolicyError = PolicyError;
	Snapshot.bHasQueuedDirectorIntent = bHasQueuedDirectorIntent;
	Snapshot.TravelKind = static_cast<EEFCalystoDungeonTravelKindV4>(
		bTravelRequestPending ? PendingTravelKind : LastTravelKind);
	const FEFCalystoResolvedFloorIntentV4* Intent = nullptr;
	if (bTravelRequestPending && PendingIntentV4.bIsValid)
	{
		Intent = &PendingIntentV4;
	}
	else if (bHasActiveRun && ActiveIntentV4.bIsValid)
	{
		Intent = &ActiveIntentV4;
	}

	if (TravelState == EEFCalystoDungeonTravelState::TravelPending)
	{
		Snapshot.State = EEFCalystoDungeonRunStateV4::Traveling;
	}
	else if (GenerationState == EEFCalystoGenerationState::Ready)
	{
		Snapshot.State = EEFCalystoDungeonRunStateV4::Ready;
	}
	else if (GenerationState == EEFCalystoGenerationState::Failed
		|| GenerationState == EEFCalystoGenerationState::Returning)
	{
		Snapshot.State = EEFCalystoDungeonRunStateV4::Failed;
	}
	else if (TravelState == EEFCalystoDungeonTravelState::AwaitingFloorReady)
	{
		Snapshot.State = EEFCalystoDungeonRunStateV4::Generating;
	}

	Snapshot.PolicyHash = CompiledPolicyHash;
	Snapshot.EcologyHash = Intent ? Intent->EcologyHash : RunEcologyV4.EcologyHash;
	Snapshot.bCompanionReady = IsCompanionRosterReady();
	Snapshot.bDoorReady = Snapshot.State == EEFCalystoDungeonRunStateV4::Ready;
	Snapshot.FailureCode = LastFailureCode;
	Snapshot.FailureMessage = LastFailureMessage;
	Snapshot.CurrentAttempt = CurrentGenerationAttempt;
	Snapshot.MaximumAttempts = MaximumGenerationAttempts;
	Snapshot.ReturnMapPackage = ReturnMapPackage;
	if (bTravelRequestPending)
	{
		Snapshot.PendingFloorNumber = PendingContext.FloorNumber;
		Snapshot.PendingGenerationSerial = PendingContext.GenerationSerial;
	}
	if (Intent)
	{
		Snapshot.RunSeed = Intent->RunSeed;
		Snapshot.FloorNumber = Intent->FloorNumber;
		Snapshot.GenerationSerial = Intent->GenerationSerial;
		Snapshot.PCGSeed = Intent->PCGSeed;
		Snapshot.Style = Intent->Style;
		Snapshot.Theme = Intent->Theme;
		Snapshot.DungeonSize = Intent->DungeonSize;
		Snapshot.DevelopmentForcedDungeonEdge = Intent->DevelopmentForcedDungeonEdge;
		Snapshot.DevelopmentPopulationScenario = Intent->DevelopmentPopulationScenario;
		Snapshot.CandidateAnchorDensity = Intent->CandidateAnchorDensity;
		Snapshot.SidePathChance = Intent->SidePathChance;
		Snapshot.ResolvedTraits = Intent->ResolvedTraits;
		Snapshot.Categories = Intent->Categories;
		Snapshot.ThreatBudget = Intent->ThreatBudget;
		Snapshot.PlannedThreatCost = Intent->PlannedThreatCost;
		Snapshot.ResourceBudget = Intent->ResourceBudget;
		Snapshot.PlannedResourceCost = Intent->PlannedResourceCost;
		Snapshot.IntentHash = Intent->IntentHash;
		Snapshot.CompanionSnapshotHash = Intent->CompanionSnapshotHash;
	}
	if (ActiveManifestV4.bIsValid)
	{
		Snapshot.ManifestHash = ActiveManifestV4.ManifestHash;
		Snapshot.RealizedThreatCost = ActiveManifestV4.RealizedThreatCost;
		Snapshot.RealizedResourceCost = ActiveManifestV4.RealizedResourceCost;
	}
	return Snapshot;
}

FEFCalystoResolvedFloorIntentV4 UEFCalystoDungeonSubsystem::GetResolvedFloorIntent() const
{
	return ActiveIntentV4;
}

FEFCalystoRealizedFloorManifestV4 UEFCalystoDungeonSubsystem::GetRealizedFloorManifest() const
{
	return ActiveManifestV4;
}

FEFCalystoRunEcologyStateV4 UEFCalystoDungeonSubsystem::GetRunEcology() const
{
	return RunEcologyV4;
}

FEFCalystoDirectorIntentV4 UEFCalystoDungeonSubsystem::GetNextFloorDirectorIntent() const
{
	return bHasQueuedDirectorIntent ? QueuedDirectorIntentV4 : FEFCalystoDirectorIntentV4();
}

bool UEFCalystoDungeonSubsystem::SetNextFloorDirectorIntent(const FEFCalystoDirectorIntentV4& NewIntent)
{
#if UE_BUILD_SHIPPING
	(void)NewIntent;
	return false;
#else
	if (!DevelopmentPopulationScenarioForAutomation.IsNone()
		|| !EFCalystoDungeonPrivate::IsValidDirectorIntent(NewIntent)
		|| bTravelRequestPending || bTravelPreparationInProgress)
	{
		return false;
	}
	QueuedDirectorIntentV4 = NewIntent;
	bHasQueuedDirectorIntent = true;
	return true;
#endif
}

void UEFCalystoDungeonSubsystem::ClearNextFloorDirectorIntent()
{
	if (!bTravelRequestPending && !bTravelPreparationInProgress)
	{
		QueuedDirectorIntentV4 = FEFCalystoDirectorIntentV4();
		bHasQueuedDirectorIntent = false;
	}
}

bool UEFCalystoDungeonSubsystem::SubmitFloorOutcome(const FEFCalystoFloorOutcomeV4& Outcome)
{
	if (!HasActiveRun() || bTravelRequestPending
		|| TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Ready
		|| !EFCalystoDungeonPrivate::IsValidOutcome(Outcome))
	{
		return false;
	}
	SubmittedOutcomeV4 = Outcome;
	bHasSubmittedOutcome = true;
	return true;
}

bool UEFCalystoDungeonSubsystem::HasActiveRun() const
{
	return bHasActiveRun && ActiveContext.IsValid() && ActiveIntentV4.bIsValid
		&& RunEcologyV4.bInitialized && !RunEcologyV4.EcologyHash.IsEmpty();
}

int64 UEFCalystoDungeonSubsystem::GetCurrentFloor() const
{
	return bHasActiveRun ? ActiveContext.FloorNumber : 0;
}

int64 UEFCalystoDungeonSubsystem::GetRunSeed() const
{
	return bHasActiveRun ? ActiveContext.RunSeed : 0;
}

int64 UEFCalystoDungeonSubsystem::GetRunEpoch() const
{
	return bHasActiveRun ? RunEpoch : 0;
}

bool UEFCalystoDungeonSubsystem::SubmitCompanionRunSnapshot(
	const FEFCalystoCompanionSnapshotV4& Snapshot)
{
	const FString SnapshotHash = FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Snapshot);
	if (bTravelRequestPending || !bTravelPreparationInProgress || SnapshotHash.IsEmpty())
	{
		return false;
	}
	SubmittedCompanionSnapshotV4 = Snapshot;
	bHasSubmittedCompanionSnapshot = true;
	return true;
}

void UEFCalystoDungeonSubsystem::ReportDirectorTravelPreparationFailure(
	const FName FailureCode,
	const FString& FailureMessage)
{
	if (!bTravelPreparationInProgress || bTravelRequestPending)
	{
		return;
	}
	bExternalTravelPreparationFailed = true;
	ExternalTravelPreparationFailureCode = FailureCode.IsNone()
		? FName(TEXT("EXTERNAL_TRAVEL_PREPARATION_FAILED"))
		: FailureCode;
	ExternalTravelPreparationFailureMessage = FailureMessage.IsEmpty()
		? TEXT("A project-owned pre-travel adapter failed closed.")
		: FailureMessage;
}

bool UEFCalystoDungeonSubsystem::IsTravelRequestPending() const
{
	return bTravelRequestPending;
}

bool UEFCalystoDungeonSubsystem::RequestStartNewRun()
{
	return RequestStartNewRunWithSeed(EFCalystoDungeonPrivate::CreatePositiveRunSeed());
}

bool UEFCalystoDungeonSubsystem::RequestStartNewRunWithSeed(const int64 NewRunSeed)
{
	return NewRunSeed > 0
		&& BeginTravel(MakeContext(NewRunSeed, 1, 1), EEFCalystoDungeonTravelKind::NewRun);
}

bool UEFCalystoDungeonSubsystem::RequestAdvanceFloor()
{
	if (!HasActiveRun() || ActiveContext.FloorNumber == MAX_int64
		|| ActiveContext.GenerationSerial >= EFCalystoDungeonPrivate::PCGSeedModulus
		|| bTravelRequestPending
		|| bTravelPreparationInProgress
		|| TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Ready
		|| !ActiveManifestV4.bIsValid)
	{
		return false;
	}
	{
		TGuardValue<bool> PreparationGuard(bTravelPreparationInProgress, true);
		BeforeFloorAdvanceEvent.Broadcast(ActiveContext.FloorNumber, ActiveIntentV4);
	}
	return BeginTravel(
		MakeContext(ActiveContext.RunSeed, ActiveContext.FloorNumber + 1, ActiveContext.GenerationSerial + 1),
		EEFCalystoDungeonTravelKind::Advance);
}

bool UEFCalystoDungeonSubsystem::RequestRerollCurrentFloor()
{
	if (!HasActiveRun() || ActiveContext.GenerationSerial >= EFCalystoDungeonPrivate::PCGSeedModulus)
	{
		return false;
	}
	if (TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Ready
		|| !ActiveManifestV4.bIsValid)
	{
		return false;
	}
	return BeginTravel(
		MakeContext(ActiveContext.RunSeed, ActiveContext.FloorNumber, ActiveContext.GenerationSerial + 1),
		EEFCalystoDungeonTravelKind::Reroll);
}

bool UEFCalystoDungeonSubsystem::RequestReplayCurrentFloor()
{
	return HasActiveRun()
		&& TravelState == EEFCalystoDungeonTravelState::Idle
		&& GenerationState == EEFCalystoGenerationState::Ready
		&& ActiveManifestV4.bIsValid
		&& BeginTravel(ActiveContext, EEFCalystoDungeonTravelKind::Replay);
}

bool UEFCalystoDungeonSubsystem::RequestTravelToFloor(const int64 TargetFloor)
{
#if UE_BUILD_SHIPPING
	(void)TargetFloor;
	return false;
#else
	if (!HasActiveRun() || TargetFloor <= 0 || ActiveContext.GenerationSerial >= EFCalystoDungeonPrivate::PCGSeedModulus
		|| TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Ready
		|| !ActiveManifestV4.bIsValid)
	{
		return false;
	}
	return BeginTravel(
		MakeContext(ActiveContext.RunSeed, TargetFloor, ActiveContext.GenerationSerial + 1),
		EEFCalystoDungeonTravelKind::DebugJump);
#endif
}

bool UEFCalystoDungeonSubsystem::NotifyPopulationRealizedLegacyV3(
	const FEFCalystoRealizedFloorManifest& Manifest)
{
	if (!HasActiveRun() || TravelState != EEFCalystoDungeonTravelState::AwaitingFloorReady
		|| !Manifest.bIsValid || ActiveManifest.bIsValid || !CachedDirectorPolicy)
	{
		return false;
	}
	const FEFCalystoDirectorLimits& Limits = CachedDirectorPolicy->Limits;
	const int32 TotalActors = Manifest.EnemyCount + Manifest.FoodCount + Manifest.ChestCount
		+ Manifest.LootCount + Manifest.SpecialEventCount;
	if (Manifest.RunSeed != ActiveIntent.RunSeed || Manifest.FloorNumber != ActiveIntent.FloorNumber
		|| Manifest.GenerationSerial != ActiveIntent.GenerationSerial || Manifest.PCGSeed != ActiveIntent.PCGSeed
		|| Manifest.IntentHash != ActiveIntent.IntentHash
		|| Manifest.EnemyCount != ActiveIntent.EnemyCount || Manifest.FoodCount != ActiveIntent.FoodCount
		|| Manifest.ChestCount != ActiveIntent.ChestCount || Manifest.LootCount != ActiveIntent.LootCount
		|| Manifest.SpecialEventCount != ActiveIntent.SpecialEventCount
		|| Manifest.SpawnedActorCount != TotalActors || TotalActors > Limits.MaxDirectorActors
		|| Manifest.EnemyCount < 0 || Manifest.EnemyCount > Limits.MaxEnemies
		|| Manifest.FoodCount < 0 || Manifest.FoodCount > Limits.MaxFood
		|| Manifest.ChestCount < 0 || Manifest.ChestCount > Limits.MaxChests
		|| Manifest.LootCount < 0 || Manifest.LootCount > Limits.MaxLoot
		|| Manifest.SpecialEventCount < 0 || Manifest.SpecialEventCount > Limits.MaxSpecialEvents
		|| !FMath::IsFinite(Manifest.RealizedThreatCost) || Manifest.RealizedThreatCost < 0.0f
		|| Manifest.RealizedThreatCost > ActiveIntent.ThreatBudget + KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Manifest.RealizedResourceCost) || Manifest.RealizedResourceCost < 0.0f
		|| Manifest.RealizedResourceCost > ActiveIntent.ResourceBudget + KINDA_SMALL_NUMBER
		|| Manifest.AnchorTopologyHash.IsEmpty() || Manifest.PopulationHash.IsEmpty() || Manifest.ResourceHash.IsEmpty()
		|| Manifest.ManifestHash.IsEmpty() || Manifest.ManifestHash != ComputeManifestHash(Manifest))
	{
		UE_LOG(LogEFCalystoDungeon, Error, TEXT("Rejected inconsistent realized manifest for intent %s."), *ActiveIntent.IntentHash);
		return false;
	}
	if (ExpectedManifestForRealization.bIsValid
		&& (Manifest.ManifestHash != ExpectedManifestForRealization.ManifestHash
			|| Manifest.AnchorTopologyHash != ExpectedManifestForRealization.AnchorTopologyHash
			|| Manifest.PopulationHash != ExpectedManifestForRealization.PopulationHash
			|| Manifest.ResourceHash != ExpectedManifestForRealization.ResourceHash))
	{
		UE_LOG(LogEFCalystoDungeon, Error,
			TEXT("Replay/recovery manifest drift: expected=%s actual=%s intent=%s."),
			*ExpectedManifestForRealization.ManifestHash, *Manifest.ManifestHash, *ActiveIntent.IntentHash);
		return false;
	}

	int32 DirectiveCounts[5] = {0, 0, 0, 0, 0};
	float CalculatedThreat = 0.0f;
	float CalculatedResources = 0.0f;
	TSet<FName> DirectiveIds;
	for (const FEFCalystoSpawnDirective& Directive : Manifest.SpawnDirectives)
	{
		const int32 CategoryIndex = static_cast<int32>(Directive.Category);
		if (Directive.StableId.IsNone() || DirectiveIds.Contains(Directive.StableId)
			|| CategoryIndex < 0 || CategoryIndex >= UE_ARRAY_COUNT(DirectiveCounts)
			|| Directive.ActorClass.IsNull() || Directive.Count <= 0
			|| !FMath::IsFinite(Directive.CostPerActor) || Directive.CostPerActor < 0.0f)
		{
			return false;
		}
		DirectiveIds.Add(Directive.StableId);
		DirectiveCounts[CategoryIndex] += Directive.Count;
		const float Cost = Directive.CostPerActor * static_cast<float>(Directive.Count);
		if (Directive.Category == EEFCalystoSpawnCategory::Enemy) { CalculatedThreat += Cost; }
		else { CalculatedResources += Cost; }
	}
	if (DirectiveCounts[0] != Manifest.EnemyCount || DirectiveCounts[1] != Manifest.FoodCount
		|| DirectiveCounts[2] != Manifest.ChestCount || DirectiveCounts[3] != Manifest.LootCount
		|| DirectiveCounts[4] != Manifest.SpecialEventCount
		|| !FMath::IsNearlyEqual(CalculatedThreat, Manifest.RealizedThreatCost, 0.01f)
		|| !FMath::IsNearlyEqual(CalculatedResources, Manifest.RealizedResourceCost, 0.01f))
	{
		return false;
	}
	TArray<FEFCalystoSpawnDirective> ExpectedDirectives = ActiveIntent.SpawnDirectives;
	TArray<FEFCalystoSpawnDirective> RealizedDirectives = Manifest.SpawnDirectives;
	auto SortDirectives = [](TArray<FEFCalystoSpawnDirective>& Directives)
	{
		Directives.Sort([](const FEFCalystoSpawnDirective& A, const FEFCalystoSpawnDirective& B)
		{
			if (A.Category != B.Category) { return static_cast<uint8>(A.Category) < static_cast<uint8>(B.Category); }
			return A.StableId.LexicalLess(B.StableId);
		});
	};
	SortDirectives(ExpectedDirectives);
	SortDirectives(RealizedDirectives);
	if (ExpectedDirectives.Num() != RealizedDirectives.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < ExpectedDirectives.Num(); ++Index)
	{
		const FEFCalystoSpawnDirective& Expected = ExpectedDirectives[Index];
		const FEFCalystoSpawnDirective& Realized = RealizedDirectives[Index];
		if (Expected.StableId != Realized.StableId || Expected.Category != Realized.Category
			|| Expected.ActorClass.ToSoftObjectPath() != Realized.ActorClass.ToSoftObjectPath()
			|| Expected.Count != Realized.Count || Expected.RelativeWeight != Realized.RelativeWeight
			|| !FMath::IsNearlyEqual(Expected.CostPerActor, Realized.CostPerActor))
		{
			return false;
		}
	}

	ActiveManifest = Manifest;
	GenerationState = EEFCalystoGenerationState::RealizingPopulation;
	return true;
}

bool UEFCalystoDungeonSubsystem::NotifyPopulationRealized(
	const FEFCalystoRealizedFloorManifestV4& Manifest)
{
	if (!bHasActiveRun || TravelState != EEFCalystoDungeonTravelState::AwaitingFloorReady
		|| !ActiveIntentV4.bIsValid || !Manifest.bIsValid || ActiveManifestV4.bIsValid
		|| !CachedDirectorPolicyV4)
	{
		return false;
	}

	auto ExpectedCount = [this](const EEFCalystoContentCategoryV4 Category)
	{
		if (const FEFCalystoResolvedCategoryV4* Resolved = ActiveIntentV4.Categories.FindByPredicate(
			[Category](const FEFCalystoResolvedCategoryV4& Candidate)
			{
				return Candidate.Category == Category;
			}))
		{
			return Resolved->TargetCount;
		}
		return 0;
	};

	const FEFCalystoSafetyCeilingsV4& Caps = CachedDirectorPolicyV4->SafetyCeilings;
	const int32 TotalActors = Manifest.EnemyCount + Manifest.NPCCount + Manifest.FoodCount
		+ Manifest.ChestCount + Manifest.LooseLootCount + Manifest.ClothingCount
		+ Manifest.SpecialEventCount;
	if (Manifest.RunSeed != ActiveIntentV4.RunSeed
		|| Manifest.FloorNumber != ActiveIntentV4.FloorNumber
		|| Manifest.GenerationSerial != ActiveIntentV4.GenerationSerial
		|| Manifest.IntentHash != ActiveIntentV4.IntentHash
		|| Manifest.CompanionSnapshotHash != ActiveIntentV4.CompanionSnapshotHash
		|| Manifest.EnemyCount != ExpectedCount(EEFCalystoContentCategoryV4::Enemy)
		|| Manifest.NPCCount != ExpectedCount(EEFCalystoContentCategoryV4::NPC)
		|| Manifest.FoodCount != ExpectedCount(EEFCalystoContentCategoryV4::Food)
		|| Manifest.ChestCount != ExpectedCount(EEFCalystoContentCategoryV4::Chest)
		|| Manifest.LooseLootCount != ExpectedCount(EEFCalystoContentCategoryV4::LooseLoot)
		|| Manifest.ClothingCount != ExpectedCount(EEFCalystoContentCategoryV4::Clothing)
		|| Manifest.SpecialEventCount != ExpectedCount(EEFCalystoContentCategoryV4::SpecialEvent)
		|| Manifest.EnemyCount < 0 || Manifest.EnemyCount > Caps.MaximumEnemies
		|| Manifest.NPCCount < 0 || Manifest.NPCCount > Caps.MaximumNPCs
		|| Manifest.FoodCount < 0 || Manifest.FoodCount > Caps.MaximumFood
		|| Manifest.ChestCount < 0 || Manifest.ChestCount > Caps.MaximumChests
		|| Manifest.LooseLootCount < 0 || Manifest.LooseLootCount > Caps.MaximumLooseLoot
		|| Manifest.ClothingCount < 0 || Manifest.ClothingCount > Caps.MaximumClothing
		|| Manifest.SpecialEventCount < 0 || Manifest.SpecialEventCount > Caps.MaximumSpecialEvents
		|| TotalActors < 0 || TotalActors > Caps.MaximumDirectorActors
		|| Manifest.SpawnedActorCount != TotalActors
		|| Manifest.Instances.Num() != TotalActors
		|| Manifest.CandidateAnchorCount < 0
		|| !FMath::IsFinite(Manifest.RealizedThreatCost) || Manifest.RealizedThreatCost < 0.0f
		|| Manifest.RealizedThreatCost > ActiveIntentV4.ThreatBudget + 0.001f
		|| !FMath::IsFinite(Manifest.RealizedResourceCost) || Manifest.RealizedResourceCost < 0.0f
		|| Manifest.RealizedResourceCost > ActiveIntentV4.ResourceBudget + 0.001f
		|| Manifest.AnchorTopologyHash.IsEmpty() || Manifest.PopulationHash.IsEmpty()
		|| Manifest.ResourceHash.IsEmpty() || Manifest.ManifestHash.IsEmpty()
		|| Manifest.ManifestHash != ComputeManifestHashV4(Manifest))
	{
		UE_LOG(LogEFCalystoDungeon, Error,
			TEXT("Rejected inconsistent realized V4 manifest for intent %s."),
			*ActiveIntentV4.IntentHash);
		return false;
	}

	if (ExpectedManifestForRealizationV4.bIsValid
		&& (Manifest.ManifestHash != ExpectedManifestForRealizationV4.ManifestHash
			|| Manifest.AnchorTopologyHash != ExpectedManifestForRealizationV4.AnchorTopologyHash
			|| Manifest.PopulationHash != ExpectedManifestForRealizationV4.PopulationHash
			|| Manifest.ResourceHash != ExpectedManifestForRealizationV4.ResourceHash
			|| Manifest.CompanionSnapshotHash != ExpectedManifestForRealizationV4.CompanionSnapshotHash))
	{
		UE_LOG(LogEFCalystoDungeon, Error,
			TEXT("V4 replay/recovery manifest drift: expected=%s actual=%s intent=%s."),
			*ExpectedManifestForRealizationV4.ManifestHash,
			*Manifest.ManifestHash,
			*ActiveIntentV4.IntentHash);
		return false;
	}

	TMap<FName, const FEFCalystoSpawnInstanceDirectiveV4*> ExpectedById;
	for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : ActiveIntentV4.SpawnDirectives)
	{
		if (Directive.StableInstanceId.IsNone() || ExpectedById.Contains(Directive.StableInstanceId))
		{
			return false;
		}
		ExpectedById.Add(Directive.StableInstanceId, &Directive);
	}
	if (ExpectedById.Num() != TotalActors)
	{
		return false;
	}

	TSet<FName> RealizedIds;
	int32 CountByCategory[9] = {};
	float CalculatedThreatCost = 0.0f;
	float CalculatedResourceCost = 0.0f;
	for (const FEFCalystoRealizedInstanceV4& Instance : Manifest.Instances)
	{
		const FEFCalystoSpawnInstanceDirectiveV4* const* ExpectedPtr =
			ExpectedById.Find(Instance.StableInstanceId);
		if (!ExpectedPtr || !*ExpectedPtr || RealizedIds.Contains(Instance.StableInstanceId))
		{
			return false;
		}
		const FEFCalystoSpawnInstanceDirectiveV4& Expected = **ExpectedPtr;
		if (Instance.StableCompanionId != Expected.StableCompanionId
			|| Instance.CatalogId != Expected.CatalogId
			|| Instance.VariantId != Expected.VariantId
			|| Instance.Archetype != Expected.Archetype
			|| Instance.Gender != Expected.Gender
			|| Instance.Lifecycle != Expected.Lifecycle
			|| Instance.Category != Expected.Category
			|| Instance.ActorClass.ToSoftObjectPath() != Expected.ActorClass.ToSoftObjectPath()
			|| Instance.Tier != Expected.Tier || Instance.LogicalLevel != Expected.LogicalLevel
			|| Instance.CooldownFloors != Expected.CooldownFloors
			|| !FMath::IsNearlyEqual(Instance.EffectiveThreatCost, Expected.EffectiveThreatCost, 0.001f))
		{
			return false;
		}
		const int32 CategoryIndex = static_cast<int32>(Instance.Category);
		if (CategoryIndex < 0 || CategoryIndex >= UE_ARRAY_COUNT(CountByCategory))
		{
			return false;
		}
		++CountByCategory[CategoryIndex];
		if (Instance.Category == EEFCalystoContentCategoryV4::Enemy)
		{
			CalculatedThreatCost += Instance.EffectiveThreatCost;
		}
		else if (Instance.Category != EEFCalystoContentCategoryV4::NPC
			&& Instance.Category != EEFCalystoContentCategoryV4::Decoration
			&& Instance.Category != EEFCalystoContentCategoryV4::Lighting)
		{
			CalculatedResourceCost += 1.0f;
		}
		RealizedIds.Add(Instance.StableInstanceId);
	}
	if (CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::Enemy)] != Manifest.EnemyCount
		|| CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::NPC)] != Manifest.NPCCount
		|| CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::Food)] != Manifest.FoodCount
		|| CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::Chest)] != Manifest.ChestCount
		|| CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::LooseLoot)] != Manifest.LooseLootCount
		|| CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::Clothing)] != Manifest.ClothingCount
		|| CountByCategory[static_cast<int32>(EEFCalystoContentCategoryV4::SpecialEvent)] != Manifest.SpecialEventCount
		|| !FMath::IsNearlyEqual(CalculatedThreatCost, Manifest.RealizedThreatCost, 0.001f)
		|| !FMath::IsNearlyEqual(CalculatedResourceCost, Manifest.RealizedResourceCost, 0.001f)
		|| !FMath::IsNearlyEqual(CalculatedThreatCost, ActiveIntentV4.PlannedThreatCost, 0.001f)
		|| !FMath::IsNearlyEqual(CalculatedResourceCost, ActiveIntentV4.PlannedResourceCost, 0.001f))
	{
		return false;
	}

	auto SortChestContents = [](TArray<FEFCalystoChestContentDirectiveV4>& Contents)
	{
		Contents.Sort([](const FEFCalystoChestContentDirectiveV4& Left,
			const FEFCalystoChestContentDirectiveV4& Right)
		{
			if (Left.ContainerInstanceId != Right.ContainerInstanceId)
			{
				return Left.ContainerInstanceId.LexicalLess(Right.ContainerInstanceId);
			}
			return Left.StableAttemptId.LexicalLess(Right.StableAttemptId);
		});
	};
	TArray<FEFCalystoChestContentDirectiveV4> ExpectedContents = ActiveIntentV4.ChestContentDirectives;
	TArray<FEFCalystoChestContentDirectiveV4> RealizedContents;
	for (const FEFCalystoRealizedInstanceV4& Instance : Manifest.Instances)
	{
		if (Instance.Category != EEFCalystoContentCategoryV4::Chest)
		{
			if (!Instance.VerifiedChestContentIds.IsEmpty() || !Instance.VerifiedChestContents.IsEmpty())
			{
				return false;
			}
			continue;
		}
		RealizedContents.Append(Instance.VerifiedChestContents);
		TArray<FName> ExpectedIds;
		for (const FEFCalystoChestContentDirectiveV4& Content : Instance.VerifiedChestContents)
		{
			ExpectedIds.Add(Content.ContentCatalogId);
		}
		ExpectedIds.Sort(FNameLexicalLess());
		TArray<FName> RealizedIdsForContainer = Instance.VerifiedChestContentIds;
		RealizedIdsForContainer.Sort(FNameLexicalLess());
		if (ExpectedIds != RealizedIdsForContainer)
		{
			return false;
		}
	}
	SortChestContents(ExpectedContents);
	SortChestContents(RealizedContents);
	if (ExpectedContents.Num() != RealizedContents.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < ExpectedContents.Num(); ++Index)
	{
		const FEFCalystoChestContentDirectiveV4& Expected = ExpectedContents[Index];
		const FEFCalystoChestContentDirectiveV4& Realized = RealizedContents[Index];
		if (Expected.ContainerInstanceId != Realized.ContainerInstanceId
			|| Expected.StableAttemptId != Realized.StableAttemptId
			|| Expected.ContentCatalogId != Realized.ContentCatalogId
			|| Expected.ContentClass.ToSoftObjectPath() != Realized.ContentClass.ToSoftObjectPath()
			|| Expected.Tier != Realized.Tier
			|| Expected.CooldownFloors != Realized.CooldownFloors)
		{
			return false;
		}
	}

	ActiveManifestV4 = Manifest;
	GenerationState = EEFCalystoGenerationState::RealizingPopulation;
	return true;
}

bool UEFCalystoDungeonSubsystem::NotifyCompanionRosterReady(
	const FString& CompanionSnapshotHash)
{
	if (!bHasActiveRun || TravelState != EEFCalystoDungeonTravelState::AwaitingFloorReady
		|| !ActiveIntentV4.bIsValid || CompanionSnapshotHash.IsEmpty()
		|| CompanionSnapshotHash != ActiveIntentV4.CompanionSnapshotHash)
	{
		return false;
	}
	bCompanionRosterReady = true;
	ReadyCompanionSnapshotHash = CompanionSnapshotHash;
	return true;
}

bool UEFCalystoDungeonSubsystem::IsCompanionRosterReady() const
{
	return bCompanionRosterReady && ActiveIntentV4.bIsValid
		&& !ReadyCompanionSnapshotHash.IsEmpty()
		&& ReadyCompanionSnapshotHash == ActiveIntentV4.CompanionSnapshotHash;
}

bool UEFCalystoDungeonSubsystem::NotifyFloorReady()
{
	if (!HasActiveRun() || TravelState != EEFCalystoDungeonTravelState::AwaitingFloorReady
		|| !ActiveIntentV4.bIsValid || !ActiveManifestV4.bIsValid
		|| !IsCompanionRosterReady())
	{
		return false;
	}
	TravelState = EEFCalystoDungeonTravelState::Idle;
	GenerationState = EEFCalystoGenerationState::Ready;
	LastFailureCode = NAME_None;
	LastFailureMessage.Reset();
	FloorReadyEvent.Broadcast(
		ActiveIntentV4.FloorNumber,
		ActiveIntentV4.PCGSeed,
		ActiveIntentV4,
		ActiveManifestV4);
	const FName CompletedTransition = AwaitingCompletionTransitionId;
	AwaitingCompletionTransitionId = NAME_None;
	if (!CompletedTransition.IsNone())
	{
		FloorCompletedEvent.Broadcast(CompletedTransition);
	}
	ExpectedManifestForRealization = FEFCalystoRealizedFloorManifest();
	ExpectedManifestForRealizationV4 = FEFCalystoRealizedFloorManifestV4();
	return true;
}

bool UEFCalystoDungeonSubsystem::NotifyGenerationFailed(const FName FailureCode, const FString& FailureMessage)
{
	if (!HasActiveRun() || TravelState != EEFCalystoDungeonTravelState::AwaitingFloorReady
		|| GenerationState == EEFCalystoGenerationState::Returning)
	{
		return false;
	}
	LastFailureCode = FailureCode.IsNone() ? FName(TEXT("UNKNOWN_GENERATION_FAILURE")) : FailureCode;
	LastFailureMessage = FailureMessage;
	GenerationState = EEFCalystoGenerationState::Failed;
	UE_LOG(LogEFCalystoDungeon, Error, TEXT("Calysto generation failed code=%s attempt=%d/%d intent=%s: %s"),
		*LastFailureCode.ToString(), CurrentGenerationAttempt, MaximumGenerationAttempts,
		*ActiveIntentV4.IntentHash, *LastFailureMessage);
	if (CurrentGenerationAttempt < MaximumGenerationAttempts)
	{
		return ReloadFrozenIntentForRecovery();
	}
	ReturnFromFailedDungeon();
	return true;
}

FString UEFCalystoDungeonSubsystem::SampleDirectorRolls(const int32 SampleCount) const
{
#if UE_BUILD_SHIPPING
	(void)SampleCount;
	return FString();
#else
	if (!bPolicyValid || !CachedDirectorPolicyV4 || SampleCount <= 0 || SampleCount > 100000)
	{
		return FString();
	}
	const int64 Seed = HasActiveRun() ? ActiveContext.RunSeed : 42;
	const FEFCalystoRunEcologyStateV4 Ecology = BuildInitialEcologyV4(
		Seed, CompiledPolicyHash, CachedDirectorPolicyV4->GeneratorVersion);
	TMap<EEFCalystoStyleV4, int32> StyleCounts;
	TMap<EEFCalystoThemeV4, int32> ThemeCounts;
	double SizeTotal = 0.0;
	double EnemyTotal = 0.0;
	int32 EmptyEnemyFloors = 0;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const FEFCalystoDungeonGenerationContext Context = MakeContext(
			Seed, static_cast<int64>(Index % 100) + 1, static_cast<int64>(Index) + 1);
		FEFCalystoResolveContextV4 ResolveContext;
		FEFCalystoResolvedFloorIntentV4 Intent;
		FString Error;
		if (!BuildResolveContextV4(
				Context,
				Ecology,
				FEFCalystoDirectorIntentV4(),
				FEFCalystoFloorOutcomeV4(),
				false,
				FEFCalystoCompanionSnapshotV4(),
				ResolveContext,
				Error)
			|| !FEFCalystoDungeonDirectorResolverV4::Resolve(
				CachedDirectorPolicyV4, ResolveContext, Intent, Error))
		{
			return FString::Printf(TEXT("Sample failed at %d: %s"), Index, *Error);
		}
		++StyleCounts.FindOrAdd(Intent.Style);
		++ThemeCounts.FindOrAdd(Intent.Theme);
		SizeTotal += Intent.DungeonSize.X;
		const FEFCalystoResolvedCategoryV4* EnemyCategory = Intent.Categories.FindByPredicate(
			[](const FEFCalystoResolvedCategoryV4& Category)
			{
				return Category.Category == EEFCalystoContentCategoryV4::Enemy;
			});
		const int32 EnemyCount = EnemyCategory ? EnemyCategory->TargetCount : 0;
		EnemyTotal += EnemyCount;
		EmptyEnemyFloors += EnemyCount == 0 ? 1 : 0;
	}
	return FString::Printf(
		TEXT("V4 Samples=%d Styles(Standard=%d Compact=%d Branching=%d) Themes(Default=%d Forge=%d Shrine=%d) MeanSize=%.3f MeanEnemies=%.3f EmptyEnemyFloors=%d"),
		SampleCount,
		StyleCounts.FindRef(EEFCalystoStyleV4::Standard),
		StyleCounts.FindRef(EEFCalystoStyleV4::Compact),
		StyleCounts.FindRef(EEFCalystoStyleV4::Branching),
		ThemeCounts.FindRef(EEFCalystoThemeV4::Default),
		ThemeCounts.FindRef(EEFCalystoThemeV4::Forge),
		ThemeCounts.FindRef(EEFCalystoThemeV4::Shrine),
		SizeTotal / SampleCount,
		EnemyTotal / SampleCount,
		EmptyEnemyFloors);
#endif
}

#if !UE_BUILD_SHIPPING
bool UEFCalystoDungeonSubsystem::SetDevelopmentCandidateValidatedDungeonSizesForAutomation(
	const TArray<int32>& CandidateSizes,
	FString& OutCandidatePolicyHash,
	FString& OutError)
{
	OutCandidatePolicyHash.Reset();
	OutError.Reset();
	const UWorld* World = GetWorld();
	if (!FApp::IsUnattended() || !World || World->WorldType != EWorldType::PIE)
	{
		OutError = TEXT("Candidate policies are restricted to unattended PIE.");
		return false;
	}
	if (CandidateSizes.IsEmpty())
	{
		OutError = TEXT("Candidate policy requires at least one dungeon size.");
		return false;
	}

	TArray<int32> SortedCandidateSizes = CandidateSizes;
	SortedCandidateSizes.Sort();
	for (int32 Index = 0; Index < SortedCandidateSizes.Num(); ++Index)
	{
		if (SortedCandidateSizes[Index] < 18 || SortedCandidateSizes[Index] > 30
			|| (Index > 0 && SortedCandidateSizes[Index] == SortedCandidateSizes[Index - 1]))
		{
			OutError = FString::Printf(TEXT("Candidate dungeon size %d is out of range or duplicated."),
				SortedCandidateSizes[Index]);
			return false;
		}
	}
	if (bUsingDevelopmentCandidatePolicyForAutomation)
	{
		if (bPolicyValid
			&& DevelopmentCandidateValidatedDungeonSizesForAutomation == SortedCandidateSizes
			&& CompiledPolicyHash.Len() == 64)
		{
			OutCandidatePolicyHash = CompiledPolicyHash;
			return true;
		}
		OutError = TEXT("A different candidate policy is already armed; one matrix session accepts exactly one candidate hash.");
		return false;
	}
	if (!DevelopmentPopulationScenarioForAutomation.IsNone())
	{
		OutError = TEXT("A population acceptance scenario is already armed; candidate-size and population policy clones are mutually exclusive.");
		return false;
	}
	if (bHasActiveRun || bTravelRequestPending || bTravelPreparationInProgress
		|| TravelState == EEFCalystoDungeonTravelState::AwaitingFloorReady)
	{
		OutError = TEXT("Candidate policy must be armed before the first New Run and outside travel.");
		return false;
	}

	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UEFCalystoDungeonDirectorPolicyV4* SourcePolicy = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	if (!SourcePolicy)
	{
		OutError = TEXT("The authored V4 source policy could not be loaded.");
		return false;
	}

	UEFCalystoDungeonDirectorPolicyV4* CandidatePolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicyV4>(
		SourcePolicy, this);
	if (!CandidatePolicy)
	{
		OutError = TEXT("The transient V4 candidate policy clone could not be created.");
		return false;
	}
	CandidatePolicy->SetFlags(RF_Transient);
	CandidatePolicy->ValidatedDungeonSizes = SortedCandidateSizes;
	if (!FEFCalystoDungeonDirectorResolverV4::Validate(CandidatePolicy, OutError))
	{
		return false;
	}
	const FString CandidatePolicyHash =
		FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(CandidatePolicy);
	if (CandidatePolicyHash.Len() != 64)
	{
		OutError = TEXT("The transient V4 candidate policy produced an invalid SHA-256.");
		return false;
	}

	CachedDirectorPolicyV4 = CandidatePolicy;
	CompiledPolicyHash = CandidatePolicyHash;
	bPolicyCompilationAttempted = true;
	bPolicyValid = true;
	PolicyError.Reset();
	bUsingDevelopmentCandidatePolicyForAutomation = true;
	DevelopmentCandidateValidatedDungeonSizesForAutomation = MoveTemp(SortedCandidateSizes);
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	OutCandidatePolicyHash = CandidatePolicyHash;
	return true;
}

bool UEFCalystoDungeonSubsystem::ClearDevelopmentCandidateValidatedDungeonSizesForAutomation(FString& OutError)
{
	OutError.Reset();
	const UWorld* World = GetWorld();
	if (!FApp::IsUnattended() || !World || World->WorldType != EWorldType::PIE)
	{
		OutError = TEXT("Candidate policies are restricted to unattended PIE.");
		return false;
	}
	if (bTravelRequestPending || bTravelPreparationInProgress
		|| TravelState == EEFCalystoDungeonTravelState::AwaitingFloorReady)
	{
		OutError = TEXT("Candidate policy cannot be cleared during travel or floor generation.");
		return false;
	}
	if (!DevelopmentPopulationScenarioForAutomation.IsNone())
	{
		OutError = TEXT("The candidate-size policy cannot be cleared while a population acceptance scenario is armed.");
		return false;
	}

	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	CachedDirectorPolicyV4 = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	bPolicyCompilationAttempted = true;
	bPolicyValid = FEFCalystoDungeonDirectorResolverV4::Validate(CachedDirectorPolicyV4, PolicyError);
	CompiledPolicyHash = bPolicyValid
		? FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(CachedDirectorPolicyV4)
		: FString();
	bUsingDevelopmentCandidatePolicyForAutomation = false;
	DevelopmentCandidateValidatedDungeonSizesForAutomation.Reset();
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Development automation cleared the transient candidate policy; sourceValid=%s hash=%s."),
		bPolicyValid ? TEXT("true") : TEXT("false"), *CompiledPolicyHash);
	return true;
}

bool UEFCalystoDungeonSubsystem::SetDevelopmentForcedDungeonEdgeForAutomation(
	const int32 DungeonEdge,
	const bool bAllowPackagedGameAcceptance)
{
	const UWorld* World = GetWorld();
	const bool bAllowedWorld = World && (World->WorldType == EWorldType::PIE
		|| (bAllowPackagedGameAcceptance
			&& EFCalystoDungeonAutomationPrivate::IsPackagedGameAcceptanceWorld(World)));
	if (!FApp::IsUnattended() || !bAllowedWorld
		|| !bPolicyValid || !CachedDirectorPolicyV4
		|| DungeonEdge < 18 || DungeonEdge > 30
		|| bTravelRequestPending || bTravelPreparationInProgress
		|| TravelState == EEFCalystoDungeonTravelState::AwaitingFloorReady)
	{
		return false;
	}
	DevelopmentForcedDungeonEdgeForAutomation = DungeonEdge;
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Development automation armed exact dungeon edge=%d for subsequent non-replay intent resolution."),
		DungeonEdge);
	return true;
}

void UEFCalystoDungeonSubsystem::ClearDevelopmentForcedDungeonEdgeForAutomation()
{
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	UE_LOG(LogEFCalystoDungeon, Log, TEXT("Development automation cleared exact dungeon edge override."));
}

#if 0 // Retired V3 automation implementation; kept only until the V4 cutover evidence is complete.
bool UEFCalystoDungeonSubsystem::SetDevelopmentPopulationScenarioForAutomation(
	const FName Scenario,
	FString& OutScenarioPolicyHash,
	FString& OutError,
	const bool bAllowPackagedGameAcceptance)
{
	OutScenarioPolicyHash.Reset();
	OutError.Reset();
	const UWorld* World = GetWorld();
	const bool bAllowedWorld = World && (World->WorldType == EWorldType::PIE
		|| (bAllowPackagedGameAcceptance
			&& EFCalystoDungeonAutomationPrivate::IsPackagedGameAcceptanceWorld(World)));
	if (!FApp::IsUnattended() || !bAllowedWorld)
	{
		OutError = TEXT("Population acceptance scenarios are restricted to unattended PIE or the explicit packaged Development acceptance runner.");
		return false;
	}
	using namespace EFCalystoDungeonAutomationPrivate;
	if (Scenario != PopulationScenarioZero && Scenario != PopulationScenarioEnemyCap25
		&& Scenario != PopulationScenarioResourceMax && Scenario != PopulationScenarioResourceMin)
	{
		OutError = FString::Printf(TEXT("Unknown population acceptance scenario '%s'."), *Scenario.ToString());
		return false;
	}
	if (!DevelopmentPopulationScenarioForAutomation.IsNone())
	{
		if (bPolicyValid && DevelopmentPopulationScenarioForAutomation == Scenario
			&& CompiledPolicyHash.Len() == 64)
		{
			OutScenarioPolicyHash = CompiledPolicyHash;
			return true;
		}
		OutError = TEXT("A different population acceptance scenario is already armed; clear it before selecting another.");
		return false;
	}
	if (bUsingDevelopmentCandidatePolicyForAutomation)
	{
		OutError = TEXT("Candidate-size and population policy clones are mutually exclusive.");
		return false;
	}
	if (bHasActiveRun || bTravelRequestPending || bOpenLevelIssued || bTravelPreparationInProgress
		|| TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Idle)
	{
		OutError = TEXT("Population acceptance scenario must be armed before the first New Run and outside travel/generation.");
		return false;
	}

	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UEFCalystoDungeonDirectorPolicy* SourcePolicy = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	FString SourceError;
	if (!ValidateDirectorPolicy(SourcePolicy, SourceError))
	{
		OutError = FString::Printf(TEXT("The authored V3 source policy is invalid: %s"), *SourceError);
		return false;
	}

	UEFCalystoDungeonDirectorPolicy* ScenarioPolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(
		SourcePolicy, this);
	if (!ScenarioPolicy)
	{
		OutError = TEXT("The transient V3 population policy clone could not be created.");
		return false;
	}
	ScenarioPolicy->ClearFlags(RF_Public | RF_Standalone);
	ScenarioPolicy->SetFlags(RF_Transient);

	// Every acceptance scenario isolates the requested categories. No unrelated
	// loot or special-event roll may consume an anchor or the 36-actor budget.
	ScenarioPolicy->Progression.StartLootPresence = 0.0f;
	ScenarioPolicy->Progression.EndLootPresence = 0.0f;
	ScenarioPolicy->Progression.StartSpecialEventPresence = 0.0f;
	ScenarioPolicy->Progression.EndSpecialEventPresence = 0.0f;
	ScenarioPolicy->Limits.MaxEnemies = 25;
	ScenarioPolicy->Limits.MaxFood = 8;
	ScenarioPolicy->Limits.MaxChests = 3;
	ScenarioPolicy->Limits.MaxDirectorActors = 36;
	ScenarioPolicy->Ecology.FoodPityAfterEmptyFloors = MAX_int32;
	ScenarioPolicy->Ecology.ChestPityAfterEmptyFloors = MAX_int32;

	if (Scenario == PopulationScenarioEnemyCap25)
	{
		ScenarioPolicy->Progression.StartEnemyPresence = 1.0f;
		ScenarioPolicy->Progression.EndEnemyPresence = 1.0f;
		ScenarioPolicy->Progression.StartEnemyCountMode = 25.0f;
		ScenarioPolicy->Progression.EndEnemyCountMode = 25.0f;
		ScenarioPolicy->Progression.EnemyCountLowerOffset = 0.0f;
		ScenarioPolicy->Progression.EnemyCountUpperOffset = 0.0f;
		ScenarioPolicy->Progression.StartThreatBudget = 25.0f;
		ScenarioPolicy->Progression.EndThreatBudget = 25.0f;
		ScenarioPolicy->Progression.ThreatBudgetRelativeRange = 0.0f;
		ScenarioPolicy->Progression.PacingAmplitude = 0.0f;
		ScenarioPolicy->Adaptation.MaximumInfluence = 0.0f;
		ScenarioPolicy->Progression.StartFoodPresence = 0.0f;
		ScenarioPolicy->Progression.EndFoodPresence = 0.0f;
		ScenarioPolicy->Progression.StartChestPresence = 0.0f;
		ScenarioPolicy->Progression.EndChestPresence = 0.0f;
		for (FEFCalystoStylePolicy& Style : ScenarioPolicy->Styles)
		{
			// ResolveTrait is in [-1,1], so this guarantees a non-negative final
			// threat trait and therefore clamps the authored mode to exactly 25.
			Style.ThreatBias = 1.0f;
		}
		for (FEFCalystoPopulationCatalogEntry& Entry : ScenarioPolicy->EnemyCatalog)
		{
			if (Entry.bEnabled)
			{
				Entry.Cost = 1.0f;
				Entry.MinimumFloor = 1;
				Entry.MaxPerFloor = 25;
				Entry.CooldownFloors = 0;
			}
		}
	}
	else
	{
		ScenarioPolicy->Progression.StartEnemyPresence = 0.0f;
		ScenarioPolicy->Progression.EndEnemyPresence = 0.0f;
		if (Scenario == PopulationScenarioResourceMax)
		{
			ScenarioPolicy->Progression.StartFoodPresence = 1.0f;
			ScenarioPolicy->Progression.EndFoodPresence = 1.0f;
			ScenarioPolicy->Progression.StartChestPresence = 1.0f;
			ScenarioPolicy->Progression.EndChestPresence = 1.0f;
			ScenarioPolicy->Progression.FoodCount.Min = 8;
			ScenarioPolicy->Progression.FoodCount.Mode = 8;
			ScenarioPolicy->Progression.FoodCount.Max = 8;
			ScenarioPolicy->Progression.ChestCount.Min = 3;
			ScenarioPolicy->Progression.ChestCount.Mode = 3;
			ScenarioPolicy->Progression.ChestCount.Max = 3;
			for (FEFCalystoPopulationCatalogEntry& Entry : ScenarioPolicy->FoodCatalog)
			{
				if (Entry.bEnabled)
				{
					Entry.Cost = 1.0f;
					Entry.MinimumFloor = 1;
					Entry.MaxPerFloor = 8;
					Entry.CooldownFloors = 0;
				}
			}
			for (FEFCalystoPopulationCatalogEntry& Entry : ScenarioPolicy->ChestCatalog)
			{
				if (Entry.bEnabled)
				{
					Entry.Cost = 1.0f;
					Entry.MinimumFloor = 1;
					Entry.MaxPerFloor = 3;
					Entry.CooldownFloors = 0;
				}
			}
		}
		else
		{
			// Zero and ResourceMin deliberately share the same exact 0/0/0
			// population contract while retaining distinct automation labels.
			ScenarioPolicy->Progression.StartFoodPresence = 0.0f;
			ScenarioPolicy->Progression.EndFoodPresence = 0.0f;
			ScenarioPolicy->Progression.StartChestPresence = 0.0f;
			ScenarioPolicy->Progression.EndChestPresence = 0.0f;
		}
	}

	FString ScenarioValidationError;
	if (!ValidateDirectorPolicy(ScenarioPolicy, ScenarioValidationError))
	{
		OutError = FString::Printf(TEXT("Transient population scenario %s failed validation: %s"),
			*Scenario.ToString(), *ScenarioValidationError);
		return false;
	}
	const FString ScenarioPolicyHash = ComputePolicyHash(ScenarioPolicy);
	if (ScenarioPolicyHash.Len() != 64)
	{
		OutError = TEXT("The transient V3 population scenario produced an invalid SHA-256.");
		return false;
	}

	CachedDirectorPolicy = ScenarioPolicy;
	CompiledPolicyHash = ScenarioPolicyHash;
	bPolicyCompilationAttempted = true;
	bPolicyValid = true;
	PolicyError.Reset();
	QueuedDirectorIntent = FEFCalystoDirectorIntent();
	bHasQueuedDirectorIntent = false;
	DevelopmentPopulationScenarioForAutomation = Scenario;
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	OutScenarioPolicyHash = ScenarioPolicyHash;
	return true;
}

bool UEFCalystoDungeonSubsystem::ClearDevelopmentPopulationScenarioForAutomation(FString& OutError)
{
	OutError.Reset();
	const UWorld* World = GetWorld();
	if (!FApp::IsUnattended() || !World || World->WorldType != EWorldType::PIE)
	{
		OutError = TEXT("Population acceptance scenarios are restricted to unattended PIE.");
		return false;
	}
	if (bUsingDevelopmentCandidatePolicyForAutomation)
	{
		OutError = TEXT("The population scenario cannot be cleared while a candidate-size policy is armed.");
		return false;
	}
	if (bHasActiveRun || bTravelRequestPending || bOpenLevelIssued || bTravelPreparationInProgress
		|| TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Idle)
	{
		OutError = TEXT("Population acceptance scenario can only be cleared before a run and outside travel/generation.");
		return false;
	}

	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UEFCalystoDungeonDirectorPolicy* SourcePolicy = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	FString SourceError;
	if (!ValidateDirectorPolicy(SourcePolicy, SourceError))
	{
		OutError = FString::Printf(TEXT("The authored V3 source policy could not be restored: %s"), *SourceError);
		return false;
	}
	const FString SourcePolicyHash = ComputePolicyHash(SourcePolicy);
	if (SourcePolicyHash.Len() != 64)
	{
		OutError = TEXT("The restored authored V3 policy produced an invalid SHA-256.");
		return false;
	}

	CachedDirectorPolicy = SourcePolicy;
	CompiledPolicyHash = SourcePolicyHash;
	bPolicyCompilationAttempted = true;
	bPolicyValid = true;
	PolicyError.Reset();
	DevelopmentPopulationScenarioForAutomation = NAME_None;
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Development automation cleared the transient population scenario; source hash=%s."),
		*CompiledPolicyHash);
	return true;
}
#endif

bool UEFCalystoDungeonSubsystem::SetDevelopmentPopulationScenarioForAutomation(
	const FName Scenario,
	FString& OutScenarioPolicyHash,
	FString& OutError,
	const bool bAllowPackagedGameAcceptance)
{
	OutScenarioPolicyHash.Reset();
	OutError.Reset();
	const UWorld* World = GetWorld();
	const bool bAllowedWorld = World && (World->WorldType == EWorldType::PIE
		|| (bAllowPackagedGameAcceptance
			&& EFCalystoDungeonAutomationPrivate::IsPackagedGameAcceptanceWorld(World)));
	using namespace EFCalystoDungeonAutomationPrivate;
	if (!FApp::IsUnattended() || !bAllowedWorld || !IsKnownPopulationScenario(Scenario))
	{
		OutError = TEXT("V4 population scenarios require unattended Development PIE or its explicit packaged runner.");
		return false;
	}
	if (bHasActiveRun || bTravelRequestPending || bOpenLevelIssued || bTravelPreparationInProgress
		|| TravelState != EEFCalystoDungeonTravelState::Idle
		|| GenerationState != EEFCalystoGenerationState::Idle
		|| bUsingDevelopmentCandidatePolicyForAutomation)
	{
		OutError = TEXT("V4 population scenarios must be armed before New Run and outside every other automation clone.");
		return false;
	}
	if (!DevelopmentPopulationScenarioForAutomation.IsNone())
	{
		if (DevelopmentPopulationScenarioForAutomation == Scenario && bPolicyValid)
		{
			OutScenarioPolicyHash = CompiledPolicyHash;
			return true;
		}
		OutError = TEXT("A different V4 population scenario is already armed.");
		return false;
	}

	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UEFCalystoDungeonDirectorPolicyV4* SourcePolicy = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	FString ValidationError;
	if (!FEFCalystoDungeonDirectorResolverV4::Validate(SourcePolicy, ValidationError))
	{
		OutError = FString::Printf(TEXT("The authored V4 policy is invalid: %s"), *ValidationError);
		return false;
	}
	if (Scenario == PopulationScenarioSpecialEvents6)
	{
		auto HasEmptyAuthoredSpecialEvents = [](const TArray<FEFCalystoCategoryProfileV4>& Categories)
		{
			const FEFCalystoCategoryProfileV4* Category =
				UEFCalystoDungeonDirectorPolicyV4::FindCategory(
					Categories, EEFCalystoContentCategoryV4::SpecialEvent);
			return Category
				&& FMath::IsNearlyZero(Category->Chance.ChanceAtFloor1)
				&& FMath::IsNearlyZero(Category->Chance.ChanceAtFloor100)
				&& Category->Catalog.IsEmpty();
		};
		for (const FEFCalystoStyleProfileV4& Style : SourcePolicy->Styles)
		{
			if (!HasEmptyAuthoredSpecialEvents(Style.Categories))
			{
				OutError = TEXT("SpecialEvents6 requires every authored Style Special Event chance and catalog to remain empty.");
				return false;
			}
		}
		for (const FEFCalystoThemeProfileV4& Theme : SourcePolicy->Themes)
		{
			if (!HasEmptyAuthoredSpecialEvents(Theme.Categories))
			{
				OutError = TEXT("SpecialEvents6 requires every authored Theme Special Event chance and catalog to remain empty.");
				return false;
			}
		}
	}
	UEFCalystoDungeonDirectorPolicyV4* ScenarioPolicy =
		DuplicateObject<UEFCalystoDungeonDirectorPolicyV4>(SourcePolicy, this);
	if (!ScenarioPolicy)
	{
		OutError = TEXT("The transient V4 population policy clone could not be created.");
		return false;
	}
	ScenarioPolicy->ClearFlags(RF_Public | RF_Standalone);
	ScenarioPolicy->SetFlags(RF_Transient);

	FName SelectedNPCVariantId;
	const bool bNPCVariantScenario = TryGetNPCVariantCatalogId(Scenario, SelectedNPCVariantId);
	const bool bNPCTotal4 = Scenario == PopulationScenarioNPCTotal4;
	const bool bSpecialEvents6 = Scenario == PopulationScenarioSpecialEvents6;
	const bool bCompanionRecallLifecycle =
		Scenario == PopulationScenarioCompanionRecallLifecycle;
	auto ConfigureCategories = [Scenario, bNPCVariantScenario, bNPCTotal4, bSpecialEvents6,
		bCompanionRecallLifecycle, SelectedNPCVariantId](
		TArray<FEFCalystoCategoryProfileV4>& Categories)
	{
		for (FEFCalystoCategoryProfileV4& Category : Categories)
		{
			if (Category.Category == EEFCalystoContentCategoryV4::Decoration
				|| Category.Category == EEFCalystoContentCategoryV4::Lighting)
			{
				continue;
			}
			const bool bEnemy = Category.Category == EEFCalystoContentCategoryV4::Enemy;
			const bool bNPC = Category.Category == EEFCalystoContentCategoryV4::NPC;
			const bool bFood = Category.Category == EEFCalystoContentCategoryV4::Food;
			const bool bChest = Category.Category == EEFCalystoContentCategoryV4::Chest;
			const bool bSpecialEvent =
				Category.Category == EEFCalystoContentCategoryV4::SpecialEvent;
			const bool bEnabled = Scenario == PopulationScenarioEnemyCap25
				? bEnemy
				: (Scenario == PopulationScenarioResourceMax && (bFood || bChest))
					|| (bNPCVariantScenario && bNPC)
					|| (bNPCTotal4 && bNPC)
					|| (bSpecialEvents6 && bSpecialEvent)
					|| (bCompanionRecallLifecycle && (bNPC || bChest));
			Category.bEnabled = bEnabled;
			Category.bBlocked = !bEnabled;
			if (!bEnabled)
			{
				Category.Chance.ChanceAtFloor1 = 0.0f;
				Category.Chance.ChanceAtFloor100 = 0.0f;
				continue;
			}
			Category.Chance.ChanceAtFloor1 = 0.90f;
			Category.Chance.ChanceAtFloor100 = 0.90f;
			const int32 ExactCount = bEnemy ? 25
				: (bNPC ? (bNPCTotal4 ? 4 : 1)
					: (bFood ? 30
						: (bSpecialEvent ? 6
							: (bCompanionRecallLifecycle ? 1 : 10))));
			Category.Limits.MinimumWhenPresent = ExactCount;
			Category.Limits.MaximumPerFloor = ExactCount;
			Category.PityAfterEmptyFloors = 0;
			if (bSpecialEvents6 && bSpecialEvent)
			{
				// The Primary Data Asset deliberately authors no Special Events.
				// Only this transient Development clone receives a harmless native
				// probe and a valid Common/Nothing tier vector for the cap gate.
				Category.Catalog.Reset();
				FEFCalystoCatalogEntryV4& Probe = Category.Catalog.AddDefaulted_GetRef();
				Probe.Name = TEXT("Development Special Event Probe");
				Probe.StableId = FName(TEXT("Event.Automation.SpecialEventProbe"));
				Probe.Rule = EEFCalystoCatalogRuleV4::Allow;
				Probe.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(
					TEXT("/Script/EFProceduralPCGRuntime.EFCalystoSpecialEventProbe")));
				Probe.Tier = EEFCalystoRarityTierV4::Common;
				Probe.bTierAgnostic = false;
				Probe.Archetype = FName(TEXT("AutomationProbe"));
				Probe.Gender = EEFCalystoGenderV4::Any;
				Probe.InitialFraction = 1.0f;
				Probe.DeepShare = 1.0f;
				Probe.RampFloors = 0;
				Probe.BaseThreatCost = 1.0f;
				Probe.ReferenceLevel = 1;
				Probe.FirstEligibleFloor = 1;
				Probe.MaxPerVariant = 6;
				Probe.CooldownFloors = 0;
				Probe.Lifecycle = EEFCalystoLifecycleV4::FloorLocal;
				Category.Tiers.AtFloor1 = FEFCalystoTierMixV4();
				Category.Tiers.AtFloor1.Common = 0.90f;
				Category.Tiers.AtFloor100 = Category.Tiers.AtFloor1;
				Category.Tiers.RefreshNothing();
			}
			for (FEFCalystoCatalogEntryV4& Entry : Category.Catalog)
			{
				if (bNPCVariantScenario && bNPC)
				{
					Entry.Rule = Entry.StableId == SelectedNPCVariantId
						? EEFCalystoCatalogRuleV4::Allow
						: EEFCalystoCatalogRuleV4::Block;
				}
				else if (bCompanionRecallLifecycle && bNPC)
				{
					const bool bLifecycleVariant =
						Entry.StableId == FName(TEXT("NPC.Companion.Generalist.Female"))
						|| Entry.StableId == FName(TEXT("NPC.Companion.Generalist.Male"));
					Entry.Rule = bLifecycleVariant
						? EEFCalystoCatalogRuleV4::Allow
						: EEFCalystoCatalogRuleV4::Block;
				}
				if (Entry.Rule == EEFCalystoCatalogRuleV4::Allow)
				{
					Entry.FirstEligibleFloor = 1;
					// Keep the acceptance fixture inside the immutable hard ceiling for
					// its own category (Enemy=25, Food=30, Chest=10). A blanket value
					// of 30 made the Enemy and Chest clones fail policy validation before
					// the runtime scenario could even begin.
					Entry.MaxPerVariant = ExactCount;
					Entry.CooldownFloors = 0;
					if (bEnemy && Scenario == PopulationScenarioEnemyCap25)
					{
						// The cap fixture validates placement/materialization of all
						// 25 actors. Keep its transient composition inside the immutable
						// 60-point ceiling instead of letting normal threat backtracking
						// turn this exact acceptance case into a smaller population.
						Entry.BaseThreatCost = 1.0f;
					}
				}
			}
			if (bCompanionRecallLifecycle && bChest)
			{
				Category.MinimumChestContentAttempts = 1;
				Category.MaximumChestContentAttempts = 1;
				for (FEFCalystoChestContentEntryV4& Content : Category.ChestContentsCatalog)
				{
					Content.Rule = Content.StableId ==
						FName(TEXT("Item.CompanionRevival.WintersRecall"))
						? EEFCalystoCatalogRuleV4::Allow
						: EEFCalystoCatalogRuleV4::Block;
				}
			}
		}
	};
	for (FEFCalystoStyleProfileV4& Style : ScenarioPolicy->Styles)
	{
		ConfigureCategories(Style.Categories);
		if (Scenario == PopulationScenarioEnemyCap25)
		{
			Style.Threat.EarlyBudget = ScenarioPolicy->SafetyCeilings.MaximumThreatBudget;
			Style.Threat.DeepBudget = ScenarioPolicy->SafetyCeilings.MaximumThreatBudget;
			Style.Threat.RelativeRange = 0.0f;
		}
	}
	for (FEFCalystoThemeProfileV4& Theme : ScenarioPolicy->Themes)
	{
		ConfigureCategories(Theme.Categories);
		if (Scenario == PopulationScenarioEnemyCap25)
		{
			Theme.Threat.EarlyBudget = ScenarioPolicy->SafetyCeilings.MaximumThreatBudget;
			Theme.Threat.DeepBudget = ScenarioPolicy->SafetyCeilings.MaximumThreatBudget;
			Theme.Threat.RelativeRange = 0.0f;
		}
	}
	if (!FEFCalystoDungeonDirectorResolverV4::Validate(ScenarioPolicy, ValidationError))
	{
		OutError = FString::Printf(TEXT("Transient V4 scenario %s failed validation: %s"),
			*Scenario.ToString(), *ValidationError);
		return false;
	}
	const FString ScenarioHash =
		FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(ScenarioPolicy);
	if (ScenarioHash.Len() != 64)
	{
		OutError = TEXT("The transient V4 scenario produced an invalid SHA-256.");
		return false;
	}
	CachedDirectorPolicyV4 = ScenarioPolicy;
	CompiledPolicyHash = ScenarioHash;
	bPolicyCompilationAttempted = true;
	bPolicyValid = true;
	PolicyError.Reset();
	QueuedDirectorIntentV4 = FEFCalystoDirectorIntentV4();
	bHasQueuedDirectorIntent = false;
	DevelopmentPopulationScenarioForAutomation = Scenario;
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	OutScenarioPolicyHash = ScenarioHash;
	return true;
}

bool UEFCalystoDungeonSubsystem::ClearDevelopmentPopulationScenarioForAutomation(FString& OutError)
{
	OutError.Reset();
	const UWorld* World = GetWorld();
	if (!FApp::IsUnattended() || !World || World->WorldType != EWorldType::PIE
		|| bHasActiveRun || bTravelRequestPending || bOpenLevelIssued || bTravelPreparationInProgress
		|| bUsingDevelopmentCandidatePolicyForAutomation)
	{
		OutError = TEXT("The V4 population scenario cannot be cleared in the current state.");
		return false;
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	CachedDirectorPolicyV4 = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	bPolicyValid = FEFCalystoDungeonDirectorResolverV4::Validate(CachedDirectorPolicyV4, PolicyError);
	CompiledPolicyHash = bPolicyValid
		? FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(CachedDirectorPolicyV4)
		: FString();
	bPolicyCompilationAttempted = true;
	DevelopmentPopulationScenarioForAutomation = NAME_None;
	DevelopmentForcedDungeonEdgeForAutomation = 0;
	return bPolicyValid;
}
#endif

FOnEFCalystoFloorCompleted& UEFCalystoDungeonSubsystem::OnFloorCompleted() { return FloorCompletedEvent; }
FOnEFCalystoBeforeFloorAdvance& UEFCalystoDungeonSubsystem::OnBeforeFloorAdvance() { return BeforeFloorAdvanceEvent; }
FOnEFCalystoFloorReady& UEFCalystoDungeonSubsystem::OnFloorReady() { return FloorReadyEvent; }
FOnEFCalystoFloorTravelFailed& UEFCalystoDungeonSubsystem::OnFloorTravelFailed() { return FloorTravelFailedEvent; }
FOnEFCalystoBeforeAnyDirectorTravel& UEFCalystoDungeonSubsystem::OnBeforeAnyDirectorTravel() { return BeforeAnyDirectorTravelEvent; }
FOnEFCalystoDirectorWorldAccepted& UEFCalystoDungeonSubsystem::OnDirectorWorldAccepted() { return DirectorWorldAcceptedEvent; }
FOnEFCalystoNewRunInitialized& UEFCalystoDungeonSubsystem::OnNewRunInitialized() { return NewRunInitializedEvent; }

bool UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(
	const UEFCalystoDungeonDirectorPolicy* Policy,
	FString& OutError)
{
	if (!Policy)
	{
		OutError = TEXT("Dungeon Director V3 policy is missing.");
		return false;
	}
	return Policy->Validate(OutError);
}

FString UEFCalystoDungeonSubsystem::ComputePolicyHash(const UEFCalystoDungeonDirectorPolicy* Policy)
{
	FString Error;
	return ValidateDirectorPolicy(Policy, Error)
		? ComputeCanonicalHash(Policy->BuildCanonicalString())
		: FString();
}

FString UEFCalystoDungeonSubsystem::ComputeCanonicalHash(const FString& Text)
{
	return EFCalystoDungeonPrivate::HashText(Text);
}

FString UEFCalystoDungeonSubsystem::ComputeEcologyHash(const FEFCalystoRunEcologyState& Ecology)
{
	return EFCalystoDungeonPrivate::BuildEcologyHash(Ecology);
}

FString UEFCalystoDungeonSubsystem::ComputeEcologyHashV4(
	const FEFCalystoRunEcologyStateV4& Ecology)
{
	return EFCalystoDungeonPrivate::BuildEcologyHashV4(Ecology);
}

FEFCalystoRunEcologyStateV4 UEFCalystoDungeonSubsystem::BuildInitialEcologyV4(
	const int64 RunSeed,
	const FString& PolicyHash,
	const int32 GeneratorVersion)
{
	FEFCalystoRunEcologyStateV4 Ecology;
	if (RunSeed <= 0 || PolicyHash.IsEmpty() || GeneratorVersion != 4)
	{
		return Ecology;
	}

	auto MakeDNA = [RunSeed, &PolicyHash](const TCHAR* TraitName)
	{
		const uint64 TraitId = FEFCalystoDungeonDirectorMathV4::StableNameId(FName(TraitName));
		return EFCalystoDungeonPrivate::SignedUnitFromCounter(
			FEFCalystoDungeonDirectorMathV4::DeriveEcologyCounterValue(
				RunSeed,
				0,
				0,
				PolicyHash,
				EFCalystoDungeonDomainsV4::RunDNA,
				TraitId));
	};
	Ecology.RunDNATraits.Scale = MakeDNA(TEXT("Scale"));
	Ecology.RunDNATraits.Branching = MakeDNA(TEXT("Branching"));
	Ecology.RunDNATraits.Danger = MakeDNA(TEXT("Danger"));
	Ecology.RunDNATraits.Safe = MakeDNA(TEXT("Safe"));
	Ecology.RunDNATraits.Abundance = MakeDNA(TEXT("Abundance"));
	Ecology.RunDNATraits.Mystery = MakeDNA(TEXT("Mystery"));
	Ecology.RunDNATraits.ClothingInfluence = MakeDNA(TEXT("Clothing"));
	// Volatility is authored by the selected Style/Theme pair. Run DNA does not
	// silently introduce a second global volatility authority.
	Ecology.RunDNATraits.Volatility = 0.5f;
	Ecology.bInitialized = true;
	Ecology.RunDNAHash = ComputeCanonicalHash(FString::Printf(
		TEXT("EFCalystoRunDNAV4|%s|%s|%s|%s|%s|%s|%s|%s"),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.Scale),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.Branching),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.Danger),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.Safe),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.Abundance),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.Mystery),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.RunDNATraits.ClothingInfluence),
		*PolicyHash));
	Ecology.PerformanceEMA = 0.5f;
	Ecology.EcologyHash = ComputeEcologyHashV4(Ecology);
	return Ecology;
}

bool UEFCalystoDungeonSubsystem::BuildResolveContextV4(
	const FEFCalystoDungeonGenerationContext& TargetContext,
	const FEFCalystoRunEcologyStateV4& Ecology,
	const FEFCalystoDirectorIntentV4& DirectorIntent,
	const FEFCalystoFloorOutcomeV4& FrozenOutcome,
	const bool bHasFrozenOutcome,
	const FEFCalystoCompanionSnapshotV4& CompanionSnapshot,
	FEFCalystoResolveContextV4& OutContext,
	FString& OutError) const
{
	OutContext = FEFCalystoResolveContextV4();
	OutError.Reset();
	if (!CachedDirectorPolicyV4 || !bPolicyValid || !TargetContext.IsValid()
		|| TargetContext.PolicyHash != CompiledPolicyHash
		|| !Ecology.bInitialized || Ecology.RunDNAHash.IsEmpty()
		|| Ecology.EcologyHash.IsEmpty()
		|| Ecology.EcologyHash != ComputeEcologyHashV4(Ecology)
		|| !EFCalystoDungeonPrivate::IsValidDirectorIntent(DirectorIntent)
		|| (bHasFrozenOutcome && !EFCalystoDungeonPrivate::IsValidOutcome(FrozenOutcome))
		|| TargetContext.FloorNumber > static_cast<int64>(MAX_int32) - 6)
	{
		OutError = TEXT("Invalid V4 policy, context, ecology, director intent or frozen outcome.");
		return false;
	}

	const FString CompanionHash =
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(CompanionSnapshot);
	if (CompanionHash.IsEmpty())
	{
		OutError = TEXT("The V4 companion snapshot could not be hashed canonically.");
		return false;
	}

	OutContext.RunSeed = TargetContext.RunSeed;
	OutContext.FloorNumber = TargetContext.FloorNumber;
	OutContext.GenerationSerial = TargetContext.GenerationSerial;
	OutContext.EcologyHash = Ecology.EcologyHash;
	OutContext.DirectorIntent = DirectorIntent;
	OutContext.PerformanceEMA = Ecology.PerformanceEMA;
	OutContext.bHasFrozenOutcome = bHasFrozenOutcome;
	OutContext.FrozenOutcome = FrozenOutcome;
	OutContext.CompanionSnapshot = CompanionSnapshot;
	OutContext.CompanionSnapshotHash = CompanionHash;
#if !UE_BUILD_SHIPPING
	OutContext.DevelopmentForcedDungeonEdge = DevelopmentForcedDungeonEdgeForAutomation;
	OutContext.DevelopmentPopulationScenario = DevelopmentPopulationScenarioForAutomation;
#endif
	OutContext.MaximumConsecutiveStyle = 2;
	OutContext.MaximumConsecutiveTheme = 3;
	OutContext.ConsecutiveFloorsWithoutFood = Ecology.ConsecutiveFloorsWithoutFood;
	OutContext.ConsecutiveFloorsWithoutChest = Ecology.ConsecutiveFloorsWithoutChest;

	if (!Ecology.RecentStyles.IsEmpty())
	{
		OutContext.PreviousStyle = Ecology.RecentStyles.Last();
		for (int32 Index = Ecology.RecentStyles.Num() - 1; Index >= 0
			&& Ecology.RecentStyles[Index] == OutContext.PreviousStyle; --Index)
		{
			++OutContext.ConsecutiveStyleCount;
		}
	}
	if (!Ecology.RecentThemes.IsEmpty())
	{
		OutContext.PreviousTheme = Ecology.RecentThemes.Last();
		for (int32 Index = Ecology.RecentThemes.Num() - 1; Index >= 0
			&& Ecology.RecentThemes[Index] == OutContext.PreviousTheme; --Index)
		{
			++OutContext.ConsecutiveThemeCount;
		}
	}

	for (const FEFCalystoCooldownStateV4& Cooldown : Ecology.Cooldowns)
	{
		if (Cooldown.StableId.IsNone() || Cooldown.LastSelectedFloor <= 0
			|| Cooldown.CooldownFloors <= 0)
		{
			OutError = TEXT("V4 ecology contains an invalid frozen cooldown.");
			return false;
		}
		const int64 Delta = TargetContext.FloorNumber - Cooldown.LastSelectedFloor;
		if (Delta > 0 && Delta <= Cooldown.CooldownFloors)
		{
			OutContext.CooldownBlockedCatalogIds.AddUnique(Cooldown.StableId);
		}
	}
	OutContext.CooldownBlockedCatalogIds.Sort(FNameLexicalLess());

	constexpr int64 SmoothNoisePeriod = 6;
	const int64 ZeroBasedFloor = TargetContext.FloorNumber - 1;
	const int64 LatticeFloorA = (ZeroBasedFloor / SmoothNoisePeriod) * SmoothNoisePeriod + 1;
	const int64 LatticeFloorB = LatticeFloorA + SmoothNoisePeriod;
	const float Alpha = static_cast<float>(ZeroBasedFloor % SmoothNoisePeriod)
		/ static_cast<float>(SmoothNoisePeriod);
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	auto ResolveTrait = [&](const TCHAR* TraitName, const float RunDNA)
	{
		const uint64 TraitId = FEFCalystoDungeonDirectorMathV4::StableNameId(FName(TraitName));
		auto SmoothSample = [&](const int64 LatticeFloor)
		{
			return EFCalystoDungeonPrivate::SignedUnitFromCounter(
				FEFCalystoDungeonDirectorMathV4::DeriveEcologyCounterValue(
					TargetContext.RunSeed,
					LatticeFloor,
					1,
					CompiledPolicyHash,
					EFCalystoDungeonDomainsV4::SmoothFloorNoise,
					TraitId));
		};
		const float Smooth = FMath::Lerp(SmoothSample(LatticeFloorA), SmoothSample(LatticeFloorB), SmoothAlpha);
		const float Jitter = EFCalystoDungeonPrivate::SignedUnitFromCounter(
			FEFCalystoDungeonDirectorMathV4::DeriveEcologyCounterValue(
				TargetContext.RunSeed,
				TargetContext.FloorNumber,
				TargetContext.GenerationSerial,
				CompiledPolicyHash,
				EFCalystoDungeonDomainsV4::Jitter,
				TraitId));
		return FEFCalystoDungeonDirectorMathV4::ResolveEcologyTrait(RunDNA, Smooth, Jitter, 0.0f);
	};

	OutContext.ResolvedRunTraits.Scale = ResolveTrait(TEXT("Scale"), Ecology.RunDNATraits.Scale);
	OutContext.ResolvedRunTraits.Branching = ResolveTrait(TEXT("Branching"), Ecology.RunDNATraits.Branching);
	OutContext.ResolvedRunTraits.Danger = ResolveTrait(TEXT("Danger"), Ecology.RunDNATraits.Danger);
	OutContext.ResolvedRunTraits.Safe = ResolveTrait(TEXT("Safe"), Ecology.RunDNATraits.Safe);
	OutContext.ResolvedRunTraits.Abundance = ResolveTrait(TEXT("Abundance"), Ecology.RunDNATraits.Abundance);
	OutContext.ResolvedRunTraits.Mystery = ResolveTrait(TEXT("Mystery"), Ecology.RunDNATraits.Mystery);
	OutContext.ResolvedRunTraits.ClothingInfluence = ResolveTrait(
		TEXT("Clothing"), Ecology.RunDNATraits.ClothingInfluence);
	OutContext.ResolvedRunTraits.Volatility = Ecology.RunDNATraits.Volatility;
	return true;
}

bool UEFCalystoDungeonSubsystem::CommitAcceptedFloorToEcologyV4(
	const FEFCalystoFloorOutcomeV4& FrozenOutcome,
	FEFCalystoRunEcologyStateV4& InOutEcology,
	FString& OutError) const
{
	OutError.Reset();
	if (!CachedDirectorPolicyV4 || !HasActiveRun() || !ActiveManifestV4.bIsValid
		|| !EFCalystoDungeonPrivate::IsValidOutcome(FrozenOutcome)
		|| InOutEcology.EcologyHash.IsEmpty()
		|| InOutEcology.EcologyHash != ComputeEcologyHashV4(InOutEcology)
		|| ActiveIntentV4.EcologyHash != InOutEcology.EcologyHash
		|| ActiveManifestV4.IntentHash != ActiveIntentV4.IntentHash
		|| ActiveContext.FloorNumber <= InOutEcology.LastCommittedFloor
		|| ActiveContext.FloorNumber != ActiveIntentV4.FloorNumber
		|| ActiveContext.GenerationSerial != ActiveIntentV4.GenerationSerial
		|| !bHasSubmittedCompanionSnapshot)
	{
		OutError = TEXT("Cannot commit an invalid or already committed V4 floor/ecology tuple.");
		return false;
	}
	if (InOutEcology.EcologyRevision == MAX_int64)
	{
		OutError = TEXT("V4 ecology revision is exhausted.");
		return false;
	}

	FEFCalystoRunEcologyStateV4 Candidate = InOutEcology;
	Candidate.PerformanceEMA = FEFCalystoDungeonDirectorMathV4::UpdatePerformanceEMA(
		Candidate.PerformanceEMA, FrozenOutcome, 0.25f);
	Candidate.RecentStyles.Add(ActiveIntentV4.Style);
	while (Candidate.RecentStyles.Num() > 3)
	{
		Candidate.RecentStyles.RemoveAt(0);
	}
	Candidate.RecentThemes.Add(ActiveIntentV4.Theme);
	while (Candidate.RecentThemes.Num() > 4)
	{
		Candidate.RecentThemes.RemoveAt(0);
	}
	Candidate.ConsecutiveFloorsWithoutFood = ActiveManifestV4.FoodCount > 0
		? 0 : FMath::Min(Candidate.ConsecutiveFloorsWithoutFood, MAX_int32 - 1) + 1;
	Candidate.ConsecutiveFloorsWithoutChest = ActiveManifestV4.ChestCount > 0
		? 0 : FMath::Min(Candidate.ConsecutiveFloorsWithoutChest, MAX_int32 - 1) + 1;
	Candidate.CompanionSnapshot = SubmittedCompanionSnapshotV4;

	auto CommitCooldown = [&](const FName StableId, const int32 CooldownFloors)
	{
		if (StableId.IsNone() || CooldownFloors <= 0)
		{
			return;
		}
		FEFCalystoCooldownStateV4* Existing = Candidate.Cooldowns.FindByPredicate(
			[StableId](const FEFCalystoCooldownStateV4& Entry)
			{
				return Entry.StableId == StableId;
			});
		if (!Existing)
		{
			Existing = &Candidate.Cooldowns.AddDefaulted_GetRef();
			Existing->StableId = StableId;
		}
		Existing->LastSelectedFloor = ActiveContext.FloorNumber;
		Existing->CooldownFloors = FMath::Max(Existing->CooldownFloors, CooldownFloors);
	};
	for (const FEFCalystoRealizedInstanceV4& Instance : ActiveManifestV4.Instances)
	{
		CommitCooldown(Instance.CatalogId, Instance.CooldownFloors);
		for (const FEFCalystoChestContentDirectiveV4& Content : Instance.VerifiedChestContents)
		{
			CommitCooldown(Content.ContentCatalogId, Content.CooldownFloors);
		}
	}
	Candidate.Cooldowns.Sort([](const FEFCalystoCooldownStateV4& Left,
		const FEFCalystoCooldownStateV4& Right)
	{
		return Left.StableId.LexicalLess(Right.StableId);
	});
	Candidate.LastCommittedFloor = ActiveContext.FloorNumber;
	++Candidate.EcologyRevision;
	Candidate.EcologyHash = ComputeEcologyHashV4(Candidate);
	if (Candidate.EcologyHash.IsEmpty())
	{
		OutError = TEXT("Committed V4 ecology failed canonical validation/hash.");
		return false;
	}
	InOutEcology = MoveTemp(Candidate);
	return true;
}

FString UEFCalystoDungeonSubsystem::ComputeManifestHash(const FEFCalystoRealizedFloorManifest& Manifest)
{
	FString Canonical = FString::Printf(
		TEXT("EFCalystoManifestV3|%lld|%lld|%lld|%d|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s|%s|"),
		Manifest.RunSeed,
		Manifest.FloorNumber,
		Manifest.GenerationSerial,
		Manifest.PCGSeed,
		*Manifest.IntentHash,
		*Manifest.AnchorTopologyHash,
		*Manifest.PopulationHash,
		*Manifest.ResourceHash,
		Manifest.CandidateAnchorCount,
		Manifest.EnemyCount,
		Manifest.FoodCount,
		Manifest.ChestCount,
		Manifest.LootCount,
		Manifest.SpecialEventCount,
		Manifest.SpawnedActorCount,
		*EFCalystoDungeonPrivate::FloatBits(Manifest.RealizedThreatCost),
		*EFCalystoDungeonPrivate::FloatBits(Manifest.RealizedResourceCost));
	TArray<FEFCalystoSpawnDirective> Directives = Manifest.SpawnDirectives;
	Directives.Sort([](const FEFCalystoSpawnDirective& A, const FEFCalystoSpawnDirective& B)
	{
		if (A.Category != B.Category) { return static_cast<uint8>(A.Category) < static_cast<uint8>(B.Category); }
		return A.StableId.LexicalLess(B.StableId);
	});
	for (const FEFCalystoSpawnDirective& Directive : Directives)
	{
		Canonical += FString::Printf(TEXT("D:%s:%d:%s:%d:%s:%d|"), *Directive.StableId.ToString(),
			static_cast<int32>(Directive.Category), *Directive.ActorClass.ToSoftObjectPath().ToString(), Directive.Count,
			*EFCalystoDungeonPrivate::FloatBits(Directive.CostPerActor), Directive.RelativeWeight);
	}
	return EFCalystoDungeonPrivate::HashText(Canonical);
}

FString UEFCalystoDungeonSubsystem::ComputeManifestHashV4(
	const FEFCalystoRealizedFloorManifestV4& Manifest)
{
	if (Manifest.RunSeed <= 0 || Manifest.FloorNumber <= 0 || Manifest.GenerationSerial <= 0
		|| Manifest.IntentHash.IsEmpty() || Manifest.AnchorTopologyHash.IsEmpty()
		|| Manifest.PopulationHash.IsEmpty() || Manifest.ResourceHash.IsEmpty()
		|| Manifest.CompanionSnapshotHash.IsEmpty())
	{
		return FString();
	}

	FString Canonical = FString::Printf(
		TEXT("EFCalystoManifestV4|%lld|%lld|%lld|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s|%s|"),
		Manifest.RunSeed,
		Manifest.FloorNumber,
		Manifest.GenerationSerial,
		*Manifest.IntentHash,
		*Manifest.AnchorTopologyHash,
		*Manifest.PopulationHash,
		*Manifest.ResourceHash,
		*Manifest.CompanionSnapshotHash,
		Manifest.EnemyCount,
		Manifest.NPCCount,
		Manifest.FoodCount,
		Manifest.ChestCount,
		Manifest.LooseLootCount,
		Manifest.ClothingCount,
		Manifest.SpecialEventCount,
		Manifest.SpawnedActorCount,
		Manifest.CandidateAnchorCount,
		*EFCalystoDungeonPrivate::FloatBits(Manifest.RealizedThreatCost),
		*EFCalystoDungeonPrivate::FloatBits(Manifest.RealizedResourceCost));

	TArray<FEFCalystoRealizedInstanceV4> Instances = Manifest.Instances;
	Instances.Sort([](const FEFCalystoRealizedInstanceV4& Left, const FEFCalystoRealizedInstanceV4& Right)
	{
		return Left.StableInstanceId.LexicalLess(Right.StableInstanceId);
	});
	TSet<FName> SeenInstanceIds;
	for (const FEFCalystoRealizedInstanceV4& Instance : Instances)
	{
		if (Instance.StableInstanceId.IsNone() || SeenInstanceIds.Contains(Instance.StableInstanceId)
			|| Instance.CatalogId.IsNone() || !Instance.ActorClass.ToSoftObjectPath().IsValid()
			|| Instance.LogicalLevel < 0 || Instance.CooldownFloors < 0
			|| !FMath::IsFinite(Instance.EffectiveThreatCost)
			|| Instance.EffectiveThreatCost < 0.0f || !Instance.Transform.IsValid())
		{
			return FString();
		}
		SeenInstanceIds.Add(Instance.StableInstanceId);

		const FVector Translation = Instance.Transform.GetTranslation();
		const FRotator Rotation = Instance.Transform.Rotator();
		const FVector Scale3D = Instance.Transform.GetScale3D();
		TArray<FName> ContentIds = Instance.VerifiedChestContentIds;
		ContentIds.Sort([](const FName Left, const FName Right)
		{
			return Left.LexicalLess(Right);
		});
		FString ContentCanonical;
		for (const FName ContentId : ContentIds)
		{
			if (ContentId.IsNone())
			{
				return FString();
			}
			ContentCanonical += ContentId.ToString() + TEXT(",");
		}
		TArray<FEFCalystoChestContentDirectiveV4> VerifiedContents = Instance.VerifiedChestContents;
		VerifiedContents.Sort([](
			const FEFCalystoChestContentDirectiveV4& Left,
			const FEFCalystoChestContentDirectiveV4& Right)
		{
			if (Left.StableAttemptId != Right.StableAttemptId)
			{
				return Left.StableAttemptId.LexicalLess(Right.StableAttemptId);
			}
			if (Left.ContentCatalogId != Right.ContentCatalogId)
			{
				return Left.ContentCatalogId.LexicalLess(Right.ContentCatalogId);
			}
			return Left.ContentClass.ToSoftObjectPath().ToString()
				< Right.ContentClass.ToSoftObjectPath().ToString();
		});
		TSet<FName> SeenAttemptIds;
		for (const FEFCalystoChestContentDirectiveV4& Content : VerifiedContents)
		{
			if (Content.ContainerInstanceId != Instance.StableInstanceId
				|| Content.StableAttemptId.IsNone()
				|| SeenAttemptIds.Contains(Content.StableAttemptId)
				|| Content.ContentCatalogId.IsNone()
				|| !Content.ContentClass.ToSoftObjectPath().IsValid()
				|| Content.CooldownFloors < 0)
			{
				return FString();
			}
			SeenAttemptIds.Add(Content.StableAttemptId);
			ContentCanonical += FString::Printf(
				TEXT("P:%s:%s:%s:%d:%d|"),
				*Content.StableAttemptId.ToString(),
				*Content.ContentCatalogId.ToString(),
				*Content.ContentClass.ToSoftObjectPath().ToString(),
				static_cast<int32>(Content.Tier),
				Content.CooldownFloors);
		}

		Canonical += FString::Printf(
			TEXT("I:%s:%s:%s:%s:%s:%d:%d:%d:%s:%d:%d:%d:%s:T%lld,%lld,%lld:R%d,%d,%d:S%d,%d,%d:C%s|"),
			*Instance.StableInstanceId.ToString(),
			*Instance.StableCompanionId.ToString(EGuidFormats::Digits),
			*Instance.CatalogId.ToString(),
			*Instance.VariantId.ToString(),
			*Instance.Archetype.ToString(),
			static_cast<int32>(Instance.Gender),
			static_cast<int32>(Instance.Lifecycle),
			static_cast<int32>(Instance.Category),
			*Instance.ActorClass.ToSoftObjectPath().ToString(),
			static_cast<int32>(Instance.Tier),
			Instance.LogicalLevel,
			Instance.CooldownFloors,
			*EFCalystoDungeonPrivate::FloatBits(Instance.EffectiveThreatCost),
			FMath::RoundToInt64(Translation.X * 10.0),
			FMath::RoundToInt64(Translation.Y * 10.0),
			FMath::RoundToInt64(Translation.Z * 10.0),
			FMath::RoundToInt(Rotation.Roll * 100.0),
			FMath::RoundToInt(Rotation.Pitch * 100.0),
			FMath::RoundToInt(Rotation.Yaw * 100.0),
			FMath::RoundToInt(Scale3D.X * 1000.0),
			FMath::RoundToInt(Scale3D.Y * 1000.0),
			FMath::RoundToInt(Scale3D.Z * 1000.0),
			*ContentCanonical);
	}
	return EFCalystoDungeonPrivate::HashText(Canonical);
}

#if 0 // Retired V3 companion snapshot hash; V4 uses EFCalystoDirectorMathV4::BuildCompanionSnapshotHash.
FString UEFCalystoDungeonSubsystem::ComputeCompanionSnapshotHash(
	const FEFCalystoCompanionRunSnapshot& Snapshot)
{
	// Four is the per-floor NPC cap, not a lifetime graveyard cap. Keep a large
	// technical bound so corrupted snapshots fail closed without constraining design.
	if (Snapshot.RunEpoch <= 0 || Snapshot.Entries.Num() > 256 || Snapshot.ActiveParty.Num() > 2)
	{
		return FString();
	}

	TArray<FEFCalystoCompanionRunEntry> Entries = Snapshot.Entries;
	Entries.Sort([](const FEFCalystoCompanionRunEntry& Left, const FEFCalystoCompanionRunEntry& Right)
	{
		return Left.StableCompanionId.ToString(EGuidFormats::Digits)
			< Right.StableCompanionId.ToString(EGuidFormats::Digits);
	});
	TSet<FGuid> SeenIds;
	// RunEpoch identifies the session that owns the roster, but the V4 contract
	// deliberately excludes it from deterministic content identity and RNG.
	FString Canonical = FString::Printf(
		TEXT("EFCalystoCompanionSnapshotV4|OWNS_RECALL:%d|"),
		Snapshot.bPlayerOwnsWintersRecall ? 1 : 0);
	for (const FEFCalystoCompanionRunEntry& Entry : Entries)
	{
		const FSoftObjectPath ClassPath = Entry.ActorClass.ToSoftObjectPath();
		if (!Entry.StableCompanionId.IsValid() || SeenIds.Contains(Entry.StableCompanionId)
			|| Entry.ContentId.IsNone() || !ClassPath.IsValid() || Entry.Archetype.IsNone()
			|| Entry.Gender.IsNone() || Entry.DifficultyGrade.IsNone()
			|| Entry.ResolvedLogicalLevel <= 0 || Entry.DeathFloor < 0 || Entry.DeathGenerationSerial < 0)
		{
			return FString();
		}
		SeenIds.Add(Entry.StableCompanionId);
		Canonical += FString::Printf(
			TEXT("E:%s:%s:%s:%s:%s:%s:%d:%d:%d:%lld:%lld|"),
			*Entry.StableCompanionId.ToString(EGuidFormats::Digits),
			*Entry.ContentId.ToString(),
			*ClassPath.ToString(),
			*Entry.Archetype.ToString(),
			*Entry.Gender.ToString(),
			*Entry.DifficultyGrade.ToString(),
			Entry.ResolvedLogicalLevel,
			static_cast<int32>(Entry.Lifecycle),
			static_cast<int32>(Entry.State),
			Entry.DeathFloor,
			Entry.DeathGenerationSerial);
	}

	TArray<FGuid> Party = Snapshot.ActiveParty;
	Party.Sort([](const FGuid& Left, const FGuid& Right)
	{
		return Left.ToString(EGuidFormats::Digits) < Right.ToString(EGuidFormats::Digits);
	});
	TSet<FGuid> SeenParty;
	for (const FGuid& PartyId : Party)
	{
		if (!PartyId.IsValid() || SeenParty.Contains(PartyId) || !SeenIds.Contains(PartyId))
		{
			return FString();
		}
		SeenParty.Add(PartyId);
		Canonical += FString::Printf(TEXT("P:%s|"), *PartyId.ToString(EGuidFormats::Digits));
	}
	return EFCalystoDungeonPrivate::HashText(Canonical);
}
#endif

FEFCalystoRunEcologyState UEFCalystoDungeonSubsystem::BuildInitialEcology(
	const int64 RunSeed,
	const FString& PolicyHash,
	const int32 GeneratorVersion)
{
	FEFCalystoRunEcologyState Ecology;
	if (RunSeed <= 0 || PolicyHash.IsEmpty() || GeneratorVersion <= 0)
	{
		return Ecology;
	}
	FEFCalystoDungeonGenerationContext Context;
	Context.RunSeed = RunSeed;
	Context.FloorNumber = 1;
	Context.GenerationSerial = 1;
	// Run-DNA draws precede the first ecology hash. PCGSeed is not mixed by DeriveDomainValue;
	// a positive sentinel only satisfies the immutable context validity contract here.
	Context.PCGSeed = 1;
	Context.PolicyHash = PolicyHash;
	auto DNA = [&Context, GeneratorVersion](const uint64 Domain)
	{
		return static_cast<float>(EFCalystoDungeonDeterminism::Uniform01(
			Context, GeneratorVersion, FString(), Domain) * 2.0 - 1.0);
	};
	Ecology.bInitialized = true;
	Ecology.Scale = DNA(EFCalystoDungeonPrivate::EcologyScaleDomain);
	Ecology.Branching = DNA(EFCalystoDungeonPrivate::EcologyBranchDomain);
	Ecology.Threat = DNA(EFCalystoDungeonPrivate::EcologyThreatDomain);
	Ecology.Abundance = DNA(EFCalystoDungeonPrivate::EcologyAbundanceDomain);
	Ecology.Mystery = DNA(EFCalystoDungeonPrivate::EcologyMysteryDomain);
	Ecology.RunDNAHash = EFCalystoDungeonPrivate::HashText(FString::Printf(
		TEXT("EFCalystoRunDNAV3|%s|%s|%s|%s|%s"),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.Scale),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.Branching),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.Threat),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.Abundance),
		*EFCalystoDungeonPrivate::FloatBits(Ecology.Mystery)));
	Ecology.PerformanceEMA = 0.5f;
	Ecology.EcologyHash = EFCalystoDungeonPrivate::BuildEcologyHash(Ecology);
	return Ecology;
}

bool UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(
	const UEFCalystoDungeonDirectorPolicy* Policy,
	const int64 CompletedFloor,
	const FEFCalystoResolvedFloorIntent& CompletedIntent,
	const FEFCalystoFloorOutcome& Outcome,
	FEFCalystoRunEcologyState& InOutEcology,
	FString& OutError)
{
	OutError.Reset();
	if (!Policy || !InOutEcology.bInitialized || InOutEcology.RunDNAHash.IsEmpty()
		|| CompletedFloor <= 0 || !CompletedIntent.bIsValid
		|| CompletedIntent.FloorNumber != CompletedFloor || !EFCalystoDungeonPrivate::IsValidOutcome(Outcome)
		|| CompletedIntent.PolicyHash != ComputePolicyHash(Policy)
		|| CompletedIntent.EcologyHash != InOutEcology.EcologyHash
		|| InOutEcology.EcologyHash != ComputeEcologyHash(InOutEcology)
		|| !EFCalystoDungeonPrivate::ValidateCooldownMemory(Policy, InOutEcology, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Cannot commit an invalid floor outcome/ecology tuple.");
		}
		return false;
	}
	if (CompletedFloor <= InOutEcology.LastCommittedFloor)
	{
		OutError = FString::Printf(TEXT("Floor %lld was already committed to run ecology."), CompletedFloor);
		return false;
	}
	TSet<FName> DirectiveIds;
	for (const FEFCalystoSpawnDirective& Directive : CompletedIntent.SpawnDirectives)
	{
		if (Directive.StableId.IsNone() || Directive.Count <= 0 || DirectiveIds.Contains(Directive.StableId)
			|| !EFCalystoDungeonPrivate::FindPopulationCatalogEntry(Policy, Directive.StableId))
		{
			OutError = FString::Printf(TEXT("Completed intent contains invalid cooldown/catalog directive %s."),
				*Directive.StableId.ToString());
			return false;
		}
		DirectiveIds.Add(Directive.StableId);
	}

	FEFCalystoRunEcologyState Candidate = InOutEcology;
	if (Outcome.bIsValid)
	{
		const FEFCalystoAdaptationPolicy& A = Policy->Adaptation;
		float Score = A.CombatWeight * Outcome.Combat
			+ A.SurvivalWeight * Outcome.Survival
			+ A.ResourcesWeight * Outcome.Resources
			+ A.PaceWeight * Outcome.Pace;
		Score -= A.DeathPenalty * static_cast<float>(Outcome.Deaths + Outcome.Failures);
		Score = FMath::Clamp(Score, 0.0f, 1.0f);
		Candidate.PerformanceEMA = FMath::Lerp(Candidate.PerformanceEMA, Score, A.EMAAlpha);
	}
	Candidate.RecentStyles.Add(CompletedIntent.Style);
	while (Candidate.RecentStyles.Num() > Policy->Ecology.MaxConsecutiveStyle) { Candidate.RecentStyles.RemoveAt(0); }
	if (!CompletedIntent.DominantTheme.IsNone())
	{
		Candidate.RecentDominantThemes.Add(CompletedIntent.DominantTheme);
		while (Candidate.RecentDominantThemes.Num() > Policy->Ecology.MaxConsecutiveDominantTheme)
		{
			Candidate.RecentDominantThemes.RemoveAt(0);
		}
	}
	Candidate.ConsecutiveFloorsWithoutFood = CompletedIntent.FoodCount > 0
		? 0 : Candidate.ConsecutiveFloorsWithoutFood + 1;
	Candidate.ConsecutiveFloorsWithoutChest = CompletedIntent.ChestCount > 0
		? 0 : Candidate.ConsecutiveFloorsWithoutChest + 1;
	for (const FEFCalystoSpawnDirective& Directive : CompletedIntent.SpawnDirectives)
	{
		const FEFCalystoPopulationCatalogEntry* Entry =
			EFCalystoDungeonPrivate::FindPopulationCatalogEntry(Policy, Directive.StableId);
		if (!Entry || Entry->CooldownFloors <= 0)
		{
			continue;
		}
		FEFCalystoPopulationCooldownState* Existing = Candidate.PopulationCooldowns.FindByPredicate(
			[&Directive](const FEFCalystoPopulationCooldownState& Memory)
			{
				return Memory.StableId == Directive.StableId;
			});
		if (Existing)
		{
			Existing->LastSelectedFloor = CompletedFloor;
		}
		else
		{
			FEFCalystoPopulationCooldownState& Added = Candidate.PopulationCooldowns.AddDefaulted_GetRef();
			Added.StableId = Directive.StableId;
			Added.LastSelectedFloor = CompletedFloor;
		}
	}
	Candidate.PopulationCooldowns.Sort([](const FEFCalystoPopulationCooldownState& A, const FEFCalystoPopulationCooldownState& B)
	{
		return A.StableId.LexicalLess(B.StableId);
	});
	Candidate.LastCommittedFloor = CompletedFloor;
	++Candidate.Revision;
	Candidate.EcologyHash = EFCalystoDungeonPrivate::BuildEcologyHash(Candidate);
	if (Candidate.EcologyHash.IsEmpty())
	{
		OutError = TEXT("Committed run ecology hash is empty.");
		return false;
	}
	InOutEcology = MoveTemp(Candidate);
	return true;
}

bool UEFCalystoDungeonSubsystem::ResolveFloorIntentForTesting(
	const UEFCalystoDungeonDirectorPolicy* Policy,
	const FEFCalystoDungeonGenerationContext& Context,
	const FEFCalystoRunEcologyState& Ecology,
	const FEFCalystoDirectorIntent& DirectorIntent,
	const FEFCalystoFloorOutcome& FrozenOutcome,
	FEFCalystoResolvedFloorIntent& OutIntent,
	FString& OutError)
{
	return ResolveFloorIntentInternal(
		Policy,
		Context,
		Ecology,
		DirectorIntent,
		FrozenOutcome,
		0,
		OutIntent,
		OutError);
}

bool UEFCalystoDungeonSubsystem::ResolveFloorIntentInternal(
	const UEFCalystoDungeonDirectorPolicy* Policy,
	const FEFCalystoDungeonGenerationContext& Context,
	const FEFCalystoRunEcologyState& Ecology,
	const FEFCalystoDirectorIntent& DirectorIntent,
	const FEFCalystoFloorOutcome& FrozenOutcome,
	const int32 DevelopmentForcedDungeonEdge,
	FEFCalystoResolvedFloorIntent& OutIntent,
	FString& OutError)
{
	using namespace EFCalystoDungeonPrivate;
	OutIntent = FEFCalystoResolvedFloorIntent();
	OutError.Reset();
	FString ValidationError;
	FString EcologyError;
	const int32 ExpectedPCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
		Context.RunSeed,
		Context.FloorNumber,
		Context.GenerationSerial,
		Policy ? Policy->GeneratorVersion : 0,
		Context.PolicyHash,
		Ecology.EcologyHash);
	if (!ValidateDirectorPolicy(Policy, ValidationError) || !Context.IsValid() || !Ecology.bInitialized
		|| Ecology.EcologyHash.IsEmpty() || Ecology.RunDNAHash.IsEmpty() || Context.PolicyHash != ComputePolicyHash(Policy)
		|| Ecology.EcologyHash != ComputeEcologyHash(Ecology)
		|| !ValidateCooldownMemory(Policy, Ecology, EcologyError)
		|| Context.PCGSeed != ExpectedPCGSeed
		|| !IsValidDirectorIntent(DirectorIntent) || !IsValidOutcome(FrozenOutcome))
	{
		OutError = !ValidationError.IsEmpty()
			? ValidationError
			: (!EcologyError.IsEmpty() ? EcologyError : TEXT("Invalid V3 intent resolution inputs."));
		return false;
	}

	TArray<FEFCalystoStylePolicy> Styles = Policy->Styles;
	Styles.Sort([](const FEFCalystoStylePolicy& A, const FEFCalystoStylePolicy& B)
	{
		return static_cast<uint8>(A.Style) < static_cast<uint8>(B.Style);
	});
	TArray<int32> StyleWeights;
	for (const FEFCalystoStylePolicy& Style : Styles)
	{
		int32 Weight = Style.SelectionWeight;
		if (DirectorIntent.PreferredStyle == Style.Style) { Weight = FMath::Min(Weight * 3, 10000); }
		if (HasConsecutiveStyle(Ecology, Style.Style, Policy->Ecology.MaxConsecutiveStyle)) { Weight = 0; }
		StyleWeights.Add(Weight);
	}
	int32 StyleIndex = EFCalystoDungeonDeterminism::SelectWeightedIndex(
		StyleWeights,
		EFCalystoDungeonDeterminism::DeriveDomainValue(
			Context, Policy->GeneratorVersion, Ecology.EcologyHash, StyleDomain));
	if (!Styles.IsValidIndex(StyleIndex))
	{
		StyleIndex = Styles.IndexOfByPredicate([](const FEFCalystoStylePolicy& Style)
		{
			return Style.Style == EEFCalystoDungeonStyle::Standard;
		});
	}
	if (!Styles.IsValidIndex(StyleIndex))
	{
		OutError = TEXT("No eligible dungeon style remains after pacing constraints.");
		return false;
	}
	const FEFCalystoStylePolicy& Style = Styles[StyleIndex];

	const float Adaptation = FMath::Clamp(
		(Ecology.PerformanceEMA - 0.5f) * 2.0f * Policy->Adaptation.MaximumInfluence,
		-Policy->Adaptation.MaximumInfluence,
		Policy->Adaptation.MaximumInfluence);
	OutIntent.Scale = FMath::Clamp(ResolveTrait(Context, Policy, Ecology, Ecology.Scale, ScaleTraitDomain)
		+ Style.ScaleBias + DirectorIntent.ScaleBias, -1.0f, 1.0f);
	OutIntent.Branching = FMath::Clamp(ResolveTrait(Context, Policy, Ecology, Ecology.Branching, BranchTraitDomain)
		+ Style.BranchingBias + DirectorIntent.BranchingBias, -1.0f, 1.0f);
	OutIntent.Threat = FMath::Clamp(ResolveTrait(Context, Policy, Ecology, Ecology.Threat, ThreatTraitDomain)
		+ Style.ThreatBias + DirectorIntent.ThreatBias + Adaptation, -1.0f, 1.0f);
	OutIntent.Abundance = FMath::Clamp(ResolveTrait(Context, Policy, Ecology, Ecology.Abundance, AbundanceTraitDomain)
		+ Style.AbundanceBias + DirectorIntent.ResourceBias - Adaptation, -1.0f, 1.0f);
	OutIntent.Mystery = FMath::Clamp(ResolveTrait(Context, Policy, Ecology, Ecology.Mystery, MysteryTraitDomain)
		+ Style.MysteryBias, -1.0f, 1.0f);
	OutIntent.Pacing = static_cast<float>(EFCalystoDungeonDeterminism::Uniform01(
		Context,
		Policy->GeneratorVersion,
		Ecology.EcologyHash,
		EFCalystoDungeonDomains::Pacing) * 2.0 - 1.0)
		* Policy->Progression.PacingAmplitude;

	const double LayoutProgress = EFCalystoDungeonDeterminism::Progression(Context.FloorNumber, Policy->Progression.LayoutTau);
	const double ThreatProgress = EFCalystoDungeonDeterminism::Progression(Context.FloorNumber, Policy->Progression.ThreatTau);
	const float SizeMode = static_cast<float>(FMath::Lerp(
		static_cast<double>(Policy->Progression.StartSizeMode),
		static_cast<double>(Policy->Progression.EndSizeMode),
		LayoutProgress)) + OutIntent.Scale * 2.0f;
	FEFCalystoIntDistribution SizeDistribution;
	SizeDistribution.Min = FMath::Clamp(FMath::FloorToInt(SizeMode - Policy->Progression.SizeHalfRange),
		Policy->Limits.MinDungeonEdge, Policy->Limits.MaxDungeonEdge);
	SizeDistribution.Mode = FMath::Clamp(FMath::RoundToInt(SizeMode), SizeDistribution.Min, Policy->Limits.MaxDungeonEdge);
	SizeDistribution.Max = FMath::Clamp(FMath::CeilToInt(SizeMode + Policy->Progression.SizeHalfRange),
		SizeDistribution.Mode, Policy->Limits.MaxDungeonEdge);
	SizeDistribution.Concentration = 4.0f;
	const int32 RawEdge = EFCalystoDungeonDeterminism::SamplePERT(
		SizeDistribution, DirectorIntent.Volatility, Context, Policy->GeneratorVersion, Ecology.EcologyHash, SizeDomain);
	int32 ValidatedEdge = NearestValidatedSize(Policy->ValidatedDungeonSizes, RawEdge);
	if (DevelopmentForcedDungeonEdge != 0)
	{
#if UE_BUILD_SHIPPING
		OutError = TEXT("Exact dungeon-edge automation overrides are unavailable in Shipping.");
		return false;
#else
		if (DevelopmentForcedDungeonEdge < Policy->Limits.MinDungeonEdge
			|| DevelopmentForcedDungeonEdge > Policy->Limits.MaxDungeonEdge)
		{
			OutError = TEXT("Development automation requested an exact dungeon edge outside the V3 hard limits.");
			return false;
		}
		ValidatedEdge = DevelopmentForcedDungeonEdge;
#endif
	}
	OutIntent.DungeonSize = FIntVector(ValidatedEdge, ValidatedEdge, 1);
	OutIntent.CandidateAnchorDensity = FMath::Clamp(
		EFCalystoDungeonDeterminism::SamplePERT(Policy->Progression.CandidateAnchorDensity, DirectorIntent.Volatility,
			Context, Policy->GeneratorVersion, Ecology.EcologyHash, AnchorDensityDomain)
			+ 0.025f * OutIntent.Mystery,
		Policy->Limits.MinCandidateAnchorDensity,
		Policy->Limits.MaxCandidateAnchorDensity);
	OutIntent.SidePathChance = FMath::Clamp(
		EFCalystoDungeonDeterminism::SamplePERT(Policy->Progression.SidePathChance, DirectorIntent.Volatility,
			Context, Policy->GeneratorVersion, Ecology.EcologyHash, SidePathDomain)
			+ 0.10f * OutIntent.Branching,
		Policy->Limits.MinSidePathChance,
		Policy->Limits.MaxSidePathChance);

	auto ProgressChance = [LayoutProgress](const float Start, const float End)
	{
		return static_cast<float>(FMath::Lerp(static_cast<double>(Start), static_cast<double>(End), LayoutProgress));
	};
	auto ApplyChanceBias = [](const float BaseChance, const float Bias)
	{
		// Exact policy boundaries are authoring gates: zero is disabled and one is guaranteed.
		// Personality/adaptation only shapes probabilities inside the open interval.
		if (BaseChance <= 0.0f) { return 0.0f; }
		if (BaseChance >= 1.0f) { return 1.0f; }
		return FMath::Clamp(BaseChance + Bias, 0.0f, 1.0f);
	};
	auto EligibleCapacity = [&Context, &Ecology](const TArray<FEFCalystoPopulationCatalogEntry>& Catalog)
	{
		int32 Capacity = 0;
		for (const FEFCalystoPopulationCatalogEntry& Entry : Catalog)
		{
			if (IsPopulationEntryAvailable(Entry, Context.FloorNumber, Ecology))
			{
				Capacity = FMath::Min(MAX_int32 - Entry.MaxPerFloor, Capacity) + Entry.MaxPerFloor;
			}
		}
		return Capacity;
	};
	const int32 EnemyEligibleCapacity = EligibleCapacity(Policy->EnemyCatalog);
	const int32 FoodEligibleCapacity = EligibleCapacity(Policy->FoodCatalog);
	const int32 ChestEligibleCapacity = EligibleCapacity(Policy->ChestCatalog);
	const int32 LootEligibleCapacity = EligibleCapacity(Policy->LootCatalog);
	const int32 SpecialEligibleCapacity = EligibleCapacity(Policy->SpecialEventCatalog);
	OutIntent.EnemyPresenceChance = EnemyEligibleCapacity > 0 ? ApplyChanceBias(
		ProgressChance(Policy->Progression.StartEnemyPresence, Policy->Progression.EndEnemyPresence),
		0.10f * OutIntent.Threat + OutIntent.Pacing) : 0.0f;
	OutIntent.FoodPresenceChance = FoodEligibleCapacity > 0 ? ApplyChanceBias(
		ProgressChance(Policy->Progression.StartFoodPresence, Policy->Progression.EndFoodPresence),
		0.15f * OutIntent.Abundance - 0.50f * OutIntent.Pacing) : 0.0f;
	OutIntent.ChestPresenceChance = ChestEligibleCapacity > 0 ? ApplyChanceBias(
		ProgressChance(Policy->Progression.StartChestPresence, Policy->Progression.EndChestPresence),
		0.10f * OutIntent.Abundance + 0.05f * OutIntent.Mystery) : 0.0f;
	OutIntent.LootPresenceChance = LootEligibleCapacity > 0 ? ApplyChanceBias(
		ProgressChance(Policy->Progression.StartLootPresence, Policy->Progression.EndLootPresence),
		0.10f * OutIntent.Mystery) : 0.0f;
	const float BaseSpecialEventChance = ProgressChance(
		Policy->Progression.StartSpecialEventPresence, Policy->Progression.EndSpecialEventPresence);
	OutIntent.SpecialEventPresenceChance = SpecialEligibleCapacity > 0 && BaseSpecialEventChance > 0.0f
		? ApplyChanceBias(BaseSpecialEventChance, 0.05f * OutIntent.Mystery)
		: 0.0f;
	if (FoodEligibleCapacity > 0
		&& Ecology.ConsecutiveFloorsWithoutFood >= Policy->Ecology.FoodPityAfterEmptyFloors)
	{
		OutIntent.FoodPresenceChance = 1.0f;
	}
	if (ChestEligibleCapacity > 0
		&& Ecology.ConsecutiveFloorsWithoutChest >= Policy->Ecology.ChestPityAfterEmptyFloors)
	{
		OutIntent.ChestPresenceChance = 1.0f;
	}

	const bool bEnemiesPresent = EFCalystoDungeonDeterminism::Bernoulli(OutIntent.EnemyPresenceChance,
		Context, Policy->GeneratorVersion, Ecology.EcologyHash, EnemyPresenceDomain);
	if (bEnemiesPresent)
	{
		const int32 EnemyCap = FMath::Min3(Policy->Limits.MaxEnemies, Policy->Limits.MaxDirectorActors, EnemyEligibleCapacity);
		const float Mode = static_cast<float>(FMath::Lerp(
			static_cast<double>(Policy->Progression.StartEnemyCountMode),
			static_cast<double>(Policy->Progression.EndEnemyCountMode),
			ThreatProgress)) + OutIntent.Threat * 3.0f + OutIntent.Pacing * 4.0f;
		FEFCalystoIntDistribution CountDistribution;
		CountDistribution.Min = FMath::Clamp(FMath::FloorToInt(Mode - Policy->Progression.EnemyCountLowerOffset), 1, EnemyCap);
		CountDistribution.Mode = FMath::Clamp(FMath::RoundToInt(Mode), CountDistribution.Min, EnemyCap);
		CountDistribution.Max = FMath::Clamp(FMath::CeilToInt(Mode + Policy->Progression.EnemyCountUpperOffset),
			CountDistribution.Mode, EnemyCap);
		CountDistribution.Concentration = 4.0f;
		OutIntent.EnemyCount = EFCalystoDungeonDeterminism::SamplePERT(
			CountDistribution, DirectorIntent.Volatility, Context, Policy->GeneratorVersion, Ecology.EcologyHash, EnemyCountDomain);
	}
	auto ResolveOptionalCount = [&Context, Policy, &Ecology, &DirectorIntent](
		const float Chance,
		const uint64 PresenceDomain,
		const uint64 CountDomain,
		const FEFCalystoIntDistribution& Distribution,
		const int32 Cap)
	{
		return Cap > 0 && EFCalystoDungeonDeterminism::Bernoulli(Chance, Context, Policy->GeneratorVersion, Ecology.EcologyHash, PresenceDomain)
			? FMath::Clamp(EFCalystoDungeonDeterminism::SamplePERT(
				Distribution, DirectorIntent.Volatility, Context, Policy->GeneratorVersion, Ecology.EcologyHash, CountDomain), 1, Cap)
			: 0;
	};
	OutIntent.FoodCount = ResolveOptionalCount(OutIntent.FoodPresenceChance, FoodPresenceDomain, FoodCountDomain,
		Policy->Progression.FoodCount, FMath::Min(Policy->Limits.MaxFood, FoodEligibleCapacity));
	OutIntent.ChestCount = ResolveOptionalCount(OutIntent.ChestPresenceChance, ChestPresenceDomain, ChestCountDomain,
		Policy->Progression.ChestCount, FMath::Min(Policy->Limits.MaxChests, ChestEligibleCapacity));
	OutIntent.LootCount = ResolveOptionalCount(OutIntent.LootPresenceChance, LootPresenceDomain, LootCountDomain,
		Policy->Progression.LootCount, FMath::Min(Policy->Limits.MaxLoot, LootEligibleCapacity));
	OutIntent.SpecialEventCount = ResolveOptionalCount(OutIntent.SpecialEventPresenceChance, SpecialPresenceDomain, SpecialCountDomain,
		Policy->Progression.SpecialEventCount, FMath::Min(Policy->Limits.MaxSpecialEvents, SpecialEligibleCapacity));

	int32 RemainingActors = FMath::Max(0, Policy->Limits.MaxDirectorActors - OutIntent.EnemyCount);
	OutIntent.FoodCount = FMath::Min(OutIntent.FoodCount, RemainingActors); RemainingActors -= OutIntent.FoodCount;
	OutIntent.ChestCount = FMath::Min(OutIntent.ChestCount, RemainingActors); RemainingActors -= OutIntent.ChestCount;
	OutIntent.LootCount = FMath::Min(OutIntent.LootCount, RemainingActors); RemainingActors -= OutIntent.LootCount;
	OutIntent.SpecialEventCount = FMath::Min(OutIntent.SpecialEventCount, RemainingActors);

	float MinimumEnemyCost = TNumericLimits<float>::Max();
	for (const FEFCalystoPopulationCatalogEntry& Entry : Policy->EnemyCatalog)
	{
		if (IsPopulationEntryAvailable(Entry, Context.FloorNumber, Ecology))
		{
			MinimumEnemyCost = FMath::Min(MinimumEnemyCost, Entry.Cost);
		}
	}
	if (!FMath::IsFinite(MinimumEnemyCost)) { MinimumEnemyCost = 0.0f; }
	const float AuthoredThreatBudgetMin = Policy->Progression.StartThreatBudget;
	const float AuthoredThreatBudgetMax = Policy->Progression.EndThreatBudget;
	const float ThreatBudgetMode = FMath::Clamp(static_cast<float>(FMath::Lerp(
		static_cast<double>(Policy->Progression.StartThreatBudget),
		static_cast<double>(Policy->Progression.EndThreatBudget),
		ThreatProgress)) * (1.0f + 0.15f * OutIntent.Threat),
		AuthoredThreatBudgetMin,
		AuthoredThreatBudgetMax);
	FEFCalystoFloatDistribution ThreatBudgetDistribution;
	ThreatBudgetDistribution.Min = FMath::Clamp(
		ThreatBudgetMode * (1.0f - Policy->Progression.ThreatBudgetRelativeRange),
		AuthoredThreatBudgetMin,
		AuthoredThreatBudgetMax);
	ThreatBudgetDistribution.Mode = ThreatBudgetMode;
	ThreatBudgetDistribution.Max = FMath::Clamp(
		ThreatBudgetMode * (1.0f + Policy->Progression.ThreatBudgetRelativeRange),
		AuthoredThreatBudgetMin,
		AuthoredThreatBudgetMax);
	ThreatBudgetDistribution.Concentration = 4.0f;
	const float SampledThreatBudget = EFCalystoDungeonDeterminism::SamplePERT(
		ThreatBudgetDistribution,
		DirectorIntent.Volatility,
		Context,
		Policy->GeneratorVersion,
		Ecology.EcologyHash,
		EFCalystoDungeonDomains::ThreatBudget);
	if (OutIntent.EnemyCount > 0 && MinimumEnemyCost > 0.0f)
	{
		const int32 MaximumAffordableEnemyCount = FMath::Max(
			0,
			FMath::FloorToInt(AuthoredThreatBudgetMax / MinimumEnemyCost));
		OutIntent.EnemyCount = FMath::Min(OutIntent.EnemyCount, MaximumAffordableEnemyCount);
	}
	const float MinimumRequiredThreatBudget = MinimumEnemyCost * static_cast<float>(OutIntent.EnemyCount);
	OutIntent.ThreatBudget = FMath::Clamp(
		FMath::Max(SampledThreatBudget, MinimumRequiredThreatBudget),
		AuthoredThreatBudgetMin,
		AuthoredThreatBudgetMax);
	const int32 ResourceActorCount = OutIntent.FoodCount + OutIntent.ChestCount + OutIntent.LootCount + OutIntent.SpecialEventCount;
	OutIntent.ResourceBudget = static_cast<float>(ResourceActorCount) * 4.0f;

	float ThreatSpent = 0.0f;
	float ResourceSpent = 0.0f;
	float CategorySpent = 0.0f;
	if (!BuildPopulationDirectives(Policy->EnemyCatalog, EEFCalystoSpawnCategory::Enemy, Context.FloorNumber,
			OutIntent.EnemyCount, OutIntent.ThreatBudget, Context, Policy->GeneratorVersion, Ecology.EcologyHash,
			Ecology, Policy->Ecology, OutIntent.Mystery, EnemyCompositionDomain,
			OutIntent.SpawnDirectives, ThreatSpent, OutError)
		|| !BuildPopulationDirectives(Policy->FoodCatalog, EEFCalystoSpawnCategory::Food, Context.FloorNumber,
			OutIntent.FoodCount, OutIntent.ResourceBudget - ResourceSpent, Context, Policy->GeneratorVersion, Ecology.EcologyHash,
			Ecology, Policy->Ecology, OutIntent.Mystery, FoodCompositionDomain,
			OutIntent.SpawnDirectives, CategorySpent, OutError))
	{
		return false;
	}
	ResourceSpent += CategorySpent;
	if (!BuildPopulationDirectives(Policy->ChestCatalog, EEFCalystoSpawnCategory::Chest, Context.FloorNumber,
			OutIntent.ChestCount, OutIntent.ResourceBudget - ResourceSpent, Context, Policy->GeneratorVersion, Ecology.EcologyHash,
			Ecology, Policy->Ecology, OutIntent.Mystery, ChestCompositionDomain,
			OutIntent.SpawnDirectives, CategorySpent, OutError))
	{
		return false;
	}
	ResourceSpent += CategorySpent;
	if (!BuildPopulationDirectives(Policy->LootCatalog, EEFCalystoSpawnCategory::Loot, Context.FloorNumber,
			OutIntent.LootCount, OutIntent.ResourceBudget - ResourceSpent, Context, Policy->GeneratorVersion, Ecology.EcologyHash,
			Ecology, Policy->Ecology, OutIntent.Mystery, LootCompositionDomain,
			OutIntent.SpawnDirectives, CategorySpent, OutError))
	{
		return false;
	}
	ResourceSpent += CategorySpent;
	if (!BuildPopulationDirectives(Policy->SpecialEventCatalog, EEFCalystoSpawnCategory::SpecialEvent, Context.FloorNumber,
			OutIntent.SpecialEventCount, OutIntent.ResourceBudget - ResourceSpent, Context, Policy->GeneratorVersion, Ecology.EcologyHash,
			Ecology, Policy->Ecology, OutIntent.Mystery, SpecialCompositionDomain,
			OutIntent.SpawnDirectives, CategorySpent, OutError))
	{
		return false;
	}
	OutIntent.SpawnDirectives.Sort([](const FEFCalystoSpawnDirective& A, const FEFCalystoSpawnDirective& B)
	{
		if (A.Category != B.Category) { return static_cast<uint8>(A.Category) < static_cast<uint8>(B.Category); }
		return A.StableId.LexicalLess(B.StableId);
	});

	TArray<FEFCalystoThemeCatalogEntry> Themes = Policy->ThemeCatalog;
	Themes.Sort([](const FEFCalystoThemeCatalogEntry& A, const FEFCalystoThemeCatalogEntry& B)
	{
		return A.StableId.LexicalLess(B.StableId);
	});
	TArray<int32> ThemeRollWeights;
	for (const FEFCalystoThemeCatalogEntry& Theme : Themes)
	{
		int32 Weight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Theme.BaseWeight)
			* (1.0f + 0.75f * DirectorIntent.ThemeBias * Theme.BiasAxis)));
		if (HasConsecutiveTheme(Ecology, Theme.StableId, Policy->Ecology.MaxConsecutiveDominantTheme)) { Weight = 0; }
		ThemeRollWeights.Add(Weight);
	}
	int32 DominantIndex = EFCalystoDungeonDeterminism::SelectWeightedIndex(
		ThemeRollWeights,
		EFCalystoDungeonDeterminism::DeriveDomainValue(
			Context, Policy->GeneratorVersion, Ecology.EcologyHash, ThemeDomain));
	if (!Themes.IsValidIndex(DominantIndex)) { DominantIndex = 0; }
	OutIntent.DominantTheme = Themes[DominantIndex].StableId;
	for (int32 Index = 0; Index < Themes.Num(); ++Index)
	{
		FEFCalystoThemeWeight Theme;
		Theme.ThemeId = Themes[Index].StableId;
		Theme.RoomType = Themes[Index].RoomType;
		const float Biased = static_cast<float>(Themes[Index].BaseWeight)
			* (1.0f + 0.75f * DirectorIntent.ThemeBias * Themes[Index].BiasAxis);
		Theme.Weight = FMath::Clamp(FMath::RoundToInt(Biased / 2.0f) + (Index == DominantIndex ? 1 : 0), 1, 5);
		OutIntent.ThemeWeights.Add(MoveTemp(Theme));
	}

	OutIntent.bIsValid = true;
	OutIntent.GeneratorVersion = Policy->GeneratorVersion;
	OutIntent.RunSeed = Context.RunSeed;
	OutIntent.FloorNumber = Context.FloorNumber;
	OutIntent.GenerationSerial = Context.GenerationSerial;
	OutIntent.PCGSeed = Context.PCGSeed;
	OutIntent.PolicyHash = Context.PolicyHash;
	OutIntent.EcologyHash = Ecology.EcologyHash;
	OutIntent.Style = Style.Style;
	OutIntent.RoomMinSize = Policy->Limits.RoomMinSize;
	OutIntent.RoomMaxSize = Policy->Limits.RoomMaxSize;
	OutIntent.DifficultyTier = FMath::Clamp(1 + FMath::FloorToInt(ThreatProgress * 4.0), 1, 5);
	OutIntent.FrozenOutcome = FrozenOutcome;
	OutIntent.OutcomeHash = BuildOutcomeHash(FrozenOutcome);
	OutIntent.IntentHash = BuildIntentHash(OutIntent);
	if (OutIntent.IntentHash.IsEmpty())
	{
		OutError = TEXT("Resolved V3 intent hash is empty.");
		OutIntent = FEFCalystoResolvedFloorIntent();
		return false;
	}
	return true;
}

void UEFCalystoDungeonSubsystem::HandlePostWorldInitialization(
	UWorld* World,
	const UWorld::InitializationValues InitializationValues)
{
	(void)InitializationValues;
	if (IsConfiguredDungeonWorld(World))
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this, &UEFCalystoDungeonSubsystem::HandleDungeonWorldReady, TWeakObjectPtr<UWorld>(World)));
	}
}

void UEFCalystoDungeonSubsystem::HandleDungeonWorldReady(TWeakObjectPtr<UWorld> WorldPtr)
{
	UWorld* World = WorldPtr.Get();
	if (!IsConfiguredDungeonWorld(World))
	{
		return;
	}
	if (!bPolicyValid || !CachedDirectorPolicyV4)
	{
		LastFailureCode = TEXT("DIRECTOR_POLICY_V4_INVALID");
		LastFailureMessage = PolicyError;
		GenerationState = EEFCalystoGenerationState::Failed;
		ReturnFromFailedDungeon();
		return;
	}
	CancelTravelWatchdog();

	int64 ParsedRunSeed = 0;
	int64 ParsedFloor = 0;
	int64 ParsedGeneration = 0;
	int64 ParsedTravelRequest = 0;
	const bool bHasRunSeed = EFCalystoDungeonPrivate::TryReadInt64Option(
		World, TEXT("DungeonRunSeed="), ParsedRunSeed);
	const bool bHasFloor = EFCalystoDungeonPrivate::TryReadInt64Option(
		World, TEXT("DungeonFloor="), ParsedFloor);
	const bool bHasGeneration = EFCalystoDungeonPrivate::TryReadInt64Option(
		World, TEXT("DungeonGeneration="), ParsedGeneration);
	const bool bHasTravelRequest = EFCalystoDungeonPrivate::TryReadInt64Option(
		World, TEXT("DungeonTravelRequest="), ParsedTravelRequest);

	if (bTravelRequestPending)
	{
		const bool bIdentityMatches = bHasRunSeed && bHasFloor && bHasGeneration
			&& bHasTravelRequest && ParsedTravelRequest > 0
			&& ParsedTravelRequest == PendingTravelRequestId
			&& PendingContext.RunSeed == ParsedRunSeed
			&& PendingContext.FloorNumber == ParsedFloor
			&& PendingContext.GenerationSerial == ParsedGeneration;
		if (!bIdentityMatches)
		{
			RejectPendingTravel(TEXT("pending V4 destination identity missing or mismatched"));
			return;
		}
	}
	else if (bHasTravelRequest)
	{
		RejectPendingTravel(TEXT("destination supplied a stale V4 travel request token"));
		return;
	}

	FEFCalystoDungeonGenerationContext DestinationContext;
	FEFCalystoRunEcologyStateV4 DestinationEcology;
	FEFCalystoResolvedFloorIntentV4 DestinationIntent;
	FEFCalystoRealizedFloorManifestV4 DestinationExpectedManifest;
	EEFCalystoDungeonTravelKind DestinationKind = EEFCalystoDungeonTravelKind::NewRun;
	FName DestinationCompletionTransition = NAME_None;
	bool bMatchesPendingContext = false;

	if (bTravelRequestPending)
	{
		bMatchesPendingContext = true;
		DestinationContext = PendingContext;
		DestinationEcology = PendingEcologyV4;
		DestinationIntent = PendingIntentV4;
		DestinationExpectedManifest = PendingExpectedManifestV4;
		DestinationKind = PendingTravelKind;
		DestinationCompletionTransition = PendingCompletionTransitionId;
		if (!DestinationIntent.bIsValid
			|| DestinationIntent.RunSeed != ParsedRunSeed
			|| DestinationIntent.FloorNumber != ParsedFloor
			|| DestinationIntent.GenerationSerial != ParsedGeneration
			|| DestinationIntent.PCGSeed != DestinationContext.PCGSeed
			|| DestinationIntent.PolicyHash != CompiledPolicyHash
			|| DestinationIntent.EcologyHash != DestinationEcology.EcologyHash
			|| DestinationEcology.EcologyHash != ComputeEcologyHashV4(DestinationEcology))
		{
			RejectPendingTravel(TEXT("frozen V4 destination intent or ecology mismatch"));
			return;
		}
	}
	else if (bHasRunSeed && bHasFloor && bHasGeneration
		&& EFCalystoDungeonPrivate::IsValidContextNumbers(ParsedRunSeed, ParsedFloor, ParsedGeneration)
		&& HasActiveRun() && ActiveContext.RunSeed == ParsedRunSeed
		&& ActiveContext.FloorNumber == ParsedFloor
		&& ActiveContext.GenerationSerial == ParsedGeneration)
	{
		DestinationContext = ActiveContext;
		DestinationEcology = RunEcologyV4;
		DestinationIntent = ActiveIntentV4;
		DestinationExpectedManifest = ActiveManifestV4;
		DestinationKind = EEFCalystoDungeonTravelKind::Replay;
	}
	else
	{
		// A dungeon map can be entered directly in PIE/development without a
		// preceding OpenLevel request.  Treat that path as a complete synthetic
		// New Run handshake so the project-owned roster/inventory adapter freezes
		// the authoritative destination inventory before the run is accepted.
		// Skipping this event would make WorldAccepted attempt to restore a travel
		// capsule that was never captured.
		bExternalTravelPreparationFailed = false;
		ExternalTravelPreparationFailureCode = NAME_None;
		ExternalTravelPreparationFailureMessage.Reset();
		bHasSubmittedCompanionSnapshot = false;
		SubmittedCompanionSnapshotV4 = FEFCalystoCompanionSnapshotV4();
		{
			TGuardValue<bool> PreparationGuard(bTravelPreparationInProgress, true);
			BeforeAnyDirectorTravelEvent.Broadcast(EEFCalystoDungeonTravelKindV4::NewRun);
		}
		if (bExternalTravelPreparationFailed || !bHasSubmittedCompanionSnapshot
			|| !SubmittedCompanionSnapshotV4.Records.IsEmpty()
			|| SubmittedCompanionSnapshotV4.bPlayerOwnsWintersRecall)
		{
			LastFailureCode = bExternalTravelPreparationFailed
				? ExternalTravelPreparationFailureCode
				: FName(TEXT("DIRECT_BOOTSTRAP_SNAPSHOT_INVALID"));
			LastFailureMessage = bExternalTravelPreparationFailed
				? ExternalTravelPreparationFailureMessage
				: TEXT("Direct V4 bootstrap requires an empty New Run roster and a captured typed inventory capsule.");
			PolicyError = LastFailureMessage;
			RejectPendingTravel(TEXT("direct V4 bootstrap preparation failed"));
			return;
		}

		const int64 NewSeed = bHasRunSeed && ParsedRunSeed > 0
			? ParsedRunSeed
			: EFCalystoDungeonPrivate::CreatePositiveRunSeed();
		DestinationContext = MakeContext(NewSeed, 1, 1);
		DestinationEcology = BuildInitialEcologyV4(
			NewSeed, CompiledPolicyHash, CachedDirectorPolicyV4->GeneratorVersion);
		DestinationEcology.CompanionSnapshot = SubmittedCompanionSnapshotV4;
		DestinationEcology.EcologyHash = ComputeEcologyHashV4(DestinationEcology);
		FEFCalystoResolveContextV4 ResolveContext;
		FString ResolveError;
		if (!DestinationContext.IsValid() || !DestinationEcology.bInitialized
			|| !BuildResolveContextV4(
				DestinationContext,
				DestinationEcology,
				FEFCalystoDirectorIntentV4(),
				FEFCalystoFloorOutcomeV4(),
				false,
				SubmittedCompanionSnapshotV4,
				ResolveContext,
				ResolveError)
			|| !FEFCalystoDungeonDirectorResolverV4::Resolve(
				CachedDirectorPolicyV4, ResolveContext, DestinationIntent, ResolveError))
		{
			PolicyError = ResolveError;
			RejectPendingTravel(TEXT("direct V4 bootstrap resolution failed"));
			return;
		}
		DestinationContext.PCGSeed = DestinationIntent.PCGSeed;
	}

	if (!DestinationContext.IsValid() || !DestinationEcology.bInitialized
		|| !DestinationIntent.bIsValid || DestinationIntent.IntentHash.IsEmpty())
	{
		RejectPendingTravel(TEXT("destination V4 state is invalid"));
		return;
	}

	const bool bAcceptedRecovery = bMatchesPendingContext && bRecoveryTravelPending;
	const int32 AcceptedGenerationAttempt = bMatchesPendingContext
		? FMath::Clamp(PendingGenerationAttempt, 1, MaximumGenerationAttempts)
		: 1;
	const int64 AcceptedRunEpoch = bMatchesPendingContext && PendingRunEpoch > 0
		? PendingRunEpoch
		: (DestinationKind == EEFCalystoDungeonTravelKind::NewRun
			? FMath::Max<int64>(RunEpoch + 1, 1)
			: RunEpoch);
	if (AcceptedRunEpoch <= 0)
	{
		RejectPendingTravel(TEXT("destination V4 RunEpoch is invalid"));
		return;
	}

	ActiveContext = DestinationContext;
	ActiveIntent = FEFCalystoResolvedFloorIntent();
	ActiveManifest = FEFCalystoRealizedFloorManifest();
	ExpectedManifestForRealization = FEFCalystoRealizedFloorManifest();
	RunEcology = FEFCalystoRunEcologyState();
	ActiveIntentV4 = MoveTemp(DestinationIntent);
	ActiveManifestV4 = FEFCalystoRealizedFloorManifestV4();
	ExpectedManifestForRealizationV4 = MoveTemp(DestinationExpectedManifest);
	RunEcologyV4 = DestinationEcology;
	bCompanionRosterReady = false;
	ReadyCompanionSnapshotHash.Reset();
	if (bMatchesPendingContext)
	{
		ReturnMapPackage = PendingReturnMapPackage;
		CommitAcceptedPendingInputs();
		ResolvedFloorPreloadHandle.Reset();
		ResolvedFloorPreloadHandle = MoveTemp(PendingResolvedFloorPreloadHandle);
	}
	else if (DestinationKind == EEFCalystoDungeonTravelKind::NewRun)
	{
		// Consume the direct-bootstrap snapshot only after the destination state
		// has been accepted.  The companion subsystem retains its independent
		// typed inventory capsule until DirectorWorldAccepted restores/verifies it.
		CommitAcceptedPendingInputs();
	}
	bHasActiveRun = true;
	RunEpoch = AcceptedRunEpoch;
	LastTravelKind = DestinationKind;
	TravelState = EEFCalystoDungeonTravelState::AwaitingFloorReady;
	CurrentGenerationAttempt = AcceptedGenerationAttempt;
	if (!bAcceptedRecovery)
	{
		LastFailureCode = NAME_None;
		LastFailureMessage.Reset();
	}
	GenerationState = EEFCalystoGenerationState::Generating;
	AwaitingCompletionTransitionId = DestinationCompletionTransition;
	ResetPendingTravelTransaction();
	if (DestinationKind == EEFCalystoDungeonTravelKind::NewRun)
	{
		NewRunInitializedEvent.Broadcast(RunEpoch);
	}
	DirectorWorldAcceptedEvent.Broadcast(
		RunEpoch,
		static_cast<EEFCalystoDungeonTravelKindV4>(DestinationKind),
		ActiveIntentV4);
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Calysto Director V4 accepted: run=%lld epoch=%lld floor=%lld generation=%lld pcg_seed=%d intent=%s ecology=%s world=%s."),
		ActiveContext.RunSeed,
		RunEpoch,
		ActiveContext.FloorNumber,
		ActiveContext.GenerationSerial,
		ActiveContext.PCGSeed,
		*ActiveIntentV4.IntentHash,
		*RunEcologyV4.EcologyHash,
		*GetNameSafe(World));
}

#if 0 // Retired V3 destination acceptance; V4 has no runtime fallback.
void UEFCalystoDungeonSubsystem::HandleDungeonWorldReadyLegacyV3(TWeakObjectPtr<UWorld> WorldPtr)
{
	UWorld* World = WorldPtr.Get();
	if (!IsConfiguredDungeonWorld(World))
	{
		return;
	}
	if (!bPolicyValid || !CachedDirectorPolicy)
	{
		LastFailureCode = TEXT("DIRECTOR_POLICY_INVALID");
		LastFailureMessage = PolicyError;
		GenerationState = EEFCalystoGenerationState::Failed;
		ReturnFromFailedDungeon();
		return;
	}
	CancelTravelWatchdog();

	int64 ParsedRunSeed = 0;
	int64 ParsedFloor = 0;
	int64 ParsedGeneration = 0;
	int64 ParsedTravelRequest = 0;
	const bool bHasRunSeed = EFCalystoDungeonPrivate::TryReadInt64Option(World, TEXT("DungeonRunSeed="), ParsedRunSeed);
	const bool bHasFloor = EFCalystoDungeonPrivate::TryReadInt64Option(World, TEXT("DungeonFloor="), ParsedFloor);
	const bool bHasGeneration = EFCalystoDungeonPrivate::TryReadInt64Option(World, TEXT("DungeonGeneration="), ParsedGeneration);
	const bool bHasTravelRequest = EFCalystoDungeonPrivate::TryReadInt64Option(
		World, TEXT("DungeonTravelRequest="), ParsedTravelRequest);

	if (bTravelRequestPending)
	{
		const bool bPendingIdentityMatches = bHasRunSeed && bHasFloor && bHasGeneration && bHasTravelRequest
			&& EFCalystoDungeonPrivate::IsValidContextNumbers(ParsedRunSeed, ParsedFloor, ParsedGeneration)
			&& ParsedTravelRequest > 0 && ParsedTravelRequest == PendingTravelRequestId
			&& PendingContext.RunSeed == ParsedRunSeed && PendingContext.FloorNumber == ParsedFloor
			&& PendingContext.GenerationSerial == ParsedGeneration;
		if (!bPendingIdentityMatches)
		{
			RejectPendingTravel(TEXT("pending destination identity missing or mismatched"));
			return;
		}
	}
	else if (bHasTravelRequest)
	{
		RejectPendingTravel(TEXT("destination supplied a stale travel request token"));
		return;
	}

	FEFCalystoDungeonGenerationContext DestinationContext;
	FEFCalystoRunEcologyState DestinationEcology;
	EEFCalystoDungeonTravelKind DestinationKind = EEFCalystoDungeonTravelKind::NewRun;
	FName DestinationCompletionTransition = NAME_None;
	bool bMatchesPendingContext = false;
	if (bHasRunSeed && bHasFloor && bHasGeneration
		&& EFCalystoDungeonPrivate::IsValidContextNumbers(ParsedRunSeed, ParsedFloor, ParsedGeneration))
	{
		DestinationContext = MakeContext(ParsedRunSeed, ParsedFloor, ParsedGeneration);
		if (bTravelRequestPending && ParsedTravelRequest == PendingTravelRequestId
			&& PendingContext.RunSeed == ParsedRunSeed
			&& PendingContext.FloorNumber == ParsedFloor && PendingContext.GenerationSerial == ParsedGeneration)
		{
			bMatchesPendingContext = true;
			DestinationContext = PendingContext;
			DestinationKind = PendingTravelKind;
			DestinationCompletionTransition = PendingCompletionTransitionId;
			DestinationEcology = PendingEcology;
		}
		else if (bHasActiveRun && ActiveContext.RunSeed == ParsedRunSeed
			&& ActiveContext.FloorNumber == ParsedFloor && ActiveContext.GenerationSerial == ParsedGeneration)
		{
			DestinationContext = ActiveContext;
			DestinationKind = EEFCalystoDungeonTravelKind::Replay;
			DestinationEcology = RunEcology;
		}
	}
	else if (bHasRunSeed && !bHasFloor && !bHasGeneration && ParsedRunSeed > 0)
	{
		DestinationContext = MakeContext(ParsedRunSeed, 1, 1);
		DestinationEcology = BuildInitialEcology(ParsedRunSeed, CompiledPolicyHash, CachedDirectorPolicy->GeneratorVersion);
	}
	else
	{
		const int64 NewSeed = EFCalystoDungeonPrivate::CreatePositiveRunSeed();
		DestinationContext = MakeContext(NewSeed, 1, 1);
		DestinationEcology = BuildInitialEcology(NewSeed, CompiledPolicyHash, CachedDirectorPolicy->GeneratorVersion);
	}
	if (DestinationContext.RunSeed > 0 && DestinationContext.FloorNumber > 0
		&& DestinationContext.GenerationSerial > 0 && DestinationEcology.bInitialized)
	{
		DestinationContext.PCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
			DestinationContext.RunSeed,
			DestinationContext.FloorNumber,
			DestinationContext.GenerationSerial,
			CachedDirectorPolicy->GeneratorVersion,
			CompiledPolicyHash,
			DestinationEcology.EcologyHash);
	}

	FEFCalystoResolvedFloorIntent DestinationIntent;
	FEFCalystoRealizedFloorManifest DestinationExpectedManifest;
	if (bMatchesPendingContext)
	{
		DestinationIntent = PendingIntent;
		DestinationExpectedManifest = PendingExpectedManifest;
		const int32 RecomputedPCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
			ParsedRunSeed,
			ParsedFloor,
			ParsedGeneration,
			CachedDirectorPolicy->GeneratorVersion,
			CompiledPolicyHash,
			DestinationEcology.EcologyHash);
		if (!DestinationIntent.bIsValid || DestinationIntent.RunSeed != ParsedRunSeed
			|| DestinationIntent.FloorNumber != ParsedFloor || DestinationIntent.GenerationSerial != ParsedGeneration
			|| DestinationIntent.PCGSeed != RecomputedPCGSeed || DestinationIntent.PolicyHash != CompiledPolicyHash
			|| DestinationIntent.EcologyHash != DestinationEcology.EcologyHash)
		{
			RejectPendingTravel(TEXT("frozen destination intent mismatch"));
			return;
		}
	}
	else if (DestinationKind == EEFCalystoDungeonTravelKind::Replay && ActiveIntent.bIsValid
		&& ActiveIntent.RunSeed == DestinationContext.RunSeed && ActiveIntent.FloorNumber == DestinationContext.FloorNumber
		&& ActiveIntent.GenerationSerial == DestinationContext.GenerationSerial)
	{
		DestinationIntent = ActiveIntent;
		DestinationExpectedManifest = ActiveManifest;
	}
	else
	{
		FString Error;
		if (!ResolveFloorIntentForTesting(CachedDirectorPolicy, DestinationContext, DestinationEcology,
			FEFCalystoDirectorIntent(), FEFCalystoFloorOutcome(), DestinationIntent, Error))
		{
			PolicyError = Error;
			RejectPendingTravel(TEXT("destination Director V3 resolution failed"));
			return;
		}
	}

	const bool bAcceptedPendingTransaction = bMatchesPendingContext;
	const bool bAcceptedRecovery = bAcceptedPendingTransaction && bRecoveryTravelPending;
	const int32 AcceptedGenerationAttempt = bAcceptedPendingTransaction
		? FMath::Clamp(PendingGenerationAttempt, 1, MaximumGenerationAttempts)
		: 1;
	const int64 AcceptedRunEpoch = bAcceptedPendingTransaction && PendingRunEpoch > 0
		? PendingRunEpoch
		: (DestinationKind == EEFCalystoDungeonTravelKind::NewRun
			? FMath::Max<int64>(RunEpoch + 1, 1)
			: RunEpoch);
	if (AcceptedRunEpoch <= 0)
	{
		RejectPendingTravel(TEXT("destination RunEpoch is invalid"));
		return;
	}

	ActiveContext = DestinationContext;
	ActiveIntent = MoveTemp(DestinationIntent);
	ExpectedManifestForRealization = MoveTemp(DestinationExpectedManifest);
	ActiveManifest = FEFCalystoRealizedFloorManifest();
	RunEcology = DestinationEcology;
	if (bAcceptedPendingTransaction)
	{
		ReturnMapPackage = PendingReturnMapPackage;
		CommitAcceptedPendingInputs();
		ResolvedFloorPreloadHandle.Reset();
		ResolvedFloorPreloadHandle = MoveTemp(PendingResolvedFloorPreloadHandle);
	}
	bHasActiveRun = true;
	RunEpoch = AcceptedRunEpoch;
	LastTravelKind = DestinationKind;
	TravelState = EEFCalystoDungeonTravelState::AwaitingFloorReady;
	CurrentGenerationAttempt = AcceptedGenerationAttempt;
	if (!bAcceptedRecovery)
	{
		LastFailureCode = NAME_None;
		LastFailureMessage.Reset();
	}
	GenerationState = EEFCalystoGenerationState::Generating;
	AwaitingCompletionTransitionId = DestinationCompletionTransition;
	ResetPendingTravelTransaction();
	if (DestinationKind == EEFCalystoDungeonTravelKind::NewRun)
	{
		NewRunInitializedEvent.Broadcast(RunEpoch);
	}
	DirectorWorldAcceptedEvent.Broadcast(RunEpoch, DestinationKind, ActiveIntent);
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Calysto Director V3 ready: run=%lld floor=%lld generation=%lld pcg_seed=%d intent=%s ecology=%s world=%s."),
		ActiveContext.RunSeed, ActiveContext.FloorNumber, ActiveContext.GenerationSerial, ActiveContext.PCGSeed,
		*ActiveIntent.IntentHash, *RunEcology.EcologyHash, *GetNameSafe(World));
}
#endif

void UEFCalystoDungeonSubsystem::HandleTravelFailure(
	UWorld* World,
	const ETravelFailure::Type FailureType,
	const FString& ErrorString)
{
	if (!bTravelRequestPending || (World && World->GetGameInstance() != GetGameInstance()))
	{
		return;
	}
	UE_LOG(LogEFCalystoDungeon, Error, TEXT("Calysto floor travel failed (type=%d): %s"),
		static_cast<int32>(FailureType), *ErrorString);
	RejectPendingTravel(TEXT("engine travel failure"));
}

bool UEFCalystoDungeonSubsystem::HandleTravelWatchdog(const float DeltaTime)
{
	(void)DeltaTime;
	TravelWatchdogHandle.Reset();
	if (bTravelRequestPending)
	{
		RejectPendingTravel(TEXT("travel/preload watchdog"));
	}
	return false;
}

bool UEFCalystoDungeonSubsystem::IsConfiguredDungeonWorld(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
	{
		return false;
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!Settings || Settings->DungeonMap.IsNull())
	{
		return false;
	}
	const FString Configured = Settings->DungeonMap.ToSoftObjectPath().GetLongPackageName();
	const FString Actual = EFCalystoDungeonPrivate::NormalizeMapPackageName(World->GetPackage()->GetName());
	return !Configured.IsEmpty() && Actual.Equals(Configured, ESearchCase::IgnoreCase);
}

bool UEFCalystoDungeonSubsystem::CompilePolicy()
{
	if (bPolicyCompilationAttempted)
	{
		return bPolicyValid;
	}
	bPolicyCompilationAttempted = true;
	bPolicyValid = false;
	PolicyError.Reset();
	CompiledPolicyHash.Reset();
	CachedDirectorPolicy = nullptr;
	CachedDirectorPolicyV4 = nullptr;
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!Settings || Settings->DirectorPolicy.IsNull())
	{
		PolicyError = TEXT("Dungeon Director V4 policy path is not configured.");
		return false;
	}
	CachedDirectorPolicyV4 = Settings->DirectorPolicy.LoadSynchronous();
	if (!FEFCalystoDungeonDirectorResolverV4::Validate(CachedDirectorPolicyV4, PolicyError))
	{
		UE_LOG(LogEFCalystoDungeon, Error,
			TEXT("Dungeon Director V4 policy failed closed: %s"), *PolicyError);
		return false;
	}
	CompiledPolicyHash = FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(CachedDirectorPolicyV4);
	bPolicyValid = !CompiledPolicyHash.IsEmpty();
	if (!bPolicyValid)
	{
		PolicyError = TEXT("Dungeon Director V4 policy SHA-256 is empty.");
		return false;
	}
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Dungeon Director V4 policy compiled: id=%s styles=%d themes=%d certified_sizes=%d hash=%s."),
		*CachedDirectorPolicyV4->PolicyId.ToString(),
		CachedDirectorPolicyV4->Styles.Num(),
		CachedDirectorPolicyV4->Themes.Num(),
		CachedDirectorPolicyV4->ValidatedDungeonSizes.Num(),
		*CompiledPolicyHash);
	return true;
}

FEFCalystoDungeonGenerationContext UEFCalystoDungeonSubsystem::MakeContext(
	const int64 RunSeed,
	const int64 FloorNumber,
	const int64 GenerationSerial) const
{
	FEFCalystoDungeonGenerationContext Context;
	if (!bPolicyValid || !EFCalystoDungeonPrivate::IsValidContextNumbers(RunSeed, FloorNumber, GenerationSerial))
	{
		return Context;
	}
	Context.RunSeed = RunSeed;
	Context.FloorNumber = FloorNumber;
	Context.GenerationSerial = GenerationSerial;
	// Final PCG seed requires the candidate ecology and is frozen inside BeginTravel.
	Context.PCGSeed = 1;
	Context.PolicyHash = CompiledPolicyHash;
	return Context;
}

bool UEFCalystoDungeonSubsystem::BeginTravel(
	const FEFCalystoDungeonGenerationContext& TargetContext,
	const EEFCalystoDungeonTravelKind Kind)
{
	if (!bPolicyValid || !CachedDirectorPolicyV4 || !TargetContext.IsValid()
		|| bTravelRequestPending || bTravelPreparationInProgress
		|| TravelState == EEFCalystoDungeonTravelState::AwaitingFloorReady)
	{
		return false;
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!Settings || Settings->DungeonMap.IsNull() || !IsValid(World) || !World->IsGameWorld())
	{
		return false;
	}
	if (Kind == EEFCalystoDungeonTravelKind::NewRun && RunEpoch == MAX_int64)
	{
		UE_LOG(LogEFCalystoDungeon, Error, TEXT("V4 New Run rejected because RunEpoch is exhausted."));
		return false;
	}

	bExternalTravelPreparationFailed = false;
	ExternalTravelPreparationFailureCode = NAME_None;
	ExternalTravelPreparationFailureMessage.Reset();
	bHasSubmittedCompanionSnapshot = false;
	SubmittedCompanionSnapshotV4 = FEFCalystoCompanionSnapshotV4();
	{
		TGuardValue<bool> PreparationGuard(bTravelPreparationInProgress, true);
		BeforeAnyDirectorTravelEvent.Broadcast(
			static_cast<EEFCalystoDungeonTravelKindV4>(Kind));
	}
	if (bExternalTravelPreparationFailed || !bHasSubmittedCompanionSnapshot)
	{
		LastFailureCode = bExternalTravelPreparationFailed
			? ExternalTravelPreparationFailureCode
			: FName(TEXT("COMPANION_SNAPSHOT_MISSING"));
		LastFailureMessage = bExternalTravelPreparationFailed
			? ExternalTravelPreparationFailureMessage
			: TEXT("The V4 companion/inventory adapter did not submit a frozen pre-travel snapshot.");
		UE_LOG(LogEFCalystoDungeon, Error,
			TEXT("V4 Director travel preparation failed closed: code=%s message=%s"),
			*LastFailureCode.ToString(), *LastFailureMessage);
		FloorTravelFailedEvent.Broadcast();
		return false;
	}
	if (Kind == EEFCalystoDungeonTravelKind::NewRun
		&& (!SubmittedCompanionSnapshotV4.Records.IsEmpty()
			|| SubmittedCompanionSnapshotV4.bPlayerOwnsWintersRecall))
	{
		LastFailureCode = TEXT("NEW_RUN_ROSTER_NOT_EMPTY");
		LastFailureMessage = TEXT("New Run requires an empty companion roster and no Winter's Recall.");
		FloorTravelFailedEvent.Broadcast();
		return false;
	}

	const FString LongPackageName = Settings->DungeonMap.ToSoftObjectPath().GetLongPackageName();
	if (!FPackageName::IsValidLongPackageName(LongPackageName))
	{
		return false;
	}

	FEFCalystoRunEcologyStateV4 CandidateEcology = RunEcologyV4;
	FEFCalystoFloorOutcomeV4 FrozenOutcome;
	bool bHasFrozenOutcome = false;
	if (Kind == EEFCalystoDungeonTravelKind::NewRun)
	{
		CandidateEcology = BuildInitialEcologyV4(
			TargetContext.RunSeed, CompiledPolicyHash, CachedDirectorPolicyV4->GeneratorVersion);
		CandidateEcology.CompanionSnapshot = SubmittedCompanionSnapshotV4;
		CandidateEcology.EcologyHash = ComputeEcologyHashV4(CandidateEcology);
	}
	else if (Kind == EEFCalystoDungeonTravelKind::Advance)
	{
		FrozenOutcome = bHasSubmittedOutcome ? SubmittedOutcomeV4 : FEFCalystoFloorOutcomeV4();
		bHasFrozenOutcome = true;
		FString CommitError;
		if (!CommitAcceptedFloorToEcologyV4(FrozenOutcome, CandidateEcology, CommitError))
		{
			UE_LOG(LogEFCalystoDungeon, Error, TEXT("V4 Advance rejected: %s"), *CommitError);
			FloorTravelFailedEvent.Broadcast();
			return false;
		}
	}
	else if (Kind == EEFCalystoDungeonTravelKind::DebugJump)
	{
		CandidateEcology = BuildInitialEcologyV4(
			TargetContext.RunSeed, CompiledPolicyHash, CachedDirectorPolicyV4->GeneratorVersion);
		CandidateEcology.bDevelopmentSyntheticHistory = true;
		CandidateEcology.LastCommittedFloor = TargetContext.FloorNumber - 1;
		CandidateEcology.EcologyRevision = RunEcologyV4.EcologyRevision + 1;
		CandidateEcology.CompanionSnapshot = SubmittedCompanionSnapshotV4;
		CandidateEcology.EcologyHash = ComputeEcologyHashV4(CandidateEcology);
	}
	if (!CandidateEcology.bInitialized || CandidateEcology.EcologyHash.IsEmpty())
	{
		LastFailureCode = TEXT("V4_ECOLOGY_INVALID");
		LastFailureMessage = TEXT("Candidate V4 ecology failed canonical validation.");
		FloorTravelFailedEvent.Broadcast();
		return false;
	}

	const bool bUsesQueuedDirectorIntent = Kind != EEFCalystoDungeonTravelKind::Replay
		&& bHasQueuedDirectorIntent;
	FEFCalystoResolvedFloorIntentV4 ResolvedIntent;
	FEFCalystoRealizedFloorManifestV4 ExpectedManifest;
	if (Kind == EEFCalystoDungeonTravelKind::Replay
		&& HasActiveRun()
		&& ActiveContext.RunSeed == TargetContext.RunSeed
		&& ActiveContext.FloorNumber == TargetContext.FloorNumber
		&& ActiveContext.GenerationSerial == TargetContext.GenerationSerial
		&& ActiveManifestV4.bIsValid)
	{
		ResolvedIntent = ActiveIntentV4;
		ExpectedManifest = ActiveManifestV4;
		CandidateEcology = RunEcologyV4;
	}
	else
	{
		const FEFCalystoDirectorIntentV4 Intent = bUsesQueuedDirectorIntent
			? QueuedDirectorIntentV4
			: FEFCalystoDirectorIntentV4();
		const FEFCalystoCompanionSnapshotV4& FrozenCompanions =
			Kind == EEFCalystoDungeonTravelKind::Replay
				? ActiveIntentV4.CompanionSnapshot
				: SubmittedCompanionSnapshotV4;
		FEFCalystoResolveContextV4 ResolveContext;
		FString ResolveError;
		if (!BuildResolveContextV4(
				TargetContext,
				CandidateEcology,
				Intent,
				FrozenOutcome,
				bHasFrozenOutcome,
				FrozenCompanions,
				ResolveContext,
				ResolveError)
			|| !FEFCalystoDungeonDirectorResolverV4::Resolve(
				CachedDirectorPolicyV4, ResolveContext, ResolvedIntent, ResolveError))
		{
			UE_LOG(LogEFCalystoDungeon, Error,
				TEXT("V4 travel rejected because intent resolution failed: %s"), *ResolveError);
			FloorTravelFailedEvent.Broadcast();
			return false;
		}
	}

	FEFCalystoDungeonGenerationContext ResolvedTargetContext = TargetContext;
	ResolvedTargetContext.PCGSeed = ResolvedIntent.PCGSeed;
	if (!ResolvedTargetContext.IsValid() || ResolvedIntent.PolicyHash != CompiledPolicyHash
		|| ResolvedIntent.EcologyHash != CandidateEcology.EcologyHash)
	{
		return false;
	}

	const bool bSourceWasDungeon = IsConfiguredDungeonWorld(World);
	FString CandidateReturnMapPackage = ReturnMapPackage;
	if (!bSourceWasDungeon)
	{
		CandidateReturnMapPackage =
			EFCalystoDungeonPrivate::NormalizeMapPackageName(World->GetPackage()->GetName());
	}
	bRecoveryTravelPending = false;
	PendingContext = ResolvedTargetContext;
	PendingRunEpoch = Kind == EEFCalystoDungeonTravelKind::NewRun
		? FMath::Max<int64>(RunEpoch + 1, 1)
		: RunEpoch;
	PendingIntent = FEFCalystoResolvedFloorIntent();
	PendingExpectedManifest = FEFCalystoRealizedFloorManifest();
	PendingEcology = FEFCalystoRunEcologyState();
	PendingIntentV4 = MoveTemp(ResolvedIntent);
	PendingExpectedManifestV4 = MoveTemp(ExpectedManifest);
	PendingEcologyV4 = CandidateEcology;
	PendingSourceWorld = World;
	bPendingSourceWasDungeon = bSourceWasDungeon;
	PendingSourceTravelState = TravelState;
	PendingSourceGenerationState = GenerationState;
	PendingReturnMapPackage = MoveTemp(CandidateReturnMapPackage);
	PendingGenerationAttempt = 1;
	bPendingConsumesSubmittedOutcome = Kind == EEFCalystoDungeonTravelKind::Advance
		|| Kind == EEFCalystoDungeonTravelKind::NewRun;
	bPendingConsumesQueuedDirectorIntent = bUsesQueuedDirectorIntent;
	PendingTravelKind = Kind;
	NextTravelRequestId = NextTravelRequestId >= MAX_int64 ? 1 : NextTravelRequestId + 1;
	PendingTravelRequestId = NextTravelRequestId;
	PendingCompletionTransitionId = Kind == EEFCalystoDungeonTravelKind::Advance
		? FName(*FString::Printf(
			TEXT("Floor%lldToFloor%lld"), ActiveContext.FloorNumber, ResolvedTargetContext.FloorNumber))
		: NAME_None;
	bTravelRequestPending = true;
	bOpenLevelIssued = false;
	TravelState = EEFCalystoDungeonTravelState::TravelPending;
	ArmTravelWatchdog();
	PreloadResolvedFloorAssets(PendingIntentV4);
	return bTravelRequestPending;
}

bool UEFCalystoDungeonSubsystem::BeginTravelLegacyV3(
	const FEFCalystoDungeonGenerationContext& TargetContext,
	const EEFCalystoDungeonTravelKind Kind)
{
	if (!bPolicyValid || !TargetContext.IsValid() || bTravelRequestPending
		|| bTravelPreparationInProgress
		|| TravelState == EEFCalystoDungeonTravelState::AwaitingFloorReady)
	{
		return false;
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!Settings || Settings->DungeonMap.IsNull() || !IsValid(World) || !World->IsGameWorld())
	{
		return false;
	}
	if (Kind == EEFCalystoDungeonTravelKind::NewRun && RunEpoch == MAX_int64)
	{
		UE_LOG(LogEFCalystoDungeon, Error, TEXT("New Run rejected because RunEpoch is exhausted."));
		return false;
	}
	bExternalTravelPreparationFailed = false;
	ExternalTravelPreparationFailureCode = NAME_None;
	ExternalTravelPreparationFailureMessage.Reset();
	{
		// Project-owned travel adapters snapshot inventory and companions here for
		// every operation, before the immutable destination intent is resolved.
		TGuardValue<bool> PreparationGuard(bTravelPreparationInProgress, true);
		BeforeAnyDirectorTravelEvent.Broadcast(
			static_cast<EEFCalystoDungeonTravelKindV4>(Kind));
	}
	if (bExternalTravelPreparationFailed)
	{
		LastFailureCode = ExternalTravelPreparationFailureCode;
		LastFailureMessage = ExternalTravelPreparationFailureMessage;
		UE_LOG(LogEFCalystoDungeon, Error,
			TEXT("Director travel preparation failed closed: code=%s message=%s"),
			*LastFailureCode.ToString(),
			*LastFailureMessage);
		FloorTravelFailedEvent.Broadcast();
		return false;
	}
	const FString LongPackageName = Settings->DungeonMap.ToSoftObjectPath().GetLongPackageName();
	if (!FPackageName::IsValidLongPackageName(LongPackageName))
	{
		return false;
	}

	FEFCalystoRunEcologyState CandidateEcology = RunEcology;
	FEFCalystoFloorOutcome FrozenOutcome;
	if (Kind == EEFCalystoDungeonTravelKind::NewRun)
	{
		CandidateEcology = BuildInitialEcology(TargetContext.RunSeed, CompiledPolicyHash, CachedDirectorPolicy->GeneratorVersion);
	}
	else if (Kind == EEFCalystoDungeonTravelKind::Advance)
	{
		FrozenOutcome = bHasSubmittedOutcome ? SubmittedOutcome : FEFCalystoFloorOutcome();
		FString CommitError;
		if (!CommitOutcomeToEcology(CachedDirectorPolicy, ActiveContext.FloorNumber, ActiveIntent,
			FrozenOutcome, CandidateEcology, CommitError))
		{
			UE_LOG(LogEFCalystoDungeon, Error, TEXT("Advance rejected: %s"), *CommitError);
			return false;
		}
	}
	else if (Kind == EEFCalystoDungeonTravelKind::DebugJump)
	{
		CandidateEcology = BuildInitialEcology(TargetContext.RunSeed, CompiledPolicyHash, CachedDirectorPolicy->GeneratorVersion);
		CandidateEcology.bSyntheticHistory = true;
		CandidateEcology.LastCommittedFloor = TargetContext.FloorNumber - 1;
		CandidateEcology.Revision = RunEcology.Revision + 1;
		CandidateEcology.EcologyHash = EFCalystoDungeonPrivate::BuildEcologyHash(CandidateEcology);
	}
	FEFCalystoDungeonGenerationContext ResolvedTargetContext = TargetContext;
	ResolvedTargetContext.PCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
		TargetContext.RunSeed,
		TargetContext.FloorNumber,
		TargetContext.GenerationSerial,
		CachedDirectorPolicy->GeneratorVersion,
		CompiledPolicyHash,
		CandidateEcology.EcologyHash);
	if (!ResolvedTargetContext.IsValid())
	{
		return false;
	}

	const bool bUsesQueuedDirectorIntent = Kind != EEFCalystoDungeonTravelKind::Replay
		&& bHasQueuedDirectorIntent;
	FEFCalystoResolvedFloorIntent ResolvedIntent;
	if (Kind == EEFCalystoDungeonTravelKind::Replay && HasActiveRun()
		&& ActiveContext.RunSeed == ResolvedTargetContext.RunSeed
		&& ActiveContext.FloorNumber == ResolvedTargetContext.FloorNumber
		&& ActiveContext.GenerationSerial == ResolvedTargetContext.GenerationSerial)
	{
		if (!ActiveManifest.bIsValid)
		{
			return false;
		}
		ResolvedIntent = ActiveIntent;
		CandidateEcology = RunEcology;
	}
	else
	{
		const FEFCalystoDirectorIntent Intent = bUsesQueuedDirectorIntent
			? QueuedDirectorIntent
			: FEFCalystoDirectorIntent();
		FString ResolveError;
		int32 ForcedDungeonEdge = 0;
#if !UE_BUILD_SHIPPING
		ForcedDungeonEdge = DevelopmentForcedDungeonEdgeForAutomation;
#endif
		if (!ResolveFloorIntentInternal(CachedDirectorPolicy, ResolvedTargetContext, CandidateEcology,
			Intent, FrozenOutcome, ForcedDungeonEdge, ResolvedIntent, ResolveError))
		{
			UE_LOG(LogEFCalystoDungeon, Error, TEXT("Travel rejected: V3 intent could not be frozen: %s"), *ResolveError);
			return false;
		}
	}

	const bool bSourceWasDungeon = IsConfiguredDungeonWorld(World);
	FString CandidateReturnMapPackage = ReturnMapPackage;
	if (!bSourceWasDungeon)
	{
		CandidateReturnMapPackage = EFCalystoDungeonPrivate::NormalizeMapPackageName(World->GetPackage()->GetName());
	}
	bRecoveryTravelPending = false;
	PendingContext = ResolvedTargetContext;
	PendingRunEpoch = Kind == EEFCalystoDungeonTravelKind::NewRun
		? FMath::Max<int64>(RunEpoch + 1, 1)
		: RunEpoch;
	PendingIntent = MoveTemp(ResolvedIntent);
	PendingExpectedManifest = Kind == EEFCalystoDungeonTravelKind::Replay
		? ActiveManifest
		: FEFCalystoRealizedFloorManifest();
	PendingEcology = CandidateEcology;
	PendingSourceWorld = World;
	bPendingSourceWasDungeon = bSourceWasDungeon;
	PendingSourceTravelState = TravelState;
	PendingSourceGenerationState = GenerationState;
	PendingReturnMapPackage = MoveTemp(CandidateReturnMapPackage);
	PendingGenerationAttempt = 1;
	bPendingConsumesSubmittedOutcome = Kind == EEFCalystoDungeonTravelKind::Advance
		|| Kind == EEFCalystoDungeonTravelKind::NewRun;
	bPendingConsumesQueuedDirectorIntent = bUsesQueuedDirectorIntent;
	PendingTravelKind = Kind;
	NextTravelRequestId = NextTravelRequestId >= MAX_int64 ? 1 : NextTravelRequestId + 1;
	PendingTravelRequestId = NextTravelRequestId;
	PendingCompletionTransitionId = Kind == EEFCalystoDungeonTravelKind::Advance
		? FName(*FString::Printf(TEXT("Floor%lldToFloor%lld"), ActiveContext.FloorNumber, ResolvedTargetContext.FloorNumber))
		: NAME_None;
	bTravelRequestPending = true;
	bOpenLevelIssued = false;
	TravelState = EEFCalystoDungeonTravelState::TravelPending;
	ArmTravelWatchdog();
	PreloadResolvedFloorAssetsLegacyV3(PendingIntent);
	return bTravelRequestPending;
}

void UEFCalystoDungeonSubsystem::ExecutePendingTravel(const int64 TravelRequestId)
{
	if (!bTravelRequestPending || TravelRequestId <= 0 || TravelRequestId != PendingTravelRequestId
		|| bOpenLevelIssued || !PendingIntentV4.bIsValid)
	{
		return;
	}
	for (const FSoftObjectPath& Path : PendingResolvedFloorAssetPaths)
	{
		if (!Path.ResolveObject())
		{
			RejectPendingTravel(TEXT("selected V4 asset failed async preload"));
			return;
		}
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!Settings || Settings->DungeonMap.IsNull() || !IsValid(World))
	{
		RejectPendingTravel(TEXT("async preload completed without a valid travel world"));
		return;
	}
	bOpenLevelIssued = true;
	const FString TravelOptions = FString::Printf(
		TEXT("DungeonRunSeed=%lld?DungeonFloor=%lld?DungeonGeneration=%lld?DungeonTravelRequest=%lld"),
		PendingContext.RunSeed, PendingContext.FloorNumber, PendingContext.GenerationSerial, PendingTravelRequestId);
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("Opening Calysto V4 run=%lld floor=%lld generation=%lld pcg_seed=%d intent=%s via %s."),
		PendingContext.RunSeed, PendingContext.FloorNumber, PendingContext.GenerationSerial,
		PendingContext.PCGSeed, *PendingIntentV4.IntentHash,
		*Settings->DungeonMap.ToSoftObjectPath().GetLongPackageName());
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, Settings->DungeonMap, true, TravelOptions);
}

void UEFCalystoDungeonSubsystem::CommitAcceptedPendingInputs()
{
	if (bPendingConsumesSubmittedOutcome)
	{
		bHasSubmittedOutcome = false;
		SubmittedOutcome = FEFCalystoFloorOutcome();
		SubmittedOutcomeV4 = FEFCalystoFloorOutcomeV4();
	}
	if (bPendingConsumesQueuedDirectorIntent)
	{
		bHasQueuedDirectorIntent = false;
		QueuedDirectorIntent = FEFCalystoDirectorIntent();
		QueuedDirectorIntentV4 = FEFCalystoDirectorIntentV4();
	}
	bHasSubmittedCompanionSnapshot = false;
	SubmittedCompanionSnapshot = FEFCalystoCompanionRunSnapshot();
	SubmittedCompanionSnapshotV4 = FEFCalystoCompanionSnapshotV4();
}

void UEFCalystoDungeonSubsystem::ResetPendingTravelTransaction()
{
	bTravelRequestPending = false;
	bOpenLevelIssued = false;
	PendingTravelRequestId = 0;
	PendingContext = FEFCalystoDungeonGenerationContext();
	PendingRunEpoch = 0;
	PendingIntent = FEFCalystoResolvedFloorIntent();
	PendingExpectedManifest = FEFCalystoRealizedFloorManifest();
	PendingIntentV4 = FEFCalystoResolvedFloorIntentV4();
	PendingExpectedManifestV4 = FEFCalystoRealizedFloorManifestV4();
	PendingEcology = FEFCalystoRunEcologyState();
	PendingEcologyV4 = FEFCalystoRunEcologyStateV4();
	PendingSourceWorld.Reset();
	bPendingSourceWasDungeon = false;
	PendingSourceTravelState = EEFCalystoDungeonTravelState::Idle;
	PendingSourceGenerationState = EEFCalystoGenerationState::Idle;
	PendingReturnMapPackage.Reset();
	PendingGenerationAttempt = 0;
	bPendingConsumesSubmittedOutcome = false;
	bPendingConsumesQueuedDirectorIntent = false;
	PendingTravelKind = EEFCalystoDungeonTravelKind::None;
	PendingCompletionTransitionId = NAME_None;
	bRecoveryTravelPending = false;
	PendingResolvedFloorAssetPaths.Reset();
	PendingResolvedFloorPreloadHandle.Reset();
}

bool UEFCalystoDungeonSubsystem::ReloadFrozenIntentForRecovery()
{
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!Settings || Settings->DungeonMap.IsNull() || !IsConfiguredDungeonWorld(World)
		|| !ActiveIntentV4.bIsValid || !RunEcologyV4.bInitialized)
	{
		ReturnFromFailedDungeon();
		return false;
	}
	bExternalTravelPreparationFailed = false;
	ExternalTravelPreparationFailureCode = NAME_None;
	ExternalTravelPreparationFailureMessage.Reset();
	bHasSubmittedCompanionSnapshot = false;
	SubmittedCompanionSnapshotV4 = FEFCalystoCompanionSnapshotV4();
	{
		TGuardValue<bool> PreparationGuard(bTravelPreparationInProgress, true);
		BeforeAnyDirectorTravelEvent.Broadcast(EEFCalystoDungeonTravelKindV4::Replay);
	}
	const FString SubmittedSnapshotHash =
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(SubmittedCompanionSnapshotV4);
	if (bExternalTravelPreparationFailed || !bHasSubmittedCompanionSnapshot
		|| SubmittedSnapshotHash != ActiveIntentV4.CompanionSnapshotHash)
	{
		LastFailureCode = bExternalTravelPreparationFailed
			? ExternalTravelPreparationFailureCode
			: FName(TEXT("RECOVERY_SNAPSHOT_MISMATCH"));
		LastFailureMessage = bExternalTravelPreparationFailed
			? ExternalTravelPreparationFailureMessage
			: TEXT("Retry could not restore the exact frozen V4 companion snapshot.");
		ReturnFromFailedDungeon();
		return false;
	}
	PendingContext = ActiveContext;
	PendingRunEpoch = RunEpoch;
	PendingIntent = FEFCalystoResolvedFloorIntent();
	PendingExpectedManifest = FEFCalystoRealizedFloorManifest();
	PendingIntentV4 = ActiveIntentV4;
	PendingExpectedManifestV4 = ExpectedManifestForRealizationV4.bIsValid
		? ExpectedManifestForRealizationV4
		: ActiveManifestV4;
	PendingEcology = FEFCalystoRunEcologyState();
	PendingEcologyV4 = RunEcologyV4;
	PendingSourceWorld = World;
	bPendingSourceWasDungeon = true;
	PendingSourceTravelState = TravelState;
	PendingSourceGenerationState = GenerationState;
	PendingReturnMapPackage = ReturnMapPackage;
	PendingGenerationAttempt = FMath::Min(CurrentGenerationAttempt + 1, MaximumGenerationAttempts);
	bPendingConsumesSubmittedOutcome = false;
	bPendingConsumesQueuedDirectorIntent = false;
	PendingTravelKind = EEFCalystoDungeonTravelKind::Replay;
	NextTravelRequestId = NextTravelRequestId >= MAX_int64 ? 1 : NextTravelRequestId + 1;
	PendingTravelRequestId = NextTravelRequestId;
	PendingCompletionTransitionId = AwaitingCompletionTransitionId;
	bTravelRequestPending = true;
	bOpenLevelIssued = false;
	bRecoveryTravelPending = true;
	TravelState = EEFCalystoDungeonTravelState::TravelPending;
	GenerationState = EEFCalystoGenerationState::Recovering;
	ArmTravelWatchdog();
	PreloadResolvedFloorAssets(PendingIntentV4);
	return bTravelRequestPending;
}

void UEFCalystoDungeonSubsystem::ReturnFromFailedDungeon()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		return;
	}
	static const FString HubPackage(TEXT("/Game/_Game/Hub/HUB"));
	FString Destination = ReturnMapPackage;
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	const FString DungeonPackage = Settings ? Settings->DungeonMap.ToSoftObjectPath().GetLongPackageName() : FString();
	if (!FPackageName::IsValidLongPackageName(Destination)
		|| Destination.Equals(DungeonPackage, ESearchCase::IgnoreCase)
		|| !FPackageName::DoesPackageExist(Destination))
	{
		Destination = HubPackage;
	}
	bTravelRequestPending = false;
	bOpenLevelIssued = false;
	bRecoveryTravelPending = false;
	TravelState = EEFCalystoDungeonTravelState::Idle;
	GenerationState = EEFCalystoGenerationState::Returning;
	FloorTravelFailedEvent.Broadcast();
	UE_LOG(LogEFCalystoDungeon, Error, TEXT("Calysto V4 exhausted attempts. Returning to %s; code=%s intent=%s."),
		*Destination, *LastFailureCode.ToString(), *ActiveIntentV4.IntentHash);
	UGameplayStatics::OpenLevel(World, FName(*Destination), true);
}

void UEFCalystoDungeonSubsystem::ArmTravelWatchdog()
{
	CancelTravelWatchdog();
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	const float Delay = Settings ? FMath::Clamp(Settings->TravelWatchdogSeconds, 1.0f, 30.0f) : 30.0f;
	TravelWatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoDungeonSubsystem::HandleTravelWatchdog), Delay);
}

void UEFCalystoDungeonSubsystem::CancelTravelWatchdog()
{
	if (TravelWatchdogHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TravelWatchdogHandle);
		TravelWatchdogHandle.Reset();
	}
}

void UEFCalystoDungeonSubsystem::RejectPendingTravel(const TCHAR* Reason)
{
	CancelTravelWatchdog();
	if (PendingResolvedFloorPreloadHandle.IsValid())
	{
		PendingResolvedFloorPreloadHandle->CancelHandle();
		PendingResolvedFloorPreloadHandle.Reset();
	}
	const bool bHadPending = bTravelRequestPending;
	const bool bWasRecovery = bRecoveryTravelPending;
	const bool bSourceWasDungeon = bPendingSourceWasDungeon;
	const EEFCalystoDungeonTravelState SourceTravelState = PendingSourceTravelState;
	const EEFCalystoGenerationState SourceGenerationState = PendingSourceGenerationState;
	const FString RejectedReturnMapPackage = PendingReturnMapPackage;
	const int32 RejectedGenerationAttempt = PendingGenerationAttempt;
	const TWeakObjectPtr<UWorld> SourceWorld = PendingSourceWorld;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const bool bSourceWorldStillCurrent = SourceWorld.IsValid() && SourceWorld.Get() == World;
	const bool bCurrentWorldIsDungeon = IsConfiguredDungeonWorld(World);
	ResetPendingTravelTransaction();
	UE_LOG(LogEFCalystoDungeon, Error, TEXT("Calysto V4 pending travel cleared: %s."), Reason ? Reason : TEXT("unknown"));
	if (!bHadPending)
	{
		LastFailureCode = TEXT("TRAVEL_CONTEXT_REJECTED");
		LastFailureMessage = Reason ? Reason : TEXT("unknown travel context rejection");
		if (bCurrentWorldIsDungeon)
		{
			ReturnFromFailedDungeon();
		}
		else
		{
			FloorTravelFailedEvent.Broadcast();
		}
		return;
	}

	if (bWasRecovery)
	{
		LastFailureCode = TEXT("TRAVEL_CONTEXT_REJECTED");
		LastFailureMessage = Reason ? Reason : TEXT("recovery travel context rejection");
		CurrentGenerationAttempt = FMath::Clamp(
			FMath::Max(CurrentGenerationAttempt, RejectedGenerationAttempt), 1, MaximumGenerationAttempts);
		TravelState = bCurrentWorldIsDungeon && HasActiveRun()
			? EEFCalystoDungeonTravelState::AwaitingFloorReady
			: EEFCalystoDungeonTravelState::Idle;
		GenerationState = EEFCalystoGenerationState::Failed;
		ReturnFromFailedDungeon();
		return;
	}

	if (bSourceWorldStillCurrent)
	{
		// Preload/OpenLevel rejection did not replace the source world. All canonical
		// run state was left untouched, so the same operation can be requested again.
		TravelState = SourceTravelState;
		GenerationState = SourceGenerationState;
		FloorTravelFailedEvent.Broadcast();
		return;
	}

	LastFailureCode = TEXT("TRAVEL_CONTEXT_REJECTED");
	LastFailureMessage = Reason ? Reason : TEXT("unknown travel context rejection");
	if (bSourceWasDungeon && bCurrentWorldIsDungeon && HasActiveRun())
	{
		// The source floor was replaced before the destination rejected its frozen
		// context. Replay the still-uncommitted source state without consuming the
		// submitted outcome or queued intent.
		TravelState = EEFCalystoDungeonTravelState::AwaitingFloorReady;
		GenerationState = EEFCalystoGenerationState::Failed;
		if (!NotifyGenerationFailed(LastFailureCode, LastFailureMessage))
		{
			ReturnFromFailedDungeon();
		}
		return;
	}

	ReturnMapPackage = RejectedReturnMapPackage;
	TravelState = EEFCalystoDungeonTravelState::Idle;
	GenerationState = EEFCalystoGenerationState::Failed;
	ReturnFromFailedDungeon();
}

void UEFCalystoDungeonSubsystem::PreloadCoreRuntimeAssets()
{
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!Settings)
	{
		return;
	}
	if (UObject* Asset = Settings->DungeonMeshDataAsset.LoadSynchronous()) { PreloadedCoreAssets.AddUnique(Asset); }
	if (UObject* Asset = Settings->SpawnerDataAsset.LoadSynchronous()) { PreloadedCoreAssets.AddUnique(Asset); }
	if (UObject* Asset = Settings->RoomThemeDataAsset.LoadSynchronous()) { PreloadedCoreAssets.AddUnique(Asset); }
	if (UObject* Asset = Settings->DungeonFloorDoorMesh.LoadSynchronous()) { PreloadedCoreAssets.AddUnique(Asset); }
	if (UClass* Asset = Settings->DungeonFloorDoorClass.LoadSynchronous()) { PreloadedCoreAssets.AddUnique(Asset); }
	if (UClass* Asset = Settings->PopulationAnchorClass.LoadSynchronous()) { PreloadedCoreAssets.AddUnique(Asset); }
}

bool UEFCalystoDungeonSubsystem::GatherResolvedFloorAssetPathsV4(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	TArray<FSoftObjectPath>& OutPaths,
	FString& OutError)
{
	OutPaths.Reset();
	OutError.Reset();
	if (!Intent.bIsValid || Intent.GeneratorVersion != 4 || Intent.IntentHash.IsEmpty())
	{
		OutError = TEXT("Cannot gather preload paths from an invalid V4 floor intent.");
		return false;
	}

	TSet<FSoftObjectPath> UniquePaths;
	auto AddPath = [&UniquePaths](const FSoftObjectPath& Path)
	{
		if (Path.IsValid())
		{
			UniquePaths.Add(Path);
		}
	};
	AddPath(Intent.CalystoRoomType.ToSoftObjectPath());
	for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
	{
		AddPath(Directive.ActorClass.ToSoftObjectPath());
	}
	for (const FEFCalystoChestContentDirectiveV4& Content : Intent.ChestContentDirectives)
	{
		AddPath(Content.ContentClass.ToSoftObjectPath());
	}

	TArray<FSoftObjectPath> BridgePaths;
	if (!IEFCalystoPopulationBridgeV4::GatherRegisteredAdditionalPreloadPaths(
			Intent, BridgePaths, OutError))
	{
		return false;
	}
	for (const FSoftObjectPath& BridgePath : BridgePaths)
	{
		AddPath(BridgePath);
	}

	OutPaths = UniquePaths.Array();
	OutPaths.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right)
	{
		return Left.ToString() < Right.ToString();
	});
	return true;
}

void UEFCalystoDungeonSubsystem::PreloadResolvedFloorAssets(
	const FEFCalystoResolvedFloorIntentV4& Intent)
{
	if (PendingResolvedFloorPreloadHandle.IsValid())
	{
		PendingResolvedFloorPreloadHandle->CancelHandle();
		PendingResolvedFloorPreloadHandle.Reset();
	}
	FString BridgeError;
	TArray<FSoftObjectPath> Paths;
	if (!GatherResolvedFloorAssetPathsV4(Intent, Paths, BridgeError))
	{
		PendingResolvedFloorAssetPaths.Reset();
		const FString PreloadFailure = FString::Printf(
			TEXT("V4 project-owned preload contract failed: %s"),
			*BridgeError);
		RejectPendingTravel(*PreloadFailure);
		return;
	}
	PendingResolvedFloorAssetPaths = Paths;
	if (Paths.IsEmpty())
	{
		ExecutePendingTravel(PendingTravelRequestId);
		return;
	}
	const int64 TravelRequestId = PendingTravelRequestId;
	TSharedPtr<FStreamableHandle> NewPreloadHandle =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			Paths,
			FStreamableDelegate::CreateUObject(
				this, &UEFCalystoDungeonSubsystem::ExecutePendingTravel, TravelRequestId));
	if (!bTravelRequestPending || PendingTravelRequestId != TravelRequestId)
	{
		if (NewPreloadHandle.IsValid())
		{
			NewPreloadHandle->CancelHandle();
		}
		return;
	}
	PendingResolvedFloorPreloadHandle = MoveTemp(NewPreloadHandle);
	if (!PendingResolvedFloorPreloadHandle.IsValid())
	{
		RejectPendingTravel(TEXT("selected V4 assets could not begin async preload"));
	}
}

void UEFCalystoDungeonSubsystem::PreloadResolvedFloorAssetsLegacyV3(
	const FEFCalystoResolvedFloorIntent& Intent)
{
	if (PendingResolvedFloorPreloadHandle.IsValid())
	{
		PendingResolvedFloorPreloadHandle->CancelHandle();
		PendingResolvedFloorPreloadHandle.Reset();
	}
	TSet<FSoftObjectPath> UniquePaths;
	for (const FEFCalystoThemeWeight& Theme : Intent.ThemeWeights)
	{
		if (Theme.RoomType.ToSoftObjectPath().IsValid()) { UniquePaths.Add(Theme.RoomType.ToSoftObjectPath()); }
	}
	for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
	{
		if (Directive.ActorClass.ToSoftObjectPath().IsValid()) { UniquePaths.Add(Directive.ActorClass.ToSoftObjectPath()); }
	}
	TArray<FSoftObjectPath> Paths = UniquePaths.Array();
	Paths.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
	PendingResolvedFloorAssetPaths = Paths;
	if (Paths.IsEmpty())
	{
		ExecutePendingTravel(PendingTravelRequestId);
		return;
	}
	const int64 TravelRequestId = PendingTravelRequestId;
	TSharedPtr<FStreamableHandle> NewPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Paths, FStreamableDelegate::CreateUObject(
			this, &UEFCalystoDungeonSubsystem::ExecutePendingTravel, TravelRequestId));
	if (!bTravelRequestPending || PendingTravelRequestId != TravelRequestId)
	{
		if (NewPreloadHandle.IsValid())
		{
			NewPreloadHandle->CancelHandle();
		}
		return;
	}
	PendingResolvedFloorPreloadHandle = MoveTemp(NewPreloadHandle);
	if (!PendingResolvedFloorPreloadHandle.IsValid())
	{
		RejectPendingTravel(TEXT("selected V3 assets could not begin async preload"));
	}
}
