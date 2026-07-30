using UnrealBuildTool;

public class EFCharacterCreationRuntime : ModuleRules
{
	public EFCharacterCreationRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AssetRegistry",
			"DeveloperSettings",
			"GameplayTags",
			"InputCore",
			"Slate",
			"SlateCore",
			"UMG"
		});
	}
}
