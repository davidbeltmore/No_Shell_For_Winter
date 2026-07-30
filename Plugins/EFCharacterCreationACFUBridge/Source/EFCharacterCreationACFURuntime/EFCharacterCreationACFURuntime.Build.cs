using UnrealBuildTool;

public class EFCharacterCreationACFURuntime : ModuleRules
{
	public EFCharacterCreationACFURuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"EFCharacterCreationRuntime",
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayAbilities"
		});
	}
}
