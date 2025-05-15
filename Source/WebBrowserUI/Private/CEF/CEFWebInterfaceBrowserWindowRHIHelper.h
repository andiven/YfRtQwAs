// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#if WITH_CEF3
#include "Layout/Geometry.h"
#if PLATFORM_WINDOWS
#include "TracerWebAcceleratedPaint.h"
#endif

class FSlateUpdatableTexture;
class FSlateTexture2DRHIRef;
class FSlateShaderResource;

class FCEFWebInterfaceBrowserWindow;
struct FCEFWebInterfaceBrowserWindowTexture
#if PLATFORM_WINDOWS
	: public CBrowserWindowTexture
#endif
{
public:
	FCEFWebInterfaceBrowserWindowTexture()
	{
		SlateTexture = nullptr;
	}

	FCEFWebInterfaceBrowserWindowTexture(FSlateUpdatableTexture* Texture)
	{
		SlateTexture = Texture;
	}

#if PLATFORM_WINDOWS
	FCEFWebInterfaceBrowserWindowTexture(const FCEFWebInterfaceBrowserWindowTexture& Other)
		: CBrowserWindowTexture(Other)
	{
		SlateTexture = Other.SlateTexture;
	}
	
	FCEFWebInterfaceBrowserWindowTexture& operator=(const FCEFWebInterfaceBrowserWindowTexture& Other)
	{
		if (this != &Other)
		{
			CBrowserWindowTexture::operator=(Other);
			SlateTexture = Other.SlateTexture;
		}

		return *this;
	}
#endif

	FSlateUpdatableTexture* GetSlateTexture() const;
	FSlateShaderResource*   GetSlateResource() const;

	void ResetSlateTexture(TSharedPtr<FCEFWebInterfaceBrowserWindow> Window = TSharedPtr<FCEFWebInterfaceBrowserWindow>());

#if PLATFORM_WINDOWS
	virtual bool IsValid() const override;

	virtual void Reset() override;
	virtual void Resize(uint32 Width, uint32 Height) override;

	virtual void* GetNativeResource() const override;

	virtual uint32 GetNativeResourceWidth()  const override;
	virtual uint32 GetNativeResourceHeight() const override;

	virtual void ResetInterop() override;
	virtual bool CreateInterop(CBrowserWindowRenderHelper* Helper) override;
#else
	virtual bool IsValid() const;

	virtual void Reset();
	virtual void Resize(uint32 Width, uint32 Height);

	virtual bool HasInterop() const;
	virtual void ResetInterop();
#endif

	FCEFWebInterfaceBrowserWindowTexture& operator=(FSlateUpdatableTexture* Texture)
	{
		Reset();

		SlateTexture = Texture;
		return *this;
	}

private:

	FSlateUpdatableTexture* SlateTexture;
};

class FCEFWebInterfaceBrowserWindowRHIHelper
#if PLATFORM_WINDOWS
	: public CBrowserWindowRenderHelper
#endif
{
public:
	FCEFWebInterfaceBrowserWindowRHIHelper()
	{
		SharedTextureSyncTime = 0;
		SharedTextureCopySubresourceRegion = false;

		RefreshSettings();
	}

#if PLATFORM_WINDOWS
	FCEFWebInterfaceBrowserWindowRHIHelper(const FCEFWebInterfaceBrowserWindowRHIHelper& Other)
		: CBrowserWindowRenderHelper(Other)
	{
		SharedTextureSyncTime              = Other.SharedTextureSyncTime;
		SharedTextureCopySubresourceRegion = Other.SharedTextureCopySubresourceRegion;
		AllottedGeometry                   = Other.AllottedGeometry;
	}
	
	FCEFWebInterfaceBrowserWindowRHIHelper& operator=(const FCEFWebInterfaceBrowserWindowRHIHelper& Other)
	{
		if (this != &Other)
		{
			CBrowserWindowRenderHelper::operator=(Other);

			SharedTextureSyncTime              = Other.SharedTextureSyncTime;
			SharedTextureCopySubresourceRegion = Other.SharedTextureCopySubresourceRegion;
			AllottedGeometry                   = Other.AllottedGeometry;
		}

		return *this;
	}
#endif

	FCEFWebInterfaceBrowserWindowTexture CreateTexture(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle);

	void UpdateSharedHandleTexture(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle, FCEFWebInterfaceBrowserWindowTexture& TextureGroup, const FIntRect& DirtyIn);
	void UpdateCachedGeometry(const FGeometry& AllottedGeometry);

	TOptional<FSlateRenderTransform> GetWebBrowserRenderTransform() const;
	TOptional<FScale2D>              GetWebBrowserRenderScale() const;

protected:
	
#if PLATFORM_WINDOWS
	virtual bool CreateDevice() override;

	virtual void* GetNativeDevice() const override;
	virtual void* GetNativeCommandQueue() const override;
#endif

	FCEFWebInterfaceBrowserWindowTexture CreateTexture_DX11(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle);
	FCEFWebInterfaceBrowserWindowTexture CreateTexture_DX12(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle);

	void UpdateSharedHandleTexture_DX11(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle, FCEFWebInterfaceBrowserWindowTexture& TextureGroup, const FIntRect& DirtyIn);
	void UpdateSharedHandleTexture_DX12(TSharedRef<FCEFWebInterfaceBrowserWindow> Window, void* SharedHandle, FCEFWebInterfaceBrowserWindowTexture& TextureGroup, const FIntRect& DirtyIn);

	void RefreshSettings();

private:

	int32 SharedTextureSyncTime;
	bool  SharedTextureCopySubresourceRegion;

	FGeometry AllottedGeometry;

public:
	
	static bool BUseRHIRenderer();
};
#endif
