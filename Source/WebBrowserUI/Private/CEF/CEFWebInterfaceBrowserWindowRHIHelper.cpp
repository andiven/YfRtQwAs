// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "CEFWebInterfaceBrowserWindowRHIHelper.h"
#include "WebInterfaceBrowserLog.h"
#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_CEF3
#include "CEF/CEFWebInterfaceBrowserWindow.h"
#include "CEF/CEFWebInterfaceBrowserWindowRHIPrivate.h"
#if WITH_ENGINE
#include "RHI.h"
#include "RHIDefinitions.h"
#include "Slate/SlateTextures.h"
#if PLATFORM_WINDOWS
#include "RenderingThread.h"
#include "WebBrowserUtils.h"
#endif
#endif

#define UI_CONSOLE(ColorName, FormatString, ...)	Window->ConsoleLog( FColor::ColorName, TEXT( FormatString ), ##__VA_ARGS__ )

bool FCEFWebInterfaceBrowserWindowTexture::IsValid() const
{
	return SlateTexture != nullptr;
}

FSlateUpdatableTexture* FCEFWebInterfaceBrowserWindowTexture::GetSlateTexture() const
{
	return SlateTexture;
}

FSlateShaderResource* FCEFWebInterfaceBrowserWindowTexture::GetSlateResource() const
{
	if (SlateTexture)
		return SlateTexture->GetSlateResource();
	return nullptr;
}

void FCEFWebInterfaceBrowserWindowTexture::ResetSlateTexture(TSharedPtr<FCEFWebInterfaceBrowserWindow> Window)
{
	ResetInterop();
	if (SlateTexture)
	{
		FSlateUpdatableTexture* TextureToRelease = SlateTexture;
		if (!Window.IsValid())
		{
			if (IsInGameThread())
			{
				if (FSlateApplication::IsInitialized())
					if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
						Renderer->ReleaseUpdatableTexture(TextureToRelease);
			}
			else if (FTaskGraphInterface::IsRunning())
				AsyncTask(ENamedThreads::GameThread, [TextureToRelease]()
				{
					if (FSlateApplication::IsInitialized())
						if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
							Renderer->ReleaseUpdatableTexture(TextureToRelease);
				});
		}
		else if (FSlateRenderer* Renderer = Window->GetRenderer())
			Renderer->ReleaseUpdatableTexture(TextureToRelease);
	}

	SlateTexture = nullptr;
}

void FCEFWebInterfaceBrowserWindowTexture::Reset()
{
#if PLATFORM_WINDOWS
	CBrowserWindowTexture::Reset();
#endif
	
	SlateTexture = nullptr;
}

void FCEFWebInterfaceBrowserWindowTexture::Resize(uint32 Width, uint32 Height)
{
#if PLATFORM_WINDOWS
	CBrowserWindowTexture::Resize(Width, Height);
#endif

	if (SlateTexture)
		SlateTexture->ResizeTexture(Width, Height);
}

void FCEFWebInterfaceBrowserWindowRHIHelper::UpdateCachedGeometry(const FGeometry& AllottedGeometryIn)
{
	AllottedGeometry = AllottedGeometryIn;
}

TOptional<FSlateRenderTransform> FCEFWebInterfaceBrowserWindowRHIHelper::GetWebBrowserRenderTransform() const
{
	TOptional<FScale2D> RenderScale = GetWebBrowserRenderScale();
	if (RenderScale.IsSet())
		return FSlateRenderTransform(Concatenate(RenderScale.GetValue(), FVector2D(0, AllottedGeometry.GetLocalSize().Y)));

	return FSlateRenderTransform();
}

TOptional<FScale2D> FCEFWebInterfaceBrowserWindowRHIHelper::GetWebBrowserRenderScale() const
{
	// undo the texture flip if we are using accelerated rendering
	return TOptional<FScale2D>();//FScale2D(1, -1);
}

#if PLATFORM_WINDOWS
bool FCEFWebInterfaceBrowserWindowRHIHelper::CreateDevice()
{
#if UE_VERSION >= 501
	check(RHIGetInterfaceType() == ERHIInterfaceType::D3D12);
#endif

	if (HasDevice())
		return false;

	if (!IsInRenderingThread())
	{
		bool bHasDevice = false;
		ENQUEUE_RENDER_COMMAND(CEFCreateDevice)(
			[&bHasDevice, this](FRHICommandListImmediate& RHICmdList)
			{
				bHasDevice = CBrowserWindowRenderHelper::CreateDevice();
			});

		FlushRenderingCommands();
		return bHasDevice;
	}
	else
		return CBrowserWindowRenderHelper::CreateDevice();
}

void* FCEFWebInterfaceBrowserWindowRHIHelper::GetNativeDevice() const
{
	return GDynamicRHI->RHIGetNativeDevice();
}

void* FCEFWebInterfaceBrowserWindowRHIHelper::GetNativeCommandQueue() const
{
#if UE_VERSION >= 427
	return GDynamicRHI->RHIGetNativeGraphicsQueue();
#else
#if UE_VERSION >= 426
	FD3D12DynamicRHI* DynamicRHI = FD3D12DynamicRHI::GetD3DRHI();
#else
	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
#endif

	if (DynamicRHI)
		return DynamicRHI->RHIGetD3DCommandQueue();
	return nullptr;
#endif
}
#endif

FCEFWebInterfaceBrowserWindowTexture FCEFWebInterfaceBrowserWindowRHIHelper::CreateTexture(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle)
{
#if WITH_ENGINE
	check(BUseRHIRenderer());
#if PLATFORM_WINDOWS
#if UE_VERSION >= 501
	switch (RHIGetInterfaceType())
	{
		case ERHIInterfaceType::D3D12:
			return CreateTexture_DX12(Window, SharedHandle);
		case ERHIInterfaceType::D3D11:
			return CreateTexture_DX11(Window, SharedHandle);
	}
#else
	static bool sbD3D12Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
	static bool sbD3D11Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D11")) == 0;
	if (sbD3D12Renderer)
		return CreateTexture_DX12(Window, SharedHandle);
	else if (sbD3D11Renderer)
		return CreateTexture_DX11(Window, SharedHandle);
#endif
#endif
#endif
	return FCEFWebInterfaceBrowserWindowTexture();
}

void FCEFWebInterfaceBrowserWindowRHIHelper::UpdateSharedHandleTexture(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle, FCEFWebInterfaceBrowserWindowTexture& TextureGroup, const FIntRect& DirtyIn)
{
#if WITH_ENGINE
	check(BUseRHIRenderer());
#if PLATFORM_WINDOWS
#if UE_VERSION >= 501
	switch (RHIGetInterfaceType())
	{
		case ERHIInterfaceType::D3D12:
			UpdateSharedHandleTexture_DX12(Window, SharedHandle, TextureGroup, DirtyIn);
			break;
		case ERHIInterfaceType::D3D11:
			UpdateSharedHandleTexture_DX11(Window, SharedHandle, TextureGroup, DirtyIn);
			break;
	}
#else
	static bool sbD3D12Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
	static bool sbD3D11Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D11")) == 0;
	if (sbD3D12Renderer)
		UpdateSharedHandleTexture_DX12(Window, SharedHandle, TextureGroup, DirtyIn);
	else if (sbD3D11Renderer)
		UpdateSharedHandleTexture_DX11(Window, SharedHandle, TextureGroup, DirtyIn);
#endif
#else
	UE_LOG(LogWebInterfaceBrowser, Error, TEXT("FCEFWebInterfaceBrowserWindowRHIHelper::UpdateSharedHandleTexture() - missing implementation"));
#endif // PLATFORM_WINDOWS
#else
	UE_LOG(LogWebInterfaceBrowser, Error, TEXT("FCEFWebInterfaceBrowserWindowRHIHelper::UpdateSharedHandleTexture() - unsupported usage, RHI renderer but missing engine"));
#endif // WITH_ENGINE
}

FCEFWebInterfaceBrowserWindowTexture FCEFWebInterfaceBrowserWindowRHIHelper::CreateTexture_DX11(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle)
{
#if WITH_ENGINE
	check(BUseRHIRenderer());
#if PLATFORM_WINDOWS
#if UE_VERSION >= 501
	check(RHIGetInterfaceType() == ERHIInterfaceType::D3D11);
#endif
	
	CBrowserWindowSharedTexture Texture;
	if (!OpenSharedTextureDX11(SharedHandle, Texture))
	{
		UI_CONSOLE(Red, "CreateTexture() -- OpenSharedTextureDX11()");
		return FCEFWebInterfaceBrowserWindowTexture();
	}

	FSlateTexture2DRHIRef* NewTexture = new FSlateTexture2DRHIRef(Texture.GetWidth(), Texture.GetHeight(), PF_R8G8B8A8, nullptr, TexCreate_Dynamic, true);
	if (!IsInRenderingThread())
	{
		BeginInitResource(NewTexture);
		FlushRenderingCommands();
	}
	else
#if UE_VERSION >= 503
		NewTexture->InitResource(FRHICommandListExecutor::GetImmediateCommandList());
#else
		NewTexture->InitResource();
#endif

	return NewTexture;
#else
	return FCEFWebInterfaceBrowserWindowTexture();
#endif
#else
	return FCEFWebInterfaceBrowserWindowTexture();
#endif
}

void FCEFWebInterfaceBrowserWindowRHIHelper::UpdateSharedHandleTexture_DX11(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle, FCEFWebInterfaceBrowserWindowTexture& TextureGroup, const FIntRect& DirtyIn)
{
	bool bResize = false;
	FIntRect Dirty = DirtyIn;

#if WITH_ENGINE
	check(BUseRHIRenderer());
#if PLATFORM_WINDOWS
#if UE_VERSION >= 501
	check(RHIGetInterfaceType() == ERHIInterfaceType::D3D11);
#endif

	CBrowserWindowSharedTexture Texture;
	if (!OpenSharedTextureGroupDX11(SharedHandle, Texture, TextureGroup, bResize))
	{
		UI_CONSOLE(Red, "UpdateSharedHandleTexture() -- OpenSharedTextureGroupDX11()");
		return;
	}
	else if (bResize)
		Dirty = FIntRect();

	ENQUEUE_RENDER_COMMAND(CEFAcceleratedPaint)(
		[Window, &TextureGroup, Dirty, Texture, this](FRHICommandList& RHICmdList)
		{
			bool bRegion = (Dirty.Area() > 0 && SharedTextureCopySubresourceRegion);
			if (!CopySharedTextureGroupDX11(TextureGroup, Texture, SharedTextureSyncTime, bRegion, Dirty.Min.X, Dirty.Min.Y, Dirty.Max.X, Dirty.Max.Y))
			{
				UI_CONSOLE(Yellow, "UpdateSharedHandleTexture() -- CopySharedTextureGroupDX11()");
				return;
			}
		}
	);
#endif
#endif
}

FCEFWebInterfaceBrowserWindowTexture FCEFWebInterfaceBrowserWindowRHIHelper::CreateTexture_DX12(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle)
{
#if WITH_ENGINE
	check(BUseRHIRenderer());
#if PLATFORM_WINDOWS
#if UE_VERSION >= 501
	check(RHIGetInterfaceType() == ERHIInterfaceType::D3D12);
#endif
	
	CBrowserWindowSharedTexture Texture;
	if (!OpenSharedTextureDX12(SharedHandle, Texture))
	{
		UI_CONSOLE(Red, "CreateTexture() -- OpenSharedTextureDX12()");
		return FCEFWebInterfaceBrowserWindowTexture();
	}

	FSlateTexture2DRHIRef* NewTexture = new FSlateTexture2DRHIRef(Texture.GetWidth(), Texture.GetHeight(), PF_R8G8B8A8, nullptr, TexCreate_Dynamic | TexCreate_Shared, true);
	if (!IsInRenderingThread())
	{
		BeginInitResource(NewTexture);
		FlushRenderingCommands();
	}
	else
#if UE_VERSION >= 503
		NewTexture->InitResource(FRHICommandListExecutor::GetImmediateCommandList());
#else
		NewTexture->InitResource();
#endif

	return NewTexture;
#else
	return FCEFWebInterfaceBrowserWindowTexture();
#endif
#else
	return FCEFWebInterfaceBrowserWindowTexture();
#endif
}

void FCEFWebInterfaceBrowserWindowRHIHelper::UpdateSharedHandleTexture_DX12(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle, FCEFWebInterfaceBrowserWindowTexture& TextureGroup, const FIntRect& DirtyIn)
{
	bool bResize = false;
	FIntRect Dirty = DirtyIn;

#if WITH_ENGINE
	check(BUseRHIRenderer());
#if PLATFORM_WINDOWS
#if UE_VERSION >= 501
	check(RHIGetInterfaceType() == ERHIInterfaceType::D3D12);
#endif

	CBrowserWindowSharedTexture Texture;
	if (!OpenSharedTextureGroupDX12(SharedHandle, Texture, TextureGroup, bResize))
	{
		UI_CONSOLE(Red, "UpdateSharedHandleTexture() -- OpenSharedTextureGroupDX12()");
		return;
	}
	else if (bResize)
		Dirty = FIntRect();

	ENQUEUE_RENDER_COMMAND(CEFAcceleratedPaint)(
		[Window, &TextureGroup, Dirty, Texture, this](FRHICommandListImmediate& RHICmdList)
		{
			bool bRegion = (Dirty.Area() > 0 && SharedTextureCopySubresourceRegion);
			if (!CopySharedTextureGroupDX12(TextureGroup, Texture, SharedTextureSyncTime, bRegion, Dirty.Min.X, Dirty.Min.Y, Dirty.Max.X, Dirty.Max.Y))
			{
				UI_CONSOLE(Yellow, "UpdateSharedHandleTexture() -- CopySharedTextureGroupDX12()");
				return;
			}
		});
#endif
#endif
}

void FCEFWebInterfaceBrowserWindowRHIHelper::RefreshSettings()
{
	if (!GConfig->GetInt(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("KeyedMutexSyncTime"), SharedTextureSyncTime, GGameIni))
		SharedTextureSyncTime = 0;
	if (!GConfig->GetBool(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("bCopySubresourceRegion"), SharedTextureCopySubresourceRegion, GGameIni))
		SharedTextureCopySubresourceRegion = false;
}

bool FCEFWebInterfaceBrowserWindowRHIHelper::BUseRHIRenderer()
{
#if WITH_ENGINE
	if (GDynamicRHI != nullptr && FCEFWebInterfaceBrowserWindow::CanSupportAcceleratedPaint())
	{
#if PLATFORM_WINDOWS
		return WebBrowserUtils::CanSupportAcceleratedPaint();
#endif
	}
#endif
	return false;
}

#if PLATFORM_WINDOWS
void* FCEFWebInterfaceBrowserWindowTexture::GetNativeResource() const
{
	if (!SlateTexture)
		return nullptr;

	FSlateTexture2DRHIRef* SlateRHITexture = static_cast<FSlateTexture2DRHIRef*>(SlateTexture);
	check(SlateRHITexture);

#if UE_VERSION >= 505
	FTextureRHIRef Slate2DRef = SlateRHITexture->GetRHIRef();
#else
	FTexture2DRHIRef Slate2DRef = SlateRHITexture->GetRHIRef();
#endif
	if (!Slate2DRef || !Slate2DRef.IsValid())
		return nullptr;

#if UE_VERSION >= 505
	FRHITexture* Slate2DTexture = Slate2DRef->GetTexture2D();
#else
	FRHITexture2D* Slate2DTexture = Slate2DRef->GetTexture2D();
#endif
	if (!Slate2DTexture)
		return nullptr;
	
	return Slate2DTexture->GetNativeResource();
}

uint32 FCEFWebInterfaceBrowserWindowTexture::GetNativeResourceWidth() const
{
	FSlateShaderResource* SlateResource = GetSlateResource();
	if (SlateResource)
		return SlateResource->GetWidth();
	
	return 0;
}

uint32 FCEFWebInterfaceBrowserWindowTexture::GetNativeResourceHeight() const
{
	FSlateShaderResource* SlateResource = GetSlateResource();
	if (SlateResource)
		return SlateResource->GetHeight();
	
	return 0;
}

void FCEFWebInterfaceBrowserWindowTexture::ResetInterop()
{
#if UE_VERSION >= 501
	if (RHIGetInterfaceType() != ERHIInterfaceType::D3D12)
		return;
#else
	static bool sbD3D12Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
	if (!sbD3D12Renderer)
		return;
#endif

	if (!IsInRenderingThread())
	{
		ENQUEUE_RENDER_COMMAND(CEFResetInterop)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				CBrowserWindowTexture::ResetInterop();
			});

		FlushRenderingCommands();
	}
	else
		CBrowserWindowTexture::ResetInterop();
}

bool FCEFWebInterfaceBrowserWindowTexture::CreateInterop(CBrowserWindowRenderHelper* Helper)
{
#if UE_VERSION >= 501
	check(RHIGetInterfaceType() == ERHIInterfaceType::D3D12);
#endif

	if (!IsInRenderingThread())
	{
		bool bHasInterop = false;
		ENQUEUE_RENDER_COMMAND(CEFCreateInterop)(
			[Helper, &bHasInterop, this](FRHICommandListImmediate& RHICmdList)
			{
				bHasInterop = CBrowserWindowTexture::CreateInterop(Helper);
			});

		FlushRenderingCommands();
		return bHasInterop;
	}
	else
		return CBrowserWindowTexture::CreateInterop(Helper);
}
#else
bool FCEFWebInterfaceBrowserWindowTexture::HasInterop() const
{
	return false;
}

void FCEFWebInterfaceBrowserWindowTexture::ResetInterop()
{
	//
}
#endif
#endif
