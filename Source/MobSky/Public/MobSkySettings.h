// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MobSkySettings.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Where MobSky finds what it draws with. The defaults are what Sky -> Generate Materials authors. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Mob Sky"))
class MOBSKY_API UMobSkySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMobSkySettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Indexed by the MobCloudVariant flags: detail 1, shadow 2, volume 4. */
	UPROPERTY(EditAnywhere, Config, Category="Clouds")
	TArray<TSoftObjectPtr<UMaterialInterface>> CloudMaterials;

	/** The sky with space marched live. What the editor shows, and what a sky that does not bake uses. */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<UMaterialInterface> SkyMaterial;

	/** The sky with space read from the cube baked at BeginPlay. */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<UMaterialInterface> SkyBakedMaterial;

	/** Space alone, which is what the bake captures. */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<UMaterialInterface> SpaceMaterial;

	/** Turns the cube the sky is captured into into the long/lat panorama MobMaterials reflects. */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<UMaterialInterface> PanoramaMaterial;

	/**
	 * Where the sky's panorama is drawn. An asset, so a surface material can name it as its Sky Panorama and
	 * reflect the live sky with no code.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<class UTextureRenderTarget2D> PanoramaTarget;

	/** What the sky tells the clouds: the horizon's colour and how much air there is. */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<class UMaterialParameterCollection> ParameterCollection;

	/** The noise the render pass marches through: the same textures the cloud material reads. */
	UPROPERTY(EditAnywhere, Config, Category="Clouds|Render Pass")
	TSoftObjectPtr<class UVolumeTexture> ShapeTexture;

	UPROPERTY(EditAnywhere, Config, Category="Clouds|Render Pass")
	TSoftObjectPtr<class UVolumeTexture> DetailTexture;

	UPROPERTY(EditAnywhere, Config, Category="Clouds|Render Pass")
	TSoftObjectPtr<class UTexture2D> WeatherTexture;

	/** Where the render pass leaves its result, and what the composite reads. */
	UPROPERTY(EditAnywhere, Config, Category="Clouds|Render Pass")
	TSoftObjectPtr<class UTextureRenderTarget2D> ColorTarget;

	UPROPERTY(EditAnywhere, Config, Category="Clouds|Render Pass")
	TSoftObjectPtr<class UTextureRenderTarget2D> DepthTarget;

	/** Draws the render pass's result into the translucency pass, where the march material would have been. */
	UPROPERTY(EditAnywhere, Config, Category="Clouds|Render Pass")
	TSoftObjectPtr<UMaterialInterface> CompositeMaterial;

	/** The sun's lens flare, drawn over everything on a proxy round the camera. */
	UPROPERTY(EditAnywhere, Config, Category="Sky")
	TSoftObjectPtr<UMaterialInterface> LensFlareMaterial;

	/** The inside-out sphere the clouds and the dome rasterise. MobLights' fog proxy. */
	UPROPERTY(EditAnywhere, Config, Category="Proxy")
	TSoftObjectPtr<UStaticMesh> ProxyMesh;

	static UMaterialInterface* GetCloudMaterial(int32 Variant);
	static UStaticMesh* GetProxyMesh();
};
