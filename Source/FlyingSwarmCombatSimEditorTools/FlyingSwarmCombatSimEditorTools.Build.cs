using UnrealBuildTool;

public class FlyingSwarmCombatSimEditorTools : ModuleRules
{
	public FlyingSwarmCombatSimEditorTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"FlyingSwarmCombatSim"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"Slate",
			"SlateCore"
		});
	}
}



