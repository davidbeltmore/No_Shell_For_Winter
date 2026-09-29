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
			"AscentSkillTree",
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
			"EFProceduralRuntime",
			"EFProceduralEditor"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ACFTrainingSystem",
			"ActionsSystem",
			"NavigationSystem",
			"AdvancedRPGSystem",
			"AGSGraphRuntime",
			"AscentQuestSystem",
			"DirtyPawnRuntime",
			"AscentGASRuntime",
			"CharacterController",
			"UnrealEd",
			"PropertyEditor",
			"EditorScriptingUtilities",
			"AssetTools",
			"IKRig",
			"IKRigEditor",
			"Json",
			"JsonUtilities",
			"RenderCore",
			"UMG",
			"Slate",
			"SlateCore",
			"InputCore",
			"BlueprintGraph",
			"AnimGraph",
			"AnimationBlueprintLibrary"
		});
	}
}
