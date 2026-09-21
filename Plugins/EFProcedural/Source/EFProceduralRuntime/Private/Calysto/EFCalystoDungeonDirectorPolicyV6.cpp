#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"

#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoDungeonDirectorPolicyV6, Log, All);

const FName UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId(TEXT("NoTheme"));

namespace EFCalystoPolicyV6Private
{
constexpr float ProbabilityTolerance = 1.0e-6f;
constexpr uint64 PositiveInt64Mask = 0x7FFFFFFFFFFFFFFFULL;

// FPlatformMisc::GetSHA256Signature is deliberately unimplemented on some UE
// platforms (including UE 5.8 Win64's generic path). Canonical gameplay hashes
// must never depend on that asserting platform hook.
FString Sha256(const uint8* Data, const uint64 ByteCount)
{
	static constexpr uint32 RoundConstants[64] = {
		0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
		0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
		0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
		0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
		0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
		0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
		0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
		0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
		0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
		0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
		0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
		0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
		0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
		0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
		0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
		0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
	};
	static constexpr uint32 InitialState[8] = {
		0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
		0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
	};
	auto RotateRight = [](const uint32 Value, const uint32 Shift)
	{
		return (Value >> Shift) | (Value << (32u - Shift));
	};

	if (ByteCount > static_cast<uint64>(MAX_int32) || (ByteCount > 0 && Data == nullptr))
	{
		return FString();
	}

	TArray<uint8> Message;
	Message.Reserve(static_cast<int32>(ByteCount) + 72);
	if (ByteCount > 0)
	{
		Message.Append(Data, static_cast<int32>(ByteCount));
	}
	Message.Add(0x80u);
	while ((Message.Num() % 64) != 56)
	{
		Message.Add(0u);
	}
	const uint64 BitCount = ByteCount * 8u;
	for (int32 Shift = 56; Shift >= 0; Shift -= 8)
	{
		Message.Add(static_cast<uint8>((BitCount >> Shift) & 0xFFu));
	}

	uint32 State[8];
	FMemory::Memcpy(State, InitialState, sizeof(State));
	for (int32 BlockOffset = 0; BlockOffset < Message.Num(); BlockOffset += 64)
	{
		uint32 Schedule[64]{};
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const int32 Offset = BlockOffset + Index * 4;
			Schedule[Index] = (static_cast<uint32>(Message[Offset]) << 24) |
				(static_cast<uint32>(Message[Offset + 1]) << 16) |
				(static_cast<uint32>(Message[Offset + 2]) << 8) |
				static_cast<uint32>(Message[Offset + 3]);
		}
		for (int32 Index = 16; Index < 64; ++Index)
		{
			const uint32 A = Schedule[Index - 15];
			const uint32 B = Schedule[Index - 2];
			const uint32 Sigma0 = RotateRight(A, 7) ^ RotateRight(A, 18) ^ (A >> 3);
			const uint32 Sigma1 = RotateRight(B, 17) ^ RotateRight(B, 19) ^ (B >> 10);
			Schedule[Index] = Schedule[Index - 16] + Sigma0 + Schedule[Index - 7] + Sigma1;
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
			const uint32 BigSigma1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
			const uint32 Choice = (E & F) ^ ((~E) & G);
			const uint32 Temp1 = H + BigSigma1 + Choice + RoundConstants[Index] + Schedule[Index];
			const uint32 BigSigma0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
			const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
			const uint32 Temp2 = BigSigma0 + Majority;
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

	return FString::Printf(
		TEXT("%08X%08X%08X%08X%08X%08X%08X%08X"), State[0], State[1],
		State[2], State[3], State[4], State[5], State[6], State[7]);
}

bool Fail(FString& OutError, const FString& Message)
{
	OutError = Message;
	return false;
}

FString CanonicalToken(FString Value)
{
	Value.TrimStartAndEndInline();
	Value.ToLowerInline();
	return Value;
}

FString CanonicalToken(const FName Value)
{
	return CanonicalToken(Value.ToString());
}

FString FloatBits(const float Value)
{
	float CanonicalValue = Value == 0.0f ? 0.0f : Value;
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &CanonicalValue, sizeof(Bits));
	return FString::Printf(TEXT("%08X"), Bits);
}

FString DoubleBits(const double Value)
{
	double CanonicalValue = Value == 0.0 ? 0.0 : Value;
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &CanonicalValue, sizeof(Bits));
	return FString::Printf(TEXT("%016llX"), static_cast<unsigned long long>(Bits));
}

FString ColorBits(const FLinearColor& Value)
{
	return FString::Printf(TEXT("%s,%s,%s,%s"), *FloatBits(Value.R),
		*FloatBits(Value.G), *FloatBits(Value.B), *FloatBits(Value.A));
}

FString PathToken(const FSoftObjectPath& Path)
{
	return CanonicalToken(Path.ToString());
}

template <typename T>
FString PathToken(const TSoftObjectPtr<T>& Pointer)
{
	return PathToken(Pointer.ToSoftObjectPath());
}

template <typename T>
FString PathToken(const TSoftClassPtr<T>& Pointer)
{
	return PathToken(Pointer.ToSoftObjectPath());
}

uint64 Mix64(uint64 Value)
{
	Value += 0x9E3779B97F4A7C15ULL;
	Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ULL;
	Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBULL;
	return Value ^ (Value >> 31);
}

uint64 HashString64(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	uint64 Hash = 1469598103934665603ULL;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
		Hash *= 1099511628211ULL;
	}
	return Mix64(Hash);
}

double UniformFromHash(const uint64 Hash)
{
	return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
}

bool IsFiniteUnit(const float Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
}

bool IsFiniteNonNegative(const float Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0f;
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) &&
		FMath::IsFinite(Value.Z);
}

bool IsFiniteRotator(const FRotator& Value)
{
	return FMath::IsFinite(Value.Pitch) && FMath::IsFinite(Value.Yaw) &&
		FMath::IsFinite(Value.Roll);
}

bool IsOrderedVectorRange(const FVector& Minimum, const FVector& Maximum)
{
	return Minimum.X <= Maximum.X && Minimum.Y <= Maximum.Y &&
		Minimum.Z <= Maximum.Z;
}

bool IsSha256(const FString& Value)
{
	if (Value.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (!FChar::IsHexDigit(Character))
		{
			return false;
		}
	}
	return true;
}

bool StableNameLess(const FName A, const FName B)
{
	return CanonicalToken(A) < CanonicalToken(B);
}

bool BuildAliasTable(const TArray<float>& Weights, TArray<float>& OutProbability,
	TArray<int32>& OutAlias)
{
	OutProbability.Reset();
	OutAlias.Reset();
	if (Weights.IsEmpty())
	{
		return false;
	}
	double Total = 0.0;
	for (const float Weight : Weights)
	{
		if (!FMath::IsFinite(Weight) || Weight <= 0.0f)
		{
			return false;
		}
		Total += Weight;
	}
	const int32 Count = Weights.Num();
	OutProbability.SetNumUninitialized(Count);
	OutAlias.SetNumUninitialized(Count);
	TArray<double> Scaled;
	Scaled.SetNumUninitialized(Count);
	TArray<int32> Small;
	TArray<int32> Large;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Scaled[Index] = static_cast<double>(Weights[Index]) * Count / Total;
		(Scaled[Index] < 1.0 ? Small : Large).Add(Index);
		OutAlias[Index] = Index;
	}
	while (!Small.IsEmpty() && !Large.IsEmpty())
	{
		const int32 SmallIndex = Small.Pop(EAllowShrinking::No);
		const int32 LargeIndex = Large.Pop(EAllowShrinking::No);
		OutProbability[SmallIndex] = static_cast<float>(Scaled[SmallIndex]);
		OutAlias[SmallIndex] = LargeIndex;
		Scaled[LargeIndex] = (Scaled[LargeIndex] + Scaled[SmallIndex]) - 1.0;
		(Scaled[LargeIndex] < 1.0 ? Small : Large).Add(LargeIndex);
	}
	for (const int32 Index : Large)
	{
		OutProbability[Index] = 1.0f;
		OutAlias[Index] = Index;
	}
	for (const int32 Index : Small)
	{
		OutProbability[Index] = 1.0f;
		OutAlias[Index] = Index;
	}
	return true;
}

int32 SelectAliasIndex(const double Uniform, const TArray<float>& Probability,
	const TArray<int32>& Alias)
{
	if (Probability.IsEmpty() || Probability.Num() != Alias.Num())
	{
		return INDEX_NONE;
	}
	const double Scaled = FMath::Clamp(Uniform, 0.0, 1.0 - UE_DOUBLE_SMALL_NUMBER) * Probability.Num();
	const int32 Column = FMath::Clamp(FMath::FloorToInt(Scaled), 0, Probability.Num() - 1);
	const double Fraction = Scaled - Column;
	const int32 Result = Fraction < static_cast<double>(Probability[Column]) ? Column : Alias[Column];
	return Probability.IsValidIndex(Result) ? Result : INDEX_NONE;
}

template <typename T>
void AddUniquePath(TArray<FSoftObjectPath>& Paths, const T& Pointer)
{
	const FSoftObjectPath Path = Pointer.ToSoftObjectPath();
	if (!Path.IsNull() && !Paths.Contains(Path))
	{
		Paths.Add(Path);
	}
}

void SortPaths(TArray<FSoftObjectPath>& Paths)
{
	Paths.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return CanonicalToken(A.ToString()) < CanonicalToken(B.ToString());
	});
}

TSoftObjectPtr<UMaterialInstance> SoftSurfaceMaterial(const TCHAR* Path)
{
	return TSoftObjectPtr<UMaterialInstance>(FSoftObjectPath(Path));
}

TSoftObjectPtr<UStaticMesh> SoftStaticMesh(const TCHAR* Path)
{
	return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path));
}

TSoftObjectPtr<UMaterialInterface> SoftMaterial(const TCHAR* Path)
{
	return TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Path));
}

TSoftObjectPtr<UObject> SoftObject(const TCHAR* Path)
{
	return TSoftObjectPtr<UObject>(FSoftObjectPath(Path));
}

TSoftObjectPtr<UTexture2D> SoftTexture(const TCHAR* Path)
{
	return TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path));
}

TSoftClassPtr<AActor> SoftActorClass(const TCHAR* Path)
{
	return TSoftClassPtr<AActor>(FSoftObjectPath(Path));
}

TSoftClassPtr<UObject> SoftObjectClass(const TCHAR* Path)
{
	return TSoftClassPtr<UObject>(FSoftObjectPath(Path));
}

FEFCalystoSurfaceMaterialSetV6 MakeMaterialSet(const TCHAR* Path)
{
	FEFCalystoSurfaceMaterialSetV6 Result;
	Result.FloorMaterial = SoftSurfaceMaterial(Path);
	Result.WallMaterial = SoftSurfaceMaterial(Path);
	Result.RoofMaterial = SoftSurfaceMaterial(Path);
	return Result;
}

FEFCalystoArchitectureTransformV6 MakeArchitectureTransform(
	const bool bUniformScale = true,
	const FVector& LocationOffset = FVector::ZeroVector,
	const FRotator& RotationOffset = FRotator::ZeroRotator,
	const FVector& Scale = FVector::OneVector)
{
	FEFCalystoArchitectureTransformV6 Result;
	Result.bUniformScale = bUniformScale;
	Result.LocationOffset = LocationOffset;
	Result.RotationOffset = RotationOffset;
	Result.Scale = Scale;
	return Result;
}

FEFCalystoArchitectureVariationV6 MakeArchitectureVariation(
	const FVector& LocationMinimum = FVector::ZeroVector,
	const FVector& LocationMaximum = FVector::ZeroVector,
	const FRotator& RotationMinimum = FRotator::ZeroRotator,
	const FRotator& RotationMaximum = FRotator::ZeroRotator,
	const FVector& ScaleMinimum = FVector::OneVector,
	const FVector& ScaleMaximum = FVector::OneVector,
	const bool bUniformScale = true)
{
	FEFCalystoArchitectureVariationV6 Result;
	Result.LocationMinimum = LocationMinimum;
	Result.LocationMaximum = LocationMaximum;
	Result.RotationMinimum = RotationMinimum;
	Result.RotationMaximum = RotationMaximum;
	Result.bUniformScale = bUniformScale;
	Result.ScaleMinimum = ScaleMinimum;
	Result.ScaleMaximum = ScaleMaximum;
	return Result;
}

FEFCalystoArchitectureMeshV6 MakeArchitectureMesh(
	const TCHAR* MeshPath, const float Weight = 1.0f,
	const FEFCalystoArchitectureTransformV6& Transform = FEFCalystoArchitectureTransformV6())
{
	FEFCalystoArchitectureMeshV6 Result;
	Result.Mesh = SoftStaticMesh(MeshPath);
	Result.SelectionWeight = Weight;
	Result.Transform = Transform;
	return Result;
}

FEFCalystoArchitectureActorV6 MakeArchitectureActor(
	const TCHAR* ActorPath, const float Weight = 1.0f,
	const FEFCalystoArchitectureTransformV6& Transform = FEFCalystoArchitectureTransformV6())
{
	FEFCalystoArchitectureActorV6 Result;
	Result.ActorClass = SoftActorClass(ActorPath);
	Result.SelectionWeight = Weight;
	Result.Transform = Transform;
	return Result;
}

FEFCalystoArchitectureObjectV6 MakeArchitectureObject(
	const EEFCalystoArchitectureObjectTypeV6 Type,
	const int32 Weight,
	const EEFCalystoArchitectureRotationV6 Rotation,
	const FEFCalystoArchitectureVariationV6& Variation)
{
	FEFCalystoArchitectureObjectV6 Result;
	Result.Type = Type;
	Result.SelectionWeight = Weight;
	Result.Rotation = Rotation;
	Result.Variation = Variation;
	return Result;
}

FEFCalystoStyleArchitectureV6 MakeDefaultStyleArchitecture()
{
	FEFCalystoStyleArchitectureV6 Result;
	Result.Floor.Add(MakeArchitectureMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Floor.SM_Floor")));
	Result.Wall.Add(MakeArchitectureMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Wall.SM_Wall"), 1.0f,
		MakeArchitectureTransform(false, FVector::ZeroVector,
			FRotator::ZeroRotator, FVector(1.01, 1.0, 1.0))));
	Result.Roof.Add(MakeArchitectureMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Roof.SM_Roof")));

	FEFCalystoArchitectureDoorwayV6& WallDoor = Result.WallDoor.AddDefaulted_GetRef();
	WallDoor.WallMesh = SoftStaticMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_WallDoor.SM_WallDoor"));
	WallDoor.FrameMesh = SoftStaticMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_IronDoorFrameClean.SM_IronDoorFrameClean"));
	WallDoor.DoorClass = SoftActorClass(
		TEXT("/Game/Calysto/Dungeon/Blueprint/Utility/BP_Door.BP_Door_C"));
	WallDoor.DoorTransform = MakeArchitectureTransform(true,
		FVector(-70.218424, 0.0, 0.0), FRotator(0.0, 124.242083, 0.0),
		FVector(1.2, 1.2, 1.2));

	Result.DoorFrame.Add(MakeArchitectureMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_IronDoorFrameClean.SM_IronDoorFrameClean"),
		1.0f, MakeArchitectureTransform(false, FVector::ZeroVector,
			FRotator::ZeroRotator, FVector(0.999802, 1.832301, 0.934894))));
	Result.Door.Add(MakeArchitectureActor(
		TEXT("/Game/Calysto/Dungeon/Blueprint/Utility/BP_Door.BP_Door_C"), 1.0f,
		MakeArchitectureTransform(false, FVector(-64.410089, 0.0, 0.0),
			FRotator(0.0, 103.693343, 0.0), FVector(1.119666, 1.0, 1.136592))));
	Result.StartBlueprint = SoftActorClass(
		TEXT("/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.BP_StartPoint_C"));
	Result.EndBlueprint = SoftActorClass(
		TEXT("/Game/Calysto/Dungeon/Blueprint/Utility/BP_EndPoint.BP_EndPoint_C"));
	Result.RampTop.Add(MakeArchitectureMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_RampBevel.SM_RampBevel"), 1.0f,
		MakeArchitectureTransform(false, FVector(0.0, 5.0, 0.0),
			FRotator::ZeroRotator, FVector(1.0, 1.5, 1.0))));
	Result.RampBottom.Add(MakeArchitectureMesh(
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_RampBevel.SM_RampBevel"), 1.0f,
		MakeArchitectureTransform(false, FVector::ZeroVector,
			FRotator::ZeroRotator, FVector(1.02, 2.0, 1.0))));

	FEFCalystoArchitectureLightV6& WallLight = Result.WallLights.AddDefaulted_GetRef();
	WallLight.ActorClass = SoftActorClass(
		TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch.BP_WallTorch_C"));
	WallLight.PlacementZone = EEFCalystoPlacementZoneV6::WallMiddle;
	WallLight.PositionJitterCm = 2.0f;

	Result.WallMiddleObjects.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::None,
		MakeArchitectureVariation(FVector(0.0, 10.0, 0.0), FVector(0.0, 15.0, 0.0),
			FRotator::ZeroRotator, FRotator::ZeroRotator,
			FVector::OneVector, FVector(2.0, 1.0, 1.0))));
	Result.WallTopObjects.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::Full360,
		MakeArchitectureVariation(FVector(0.0, 10.0, 0.0), FVector(0.0, 50.0, 0.0),
			FRotator(-60.0, 90.0, -60.0), FRotator(60.0, 90.0, 60.0),
			FVector(2.0, 1.0, 1.0), FVector(5.0, 1.0, 1.0))));
	Result.WallTopObjects.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::Full360,
		MakeArchitectureVariation(FVector(0.0, 20.0, 0.0), FVector(0.0, 50.0, 0.0),
			FRotator(-60.0, 90.0, -60.0), FRotator(60.0, 90.0, 60.0),
			FVector(2.0, 1.0, 1.0), FVector(5.0, 1.0, 1.0))));
	Result.WallTopObjects.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::Full360,
		MakeArchitectureVariation(FVector::ZeroVector, FVector::ZeroVector,
			FRotator(-60.0, 90.0, -60.0), FRotator(60.0, 90.0, 60.0),
			FVector(2.0, 1.0, 1.0), FVector(5.0, 1.0, 1.0))));
	return Result;
}

FEFCalystoRoomArchitectureV6 MakeForgeRoomArchitecture()
{
	FEFCalystoRoomArchitectureV6 Result;
	const FEFCalystoArchitectureVariationV6 WallBottom = MakeArchitectureVariation(
		FVector(0.0, 25.0, 0.0), FVector(0.0, 25.0, 0.0));
	Result.WallBottom.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::LevelInstance, 1,
		EEFCalystoArchitectureRotationV6::None, WallBottom));
	Result.WallBottom.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 3,
		EEFCalystoArchitectureRotationV6::None, WallBottom));

	const FEFCalystoArchitectureVariationV6 WallMiddle = MakeArchitectureVariation(
		FVector(-50.0, 25.0, -75.0), FVector(50.0, 25.0, 75.0),
		FRotator::ZeroRotator, FRotator::ZeroRotator,
		FVector::OneVector, FVector(1.2, 1.0, 1.0));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Result.WallMiddle.Add(MakeArchitectureObject(
			EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
			EEFCalystoArchitectureRotationV6::None, WallMiddle));
	}
	Result.WallMiddle.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::LevelInstance, 1,
		EEFCalystoArchitectureRotationV6::None, WallBottom));

	const FEFCalystoArchitectureVariationV6 WallTop = MakeArchitectureVariation(
		FVector(-100.0, 10.0, 0.0), FVector(100.0, 20.0, 50.0),
		FRotator(0.0, -30.0, 10.0), FRotator(0.0, 30.0, 45.0),
		FVector::OneVector, FVector(3.0, 1.0, 1.0));
	Result.WallTop.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::None, WallTop));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Result.WallTop.Add(MakeArchitectureObject(
			EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
			EEFCalystoArchitectureRotationV6::Full360, WallTop));
	}
	FEFCalystoArchitectureVariationV6 WideWallTop = WallTop;
	WideWallTop.ScaleMinimum = FVector(2.0, 1.0, 1.0);
	Result.WallTop.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::Full360, WideWallTop));

	Result.Floor.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::ActorBlueprint, 1,
		EEFCalystoArchitectureRotationV6::Full360, MakeArchitectureVariation()));
	FEFCalystoArchitectureObjectV6 Table = MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::LevelInstance, 1,
		EEFCalystoArchitectureRotationV6::Degrees45, MakeArchitectureVariation());
	Table.LevelInstance = SoftObject(
		TEXT("/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Table.PCGDA_Table"));
	Result.Floor.Add(MoveTemp(Table));

	FEFCalystoArchitectureVariationV6 Hanging = MakeArchitectureVariation(
		FVector(0.0, 0.0, 10.0), FVector(0.0, 0.0, 30.0),
		FRotator::ZeroRotator, FRotator(0.0, 360.0, 0.0));
	Result.Roof.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::ActorBlueprint, 1,
		EEFCalystoArchitectureRotationV6::Full360, Hanging));
	Result.Roof.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::None, MakeArchitectureVariation()));
	Result.Roof.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::None, MakeArchitectureVariation()));
	return Result;
}

FEFCalystoRoomArchitectureV6 MakeShrineRoomArchitecture()
{
	FEFCalystoRoomArchitectureV6 Result;
	const FEFCalystoArchitectureVariationV6 Raised = MakeArchitectureVariation(
		FVector(0.0, 0.0, 10.0), FVector(0.0, 0.0, 10.0));
	Result.WallBottom.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::LevelInstance, 1,
		EEFCalystoArchitectureRotationV6::None, Raised));
	FEFCalystoArchitectureVariationV6 ShrineFloor = Raised;
	ShrineFloor.ScaleMinimum = FVector(0.8, 1.0, 1.0);
	ShrineFloor.ScaleMaximum = FVector(1.5, 1.0, 1.0);
	Result.Floor.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::ActorBlueprint, 1,
		EEFCalystoArchitectureRotationV6::Full360, ShrineFloor));
	Result.Floor.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::StaticMesh, 1,
		EEFCalystoArchitectureRotationV6::None, MakeArchitectureVariation()));
	Result.CornerBottom.Add(MakeArchitectureObject(
		EEFCalystoArchitectureObjectTypeV6::LevelInstance, 1,
		EEFCalystoArchitectureRotationV6::Full360, Raised));
	return Result;
}

FEFCalystoPertRangeV6 MakeRange(const float Minimum, const float Mode,
	const float Maximum, const float Shape = 4.0f)
{
	FEFCalystoPertRangeV6 Result;
	Result.Minimum = Minimum;
	Result.Mode = Mode;
	Result.Maximum = Maximum;
	Result.Shape = Shape;
	return Result;
}

FEFCalystoChanceCurveV6 MakeChance(const float Floor1, const float Floor100,
	const float Tau = 12.0f)
{
	FEFCalystoChanceCurveV6 Result;
	Result.ChanceAtFloor1 = Floor1;
	Result.ChanceAtFloor100 = Floor100;
	Result.Tau = Tau;
	return Result;
}

FEFCalystoTierMixV6 MakeTierMix(const float Common, const float Uncommon,
	const float Rare, const float Epic)
{
	FEFCalystoTierMixV6 Result;
	Result.Common = Common;
	Result.Uncommon = Uncommon;
	Result.Rare = Rare;
	Result.Epic = Epic;
	Result.RefreshNothing();
	return Result;
}

FEFCalystoTierCurveV6 MakeTierCurve(const FEFCalystoTierMixV6& Floor1,
	const FEFCalystoTierMixV6& Floor100)
{
	FEFCalystoTierCurveV6 Result;
	Result.AtFloor1 = Floor1;
	Result.AtFloor100 = Floor100;
	return Result;
}

FEFCalystoCatalogEntryV6 MakeActorEntry(
	const TCHAR* StableId, const TCHAR* ClassPath, const float Weight,
	const TCHAR* Archetype, const EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common,
	const EEFCalystoGenderV6 Gender = EEFCalystoGenderV6::Any,
	const float ThreatCost = 1.0f, const int32 FirstFloor = 1,
	const int32 MaximumPerVariant = 4, const int32 CooldownFloors = 0,
	const EEFCalystoLifecycleV6 Lifecycle = EEFCalystoLifecycleV6::FloorLocal)
{
	FEFCalystoCatalogEntryV6 Result;
	Result.StableId = FName(StableId);
	Result.DisplayName = StableId;
	Result.ActorClass = SoftActorClass(ClassPath);
	Result.SelectionWeight = Weight;
	Result.PlacementZone = EEFCalystoPlacementZoneV6::Floor;
	Result.PositionJitterCm = 2.0f;
	Result.Tier = Tier;
	Result.Archetype = FName(Archetype);
	Result.Gender = Gender;
	Result.BaseThreatCost = ThreatCost;
	Result.FirstEligibleFloor = FirstFloor;
	Result.MaximumPerVariant = MaximumPerVariant;
	Result.CooldownFloors = CooldownFloors;
	Result.Lifecycle = Lifecycle;
	return Result;
}

FEFCalystoChestContentEntryV6 MakeChestContent(
	const TCHAR* StableId, const TCHAR* ClassPath, const float Weight,
	const EEFCalystoRarityTierV6 Tier, const int32 Cooldown = 0,
	const bool bRequiresGraveyard = false)
{
	FEFCalystoChestContentEntryV6 Result;
	Result.StableId = FName(StableId);
	Result.DisplayName = StableId;
	Result.ContentClass = SoftObjectClass(ClassPath);
	Result.SelectionWeight = Weight;
	Result.Tier = Tier;
	Result.CooldownFloors = Cooldown;
	Result.bRequiresGraveyardEligibility = bRequiresGraveyard;
	return Result;
}

FEFCalystoCatalogOverlayV6 MakeCategory(
	const TCHAR* CategoryId, const EEFCalystoCatalogOverlayModeV6 Mode,
	const float Chance, const int32 Maximum)
{
	FEFCalystoCatalogOverlayV6 Result;
	Result.CategoryId = FName(CategoryId);
	Result.Mode = Mode;
	Result.Presence = MakeChance(Chance, Chance);
	Result.Limits.MinimumWhenPresent = Chance > 0.0f ? 1 : 0;
	Result.Limits.MaximumPerFloor = Maximum;
	Result.Tiers = MakeTierCurve(MakeTierMix(0.60f, 0.20f, 0.08f, 0.02f),
		MakeTierMix(0.35f, 0.30f, 0.17f, 0.08f));
	return Result;
}

TArray<FEFCalystoCatalogEntryV6> MakeEnemyCatalog()
{
	return {
		MakeActorEntry(TEXT("Enemy.Melee.Female"), TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C"), 1.0f, TEXT("Melee"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Female, 2.0f, 1, 8),
		MakeActorEntry(TEXT("Enemy.Melee.Male"), TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C"), 1.0f, TEXT("Melee"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Male, 2.0f, 1, 8),
		MakeActorEntry(TEXT("Enemy.MM.Female"), TEXT("/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C"), 0.25f, TEXT("MM"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Female, 1.0f, 1, 12),
		MakeActorEntry(TEXT("Enemy.MM.Male"), TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C"), 0.25f, TEXT("MM"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Male, 1.0f, 1, 12),
		MakeActorEntry(TEXT("Enemy.Defender.Female"), TEXT("/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C"), 0.35f, TEXT("Defender"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Female, 3.0f, 1, 6),
		MakeActorEntry(TEXT("Enemy.Defender.Male"), TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C"), 0.35f, TEXT("Defender"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Male, 3.0f, 1, 6),
		MakeActorEntry(TEXT("Enemy.Ranged.Female"), TEXT("/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"), 0.25f, TEXT("Ranged"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Female, 3.0f, 2, 6),
		MakeActorEntry(TEXT("Enemy.Ranged.Male"), TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C"), 0.25f, TEXT("Ranged"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Male, 3.0f, 2, 6),
		MakeActorEntry(TEXT("Enemy.Gun.Female"), TEXT("/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C"), 0.10f, TEXT("Gun"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Female, 3.0f, 3, 5, 1),
		MakeActorEntry(TEXT("Enemy.Gun.Male"), TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C"), 0.10f, TEXT("Gun"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Male, 3.0f, 3, 5, 1),
		MakeActorEntry(TEXT("Enemy.Mage.Female"), TEXT("/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C"), 0.10f, TEXT("Mage"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Female, 4.0f, 3, 4, 2),
		MakeActorEntry(TEXT("Enemy.Mage.Male"), TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C"), 0.10f, TEXT("Mage"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Male, 4.0f, 3, 4, 2)
	};
}

TArray<FEFCalystoCatalogEntryV6> MakeCompanionCatalog()
{
	return {
		MakeActorEntry(TEXT("NPC.Companion.Generalist.Female"), TEXT("/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C"), 1.0f, TEXT("Generalist"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Female, 1.0f, 1, 1, 0, EEFCalystoLifecycleV6::Recruitable),
		MakeActorEntry(TEXT("NPC.Companion.Generalist.Male"), TEXT("/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C"), 1.0f, TEXT("Generalist"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Male, 1.0f, 1, 1, 0, EEFCalystoLifecycleV6::Recruitable),
		MakeActorEntry(TEXT("NPC.Companion.Melee.Female"), TEXT("/Game/_Game/Characters/Female/ACFMeleeCompanionBPFemale.ACFMeleeCompanionBPFemale_C"), 0.35f, TEXT("Melee"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Female, 1.0f, 3, 1, 0, EEFCalystoLifecycleV6::Recruitable),
		MakeActorEntry(TEXT("NPC.Companion.Melee.Male"), TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C"), 0.35f, TEXT("Melee"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Male, 1.0f, 3, 1, 0, EEFCalystoLifecycleV6::Recruitable),
		MakeActorEntry(TEXT("NPC.Companion.Ranged.Female"), TEXT("/Game/_Game/Characters/Female/ACFRangedCompanionBPFemale.ACFRangedCompanionBPFemale_C"), 0.15f, TEXT("Ranged"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Female, 1.0f, 5, 1, 0, EEFCalystoLifecycleV6::Recruitable),
		MakeActorEntry(TEXT("NPC.Companion.Ranged.Male"), TEXT("/Game/_Game/Characters/Male/ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C"), 0.15f, TEXT("Ranged"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Male, 1.0f, 5, 1, 0, EEFCalystoLifecycleV6::Recruitable)
	};
}

TArray<FEFCalystoCatalogEntryV6> MakeFoodCatalog()
{
	return {
		MakeActorEntry(TEXT("Food.Apple"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01.BP_Pickup_Food_Apple01_C"), 0.40f, TEXT("Food")),
		MakeActorEntry(TEXT("Food.Bread"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Bread01.BP_Pickup_Food_Bread01_C"), 0.30f, TEXT("Food")),
		MakeActorEntry(TEXT("Drink.Water"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/BP_Pickup_Drink_WaterBottle01.BP_Pickup_Drink_WaterBottle01_C"), 0.30f, TEXT("Drink")),
		MakeActorEntry(TEXT("Food.CookedMeat"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_CookedMeat01.BP_Pickup_Food_CookedMeat01_C"), 0.20f, TEXT("Food"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Any, 1.0f, 2)
	};
}

TArray<FEFCalystoCatalogEntryV6> MakeChestCatalog()
{
	return {
		MakeActorEntry(TEXT("Chest.Locked"), TEXT("/Game/_Game/Items/Chests/BP_CalystoLockedChest.BP_CalystoLockedChest_C"), 1.0f, TEXT("Chest"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Any, 1.0f, 1, 3),
		MakeActorEntry(TEXT("Chest.LockPick"), TEXT("/Game/_Game/Items/Chests/BP_CalystoLockPickChest.BP_CalystoLockPickChest_C"), 1.0f, TEXT("Chest"), EEFCalystoRarityTierV6::Common, EEFCalystoGenderV6::Any, 1.0f, 1, 3)
	};
}

TArray<FEFCalystoChestContentEntryV6> MakeChestContents()
{
	return {
		MakeChestContent(TEXT("ChestContent.HealthPotion"), TEXT("/Game/FullSample/Blueprints/Items/Consumable/ACFHealthPotionBP.ACFHealthPotionBP_C"), 1.0f, EEFCalystoRarityTierV6::Common),
		MakeChestContent(TEXT("ChestContent.ManaPotion"), TEXT("/Game/FullSample/Blueprints/Items/Consumable/ACFManaPotionBP.ACFManaPotionBP_C"), 0.75f, EEFCalystoRarityTierV6::Uncommon),
		MakeChestContent(TEXT("Item.CompanionRevival.WintersRecall"), TEXT("/Game/_Game/Items/Companions/BP_Item_WintersRecall.BP_Item_WintersRecall_C"), 0.10f, EEFCalystoRarityTierV6::Epic, 8, true)
	};
}

TArray<FEFCalystoCatalogOverlayV6> MakeBaseCatalogs()
{
	TArray<FEFCalystoCatalogOverlayV6> Result;
	FEFCalystoCatalogOverlayV6& Enemies = Result.Add_GetRef(MakeCategory(TEXT("Enemy"), EEFCalystoCatalogOverlayModeV6::Replace, 0.70f, 25));
	Enemies.Catalog = MakeEnemyCatalog();
	FEFCalystoCatalogOverlayV6& NPCs = Result.Add_GetRef(MakeCategory(TEXT("NPC"), EEFCalystoCatalogOverlayModeV6::Replace, 0.15f, 4));
	NPCs.Catalog = MakeCompanionCatalog();
	FEFCalystoCatalogOverlayV6& Food = Result.Add_GetRef(MakeCategory(TEXT("Food"), EEFCalystoCatalogOverlayModeV6::Replace, 0.50f, 8));
	Food.Catalog = MakeFoodCatalog();
	FEFCalystoCatalogOverlayV6& Chests = Result.Add_GetRef(MakeCategory(TEXT("Chest"), EEFCalystoCatalogOverlayModeV6::Replace, 0.35f, 3));
	Chests.Catalog = MakeChestCatalog();
	FEFCalystoCatalogOverlayV6& ChestContents = Result.Add_GetRef(MakeCategory(TEXT("ChestContents"), EEFCalystoCatalogOverlayModeV6::Replace, 1.0f, 4));
	ChestContents.ChestContentsCatalog = MakeChestContents();
	FEFCalystoCatalogOverlayV6& Loot = Result.Add_GetRef(MakeCategory(TEXT("LooseLoot"), EEFCalystoCatalogOverlayModeV6::Replace, 0.15f, 4));
	Loot.Catalog.Add(MakeActorEntry(TEXT("Drink.RareAlcohol07"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/BP_Pickup_Drink_AlcoholBottle07.BP_Pickup_Drink_AlcoholBottle07_C"), 1.0f, TEXT("RareDrink"), EEFCalystoRarityTierV6::Rare, EEFCalystoGenderV6::Any, 1.0f, 4, 1, 4));
	FEFCalystoCatalogOverlayV6& Clothing = Result.Add_GetRef(MakeCategory(TEXT("Clothing"), EEFCalystoCatalogOverlayModeV6::Replace, 0.10f, 4));
	Clothing.Catalog.Add(MakeActorEntry(TEXT("Clothing.Armor.ACF.Default"), TEXT("/Game/_Game/Items/Clothing/BP_CalystoArmorPickup.BP_CalystoArmorPickup_C"), 1.0f, TEXT("Armor"), EEFCalystoRarityTierV6::Uncommon, EEFCalystoGenderV6::Any, 1.0f, 1, 4));
	Result.Add(MakeCategory(TEXT("SpecialEvent"), EEFCalystoCatalogOverlayModeV6::Block, 0.0f, 0));
	return Result;
}

FEFCalystoDecalVariantV6 MakeBloodDecalVariant(const TCHAR* StableId,
	const TCHAR* MaterialPath, const EEFCalystoDecalSurfaceV6 Surface)
{
	FEFCalystoDecalVariantV6 Result;
	Result.StableId = FName(StableId);
	Result.Material = SoftMaterial(MaterialPath);
	Result.SourceColorTexture = SoftTexture(TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.T_Splat_04"));
	Result.SourceNormalTexture = SoftTexture(TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.T_Splat_N_04"));
	Result.AllowedSurfaces = static_cast<int32>(Surface);
	return Result;
}

FEFCalystoDecalProfileV6 MakeStyleDecals()
{
	FEFCalystoDecalProfileV6 Result;
	Result.Mode = EEFCalystoDecalResolutionModeV6::Replace;
	Result.ChancePerEligibleRoom = 0.10f;
	Result.Catalog.Add(MakeBloodDecalVariant(TEXT("Blood.Splat.04.Floor"),
		TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor.MI_CalystoBloodDecal_Floor"),
		EEFCalystoDecalSurfaceV6::Floor));
	Result.Catalog.Add(MakeBloodDecalVariant(TEXT("Blood.Splat.04.Wall"),
		TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall.MI_CalystoBloodDecal_Wall"),
		EEFCalystoDecalSurfaceV6::Wall));
	Result.Catalog.Add(MakeBloodDecalVariant(TEXT("Blood.Splat.04.Ceiling"),
		TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling.MI_CalystoBloodDecal_Ceiling"),
		EEFCalystoDecalSurfaceV6::Roof));
	return Result;
}

FEFCalystoStyleProfileV6 MakeStyle(const FName StyleId, const float Weight)
{
	FEFCalystoStyleProfileV6 Result;
	Result.StyleId = StyleId;
	Result.DisplayName = StyleId.ToString();
	Result.SelectionWeight = Weight;
	Result.RoomThemeChance = 0.25f;
	Result.DungeonMaterials = MakeMaterialSet(TEXT("/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles"));
	Result.Architecture = MakeDefaultStyleArchitecture();
	Result.Catalogs = MakeBaseCatalogs();
	Result.Decals = MakeStyleDecals();
	Result.Layout.DungeonSize = MakeRange(18.0f, 26.0f, 30.0f);
	Result.Layout.CandidateDensity = MakeRange(0.20f, 0.30f, 0.50f);
	Result.Layout.SidePathChance = MakeRange(0.30f, 0.50f, 0.70f);
	if (StyleId == FName(TEXT("Compact")))
	{
		Result.Layout.DungeonSize = MakeRange(18.0f, 21.0f, 24.0f);
		Result.Layout.CandidateDensity = MakeRange(0.30f, 0.40f, 0.50f);
		Result.Layout.SidePathChance = MakeRange(0.30f, 0.38f, 0.50f);
		Result.Threat.BudgetAtFloor1 = 10.0f;
	}
	else if (StyleId == FName(TEXT("Branching")))
	{
		Result.Layout.DungeonSize = MakeRange(24.0f, 28.0f, 30.0f);
		Result.Layout.SidePathChance = MakeRange(0.50f, 0.62f, 0.70f);
		Result.Threat.BudgetAtFloor1 = 14.0f;
		Result.Traits.Mystery = 0.30f;
	}
	return Result;
}

FEFCalystoRoomThemeProfileV6 MakeForgeTheme()
{
	FEFCalystoRoomThemeProfileV6 Result;
	Result.ThemeId = TEXT("Forge");
	Result.DisplayName = TEXT("Forge");
	Result.Description = TEXT("Combat, armor, locked chests, cooked meat, and strong supplies.");
	Result.PreviewColor = FLinearColor(1.0f, 0.32f, 0.02f, 1.0f);
	Result.SelectionWeight = 5.0f;
	Result.Architecture = MakeForgeRoomArchitecture();
	Result.RoomMaterials.Overrides = MakeMaterialSet(
		TEXT("/EFProcedural/Calysto/Internal/Materials/Architecture/MI_Template_BaseOrange.MI_Template_BaseOrange"));
	Result.Traits.Danger = 0.85f;
	Result.Traits.Abundance = 0.45f;

	FEFCalystoCatalogOverlayV6 Enemy = MakeCategory(TEXT("Enemy"), EEFCalystoCatalogOverlayModeV6::Replace, 0.85f, 25);
	const TArray<FEFCalystoCatalogEntryV6> AllEnemies = MakeEnemyCatalog();
	for (const FEFCalystoCatalogEntryV6& Entry : AllEnemies)
	{
		if (Entry.Archetype == FName(TEXT("Melee")) || Entry.Archetype == FName(TEXT("Defender")))
		{
			Enemy.Catalog.Add(Entry);
		}
	}
	Result.Catalogs.Add(MoveTemp(Enemy));
	Result.Catalogs.Add(MakeCategory(TEXT("NPC"), EEFCalystoCatalogOverlayModeV6::Block, 0.0f, 0));
	FEFCalystoCatalogOverlayV6 Food = MakeCategory(TEXT("Food"), EEFCalystoCatalogOverlayModeV6::Replace, 0.60f, 8);
	Food.Catalog.Add(MakeFoodCatalog()[3]);
	Result.Catalogs.Add(MoveTemp(Food));
	FEFCalystoCatalogOverlayV6 Chests = MakeCategory(TEXT("Chest"), EEFCalystoCatalogOverlayModeV6::Replace, 0.55f, 3);
	Chests.Catalog = MakeChestCatalog();
	Result.Catalogs.Add(MoveTemp(Chests));
	FEFCalystoCatalogOverlayV6 Loot = MakeCategory(TEXT("LooseLoot"), EEFCalystoCatalogOverlayModeV6::Replace, 0.30f, 4);
	Loot.Catalog.Add(MakeBaseCatalogs()[5].Catalog[0]);
	Result.Catalogs.Add(MoveTemp(Loot));
	FEFCalystoCatalogOverlayV6 Clothing = MakeCategory(TEXT("Clothing"), EEFCalystoCatalogOverlayModeV6::Replace, 0.30f, 4);
	Clothing.Catalog.Add(MakeBaseCatalogs()[6].Catalog[0]);
	Result.Catalogs.Add(MoveTemp(Clothing));

	Result.Decals = MakeStyleDecals();
	Result.Decals.Mode = EEFCalystoDecalResolutionModeV6::Replace;
	Result.Decals.ChancePerEligibleRoom = 0.25f;
	return Result;
}

FEFCalystoRoomThemeProfileV6 MakeShrineTheme()
{
	FEFCalystoRoomThemeProfileV6 Result;
	Result.ThemeId = TEXT("Shrine");
	Result.DisplayName = TEXT("Shrine");
	Result.Description = TEXT("Mage threats, support, companions, potions, water, and Winter's Recall.");
	Result.PreviewColor = FLinearColor(0.03f, 0.32f, 1.0f, 1.0f);
	Result.SelectionWeight = 3.0f;
	Result.Architecture = MakeShrineRoomArchitecture();
	Result.RoomMaterials.Overrides = MakeMaterialSet(
		TEXT("/Game/FullSample/DemoRoom/Materials/MI_Display_Blue.MI_Display_Blue"));
	Result.Traits.Safe = 0.70f;
	Result.Traits.Mystery = 0.65f;

	FEFCalystoCatalogOverlayV6 Enemy = MakeCategory(TEXT("Enemy"), EEFCalystoCatalogOverlayModeV6::Replace, 0.35f, 25);
	for (const FEFCalystoCatalogEntryV6& Entry : MakeEnemyCatalog())
	{
		if (Entry.Archetype == FName(TEXT("Mage")))
		{
			Enemy.Catalog.Add(Entry);
		}
	}
	Result.Catalogs.Add(MoveTemp(Enemy));
	FEFCalystoCatalogOverlayV6 NPCs = MakeCategory(TEXT("NPC"), EEFCalystoCatalogOverlayModeV6::Replace, 0.45f, 4);
	NPCs.Catalog = MakeCompanionCatalog();
	Result.Catalogs.Add(MoveTemp(NPCs));
	FEFCalystoCatalogOverlayV6 Food = MakeCategory(TEXT("Food"), EEFCalystoCatalogOverlayModeV6::Replace, 0.50f, 8);
	Food.Catalog.Add(MakeFoodCatalog()[2]);
	Result.Catalogs.Add(MoveTemp(Food));
	FEFCalystoCatalogOverlayV6 ChestContents = MakeCategory(TEXT("ChestContents"), EEFCalystoCatalogOverlayModeV6::Replace, 1.0f, 4);
	ChestContents.ChestContentsCatalog = MakeChestContents();
	Result.Catalogs.Add(MoveTemp(ChestContents));
	Result.Decals.Mode = EEFCalystoDecalResolutionModeV6::Block;
	Result.Decals.ChancePerEligibleRoom = 0.0f;
	Result.Decals.AllowedSurfaces = 0;
	Result.Decals.MaximumPerRoom = 0;
	Result.Decals.MaximumActivePerFloor = 0;
	Result.Decals.FloorLimit = 0;
	Result.Decals.WallLimit = 0;
	Result.Decals.RoofLimit = 0;
	Result.Decals.Catalog.Reset();
	return Result;
}

bool ValidateTierMix(const FEFCalystoTierMixV6& Mix, const FString& Label,
	FString& OutError)
{
	if (!IsFiniteUnit(Mix.Common) || !IsFiniteUnit(Mix.Uncommon) ||
		!IsFiniteUnit(Mix.Rare) || !IsFiniteUnit(Mix.Epic))
	{
		return Fail(OutError, Label + TEXT(" contains a non-finite or out-of-range tier probability."));
	}
	const float SelectableMass = Mix.Common + Mix.Uncommon + Mix.Rare + Mix.Epic;
	if (SelectableMass > 0.90f + ProbabilityTolerance ||
		!FMath::IsNearlyEqual(Mix.Nothing, 1.0f - SelectableMass, ProbabilityTolerance))
	{
		return Fail(OutError, Label + TEXT(" must reserve at least 10% Nothing and refresh its calculated value."));
	}
	return true;
}

bool ValidateChance(const FEFCalystoChanceCurveV6& Chance,
	const FString& Label, FString& OutError)
{
	if (!IsFiniteUnit(Chance.ChanceAtFloor1) ||
		!IsFiniteUnit(Chance.ChanceAtFloor100) ||
		!FMath::IsFinite(Chance.Tau) || Chance.Tau <= 0.0f)
	{
		return Fail(OutError, Label + TEXT(" has an invalid chance curve."));
	}
	return true;
}

bool ValidateRange(const FEFCalystoPertRangeV6& Range, const float HardMinimum,
	const float HardMaximum, const FString& Label, FString& OutError)
{
	if (!FMath::IsFinite(Range.Minimum) || !FMath::IsFinite(Range.Mode) ||
		!FMath::IsFinite(Range.Maximum) || !FMath::IsFinite(Range.Shape) ||
		Range.Minimum < HardMinimum || Range.Maximum > HardMaximum ||
		Range.Minimum > Range.Mode || Range.Mode > Range.Maximum || Range.Shape <= 0.0f)
	{
		return Fail(OutError, Label + TEXT(" must be a finite ordered PERT range inside its Calysto hard bounds."));
	}
	return true;
}

bool ValidateMaterials(const FEFCalystoSurfaceMaterialSetV6& Materials,
	const FString& Label, FString& OutError)
{
	if (Materials.FloorMaterial.IsNull() || Materials.WallMaterial.IsNull() ||
		Materials.RoofMaterial.IsNull())
	{
		return Fail(OutError, Label + TEXT(" requires non-null Floor, Wall, and Roof materials."));
	}
	return true;
}

bool ValidateArchitectureTransform(const FEFCalystoArchitectureTransformV6& Transform,
	const FString& Label, FString& OutError)
{
	if (!IsFiniteVector(Transform.LocationOffset) ||
		!IsFiniteRotator(Transform.RotationOffset) || !IsFiniteVector(Transform.Scale) ||
		Transform.Scale.X <= 0.0 || Transform.Scale.Y <= 0.0 || Transform.Scale.Z <= 0.0)
	{
		return Fail(OutError, Label + TEXT(" contains a non-finite transform or non-positive scale."));
	}
	return true;
}

bool ValidateArchitectureVariation(const FEFCalystoArchitectureVariationV6& Variation,
	const FString& Label, FString& OutError)
{
	if (!IsFiniteVector(Variation.LocationMinimum) ||
		!IsFiniteVector(Variation.LocationMaximum) ||
		!IsOrderedVectorRange(Variation.LocationMinimum, Variation.LocationMaximum) ||
		!IsFiniteRotator(Variation.RotationMinimum) ||
		!IsFiniteRotator(Variation.RotationMaximum) ||
		Variation.RotationMinimum.Pitch > Variation.RotationMaximum.Pitch ||
		Variation.RotationMinimum.Yaw > Variation.RotationMaximum.Yaw ||
		Variation.RotationMinimum.Roll > Variation.RotationMaximum.Roll ||
		!IsFiniteVector(Variation.ScaleMinimum) ||
		!IsFiniteVector(Variation.ScaleMaximum) ||
		!IsOrderedVectorRange(Variation.ScaleMinimum, Variation.ScaleMaximum) ||
		Variation.ScaleMinimum.X <= 0.0 || Variation.ScaleMinimum.Y <= 0.0 ||
		Variation.ScaleMinimum.Z <= 0.0)
	{
		return Fail(OutError, Label + TEXT(" contains invalid or unordered Architecture variation bounds."));
	}
	return true;
}

bool ValidateArchitectureObjects(const TArray<FEFCalystoArchitectureObjectV6>& Objects,
	const FString& Label, FString& OutError)
{
	for (int32 Index = 0; Index < Objects.Num(); ++Index)
	{
		const FEFCalystoArchitectureObjectV6& Object = Objects[Index];
		if (Object.SelectionWeight <= 0 ||
			static_cast<uint8>(Object.Type) >
				static_cast<uint8>(EEFCalystoArchitectureObjectTypeV6::LevelInstance) ||
			static_cast<uint8>(Object.Rotation) >
				static_cast<uint8>(EEFCalystoArchitectureRotationV6::Full360) ||
			!ValidateArchitectureVariation(Object.Variation,
				FString::Printf(TEXT("%s[%d]"), *Label, Index), OutError))
		{
			return OutError.IsEmpty()
				? Fail(OutError, Label + TEXT(" contains an invalid object entry."))
				: false;
		}
	}
	return true;
}

bool ValidateStyleArchitecture(const FEFCalystoStyleArchitectureV6& Architecture,
	const FString& Label, FString& OutError)
{
	auto ValidateMeshes = [&OutError, &Label](
		const TArray<FEFCalystoArchitectureMeshV6>& Entries,
		const TCHAR* Zone, const bool bRequired)
	{
		if (bRequired && Entries.IsEmpty())
		{
			return Fail(OutError, Label + TEXT(" requires at least one ") + Zone + TEXT(" entry."));
		}
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FEFCalystoArchitectureMeshV6& Entry = Entries[Index];
			if (Entry.Mesh.IsNull() || !IsFiniteNonNegative(Entry.SelectionWeight) ||
				Entry.SelectionWeight <= 0.0f ||
				!ValidateArchitectureTransform(Entry.Transform,
					FString::Printf(TEXT("%s %s[%d]"), *Label, Zone, Index), OutError))
			{
				return OutError.IsEmpty()
					? Fail(OutError, Label + TEXT(" contains an invalid ") + Zone + TEXT(" mesh entry."))
					: false;
			}
		}
		return true;
	};
	if (!ValidateMeshes(Architecture.Floor, TEXT("Floor"), true) ||
		!ValidateMeshes(Architecture.Wall, TEXT("Wall"), true) ||
		!ValidateMeshes(Architecture.Roof, TEXT("Roof"), true) ||
		!ValidateMeshes(Architecture.DoorFrame, TEXT("Door Frame"), true) ||
		!ValidateMeshes(Architecture.RampTop, TEXT("Ramp Top"), true) ||
		!ValidateMeshes(Architecture.RampBottom, TEXT("Ramp Bottom"), true))
	{
		return false;
	}
	if (Architecture.WallDoor.IsEmpty() || Architecture.Door.IsEmpty() ||
		Architecture.StartBlueprint.IsNull() || Architecture.EndBlueprint.IsNull())
	{
		return Fail(OutError, Label + TEXT(" requires Wall Door, Door, Start Blueprint, and End Blueprint entries."));
	}
	for (int32 Index = 0; Index < Architecture.WallDoor.Num(); ++Index)
	{
		const FEFCalystoArchitectureDoorwayV6& Entry = Architecture.WallDoor[Index];
		if (Entry.WallMesh.IsNull() || Entry.FrameMesh.IsNull() || Entry.DoorClass.IsNull() ||
			!IsFiniteNonNegative(Entry.SelectionWeight) || Entry.SelectionWeight <= 0.0f ||
			!ValidateArchitectureTransform(Entry.WallTransform, Label + TEXT(" Wall Door Wall"), OutError) ||
			!ValidateArchitectureTransform(Entry.FrameTransform, Label + TEXT(" Wall Door Frame"), OutError) ||
			!ValidateArchitectureTransform(Entry.DoorTransform, Label + TEXT(" Wall Door Door"), OutError))
		{
			return OutError.IsEmpty()
				? Fail(OutError, Label + TEXT(" contains an invalid Wall Door entry."))
				: false;
		}
	}
	for (int32 Index = 0; Index < Architecture.Door.Num(); ++Index)
	{
		const FEFCalystoArchitectureActorV6& Entry = Architecture.Door[Index];
		if (Entry.ActorClass.IsNull() || !IsFiniteNonNegative(Entry.SelectionWeight) ||
			Entry.SelectionWeight <= 0.0f ||
			!ValidateArchitectureTransform(Entry.Transform,
				FString::Printf(TEXT("%s Door[%d]"), *Label, Index), OutError))
		{
			return OutError.IsEmpty()
				? Fail(OutError, Label + TEXT(" contains an invalid Door entry."))
				: false;
		}
	}
	for (int32 Index = 0; Index < Architecture.WallLights.Num(); ++Index)
	{
		const FEFCalystoArchitectureLightV6& Entry = Architecture.WallLights[Index];
		const bool bWallZone = Entry.PlacementZone == EEFCalystoPlacementZoneV6::WallBottom ||
			Entry.PlacementZone == EEFCalystoPlacementZoneV6::WallMiddle ||
			Entry.PlacementZone == EEFCalystoPlacementZoneV6::WallTop;
		if (Entry.ActorClass.IsNull() || Entry.SelectionWeight <= 0 || !bWallZone ||
			!FMath::IsFinite(Entry.PositionJitterCm) || Entry.PositionJitterCm < 0.0f ||
			Entry.PositionJitterCm > 25.0f ||
			!ValidateArchitectureVariation(Entry.Variation,
				FString::Printf(TEXT("%s Wall Lights[%d]"), *Label, Index), OutError))
		{
			return OutError.IsEmpty()
				? Fail(OutError, Label + TEXT(" contains an invalid Wall Light entry."))
				: false;
		}
	}
	return ValidateArchitectureObjects(Architecture.WallBottomObjects,
		Label + TEXT(" Wall Bottom Objects"), OutError) &&
		ValidateArchitectureObjects(Architecture.WallMiddleObjects,
			Label + TEXT(" Wall Middle Objects"), OutError) &&
		ValidateArchitectureObjects(Architecture.WallTopObjects,
			Label + TEXT(" Wall Top Objects"), OutError) &&
		ValidateArchitectureObjects(Architecture.RoofObjects,
			Label + TEXT(" Roof Objects"), OutError);
}

bool ValidateRoomArchitecture(const FEFCalystoRoomArchitectureV6& Architecture,
	const FString& Label, FString& OutError)
{
	return ValidateArchitectureObjects(Architecture.WallBottom, Label + TEXT(" Wall Bottom"), OutError) &&
		ValidateArchitectureObjects(Architecture.WallMiddle, Label + TEXT(" Wall Middle"), OutError) &&
		ValidateArchitectureObjects(Architecture.WallTop, Label + TEXT(" Wall Top"), OutError) &&
		ValidateArchitectureObjects(Architecture.Floor, Label + TEXT(" Floor"), OutError) &&
		ValidateArchitectureObjects(Architecture.CornerBottom, Label + TEXT(" Corner Bottom"), OutError) &&
		ValidateArchitectureObjects(Architecture.CornerMiddle, Label + TEXT(" Corner Middle"), OutError) &&
		ValidateArchitectureObjects(Architecture.CornerTop, Label + TEXT(" Corner Top"), OutError) &&
		ValidateArchitectureObjects(Architecture.Roof, Label + TEXT(" Roof"), OutError);
}

bool ValidateCatalogs(const TArray<FEFCalystoCatalogOverlayV6>& Catalogs,
	const bool bStyleCatalogs, const FString& Label, FString& OutError)
{
	TSet<FString> CategoryIds;
	for (const FEFCalystoCatalogOverlayV6& Category : Catalogs)
	{
		const FString CategoryToken = CanonicalToken(Category.CategoryId);
		if (CategoryToken.IsEmpty() || CategoryIds.Contains(CategoryToken))
		{
			return Fail(OutError, Label + TEXT(" contains a missing or duplicate Category ID."));
		}
		CategoryIds.Add(CategoryToken);
		if (bStyleCatalogs &&
			(Category.Mode == EEFCalystoCatalogOverlayModeV6::Inherit ||
			 Category.Mode == EEFCalystoCatalogOverlayModeV6::Extend))
		{
			return Fail(OutError, Label + TEXT(" Style categories must use Replace or Block because no global catalog exists."));
		}
		if (Category.Mode == EEFCalystoCatalogOverlayModeV6::Inherit &&
			(!Category.Catalog.IsEmpty() || !Category.ChestContentsCatalog.IsEmpty()))
		{
			return Fail(OutError, Label + TEXT(" Inherit categories must not carry ignored catalog entries."));
		}
		if (!ValidateChance(Category.Presence, Label + TEXT("/") + Category.CategoryId.ToString(), OutError) ||
			!ValidateTierMix(Category.Tiers.AtFloor1, Label + TEXT(" tier Floor 1"), OutError) ||
			!ValidateTierMix(Category.Tiers.AtFloor100, Label + TEXT(" tier Floor 100"), OutError))
		{
			return false;
		}
		if (Category.Limits.MinimumWhenPresent < 0 ||
			Category.Limits.MaximumPerFloor < Category.Limits.MinimumWhenPresent ||
			Category.Limits.MaximumPerFloor > 36)
		{
			return Fail(OutError, Label + TEXT(" contains invalid category limits."));
		}
		if (Category.Mode == EEFCalystoCatalogOverlayModeV6::Block)
		{
			if (!Category.Catalog.IsEmpty() || !Category.ChestContentsCatalog.IsEmpty() ||
				Category.Presence.ChanceAtFloor1 != 0.0f || Category.Presence.ChanceAtFloor100 != 0.0f ||
				Category.Limits.MaximumPerFloor != 0)
			{
				return Fail(OutError, Label + TEXT(" Block categories must contain no entries, zero chance, and a zero cap."));
			}
			continue;
		}

		TSet<FString> EntryIds;
		for (const FEFCalystoCatalogEntryV6& Entry : Category.Catalog)
		{
			const FString EntryId = CanonicalToken(Entry.StableId);
			if (EntryId.IsEmpty() || EntryIds.Contains(EntryId))
			{
				return Fail(OutError, Label + TEXT(" contains a missing or duplicate actor Stable ID."));
			}
			EntryIds.Add(EntryId);
			if (!IsFiniteNonNegative(Entry.SelectionWeight) ||
				!FMath::IsFinite(Entry.PositionJitterCm) || Entry.PositionJitterCm < 0.0f ||
				Entry.PositionJitterCm > 25.0f ||
				static_cast<uint8>(Entry.PlacementZone) >
					static_cast<uint8>(EEFCalystoPlacementZoneV6::Roof) ||
				Entry.FirstEligibleFloor < 1 || Entry.MaximumPerVariant < 1 ||
				Entry.CooldownFloors < 0 || !IsFiniteNonNegative(Entry.BaseThreatCost))
			{
				return Fail(OutError, Label + TEXT(" contains invalid actor catalog numeric fields."));
			}
			if (Entry.Rule == EEFCalystoCatalogEntryRuleV6::Allow &&
				(Entry.ActorClass.IsNull() || Entry.SelectionWeight <= 0.0f))
			{
				return Fail(OutError, Label + TEXT(" allowed actor entries require a class and positive weight."));
			}
		}

		TSet<FString> ChestIds;
		for (const FEFCalystoChestContentEntryV6& Entry : Category.ChestContentsCatalog)
		{
			const FString EntryId = CanonicalToken(Entry.StableId);
			if (EntryId.IsEmpty() || ChestIds.Contains(EntryId) || Entry.ContentClass.IsNull() ||
				!IsFiniteNonNegative(Entry.SelectionWeight) || Entry.SelectionWeight <= 0.0f ||
				Entry.FirstEligibleFloor < 1 || Entry.CooldownFloors < 0 || Entry.MaximumPerFloor < 1)
			{
				return Fail(OutError, Label + TEXT(" contains an invalid or duplicate chest-content entry."));
			}
			ChestIds.Add(EntryId);
		}
	}
	return true;
}

bool ValidateDecals(const FEFCalystoDecalProfileV6& Decals,
	const bool bStyleProfile, const int32 PoolCapacity, const FString& Label,
	FString& OutError)
{
	if (bStyleProfile && Decals.Mode == EEFCalystoDecalResolutionModeV6::InheritStyle)
	{
		return Fail(OutError, Label + TEXT(" Style decals cannot inherit from another profile."));
	}
	if (!IsFiniteUnit(Decals.ChancePerEligibleRoom) ||
		Decals.MaximumPerRoom < 0 || Decals.MaximumActivePerFloor < 0 ||
		Decals.MaximumActivePerFloor > PoolCapacity || Decals.FloorLimit < 0 ||
		Decals.WallLimit < 0 || Decals.RoofLimit < 0)
	{
		return Fail(OutError, Label + TEXT(" has invalid chance, cap, or pool fields."));
	}
	if (Decals.Mode == EEFCalystoDecalResolutionModeV6::Block)
	{
		if (Decals.ChancePerEligibleRoom != 0.0f || Decals.AllowedSurfaces != 0 ||
			Decals.MaximumPerRoom != 0 || Decals.MaximumActivePerFloor != 0 ||
			!Decals.Catalog.IsEmpty())
		{
			return Fail(OutError, Label + TEXT(" Block decals must have no chance, surfaces, capacity, or catalog."));
		}
		return true;
	}
	if (Decals.Mode == EEFCalystoDecalResolutionModeV6::InheritStyle)
	{
		return Decals.Catalog.IsEmpty()
			? true
			: Fail(OutError, Label + TEXT(" inherited decals must not carry a second catalog."));
	}
	const int32 SurfaceLimitTotal = Decals.FloorLimit + Decals.WallLimit + Decals.RoofLimit;
	if (Decals.MaximumPerRoom < 1 || Decals.MaximumActivePerFloor < 1 ||
		SurfaceLimitTotal < Decals.MaximumActivePerFloor || Decals.AllowedSurfaces == 0 ||
		!FMath::IsFinite(Decals.MinimumSizeCm) || !FMath::IsFinite(Decals.MaximumSizeCm) ||
		Decals.MinimumSizeCm <= 0.0f || Decals.MaximumSizeCm < Decals.MinimumSizeCm ||
		!FMath::IsFinite(Decals.FadeStartDistanceCm) || !FMath::IsFinite(Decals.CullDistanceCm) ||
		Decals.FadeStartDistanceCm <= 0.0f || Decals.CullDistanceCm <= Decals.FadeStartDistanceCm ||
		Decals.Catalog.IsEmpty())
	{
		return Fail(OutError, Label + TEXT(" requires a bounded non-empty shared decal catalog and valid fade/cull distances."));
	}
	TSet<FString> VariantIds;
	for (const FEFCalystoDecalVariantV6& Variant : Decals.Catalog)
	{
		const FString Id = CanonicalToken(Variant.StableId);
		if (Id.IsEmpty() || VariantIds.Contains(Id) || Variant.Material.IsNull() ||
			Variant.AllowedSurfaces == 0 ||
			(Variant.AllowedSurfaces & ~Decals.AllowedSurfaces) != 0 ||
			!IsFiniteNonNegative(Variant.SelectionWeight) || Variant.SelectionWeight <= 0.0f)
		{
			return Fail(OutError, Label + TEXT(" contains an invalid or duplicate decal variant."));
		}
		VariantIds.Add(Id);
	}
	return true;
}

void AppendTierMix(FString& Out, const FEFCalystoTierMixV6& Mix)
{
	Out += FString::Printf(TEXT("TM:%s,%s,%s,%s,%s|"), *FloatBits(Mix.Common),
		*FloatBits(Mix.Uncommon), *FloatBits(Mix.Rare), *FloatBits(Mix.Epic),
		*FloatBits(Mix.Nothing));
}

void AppendMaterials(FString& Out, const FEFCalystoSurfaceMaterialSetV6& Materials)
{
	Out += FString::Printf(TEXT("MAT:%s,%s,%s|"), *PathToken(Materials.FloorMaterial),
		*PathToken(Materials.WallMaterial), *PathToken(Materials.RoofMaterial));
}

void AppendVector(FString& Out, const FVector& Value)
{
	Out += FString::Printf(TEXT("%s,%s,%s"), *DoubleBits(Value.X),
		*DoubleBits(Value.Y), *DoubleBits(Value.Z));
}

void AppendRotator(FString& Out, const FRotator& Value)
{
	Out += FString::Printf(TEXT("%s,%s,%s"), *DoubleBits(Value.Pitch),
		*DoubleBits(Value.Yaw), *DoubleBits(Value.Roll));
}

void AppendArchitectureTransform(FString& Out,
	const FEFCalystoArchitectureTransformV6& Transform)
{
	Out += Transform.bUniformScale ? TEXT("U1|") : TEXT("U0|");
	Out += TEXT("L:"); AppendVector(Out, Transform.LocationOffset);
	Out += TEXT("|R:"); AppendRotator(Out, Transform.RotationOffset);
	Out += TEXT("|S:"); AppendVector(Out, Transform.Scale);
	Out += TEXT("|");
}

void AppendArchitectureVariation(FString& Out,
	const FEFCalystoArchitectureVariationV6& Variation)
{
	Out += Variation.bUniformScale ? TEXT("U1|") : TEXT("U0|");
	Out += TEXT("LMIN:"); AppendVector(Out, Variation.LocationMinimum);
	Out += TEXT("|LMAX:"); AppendVector(Out, Variation.LocationMaximum);
	Out += TEXT("|RMIN:"); AppendRotator(Out, Variation.RotationMinimum);
	Out += TEXT("|RMAX:"); AppendRotator(Out, Variation.RotationMaximum);
	Out += TEXT("|SMIN:"); AppendVector(Out, Variation.ScaleMinimum);
	Out += TEXT("|SMAX:"); AppendVector(Out, Variation.ScaleMaximum);
	Out += TEXT("|");
}

void AppendArchitectureMeshes(FString& Out, const TCHAR* Zone,
	const TArray<FEFCalystoArchitectureMeshV6>& Entries)
{
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FEFCalystoArchitectureMeshV6& Entry = Entries[Index];
		Out += FString::Printf(TEXT("AM:%s,%d,%s,%s|"), Zone, Index,
			*PathToken(Entry.Mesh), *FloatBits(Entry.SelectionWeight));
		AppendArchitectureTransform(Out, Entry.Transform);
	}
}

void AppendArchitectureActors(FString& Out, const TCHAR* Zone,
	const TArray<FEFCalystoArchitectureActorV6>& Entries)
{
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FEFCalystoArchitectureActorV6& Entry = Entries[Index];
		Out += FString::Printf(TEXT("AA:%s,%d,%s,%s|"), Zone, Index,
			*PathToken(Entry.ActorClass), *FloatBits(Entry.SelectionWeight));
		AppendArchitectureTransform(Out, Entry.Transform);
	}
}

void AppendArchitectureObjects(FString& Out, const TCHAR* Zone,
	const TArray<FEFCalystoArchitectureObjectV6>& Entries)
{
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FEFCalystoArchitectureObjectV6& Entry = Entries[Index];
		Out += FString::Printf(TEXT("AO:%s,%d,%d,%s,%s,%s,%d,%d|"), Zone, Index,
			static_cast<int32>(Entry.Type), *PathToken(Entry.Mesh),
			*PathToken(Entry.ActorClass), *PathToken(Entry.LevelInstance),
			Entry.SelectionWeight, static_cast<int32>(Entry.Rotation));
		AppendArchitectureVariation(Out, Entry.Variation);
	}
}

void AppendStyleArchitecture(FString& Out,
	const FEFCalystoStyleArchitectureV6& Architecture)
{
	Out += TEXT("STYLE_ARCH_BEGIN|");
	AppendArchitectureMeshes(Out, TEXT("FLOOR"), Architecture.Floor);
	AppendArchitectureMeshes(Out, TEXT("WALL"), Architecture.Wall);
	AppendArchitectureMeshes(Out, TEXT("ROOF"), Architecture.Roof);
	for (int32 Index = 0; Index < Architecture.WallDoor.Num(); ++Index)
	{
		const FEFCalystoArchitectureDoorwayV6& Entry = Architecture.WallDoor[Index];
		Out += FString::Printf(TEXT("AD:WALL_DOOR,%d,%s,%s,%s,%s|"), Index,
			*PathToken(Entry.WallMesh), *PathToken(Entry.FrameMesh),
			*PathToken(Entry.DoorClass), *FloatBits(Entry.SelectionWeight));
		AppendArchitectureTransform(Out, Entry.WallTransform);
		AppendArchitectureTransform(Out, Entry.FrameTransform);
		AppendArchitectureTransform(Out, Entry.DoorTransform);
	}
	AppendArchitectureMeshes(Out, TEXT("DOOR_FRAME"), Architecture.DoorFrame);
	AppendArchitectureActors(Out, TEXT("DOOR"), Architecture.Door);
	Out += TEXT("START:") + PathToken(Architecture.StartBlueprint) + TEXT("|");
	Out += TEXT("END:") + PathToken(Architecture.EndBlueprint) + TEXT("|");
	AppendArchitectureMeshes(Out, TEXT("RAMP_TOP"), Architecture.RampTop);
	AppendArchitectureMeshes(Out, TEXT("RAMP_BOTTOM"), Architecture.RampBottom);
	for (int32 Index = 0; Index < Architecture.WallLights.Num(); ++Index)
	{
		const FEFCalystoArchitectureLightV6& Entry = Architecture.WallLights[Index];
		Out += FString::Printf(TEXT("AL:WALL,%d,%s,%d,%d,%s|"), Index,
			*PathToken(Entry.ActorClass), Entry.SelectionWeight,
			static_cast<int32>(Entry.PlacementZone), *FloatBits(Entry.PositionJitterCm));
		AppendArchitectureVariation(Out, Entry.Variation);
	}
	AppendArchitectureObjects(Out, TEXT("WALL_BOTTOM"), Architecture.WallBottomObjects);
	AppendArchitectureObjects(Out, TEXT("WALL_MIDDLE"), Architecture.WallMiddleObjects);
	AppendArchitectureObjects(Out, TEXT("WALL_TOP"), Architecture.WallTopObjects);
	AppendArchitectureObjects(Out, TEXT("ROOF"), Architecture.RoofObjects);
	Out += TEXT("STYLE_ARCH_END|");
}

void AppendRoomArchitecture(FString& Out,
	const FEFCalystoRoomArchitectureV6& Architecture)
{
	Out += TEXT("ROOM_ARCH_BEGIN|");
	AppendArchitectureObjects(Out, TEXT("WALL_BOTTOM"), Architecture.WallBottom);
	AppendArchitectureObjects(Out, TEXT("WALL_MIDDLE"), Architecture.WallMiddle);
	AppendArchitectureObjects(Out, TEXT("WALL_TOP"), Architecture.WallTop);
	AppendArchitectureObjects(Out, TEXT("FLOOR"), Architecture.Floor);
	AppendArchitectureObjects(Out, TEXT("CORNER_BOTTOM"), Architecture.CornerBottom);
	AppendArchitectureObjects(Out, TEXT("CORNER_MIDDLE"), Architecture.CornerMiddle);
	AppendArchitectureObjects(Out, TEXT("CORNER_TOP"), Architecture.CornerTop);
	AppendArchitectureObjects(Out, TEXT("ROOF"), Architecture.Roof);
	Out += TEXT("ROOM_ARCH_END|");
}

void AppendDecals(FString& Out, const FEFCalystoDecalProfileV6& Decals)
{
	Out += FString::Printf(TEXT("DEC:%d|"), static_cast<int32>(Decals.Mode));
	if (Decals.Mode == EEFCalystoDecalResolutionModeV6::InheritStyle ||
		Decals.Mode == EEFCalystoDecalResolutionModeV6::Block)
	{
		return;
	}
	Out += FString::Printf(TEXT("DSET:%s,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s|"),
		*FloatBits(Decals.ChancePerEligibleRoom),
		Decals.AllowedSurfaces, Decals.MaximumPerRoom, Decals.MaximumActivePerFloor,
		Decals.FloorLimit, Decals.WallLimit, Decals.RoofLimit,
		*FloatBits(Decals.MinimumSizeCm), *FloatBits(Decals.MaximumSizeCm),
		*FloatBits(Decals.FadeStartDistanceCm), *FloatBits(Decals.CullDistanceCm));
	TArray<const FEFCalystoDecalVariantV6*> Ordered;
	for (const FEFCalystoDecalVariantV6& Variant : Decals.Catalog)
	{
		Ordered.Add(&Variant);
	}
	Ordered.Sort([](const FEFCalystoDecalVariantV6& A, const FEFCalystoDecalVariantV6& B)
	{
		return StableNameLess(A.StableId, B.StableId);
	});
	for (const FEFCalystoDecalVariantV6* Variant : Ordered)
	{
		Out += FString::Printf(TEXT("DV:%s,%s,%s,%s,%d,%s|"), *CanonicalToken(Variant->StableId),
			*PathToken(Variant->Material), *PathToken(Variant->SourceColorTexture),
			*PathToken(Variant->SourceNormalTexture), Variant->AllowedSurfaces,
			*FloatBits(Variant->SelectionWeight));
	}
}

void AppendCatalogs(FString& Out, const TArray<FEFCalystoCatalogOverlayV6>& Catalogs)
{
	TArray<const FEFCalystoCatalogOverlayV6*> OrderedCategories;
	for (const FEFCalystoCatalogOverlayV6& Category : Catalogs)
	{
		OrderedCategories.Add(&Category);
	}
	OrderedCategories.Sort([](const FEFCalystoCatalogOverlayV6& A, const FEFCalystoCatalogOverlayV6& B)
	{
		return StableNameLess(A.CategoryId, B.CategoryId);
	});
	for (const FEFCalystoCatalogOverlayV6* Category : OrderedCategories)
	{
		Out += FString::Printf(TEXT("CAT:%s,%d|"),
			*CanonicalToken(Category->CategoryId), static_cast<int32>(Category->Mode));
		if (Category->Mode == EEFCalystoCatalogOverlayModeV6::Inherit ||
			Category->Mode == EEFCalystoCatalogOverlayModeV6::Block)
		{
			continue;
		}
		Out += FString::Printf(TEXT("CSET:%s,%s,%s,%d,%d|"),
			*FloatBits(Category->Presence.ChanceAtFloor1),
			*FloatBits(Category->Presence.ChanceAtFloor100), *FloatBits(Category->Presence.Tau),
			Category->Limits.MinimumWhenPresent, Category->Limits.MaximumPerFloor);
		AppendTierMix(Out, Category->Tiers.AtFloor1);
		AppendTierMix(Out, Category->Tiers.AtFloor100);
		TArray<const FEFCalystoCatalogEntryV6*> Entries;
		for (const FEFCalystoCatalogEntryV6& Entry : Category->Catalog)
		{
			Entries.Add(&Entry);
		}
		Entries.Sort([](const FEFCalystoCatalogEntryV6& A, const FEFCalystoCatalogEntryV6& B)
		{
			return StableNameLess(A.StableId, B.StableId);
		});
		for (const FEFCalystoCatalogEntryV6* Entry : Entries)
		{
			Out += FString::Printf(TEXT("CE:%s,%d,%s,%s,%d,%s,%d,%s,%d,%s,%d,%d,%d,%d|"),
				*CanonicalToken(Entry->StableId), static_cast<int32>(Entry->Rule),
				*PathToken(Entry->ActorClass), *FloatBits(Entry->SelectionWeight),
				static_cast<int32>(Entry->PlacementZone), *FloatBits(Entry->PositionJitterCm),
				static_cast<int32>(Entry->Tier), *CanonicalToken(Entry->Archetype),
				static_cast<int32>(Entry->Gender), *FloatBits(Entry->BaseThreatCost),
				Entry->FirstEligibleFloor, Entry->MaximumPerVariant, Entry->CooldownFloors,
				static_cast<int32>(Entry->Lifecycle));
		}
		TArray<const FEFCalystoChestContentEntryV6*> ChestEntries;
		for (const FEFCalystoChestContentEntryV6& Entry : Category->ChestContentsCatalog)
		{
			ChestEntries.Add(&Entry);
		}
		ChestEntries.Sort([](const FEFCalystoChestContentEntryV6& A, const FEFCalystoChestContentEntryV6& B)
		{
			return StableNameLess(A.StableId, B.StableId);
		});
		for (const FEFCalystoChestContentEntryV6* Entry : ChestEntries)
		{
			Out += FString::Printf(TEXT("CC:%s,%s,%s,%d,%d,%d,%d|"),
				*CanonicalToken(Entry->StableId), *PathToken(Entry->ContentClass),
				*FloatBits(Entry->SelectionWeight), static_cast<int32>(Entry->Tier),
				Entry->FirstEligibleFloor, Entry->CooldownFloors,
				Entry->bRequiresGraveyardEligibility ? 1 : 0);
		}
	}
}

FString CatalogHash(const TArray<FEFCalystoCatalogOverlayV6>& Catalogs)
{
	FString Canonical(TEXT("EFCalystoCatalogV6|"));
	AppendCatalogs(Canonical, Catalogs);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString MaterialHash(const FEFCalystoSurfaceMaterialSetV6& Materials)
{
	FString Canonical(TEXT("EFCalystoMaterialsV6|"));
	AppendMaterials(Canonical, Materials);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString ArchitectureHash(const FEFCalystoStyleArchitectureV6& Architecture)
{
	FString Canonical(TEXT("EFCalystoStyleArchitectureV6|"));
	AppendStyleArchitecture(Canonical, Architecture);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString ArchitectureHash(const FEFCalystoRoomArchitectureV6& Architecture)
{
	FString Canonical(TEXT("EFCalystoRoomArchitectureV6|"));
	AppendRoomArchitecture(Canonical, Architecture);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString DecalHash(const FEFCalystoDecalProfileV6& Decals)
{
	FString Canonical(TEXT("EFCalystoDecalsV6|"));
	AppendDecals(Canonical, Decals);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FEFCalystoSurfaceMaterialSetV6 ResolveThemeMaterials(
	const FEFCalystoSurfaceMaterialSetV6& StyleMaterials,
	const FEFCalystoThemeMaterialPolicyV6& ThemeMaterials)
{
	FEFCalystoSurfaceMaterialSetV6 Result = StyleMaterials;
	if (ThemeMaterials.FloorMode == EEFCalystoMaterialResolutionModeV6::Override)
	{
		Result.FloorMaterial = ThemeMaterials.Overrides.FloorMaterial;
	}
	if (ThemeMaterials.WallMode == EEFCalystoMaterialResolutionModeV6::Override)
	{
		Result.WallMaterial = ThemeMaterials.Overrides.WallMaterial;
	}
	if (ThemeMaterials.RoofMode == EEFCalystoMaterialResolutionModeV6::Override)
	{
		Result.RoofMaterial = ThemeMaterials.Overrides.RoofMaterial;
	}
	return Result;
}

void GatherMaterials(const FEFCalystoSurfaceMaterialSetV6& Materials,
	TArray<FSoftObjectPath>& Paths)
{
	AddUniquePath(Paths, Materials.FloorMaterial);
	AddUniquePath(Paths, Materials.WallMaterial);
	AddUniquePath(Paths, Materials.RoofMaterial);
}

void GatherArchitectureObjects(const TArray<FEFCalystoArchitectureObjectV6>& Objects,
	TArray<FSoftObjectPath>& Paths)
{
	for (const FEFCalystoArchitectureObjectV6& Object : Objects)
	{
		switch (Object.Type)
		{
		case EEFCalystoArchitectureObjectTypeV6::StaticMesh:
			AddUniquePath(Paths, Object.Mesh);
			break;
		case EEFCalystoArchitectureObjectTypeV6::ActorBlueprint:
			AddUniquePath(Paths, Object.ActorClass);
			break;
		case EEFCalystoArchitectureObjectTypeV6::LevelInstance:
			AddUniquePath(Paths, Object.LevelInstance);
			break;
		default:
			break;
		}
	}
}

void GatherStyleArchitecture(const FEFCalystoStyleArchitectureV6& Architecture,
	TArray<FSoftObjectPath>& Paths)
{
	auto GatherMeshes = [&Paths](const TArray<FEFCalystoArchitectureMeshV6>& Entries)
	{
		for (const FEFCalystoArchitectureMeshV6& Entry : Entries)
		{
			AddUniquePath(Paths, Entry.Mesh);
		}
	};
	GatherMeshes(Architecture.Floor);
	GatherMeshes(Architecture.Wall);
	GatherMeshes(Architecture.Roof);
	for (const FEFCalystoArchitectureDoorwayV6& Entry : Architecture.WallDoor)
	{
		AddUniquePath(Paths, Entry.WallMesh);
		AddUniquePath(Paths, Entry.FrameMesh);
		AddUniquePath(Paths, Entry.DoorClass);
	}
	GatherMeshes(Architecture.DoorFrame);
	for (const FEFCalystoArchitectureActorV6& Entry : Architecture.Door)
	{
		AddUniquePath(Paths, Entry.ActorClass);
	}
	AddUniquePath(Paths, Architecture.StartBlueprint);
	AddUniquePath(Paths, Architecture.EndBlueprint);
	GatherMeshes(Architecture.RampTop);
	GatherMeshes(Architecture.RampBottom);
	for (const FEFCalystoArchitectureLightV6& Entry : Architecture.WallLights)
	{
		AddUniquePath(Paths, Entry.ActorClass);
	}
	GatherArchitectureObjects(Architecture.WallBottomObjects, Paths);
	GatherArchitectureObjects(Architecture.WallMiddleObjects, Paths);
	GatherArchitectureObjects(Architecture.WallTopObjects, Paths);
	GatherArchitectureObjects(Architecture.RoofObjects, Paths);
}

void GatherRoomArchitecture(const FEFCalystoRoomArchitectureV6& Architecture,
	TArray<FSoftObjectPath>& Paths)
{
	GatherArchitectureObjects(Architecture.WallBottom, Paths);
	GatherArchitectureObjects(Architecture.WallMiddle, Paths);
	GatherArchitectureObjects(Architecture.WallTop, Paths);
	GatherArchitectureObjects(Architecture.Floor, Paths);
	GatherArchitectureObjects(Architecture.CornerBottom, Paths);
	GatherArchitectureObjects(Architecture.CornerMiddle, Paths);
	GatherArchitectureObjects(Architecture.CornerTop, Paths);
	GatherArchitectureObjects(Architecture.Roof, Paths);
}

void GatherDecals(const FEFCalystoDecalProfileV6& Decals,
	TArray<FSoftObjectPath>& Paths)
{
	if (Decals.Mode == EEFCalystoDecalResolutionModeV6::Block ||
		Decals.Mode == EEFCalystoDecalResolutionModeV6::InheritStyle)
	{
		return;
	}
	for (const FEFCalystoDecalVariantV6& Variant : Decals.Catalog)
	{
		AddUniquePath(Paths, Variant.Material);
		AddUniquePath(Paths, Variant.SourceColorTexture);
		AddUniquePath(Paths, Variant.SourceNormalTexture);
	}
}

void GatherCategoryEntries(const FEFCalystoCatalogOverlayV6& Category,
	TArray<FSoftObjectPath>& Paths)
{
	if (Category.Mode == EEFCalystoCatalogOverlayModeV6::Block)
	{
		return;
	}
	for (const FEFCalystoCatalogEntryV6& Entry : Category.Catalog)
	{
		if (Entry.Rule == EEFCalystoCatalogEntryRuleV6::Allow)
		{
			AddUniquePath(Paths, Entry.ActorClass);
		}
	}
	for (const FEFCalystoChestContentEntryV6& Entry : Category.ChestContentsCatalog)
	{
		AddUniquePath(Paths, Entry.ContentClass);
	}
}

const FEFCalystoCatalogOverlayV6* FindCategory(
	const TArray<FEFCalystoCatalogOverlayV6>& Categories, const FName CategoryId)
{
	return Categories.FindByPredicate([CategoryId](const FEFCalystoCatalogOverlayV6& Candidate)
	{
		return Candidate.CategoryId.IsEqual(CategoryId, ENameCase::IgnoreCase);
	});
}

void GatherResolvedCatalogs(const TArray<FEFCalystoCatalogOverlayV6>& StyleCatalogs,
	const TArray<FEFCalystoCatalogOverlayV6>* ThemeCatalogs,
	TArray<FSoftObjectPath>& Paths)
{
	TSet<FName> CategoryIds;
	for (const FEFCalystoCatalogOverlayV6& StyleCategory : StyleCatalogs)
	{
		CategoryIds.Add(StyleCategory.CategoryId);
	}
	if (ThemeCatalogs)
	{
		for (const FEFCalystoCatalogOverlayV6& ThemeCategory : *ThemeCatalogs)
		{
			CategoryIds.Add(ThemeCategory.CategoryId);
		}
	}
	TArray<FName> OrderedIds = CategoryIds.Array();
	OrderedIds.Sort([](const FName A, const FName B) { return StableNameLess(A, B); });
	for (const FName CategoryId : OrderedIds)
	{
		const FEFCalystoCatalogOverlayV6* StyleCategory = FindCategory(StyleCatalogs, CategoryId);
		const FEFCalystoCatalogOverlayV6* ThemeCategory = ThemeCatalogs ? FindCategory(*ThemeCatalogs, CategoryId) : nullptr;
		if (!ThemeCategory || ThemeCategory->Mode == EEFCalystoCatalogOverlayModeV6::Inherit)
		{
			if (StyleCategory) GatherCategoryEntries(*StyleCategory, Paths);
		}
		else if (ThemeCategory->Mode == EEFCalystoCatalogOverlayModeV6::Extend)
		{
			if (StyleCategory) GatherCategoryEntries(*StyleCategory, Paths);
			GatherCategoryEntries(*ThemeCategory, Paths);
		}
		else if (ThemeCategory->Mode == EEFCalystoCatalogOverlayModeV6::Replace)
		{
			GatherCategoryEntries(*ThemeCategory, Paths);
		}
	}
}

const FEFCalystoResolvedThemeProfileV6* FindResolvedTheme(
	const FEFCalystoResolvedFloorPlanV6& Plan, const FName ThemeId)
{
	return Plan.Themes.FindByPredicate([ThemeId](const FEFCalystoResolvedThemeProfileV6& Candidate)
	{
		return Candidate.ThemeId.IsEqual(ThemeId, ENameCase::IgnoreCase);
	});
}

} // namespace EFCalystoPolicyV6Private

void FEFCalystoTierMixV6::RefreshNothing()
{
	Nothing = FMath::Max(0.0f, 1.0f - (Common + Uncommon + Rare + Epic));
}

void FEFCalystoTierCurveV6::RefreshNothing()
{
	AtFloor1.RefreshNothing();
	AtFloor100.RefreshNothing();
}

FString FEFCalystoDungeonDirectorMathV6::HashCanonicalText(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	return EFCalystoPolicyV6Private::Sha256(
		reinterpret_cast<const uint8*>(Utf8.Get()), static_cast<uint64>(Utf8.Length()));
}

int64 FEFCalystoDungeonDirectorMathV6::BuildStableRoomId(
	const int64 FloorSeed, const FEFCalystoRoomIdentityInputV6& Input,
	const float QuantizationCm)
{
	using namespace EFCalystoPolicyV6Private;
	if (!FMath::IsFinite(QuantizationCm) || QuantizationCm <= 0.0f ||
		Input.LocalCenter.ContainsNaN() || Input.Extents.ContainsNaN() ||
		Input.CollisionOrdinal < 0 || Input.TopologyKind.IsNone())
	{
		return 0;
	}
	auto Q = [QuantizationCm](const double Value)
	{
		return FMath::RoundToInt64(Value / static_cast<double>(QuantizationCm));
	};
	const FString Canonical = FString::Printf(
		TEXT("EFCalystoStableRoomV6|%lld|%lld,%lld,%lld|%lld,%lld,%lld|%s|%d|%d"),
		FloorSeed, Q(Input.LocalCenter.X), Q(Input.LocalCenter.Y), Q(Input.LocalCenter.Z),
		Q(Input.Extents.X), Q(Input.Extents.Y), Q(Input.Extents.Z),
		*CanonicalToken(Input.TopologyKind), Input.CollisionOrdinal, Input.RoomFlags);
	uint64 Result = HashString64(Canonical) & PositiveInt64Mask;
	return static_cast<int64>(Result == 0 ? 1 : Result);
}

bool FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(const int32 RoomFlags)
{
	const int32 ProtectedMask = static_cast<int32>(EEFCalystoRoomFlagsV6::Start) |
		static_cast<int32>(EEFCalystoRoomFlagsV6::End) |
		static_cast<int32>(EEFCalystoRoomFlagsV6::Critical) |
		static_cast<int32>(EEFCalystoRoomFlagsV6::Progression);
	return (RoomFlags & ProtectedMask) != 0;
}

double FEFCalystoDungeonDirectorMathV6::ThemePresenceUniform(
	const int64 FloorSeed, const int64 StableRoomId, const FName StyleId)
{
	using namespace EFCalystoPolicyV6Private;
	return UniformFromHash(HashString64(FString::Printf(
		TEXT("EFCalystoThemePresenceV6|%lld|%lld|%s"), FloorSeed,
		StableRoomId, *CanonicalToken(StyleId))));
}

double FEFCalystoDungeonDirectorMathV6::ThemeGuaranteeUniform(
	const int64 FloorSeed, const int64 StableRoomId, const FName StyleId)
{
	using namespace EFCalystoPolicyV6Private;
	return UniformFromHash(HashString64(FString::Printf(
		TEXT("EFCalystoThemeGuaranteeV6|%lld|%lld|%s"), FloorSeed,
		StableRoomId, *CanonicalToken(StyleId))));
}

double FEFCalystoDungeonDirectorMathV6::ThemeTypeUniform(
	const int64 FloorSeed, const int64 StableRoomId, const FName StyleId)
{
	using namespace EFCalystoPolicyV6Private;
	return UniformFromHash(HashString64(FString::Printf(
		TEXT("EFCalystoThemeTypeV6|%lld|%lld|%s"), FloorSeed,
		StableRoomId, *CanonicalToken(StyleId))));
}

int64 FEFCalystoDungeonDirectorMathV6::SelectGuaranteedThemeRoomId(
	const int64 FloorSeed,
	const FName StyleId,
	const TArray<int64>& EligibleStableRoomIds)
{
	if (StyleId.IsNone())
	{
		return 0;
	}

	int64 SelectedRoomId = 0;
	double SelectedDraw = 1.0;
	for (const int64 StableRoomId : EligibleStableRoomIds)
	{
		if (StableRoomId <= 0)
		{
			continue;
		}
		const double Draw = ThemeGuaranteeUniform(FloorSeed, StableRoomId, StyleId);
		if (SelectedRoomId == 0 || Draw < SelectedDraw
			|| (Draw == SelectedDraw && StableRoomId < SelectedRoomId))
		{
			SelectedRoomId = StableRoomId;
			SelectedDraw = Draw;
		}
	}
	return SelectedRoomId;
}

bool FEFCalystoDungeonDirectorMathV6::ResolveCatalogs(
	const TArray<FEFCalystoCatalogOverlayV6>& StyleCatalogs,
	const TArray<FEFCalystoCatalogOverlayV6>& ThemeOverlays,
	TArray<FEFCalystoCatalogOverlayV6>& OutCatalogs, FString& OutError)
{
	using namespace EFCalystoPolicyV6Private;
	OutCatalogs.Reset();
	OutError.Reset();
	if (!ValidateCatalogs(StyleCatalogs, true, TEXT("Frozen Style catalogs"), OutError) ||
		!ValidateCatalogs(ThemeOverlays, false, TEXT("Frozen Theme overlays"), OutError))
	{
		return false;
	}
	auto Normalize = [](FEFCalystoCatalogOverlayV6& Category)
	{
		Category.Mode = EEFCalystoCatalogOverlayModeV6::Replace;
		Category.Catalog.RemoveAll([](const FEFCalystoCatalogEntryV6& Entry)
		{
			return Entry.Rule == EEFCalystoCatalogEntryRuleV6::Block;
		});
		for (FEFCalystoCatalogEntryV6& Entry : Category.Catalog)
		{
			Entry.Rule = EEFCalystoCatalogEntryRuleV6::Allow;
		}
		Category.Catalog.Sort([](const FEFCalystoCatalogEntryV6& A, const FEFCalystoCatalogEntryV6& B)
		{
			return StableNameLess(A.StableId, B.StableId);
		});
		Category.ChestContentsCatalog.Sort([](const FEFCalystoChestContentEntryV6& A, const FEFCalystoChestContentEntryV6& B)
		{
			return StableNameLess(A.StableId, B.StableId);
		});
	};
	for (const FEFCalystoCatalogOverlayV6& StyleCategory : StyleCatalogs)
	{
		if (StyleCategory.Mode != EEFCalystoCatalogOverlayModeV6::Block)
		{
			FEFCalystoCatalogOverlayV6 Copy = StyleCategory;
			Normalize(Copy);
			OutCatalogs.Add(MoveTemp(Copy));
		}
	}
	for (const FEFCalystoCatalogOverlayV6& Overlay : ThemeOverlays)
	{
		const int32 ExistingIndex = OutCatalogs.IndexOfByPredicate([&Overlay](const FEFCalystoCatalogOverlayV6& Candidate)
		{
			return Candidate.CategoryId.IsEqual(Overlay.CategoryId, ENameCase::IgnoreCase);
		});
		if (Overlay.Mode == EEFCalystoCatalogOverlayModeV6::Inherit)
		{
			continue;
		}
		if (Overlay.Mode == EEFCalystoCatalogOverlayModeV6::Block)
		{
			if (ExistingIndex != INDEX_NONE) OutCatalogs.RemoveAt(ExistingIndex);
			continue;
		}
		if (Overlay.Mode == EEFCalystoCatalogOverlayModeV6::Replace || ExistingIndex == INDEX_NONE)
		{
			FEFCalystoCatalogOverlayV6 Copy = Overlay;
			Normalize(Copy);
			if (ExistingIndex == INDEX_NONE) OutCatalogs.Add(MoveTemp(Copy));
			else OutCatalogs[ExistingIndex] = MoveTemp(Copy);
			continue;
		}

		FEFCalystoCatalogOverlayV6& Effective = OutCatalogs[ExistingIndex];
		Effective.Presence = Overlay.Presence;
		Effective.Tiers = Overlay.Tiers;
		Effective.Limits = Overlay.Limits;
		for (const FEFCalystoCatalogEntryV6& Entry : Overlay.Catalog)
		{
			const int32 EntryIndex = Effective.Catalog.IndexOfByPredicate([&Entry](const FEFCalystoCatalogEntryV6& Candidate)
			{
				return Candidate.StableId.IsEqual(Entry.StableId, ENameCase::IgnoreCase);
			});
			if (Entry.Rule == EEFCalystoCatalogEntryRuleV6::Block)
			{
				if (EntryIndex != INDEX_NONE) Effective.Catalog.RemoveAt(EntryIndex);
			}
			else if (EntryIndex == INDEX_NONE)
			{
				Effective.Catalog.Add(Entry);
			}
			else
			{
				Effective.Catalog[EntryIndex] = Entry;
			}
		}
		for (const FEFCalystoChestContentEntryV6& Entry : Overlay.ChestContentsCatalog)
		{
			const int32 EntryIndex = Effective.ChestContentsCatalog.IndexOfByPredicate([&Entry](const FEFCalystoChestContentEntryV6& Candidate)
			{
				return Candidate.StableId.IsEqual(Entry.StableId, ENameCase::IgnoreCase);
			});
			if (EntryIndex == INDEX_NONE) Effective.ChestContentsCatalog.Add(Entry);
			else Effective.ChestContentsCatalog[EntryIndex] = Entry;
		}
		Normalize(Effective);
	}
	OutCatalogs.Sort([](const FEFCalystoCatalogOverlayV6& A, const FEFCalystoCatalogOverlayV6& B)
	{
		return StableNameLess(A.CategoryId, B.CategoryId);
	});
	return true;
}

FEFCalystoDecalProfileV6 FEFCalystoDungeonDirectorMathV6::ResolveDecals(
	const FEFCalystoDecalProfileV6& StyleDecals,
	const FEFCalystoDecalProfileV6& ThemeDecals)
{
	return ThemeDecals.Mode == EEFCalystoDecalResolutionModeV6::InheritStyle
		? StyleDecals
		: ThemeDecals;
}

bool FEFCalystoDungeonDirectorMathV6::ResolveRoomContext(
	const FEFCalystoResolvedFloorPlanV6& Plan, const int64 StableRoomId,
	const int32 RoomFlags, FEFCalystoRoomContextV6& OutContext,
	FString& OutError, const bool bForceThemePresence)
{
	using namespace EFCalystoPolicyV6Private;
	OutContext = FEFCalystoRoomContextV6();
	OutError.Reset();
	if (StableRoomId <= 0 || Plan.StyleId.IsNone() ||
		!IsFiniteUnit(Plan.ThemeRoomChance) || !IsSha256(Plan.PolicyHash) ||
		!FMath::IsFinite(Plan.RoomIdentityQuantizationCm) ||
		Plan.RoomIdentityQuantizationCm <= 0.0f ||
		Plan.MaximumRoomRecords <= 0 || Plan.DecalComponentPoolCapacity <= 0 ||
		!IsSha256(Plan.FloorPlanHash) || !IsSha256(Plan.StyleArchitectureHash) ||
		!ValidateMaterials(Plan.StyleMaterials, TEXT("Resolved Style"), OutError))
	{
		return Fail(OutError, OutError.IsEmpty()
			? TEXT("Room resolution requires a valid frozen V6 floor plan and positive Stable Room ID.")
			: OutError);
	}

	OutContext.StableRoomId = StableRoomId;
	OutContext.RoomFlags = RoomFlags;
	OutContext.StyleId = Plan.StyleId;
	OutContext.ThemeId = UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId;
	OutContext.EffectiveMaterials = Plan.StyleMaterials;
	OutContext.ArchitectureHash = Plan.StyleArchitectureHash;
	OutContext.CatalogHash = Plan.StyleCatalogHash;
	OutContext.DecalHash = Plan.StyleDecalHash;

	if (!IsProtectedRoom(RoomFlags) &&
		(bForceThemePresence ||
		 ThemePresenceUniform(Plan.FloorSeed, StableRoomId, Plan.StyleId) <
			 static_cast<double>(Plan.ThemeRoomChance)))
	{
		const int32 SelectedIndex = SelectAliasIndex(
			ThemeTypeUniform(Plan.FloorSeed, StableRoomId, Plan.StyleId),
			Plan.ThemeAliasProbability, Plan.ThemeAliasIndex);
		if (!Plan.Themes.IsValidIndex(SelectedIndex))
		{
			return Fail(OutError, TEXT("A themed-room roll succeeded but the frozen V6 Theme alias table is invalid."));
		}
		const FEFCalystoResolvedThemeProfileV6* Selected = &Plan.Themes[SelectedIndex];
		if (!IsSha256(Selected->ArchitectureHash))
		{
			return Fail(OutError, TEXT("A themed-room roll selected an invalid frozen Architecture profile."));
		}
		OutContext.bIsThemed = true;
		OutContext.ThemeId = Selected->ThemeId;
		OutContext.ArchitectureHash = Selected->ArchitectureHash;
		OutContext.EffectiveMaterials = Selected->EffectiveMaterials;
		OutContext.CatalogHash = Selected->CatalogHash;
		OutContext.DecalHash = Selected->DecalHash;
	}

	const FString Canonical = FString::Printf(
		TEXT("EFCalystoRoomContextV6|%s|%lld|%d|%s|%d|%s|%s|%s|%s|%s"),
		*Plan.FloorPlanHash, StableRoomId, RoomFlags, *CanonicalToken(OutContext.StyleId),
		OutContext.bIsThemed ? 1 : 0, *CanonicalToken(OutContext.ThemeId),
		*MaterialHash(OutContext.EffectiveMaterials), *OutContext.CatalogHash,
		*OutContext.DecalHash, *OutContext.ArchitectureHash);
	OutContext.RoomContextHash = HashCanonicalText(Canonical);
	if (!IsSha256(OutContext.RoomContextHash))
	{
		return Fail(OutError, TEXT("V6 failed to hash the resolved room context."));
	}
	return true;
}

UEFCalystoDungeonDirectorPolicyV6Asset::UEFCalystoDungeonDirectorPolicyV6Asset()
{
	if (Styles.IsEmpty() && RoomThemes.IsEmpty())
	{
		InitializeV6Defaults();
	}
}

FPrimaryAssetId UEFCalystoDungeonDirectorPolicyV6Asset::GetPrimaryAssetId() const
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return FPrimaryAssetId();
	}
	return FPrimaryAssetId(
		FPrimaryAssetType(TEXT("EFCalystoDungeonDirectorPolicyV6Asset")), GetFName());
}

void UEFCalystoDungeonDirectorPolicyV6Asset::InitializeV6Defaults()
{
	using namespace EFCalystoPolicyV6Private;
	SchemaVersion = 6;
	RuntimeGeneratorVersion = 6;
	IdentityHashSchemaVersion = HashSchemaVersion;
	PolicyId = TEXT("CalystoDungeonDirectorV6");
	Styles = {
		MakeStyle(TEXT("Standard"), 0.50f),
		MakeStyle(TEXT("Compact"), 0.25f),
		MakeStyle(TEXT("Branching"), 0.25f)
	};
	RoomThemes = {MakeForgeTheme(), MakeShrineTheme()};
	PerformanceAndSafety.ValidatedDungeonSizes.Reset();
	for (int32 Size = 18; Size <= 30; ++Size)
	{
		PerformanceAndSafety.ValidatedDungeonSizes.Add(Size);
	}
	PerformanceAndSafety.HardCeilings = FEFCalystoFloorBudgetV6();
	PerformanceAndSafety.RoomIdentityQuantizationCm = 10.0f;
	PerformanceAndSafety.DecalComponentPoolCapacity = 24;
	PerformanceAndSafety.MaximumRoomRecords = 2048;
	RefreshDerivedData();
	RebuildCookBundleReferences();
}

const FEFCalystoStyleProfileV6* UEFCalystoDungeonDirectorPolicyV6Asset::FindStyle(
	const FName StyleId) const
{
	return Styles.FindByPredicate([StyleId](const FEFCalystoStyleProfileV6& Candidate)
	{
		return Candidate.StyleId.IsEqual(StyleId, ENameCase::IgnoreCase);
	});
}

const FEFCalystoRoomThemeProfileV6*
UEFCalystoDungeonDirectorPolicyV6Asset::FindRoomTheme(const FName ThemeId) const
{
	return RoomThemes.FindByPredicate([ThemeId](const FEFCalystoRoomThemeProfileV6& Candidate)
	{
		return Candidate.ThemeId.IsEqual(ThemeId, ENameCase::IgnoreCase);
	});
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::Validate(FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutError.Reset();
	if (SchemaVersion != 6 || RuntimeGeneratorVersion != 6 ||
		IdentityHashSchemaVersion != HashSchemaVersion ||
		PolicyId != FName(TEXT("CalystoDungeonDirectorV6")))
	{
		return Fail(OutError, TEXT("V6 identity must be schema 6, generator 6, identity hash schema 3, and Policy ID CalystoDungeonDirectorV6."));
	}
	if (!FMath::IsFinite(PerformanceAndSafety.RoomIdentityQuantizationCm) ||
		PerformanceAndSafety.RoomIdentityQuantizationCm <= 0.0f ||
		PerformanceAndSafety.DecalComponentPoolCapacity < 1 ||
		PerformanceAndSafety.DecalComponentPoolCapacity > 24 ||
		PerformanceAndSafety.MaximumRoomRecords < 1 ||
		PerformanceAndSafety.ValidatedDungeonSizes.IsEmpty())
	{
		return Fail(OutError, TEXT("V6 Performance & Safety contains invalid limits."));
	}
	TSet<int32> Sizes;
	for (const int32 Size : PerformanceAndSafety.ValidatedDungeonSizes)
	{
		if (Size < 18 || Size > 30 || Sizes.Contains(Size))
		{
			return Fail(OutError, TEXT("Validated Dungeon Sizes must be unique and remain inside 18..30."));
		}
		Sizes.Add(Size);
	}

	if (Styles.IsEmpty())
	{
		return Fail(OutError, TEXT("V6 requires at least one Dungeon Style."));
	}
	TSet<FString> StyleIds;
	double EnabledStyleWeight = 0.0;
	bool bAnyThemedRooms = false;
	for (const FEFCalystoStyleProfileV6& Style : Styles)
	{
		const FString StyleToken = CanonicalToken(Style.StyleId);
		if (StyleToken.IsEmpty() || StyleIds.Contains(StyleToken))
		{
			return Fail(OutError, TEXT("V6 contains a missing or duplicate Style ID."));
		}
		StyleIds.Add(StyleToken);
		if (!IsFiniteNonNegative(Style.SelectionWeight) || !IsFiniteUnit(Style.RoomThemeChance))
		{
			return Fail(OutError, FString::Printf(TEXT("Style %s contains an invalid weight or Room Theme Chance."), *Style.StyleId.ToString()));
		}
		if (Style.bEnabled)
		{
			if (Style.SelectionWeight <= 0.0f)
			{
				return Fail(OutError, FString::Printf(TEXT("Enabled Style %s requires positive Selection Weight."), *Style.StyleId.ToString()));
			}
			EnabledStyleWeight += Style.SelectionWeight;
			bAnyThemedRooms |= Style.RoomThemeChance > 0.0f;
		}
		if (!ValidateRange(Style.Layout.DungeonSize, 18.0f, 30.0f, Style.StyleId.ToString() + TEXT(" Dungeon Size"), OutError) ||
			!ValidateRange(Style.Layout.CandidateDensity, 0.20f, 0.50f, Style.StyleId.ToString() + TEXT(" Candidate Density"), OutError) ||
			!ValidateRange(Style.Layout.SidePathChance, 0.30f, 0.70f, Style.StyleId.ToString() + TEXT(" Side Path Chance"), OutError))
		{
			return false;
		}
		if (Style.Layout.MinimumRoomSize < 4 || Style.Layout.MaximumRoomSize > 8 ||
			Style.Layout.MinimumRoomSize > Style.Layout.MaximumRoomSize ||
			!IsFiniteNonNegative(Style.Threat.BudgetAtFloor1) ||
			!IsFiniteNonNegative(Style.Threat.BudgetAtFloor100) || Style.Threat.Tau <= 0.0f ||
			Style.Threat.MinimumLevelOffset > Style.Threat.MaximumLevelOffset)
		{
			return Fail(OutError, FString::Printf(TEXT("Style %s has invalid layout or threat bounds."), *Style.StyleId.ToString()));
		}
		const FEFCalystoFloorBudgetV6& B = Style.GlobalBudgets;
		const FEFCalystoFloorBudgetV6& H = PerformanceAndSafety.HardCeilings;
		if (B.MaximumEnemies < 0 || B.MaximumEnemies > H.MaximumEnemies ||
			B.MaximumLooseFood < 0 || B.MaximumLooseFood > H.MaximumLooseFood ||
			B.MaximumChests < 0 || B.MaximumChests > H.MaximumChests ||
			B.MaximumLootActors < 0 || B.MaximumLootActors > H.MaximumLootActors ||
			B.MaximumSpecialEvents < 0 || B.MaximumSpecialEvents > H.MaximumSpecialEvents ||
			B.MaximumDirectorActors < 0 || B.MaximumDirectorActors > H.MaximumDirectorActors)
		{
			return Fail(OutError, FString::Printf(TEXT("Style %s exceeds a V6 hard safety ceiling."), *Style.StyleId.ToString()));
		}
		if (!ValidateMaterials(Style.DungeonMaterials, TEXT("Style ") + Style.StyleId.ToString(), OutError) ||
			!ValidateStyleArchitecture(Style.Architecture,
				TEXT("Style ") + Style.StyleId.ToString() + TEXT(" Architecture"), OutError) ||
			!ValidateCatalogs(Style.Catalogs, true, TEXT("Style ") + Style.StyleId.ToString(), OutError) ||
			!ValidateDecals(Style.Decals, true, PerformanceAndSafety.DecalComponentPoolCapacity,
				TEXT("Style ") + Style.StyleId.ToString(), OutError))
		{
			return false;
		}
	}
	if (EnabledStyleWeight <= 0.0)
	{
		return Fail(OutError, TEXT("V6 requires positive enabled Dungeon Style weight."));
	}

	TSet<FString> ThemeIds;
	double EnabledThemeWeight = 0.0;
	for (const FEFCalystoRoomThemeProfileV6& Theme : RoomThemes)
	{
		const FString ThemeToken = CanonicalToken(Theme.ThemeId);
		if (ThemeToken.IsEmpty() || ThemeToken == CanonicalToken(NoThemeId) || ThemeIds.Contains(ThemeToken))
		{
			return Fail(OutError, TEXT("Room Themes require unique IDs and may not author the reserved NoTheme result."));
		}
		ThemeIds.Add(ThemeToken);
		if (!IsFiniteNonNegative(Theme.SelectionWeight))
		{
			return Fail(OutError, FString::Printf(TEXT("Theme %s has an invalid Selection Weight."), *Theme.ThemeId.ToString()));
		}
		if (Theme.bEnabled)
		{
			if (Theme.SelectionWeight <= 0.0f)
			{
				return Fail(OutError, FString::Printf(TEXT("Enabled Theme %s requires positive Selection Weight."), *Theme.ThemeId.ToString()));
			}
			EnabledThemeWeight += Theme.SelectionWeight;
		}
		if ((Theme.RoomMaterials.FloorMode == EEFCalystoMaterialResolutionModeV6::Override && Theme.RoomMaterials.Overrides.FloorMaterial.IsNull()) ||
			(Theme.RoomMaterials.WallMode == EEFCalystoMaterialResolutionModeV6::Override && Theme.RoomMaterials.Overrides.WallMaterial.IsNull()) ||
			(Theme.RoomMaterials.RoofMode == EEFCalystoMaterialResolutionModeV6::Override && Theme.RoomMaterials.Overrides.RoofMaterial.IsNull()))
		{
			return Fail(OutError, FString::Printf(TEXT("Theme %s has an Override material mode with a null material."), *Theme.ThemeId.ToString()));
		}
		if (!ValidateRoomArchitecture(Theme.Architecture,
				TEXT("Theme ") + Theme.ThemeId.ToString() + TEXT(" Architecture"), OutError) ||
			!ValidateCatalogs(Theme.Catalogs, false, TEXT("Theme ") + Theme.ThemeId.ToString(), OutError) ||
			!ValidateDecals(Theme.Decals, false, PerformanceAndSafety.DecalComponentPoolCapacity,
				TEXT("Theme ") + Theme.ThemeId.ToString(), OutError))
		{
			return false;
		}
	}
	if (bAnyThemedRooms && EnabledThemeWeight <= 0.0)
	{
		return Fail(OutError, TEXT("At least one enabled Style can create themed rooms, but V6 has no enabled Room Theme weight."));
	}
	return true;
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::ValidatePolicy() const
{
	FString Error;
	const bool bValid = Validate(Error);
	if (!bValid)
	{
		UE_LOG(LogEFCalystoDungeonDirectorPolicyV6, Warning, TEXT("Calysto V6 policy validation failed: %s"), *Error);
	}
	return bValid;
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::BuildMaterialCanonicalString() const
{
	using namespace EFCalystoPolicyV6Private;
	FString Result(TEXT("EFCalystoPolicyMaterialsV6|"));
	TArray<const FEFCalystoStyleProfileV6*> OrderedStyles;
	for (const FEFCalystoStyleProfileV6& Style : Styles) OrderedStyles.Add(&Style);
	OrderedStyles.Sort([](const FEFCalystoStyleProfileV6& A, const FEFCalystoStyleProfileV6& B)
	{
		return StableNameLess(A.StyleId, B.StyleId);
	});
	for (const FEFCalystoStyleProfileV6* Style : OrderedStyles)
	{
		Result += TEXT("S:") + CanonicalToken(Style->StyleId) + TEXT("|");
		AppendMaterials(Result, Style->DungeonMaterials);
	}
	TArray<const FEFCalystoRoomThemeProfileV6*> OrderedThemes;
	for (const FEFCalystoRoomThemeProfileV6& Theme : RoomThemes) OrderedThemes.Add(&Theme);
	OrderedThemes.Sort([](const FEFCalystoRoomThemeProfileV6& A, const FEFCalystoRoomThemeProfileV6& B)
	{
		return StableNameLess(A.ThemeId, B.ThemeId);
	});
	for (const FEFCalystoRoomThemeProfileV6* Theme : OrderedThemes)
	{
		Result += FString::Printf(TEXT("T:%s,%d,%d,%d|"), *CanonicalToken(Theme->ThemeId),
			static_cast<int32>(Theme->RoomMaterials.FloorMode),
			static_cast<int32>(Theme->RoomMaterials.WallMode),
			static_cast<int32>(Theme->RoomMaterials.RoofMode));
		Result += Theme->RoomMaterials.FloorMode == EEFCalystoMaterialResolutionModeV6::Override
			? TEXT("F:") + PathToken(Theme->RoomMaterials.Overrides.FloorMaterial) + TEXT("|")
			: TEXT("F:INHERIT|");
		Result += Theme->RoomMaterials.WallMode == EEFCalystoMaterialResolutionModeV6::Override
			? TEXT("W:") + PathToken(Theme->RoomMaterials.Overrides.WallMaterial) + TEXT("|")
			: TEXT("W:INHERIT|");
		Result += Theme->RoomMaterials.RoofMode == EEFCalystoMaterialResolutionModeV6::Override
			? TEXT("R:") + PathToken(Theme->RoomMaterials.Overrides.RoofMaterial) + TEXT("|")
			: TEXT("R:INHERIT|");
	}
	return Result;
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::BuildDecalCanonicalString() const
{
	using namespace EFCalystoPolicyV6Private;
	FString Result(TEXT("EFCalystoPolicyDecalsV6|"));
	TArray<const FEFCalystoStyleProfileV6*> OrderedStyles;
	for (const FEFCalystoStyleProfileV6& Style : Styles) OrderedStyles.Add(&Style);
	OrderedStyles.Sort([](const FEFCalystoStyleProfileV6& A, const FEFCalystoStyleProfileV6& B)
	{
		return StableNameLess(A.StyleId, B.StyleId);
	});
	for (const FEFCalystoStyleProfileV6* Style : OrderedStyles)
	{
		Result += TEXT("S:") + CanonicalToken(Style->StyleId) + TEXT("|");
		AppendDecals(Result, Style->Decals);
	}
	TArray<const FEFCalystoRoomThemeProfileV6*> OrderedThemes;
	for (const FEFCalystoRoomThemeProfileV6& Theme : RoomThemes) OrderedThemes.Add(&Theme);
	OrderedThemes.Sort([](const FEFCalystoRoomThemeProfileV6& A, const FEFCalystoRoomThemeProfileV6& B)
	{
		return StableNameLess(A.ThemeId, B.ThemeId);
	});
	for (const FEFCalystoRoomThemeProfileV6* Theme : OrderedThemes)
	{
		Result += TEXT("T:") + CanonicalToken(Theme->ThemeId) + TEXT("|");
		AppendDecals(Result, Theme->Decals);
	}
	return Result;
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::BuildGameplayCanonicalString() const
{
	using namespace EFCalystoPolicyV6Private;
	FString Result = FString::Printf(TEXT("EFCalystoPolicyV6|SCHEMA:%d|GEN:%d|IDENTITY:%d|ID:%s|Q:%s|POOL:%d|ROOMS:%d|"),
		SchemaVersion, RuntimeGeneratorVersion, IdentityHashSchemaVersion,
		*CanonicalToken(PolicyId), *FloatBits(PerformanceAndSafety.RoomIdentityQuantizationCm),
		PerformanceAndSafety.DecalComponentPoolCapacity, PerformanceAndSafety.MaximumRoomRecords);
	TArray<int32> Sizes = PerformanceAndSafety.ValidatedDungeonSizes;
	Sizes.Sort();
	for (const int32 Size : Sizes) Result += FString::Printf(TEXT("SIZE:%d|"), Size);
	const FEFCalystoFloorBudgetV6& Hard = PerformanceAndSafety.HardCeilings;
	Result += FString::Printf(TEXT("HARD:%d,%d,%d,%d,%d,%d|"), Hard.MaximumEnemies,
		Hard.MaximumLooseFood, Hard.MaximumChests, Hard.MaximumLootActors,
		Hard.MaximumSpecialEvents, Hard.MaximumDirectorActors);

	TArray<const FEFCalystoStyleProfileV6*> OrderedStyles;
	for (const FEFCalystoStyleProfileV6& Style : Styles) OrderedStyles.Add(&Style);
	OrderedStyles.Sort([](const FEFCalystoStyleProfileV6& A, const FEFCalystoStyleProfileV6& B)
	{
		return StableNameLess(A.StyleId, B.StyleId);
	});
	for (const FEFCalystoStyleProfileV6* Style : OrderedStyles)
	{
		const FEFCalystoLayoutProfileV6& L = Style->Layout;
		const FEFCalystoThreatCurveV6& T = Style->Threat;
		const FEFCalystoFloorBudgetV6& B = Style->GlobalBudgets;
		const FEFCalystoContextTraitsV6& X = Style->Traits;
		Result += FString::Printf(TEXT("STYLE:%s,%d,%s,%s|"), *CanonicalToken(Style->StyleId),
			Style->bEnabled ? 1 : 0, *FloatBits(Style->SelectionWeight), *FloatBits(Style->RoomThemeChance));
		Result += FString::Printf(TEXT("LAYOUT:%s,%s,%s,%s;%s,%s,%s,%s;%s,%s,%s,%s;%d,%d|"),
			*FloatBits(L.DungeonSize.Minimum), *FloatBits(L.DungeonSize.Mode), *FloatBits(L.DungeonSize.Maximum), *FloatBits(L.DungeonSize.Shape),
			*FloatBits(L.CandidateDensity.Minimum), *FloatBits(L.CandidateDensity.Mode), *FloatBits(L.CandidateDensity.Maximum), *FloatBits(L.CandidateDensity.Shape),
			*FloatBits(L.SidePathChance.Minimum), *FloatBits(L.SidePathChance.Mode), *FloatBits(L.SidePathChance.Maximum), *FloatBits(L.SidePathChance.Shape),
			L.MinimumRoomSize, L.MaximumRoomSize);
		Result += FString::Printf(TEXT("THREAT:%s,%s,%s,%d,%d|BUDGET:%d,%d,%d,%d,%d,%d|"),
			*FloatBits(T.BudgetAtFloor1), *FloatBits(T.BudgetAtFloor100), *FloatBits(T.Tau),
			T.MinimumLevelOffset, T.MaximumLevelOffset, B.MaximumEnemies, B.MaximumLooseFood,
			B.MaximumChests, B.MaximumLootActors, B.MaximumSpecialEvents, B.MaximumDirectorActors);
		Result += FString::Printf(TEXT("TRAITS:%s,%s,%s,%s,%s|LIGHT:%d,%s|DECO:%s,%d|"),
			*FloatBits(X.Mystery), *FloatBits(X.Danger), *FloatBits(X.Safe),
			*FloatBits(X.Abundance), *FloatBits(X.ClothingInfluence),
			static_cast<int32>(Style->Lighting.Mode), *FloatBits(Style->Lighting.IntensityMultiplier),
			*FloatBits(Style->Decoration.Density), Style->Decoration.MaximumDecorationsPerRoom);
		AppendStyleArchitecture(Result, Style->Architecture);
		AppendCatalogs(Result, Style->Catalogs);
	}

	TArray<const FEFCalystoRoomThemeProfileV6*> OrderedThemes;
	for (const FEFCalystoRoomThemeProfileV6& Theme : RoomThemes) OrderedThemes.Add(&Theme);
	OrderedThemes.Sort([](const FEFCalystoRoomThemeProfileV6& A, const FEFCalystoRoomThemeProfileV6& B)
	{
		return StableNameLess(A.ThemeId, B.ThemeId);
	});
	for (const FEFCalystoRoomThemeProfileV6* Theme : OrderedThemes)
	{
		const FEFCalystoContextTraitsV6& X = Theme->Traits;
		Result += FString::Printf(TEXT("THEME:%s,%d,%s|TRAITS:%s,%s,%s,%s,%s|"),
			*CanonicalToken(Theme->ThemeId), Theme->bEnabled ? 1 : 0,
			*FloatBits(Theme->SelectionWeight), *FloatBits(X.Mystery), *FloatBits(X.Danger),
			*FloatBits(X.Safe), *FloatBits(X.Abundance), *FloatBits(X.ClothingInfluence));
		AppendRoomArchitecture(Result, Theme->Architecture);
		AppendCatalogs(Result, Theme->Catalogs);
	}
	Result += TEXT("MATERIAL_HASH:") + FEFCalystoDungeonDirectorMathV6::HashCanonicalText(BuildMaterialCanonicalString()) + TEXT("|");
	Result += TEXT("DECAL_HASH:") + FEFCalystoDungeonDirectorMathV6::HashCanonicalText(BuildDecalCanonicalString()) + TEXT("|");
	return Result;
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::BuildAuthoringCanonicalString() const
{
	using namespace EFCalystoPolicyV6Private;
	FString Result = TEXT("EFCalystoPolicyAuthoringV6|") + BuildGameplayCanonicalString();
	Result += TEXT("MIGRATION:") + CanonicalToken(MigrationSourceHash) + TEXT("|");
	for (const FEFCalystoStyleProfileV6& Style : Styles)
	{
		Result += FString::Printf(TEXT("SD:%s,%s|"), *CanonicalToken(Style.StyleId), *CanonicalToken(Style.DisplayName));
	}
	for (const FEFCalystoRoomThemeProfileV6& Theme : RoomThemes)
	{
		Result += FString::Printf(TEXT("TD:%s,%s,%s,%s|"), *CanonicalToken(Theme.ThemeId),
			*CanonicalToken(Theme.DisplayName), *CanonicalToken(Theme.Description),
			*ColorBits(Theme.PreviewColor));
	}
	return Result;
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::GetGameplayHash() const
{
	FString Error;
	return Validate(Error)
		? FEFCalystoDungeonDirectorMathV6::HashCanonicalText(BuildGameplayCanonicalString())
		: FString();
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::GetAuthoringHash() const
{
	FString Error;
	return Validate(Error)
		? FEFCalystoDungeonDirectorMathV6::HashCanonicalText(BuildAuthoringCanonicalString())
		: FString();
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::GetMaterialHash() const
{
	FString Error;
	return Validate(Error)
		? FEFCalystoDungeonDirectorMathV6::HashCanonicalText(BuildMaterialCanonicalString())
		: FString();
}

FString UEFCalystoDungeonDirectorPolicyV6Asset::GetDecalHash() const
{
	FString Error;
	return Validate(Error)
		? FEFCalystoDungeonDirectorMathV6::HashCanonicalText(BuildDecalCanonicalString())
		: FString();
}

void UEFCalystoDungeonDirectorPolicyV6Asset::RefreshDerivedData()
{
	using namespace EFCalystoPolicyV6Private;
	CompiledStyleSourceIndices.Reset();
	CompiledThemeSourceIndices.Reset();
	TArray<float> StyleWeights;
	TArray<float> ThemeWeights;
	double TotalStyleWeight = 0.0;
	double TotalThemeWeight = 0.0;
	for (int32 Index = 0; Index < Styles.Num(); ++Index)
	{
		FEFCalystoStyleProfileV6& Style = Styles[Index];
		for (FEFCalystoCatalogOverlayV6& Category : Style.Catalogs) Category.Tiers.RefreshNothing();
		if (Style.bEnabled && Style.SelectionWeight > 0.0f && FMath::IsFinite(Style.SelectionWeight))
		{
			CompiledStyleSourceIndices.Add(Index);
			TotalStyleWeight += Style.SelectionWeight;
		}
	}
	CompiledStyleSourceIndices.Sort([this](const int32 A, const int32 B)
	{
		return EFCalystoPolicyV6Private::StableNameLess(Styles[A].StyleId, Styles[B].StyleId);
	});
	for (const int32 Index : CompiledStyleSourceIndices) StyleWeights.Add(Styles[Index].SelectionWeight);
	BuildAliasTable(StyleWeights, CompiledStyleAliasProbability, CompiledStyleAliasIndex);
	for (FEFCalystoStyleProfileV6& Style : Styles)
	{
		Style.NormalizedSelectionProbability = Style.bEnabled && TotalStyleWeight > 0.0
			? static_cast<float>(Style.SelectionWeight / TotalStyleWeight) : 0.0f;
		Style.EditorSummary = FString::Printf(TEXT("%s | %.1f%% floor | Themes %.1f%%"),
			*Style.StyleId.ToString(), Style.NormalizedSelectionProbability * 100.0f,
			Style.RoomThemeChance * 100.0f);
	}
	for (int32 Index = 0; Index < RoomThemes.Num(); ++Index)
	{
		FEFCalystoRoomThemeProfileV6& Theme = RoomThemes[Index];
		for (FEFCalystoCatalogOverlayV6& Category : Theme.Catalogs) Category.Tiers.RefreshNothing();
		if (Theme.bEnabled && Theme.SelectionWeight > 0.0f && FMath::IsFinite(Theme.SelectionWeight))
		{
			CompiledThemeSourceIndices.Add(Index);
			TotalThemeWeight += Theme.SelectionWeight;
		}
	}
	CompiledThemeSourceIndices.Sort([this](const int32 A, const int32 B)
	{
		return EFCalystoPolicyV6Private::StableNameLess(RoomThemes[A].ThemeId, RoomThemes[B].ThemeId);
	});
	for (const int32 Index : CompiledThemeSourceIndices) ThemeWeights.Add(RoomThemes[Index].SelectionWeight);
	BuildAliasTable(ThemeWeights, CompiledThemeAliasProbability, CompiledThemeAliasIndex);

	double AverageThemeChance = 0.0;
	for (const FEFCalystoStyleProfileV6& Style : Styles)
	{
		AverageThemeChance += static_cast<double>(Style.NormalizedSelectionProbability) * Style.RoomThemeChance;
	}
	for (FEFCalystoRoomThemeProfileV6& Theme : RoomThemes)
	{
		Theme.ConditionalSelectionProbability = Theme.bEnabled && TotalThemeWeight > 0.0
			? static_cast<float>(Theme.SelectionWeight / TotalThemeWeight) : 0.0f;
		Theme.OverallEligibleRoomProbability = Theme.ConditionalSelectionProbability * static_cast<float>(AverageThemeChance);
		Theme.EditorSummary = FString::Printf(TEXT("%s | %.1f%% themed / %.3f%% eligible"),
			*Theme.ThemeId.ToString(), Theme.ConditionalSelectionProbability * 100.0f,
			Theme.OverallEligibleRoomProbability * 100.0f);
	}

	ProbabilityPreview.Reset();
	for (const FEFCalystoStyleProfileV6& Style : Styles)
	{
		FEFCalystoStyleProbabilityPreviewV6& Preview = ProbabilityPreview.AddDefaulted_GetRef();
		Preview.StyleId = Style.StyleId;
		Preview.FloorProbability = Style.NormalizedSelectionProbability;
		Preview.ThemeRoomChance = Style.RoomThemeChance;
		Preview.NoThemeProbability = 1.0f - Style.RoomThemeChance;
		for (const int32 ThemeIndex : CompiledThemeSourceIndices)
		{
			const FEFCalystoRoomThemeProfileV6& Theme = RoomThemes[ThemeIndex];
			FEFCalystoThemeProbabilityPreviewV6& ThemePreview = Preview.Themes.AddDefaulted_GetRef();
			ThemePreview.ThemeId = Theme.ThemeId;
			ThemePreview.ConditionalProbability = Theme.ConditionalSelectionProbability;
			ThemePreview.EligibleRoomProbability = Style.RoomThemeChance * Theme.ConditionalSelectionProbability;
		}
	}
}

TArray<FEFCalystoStyleProbabilityPreviewV6>
UEFCalystoDungeonDirectorPolicyV6Asset::GetProbabilityPreview() const
{
	return ProbabilityPreview;
}

void UEFCalystoDungeonDirectorPolicyV6Asset::RebuildCookBundleReferences()
{
	using namespace EFCalystoPolicyV6Private;
	TArray<FSoftObjectPath> Paths;
	// These two project-owned PCG graphs are the cooked-safe implementation
	// closure. They remain internal, but the definitive Director owns their cook
	// reachability just like its materials and catalogs.
	AddUniquePath(Paths, TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT(
		"/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe"))));
	AddUniquePath(Paths, TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT(
		"/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe"))));
	for (const FEFCalystoStyleProfileV6& Style : Styles)
	{
		GatherMaterials(Style.DungeonMaterials, Paths);
		GatherStyleArchitecture(Style.Architecture, Paths);
		for (const FEFCalystoCatalogOverlayV6& Category : Style.Catalogs) GatherCategoryEntries(Category, Paths);
		GatherDecals(Style.Decals, Paths);
	}
	for (const FEFCalystoRoomThemeProfileV6& Theme : RoomThemes)
	{
		GatherRoomArchitecture(Theme.Architecture, Paths);
		if (Theme.RoomMaterials.FloorMode == EEFCalystoMaterialResolutionModeV6::Override) AddUniquePath(Paths, Theme.RoomMaterials.Overrides.FloorMaterial);
		if (Theme.RoomMaterials.WallMode == EEFCalystoMaterialResolutionModeV6::Override) AddUniquePath(Paths, Theme.RoomMaterials.Overrides.WallMaterial);
		if (Theme.RoomMaterials.RoofMode == EEFCalystoMaterialResolutionModeV6::Override) AddUniquePath(Paths, Theme.RoomMaterials.Overrides.RoofMaterial);
		for (const FEFCalystoCatalogOverlayV6& Category : Theme.Catalogs) GatherCategoryEntries(Category, Paths);
		GatherDecals(Theme.Decals, Paths);
	}
	SortPaths(Paths);
	CookBundleReferences.Reset(Paths.Num());
	for (const FSoftObjectPath& Path : Paths) CookBundleReferences.Emplace(Path);
}

TArray<FName> UEFCalystoDungeonDirectorPolicyV6Asset::GetCookBundleNames() const
{
	return {TEXT("CalystoFloorV6"), TEXT("CalystoStyleV6"),
		TEXT("CalystoThemeV6"), TEXT("CalystoDecalsV6")};
}

TArray<FString> UEFCalystoDungeonDirectorPolicyV6Asset::GetCookBundleAssetPaths() const
{
	TArray<FString> Result;
	for (const TSoftObjectPtr<UObject>& Reference : CookBundleReferences)
	{
		if (!Reference.IsNull()) Result.Add(Reference.ToSoftObjectPath().ToString());
	}
	Result.Sort([](const FString& A, const FString& B)
	{
		return A.Compare(B, ESearchCase::IgnoreCase) < 0;
	});
	return Result;
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::BuildResolvedFloorPlan(
	const int64 FloorSeed, FEFCalystoResolvedFloorPlanV6& OutPlan,
	FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutPlan = FEFCalystoResolvedFloorPlanV6();
	OutError.Reset();
	if (!Validate(OutError))
	{
		return false;
	}
	if (CompiledStyleSourceIndices.IsEmpty() ||
		CompiledStyleSourceIndices.Num() != CompiledStyleAliasProbability.Num() ||
		CompiledStyleSourceIndices.Num() != CompiledStyleAliasIndex.Num())
	{
		return Fail(OutError, TEXT("The V6 Style selection cache is absent or stale. Reload or revalidate the policy before generation."));
	}
	const double StyleUniform = UniformFromHash(HashString64(FString::Printf(
		TEXT("EFCalystoStyleSelectionV6|%lld|%s"), FloorSeed, *CanonicalToken(PolicyId))));
	const int32 CacheIndex = SelectAliasIndex(
		StyleUniform, CompiledStyleAliasProbability, CompiledStyleAliasIndex);
	if (!CompiledStyleSourceIndices.IsValidIndex(CacheIndex) ||
		!Styles.IsValidIndex(CompiledStyleSourceIndices[CacheIndex]))
	{
		return Fail(OutError, TEXT("The V6 Style alias table selected an invalid source index."));
	}
	return BuildResolvedFloorPlanForStyle(
		FloorSeed, Styles[CompiledStyleSourceIndices[CacheIndex]].StyleId,
		OutPlan, OutError);
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::BuildResolvedFloorPlanForStyle(
	const int64 FloorSeed, const FName StyleId,
	FEFCalystoResolvedFloorPlanV6& OutPlan, FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutPlan = FEFCalystoResolvedFloorPlanV6();
	OutError.Reset();
	if (!Validate(OutError))
	{
		return false;
	}
	const FEFCalystoStyleProfileV6* Style = FindStyle(StyleId);
	if (!Style || !Style->bEnabled || Style->SelectionWeight <= 0.0f)
	{
		return Fail(OutError, FString::Printf(TEXT("V6 cannot freeze missing or disabled Style %s."), *StyleId.ToString()));
	}
	if (CompiledThemeSourceIndices.Num() != CompiledThemeAliasProbability.Num() ||
		CompiledThemeSourceIndices.Num() != CompiledThemeAliasIndex.Num() ||
		(Style->RoomThemeChance > 0.0f && CompiledThemeSourceIndices.IsEmpty()))
	{
		return Fail(OutError, TEXT("The V6 Theme selection cache is absent or stale. Reload or revalidate the policy before generation."));
	}

	OutPlan.FloorSeed = FloorSeed;
	OutPlan.StyleId = Style->StyleId;
	OutPlan.ThemeRoomChance = Style->RoomThemeChance;
	OutPlan.RoomIdentityQuantizationCm = PerformanceAndSafety.RoomIdentityQuantizationCm;
	OutPlan.MaximumRoomRecords = PerformanceAndSafety.MaximumRoomRecords;
	OutPlan.DecalComponentPoolCapacity = PerformanceAndSafety.DecalComponentPoolCapacity;
	OutPlan.Layout = Style->Layout;
	OutPlan.Threat = Style->Threat;
	OutPlan.GlobalBudgets = Style->GlobalBudgets;
	OutPlan.StyleMaterials = Style->DungeonMaterials;
	OutPlan.StyleArchitecture = Style->Architecture;
	OutPlan.StyleCatalogs = Style->Catalogs;
	OutPlan.StyleDecals = Style->Decals;
	OutPlan.StyleCatalogHash = CatalogHash(OutPlan.StyleCatalogs);
	OutPlan.StyleMaterialHash = MaterialHash(OutPlan.StyleMaterials);
	OutPlan.StyleArchitectureHash = ArchitectureHash(OutPlan.StyleArchitecture);
	OutPlan.StyleDecalHash = DecalHash(OutPlan.StyleDecals);
	OutPlan.PolicyHash = GetGameplayHash();
	if (!IsSha256(OutPlan.PolicyHash))
	{
		return Fail(OutError, TEXT("V6 could not freeze a valid gameplay policy hash."));
	}

	for (const int32 SourceIndex : CompiledThemeSourceIndices)
	{
		if (!RoomThemes.IsValidIndex(SourceIndex))
		{
			return Fail(OutError, TEXT("The V6 Theme selection cache references an invalid source profile."));
		}
		const FEFCalystoRoomThemeProfileV6& Theme = RoomThemes[SourceIndex];
		FEFCalystoResolvedThemeProfileV6& Snapshot = OutPlan.Themes.AddDefaulted_GetRef();
		Snapshot.ThemeId = Theme.ThemeId;
		Snapshot.SelectionWeight = Theme.SelectionWeight;
		Snapshot.Architecture = Theme.Architecture;
		Snapshot.ArchitectureHash = ArchitectureHash(Snapshot.Architecture);
		Snapshot.FloorMaterialMode = Theme.RoomMaterials.FloorMode;
		Snapshot.WallMaterialMode = Theme.RoomMaterials.WallMode;
		Snapshot.RoofMaterialMode = Theme.RoomMaterials.RoofMode;
		Snapshot.EffectiveMaterials = ResolveThemeMaterials(Style->DungeonMaterials, Theme.RoomMaterials);
		if (!FEFCalystoDungeonDirectorMathV6::ResolveCatalogs(
			Style->Catalogs, Theme.Catalogs, Snapshot.Catalogs, OutError))
		{
			return false;
		}
		Snapshot.Decals = FEFCalystoDungeonDirectorMathV6::ResolveDecals(
			Style->Decals, Theme.Decals);
		Snapshot.CatalogHash = CatalogHash(Snapshot.Catalogs);
		Snapshot.MaterialHash = MaterialHash(Snapshot.EffectiveMaterials);
		Snapshot.DecalHash = DecalHash(Snapshot.Decals);
	}
	OutPlan.ThemeAliasProbability = CompiledThemeAliasProbability;
	OutPlan.ThemeAliasIndex = CompiledThemeAliasIndex;
	if (!GatherReachableVisualPreloadPaths(OutPlan, OutPlan.ReachableVisualPreloadPaths, OutError))
	{
		return false;
	}

	FString Canonical = FString::Printf(TEXT("EFCalystoResolvedFloorPlanV6|%lld|%s|%s|%s|%d|%d|%s|%s|%s|%s|%s|"),
		FloorSeed, *CanonicalToken(Style->StyleId), *FloatBits(Style->RoomThemeChance),
		*FloatBits(OutPlan.RoomIdentityQuantizationCm), OutPlan.MaximumRoomRecords,
		OutPlan.DecalComponentPoolCapacity,
		*OutPlan.PolicyHash, *OutPlan.StyleMaterialHash, *OutPlan.StyleArchitectureHash, *OutPlan.StyleCatalogHash,
		*OutPlan.StyleDecalHash);
	for (const FEFCalystoResolvedThemeProfileV6& Theme : OutPlan.Themes)
	{
		Canonical += FString::Printf(TEXT("T:%s,%s,%s,%d,%d,%d,%s,%s,%s|"),
			*CanonicalToken(Theme.ThemeId), *FloatBits(Theme.SelectionWeight),
			*Theme.ArchitectureHash,
			static_cast<int32>(Theme.FloorMaterialMode),
			static_cast<int32>(Theme.WallMaterialMode),
			static_cast<int32>(Theme.RoofMaterialMode), *Theme.MaterialHash,
			*Theme.CatalogHash, *Theme.DecalHash);
	}
	OutPlan.FloorPlanHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
	return IsSha256(OutPlan.FloorPlanHash)
		? true
		: Fail(OutError, TEXT("V6 failed to hash its frozen floor plan."));
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::BuildRoomManifest(
	const FEFCalystoResolvedFloorPlanV6& Plan,
	const TArray<FEFCalystoRoomIdentityInputV6>& RoomInputs,
	FEFCalystoRoomManifestV6& OutManifest, FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutManifest = FEFCalystoRoomManifestV6();
	OutError.Reset();
	if (RoomInputs.Num() > Plan.MaximumRoomRecords)
	{
		return Fail(OutError, TEXT("V6 room topology exceeds Maximum Room Records."));
	}
	if (!IsSha256(Plan.PolicyHash) || Plan.PolicyHash != GetGameplayHash() ||
		!IsSha256(Plan.FloorPlanHash))
	{
		return Fail(OutError, TEXT("V6 rejected a stale or foreign frozen floor plan."));
	}
	OutManifest.FloorSeed = Plan.FloorSeed;
	OutManifest.StyleId = Plan.StyleId;
	OutManifest.FloorPlanHash = Plan.FloorPlanHash;
	struct FPreparedRoom
	{
		const FEFCalystoRoomIdentityInputV6* Input = nullptr;
		int64 StableRoomId = 0;
	};
	TArray<FPreparedRoom> PreparedRooms;
	PreparedRooms.Reserve(RoomInputs.Num());
	TArray<int64> EligibleStableRoomIds;
	EligibleStableRoomIds.Reserve(RoomInputs.Num());
	TSet<int64> StableIds;
	for (const FEFCalystoRoomIdentityInputV6& Input : RoomInputs)
	{
		const int64 StableRoomId = FEFCalystoDungeonDirectorMathV6::BuildStableRoomId(
			Plan.FloorSeed, Input, Plan.RoomIdentityQuantizationCm);
		if (StableRoomId <= 0)
		{
			return Fail(OutError, TEXT("V6 could not build a Stable Room ID from the supplied topology record."));
		}
		if (StableIds.Contains(StableRoomId))
		{
			return Fail(OutError, TEXT("V6 detected a Stable Room ID collision. Supply a deterministic Collision Ordinal."));
		}
		StableIds.Add(StableRoomId);
		PreparedRooms.Add({&Input, StableRoomId});
		if (!FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Input.RoomFlags))
		{
			EligibleStableRoomIds.Add(StableRoomId);
		}
	}

	const int64 GuaranteedThemeRoomId =
		Plan.ThemeRoomChance > 0.0f && !Plan.Themes.IsEmpty()
		? FEFCalystoDungeonDirectorMathV6::SelectGuaranteedThemeRoomId(
			Plan.FloorSeed, Plan.StyleId, EligibleStableRoomIds)
		: 0;
	for (const FPreparedRoom& Prepared : PreparedRooms)
	{
		check(Prepared.Input);
		const FEFCalystoRoomIdentityInputV6& Input = *Prepared.Input;
		FEFCalystoRoomContextV6& Context = OutManifest.Rooms.AddDefaulted_GetRef();
		if (!FEFCalystoDungeonDirectorMathV6::ResolveRoomContext(
			Plan,
			Prepared.StableRoomId,
			Input.RoomFlags,
			Context,
			OutError,
			Prepared.StableRoomId == GuaranteedThemeRoomId))
		{
			return false;
		}
		Context.LocalCenter = Input.LocalCenter;
		Context.Extents = Input.Extents;
		Context.TopologyKind = Input.TopologyKind;
		Context.CollisionOrdinal = Input.CollisionOrdinal;
		if (!FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Input.RoomFlags))
		{
			++OutManifest.EligibleRoomCount;
			if (Context.bIsThemed) ++OutManifest.ThemedRoomCount;
		}
	}
	if (GuaranteedThemeRoomId > 0 && OutManifest.ThemedRoomCount < 1)
	{
		return Fail(OutError, TEXT("V6 failed to realize its deterministic minimum Room Theme guarantee."));
	}
	OutManifest.Rooms.Sort([](const FEFCalystoRoomContextV6& A,
		const FEFCalystoRoomContextV6& B) { return A.StableRoomId < B.StableRoomId; });
	FString Canonical = FString::Printf(TEXT("EFCalystoRoomManifestV6|%s|%lld|%s|%d|%d|"),
		*Plan.FloorPlanHash, Plan.FloorSeed, *CanonicalToken(Plan.StyleId),
		OutManifest.EligibleRoomCount, OutManifest.ThemedRoomCount);
	for (const FEFCalystoRoomContextV6& Context : OutManifest.Rooms)
	{
		Canonical += Context.RoomContextHash + TEXT("|");
	}
	OutManifest.ManifestHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
	return IsSha256(OutManifest.ManifestHash)
		? true
		: Fail(OutError, TEXT("V6 failed to hash the immutable room manifest."));
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::GatherReachableVisualPreloadPaths(
	const FEFCalystoResolvedFloorPlanV6& Plan, TArray<FSoftObjectPath>& OutPaths,
	FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutPaths.Reset();
	OutError.Reset();
	if (Plan.StyleId.IsNone() ||
		!ValidateMaterials(Plan.StyleMaterials, TEXT("Frozen Style"), OutError) ||
		!ValidateStyleArchitecture(Plan.StyleArchitecture,
			TEXT("Frozen Style Architecture"), OutError))
	{
		return false;
	}
	GatherMaterials(Plan.StyleMaterials, OutPaths);
	GatherStyleArchitecture(Plan.StyleArchitecture, OutPaths);
	for (const FEFCalystoResolvedThemeProfileV6& Theme : Plan.Themes)
	{
		if (!ValidateRoomArchitecture(Theme.Architecture,
				TEXT("Frozen Theme Architecture"), OutError) ||
			!ValidateMaterials(Theme.EffectiveMaterials, TEXT("Frozen Theme"), OutError))
		{
			return false;
		}
		GatherRoomArchitecture(Theme.Architecture, OutPaths);
		GatherMaterials(Theme.EffectiveMaterials, OutPaths);
	}
	SortPaths(OutPaths);
	return true;
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::GatherPostTopologyContentPaths(
	const FEFCalystoResolvedFloorPlanV6& Plan,
	const FEFCalystoRoomManifestV6& Manifest, TArray<FSoftObjectPath>& OutPaths,
	FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutPaths.Reset();
	OutError.Reset();
	if (!IsSha256(Manifest.ManifestHash) || Manifest.FloorPlanHash != Plan.FloorPlanHash ||
		Manifest.FloorSeed != Plan.FloorSeed ||
		!Manifest.StyleId.IsEqual(Plan.StyleId, ENameCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Post-topology content streaming requires a manifest from the supplied V6 floor plan."));
	}
	bool bHasOrdinaryNoThemeRoom = false;
	TSet<FName> UsedThemes;
	for (const FEFCalystoRoomContextV6& Room : Manifest.Rooms)
	{
		if (FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room.RoomFlags)) continue;
		if (Room.bIsThemed) UsedThemes.Add(Room.ThemeId);
		else bHasOrdinaryNoThemeRoom = true;
	}
	if (bHasOrdinaryNoThemeRoom) GatherResolvedCatalogs(Plan.StyleCatalogs, nullptr, OutPaths);
	TArray<FName> OrderedThemes = UsedThemes.Array();
	OrderedThemes.Sort([](const FName A, const FName B) { return StableNameLess(A, B); });
	for (const FName ThemeId : OrderedThemes)
	{
		const FEFCalystoResolvedThemeProfileV6* Theme = FindResolvedTheme(Plan, ThemeId);
		if (!Theme) return Fail(OutError, TEXT("The manifest references a Theme absent from its frozen floor plan."));
		for (const FEFCalystoCatalogOverlayV6& Category : Theme->Catalogs)
		{
			GatherCategoryEntries(Category, OutPaths);
		}
	}
	SortPaths(OutPaths);
	return true;
}

bool UEFCalystoDungeonDirectorPolicyV6Asset::GatherPostTopologyDecalPaths(
	const FEFCalystoResolvedFloorPlanV6& Plan,
	const FEFCalystoRoomManifestV6& Manifest, TArray<FSoftObjectPath>& OutPaths,
	FString& OutError) const
{
	using namespace EFCalystoPolicyV6Private;
	OutPaths.Reset();
	OutError.Reset();
	if (!IsSha256(Manifest.ManifestHash) || Manifest.FloorPlanHash != Plan.FloorPlanHash ||
		Manifest.FloorSeed != Plan.FloorSeed ||
		!Manifest.StyleId.IsEqual(Plan.StyleId, ENameCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Post-topology decal streaming requires a manifest from the supplied V6 floor plan."));
	}
	bool bNeedsStyleDecals = false;
	TSet<FName> UsedThemes;
	for (const FEFCalystoRoomContextV6& Room : Manifest.Rooms)
	{
		if (FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room.RoomFlags) ||
			(Room.RoomFlags & static_cast<int32>(EEFCalystoRoomFlagsV6::MainPath)) != 0 ||
			(Room.RoomFlags & static_cast<int32>(EEFCalystoRoomFlagsV6::DoorClearance)) != 0)
		{
			continue;
		}
		if (Room.bIsThemed) UsedThemes.Add(Room.ThemeId);
		else bNeedsStyleDecals = true;
	}
	if (bNeedsStyleDecals) GatherDecals(Plan.StyleDecals, OutPaths);
	for (const FName ThemeId : UsedThemes)
	{
		const FEFCalystoResolvedThemeProfileV6* Theme = FindResolvedTheme(Plan, ThemeId);
		if (!Theme) return Fail(OutError, TEXT("The manifest references a Theme absent from its frozen floor plan."));
		GatherDecals(Theme->Decals, OutPaths);
	}
	SortPaths(OutPaths);
	return true;
}

void UEFCalystoDungeonDirectorPolicyV6Asset::PostLoad()
{
	Super::PostLoad();
	RefreshDerivedData();
	RebuildCookBundleReferences();
}

#if WITH_EDITORONLY_DATA
void UEFCalystoDungeonDirectorPolicyV6Asset::PreSave(
	FObjectPreSaveContext ObjectSaveContext)
{
	RefreshDerivedData();
	RebuildCookBundleReferences();
	Super::PreSave(ObjectSaveContext);
}
#endif

#if WITH_EDITOR
void UEFCalystoDungeonDirectorPolicyV6Asset::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	RefreshDerivedData();
	RebuildCookBundleReferences();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

EDataValidationResult UEFCalystoDungeonDirectorPolicyV6Asset::IsDataValid(
	FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);
	if (SuperResult == EDataValidationResult::Invalid) return SuperResult;
	FString Error;
	if (!Validate(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
