// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobSkyEditorLibrary.generated.h"

/** What author_sky.py needs from C++: the editor's Python cannot write voxels into a volume texture. */
UCLASS()
class MOBSKYEDITOR_API UMobSkyEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Writes the two tiling volume textures the clouds and nebulae march through.
	 *
	 * T_MobCloudShape, 128 cubed: Perlin-Worley in red, Worley octaves in green, blue and alpha.
	 * T_MobCloudDetail, 32 cubed: three Worley octaves. Both uncompressed and linear, because they
	 * are density rather than colour.
	 */
	UFUNCTION(BlueprintCallable, Category="Mob Sky")
	static bool BuildNoiseTextures();
};
