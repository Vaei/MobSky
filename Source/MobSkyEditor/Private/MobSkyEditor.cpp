// Copyright (c) Jared Taylor

#include "MobSkyEditor.h"

#include "Editor.h"
#include "IPythonScriptPlugin.h"
#include "ISettingsModule.h"
#include "LevelEditorViewport.h"
#include "MobCloudComponent.h"
#include "MobSkyActors.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MobSkyEditor"

namespace
{
	/** The fixed left to right order of the Mob toolbar buttons. A button that is not listed sorts to the end. */
	int32 MobToolbarOrder(const FName EntryName)
	{
		static const FName Order[] =
		{
			TEXT("WorldMenu"),
			TEXT("MobSkyMenu"),
			TEXT("MobLightsMenu"),
			TEXT("MobWaterMenu"),
			TEXT("FortMenu"),
			TEXT("MatMenu"),
		};

		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Order); ++Index)
		{
			if (Order[Index] == EntryName)
			{
				return Index;
			}
		}
		return MAX_int32;
	}

	bool MobToolbarOrderLess(const FToolMenuEntry& A, const FToolMenuEntry& B, const FToolMenuContext&)
	{
		return MobToolbarOrder(A.Name) < MobToolbarOrder(B.Name);
	}

	void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 4.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

void FMobSkyEditorModule::StartupModule()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FMobSkyEditorModule::RegisterMenus));
	}
}

void FMobSkyEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
}

void FMobSkyEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
	if (!ToolBar)
	{
		return;
	}

	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
		TEXT("MobSkyMenu"),
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FMobSkyEditorModule::BuildMenu),
		LOCTEXT("SkyToolbar", "Sky"),
		LOCTEXT("SkyToolbarTip", "Place and author the sky and clouds"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.VolumetricCloud")));
	Entry.StyleNameOverride = TEXT("CalloutToolbar");

	FToolMenuSection& Section = ToolBar->FindOrAddSection(TEXT("MobTools"));
	Section.Sorter = FToolMenuSectionSorter::CreateStatic(&MobToolbarOrderLess);
	Section.AddEntry(Entry);
}

TSharedRef<SWidget> FMobSkyEditorModule::BuildMenu()
{
	FMenuBuilder Menu(true, nullptr);

	Menu.BeginSection(TEXT("MobSkyPlace"), LOCTEXT("PlaceSection", "Place"));
	Menu.AddMenuEntry(
		LOCTEXT("PlaceSky", "Sky"),
		LOCTEXT("PlaceSkyTip", "The dome: atmosphere, planet, stars, galaxy and nebulae. One per level."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.SkyAtmosphere")),
		FUIAction(FExecuteAction::CreateStatic(&FMobSkyEditorModule::Place, AMobSky::StaticClass(), false)));
	Menu.AddMenuEntry(
		LOCTEXT("PlaceLayer", "Cloud Layer"),
		LOCTEXT("PlaceLayerTip", "A deck of cloud between two heights that follows the camera to the horizon."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.VolumetricCloud")),
		FUIAction(FExecuteAction::CreateStatic(&FMobSkyEditorModule::Place, AMobCloud::StaticClass(), false)));
	Menu.AddMenuEntry(
		LOCTEXT("PlaceVolume", "Cloud Volume"),
		LOCTEXT("PlaceVolumeTip", "A bank of cloud that stays where it is put. Give it an emissive colour for a nebula."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.VolumetricCloud")),
		FUIAction(FExecuteAction::CreateStatic(&FMobSkyEditorModule::Place, AMobCloud::StaticClass(), true)));
	Menu.EndSection();

	Menu.BeginSection(TEXT("MobSkyBuild"), LOCTEXT("BuildSection", "Build"));
	Menu.AddMenuEntry(
		LOCTEXT("Generate", "Generate Materials"),
		LOCTEXT("GenerateTip",
			"Authors the noise textures, the cloud and sky masters and their instances. Run once after "
			"installing, and again after changing the shaders' inputs."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
		FUIAction(FExecuteAction::CreateStatic(&FMobSkyEditorModule::GenerateMaterials)));
	Menu.AddMenuEntry(
		LOCTEXT("Settings", "Project Settings"),
		LOCTEXT("SettingsTip", "Where MobSky finds its materials."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
			{
				Settings->ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("MobSkySettings"));
			}
		})));
	Menu.EndSection();

	return Menu.MakeWidget();
}

void FMobSkyEditorModule::Place(UClass* ActorClass, bool bVolume)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || !ActorClass)
	{
		return;
	}

	FVector Location = FVector::ZeroVector;
	if (GCurrentLevelEditingViewportClient)
	{
		Location = GCurrentLevelEditingViewportClient->GetViewLocation()
			+ GCurrentLevelEditingViewportClient->GetViewRotation().Vector() * 2000.f;
	}

	const FScopedTransaction Transaction(LOCTEXT("PlaceTransaction", "Place Mob Sky Actor"));
	AActor* Actor = GEditor->AddActor(World->GetCurrentLevel(), ActorClass, FTransform(Location));
	if (!Actor)
	{
		return;
	}

	if (const AMobCloud* Cloud = Cast<AMobCloud>(Actor); Cloud && bVolume)
	{
		UMobCloudComponent* Component = Cloud->GetCloud();
		Component->Modify();
		Component->Shape = EMobCloudShape::Volume;
		Component->RefreshClouds();
		Actor->SetActorLabel(TEXT("MobCloudVolume"));
	}

	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Actor, true, true);
}

bool FMobSkyEditorModule::RunPython(const FString& Snippet)
{
	if (!IPythonScriptPlugin::Get() || !IPythonScriptPlugin::Get()->IsPythonAvailable())
	{
		return false;
	}

	// MobSky's generator borrows MobLights' graph helpers, so both script folders go on the path.
	FString Paths;
	for (const TCHAR* Name : { TEXT("MobSky"), TEXT("MobLights") })
	{
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name))
		{
			const FString Dir = FPaths::Combine(FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()),
				TEXT("Python")).Replace(TEXT("\\"), TEXT("/"));
			Paths += FString::Printf(TEXT("p = r'%s'\nsys.path.append(p) if p not in sys.path else None\n"), *Dir);
		}
	}

	const FString Command = FString::Printf(TEXT("import sys\n%s%s\n"), *Paths, *Snippet);
	return IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);
}

void FMobSkyEditorModule::GenerateMaterials()
{
	const bool bOk = RunPython(
		TEXT("import importlib, author_sky; importlib.reload(author_sky); author_sky.build_all()"));
	Notify(bOk ? LOCTEXT("GenerateDone", "MobSky: materials authored. See the Output Log.")
		: LOCTEXT("GenerateFailed", "MobSky: failed. See the Output Log."), bOk);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMobSkyEditorModule, MobSkyEditor)
