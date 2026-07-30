using System.IO;
using UnrealBuildTool;

public class EFProjectSystemsEditor : ModuleRules
{
	public EFProjectSystemsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory,
			Path.Combine(ModuleDirectory, "..", "EFProjectSystemsGameplay"),
			Path.Combine(ModuleDirectory, "..", "EFProjectSystemsCore")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"GameplayAbilities",
			"AscentCombatFramework",
			"EFCharacterCreationRuntime",
			"EFLevelFlowRuntime",
			"EFProjectSystemsCore",
			"EFProjectSystemsGameplay",
			"EFProjectSystemsUI",
			"EFProceduralEditor"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ACFTrainingSystem",
			"CharacterController",
			"UnrealEd",
			"EditorScriptingUtilities",
			"AssetTools",
			"IKRig",
			"IKRigEditor",
			"Json",
			"JsonUtilities",
			"UMG",
			"Slate",
			"SlateCore",
			"BlueprintGraph",
			"AnimGraph",
			"AnimationBlueprintLibrary"
		});
	}
}
