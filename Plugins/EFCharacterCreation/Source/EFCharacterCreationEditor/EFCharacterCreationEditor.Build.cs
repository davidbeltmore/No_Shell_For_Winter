using UnrealBuildTool;

public class EFCharacterCreationEditor : ModuleRules
{
	public EFCharacterCreationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EFCharacterCreationRuntime"
		});

		PrivateDependencyModuleNames.Add("UnrealEd");
	}
}
