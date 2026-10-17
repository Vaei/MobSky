// Copyright (c) Jared Taylor

#include "MobCloudComponent.h"

#include "MobSkySettings.h"
#include "MobSkySubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UMobCloudComponent::UMobCloudComponent()
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
	bVisibleInReflectionCaptures = false;
	bVisibleInSceneCaptureOnly = false;

	// A layer is kept on the camera in world space, so it must not inherit where its owner is.
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);

	// Over the sky dome, which sorts far behind everything.
	TranslucencySortPriority = 10;

	// The bound follows the camera and is huge, so culling it by its bounds only ever gets it wrong.
	BoundsScale = 1.f;
	bNeverDistanceCull = true;
}

void UMobCloudComponent::OnRegister()
{
	Super::OnRegister();
	RefreshClouds();

	if (UWorld* World = GetWorld())
	{
		if (UMobSkySubsystem* Subsystem = World->GetSubsystem<UMobSkySubsystem>())
		{
			Subsystem->RegisterCloud(this);
		}
	}
}

void UMobCloudComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UMobSkySubsystem* Subsystem = World->GetSubsystem<UMobSkySubsystem>())
		{
			Subsystem->UnregisterCloud(this);
		}
	}

	Super::OnUnregister();
}

#if WITH_EDITOR
void UMobCloudComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshClouds();
}
#endif

int32 UMobCloudComponent::WantedVariant() const
{
	int32 Variant = 0;
	Variant |= bDetail ? MobCloudVariant::Detail : 0;
	Variant |= bShadow ? MobCloudVariant::Shadow : 0;
	Variant |= Shape == EMobCloudShape::Volume ? MobCloudVariant::Volume : 0;
	return Variant;
}

FVector UMobCloudComponent::ComputeProxyScale() const
{
	if (Shape == EMobCloudShape::Volume)
	{
		return Extent.ComponentMax(FVector(1.f));
	}

	// An ellipsoid that contains the cylinder the layer is marched in: root two on each axis is the
	// smallest one that reaches the cylinder's rim at its top and bottom.
	const float Half = FMath::Max((TopHeight - BaseHeight) * 0.5f, 100.f);
	const float Reach = FMath::Max(MaxDistance, 10000.f);
	return FVector(Reach, Reach, Half) * UE_SQRT_2;
}

void UMobCloudComponent::RefreshClouds()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobCloudComponent::RefreshClouds);

	if (UStaticMesh* Mesh = UMobSkySettings::GetProxyMesh(); Mesh && Mesh != GetStaticMesh())
	{
		SetStaticMesh(Mesh);
	}

	ApplyMaterial();
	ApplyParameters();

	if (Shape == EMobCloudShape::Volume)
	{
		SetUsingAbsoluteLocation(false);
		SetUsingAbsoluteRotation(false);
		SetUsingAbsoluteScale(false);
		SetRelativeScale3D(ComputeProxyScale());
	}
	else
	{
		SetUsingAbsoluteLocation(true);
		SetUsingAbsoluteRotation(true);
		SetUsingAbsoluteScale(true);
		SetWorldRotation(FQuat::Identity);
		SetWorldScale3D(ComputeProxyScale());
	}
}

void UMobCloudComponent::ApplyMaterial()
{
	UMaterialInterface* Parent = MaterialOverride
		? ToRawPtr(MaterialOverride) : UMobSkySettings::GetCloudMaterial(WantedVariant());
	if (!Parent)
	{
		return;
	}

	if (!Instance || InstanceParent != Parent)
	{
		Instance = UMaterialInstanceDynamic::Create(Parent, this);
		InstanceParent = Parent;
	}

	if (GetMaterial(0) != Instance)
	{
		SetMaterial(0, Instance);
	}
}

FMobCloudPacked UMobCloudComponent::Pack() const
{
	const bool bVolume = Shape == EMobCloudShape::Volume;
	const FVector Bound = bVolume ? ComputeProxyScale() : FVector(1.f);

	FMobCloudPacked Packed;
	Packed.A = FLinearColor(BaseHeight, FMath::Max(TopHeight, BaseHeight + 100.f),
		FMath::Max(Density, 0.f), FMath::Clamp(Coverage, 0.f, 1.f));
	Packed.B = FLinearColor(FMath::Max(CoverageScale, 1000.f), FMath::Max(ShapeScale, 1000.f),
		FMath::Max(DetailScale, 100.f), bDetail ? DetailAmount : 0.f);
	Packed.C = FLinearColor(Wind.X, Wind.Y, BottomFade, TopFade);
	Packed.D = FLinearColor(MaxDistance, ReachPerHeight, static_cast<float>(FMath::Clamp(StepCount, 4, 96)), PlanetRadius);
	Packed.E = FLinearColor(PhaseForward, PhaseBack, PhaseBlend, AmbientBottom);
	Packed.F = FLinearColor(ShadowDistance, EdgeFade, HazeDistance, Powder);
	Packed.G = FLinearColor(FMath::Max(MacroScale, 100000.f), bVolume ? 0.f : MacroAmount, bVolume ? 0.f : Shear, bVolume ? 0.f : Variety);
	Packed.Scatter = ScatterColor;
	Packed.Emissive = EmissiveColor;
	Packed.Extent = FLinearColor(Bound.X, Bound.Y, Bound.Z, 0.f);
	Packed.Flags = FLinearColor(bDetail ? 1.f : 0.f, bShadow ? 1.f : 0.f, 0.f, 0.f);
	return Packed;
}

void UMobCloudComponent::ApplyParameters()
{
	if (!Instance)
	{
		return;
	}

	using namespace MobSkyParams;
	const FMobCloudPacked Packed = Pack();
	Instance->SetVectorParameterValue(CloudA, Packed.A);
	Instance->SetVectorParameterValue(CloudB, Packed.B);
	Instance->SetVectorParameterValue(CloudC, Packed.C);
	Instance->SetVectorParameterValue(CloudD, Packed.D);
	Instance->SetVectorParameterValue(CloudE, Packed.E);
	Instance->SetVectorParameterValue(CloudF, Packed.F);
	Instance->SetVectorParameterValue(CloudG, Packed.G);
	Instance->SetVectorParameterValue(MobSkyParams::ScatterColor, Packed.Scatter);
	Instance->SetVectorParameterValue(MobSkyParams::EmissiveColor, Packed.Emissive);
	Instance->SetVectorParameterValue(MobSkyParams::Extent, Packed.Extent);
}

void UMobCloudComponent::FitToView(const FVector& ViewLocation)
{
	if (Shape != EMobCloudShape::Layer)
	{
		return;
	}

	const FVector Centre(ViewLocation.X, ViewLocation.Y, (BaseHeight + TopHeight) * 0.5f);

	// The reach grows as the camera climbs above the layer, and the bound has to grow with it.
	const float Above = FMath::Max(ViewLocation.Z - TopHeight, 0.f);
	FVector Scale = ComputeProxyScale();
	const float Reach = (MaxDistance + Above * ReachPerHeight) * UE_SQRT_2;
	Scale.X = Reach;
	Scale.Y = Reach;

	if (!GetComponentLocation().Equals(Centre, 50.f) || !GetComponentScale().Equals(Scale, 1.f))
	{
		SetWorldLocation(Centre);
		SetWorldScale3D(Scale);
	}
}

void UMobCloudComponent::SetCoverage(float NewCoverage)
{
	Coverage = FMath::Clamp(NewCoverage, 0.f, 1.f);
	ApplyParameters();
}

void UMobCloudComponent::SetDensity(float NewDensity)
{
	Density = FMath::Max(NewDensity, 0.f);
	ApplyParameters();
}
