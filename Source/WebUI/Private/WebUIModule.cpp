// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebUIModule.h"
#include "Modules/ModuleManager.h"
#include "WebInterfaceSettings.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
	#include "ISettingsSection.h"
#endif

#ifndef UE_SERVER
#define UE_SERVER 0
#endif

#if !UE_SERVER
#include "WebInterfaceAssetManager.h"
#include "Materials/Material.h"
#include "IWebInterfaceBrowserSingleton.h"
#include "WebInterfaceBrowserModule.h"
#endif

#define LOCTEXT_NAMESPACE "FWebUIModule"

class FWebUIModule : public IWebUIModule
{
public:
	virtual void StartupModule() override
	{
#if !UE_SERVER
		if (WebInterfaceAssetMgr == nullptr)
		{
#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
			WebInterfaceAssetMgr = NewObject<UWebInterfaceAssetManager>((UObject*)GetTransientPackage(), NAME_None, RF_Transient | RF_Public);
			WebInterfaceAssetMgr->LoadDefaultMaterials();

#if UE_VERSION >= 500
			IWebInterfaceBrowserModule::Get(); // force the module to load
			if (IWebInterfaceBrowserModule::IsAvailable() && IWebInterfaceBrowserModule::Get().IsWebModuleAvailable())
			{
#endif
				IWebInterfaceBrowserSingleton* WebBrowserSingleton = IWebInterfaceBrowserModule::Get().GetSingleton();
				if (WebBrowserSingleton)
				{
					WebBrowserSingleton->SetDefaultMaterial(WebInterfaceAssetMgr->GetDefaultMaterial());
					WebBrowserSingleton->SetDefaultTranslucentMaterial(WebInterfaceAssetMgr->GetDefaultTranslucentMaterial());
				}
#if UE_VERSION >= 500
			}
#endif
#endif
		}
#endif

#if WITH_EDITOR
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
        if (SettingsModule)
        {
            ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "WebUI",
                LOCTEXT("WebInterfaceSettingsName", "WebUI"),
                LOCTEXT("WebInterfaceSettingsDescription", "Configure the WebUI plug-in."),
                GetMutableDefault<UWebInterfaceSettings>()
            	);

            if (SettingsSection.IsValid())
            {
                SettingsSection->OnModified().BindRaw(this, &FWebUIModule::HandleSettingsSaved);
            }
        }
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule)
        {
            SettingsModule->UnregisterSettings("Project", "Plugins", "WebUI");
        }
#endif
	}

private:

#if WITH_EDITOR
	bool HandleSettingsSaved()
	{
		return true;
	}
#endif

#if !UE_SERVER
	UWebInterfaceAssetManager* WebInterfaceAssetMgr;
#endif
};

IMPLEMENT_MODULE(FWebUIModule, WebUI);
#undef LOCTEXT_NAMESPACE
