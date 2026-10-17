// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MobSkySubsystem.generated.h"

class FMobCloudViewExtension;
class FSceneView;
class UMobCloudComponent;
class UMobSkyComponent;
class UStaticMeshComponent;
struct FMobCloudFrame;

/**
 * Keeps the sky and the cloud layers on the camera, and the fog collection's ambient on the sky.
 *
 * Ticks in the editor as well as in play, so what is authored in the viewport is what the game shows.
 */
UCLASS()
class MOBSKY_API UMobSkySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

	void RegisterCloud(UMobCloudComponent* Cloud);
	void UnregisterCloud(UMobCloudComponent* Cloud);

	void RegisterSky(UMobSkyComponent* Sky);
	void UnregisterSky(UMobSkyComponent* Sky);

	/** The sky that decides the ambient: the first one registered. */
	UMobSkyComponent* GetSky() const;

	const TArray<TWeakObjectPtr<UMobCloudComponent>>& GetClouds() const { return Clouds; }

	/**
	 * Hides whichever way of drawing the cloud layers this view is not using: the composite from every
	 * view but the one the render pass runs for, and the marching layers from that one. Game thread.
	 */
	void HideForView(FSceneView& View, bool bRenderPassView) const;

	/** Snapshots the layers and the light they are under for the render pass. False when there is nothing to march. */
	bool GatherCloudFrame(FMobCloudFrame& Out, FIntPoint Size) const;

protected:
	bool GetViewLocation(FVector& OutLocation) const;
	void TickVisibility();
	void PublishAmbient(const UMobSkyComponent& Sky, float ViewZ) const;

	/** Tells the sky which deck to paint past the layer's reach, and where to start. */
	void PublishFarClouds(const FVector& View) const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UMobCloudComponent>> Clouds;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UMobSkyComponent>> Skies;

	/** Keeps the composite on the camera, and makes it the first time a layer needs it. */
	void TickComposite(const FVector& View);

	/** A sphere round the camera carrying the composite material, on an actor of its own. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Composite;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CompositeOwner;

	TSharedPtr<FMobCloudViewExtension, ESPMode::ThreadSafe> Extension;
	FVector LastView = FVector::ZeroVector;

	int32 LastCloudsEnabled = -1;
	int32 LastSkyEnabled = -1;
};
