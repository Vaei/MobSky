// Copyright (c) Jared Taylor

#include "MobSkySettings.h"

#include "MobCloudTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"

namespace
{
	TSoftObjectPtr<UMaterialInterface> MaterialAt(const FString& Name)
	{
		return TSoftObjectPtr<UMaterialInterface>(
			FSoftObjectPath(FString::Printf(TEXT("/MobSky/Materials/%s.%s"), *Name, *Name)));
	}
}

UMobSkySettings::UMobSkySettings()
{
	for (int32 Variant = 0; Variant < MobCloudVariant::Num; ++Variant)
	{
		CloudMaterials.Add(MaterialAt(MobCloudVariant::InstanceName(Variant)));
	}

	SkyMaterial = MaterialAt(TEXT("MI_MobSky"));
	SkyBakedMaterial = MaterialAt(TEXT("MI_MobSky_Baked"));
	SpaceMaterial = MaterialAt(TEXT("MI_MobSky_Space"));
	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(
		FSoftObjectPath(TEXT("/MobSky/Materials/MPC_MobSky.MPC_MobSky")));
	ShapeTexture = TSoftObjectPtr<UVolumeTexture>(FSoftObjectPath(TEXT("/MobSky/Textures/T_MobCloudShape.T_MobCloudShape")));
	DetailTexture = TSoftObjectPtr<UVolumeTexture>(FSoftObjectPath(TEXT("/MobSky/Textures/T_MobCloudDetail.T_MobCloudDetail")));
	WeatherTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/MobSky/Textures/T_MobCloudWeather.T_MobCloudWeather")));
	ColorTarget = TSoftObjectPtr<UTextureRenderTarget2D>(FSoftObjectPath(TEXT("/MobSky/Textures/RT_MobCloudColor.RT_MobCloudColor")));
	DepthTarget = TSoftObjectPtr<UTextureRenderTarget2D>(FSoftObjectPath(TEXT("/MobSky/Textures/RT_MobCloudDepth.RT_MobCloudDepth")));
	CompositeMaterial = MaterialAt(TEXT("M_MobCloudComposite"));
	ProxyMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/MobLights/Meshes/SM_MobLightProxy_Sphere.SM_MobLightProxy_Sphere")));
}

UMaterialInterface* UMobSkySettings::GetCloudMaterial(int32 Variant)
{
	const UMobSkySettings* Settings = GetDefault<UMobSkySettings>();
	return Settings->CloudMaterials.IsValidIndex(Variant)
		? Settings->CloudMaterials[Variant].LoadSynchronous() : nullptr;
}

UStaticMesh* UMobSkySettings::GetProxyMesh()
{
	return GetDefault<UMobSkySettings>()->ProxyMesh.LoadSynchronous();
}
