// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserTextureModule.h"


class FWebInterfaceBrowserTextureModule : public IWebInterfaceBrowserTextureModule
{
private:
	// IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FWebInterfaceBrowserTextureModule, WebBrowserTextureUI );

void FWebInterfaceBrowserTextureModule::StartupModule()
{
}

void FWebInterfaceBrowserTextureModule::ShutdownModule()
{
}
