// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserTextureResource.h"

#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "ExternalTexture.h"
#include "PipelineStateCache.h"
#include "SceneUtils.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "ExternalTexture.h"
#include "WebInterfaceBrowserTexture.h"


#define WebInterfaceBrowserTextureRESOURCE_TRACE_RENDER 0

DEFINE_LOG_CATEGORY(LogWebInterfaceBrowserTexture);

/* FWebInterfaceBrowserTextureResource structors
 *****************************************************************************/

FWebInterfaceBrowserTextureResource::FWebInterfaceBrowserTextureResource(UWebInterfaceBrowserTexture& InOwner, FIntPoint& InOwnerDim, SIZE_T& InOwnerSize)
	: Cleared(false)
	, CurrentClearColor(FLinearColor::Transparent)
	, Owner(InOwner)
	, OwnerDim(InOwnerDim)
	, OwnerSize(InOwnerSize)
{
	UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:FWebInterfaceBrowserTextureResource %d %d"), OwnerDim.X, OwnerDim.Y);
}


/* FWebInterfaceBrowserTextureResource interface
 *****************************************************************************/

void FWebInterfaceBrowserTextureResource::Render(const FRenderParams& Params)
{
	check(IsInRenderingThread());

	TSharedPtr<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe> SampleSource = Params.SampleSource.Pin();

	if (SampleSource.IsValid())
	{
		// get the most current sample to be rendered
		TSharedPtr<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe> Sample;
		bool SampleValid = false;
		
		while (SampleSource->Peek(Sample) && Sample.IsValid())
		{
			SampleValid = SampleSource->Dequeue(Sample);
		}

		if (!SampleValid)
		{
			return; // no sample to render
		}

		check(Sample.IsValid());

		// render the sample
		CopySample(Sample, Params.ClearColor);

		if (!GSupportsImageExternal && Params.PlayerGuid.IsValid())
		{
			FTextureRHIRef VideoTexture = (FTextureRHIRef)Owner.TextureReference.TextureReferenceRHI;
			FExternalTextureRegistry::Get().RegisterExternalTexture(Params.PlayerGuid, VideoTexture, SamplerStateRHI, Sample->GetScaleRotation(), Sample->GetOffset());
		}
	}
	else if (!Cleared)
	{
		ClearTexture(Params.ClearColor);

		if (!GSupportsImageExternal && Params.PlayerGuid.IsValid())
		{
			FTextureRHIRef VideoTexture = (FTextureRHIRef)Owner.TextureReference.TextureReferenceRHI;
			FExternalTextureRegistry::Get().RegisterExternalTexture(Params.PlayerGuid, VideoTexture, SamplerStateRHI, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		}
	}
}


/* FRenderTarget interface
 *****************************************************************************/

FIntPoint FWebInterfaceBrowserTextureResource::GetSizeXY() const
{
	return FIntPoint(Owner.GetWidth(), Owner.GetHeight());
}


/* FTextureResource interface
 *****************************************************************************/

FString FWebInterfaceBrowserTextureResource::GetFriendlyName() const
{
	return Owner.GetPathName();
}


uint32 FWebInterfaceBrowserTextureResource::GetSizeX() const
{
	return Owner.GetWidth();
}


uint32 FWebInterfaceBrowserTextureResource::GetSizeY() const
{
	return Owner.GetHeight();
}


#if UE_VERSION >= 503
void FWebInterfaceBrowserTextureResource::InitRHI(FRHICommandListBase& RHICmdList)
#else
void FWebInterfaceBrowserTextureResource::InitDynamicRHI()
#endif
{
	// create the sampler state
	FSamplerStateInitializerRHI SamplerStateInitializer(
		(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(&Owner),
		(Owner.AddressX == TA_Wrap) ? AM_Wrap : ((Owner.AddressX == TA_Clamp) ? AM_Clamp : AM_Mirror),
		(Owner.AddressY == TA_Wrap) ? AM_Wrap : ((Owner.AddressY == TA_Clamp) ? AM_Clamp : AM_Mirror),
		AM_Wrap
	);

	SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
}


#if UE_VERSION >= 503
void FWebInterfaceBrowserTextureResource::ReleaseRHI()
#else
void FWebInterfaceBrowserTextureResource::ReleaseDynamicRHI()
#endif
{
	Cleared = false;

	InputTarget.SafeRelease();
	OutputTarget.SafeRelease();
	RenderTargetTextureRHI.SafeRelease();
	TextureRHI.SafeRelease();

	UpdateTextureReference(nullptr);
}


/* FWebInterfaceBrowserTextureResource implementation
 *****************************************************************************/

void FWebInterfaceBrowserTextureResource::ClearTexture(const FLinearColor& ClearColor)
{
	UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:ClearTexture"));
	// create output render target if we don't have one yet
#if UE_VERSION >= 501
	const ETextureCreateFlags OutputCreateFlags = ETextureCreateFlags::Dynamic | ETextureCreateFlags::SRGB;
#elif UE_VERSION >= 426
	const ETextureCreateFlags OutputCreateFlags = TexCreate_Dynamic | TexCreate_SRGB;
#else
	const uint32 OutputCreateFlags = TexCreate_Dynamic | TexCreate_SRGB;
#endif

#if UE_VERSION >= 501
	if ((ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || !EnumHasAllFlags(OutputTarget->GetFlags(), OutputCreateFlags))
#else
	if ((ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags))
#endif
	{
#if UE_VERSION >= 501
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("FWebInterfaceBrowserTextureResource"))
			.SetExtent(2, 2)
			.SetFormat(PF_B8G8R8A8)
			.SetFlags(OutputCreateFlags | ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask)
			.SetClearValue(FClearValueBinding(ClearColor));

		OutputTarget = RHICreateTexture(Desc);
#else
#if UE_VERSION >= 500
		FRHIResourceCreateInfo CreateInfo(TEXT("FWebInterfaceBrowserTextureResource"), FClearValueBinding(ClearColor));
#else
		FRHIResourceCreateInfo CreateInfo = {
			FClearValueBinding(ClearColor)
		};
#endif

#if UE_VERSION >= 505
		TRefCountPtr<FRHITexture> DummyTexture2DRHI;
#else
		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
#endif

		RHICreateTargetableShaderResource2D(
			2,
			2,
			PF_B8G8R8A8,
			1,
			OutputCreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			OutputTarget,
			DummyTexture2DRHI
		);
#endif

		CurrentClearColor = ClearColor;
		UpdateResourceSize();
	}

	if (RenderTargetTextureRHI != OutputTarget)
	{
		UpdateTextureReference(OutputTarget);
	}

	// draw the clear color
	FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();
	{
#if UE_VERSION >= 501
		CommandList.Transition(FRHITransitionInfo(RenderTargetTextureRHI, ERHIAccess::Unknown, ERHIAccess::RTV));
		ClearRenderTarget(CommandList, RenderTargetTextureRHI);
#else
		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::Clear_Store);
		CommandList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
		CommandList.EndRenderPass();
#endif

#if UE_VERSION >= 426
		CommandList.Transition(FRHITransitionInfo(RenderTargetTextureRHI, ERHIAccess::RTV, ERHIAccess::SRVMask));
#else
		CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
#endif
	}

	Cleared = true;
}

void FWebInterfaceBrowserTextureResource::CopySample(const TSharedPtr<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor)
{
	FRHITexture* SampleTexture = Sample->GetTexture();
#if UE_VERSION >= 505
	FRHITexture* SampleTexture2D = (SampleTexture != nullptr) ? SampleTexture->GetTexture2D() : nullptr;
#else
	FRHITexture2D* SampleTexture2D = (SampleTexture != nullptr) ? SampleTexture->GetTexture2D() : nullptr;
#endif
	// If the sample already provides a texture resource, we simply use that
	// as the output render target. If the sample only provides raw data, then
	// we create our own output render target and copy the data into it.
	if (SampleTexture2D != nullptr)
	{
		UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:CopySample 1"));
		// use sample's texture as the new render target.
		if (TextureRHI != SampleTexture2D)
		{
			UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:CopySample 11"));
			UpdateTextureReference(SampleTexture2D);

			OutputTarget.SafeRelease();
			UpdateResourceSize();
		}
	}
	else
	{
		UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:CopySample 2"));
		// create a new output render target if necessary
#if UE_VERSION >= 426
		const ETextureCreateFlags OutputCreateFlags = TexCreate_Dynamic | TexCreate_SRGB;
#else
		const uint32 OutputCreateFlags = TexCreate_Dynamic | TexCreate_SRGB;
#endif
		const FIntPoint SampleDim = Sample->GetDim();

		if ((ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || (OutputTarget->GetSizeXY() != SampleDim) || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags))
		{
			UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:CopySample 1"));

#if UE_VERSION >= 501
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create2D(TEXT("FWebInterfaceBrowserTextureResource"))
				.SetExtent(SampleDim)
				.SetFormat(PF_B8G8R8A8)
				.SetFlags(OutputCreateFlags | ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)
				.SetInitialState(ERHIAccess::SRVMask)
				.SetClearValue(FClearValueBinding(ClearColor));

			OutputTarget = RHICreateTexture(Desc);
#else
#if UE_VERSION >= 505
			TRefCountPtr<FRHITexture> DummyTexture2DRHI;
#else
			TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
#endif

#if UE_VERSION >= 500
			FRHIResourceCreateInfo CreateInfo(TEXT("FWebBrowserTextureResource"), FClearValueBinding(ClearColor));
#else
			FRHIResourceCreateInfo CreateInfo = {
				FClearValueBinding(ClearColor)
			};
#endif

			RHICreateTargetableShaderResource2D(
				SampleDim.X,
				SampleDim.Y,
				PF_B8G8R8A8,
				1,
				OutputCreateFlags,
				TexCreate_RenderTargetable,
				false,
				CreateInfo,
				OutputTarget,
				DummyTexture2DRHI
			);
#endif

			CurrentClearColor = ClearColor;
			UpdateResourceSize();
		}

		if (RenderTargetTextureRHI != OutputTarget)
		{
			UpdateTextureReference(OutputTarget);
		}

		UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("WebInterfaceBrowserTextureResource:CopySample: %d x %d"), SampleDim.X, SampleDim.Y);

		// copy sample data to output render target
		FUpdateTextureRegion2D Region(0, 0, 0, 0, SampleDim.X, SampleDim.Y);
		RHIUpdateTexture2D(RenderTargetTextureRHI.GetReference(), 0, Region, Sample->GetStride(), (uint8*)Sample->GetBuffer());
	}
	Cleared = false;
}


void FWebInterfaceBrowserTextureResource::UpdateResourceSize()
{
	UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:UpdateResourceSize"));

	SIZE_T ResourceSize = 0;

	if (InputTarget.IsValid())
	{
		ResourceSize += CalcTextureSize(InputTarget->GetSizeX(), InputTarget->GetSizeY(), InputTarget->GetFormat(), 1);
	}

	if (OutputTarget.IsValid())
	{
		ResourceSize += CalcTextureSize(OutputTarget->GetSizeX(), OutputTarget->GetSizeY(), OutputTarget->GetFormat(), 1);
	}

	OwnerSize = ResourceSize;
}


#if UE_VERSION >= 505
void FWebInterfaceBrowserTextureResource::UpdateTextureReference(FRHITexture* NewTexture)
#else
void FWebInterfaceBrowserTextureResource::UpdateTextureReference(FRHITexture2D* NewTexture)
#endif
{
	TextureRHI = NewTexture;
	RenderTargetTextureRHI = NewTexture;

	RHIUpdateTextureReference(Owner.TextureReference.TextureReferenceRHI, NewTexture);

	if (RenderTargetTextureRHI != nullptr)
	{
		OwnerDim = FIntPoint(RenderTargetTextureRHI->GetSizeX(), RenderTargetTextureRHI->GetSizeY());
	}
	else
	{
		OwnerDim = FIntPoint::ZeroValue;
	}
	UE_LOG(LogWebInterfaceBrowserTexture, VeryVerbose, TEXT("FWebInterfaceBrowserTextureResource:UpdateTextureReference: %d x %d"), OwnerDim.X, OwnerDim.Y);
}
