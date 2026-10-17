// Copyright (c) Jared Taylor

#include "MobSkySubsystem.h"

#include "MobCloudComponent.h"
#include "MobCloudRendering.h"
#include "MobFogSettings.h"
#include "MobFogSubsystem.h"
#include "MobSkyComponent.h"
#include "MobSkySettings.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace MobSkyDebug
{
	static TAutoConsoleVariable<int32> CVarClouds(
		TEXT("mob.Clouds.Enable"),
		1,
		TEXT("Shows or hides every cloud component. The A and the B of measuring what the clouds cost."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarSky(
		TEXT("mob.Sky.Enable"),
		1,
		TEXT("Shows or hides the sky dome."),
		ECVF_Cheat);
}

namespace
{
	const FName AmbientColorName(TEXT("AmbientColor"));
	const FName KeyDirectionName(TEXT("KeyDirection"));
	const FName KeyColorName(TEXT("KeyColor"));
	const FName HazeColorName(TEXT("HazeColor"));
	const FName FogTimeName(TEXT("FogTime"));
	const FName FarCloudsAName(TEXT("FarCloudsA"));
	const FName FarCloudsBName(TEXT("FarCloudsB"));

	// Well inside the dome and well past the near plane. The composite draws without a depth test, so
	// the radius only has to keep the camera inside it.
	constexpr float CompositeRadius = 1000000.f;
}

bool UMobSkySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

void UMobSkySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Extension = FSceneViewExtensions::NewExtension<FMobCloudViewExtension>(GetWorld(), this);
}

void UMobSkySubsystem::Deinitialize()
{
	Extension.Reset();
	if (CompositeOwner)
	{
		CompositeOwner->Destroy();
		CompositeOwner = nullptr;
	}
	Composite = nullptr;
	Clouds.Reset();
	Skies.Reset();
	Super::Deinitialize();
}

void UMobSkySubsystem::HideForView(FSceneView& View, bool bRenderPassView) const
{
	if (!bRenderPassView)
	{
		if (Composite)
		{
			View.HiddenPrimitives.Add(Composite->GetPrimitiveSceneId());
		}
		return;
	}

	// Volumes are not in the pass: they stay where they are put, cover little of the screen, and keep
	// marching in their own material.
	for (const TWeakObjectPtr<UMobCloudComponent>& Cloud : Clouds)
	{
		if (const UMobCloudComponent* Component = Cloud.Get(); Component && Component->Shape == EMobCloudShape::Layer)
		{
			View.HiddenPrimitives.Add(Component->GetPrimitiveSceneId());
		}
	}
}

bool UMobSkySubsystem::GatherCloudFrame(FMobCloudFrame& Out, FIntPoint Size) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobSkySubsystem::GatherCloudFrame);

	UWorld* World = GetWorld();
	const UMobSkySettings* Settings = GetDefault<UMobSkySettings>();
	UTextureRenderTarget2D* ColorTarget = Settings->ColorTarget.LoadSynchronous();
	UTextureRenderTarget2D* DepthTarget = Settings->DepthTarget.LoadSynchronous();
	UVolumeTexture* Shape = Settings->ShapeTexture.LoadSynchronous();
	UVolumeTexture* Detail = Settings->DetailTexture.LoadSynchronous();
	UTexture2D* Weather = Settings->WeatherTexture.LoadSynchronous();
	if (!World || !ColorTarget || !DepthTarget || !Shape || !Detail || !Weather)
	{
		return false;
	}

	// Textures are still being built for a while after an uncooked start, and a resource bound before
	// it is real is a crash on the RHI thread rather than a black frame.
	for (const UTexture* Texture : { static_cast<const UTexture*>(Shape), static_cast<const UTexture*>(Detail),
		static_cast<const UTexture*>(Weather) })
	{
#if WITH_EDITOR
		if (Texture->IsCompiling())
		{
			return false;
		}
#endif
		if (!Texture->GetResource() || Texture->IsDefaultTexture())
		{
			return false;
		}
	}

	// Nearest the camera first, which is the order a ray meets slabs in whichever way it points.
	TArray<const UMobCloudComponent*, TInlineAllocator<MobCloudRendering::MaxLayers>> Layers;
	for (const TWeakObjectPtr<UMobCloudComponent>& Cloud : Clouds)
	{
		const UMobCloudComponent* Component = Cloud.Get();
		if (Component && Component->Shape == EMobCloudShape::Layer && Component->IsVisible()
			&& Layers.Num() < MobCloudRendering::MaxLayers)
		{
			Layers.Add(Component);
		}
	}
	if (Layers.IsEmpty())
	{
		return false;
	}

	const float ViewZ = static_cast<float>(LastView.Z);
	Layers.Sort([ViewZ](const UMobCloudComponent& A, const UMobCloudComponent& B)
	{
		return FMath::Abs((A.BaseHeight + A.TopHeight) * 0.5f - ViewZ) < FMath::Abs((B.BaseHeight + B.TopHeight) * 0.5f - ViewZ);
	});

	Out.LayerCount = Layers.Num();
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FMobCloudPacked Packed = Layers[Index]->Pack();
		Out.A[Index] = FVector4f(Packed.A);
		Out.B[Index] = FVector4f(Packed.B);
		Out.C[Index] = FVector4f(Packed.C);
		Out.D[Index] = FVector4f(Packed.D);
		Out.E[Index] = FVector4f(Packed.E);
		Out.F[Index] = FVector4f(Packed.F);
		Out.G[Index] = FVector4f(Packed.G);
		Out.Scatter[Index] = FVector4f(Packed.Scatter);
		Out.Emissive[Index] = FVector4f(Packed.Emissive);
		Out.Flags[Index] = FVector4f(Packed.Flags);
	}

	if (UMaterialParameterCollection* Fog = GetDefault<UMobFogSettings>()->ParameterCollection.LoadSynchronous())
	{
		Out.KeyDir = FVector4f(UKismetMaterialLibrary::GetVectorParameterValue(World, Fog, KeyDirectionName));
		Out.KeyColor = FVector4f(UKismetMaterialLibrary::GetVectorParameterValue(World, Fog, KeyColorName));
		Out.Ambient = FVector4f(UKismetMaterialLibrary::GetVectorParameterValue(World, Fog, AmbientColorName));
		Out.Time = UKismetMaterialLibrary::GetScalarParameterValue(World, Fog, FogTimeName);
	}
	if (UMaterialParameterCollection* SkyCollection = Settings->ParameterCollection.LoadSynchronous())
	{
		Out.Haze = FVector4f(UKismetMaterialLibrary::GetVectorParameterValue(World, SkyCollection, HazeColorName));
	}

	if (ColorTarget->SizeX != Size.X || ColorTarget->SizeY != Size.Y)
	{
		ColorTarget->ResizeTarget(Size.X, Size.Y);
	}
	if (DepthTarget->SizeX != Size.X || DepthTarget->SizeY != Size.Y)
	{
		DepthTarget->ResizeTarget(Size.X, Size.Y);
	}

	Out.Shape = Shape->GetResource();
	Out.Detail = Detail->GetResource();
	Out.Weather = Weather->GetResource();
	Out.ColorTarget = ColorTarget->GameThread_GetRenderTargetResource();
	Out.DepthTarget = DepthTarget->GameThread_GetRenderTargetResource();
	Out.Size = Size;
	return true;
}

void UMobSkySubsystem::TickComposite(const FVector& View)
{
	UWorld* World = GetWorld();
	const bool bWanted = MobCloudRendering::IsEnabled() && Clouds.ContainsByPredicate(
		[](const TWeakObjectPtr<UMobCloudComponent>& Cloud) { return Cloud.IsValid() && Cloud->Shape == EMobCloudShape::Layer; });

	if (!Composite && bWanted && World)
	{
		UMaterialInterface* Material = GetDefault<UMobSkySettings>()->CompositeMaterial.LoadSynchronous();
		UStaticMesh* Mesh = UMobSkySettings::GetProxyMesh();
		if (!Material || !Mesh)
		{
			return;
		}

		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
#if WITH_EDITOR
		Params.bHideFromSceneOutliner = true;
#endif
		CompositeOwner = World->SpawnActor<AActor>(Params);
		if (!CompositeOwner)
		{
			return;
		}

		Composite = NewObject<UStaticMeshComponent>(CompositeOwner, TEXT("MobCloudComposite"), RF_Transient);
		Composite->SetStaticMesh(Mesh);
		Composite->SetMaterial(0, Material);
		Composite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Composite->CastShadow = false;
		Composite->bNeverDistanceCull = true;
		Composite->bVisibleInReflectionCaptures = false;
		Composite->bVisibleInRayTracing = false;

		// Over the sky dome and under everything else translucent, as the marching layers were.
		Composite->TranslucencySortPriority = 10;
		Composite->SetUsingAbsoluteLocation(true);
		Composite->SetUsingAbsoluteRotation(true);
		Composite->SetUsingAbsoluteScale(true);
		Composite->SetWorldScale3D(FVector(CompositeRadius));
		CompositeOwner->SetRootComponent(Composite);
		Composite->RegisterComponent();
	}

	if (Composite)
	{
		Composite->SetVisibility(bWanted && LastCloudsEnabled != 0);
		if (!Composite->GetComponentLocation().Equals(View, 1.f))
		{
			Composite->SetWorldLocation(View);
		}
	}
}


TStatId UMobSkySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMobSkySubsystem, STATGROUP_Tickables);
}

void UMobSkySubsystem::RegisterCloud(UMobCloudComponent* Cloud)
{
	Clouds.AddUnique(Cloud);

	// The tick only acts on a change, so one that arrives after the toggle has to honour it here.
	if (Cloud && MobSkyDebug::CVarClouds.GetValueOnGameThread() == 0)
	{
		Cloud->SetVisibility(false);
	}
}

void UMobSkySubsystem::UnregisterCloud(UMobCloudComponent* Cloud)
{
	Clouds.Remove(Cloud);
}

void UMobSkySubsystem::RegisterSky(UMobSkyComponent* Sky)
{
	Skies.AddUnique(Sky);

	if (Sky && MobSkyDebug::CVarSky.GetValueOnGameThread() == 0)
	{
		Sky->SetVisibility(false);
	}
}

void UMobSkySubsystem::UnregisterSky(UMobSkyComponent* Sky)
{
	Skies.Remove(Sky);
}

UMobSkyComponent* UMobSkySubsystem::GetSky() const
{
	for (const TWeakObjectPtr<UMobSkyComponent>& Sky : Skies)
	{
		if (UMobSkyComponent* Found = Sky.Get())
		{
			return Found;
		}
	}
	return nullptr;
}

bool UMobSkySubsystem::GetViewLocation(FVector& OutLocation) const
{
	// The fog subsystem already answers this for the editor viewport and the player camera alike.
	const UWorld* World = GetWorld();
	const UMobFogSubsystem* Fog = World ? World->GetSubsystem<UMobFogSubsystem>() : nullptr;
	return Fog && Fog->GetViewLocation(OutLocation);
}

void UMobSkySubsystem::TickVisibility()
{
	const int32 CloudsEnabled = MobSkyDebug::CVarClouds.GetValueOnGameThread() != 0 ? 1 : 0;
	if (CloudsEnabled != LastCloudsEnabled)
	{
		LastCloudsEnabled = CloudsEnabled;
		for (const TWeakObjectPtr<UMobCloudComponent>& Cloud : Clouds)
		{
			if (UMobCloudComponent* Component = Cloud.Get())
			{
				Component->SetVisibility(CloudsEnabled != 0);
			}
		}
	}

	const int32 SkyEnabled = MobSkyDebug::CVarSky.GetValueOnGameThread() != 0 ? 1 : 0;
	if (SkyEnabled != LastSkyEnabled)
	{
		LastSkyEnabled = SkyEnabled;
		for (const TWeakObjectPtr<UMobSkyComponent>& Sky : Skies)
		{
			if (UMobSkyComponent* Component = Sky.Get())
			{
				Component->SetVisibility(SkyEnabled != 0);
			}
		}
	}
}

void UMobSkySubsystem::PublishAmbient(const UMobSkyComponent& Sky, float ViewZ) const
{
	UMaterialParameterCollection* Collection = GetDefault<UMobFogSettings>()->ParameterCollection.LoadSynchronous();
	UWorld* World = GetWorld();
	if (!Collection || !World)
	{
		return;
	}

	// The key light is whatever the fog subsystem found, so the sky and the fog agree on where the sun is.
	const FLinearColor Key = UKismetMaterialLibrary::GetVectorParameterValue(World, Collection, KeyDirectionName);
	const float Daylight = FMath::SmoothStep(-0.12f, 0.12f, Key.B);
	const float Air = Sky.GetAirAmount(ViewZ);

	const FLinearColor Lit = FMath::Lerp(Sky.NightAmbient, Sky.DayAmbient, Daylight);
	const FLinearColor Ambient = FMath::Lerp(Sky.SpaceAmbient, Lit, Air);
	UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, AmbientColorName,
		FLinearColor(Ambient.R, Ambient.G, Ambient.B, 1.f));

	// The horizon as the dome draws it, which is what a distant cloud fades into.
	if (UMaterialParameterCollection* SkyCollection = GetDefault<UMobSkySettings>()->ParameterCollection.LoadSynchronous())
	{
		const FLinearColor KeyColor = UKismetMaterialLibrary::GetVectorParameterValue(World, Collection, KeyColorName);
		const FLinearColor Haze = FMath::Lerp(Sky.Night, Sky.Horizon * KeyColor, Daylight);
		UKismetMaterialLibrary::SetVectorParameterValue(World, SkyCollection, HazeColorName,
			FLinearColor(Haze.R, Haze.G, Haze.B, Air));
	}
}

void UMobSkySubsystem::PublishFarClouds(const FVector& View) const
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* Collection = GetDefault<UMobSkySettings>()->ParameterCollection.LoadSynchronous();
	if (!World || !Collection)
	{
		return;
	}

	// The heaviest deck is the one seen from orbit; a thin sheet of cirrus above it is not.
	const UMobCloudComponent* Deck = nullptr;
	float Heaviest = 0.f;
	for (const TWeakObjectPtr<UMobCloudComponent>& Cloud : Clouds)
	{
		const UMobCloudComponent* Component = Cloud.Get();
		if (!Component || Component->Shape != EMobCloudShape::Layer || !Component->IsVisible())
		{
			continue;
		}
		const float Weight = (Component->TopHeight - Component->BaseHeight) * Component->Density;
		if (Weight > Heaviest)
		{
			Heaviest = Weight;
			Deck = Component;
		}
	}

	if (!Deck)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, FarCloudsAName, FLinearColor::Transparent);
		return;
	}

	// The layer fades out over the last third of its reach, and the painted deck fades in over the same.
	const float Reach = Deck->MaxDistance + FMath::Max(static_cast<float>(View.Z) - Deck->TopHeight, 0.f) * Deck->ReachPerHeight;
	UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, FarCloudsAName,
		FLinearColor(Deck->CoverageScale, Deck->Coverage, Deck->MacroScale, Deck->MacroAmount));
	UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, FarCloudsBName,
		FLinearColor(Deck->Wind.X, Deck->Wind.Y, Reach * 0.7f, (Deck->BaseHeight + Deck->TopHeight) * 0.5f));
}

void UMobSkySubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobSkySubsystem::Tick);

	Super::Tick(DeltaTime);
	TickVisibility();

	FVector View;
	if (!GetViewLocation(View))
	{
		return;
	}
	LastView = View;
	TickComposite(View);

	for (int32 Index = Clouds.Num() - 1; Index >= 0; --Index)
	{
		if (UMobCloudComponent* Cloud = Clouds[Index].Get())
		{
			Cloud->FitToView(View);
		}
		else
		{
			Clouds.RemoveAtSwap(Index);
		}
	}

	for (int32 Index = Skies.Num() - 1; Index >= 0; --Index)
	{
		if (UMobSkyComponent* Sky = Skies[Index].Get())
		{
			Sky->FollowView(View);
			Sky->TickBake(DeltaTime);
		}
		else
		{
			Skies.RemoveAtSwap(Index);
		}
	}

	if (const UMobSkyComponent* Sky = GetSky(); Sky && Sky->bDrivesAmbient)
	{
		// The ambient lights the media, and the media are down in the air however high the camera is:
		// a cloud seen from orbit still sits under a blue sky.
		float MediaTop = static_cast<float>(View.Z);
		for (const TWeakObjectPtr<UMobCloudComponent>& Cloud : Clouds)
		{
			if (const UMobCloudComponent* Component = Cloud.Get(); Component && Component->Shape == EMobCloudShape::Layer)
			{
				MediaTop = FMath::Min(MediaTop, Component->TopHeight);
			}
		}
		PublishAmbient(*Sky, MediaTop);
	}

	PublishFarClouds(View);
}
