using UnrealBuildTool;

public class EFProceduralRuntime : ModuleRules
{
	public EFProceduralRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AssetRegistry",
			"DeveloperSettings"
		});

	}
}
