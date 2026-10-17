// Copyright (c) Jared Taylor

#include "MobCloudRendering.h"

#include "GlobalShader.h"
#include "MobCloudComponent.h"
#include "MobSkySubsystem.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace MobCloudRendering
{
	static TAutoConsoleVariable<int32> CVarRenderPass(
		TEXT("mob.Clouds.RenderPass"),
		1,
		TEXT("Marches cloud layers in a render pass at reduced resolution and accumulates them over frames. 0 marches them in their material at full resolution."),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarResolution(
		TEXT("mob.Clouds.Resolution"),
		0.5f,
		TEXT("Fraction of the screen's resolution the render pass marches at."),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarCheckerboard(
		TEXT("mob.Clouds.Checkerboard"),
		1,
		TEXT("Marches one pixel of every 2x2 block a frame, in turn, and rebuilds the rest from history. A quarter of the cost; 0 marches every pixel every frame."),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarHistory(
		TEXT("mob.Clouds.History"),
		0.9f,
		TEXT("How much of the previous frame each frame keeps. Higher is smoother and trails more."),
		ECVF_RenderThreadSafe);

	bool IsEnabled()
	{
		return CVarRenderPass.GetValueOnAnyThread() != 0;
	}
}

class FMobCloudMarchPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMobCloudMarchPS);
	SHADER_USE_PARAMETER_STRUCT(FMobCloudMarchPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, LayerCount)
		SHADER_PARAMETER_ARRAY(FVector4f, LayerA, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerB, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerC, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerD, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerE, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerF, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerG, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerScatter, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerEmissive, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER_ARRAY(FVector4f, LayerFlags, [MobCloudRendering::MaxLayers])
		SHADER_PARAMETER(FMatrix44f, ClipToRelative)
		SHADER_PARAMETER(FVector4f, CamWorld)
		SHADER_PARAMETER(FVector2f, OutputSize)
		SHADER_PARAMETER(float, FrameJitter)
		SHADER_PARAMETER(float, Time)
		SHADER_PARAMETER(FVector4f, KeyDir)
		SHADER_PARAMETER(FVector4f, KeyColor)
		SHADER_PARAMETER(FVector4f, Ambient)
		SHADER_PARAMETER(FVector4f, Haze)
		SHADER_PARAMETER(float, PixelAngle)
		SHADER_PARAMETER(FVector4f, Checker)
		SHADER_PARAMETER(FVector2f, SubPixel)
		SHADER_PARAMETER_TEXTURE(Texture3D, ShapeTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShapeTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture3D, DetailTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DetailTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, WeatherTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeatherTexSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FMobCloudMarchPS, "/MobSky/Private/MobCloudPass.usf", "MarchPS", SF_Pixel);

class FMobCloudTemporalPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMobCloudTemporalPS);
	SHADER_USE_PARAMETER_STRUCT(FMobCloudTemporalPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, ClipToRelative)
		SHADER_PARAMETER(FMatrix44f, PrevRelativeToClip)
		SHADER_PARAMETER(FVector4f, CamDelta)
		SHADER_PARAMETER(FVector2f, OutputSize)
		SHADER_PARAMETER(float, HistoryWeight)
		SHADER_PARAMETER(int32, bHistoryValid)
		SHADER_PARAMETER(FVector4f, Checker)
		SHADER_PARAMETER(FVector2f, MarchSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FMobCloudTemporalPS, "/MobSky/Private/MobCloudPass.usf", "TemporalPS", SF_Pixel);

FMobCloudViewExtension::FMobCloudViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UMobSkySubsystem* InSubsystem)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
	, Subsystem(InSubsystem)
{
}

bool FMobCloudViewExtension::ClaimView(const FSceneViewFamily& Family, const FSceneView& View)
{
	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection || !View.State)
	{
		return false;
	}

	const uint32 Key = View.State->GetViewKey();

	// A view that has not been drawn for a couple of frames has been closed or stopped updating, and
	// the pass moves to whichever view is being drawn now.
	if (PrimaryKey == 0 || PrimaryLastFrame + 2 < GFrameCounter)
	{
		PrimaryKey = Key;
	}

	if (Key != PrimaryKey)
	{
		return false;
	}

	PrimaryLastFrame = GFrameCounter;
	return true;
}

void FMobCloudViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobCloudViewExtension::SetupView);

	UMobSkySubsystem* Sky = Subsystem.Get();
	if (!Sky)
	{
		return;
	}

	const float Scale = FMath::Clamp(MobCloudRendering::CVarResolution.GetValueOnGameThread(), 0.125f, 1.f);
	const FIntPoint ViewSize = InView.UnscaledViewRect.Size();
	const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(ViewSize.X * Scale)), FMath::Max(1, FMath::RoundToInt(ViewSize.Y * Scale)));

	// A view the pass cannot serve keeps the marching layers, so a missing asset costs quality rather
	// than the clouds themselves.
	FMobCloudFrame Next;
	const bool bPrimary = MobCloudRendering::IsEnabled() && ClaimView(InViewFamily, InView)
		&& Sky->GatherCloudFrame(Next, Size);
	Sky->HideForView(InView, bPrimary);

	if (!bPrimary)
	{
		return;
	}

	Next.ViewKey = InView.State->GetViewKey();
	Next.HistoryWeight = FMath::Clamp(MobCloudRendering::CVarHistory.GetValueOnGameThread(), 0.f, 0.98f);

	TWeakPtr<FMobCloudViewExtension, ESPMode::ThreadSafe> Weak = StaticCastSharedRef<FMobCloudViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(MobCloudFrame)([Weak, Next](FRHICommandListImmediate&)
	{
		if (const TSharedPtr<FMobCloudViewExtension, ESPMode::ThreadSafe> Extension = Weak.Pin())
		{
			Extension->Frame = Next;
		}
	});
}

void FMobCloudViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	if (!InView.State || InView.State->GetViewKey() != Frame.ViewKey || Frame.LayerCount == 0
		|| !Frame.Shape || !Frame.Detail || !Frame.Weather || !Frame.ColorTarget || !Frame.DepthTarget)
	{
		return;
	}

	// Each snapshot is used once. A view that is not set up next frame must not march with pointers
	// the game thread may since have released.
	const FMobCloudFrame Current = Frame;
	Frame.LayerCount = 0;

	if (!Current.Shape->IsInitialized() || !Current.Detail->IsInitialized() || !Current.Weather->IsInitialized()
		|| !Current.ColorTarget->IsInitialized() || !Current.DepthTarget->IsInitialized())
	{
		return;
	}

	FRHITexture* ColorRHI = Current.ColorTarget->GetRenderTargetTexture();
	FRHITexture* DepthRHI = Current.DepthTarget->GetRenderTargetTexture();
	if (!ColorRHI || !DepthRHI || !Current.Shape->TextureRHI || !Current.Detail->TextureRHI || !Current.Weather->TextureRHI)
	{
		return;
	}

	// The targets are resized on the game thread and catch up a frame later; until they match, skip.
	const FIntPoint Size = Current.Size;
	if (ColorRHI->GetSizeXY() != Size || DepthRHI->GetSizeXY() != Size)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(MobCloudViewExtension::PreRenderView);
	RDG_EVENT_SCOPE(GraphBuilder, "MobClouds %dx%d", Size.X, Size.Y);

	// Clip space to a direction from the camera, and back again for last frame, with the camera's own
	// position taken out so neither loses precision a long way from the origin.
	const FViewMatrices& Matrices = InView.ViewMatrices;
	const FMatrix RelativeToClip = Matrices.GetViewMatrix().RemoveTranslation() * Matrices.GetProjectionNoAAMatrix();
	const FMatrix44f ClipToRelative(RelativeToClip.Inverse());
	const FVector Camera = Matrices.GetViewOrigin();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());

	// One pixel of each 2x2 block a frame, visiting all four in an order that never marches two
	// neighbours back to back.
	static const FIntPoint Order[] = { FIntPoint(0, 0), FIntPoint(1, 1), FIntPoint(1, 0), FIntPoint(0, 1) };
	const int32 Stride = MobCloudRendering::CVarCheckerboard.GetValueOnRenderThread() != 0 ? 2 : 1;
	const FIntPoint Phase = Stride > 1 ? Order[FrameIndex % UE_ARRAY_COUNT(Order)] : FIntPoint::ZeroValue;
	const FIntPoint MarchSize((Size.X + Stride - 1) / Stride, (Size.Y + Stride - 1) / Stride);
	const FVector4f Checker(static_cast<float>(Phase.X), static_cast<float>(Phase.Y), static_cast<float>(Stride), 0.f);

	const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(Size, PF_FloatRGBA, FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(Size, PF_G16R16F, FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureDesc MarchColorDesc = ColorDesc;
	MarchColorDesc.Extent = MarchSize;
	FRDGTextureDesc MarchDepthDesc = DepthDesc;
	MarchDepthDesc.Extent = MarchSize;

	FRDGTextureRef MarchColor = GraphBuilder.CreateTexture(MarchColorDesc, TEXT("MobClouds.MarchColor"));
	FRDGTextureRef MarchDepth = GraphBuilder.CreateTexture(MarchDepthDesc, TEXT("MobClouds.MarchDepth"));

	{
		FMobCloudMarchPS::FParameters* Parameters = GraphBuilder.AllocParameters<FMobCloudMarchPS::FParameters>();
		Parameters->LayerCount = Current.LayerCount;
		for (int32 Index = 0; Index < MobCloudRendering::MaxLayers; ++Index)
		{
			Parameters->LayerA[Index] = Current.A[Index];
			Parameters->LayerB[Index] = Current.B[Index];
			Parameters->LayerC[Index] = Current.C[Index];
			Parameters->LayerD[Index] = Current.D[Index];
			Parameters->LayerE[Index] = Current.E[Index];
			Parameters->LayerF[Index] = Current.F[Index];
			Parameters->LayerG[Index] = Current.G[Index];
			Parameters->LayerScatter[Index] = Current.Scatter[Index];
			Parameters->LayerEmissive[Index] = Current.Emissive[Index];
			Parameters->LayerFlags[Index] = Current.Flags[Index];
		}
		Parameters->ClipToRelative = ClipToRelative;
		Parameters->CamWorld = FVector4f(FVector3f(Camera), 0.f);
		Parameters->OutputSize = FVector2f(Size);
		Parameters->FrameJitter = FMath::Frac(FrameIndex * 0.61803398875f);
		Parameters->Checker = Checker;

		// R2 sequence, stepped once per visit so a checkerboarded pixel still sees every offset.
		const float Visit = static_cast<float>(FrameIndex / (Stride * Stride));
		Parameters->SubPixel = FVector2f(FMath::Frac(Visit * 0.7548776662f), FMath::Frac(Visit * 0.5698402910f)) - 0.5f;
		Parameters->Time = Current.Time;
		Parameters->KeyDir = Current.KeyDir;
		Parameters->KeyColor = Current.KeyColor;
		Parameters->Ambient = Current.Ambient;
		Parameters->Haze = Current.Haze;

		// One of the pass's own pixels, which are larger than the screen's.
		Parameters->PixelAngle = 2.f / (Size.Y * static_cast<float>(Matrices.GetProjectionNoAAMatrix().M[1][1]));
		Parameters->ShapeTex = Current.Shape->TextureRHI;
		Parameters->ShapeTexSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Parameters->DetailTex = Current.Detail->TextureRHI;
		Parameters->DetailTexSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Parameters->WeatherTex = Current.Weather->TextureRHI;
		Parameters->WeatherTexSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Parameters->RenderTargets[0] = FRenderTargetBinding(MarchColor, ERenderTargetLoadAction::ENoAction);
		Parameters->RenderTargets[1] = FRenderTargetBinding(MarchDepth, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FMobCloudMarchPS> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("March %dx%d", MarchSize.X, MarchSize.Y),
			PixelShader, Parameters, FIntRect(FIntPoint::ZeroValue, MarchSize));
	}

	// History from another view or another size is worse than none.
	const bool bHistoryValid = HistoryColor.IsValid() && HistoryDepth.IsValid() && HistoryKey == Current.ViewKey
		&& HistoryColor->GetDesc().Extent == Size && HistoryStride == Stride;

	FRDGTextureRef PreviousColor = bHistoryValid ? GraphBuilder.RegisterExternalTexture(HistoryColor) : MarchColor;
	FRDGTextureRef PreviousDepth = bHistoryValid ? GraphBuilder.RegisterExternalTexture(HistoryDepth) : MarchDepth;
	FRDGTextureRef ResolvedColor = GraphBuilder.CreateTexture(ColorDesc, TEXT("MobClouds.Color"));
	FRDGTextureRef ResolvedDepth = GraphBuilder.CreateTexture(DepthDesc, TEXT("MobClouds.Depth"));

	{
		FMobCloudTemporalPS::FParameters* Parameters = GraphBuilder.AllocParameters<FMobCloudTemporalPS::FParameters>();
		Parameters->ClipToRelative = ClipToRelative;
		Parameters->PrevRelativeToClip = PrevRelativeToClip;
		Parameters->CamDelta = FVector4f(FVector3f(Camera - PrevCamera), 0.f);
		Parameters->OutputSize = FVector2f(Size);
		// A pixel is marched once every Stride squared frames, so its fresh sample stands in for that many
		// and the history it is blended with has to count for correspondingly less.
		Parameters->HistoryWeight = FMath::Pow(Current.HistoryWeight, static_cast<float>(Stride * Stride));
		Parameters->Checker = Checker;
		Parameters->MarchSize = FVector2f(MarchSize);
		Parameters->bHistoryValid = bHistoryValid ? 1 : 0;
		Parameters->CurrentColor = MarchColor;
		Parameters->CurrentDepth = MarchDepth;
		Parameters->HistoryColor = PreviousColor;
		Parameters->HistoryDepth = PreviousDepth;
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->RenderTargets[0] = FRenderTargetBinding(ResolvedColor, ERenderTargetLoadAction::ENoAction);
		Parameters->RenderTargets[1] = FRenderTargetBinding(ResolvedDepth, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FMobCloudTemporalPS> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Temporal"),
			PixelShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
	}

	GraphBuilder.QueueTextureExtraction(ResolvedColor, &HistoryColor);
	GraphBuilder.QueueTextureExtraction(ResolvedDepth, &HistoryDepth);

	// Copied into the targets the composite material samples. They have to be assets, or a material
	// could not name them.
	FRDGTextureRef OutColor = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(ColorRHI, TEXT("MobClouds.Target.Color")));
	FRDGTextureRef OutDepth = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DepthRHI, TEXT("MobClouds.Target.Depth")));
	AddCopyTexturePass(GraphBuilder, ResolvedColor, OutColor);
	AddCopyTexturePass(GraphBuilder, ResolvedDepth, OutDepth);
	GraphBuilder.SetTextureAccessFinal(OutColor, ERHIAccess::SRVGraphics);
	GraphBuilder.SetTextureAccessFinal(OutDepth, ERHIAccess::SRVGraphics);

	HistoryKey = Current.ViewKey;
	HistoryStride = Stride;
	PrevRelativeToClip = FMatrix44f(RelativeToClip);
	PrevCamera = Camera;
	++FrameIndex;
}
