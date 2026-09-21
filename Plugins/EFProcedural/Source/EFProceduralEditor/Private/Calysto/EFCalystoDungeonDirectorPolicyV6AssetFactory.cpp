#include "Calysto/EFCalystoDungeonDirectorPolicyV6AssetFactory.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"

#define LOCTEXT_NAMESPACE "EFCalystoDungeonDirectorPolicyV6AssetFactory"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoDungeonDirectorPolicyV6AssetFactory, Log, All);

namespace EFCalystoPolicyV6FactoryPrivate
{
static constexpr const TCHAR* PolicyPackagePath =
	TEXT("/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy");
static const FName PolicyAssetName(TEXT("DA_CalystoDungeonDirectorPolicy"));
}

UEFCalystoDungeonDirectorPolicyV6AssetFactory::
	UEFCalystoDungeonDirectorPolicyV6AssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UEFCalystoDungeonDirectorPolicyV6Asset::StaticClass();
}

UObject* UEFCalystoDungeonDirectorPolicyV6AssetFactory::FactoryCreateNew(
	UClass* InClass, UObject* InParent, const FName InName,
	const EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	(void)Context;
	(void)Warn;
	using namespace EFCalystoPolicyV6FactoryPrivate;
	if (InClass != UEFCalystoDungeonDirectorPolicyV6Asset::StaticClass() ||
		!IsValid(InParent) || InParent->GetPathName() != PolicyPackagePath ||
		InName != PolicyAssetName)
	{
		UE_LOG(LogEFCalystoDungeonDirectorPolicyV6AssetFactory, Error,
			TEXT("Calysto V6 policy creation is restricted to singleton %s.%s."),
			PolicyPackagePath, *PolicyAssetName.ToString());
		return nullptr;
	}
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy =
		NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>(
			InParent, InClass, InName, Flags | RF_Transactional);
	if (!Policy)
	{
		return nullptr;
	}
	Policy->InitializeV6Defaults();
	if (!Policy->ValidatePolicy())
	{
		UE_LOG(LogEFCalystoDungeonDirectorPolicyV6AssetFactory, Error,
			TEXT("The default Calysto V6 singleton failed native validation."));
		Policy->MarkAsGarbage();
		return nullptr;
	}
	return Policy;
}

FText UEFCalystoDungeonDirectorPolicyV6AssetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Calysto Dungeon Director Policy V6 Data Asset");
}

#undef LOCTEXT_NAMESPACE
