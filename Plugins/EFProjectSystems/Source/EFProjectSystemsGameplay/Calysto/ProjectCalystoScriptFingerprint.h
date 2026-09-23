#pragma once

#include "CoreMinimal.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

namespace ProjectCalystoScriptFingerprint
{
	/** Serialize VM operands by stable object/field/name paths, never process addresses. */
	class FCanonicalArchive final : public FObjectAndNameAsStringProxyArchive
	{
	public:
		explicit FCanonicalArchive(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, false) {}
		using FObjectAndNameAsStringProxyArchive::operator<<;
		virtual FArchive& operator<<(FField*& Field) override
		{
			FString Path = Field ? Field->GetPathName() : TEXT("None");
			*this << Path;
			return *this;
		}
	};

	inline FString Compute(const UClass* Class, TArray<uint8>* DiagnosticBytes = nullptr)
	{
		if (!Class) return {};
		TArray<UFunction*> Functions;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			if (!It->Script.IsEmpty()) Functions.Add(*It);
		if (Functions.IsEmpty() || Functions.Num() > 256) return {};
		Functions.Sort([](const UFunction& A, const UFunction& B) { return A.GetName() < B.GetName(); });
		TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FCanonicalArchive Archive(Writer);
		FString ClassPath = Class->GetPathName(), ParentPath = GetPathNameSafe(Class->GetSuperClass());
		Archive << ClassPath << ParentPath;
		for (UFunction* Function : Functions)
		{
			FString Name = Function->GetName(); int32 Size = Function->Script.Num();
			if (Size > 1024 * 1024) return {};
			Archive << Name << Size;
			int32 Offset = 0;
			while (Offset < Size && !Archive.IsError())
			{
				const int32 Before = Offset;
				Function->SerializeExpr(Offset, Archive);
				if (Offset <= Before || Offset > Size || Bytes.Num() > 4 * 1024 * 1024) return {};
			}
			if (Archive.IsError()) return {};
		}
		if (DiagnosticBytes) *DiagnosticBytes = Bytes;
		uint8 Digest[FSHA1::DigestSize]; FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
		return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
	}
}
