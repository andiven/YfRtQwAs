// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceAssetManager.h"

#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
#include "Materials/Material.h"
#include "WebInterfaceBrowserTexture.h"
#endif

UWebInterfaceAssetManager::UWebInterfaceAssetManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
	, DefaultMaterial(FSoftObjectPath("/WebUI/WebTexture_M.WebTexture_M"))
	, DefaultTranslucentMaterial(FSoftObjectPath("/WebUI/WebTexture_TM.WebTexture_TM"))
#endif
{
#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
	UWebInterfaceBrowserTexture::StaticClass();
#endif
};

void UWebInterfaceAssetManager::LoadDefaultMaterials()
{
#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
	DefaultMaterial.LoadSynchronous();
	DefaultTranslucentMaterial.LoadSynchronous();
#endif
}

UMaterial* UWebInterfaceAssetManager::GetDefaultMaterial()
{
#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
	return DefaultMaterial.Get();
#else
	return nullptr;
#endif
}

UMaterial* UWebInterfaceAssetManager::GetDefaultTranslucentMaterial()
{
#if WITH_EDITOR || PLATFORM_ANDROID || PLATFORM_IOS
	return DefaultTranslucentMaterial.Get();
#else
	return nullptr;
#endif
}
