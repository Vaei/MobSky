// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "SceneViewExtension.h"

class FTextureResource;
class FTextureRenderTargetResource;
class UMobSkySubsystem;

namespace MobCloudRendering
{
	static constexpr int32 MaxLayers = 4;

	/** Whether the render pass is on. Off, every view marches the clouds in their material. */
	bool IsEnabled();
}

/** One frame of cloud, as the game thread saw it, handed to the render thread whole. */
struct FMobCloudFrame
{
	uint32 ViewKey = 0;
	int32 LayerCount = 0;

	FVector4f A[MobCloudRendering::MaxLayers];
	FVector4f B[MobCloudRendering::MaxLayers];
	FVector4f C[MobCloudRendering::MaxLayers];
	FVector4f D[MobCloudRendering::MaxLayers];
	FVector4f E[MobCloudRendering::MaxLayers];
	FVector4f F[MobCloudRendering::MaxLayers];
	FVector4f G[MobCloudRendering::MaxLayers];
	FVector4f Scatter[MobCloudRendering::MaxLayers];
	FVector4f Emissive[MobCloudRendering::MaxLayers];
	FVector4f Flags[MobCloudRendering::MaxLayers];

	FVector4f KeyDir = FVector4f(0.f, 0.f, 1.f, 0.f);
	FVector4f KeyColor = FVector4f(1.f, 1.f, 1.f, 1.f);
	FVector4f Ambient = FVector4f(0.3f, 0.35f, 0.45f, 1.f);
	FVector4f Haze = FVector4f(0.6f, 0.75f, 0.95f, 1.f);
	float Time = 0.f;
	float HistoryWeight = 0.9f;

	FTextureResource* Shape = nullptr;
	FTextureResource* Detail = nullptr;
	FTextureResource* Weather = nullptr;
	FTextureRenderTargetResource* ColorTarget = nullptr;
	FTextureRenderTargetResource* DepthTarget = nullptr;
	FIntPoint Size = FIntPoint::ZeroValue;
};

/**
 * Marches the cloud layers at a fraction of the screen's resolution before the base pass, accumulates
 * them over frames, and leaves the result in two render targets the composite material reads.
 *
 * One view at a time: the game's, or the editor viewport being looked through. Every other view keeps
 * marching in the material, and the composite is hidden from it, so a second viewport never shows
 * the first one's clouds.
 */
class FMobCloudViewExtension final : public FWorldSceneViewExtension
{
public:
	FMobCloudViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UMobSkySubsystem* InSubsystem);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;

private:
	/** Picks the view the pass runs for, and keeps it until it stops being drawn. */
	bool ClaimView(const FSceneViewFamily& Family, const FSceneView& View);

	TWeakObjectPtr<UMobSkySubsystem> Subsystem;

	// Game thread.
	uint32 PrimaryKey = 0;
	uint64 PrimaryLastFrame = 0;

	// Render thread.
	FMobCloudFrame Frame;
	TRefCountPtr<IPooledRenderTarget> HistoryColor;
	TRefCountPtr<IPooledRenderTarget> HistoryDepth;
	FMatrix44f PrevRelativeToClip = FMatrix44f::Identity;
	FVector PrevCamera = FVector::ZeroVector;
	uint32 HistoryKey = 0;
	int32 HistoryStride = 0;
	uint32 FrameIndex = 0;
};
