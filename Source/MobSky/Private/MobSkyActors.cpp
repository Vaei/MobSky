// Copyright (c) Jared Taylor

#include "MobSkyActors.h"

#include "MobCloudComponent.h"
#include "MobSkyComponent.h"

AMobSky::AMobSky()
{
	PrimaryActorTick.bCanEverTick = false;

	Sky = CreateDefaultSubobject<UMobSkyComponent>(TEXT("Sky"));
	RootComponent = Sky;
}

AMobCloud::AMobCloud()
{
	PrimaryActorTick.bCanEverTick = false;

	// A scene root, so a volume can be moved while a layer's proxy stays on the camera.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Cloud = CreateDefaultSubobject<UMobCloudComponent>(TEXT("Cloud"));
	Cloud->SetupAttachment(RootComponent);
}
