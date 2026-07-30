using UnrealBuildTool;

public class EFCharacterCreationDazBridgeEditor : ModuleRules
{
	public EFCharacterCreationDazBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"EFCharacterCreationRuntime",
			"Core",
			"CoreUObject",
			"DazToUnreal",
			"Engine",
			"Projects"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimGraph",
			"AssetRegistry",
			"BlueprintGraph",
			"ControlRig",
			"ControlRigDeveloper",
			"Json",
			"UnrealEd"
		});
	}
}
