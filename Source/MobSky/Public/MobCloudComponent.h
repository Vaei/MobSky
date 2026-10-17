// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "MobCloudTypes.h"
#include "MobCloudComponent.generated.h"

class UMaterialInstanceDynamic;

/** A layer's parameters as the shaders take them, whether they arrive in a material or a render pass. */
struct FMobCloudPacked
{
	FLinearColor A, B, C, D, E, F, G;
	FLinearColor Scatter;
	FLinearColor Emissive;
	FLinearColor Extent;

	/** Detail and shadow on or off, for the render pass; the material has them as permutations. */
	FLinearColor Flags;
};

/**
 * Clouds, marched in a material.
 *
 * A Layer is weather: a slab between two heights that the subsystem keeps centred on the camera, so it
 * reaches the horizon wherever you fly and you can fly through it. A Volume stays where it is put, and
 * with an emissive colour it is a nebula.
 *
 * Lit by the key light and the ambient in MobLights' fog collection, which the fog subsystem and the
 * sky keep up to date. Distances are world units.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent),
	HideCategories=(Collision, Physics, Lighting, HLOD, Navigation, VirtualTexture, Mobile, RayTracing, Materials))
class MOBSKY_API UMobCloudComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMobCloudComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	EMobCloudShape Shape = EMobCloudShape::Layer;

	/** World Z the layer starts at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float BaseHeight = 180000.f;

	/** World Z the heaviest cloud reaches. Lighter cloud stops short of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float TopHeight = 420000.f;

	/** Half extents of a volume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(EditCondition="Shape==EMobCloudShape::Volume", EditConditionHides))
	FVector Extent = FVector(500000.f, 500000.f, 250000.f);

	/** How much of a volume's radius its density fades out across. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(UIMin="0.05", UIMax="1", ClampMin="0.01", ClampMax="1", EditCondition="Shape==EMobCloudShape::Volume", EditConditionHides))
	float EdgeFade = 0.4f;

	/** How far a layer reaches from the camera, horizontally, while the camera is in or under it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="10000", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float MaxDistance = 6000000.f;

	/** How much further a layer reaches for every unit the camera climbs above it, so it fills the view from high up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="0", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float ReachPerHeight = 3.f;

	/** Radius of the planet under the layer, which bends it down towards the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="100000", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float PlanetRadius = 637100000.f;

	/** How much of the sky is cloud. The weather, and the one to drive at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float Coverage = 0.45f;

	/** Extinction per world unit, base two: transmittance halves every 1/Density units inside full cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(UIMin="0", UIMax="0.002", ClampMin="0"))
	float Density = 0.0003f;

	/** World units one tile of the weather map spans. A few clusters of cloud cross it, of every size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(ClampMin="1000"))
	float CoverageScale = 3000000.f;

	/** World units one tile of the shape noise spans. How big a billow is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(ClampMin="1000"))
	float ShapeScale = 150000.f;

	/** World units a second the cloud drifts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud")
	FVector2D Wind = FVector2D(800.f, 250.f);

	/** World units one tile of the planet scale weather spans: storms, fronts and the clear sky between them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(ClampMin="100000", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float MacroScale = 60000000.f;

	/** How far the planet scale weather pushes Coverage either way. 0 is the same weather everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float MacroAmount = 0.8f;

	/** How much of the layer's height the underside rounds off over. Small is a flat base. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(UIMin="0.01", UIMax="1", ClampMin="0.01", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float BottomFade = 0.05f;

	/** How much of the layer's height the top tapers over. Large is a towering, rounded top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cloud", meta=(UIMin="0.01", UIMax="1", ClampMin="0.01", EditCondition="Shape==EMobCloudShape::Layer", EditConditionHides))
	float TopFade = 0.5f;

	/** Erodes the edges into wisps. A permutation: off costs nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Detail")
	bool bDetail = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Detail", meta=(ClampMin="100", EditCondition="bDetail"))
	float DetailScale = 25000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Detail", meta=(UIMin="0", UIMax="1", ClampMin="0", EditCondition="bDetail"))
	float DetailAmount = 0.4f;

	/** Each step in cloud looks towards the sun, so clouds shade themselves. A permutation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting")
	bool bShadow = true;

	/** World units that look towards the sun reaches. About the height of the tallest cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(ClampMin="100", EditCondition="bShadow"))
	float ShadowDistance = 200000.f;

	/** What colour the cloud turns the light it scatters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(HideAlphaChannel))
	FLinearColor ScatterColor = FLinearColor(1.f, 1.f, 1.f);

	/** Light the cloud makes itself. Black for weather; a nebula glows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(HideAlphaChannel))
	FLinearColor EmissiveColor = FLinearColor::Black;

	/** How strongly light is thrown forward, which is the silver lining looking towards the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(UIMin="0", UIMax="0.9", ClampMin="0", ClampMax="0.95"))
	float PhaseForward = 0.6f;

	/** How strongly light is thrown back, which lifts the sunlit side seen with the sun behind you. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(UIMin="-0.9", UIMax="0", ClampMin="-0.95", ClampMax="0"))
	float PhaseBack = -0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float PhaseBlend = 0.35f;

	/** How much ambient reaches the underside, relative to the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float AmbientBottom = 0.3f;

	/** How much the thin edges darken seen away from the sun, which is what shows a cloud's folds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float Powder = 0.6f;

	/** World units of sea level air over which a cloud has faded halfway into the horizon. Thinner air hazes less. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lighting", meta=(ClampMin="1000"))
	float HazeDistance = 4000000.f;

	/** How many samples the march takes. Spaced finer near the camera than far away. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cost", meta=(UIMin="8", UIMax="96", ClampMin="4", ClampMax="96"))
	int32 StepCount = 48;

	/** Replaces the generated material. The parameters are written to a dynamic instance of it either way. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cost", AdvancedDisplay)
	TObjectPtr<UMaterialInterface> MaterialOverride;

	UFUNCTION(BlueprintCallable, Category="Clouds")
	void RefreshClouds();

	UFUNCTION(BlueprintCallable, Category="Clouds")
	void SetCoverage(float NewCoverage);

	UFUNCTION(BlueprintCallable, Category="Clouds")
	void SetDensity(float NewDensity);

	/** Everything the shaders read, packed the way they read it. */
	FMobCloudPacked Pack() const;

	/** Centres a layer on the view. The subsystem calls this every frame; a volume ignores it. */
	void FitToView(const FVector& ViewLocation);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	int32 WantedVariant() const;
	FVector ComputeProxyScale() const;
	void ApplyMaterial();
	void ApplyParameters();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Instance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> InstanceParent;
};
