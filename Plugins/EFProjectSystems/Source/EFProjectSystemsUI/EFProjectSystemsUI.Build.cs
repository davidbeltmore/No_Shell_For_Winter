using UnrealBuildTool;

public class EFProjectSystemsUI : ModuleRules
{
	public EFProjectSystemsUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"AssetRegistry",
			"GameplayTags",
			"EFCharacterCreationRuntime",
			"EFLevelFlowRuntime",
			"EFProjectSystemsCore"
		});
	}
}
