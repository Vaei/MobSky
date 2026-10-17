// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobSkyActors.generated.h"

class UMobCloudComponent;
class UMobSkyComponent;

/** The sky, placed in a level. Where it is placed does not matter: the dome follows the camera. */
UCLASS(ClassGroup=(Mob), HideCategories=(Input, Replication, Collision, HLOD, Physics, Networking, Cooking))
class MOBSKY_API AMobSky : public AActor
{
	GENERATED_BODY()

public:
	AMobSky();

	UMobSkyComponent* GetSky() const { return Sky; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sky", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UMobSkyComponent> Sky;
};

/** A cloud layer or volume, placed in a level. A layer ignores where it is placed; a volume is where it is placed. */
UCLASS(ClassGroup=(Mob), HideCategories=(Input, Replication, Collision, HLOD, Physics, Networking, Cooking))
class MOBSKY_API AMobCloud : public AActor
{
	GENERATED_BODY()

public:
	AMobCloud();

	UMobCloudComponent* GetCloud() const { return Cloud; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Clouds", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UMobCloudComponent> Cloud;
};
