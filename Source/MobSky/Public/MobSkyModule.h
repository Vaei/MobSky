// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

MOBSKY_API DECLARE_LOG_CATEGORY_EXTERN(LogMobSky, Log, All);

class FMobSkyModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
