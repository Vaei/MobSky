// Copyright (c) Jared Taylor

#include "MobSkyModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogMobSky);

void FMobSkyModule::StartupModule()
{
	// PostConfigInit, for the same reason as MobLights: the mapping has to exist before any material
	// that includes /MobSky/Public/*.ush compiles.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobSky"));
	if (Plugin.IsValid())
	{
		const FString PluginRoot = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		AddShaderSourceDirectoryMapping(TEXT("/MobSky"), FPaths::Combine(PluginRoot, TEXT("Shaders")));
	}
}

IMPLEMENT_MODULE(FMobSkyModule, MobSky)
