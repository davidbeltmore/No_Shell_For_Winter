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

const UEFClothingSurfaceBinding* UEFClothingFitRegistry::FindNativeSourceBinding(
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh) const
{
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}
	if (IndexedNativeBindingCount != NativeSourceBindings.Num())
	{
		RuntimeNativeBindingIndex.Reset();
		for (const UEFClothingSurfaceBinding* Binding : NativeSourceBindings)
		{
			if (!IsValid(Binding)
				|| Binding->SourceGarment.IsNull()
				|| Binding->BodySurface.IsNull())
			{
				continue;
			}
			const FString Key = MakeRuntimeKey(
				Binding->SourceGarment.ToSoftObjectPath(),
				Binding->BodySurface.ToSoftObjectPath());
			// Publication rejects duplicates. If an invalid external edit still
			// creates one, fail closed instead of letting array order select a bind.
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
	}

	const TWeakObjectPtr<const UEFClothingSurfaceBinding>* Match =
		RuntimeNativeBindingIndex.Find(MakeRuntimeKey(FSoftObjectPath(SourceMesh), FSoftObjectPath(BodyMesh)));
	return Match ? Match->Get() : nullptr;
}
