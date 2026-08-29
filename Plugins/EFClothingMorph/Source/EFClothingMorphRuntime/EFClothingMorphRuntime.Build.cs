using UnrealBuildTool;

public class EFClothingMorphRuntime : ModuleRules
{
	public EFClothingMorphRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DeveloperSettings",
			"ComputeFramework",
			"OptimusCore",
			"RenderCore",
			"RHI",
			"EFCharacterCreationRuntime"
		});
	}
}
