#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFBodyOnboardingLibrary.generated.h"
class USkeletalMesh;
class USkeleton;
UCLASS()
class EFCHARACTERCREATIONEDITOR_API UEFBodyOnboardingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="EF Body Onboarding")
	static FString InspectBody(USkeletalMesh* Mesh, USkeleton* ProposedSkeleton);
	/** Bind an already converted mesh to its compatible local skeleton. Does not save shared assets. */
	UFUNCTION(BlueprintCallable, Category="EF Body Onboarding")
	static bool BindLocalSkeleton(USkeletalMesh* Mesh, USkeleton* LocalSkeleton, USkeletalMesh* ReferenceBody, FString& Error);
	/** G9 only: new isolated bone tree at the imported skeleton path. Never saves assets. */
	UFUNCTION(BlueprintCallable, Category="EF Body Onboarding")
	static bool ConvertGenesis9Body(USkeletalMesh* Mesh, USkeleton* LocalSkeleton, USkeletalMesh* ReferenceBody, FString& Error);
	/** Python preserves this error string even when the conversion fails. */
	UFUNCTION(BlueprintCallable, Category="EF Body Onboarding")
	static FString ConvertGenesis9BodyReport(USkeletalMesh* Mesh, USkeleton* LocalSkeleton, USkeletalMesh* ReferenceBody);
};
