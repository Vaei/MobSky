// Copyright (c) Jared Taylor

#include "MobCloudTypes.h"

FString MobCloudVariant::InstanceName(int32 Variant)
{
	FString Name = (Variant & Volume) ? TEXT("MI_MobCloud_Volume") : TEXT("MI_MobCloud_Layer");
	if (Variant & Detail)
	{
		Name += TEXT("_Detail");
	}
	if (Variant & Shadow)
	{
		Name += TEXT("_Shadow");
	}
	return Name;
}

namespace MobSkyParams
{
	const FName CloudA(TEXT("CloudA"));
	const FName CloudB(TEXT("CloudB"));
	const FName CloudC(TEXT("CloudC"));
	const FName CloudD(TEXT("CloudD"));
	const FName CloudE(TEXT("CloudE"));
	const FName CloudF(TEXT("CloudF"));
	const FName CloudG(TEXT("CloudG"));
	const FName ScatterColor(TEXT("ScatterColor"));
	const FName EmissiveColor(TEXT("EmissiveColor"));
	const FName Extent(TEXT("Extent"));

	const FName SkyZenith(TEXT("SkyZenith"));
	const FName SkyHorizon(TEXT("SkyHorizon"));
	const FName SkySunset(TEXT("SkySunset"));
	const FName SkyNight(TEXT("SkyNight"));
	const FName SkyGround(TEXT("SkyGround"));
	const FName SunA(TEXT("SunA"));
	const FName AtmosA(TEXT("AtmosA"));
	const FName SkyState(TEXT("SkyState"));
	const FName StarsA(TEXT("StarsA"));
	const FName StarsB(TEXT("StarsB"));
	const FName GalaxyAxisA(TEXT("GalaxyAxisA"));
	const FName GalaxyAxisB(TEXT("GalaxyAxisB"));
	const FName GalaxyNormal(TEXT("GalaxyNormal"));
	const FName GalaxyB(TEXT("GalaxyB"));
	const FName GalaxyC(TEXT("GalaxyC"));
	const FName GalaxyDiscColor(TEXT("GalaxyDiscColor"));
	const FName GalaxyBulgeColor(TEXT("GalaxyBulgeColor"));
	const FName DistantA(TEXT("DistantA"));
	const FName SpaceCube(TEXT("SpaceCube"));

	FName Nebula(int32 Index, const TCHAR* Field)
	{
		return FName(*FString::Printf(TEXT("Nebula%d%s"), Index, Field));
	}
}
