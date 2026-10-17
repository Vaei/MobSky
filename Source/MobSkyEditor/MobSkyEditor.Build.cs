// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobSkyEditor : ModuleRules
{
	public MobSkyEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"LevelEditor",
				"AssetTools",
				"ToolMenus",
				"Projects",
				"PythonScriptPlugin",
				"Settings",
				"ImageCore",
				"MobSky",
			}
			);
	}
}
