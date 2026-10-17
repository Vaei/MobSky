// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobSky : ModuleRules
{
	public MobSky(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Projects",
				"RenderCore",
				"RHI",
				"Renderer",
				"MobLights",
			}
			);
	}
}
