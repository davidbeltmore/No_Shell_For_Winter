// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NoShellForWinter : ModuleRules
{
	public NoShellForWinter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"NoShellForWinter",
			"NoShellForWinter/Variant_Platforming",
			"NoShellForWinter/Variant_Platforming/Animation",
			"NoShellForWinter/Variant_Combat",
			"NoShellForWinter/Variant_Combat/AI",
			"NoShellForWinter/Variant_Combat/Animation",
			"NoShellForWinter/Variant_Combat/Gameplay",
			"NoShellForWinter/Variant_Combat/Interfaces",
			"NoShellForWinter/Variant_Combat/UI",
			"NoShellForWinter/Variant_SideScrolling",
			"NoShellForWinter/Variant_SideScrolling/AI",
			"NoShellForWinter/Variant_SideScrolling/Gameplay",
			"NoShellForWinter/Variant_SideScrolling/Interfaces",
			"NoShellForWinter/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
