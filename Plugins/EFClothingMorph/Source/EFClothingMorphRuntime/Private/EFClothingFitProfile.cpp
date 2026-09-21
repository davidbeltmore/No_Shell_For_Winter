#include "EFClothingFitProfile.h"

#include "EFClothingSurfaceBinding.h"

#include "Engine/SkeletalMesh.h"
#include "Misc/SecureHash.h"

namespace EFClothingFitProfilePrivate
{
	static FString CanonicalNames(const TArray<FName>& Values)
	{
		TArray<FString> Names;
		Names.Reserve(Values.Num());
		for (const FName Value : Values)
		{
			if (!Value.IsNone())
			{
				Names.AddUnique(Value.ToString());
			}
		}
		Names.Sort();
		return FString::Join(Names, TEXT(","));
	}
}

FString FEFClothingV5StreamableBinding::ComputePayloadContentHash(
	const UEFClothingSurfaceBinding* LoadedBinding,
	const FEFClothingSurfaceLODPairBinding& LODPair)
{
	if (!IsValid(LoadedBinding))
	{
		return FString();
	}

	const FString Canonical = FString::Printf(
		TEXT("Path=%s|GarmentCompile=%s|Build=%s|Compiler=%d|Schema=%d|")
		TEXT("SourceContent=%s|FittedContent=%s|BodyContent=%s|")
		TEXT("SourceSkeleton=%s|FittedSkeleton=%s|BodySkeleton=%s|SharedSkeleton=%s|")
		TEXT("ExcludedSlots=%s|GarmentLOD=%d|BodyLOD=%d|")
		TEXT("GarmentTopology=%s|GarmentLODContent=%s|BodyTopology=%s|BodyLODContent=%s"),
		*FSoftObjectPath(LoadedBinding).ToString(),
		*LoadedBinding->GarmentCompileFingerprint,
		*LoadedBinding->BuildGuid.ToString(EGuidFormats::Digits),
		LoadedBinding->CompilerVersion,
		LoadedBinding->SchemaVersion,
		*LoadedBinding->SourceContentFingerprint,
		*LoadedBinding->FittedContentFingerprint,
		*LoadedBinding->BodyContentFingerprint,
		*LoadedBinding->SourceSkeletonFingerprint,
		*LoadedBinding->FittedSkeletonFingerprint,
		*LoadedBinding->BodySkeletonFingerprint,
		*LoadedBinding->SharedSkeletonFingerprint,
		*EFClothingFitProfilePrivate::CanonicalNames(
			LoadedBinding->ExcludedBodySurfaceMaterialSlots),
		LODPair.GarmentTopology.LODIndex,
		LODPair.BodyTopology.LODIndex,
		*LODPair.GarmentTopology.TopologyFingerprint,
		*LODPair.GarmentTopology.ContentFingerprint,
		*LODPair.BodyTopology.TopologyFingerprint,
		*LODPair.BodyTopology.ContentFingerprint);
	return FMD5::HashAnsiString(*(LoadedBinding->ReferenceBodySurface.IsNull() ? Canonical
		: Canonical + TEXT("|ReferenceBody=") + LoadedBinding->ReferenceBodySurface.ToSoftObjectPath().ToString()
			+ TEXT("|ReferenceContent=") + LoadedBinding->ReferenceBodyContentFingerprint));
}

bool FEFClothingV5StreamableBinding::ValidateLoadedPayload(
	const UEFClothingSurfaceBinding* LoadedBinding,
	FString* OutFailureReason) const
{
	auto Fail = [OutFailureReason](const FString& FailureReason)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FailureReason;
		}
		return false;
	};

	if (!HasValidMetadata())
	{
		return Fail(TEXT("V5 streamable binding metadata is incomplete."));
	}
	if (!IsValid(LoadedBinding))
	{
		return Fail(TEXT("V5 streamable binding payload did not resolve to a valid asset."));
	}
	if (FSoftObjectPath(LoadedBinding) != Binding.ToSoftObjectPath())
	{
		return Fail(TEXT("Resolved V5 binding asset path does not match registry metadata."));
	}
	if (LoadedBinding->GarmentId != GarmentId
		|| LoadedBinding->SourceGarment.ToSoftObjectPath() != SourceGarment.ToSoftObjectPath()
		|| LoadedBinding->BodySurface.ToSoftObjectPath() != BodySurface.ToSoftObjectPath())
	{
		return Fail(TEXT("Resolved V5 binding identity does not match registry metadata."));
	}
	if (LoadedBinding->SchemaVersion != SchemaVersion)
	{
		return Fail(FString::Printf(
			TEXT("Resolved V5 binding schema %d does not match published schema %d."),
			LoadedBinding->SchemaVersion,
			SchemaVersion));
	}

	const FEFClothingSurfaceLODPairBinding* MatchingLODPair = nullptr;
	for (const FEFClothingSurfaceLODPairBinding& Candidate : LoadedBinding->LODPairBindings)
	{
		if (Candidate.GarmentTopology.LODIndex != LODIndex)
		{
			continue;
		}
		if (MatchingLODPair)
		{
			return Fail(FString::Printf(
				TEXT("Resolved V5 binding contains duplicate garment LOD %d payloads."),
				LODIndex));
		}
		MatchingLODPair = &Candidate;
	}
	if (!MatchingLODPair)
	{
		return Fail(FString::Printf(
			TEXT("Resolved V5 binding has no payload for garment LOD %d."),
			LODIndex));
	}

	const FString LoadedContentHash = ComputePayloadContentHash(LoadedBinding, *MatchingLODPair);
	if (LoadedContentHash.IsEmpty() || LoadedContentHash != ContentHash)
	{
		return Fail(FString::Printf(
			TEXT("Resolved V5 binding content hash '%s' does not match published hash '%s'."),
			*LoadedContentHash,
			*ContentHash));
	}

	if (OutFailureReason)
	{
		OutFailureReason->Reset();
	}
	return true;
}

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

FString UEFClothingFitRegistry::MakeV5PairRuntimeKey(
	const FSoftObjectPath& SourcePath,
	const FSoftObjectPath& BodyPath,
	const int32 LODIndex)
{
	return MakeRuntimeKey(SourcePath, BodyPath)
		+ TEXT("|")
		+ LexToString(LODIndex);
}

FString UEFClothingFitRegistry::MakeV5RuntimeKey(
	const FName GarmentId,
	const FSoftObjectPath& SourcePath,
	const FSoftObjectPath& BodyPath,
	const int32 LODIndex)
{
	return GarmentId.ToString()
		+ TEXT("|")
		+ MakeV5PairRuntimeKey(SourcePath, BodyPath, LODIndex);
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

uint32 UEFClothingFitRegistry::CalculateV5StreamableBindingSignature() const
{
	uint32 Signature = GetTypeHash(V5StreamableBindings.Num());
	for (const FEFClothingV5StreamableBinding& Record : V5StreamableBindings)
	{
		Signature = HashCombineFast(Signature, GetTypeHash(Record.StableBindingId));
		Signature = HashCombineFast(Signature, GetTypeHash(Record.GarmentId));
		Signature = HashCombineFast(
			Signature,
			GetTypeHash(Record.SourceGarment.ToSoftObjectPath()));
		Signature = HashCombineFast(
			Signature,
			GetTypeHash(Record.BodySurface.ToSoftObjectPath()));
		Signature = HashCombineFast(
			Signature,
			GetTypeHash(Record.Binding.ToSoftObjectPath()));
		Signature = HashCombineFast(Signature, GetTypeHash(Record.LODIndex));
		Signature = HashCombineFast(Signature, GetTypeHash(Record.ContentHash));
		Signature = HashCombineFast(Signature, GetTypeHash(Record.SchemaVersion));
	}
	return Signature;
}

void UEFClothingFitRegistry::RebuildV5StreamableBindingIndex() const
{
	RuntimeV5BindingPairIndex.Reset();
	RuntimeV5BindingIndex.Reset();
	RuntimeV5BindingStableIdIndex.Reset();

	for (int32 RecordIndex = 0; RecordIndex < V5StreamableBindings.Num(); ++RecordIndex)
	{
		const FEFClothingV5StreamableBinding& Record = V5StreamableBindings[RecordIndex];
		if (!Record.HasValidMetadata())
		{
			continue;
		}

		const FSoftObjectPath SourcePath = Record.SourceGarment.ToSoftObjectPath();
		const FSoftObjectPath BodyPath = Record.BodySurface.ToSoftObjectPath();
		const FString PairKey = MakeV5PairRuntimeKey(SourcePath, BodyPath, Record.LODIndex);
		const FString ExactKey = MakeV5RuntimeKey(
			Record.GarmentId,
			SourcePath,
			BodyPath,
			Record.LODIndex);

		// Every duplicate is ambiguous regardless of publication order.
		if (RuntimeV5BindingPairIndex.Contains(PairKey))
		{
			RuntimeV5BindingPairIndex.Add(PairKey, INDEX_NONE);
		}
		else
		{
			RuntimeV5BindingPairIndex.Add(PairKey, RecordIndex);
		}

		if (RuntimeV5BindingIndex.Contains(ExactKey))
		{
			RuntimeV5BindingIndex.Add(ExactKey, INDEX_NONE);
		}
		else
		{
			RuntimeV5BindingIndex.Add(ExactKey, RecordIndex);
		}

		if (RuntimeV5BindingStableIdIndex.Contains(Record.StableBindingId))
		{
			RuntimeV5BindingStableIdIndex.Add(Record.StableBindingId, INDEX_NONE);
		}
		else
		{
			RuntimeV5BindingStableIdIndex.Add(Record.StableBindingId, RecordIndex);
		}
	}

	IndexedV5StreamableBindingCount = V5StreamableBindings.Num();
	IndexedV5StreamableBindingSignature = CalculateV5StreamableBindingSignature();
}

void UEFClothingFitRegistry::EnsureV5StreamableBindingIndex() const
{
	const uint32 CurrentSignature = CalculateV5StreamableBindingSignature();
	if (IndexedV5StreamableBindingCount != V5StreamableBindings.Num()
		|| IndexedV5StreamableBindingSignature != CurrentSignature)
	{
		RebuildV5StreamableBindingIndex();
	}
}

void UEFClothingFitRegistry::PostLoad()
{
	Super::PostLoad();
	RebuildV5StreamableBindingIndex();
}

const FEFClothingV5StreamableBinding* UEFClothingFitRegistry::FindV5StreamableBinding(
	const FSoftObjectPath& SourcePath,
	const FSoftObjectPath& BodyPath,
	const int32 LODIndex) const
{
	if (SourcePath.IsNull() || BodyPath.IsNull() || LODIndex < 0)
	{
		return nullptr;
	}

	EnsureV5StreamableBindingIndex();
	const int32* RecordIndex = RuntimeV5BindingPairIndex.Find(
		MakeV5PairRuntimeKey(SourcePath, BodyPath, LODIndex));
	return RecordIndex && V5StreamableBindings.IsValidIndex(*RecordIndex)
		? &V5StreamableBindings[*RecordIndex]
		: nullptr;
}

const FEFClothingV5StreamableBinding* UEFClothingFitRegistry::FindV5StreamableBinding(
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh,
	const int32 LODIndex) const
{
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}

	return FindV5StreamableBinding(
		FSoftObjectPath(SourceMesh),
		FSoftObjectPath(BodyMesh),
		LODIndex);
}

const FEFClothingV5StreamableBinding* UEFClothingFitRegistry::FindV5StreamableBinding(
	const FName GarmentId,
	const FSoftObjectPath& SourcePath,
	const FSoftObjectPath& BodyPath,
	const int32 LODIndex) const
{
	if (GarmentId.IsNone() || SourcePath.IsNull() || BodyPath.IsNull() || LODIndex < 0)
	{
		return nullptr;
	}

	EnsureV5StreamableBindingIndex();
	const int32* RecordIndex = RuntimeV5BindingIndex.Find(
		MakeV5RuntimeKey(GarmentId, SourcePath, BodyPath, LODIndex));
	return RecordIndex && V5StreamableBindings.IsValidIndex(*RecordIndex)
		? &V5StreamableBindings[*RecordIndex]
		: nullptr;
}

const FEFClothingV5StreamableBinding* UEFClothingFitRegistry::FindV5StreamableBinding(
	const FName GarmentId,
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh,
	const int32 LODIndex) const
{
	if (GarmentId.IsNone() || !IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}

	return FindV5StreamableBinding(
		GarmentId,
		FSoftObjectPath(SourceMesh),
		FSoftObjectPath(BodyMesh),
		LODIndex);
}

const FEFClothingV5StreamableBinding* UEFClothingFitRegistry::FindV5StreamableBindingByStableId(
	const FName StableBindingId) const
{
	if (StableBindingId.IsNone())
	{
		return nullptr;
	}

	EnsureV5StreamableBindingIndex();
	const int32* RecordIndex = RuntimeV5BindingStableIdIndex.Find(StableBindingId);
	return RecordIndex && V5StreamableBindings.IsValidIndex(*RecordIndex)
		? &V5StreamableBindings[*RecordIndex]
		: nullptr;
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
