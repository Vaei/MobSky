// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SWidget;

class FMobSkyEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Authors the noise, the cloud master and its instances, and the sky master and its instances. */
	static void GenerateMaterials();

private:
	void RegisterMenus();
	TSharedRef<SWidget> BuildMenu();

	static void Place(UClass* ActorClass, bool bVolume);
	static bool RunPython(const FString& Snippet);
};
