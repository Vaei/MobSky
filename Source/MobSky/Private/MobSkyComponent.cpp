// Copyright (c) Jared Taylor

#include "MobSkyComponent.h"

#include "MobCloudTypes.h"
#include "MobSkySettings.h"
#include "MobSkySubsystem.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	// Air thins roughly exponentially with height, so most of it is gone well before the top.
	constexpr float AirFalloff = 4.f;

	TAutoConsoleVariable<int32> CVarBake(
		TEXT("mob.Sky.Bake"),
		1,
		TEXT("Bakes space into a cube at BeginPlay. 0 marches it live every frame, which is the A and B of what the bake saves."),
		ECVF_Cheat);
}

UMobSkyComponent::UMobSkyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	Mobility = EComponentMobility::Movable;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionProfileName(TEXT("NoCollision"));
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bCastStaticShadow = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bUseAsOccluder = false;
	bReceivesDecals = false;
	bVisibleInRayTracing = false;
	bNeverDistanceCull = true;

	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);

	// Behind every other translucent thing, clouds included.
	TranslucencySortPriority = -1000;

	Nebulae.Add(FMobSkyNebula());

	FMobSkyNebula Second;
	Second.Yaw = 140.f;
	Second.Pitch = -15.f;
	Second.Radius = 8.f;
	Second.ColorA = FLinearColor(0.2f, 0.9f, 0.6f);
	Second.ColorB = FLinearColor(0.9f, 0.6f, 0.2f);
	Second.Brightness = 1.f;
	Second.Seed = 17.f;
	Nebulae.Add(Second);
}

void UMobSkyComponent::OnRegister()
{
	Super::OnRegister();
	RefreshSky();

	if (UWorld* World = GetWorld())
	{
		if (UMobSkySubsystem* Subsystem = World->GetSubsystem<UMobSkySubsystem>())
		{
			Subsystem->RegisterSky(this);
		}
	}
}

void UMobSkyComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UMobSkySubsystem* Subsystem = World->GetSubsystem<UMobSkySubsystem>())
		{
			Subsystem->UnregisterSky(this);
		}
	}

	Super::OnUnregister();
}

void UMobSkyComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bBakeSpace && CVarBake.GetValueOnGameThread() != 0)
	{
		BakeSpace();
	}
}

void UMobSkyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Capture)
	{
		Capture->DestroyComponent();
		Capture = nullptr;
	}
	if (SpaceDome)
	{
		SpaceDome->DestroyComponent();
		SpaceDome = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UMobSkyComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshSky();
}
#endif

UMaterialInstanceDynamic* UMobSkyComponent::MakeInstance(UMaterialInterface* Parent)
{
	return Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
}

void UMobSkyComponent::RefreshSky()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobSkyComponent::RefreshSky);

	if (UStaticMesh* Mesh = UMobSkySettings::GetProxyMesh(); Mesh && Mesh != GetStaticMesh())
	{
		SetStaticMesh(Mesh);
	}
	SetWorldScale3D(FVector(DomeRadius));

	// Keeps the baked instance once a bake has made one; everything else starts on the live march.
	if (!Instance)
	{
		Instance = MakeInstance(GetDefault<UMobSkySettings>()->SkyMaterial.LoadSynchronous());
	}

	if (Instance && GetMaterial(0) != Instance)
	{
		SetMaterial(0, Instance);
	}

	LastState = FLinearColor(-1.f, -1.f, -1.f, -1.f);
	ApplyParameters(Instance);

	if (SpaceInstance)
	{
		ApplyParameters(SpaceInstance);
		BakeRetakes = Capture ? 8 : 0;
		BakeClock = 0.f;
	}
}

void UMobSkyComponent::BakeSpace()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobSkyComponent::BakeSpace);

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	const UMobSkySettings* Settings = GetDefault<UMobSkySettings>();
	UMaterialInterface* SpaceParent = Settings->SpaceMaterial.LoadSynchronous();
	UMaterialInterface* BakedParent = Settings->SkyBakedMaterial.LoadSynchronous();
	if (!World || !World->IsGameWorld() || !Owner || !SpaceParent || !BakedParent)
	{
		return;
	}

	if (!SpaceTarget)
	{
		SpaceTarget = NewObject<UTextureRenderTargetCube>(this);
		SpaceTarget->Init(BakeResolution, PF_FloatRGBA);
		SpaceTarget->UpdateResource();
	}

	if (!SpaceDome)
	{
		SpaceInstance = MakeInstance(SpaceParent);

		// Seen only by the capture, so it never draws in the game view.
		SpaceDome = NewObject<UStaticMeshComponent>(Owner, TEXT("MobSkySpaceDome"), RF_Transient);
		SpaceDome->SetStaticMesh(GetStaticMesh());
		SpaceDome->SetMaterial(0, SpaceInstance);
		SpaceDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SpaceDome->CastShadow = false;
		SpaceDome->bVisibleInSceneCaptureOnly = true;
		SpaceDome->bNeverDistanceCull = true;
		SpaceDome->SetUsingAbsoluteLocation(true);
		SpaceDome->SetUsingAbsoluteRotation(true);
		SpaceDome->SetUsingAbsoluteScale(true);
		SpaceDome->SetWorldLocationAndRotation(GetComponentLocation(), FQuat::Identity);
		SpaceDome->SetWorldScale3D(FVector(DomeRadius));
		SpaceDome->RegisterComponent();
	}
	ApplyParameters(SpaceInstance);

	if (!Capture)
	{
		Capture = NewObject<USceneCaptureComponentCube>(Owner, TEXT("MobSkySpaceCapture"), RF_Transient);
		Capture->TextureTarget = SpaceTarget;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->ShowOnlyComponents.Add(SpaceDome);
		Capture->CaptureSource = SCS_SceneColorHDR;
		Capture->ShowFlags.SetFog(false);
		Capture->ShowFlags.SetAtmosphere(false);
		Capture->SetUsingAbsoluteLocation(true);
		Capture->SetWorldLocation(GetComponentLocation());
		Capture->RegisterComponent();
	}
	BakeRetakes = 8;
	BakeClock = 0.f;

	Instance = MakeInstance(BakedParent);
	if (Instance)
	{
		Instance->SetTextureParameterValue(MobSkyParams::SpaceCube, SpaceTarget);
		SetMaterial(0, Instance);
		LastState = FLinearColor(-1.f, -1.f, -1.f, -1.f);
		ApplyParameters(Instance);
	}
}

float UMobSkyComponent::GetAirAmount(float WorldZ) const
{
	const float Span = FMath::Max(VacuumZ - GroundZ, 1.f);
	const float Height = FMath::Clamp((WorldZ - GroundZ) / Span, 0.f, 1.f);
	const float Floor = FMath::Exp(-AirFalloff);
	return FMath::Clamp((FMath::Exp(-AirFalloff * Height) - Floor) / (1.f - Floor), 0.f, 1.f);
}

void UMobSkyComponent::ApplyParameters(UMaterialInstanceDynamic* Target) const
{
	if (!Target)
	{
		return;
	}

	using namespace MobSkyParams;

	Target->SetVectorParameterValue(SkyZenith, Zenith);
	Target->SetVectorParameterValue(SkyHorizon, Horizon);
	Target->SetVectorParameterValue(SkySunset, Sunset);
	Target->SetVectorParameterValue(SkyNight, Night);
	Target->SetVectorParameterValue(SkyGround, Ground);
	Target->SetVectorParameterValue(SunA, FLinearColor(
		FMath::Cos(FMath::DegreesToRadians(SunRadius)), SunIntensity, HaloExponent, HaloIntensity));
	Target->SetVectorParameterValue(AtmosA, FLinearColor(HorizonExponent, LimbWidth, LimbIntensity, 0.f));

	Target->SetVectorParameterValue(StarsA, FLinearColor(FaintStarCells, FaintStarChance, BrightStarCells, BrightStarChance));
	Target->SetVectorParameterValue(StarsB, FLinearColor(StarBrightness, StarMagnitude, Twinkle, 0.f));

	const FQuat Frame = FQuat(FRotator(0.f, GalaxyYaw, 0.f))
		* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(GalaxyTilt))
		* FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(GalaxySpin));
	const FVector AxisA = Frame.RotateVector(FVector::XAxisVector);
	const FVector AxisB = Frame.RotateVector(FVector::YAxisVector);
	const FVector Normal = Frame.RotateVector(FVector::ZAxisVector);
	Target->SetVectorParameterValue(GalaxyAxisA, FLinearColor(AxisA.X, AxisA.Y, AxisA.Z, 0.f));
	Target->SetVectorParameterValue(GalaxyAxisB, FLinearColor(AxisB.X, AxisB.Y, AxisB.Z, 0.f));
	Target->SetVectorParameterValue(GalaxyNormal, FLinearColor(Normal.X, Normal.Y, Normal.Z, 0.f));
	Target->SetVectorParameterValue(GalaxyB, FLinearColor(GalaxyBrightness, GalaxyDust, GalaxyArms, GalaxyArmTightness));
	Target->SetVectorParameterValue(GalaxyC, FLinearColor(GalaxyBulge, static_cast<float>(GalaxySteps), 0.f, 0.f));
	Target->SetVectorParameterValue(MobSkyParams::GalaxyDiscColor, GalaxyDiscColor);
	Target->SetVectorParameterValue(MobSkyParams::GalaxyBulgeColor, GalaxyBulgeColor);
	Target->SetVectorParameterValue(DistantA, FLinearColor(DistantGalaxyCells, DistantGalaxyChance,
		DistantGalaxyBrightness, FMath::DegreesToRadians(DistantGalaxySize)));

	for (int32 Index = 0; Index < MaxNebulae; ++Index)
	{
		const FMobSkyNebula* Nebula = Nebulae.IsValidIndex(Index) && Nebulae[Index].bEnabled ? &Nebulae[Index] : nullptr;
		const FVector Dir = Nebula ? FRotator(Nebula->Pitch, Nebula->Yaw, 0.f).Vector() : FVector::XAxisVector;

		Target->SetVectorParameterValue(MobSkyParams::Nebula(Index, TEXT("Placement")), FLinearColor(Dir.X, Dir.Y, Dir.Z,
			Nebula ? FMath::DegreesToRadians(Nebula->Radius) : 0.1f));
		Target->SetVectorParameterValue(MobSkyParams::Nebula(Index, TEXT("ColorA")), Nebula ? Nebula->ColorA : FLinearColor::Black);
		Target->SetVectorParameterValue(MobSkyParams::Nebula(Index, TEXT("ColorB")), Nebula ? Nebula->ColorB : FLinearColor::Black);
		Target->SetVectorParameterValue(MobSkyParams::Nebula(Index, TEXT("Params")), Nebula
			? FLinearColor(Nebula->Brightness, Nebula->Dust, Nebula->NoiseScale, Nebula->Seed)
			: FLinearColor(0.f, 0.f, 1.f, 0.f));
	}

	Target->SetVectorParameterValue(SkyState, FLinearColor(1.f, 0.f, PlanetRadius, FMath::DegreesToRadians(SpaceYaw)));
}

void UMobSkyComponent::TickBake(float DeltaTime)
{
	if (!Capture || !SpaceInstance || BakeRetakes <= 0)
	{
		return;
	}

	// Shaders compile on demand where they are not cooked, and only once the capture first draws the
	// dome, so the first capture can come back empty. It is taken a few times over the first seconds,
	// and once more whenever a compile finishes.
	const bool bCompiling = SpaceInstance->IsCompiling();
	const bool bFinishedCompiling = bWasCompiling && !bCompiling;
	bWasCompiling = bCompiling;

	BakeClock -= DeltaTime;
	if (BakeClock > 0.f && !bFinishedCompiling)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(MobSkyComponent::TickBake);

	Capture->CaptureSceneDeferred();
	BakeClock = 0.5f;
	--BakeRetakes;
}

void UMobSkyComponent::FollowView(const FVector& ViewLocation)
{
	if (!GetComponentLocation().Equals(ViewLocation, 1.f))
	{
		SetWorldLocation(ViewLocation);
	}

	const float Air = GetAirAmount(ViewLocation.Z);
	const FLinearColor State(Air, ViewLocation.Z - GroundZ, PlanetRadius, FMath::DegreesToRadians(SpaceYaw));
	if (Instance && !State.Equals(LastState, 1e-4f))
	{
		LastState = State;
		Instance->SetVectorParameterValue(MobSkyParams::SkyState, State);
	}
}
