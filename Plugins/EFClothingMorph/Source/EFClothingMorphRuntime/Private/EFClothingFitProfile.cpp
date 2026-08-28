#include "EFClothingFitProfile.h"

#include "EFClothingSurfaceBinding.h"

#include "Engine/SkeletalMesh.h"

bool UEFClothingFitProfile::MatchesSource(const USkeletalMesh* Mesh) const
{
	if (!IsValid(Mesh) || SourceGarment.IsNull())
	{
		return false;
	}

	return SourceGarment.ToSoftObjectPath() == FSoftObjectPath(Mesh);
}

const UEFClothingFitProfile* UEFClothingFitRegistry::FindProfileForSource(const USkeletalMesh* SourceMesh) const
{
	for (const UEFClothingFitProfile* Profile : Profiles)
	{
		if (IsValid(Profile) && Profile->MatchesSource(SourceMesh))
		{
			return Profile;
		}
	}

	return nullptr;
}

FString UEFClothingFitRegistry::MakeRuntimeKey(
	const FSoftObjectPath& SourcePath,
	const FSoftObjectPath& BodyPath)
{
	return SourcePath.ToString() + TEXT("|") + BodyPath.ToString();
}

FString UEFClothingFitRegistry::MakeNativeRuntimeKey(
	const FName ClothingId,
	const FSoftObjectPath& SourcePath,
	const FSoftObjectPath& BodyPath)
{
	return ClothingId.ToString() + TEXT("|") + MakeRuntimeKey(SourcePath, BodyPath);
}

void UEFClothingFitRegistry::RebuildRuntimeIndex() const
{
	RuntimeProfileIndex.Reset();
	for (const UEFClothingFitProfile* Profile : Profiles)
	{
		if (!IsValid(Profile) || Profile->SourceGarment.IsNull() || Profile->BodySurface.IsNull())
		{
			continue;
		}
		const FString Key = MakeRuntimeKey(
			Profile->SourceGarment.ToSoftObjectPath(),
			Profile->BodySurface.ToSoftObjectPath());
		// Publication validation rejects duplicate source/body keys. Remaining
		// duplicates fail closed here instead of making selection order-dependent.
		if (RuntimeProfileIndex.Contains(Key))
		{
			RuntimeProfileIndex.Add(Key, nullptr);
		}
		else
		{
			RuntimeProfileIndex.Add(Key, Profile);
		}
	}
	IndexedProfileCount = Profiles.Num();
}

const UEFClothingFitProfile* UEFClothingFitRegistry::FindProfileForSourceAndBody(
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh) const
{
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}
	if (IndexedProfileCount != Profiles.Num())
	{
		RebuildRuntimeIndex();
	}
	const TWeakObjectPtr<const UEFClothingFitProfile>* Found = RuntimeProfileIndex.Find(
		MakeRuntimeKey(FSoftObjectPath(SourceMesh), FSoftObjectPath(BodyMesh)));
	return Found && Found->IsValid() ? Found->Get() : nullptr;
}

uint32 UEFClothingFitRegistry::CalculateNativeBindingSignature() const
{
	uint32 Signature = GetTypeHash(NativeSourceBindings.Num());
	for (const UEFClothingSurfaceBinding* Binding : NativeSourceBindings)
	{
		Signature = HashCombineFast(Signature, GetTypeHash(Binding));
		if (!IsValid(Binding))
		{
			continue;
		}
		Signature = HashCombineFast(Signature, GetTypeHash(Binding->GarmentId));
		Signature = HashCombineFast(Signature, GetTypeHash(Binding->BuildGuid));
		Signature = HashCombineFast(
			Signature,
			GetTypeHash(Binding->SourceGarment.ToSoftObjectPath()));
		Signature = HashCombineFast(
			Signature,
			GetTypeHash(Binding->BodySurface.ToSoftObjectPath()));
	}
	return Signature;
}

void UEFClothingFitRegistry::RebuildNativeBindingIndex() const
{
	RuntimeNativeBindingIndex.Reset();
	for (const UEFClothingSurfaceBinding* Binding : NativeSourceBindings)
	{
		if (!IsValid(Binding)
			|| Binding->GarmentId.IsNone()
			|| Binding->SourceGarment.IsNull()
			|| Binding->BodySurface.IsNull())
		{
			continue;
		}
		const FString Key = MakeNativeRuntimeKey(
			Binding->GarmentId,
			Binding->SourceGarment.ToSoftObjectPath(),
			Binding->BodySurface.ToSoftObjectPath());
		// Duplicate complete V4 keys are ambiguous and therefore fail closed.
		if (RuntimeNativeBindingIndex.Contains(Key))
		{
			RuntimeNativeBindingIndex.Add(Key, nullptr);
		}
		else
		{
			RuntimeNativeBindingIndex.Add(Key, Binding);
		}
	}
	IndexedNativeBindingCount = NativeSourceBindings.Num();
	IndexedNativeBindingSignature = CalculateNativeBindingSignature();
}

const UEFClothingSurfaceBinding* UEFClothingFitRegistry::FindNativeSourceBinding(
	const FName ClothingId,
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh) const
{
	if (ClothingId.IsNone() || !IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}
	const uint32 CurrentSignature = CalculateNativeBindingSignature();
	if (IndexedNativeBindingCount != NativeSourceBindings.Num()
		|| IndexedNativeBindingSignature != CurrentSignature)
	{
		RebuildNativeBindingIndex();
	}

	const TWeakObjectPtr<const UEFClothingSurfaceBinding>* Match =
		RuntimeNativeBindingIndex.Find(MakeNativeRuntimeKey(
			ClothingId,
			FSoftObjectPath(SourceMesh),
			FSoftObjectPath(BodyMesh)));
	return Match ? Match->Get() : nullptr;
}

const UEFClothingSurfaceBinding* UEFClothingFitRegistry::FindNativeSourceBinding(
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh) const
{
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}

	const UEFClothingSurfaceBinding* Match = nullptr;
	for (const UEFClothingSurfaceBinding* Binding : NativeSourceBindings)
	{
		if (!IsValid(Binding)
			|| Binding->SourceGarment.ToSoftObjectPath() != FSoftObjectPath(SourceMesh)
			|| Binding->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodyMesh))
		{
			continue;
		}
		if (Match)
		{
			return nullptr;
		}
		Match = Binding;
	}
	return Match;
}
