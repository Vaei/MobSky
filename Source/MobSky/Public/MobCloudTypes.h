// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobCloudTypes.generated.h"

/** What a cloud component draws. */
UENUM(BlueprintType)
enum class EMobCloudShape : uint8
{
	/** A slab between two heights that follows the camera and reaches to the horizon. Weather. */
	Layer		UMETA(DisplayName = "Layer"),

	/** An ellipsoid that stays where it is put. A bank round a set piece, or a nebula. */
	Volume		UMETA(DisplayName = "Volume"),
};

/**
 * Which of the cloud materials to use.
 *
 * Detail and shadow are compile time choices because each is a set of texture taps inside the march
 * that the compiler cannot fold away unless it can see they are not wanted.
 */
namespace MobCloudVariant
{
	static constexpr int32 Detail = 1 << 0;
	static constexpr int32 Shadow = 1 << 1;
	static constexpr int32 Volume = 1 << 2;

	static constexpr int32 Num = 8;

	/** The instance name the generator gives this combination: MI_MobCloud_Layer_Detail_Shadow and so on. */
	MOBSKY_API FString InstanceName(int32 Variant);
}

/** Material parameter names, shared by the components and author_sky.py. */
namespace MobSkyParams
{
	MOBSKY_API extern const FName CloudA;
	MOBSKY_API extern const FName CloudB;
	MOBSKY_API extern const FName CloudC;
	MOBSKY_API extern const FName CloudD;
	MOBSKY_API extern const FName CloudE;
	MOBSKY_API extern const FName CloudF;
	MOBSKY_API extern const FName CloudG;
	MOBSKY_API extern const FName ScatterColor;
	MOBSKY_API extern const FName EmissiveColor;
	MOBSKY_API extern const FName Extent;

	MOBSKY_API extern const FName SkyZenith;
	MOBSKY_API extern const FName SkyHorizon;
	MOBSKY_API extern const FName SkySunset;
	MOBSKY_API extern const FName SkyNight;
	MOBSKY_API extern const FName SkyGround;
	MOBSKY_API extern const FName SunA;
	MOBSKY_API extern const FName AtmosA;
	MOBSKY_API extern const FName SkyState;
	MOBSKY_API extern const FName StarsA;
	MOBSKY_API extern const FName StarsB;
	MOBSKY_API extern const FName GalaxyAxisA;
	MOBSKY_API extern const FName GalaxyAxisB;
	MOBSKY_API extern const FName GalaxyNormal;
	MOBSKY_API extern const FName GalaxyB;
	MOBSKY_API extern const FName GalaxyC;
	MOBSKY_API extern const FName GalaxyDiscColor;
	MOBSKY_API extern const FName GalaxyBulgeColor;
	MOBSKY_API extern const FName DistantA;
	MOBSKY_API extern const FName SpaceCube;

	/** A nebula slot's parameter: Nebula0Placement, Nebula1ColorA and so on. */
	MOBSKY_API FName Nebula(int32 Index, const TCHAR* Field);

	static constexpr int32 MaxNebulae = 2;
}
