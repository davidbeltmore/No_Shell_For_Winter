using UnrealBuildTool;

public class EFEquipmentEditor : ModuleRules
{
    public EFEquipmentEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "EFEquipmentRuntime", "InventorySystem", "GameplayTags" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "Kismet", "KismetCompiler", "BlueprintGraph", "BlueprintEditorLibrary", "UMG", "UMGEditor", "Slate", "SlateCore", "InputCore" });
    }
}
