// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserTexture.h"

#include "ExternalTexture.h"
#include "Modules/ModuleManager.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "WebInterfaceBrowserTextureResource.h"
//#include "IWebInterfaceBrowserWindow.h"

/* UWebInterfaceBrowserTexture structors
*****************************************************************************/

UWebInterfaceBrowserTexture::UWebInterfaceBrowserTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AddressX(TA_Clamp)
	, AddressY(TA_Clamp)
	, ClearColor(FLinearColor::Black)
	, Size(0)
{
	SampleQueue = MakeShared<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe>();
	WebPlayerGuid = FGuid::NewGuid();
	NeverStream = true;
}

/* UWebInterfaceBrowserTexture interface
*****************************************************************************/

float UWebInterfaceBrowserTexture::GetAspectRatio() const
{
	if (Dimensions.Y == 0)
	{
		return 0.0f;
	}

	return (float)(Dimensions.X) / Dimensions.Y;
}

int32 UWebInterfaceBrowserTexture::GetHeight() const
{
	return Dimensions.Y;
}

int32 UWebInterfaceBrowserTexture::GetWidth() const
{
	return Dimensions.X;
}

/* UTexture interface
*****************************************************************************/

FTextureResource* UWebInterfaceBrowserTexture::CreateResource()
{
	return new FWebInterfaceBrowserTextureResource(*this, Dimensions, Size);
}

EMaterialValueType UWebInterfaceBrowserTexture::GetMaterialType() const
{
	return MCT_TextureExternal;
}

float UWebInterfaceBrowserTexture::GetSurfaceWidth() const
{
	return Dimensions.X;
}

float UWebInterfaceBrowserTexture::GetSurfaceHeight() const
{
	return Dimensions.Y;
}

FGuid UWebInterfaceBrowserTexture::GetExternalTextureGuid() const
{
	return WebPlayerGuid;
}

void UWebInterfaceBrowserTexture::SetExternalTextureGuid(FGuid guid)
{
	WebPlayerGuid = guid;
}

/* UObject interface
*****************************************************************************/

void UWebInterfaceBrowserTexture::BeginDestroy()
{
	UnregisterPlayerGuid();

	Super::BeginDestroy();
}

FString UWebInterfaceBrowserTexture::GetDesc()
{
	return FString::Printf(TEXT("%ix%i [%s]"), Dimensions.X, Dimensions.Y, GPixelFormats[PF_B8G8R8A8].Name);
}


void UWebInterfaceBrowserTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	CumulativeResourceSize.AddUnknownMemoryBytes(Size);
}

/* UWebInterfaceBrowserTexture implementation
*****************************************************************************/


void UWebInterfaceBrowserTexture::TickResource(TSharedPtr<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe> Sample)
{
#if UE_VERSION >= 427
	if (GetResource() == nullptr)
#else
	if (Resource == nullptr)
#endif
	{
		return;
	}
	
	check(SampleQueue.IsValid());

	if (Sample.IsValid())
	{
		SampleQueue.Get()->Enqueue(Sample);
	}

	// issue a render command to render the current sample
	FWebInterfaceBrowserTextureResource::FRenderParams RenderParams;
	{
		RenderParams.ClearColor = ClearColor;
		RenderParams.PlayerGuid = GetExternalTextureGuid();
		RenderParams.SampleSource = SampleQueue;
	}

#if UE_VERSION >= 427
	FWebInterfaceBrowserTextureResource* ResourceParam = (FWebInterfaceBrowserTextureResource*)GetResource();
#else
	FWebInterfaceBrowserTextureResource* ResourceParam = (FWebInterfaceBrowserTextureResource*)Resource;
#endif

	ENQUEUE_RENDER_COMMAND(UWebInterfaceBrowserTextureResourceRender)(
		[ResourceParam, RenderParams](FRHICommandListImmediate& RHICmdList)
		{
			ResourceParam->Render(RenderParams);
		});
}

void UWebInterfaceBrowserTexture::UnregisterPlayerGuid()
{
	if (!WebPlayerGuid.IsValid())
	{
		return;
	}

	FGuid PlayerGuid = WebPlayerGuid;
	ENQUEUE_RENDER_COMMAND(UWebInterfaceBrowserTextureUnregisterPlayerGuid)(
		[PlayerGuid](FRHICommandListImmediate& RHICmdList)
		{
			FExternalTextureRegistry::Get().UnregisterExternalTexture(PlayerGuid);
		});
}
