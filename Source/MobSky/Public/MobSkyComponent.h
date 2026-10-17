// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "MobSkyComponent.generated.h"

class UMaterialInstanceDynamic;
class USceneCaptureComponentCube;
class UTextureRenderTargetCube;

/** A nebula on the backdrop. Far enough away to have no parallax; fly-through nebulae are cloud volumes. */
USTRUCT(BlueprintType)
struct MOBSKY_API FMobSkyNebula
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula")
	bool bEnabled = true;

	/** Where it is in the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(Units="Degrees"))
	float Yaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(Units="Degrees", UIMin="-90", UIMax="90"))
	float Pitch = 20.f;

	/** How much sky it covers, as an angular radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(Units="Degrees", UIMin="1", UIMax="60", ClampMin="0.5", ClampMax="80"))
	float Radius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(HideAlphaChannel))
	FLinearColor ColorA = FLinearColor(0.9f, 0.2f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(HideAlphaChannel))
	FLinearColor ColorB = FLinearColor(0.2f, 0.5f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(ClampMin="0"))
	float Brightness = 1.5f;

	/** How much dark dust is threaded through it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(UIMin="0", UIMax="2", ClampMin="0"))
	float Dust = 0.8f;

	/** How finely the gas breaks up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula", meta=(UIMin="0.2", UIMax="3", ClampMin="0.05"))
	float NoiseScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nebula")
	float Seed = 0.f;
};

/**
 * The sky: atmosphere, planet, stars, galaxy and nebulae, on a dome that follows the camera.
 *
 * How much air is above the camera decides how much of the atmosphere is drawn, so the sky goes from
 * blue on the ground to black with the planet's rim below you in space, with no game code. Set
 * GroundZ and VacuumZ to the world's own.
 *
 * The galaxy and nebulae are static, so in a game they are baked into a cube once at BeginPlay and the
 * dome reads that. The editor always marches them live, which is what lets you tune them.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent),
	HideCategories=(Collision, Physics, Lighting, HLOD, Navigation, VirtualTexture, Mobile, RayTracing, Materials))
class MOBSKY_API UMobSkyComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMobSkyComponent();

	/** World Z of the ground: full air. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air")
	float GroundZ = 0.f;

	/** World Z where the air runs out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air")
	float VacuumZ = 6096000.f;

	/** Radius of the planet, for how far the horizon dips and how big the planet looks from above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air", meta=(ClampMin="100000"))
	float PlanetRadius = 637100000.f;

	/** Writes the ambient into MobLights' fog collection, so clouds and fog go blue by day and dark in vacuum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air")
	bool bDrivesAmbient = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air", meta=(HideAlphaChannel, EditCondition="bDrivesAmbient"))
	FLinearColor DayAmbient = FLinearColor(0.35f, 0.45f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air", meta=(HideAlphaChannel, EditCondition="bDrivesAmbient"))
	FLinearColor NightAmbient = FLinearColor(0.02f, 0.025f, 0.04f);

	/** The ambient above the air, where only starlight and the planet's glow are left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Air", meta=(HideAlphaChannel, EditCondition="bDrivesAmbient"))
	FLinearColor SpaceAmbient = FLinearColor(0.03f, 0.035f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(HideAlphaChannel))
	FLinearColor Zenith = FLinearColor(0.12f, 0.3f, 0.8f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(HideAlphaChannel))
	FLinearColor Horizon = FLinearColor(0.6f, 0.75f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(HideAlphaChannel))
	FLinearColor Sunset = FLinearColor(1.f, 0.42f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(HideAlphaChannel))
	FLinearColor Night = FLinearColor(0.004f, 0.006f, 0.015f);

	/** The planet below the horizon, lit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(HideAlphaChannel))
	FLinearColor Ground = FLinearColor(0.05f, 0.08f, 0.12f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(UIMin="1", UIMax="12", ClampMin="0.5"))
	float HorizonExponent = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(Units="Degrees", UIMin="0.1", UIMax="3", ClampMin="0.01"))
	float SunRadius = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(ClampMin="0"))
	float SunIntensity = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(UIMin="2", UIMax="2000", ClampMin="1"))
	float HaloExponent = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(ClampMin="0"))
	float HaloIntensity = 0.8f;

	/** How thick the band of air on the planet's rim looks from above it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(UIMin="0.001", UIMax="0.1", ClampMin="0.0001"))
	float LimbWidth = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Atmosphere", meta=(ClampMin="0"))
	float LimbIntensity = 1.2f;

	/** Turns the whole of space round the vertical. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Space", meta=(Units="Degrees"))
	float SpaceYaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(ClampMin="0"))
	float StarBrightness = 1.f;

	/** How many cells cross a cube face for the faint and the bright stars, and how many cells hold one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(ClampMin="8"))
	float FaintStarCells = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float FaintStarChance = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(ClampMin="8"))
	float BrightStarCells = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float BrightStarChance = 0.25f;

	/** How steeply brightness falls away. High is mostly faint stars and a few bright ones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(UIMin="1", UIMax="30", ClampMin="1"))
	float StarMagnitude = 16.f;

	/** How much stars shimmer through air. None in vacuum, whatever this says. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stars", meta=(UIMin="0", UIMax="1", ClampMin="0"))
	float Twinkle = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(ClampMin="0"))
	float GalaxyBrightness = 10.f;

	/** Tilt and turn of the galaxy's plane, which is where the band crosses the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(Units="Degrees"))
	float GalaxyTilt = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(Units="Degrees"))
	float GalaxyYaw = 30.f;

	/** Which way along the band the core lies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(Units="Degrees"))
	float GalaxySpin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(UIMin="0", UIMax="3", ClampMin="0"))
	float GalaxyDust = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(UIMin="1", UIMax="6", ClampMin="1"))
	float GalaxyArms = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(UIMin="0.1", UIMax="1", ClampMin="0.05"))
	float GalaxyArmTightness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(ClampMin="0"))
	float GalaxyBulge = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(HideAlphaChannel))
	FLinearColor GalaxyDiscColor = FLinearColor(0.75f, 0.82f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(HideAlphaChannel))
	FLinearColor GalaxyBulgeColor = FLinearColor(1.f, 0.8f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(UIMin="8", UIMax="48", ClampMin="4", ClampMax="48"))
	int32 GalaxySteps = 24;

	/** Other galaxies, as sprites: how many cells cross a cube face and how many hold one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(ClampMin="1"))
	float DistantGalaxyCells = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float DistantGalaxyChance = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(ClampMin="0"))
	float DistantGalaxyBrightness = 1.5f;

	/** Angular radius of the largest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Galaxy", meta=(Units="Degrees", ClampMin="0.05"))
	float DistantGalaxySize = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Nebulae", meta=(TitleProperty="Radius"))
	TArray<FMobSkyNebula> Nebulae;

	/** Bakes the galaxy and nebulae into a cube at BeginPlay, so a game never marches them per pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cost")
	bool bBakeSpace = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cost", meta=(EditCondition="bBakeSpace", ClampMin="64", ClampMax="4096"))
	int32 BakeResolution = 1024;

	/** Radius of the dome. Larger than anything you can see, and no more. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cost", AdvancedDisplay, meta=(ClampMin="100000"))
	float DomeRadius = 50000000.f;

	UFUNCTION(BlueprintCallable, Category="Sky")
	void RefreshSky();

	/** Bakes space again, after changing the galaxy or a nebula at runtime. */
	UFUNCTION(BlueprintCallable, Category="Sky")
	void BakeSpace();

	/** How much air is above a height: 1 at GroundZ, 0 at VacuumZ, thinning fastest near the ground. */
	UFUNCTION(BlueprintPure, Category="Sky")
	float GetAirAmount(float WorldZ) const;

	/** Keeps the dome on the view and tells it how much air is overhead. The subsystem calls this every frame. */
	void FollowView(const FVector& ViewLocation);

	/** Takes a pending bake's captures. The subsystem calls this every frame. */
	void TickBake(float DeltaTime);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	void ApplyParameters(UMaterialInstanceDynamic* Target) const;
	UMaterialInstanceDynamic* MakeInstance(UMaterialInterface* Parent);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Instance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpaceInstance;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SpaceDome;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponentCube> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetCube> SpaceTarget;

	FLinearColor LastState = FLinearColor(-1.f, -1.f, -1.f, -1.f);

	/** Captures a bake still has to take, the time until the next, and whether its material was compiling. */
	int32 BakeRetakes = 0;
	float BakeClock = 0.f;
	bool bWasCompiling = false;
};
