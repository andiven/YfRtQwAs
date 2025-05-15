// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "AndroidWebInterfaceBrowserWidget.h"

#if USE_ANDROID_JNI

#include "AndroidWebInterfaceBrowserWindow.h"
#include "AndroidWebInterfaceBrowserDialog.h"
#include "MobileJS/MobileInterfaceJSScripting.h"
#include "Android/AndroidApplication.h"
#include "Android/AndroidWindow.h"
#include "Android/AndroidJava.h"
#include "Android/AndroidJavaEnv.h"
#include "Android/AndroidJNI.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "ExternalTexture.h"
#include "Slate/SlateTextures.h"
#include "SlateMaterialBrush.h"
#include "Templates/SharedPointer.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "WebInterfaceBrowserSchemeHandler.h"
#include "WebInterfaceBrowserTextureSample.h"
#include "WebInterfaceBrowserModule.h"
#include "IWebInterfaceBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

// For UrlDecode
#include "Http.h"

#include <jni.h>

extern bool AndroidThunkCpp_IsOculusMobileApplication();

FCriticalSection SAndroidWebInterfaceBrowserWidget::WebControlsCS;
TMap<int64, TWeakPtr<SAndroidWebInterfaceBrowserWidget>> SAndroidWebInterfaceBrowserWidget::AllWebControls;

TSharedPtr<SAndroidWebInterfaceBrowserWidget> SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JNIEnv* JEnv, jobject Jobj)
{
	FScopeLock L(&WebControlsCS);

	auto Class = NewScopedJavaObject(JEnv, JEnv->GetObjectClass(Jobj));
	jmethodID JMethod = JEnv->GetMethodID(*Class, "GetNativePtr", "()J");
	check(JMethod != nullptr);

	int64 ObjAddr = JEnv->CallLongMethod(Jobj, JMethod);

	TWeakPtr<SAndroidWebInterfaceBrowserWidget> WebControl = AllWebControls.FindRef(ObjAddr);
	return (WebControl.IsValid()) ? WebControl.Pin() : TSharedPtr<SAndroidWebInterfaceBrowserWidget>();
}

SAndroidWebInterfaceBrowserWidget::~SAndroidWebInterfaceBrowserWidget()
{
	if (JavaWebBrowser.IsValid())
	{
		if (GSupportsImageExternal && !FAndroidMisc::ShouldUseVulkan())
		{
			// Unregister the external texture on render thread
			FTextureRHIRef VideoTexture = JavaWebBrowser->GetVideoTexture();

			JavaWebBrowser->SetVideoTexture(nullptr);
			JavaWebBrowser->Release();

			struct FReleaseVideoResourcesParams
			{
				FTextureRHIRef VideoTexture;
				FGuid PlayerGuid;
			};

			FReleaseVideoResourcesParams ReleaseVideoResourcesParams = { VideoTexture, WebBrowserTexture->GetExternalTextureGuid() };

			ENQUEUE_RENDER_COMMAND(AndroidWebBrowserWriteVideoSample)(
				[Params = ReleaseVideoResourcesParams](FRHICommandListImmediate& RHICmdList)
				{
					FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PlayerGuid);
					// @todo: this causes a crash
					//					Params.VideoTexture->Release();
				});
		}
		else
		{
			JavaWebBrowser->SetVideoTexture(nullptr);
			JavaWebBrowser->Release();
		}

	}
	delete TextureSamplePool;
	TextureSamplePool = nullptr;
	
	WebBrowserTextureSamplesQueue->RequestFlush();

	if (WebBrowserMaterial != nullptr)
	{
		WebBrowserMaterial->RemoveFromRoot();
		WebBrowserMaterial = nullptr;
	}

	if (WebBrowserTexture != nullptr)
	{
		WebBrowserTexture->RemoveFromRoot();
		WebBrowserTexture = nullptr;
	}

	FScopeLock L(&WebControlsCS);
	AllWebControls.Remove(reinterpret_cast<int64>(this));

	if (auto Env = FAndroidApplication::GetJavaEnv())
	{
		Env->DeleteGlobalRef(InterceptedResponseHeadersClass);
	}
}

void SAndroidWebInterfaceBrowserWidget::Construct(const FArguments& Args)
{
	JNIEnv* JEnv = FAndroidApplication::GetJavaEnv();

	// get method IDs for InterceptedResponseHeaders class members
	InterceptedResponseHeadersClass = FAndroidApplication::FindJavaClassGlobalRef("com/epicgames/unreal/WebInterfaceViewControl$InterceptedResponseHeaders");
	InterceptedResponseHeaders_StatusCode   = FJavaWrapper::FindField(JEnv, InterceptedResponseHeadersClass, "StatusCode",   "I",                  false);
	InterceptedResponseHeaders_MimeType     = FJavaWrapper::FindField(JEnv, InterceptedResponseHeadersClass, "MimeType",     "Ljava/lang/String;", false);
	InterceptedResponseHeaders_Encoding     = FJavaWrapper::FindField(JEnv, InterceptedResponseHeadersClass, "Encoding",     "Ljava/lang/String;", false);
	InterceptedResponseHeaders_ReasonPhrase = FJavaWrapper::FindField(JEnv, InterceptedResponseHeadersClass, "ReasonPhrase", "Ljava/lang/String;", false);
	InterceptedResponseHeaders_Headers      = FJavaWrapper::FindField(JEnv, InterceptedResponseHeadersClass, "Headers",      "Ljava/util/Map;",    false);

	{
		FScopeLock L(&WebControlsCS);
		AllWebControls.Add(reinterpret_cast<int64>(this), StaticCastSharedRef<SAndroidWebInterfaceBrowserWidget>(AsShared()));
	}

	WebBrowserWindowPtr = Args._WebBrowserWindow;
	IsAndroid3DBrowser = true;

	bShouldUseBitmapRender = false; //AndroidThunkCpp_IsOculusMobileApplication();
	bMouseCapture = false;

	HistorySize = 0;
	HistoryPosition = 0;

	// Check if DOM storage should be enabled
	bool bEnableDomStorage = false;
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bEnableDomStorage"), bEnableDomStorage, GEngineIni);

	FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::Construct viewport=%d x %d"), viewportSize.X, viewportSize.Y);

	JavaWebBrowser = MakeShared<FJavaAndroidWebInterfaceBrowser, ESPMode::ThreadSafe>(false, FAndroidMisc::ShouldUseVulkan(), viewportSize.X, viewportSize.Y,
		reinterpret_cast<jlong>(this), !(UE_BUILD_SHIPPING || UE_BUILD_TEST), Args._UseTransparency, bEnableDomStorage, bShouldUseBitmapRender);

	TextureSamplePool = new FWebInterfaceBrowserTextureSamplePool();
	WebBrowserTextureSamplesQueue = MakeShared<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe>();
	WebBrowserTexture = nullptr;
	WebBrowserMaterial = nullptr;
	WebBrowserBrush = nullptr;

	// create external texture
	WebBrowserTexture = NewObject<UWebInterfaceBrowserTexture>((UObject*)GetTransientPackage(), NAME_None, RF_Transient | RF_Public);

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::Construct0"));
	if (WebBrowserTexture != nullptr)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::Construct01"));

		WebBrowserTexture->UpdateResource();
		WebBrowserTexture->AddToRoot();
	}

	// create wrapper material
	IWebInterfaceBrowserSingleton* WebBrowserSingleton = IWebInterfaceBrowserModule::Get().GetSingleton();
	if (WebBrowserSingleton)
	{
		UMaterialInterface* DefaultWBMaterial = Args._UseTransparency ?
			                                    WebBrowserSingleton->GetDefaultTranslucentMaterial() :
			                                    WebBrowserSingleton->GetDefaultMaterial();
		if (DefaultWBMaterial)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::Construct1"));
			// create wrapper material
			WebBrowserMaterial = UMaterialInstanceDynamic::Create(DefaultWBMaterial, nullptr);

			if (WebBrowserMaterial)
			{
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::Construct2"));
				WebBrowserMaterial->SetTextureParameterValue("SlateUI", WebBrowserTexture);
				WebBrowserMaterial->AddToRoot();

				// create Slate brush
				WebBrowserBrush = MakeShareable(new FSlateBrush());
				{
					WebBrowserBrush->SetResourceObject(WebBrowserMaterial);
				}
			}
		}
	}
	
	check(JavaWebBrowser.IsValid());

	JavaWebBrowser->LoadURL(Args._InitialURL);
}

void SAndroidWebInterfaceBrowserWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if(WebBrowserWindowPtr.IsValid())
	{
		WebBrowserWindowPtr.Pin()->SetTickLastFrame();
		if (WebBrowserWindowPtr.Pin()->GetParentWindow().IsValid())
		{
			bool ShouldSetAndroid3DBrowser = WebBrowserWindowPtr.Pin()->GetParentWindow().Get()->IsVirtualWindow();
			if (IsAndroid3DBrowser != ShouldSetAndroid3DBrowser)
			{
				IsAndroid3DBrowser = ShouldSetAndroid3DBrowser;
				JavaWebBrowser->SetAndroid3DBrowser(IsAndroid3DBrowser);
			}
		}
	}

	if (!JavaWebBrowser.IsValid())
	{	
		return;
	}
	// deal with resolution changes (usually from streams)
	if (JavaWebBrowser->DidResolutionChange())
	{
		JavaWebBrowser->SetVideoTextureValid(false);
	}


	FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();

	if (IsAndroid3DBrowser)
	{
		//FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		//FIntPoint IntLocalSize = FIntPoint((int32)LocalSize.X, (int32)LocalSize.Y);

		//if (viewportSize != IntLocalSize)
		//{
		//	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowser::Tick: updating viewport to localSize = %d x %d"), IntLocalSize.X, IntLocalSize.Y);
		//	WebBrowserWindowPtr.Pin()->SetViewportSize(IntLocalSize, FIntPoint(0, 0));
		//}

		JavaWebBrowser->Update(0, 0, viewportSize.X, viewportSize.Y);
	}
	else
	{
		// Calculate UIScale, which can vary frame-to-frame thanks to device rotation
		// UI Scale is calculated relative to vertical axis of 1280x720 / 720x1280
		float UIScale;
		FPlatformRect ScreenRect = FAndroidWindow::GetScreenRect();
		int32_t ScreenWidth, ScreenHeight;
		FAndroidWindow::CalculateSurfaceSize(ScreenWidth, ScreenHeight);
		if (ScreenWidth > ScreenHeight)
		{
			UIScale = (float)ScreenHeight / (ScreenRect.Bottom - ScreenRect.Top);
		}
		else
		{
			UIScale = (float)ScreenHeight / (ScreenRect.Bottom - ScreenRect.Top);
		}

		FVector2D Position = AllottedGeometry.GetAccumulatedRenderTransform().GetTranslation() * UIScale;
		FVector2D Size = TransformVector(AllottedGeometry.GetAccumulatedRenderTransform(), AllottedGeometry.GetLocalSize()) * UIScale;

		// Convert position to integer coordinates
		FIntPoint IntPos(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
		// Convert size to integer taking the rounding of position into account to avoid double round-down or double round-up causing a noticeable error.
		FIntPoint IntSize = FIntPoint(FMath::RoundToInt(Position.X + Size.X), FMath::RoundToInt(Size.Y + Position.Y)) - IntPos;

		JavaWebBrowser->Update(IntPos.X, IntPos.Y, IntSize.X, IntSize.Y);
	}

	if (IsAndroid3DBrowser)
	{
		if (WebBrowserTexture)
		{
			TSharedPtr<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe> WebBrowserTextureSample;
			WebBrowserTextureSamplesQueue->Peek(WebBrowserTextureSample);

			WebBrowserTexture->TickResource(WebBrowserTextureSample);
		}

		if (FAndroidMisc::ShouldUseVulkan())
		{
			// create new video sample
			auto NewTextureSample = TextureSamplePool->AcquireShared();

			if (!NewTextureSample->Initialize(viewportSize))
			{
				return;
			}

			struct FWriteWebInterfaceBrowserParams
			{
				TWeakPtr<FJavaAndroidWebInterfaceBrowser, ESPMode::ThreadSafe> JavaWebBrowserPtr;
				TWeakPtr<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSampleQueuePtr;
				TSharedRef<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe> NewTextureSamplePtr;
				int32 SampleCount;
			}
			WriteWebBrowserParams = { JavaWebBrowser, WebBrowserTextureSamplesQueue, NewTextureSample, (int32)(viewportSize.X * viewportSize.Y * sizeof(int32)) };

			if (bShouldUseBitmapRender)
			{
				ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
					[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
					{
						auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();
						auto PinnedSamples = Params.WebBrowserTextureSampleQueuePtr.Pin();

						if (!PinnedJavaWebBrowser.IsValid() || !PinnedSamples.IsValid())
						{
							return;
						}

						int32 SampleBufferSize = Params.NewTextureSamplePtr->InitializeBufferForCopy();
						void* Buffer = (void*)Params.NewTextureSamplePtr->GetBuffer();

						if (!PinnedJavaWebBrowser->GetVideoLastFrameBitmap(Buffer, SampleBufferSize))
						{
							return;
						}

						PinnedSamples->RequestFlush();
						PinnedSamples->Enqueue(Params.NewTextureSamplePtr);
					});
			}
			else
			{
				ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
					[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
					{
						auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();
						auto PinnedSamples = Params.WebBrowserTextureSampleQueuePtr.Pin();

						if (!PinnedJavaWebBrowser.IsValid() || !PinnedSamples.IsValid())
						{
							return;
						}

						bool bRegionChanged = false;

						// write frame into buffer
						void* Buffer = nullptr;
						int64 SampleCount = 0;

						if (!PinnedJavaWebBrowser->GetVideoLastFrameData(Buffer, SampleCount, &bRegionChanged))
						{
							//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: ShouldUseVulkan couldn't get texture buffer"));
							return;
						}

						if (SampleCount != Params.SampleCount)
						{
							FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::Fetch: Sample count mismatch (Buffer=%llu, Available=%llu"), Params.SampleCount, SampleCount);
						}
						check(Params.SampleCount <= SampleCount);

						// must make a copy (buffer is owned by Java, not us!)
						Params.NewTextureSamplePtr->InitializeBuffer(Buffer, true);

						PinnedSamples->RequestFlush();
						PinnedSamples->Enqueue(Params.NewTextureSamplePtr);
					});
			}
		}
		else if (GSupportsImageExternal && WebBrowserTexture != nullptr)
		{
			struct FWriteWebInterfaceBrowserParams
			{
				TWeakPtr<FJavaAndroidWebInterfaceBrowser, ESPMode::ThreadSafe> JavaWebBrowserPtr;
				FGuid PlayerGuid;
				FIntPoint Size;
			};

			FWriteWebInterfaceBrowserParams WriteWebBrowserParams = { JavaWebBrowser, WebBrowserTexture->GetExternalTextureGuid(), viewportSize };
			ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
				[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
				{
					auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();

					if (!PinnedJavaWebBrowser.IsValid())
					{
						return;
					}

					FTextureRHIRef VideoTexture = PinnedJavaWebBrowser->GetVideoTexture();
					if (VideoTexture == nullptr)
					{
#if UE_VERSION >= 501
						const FIntPoint LocalSize = Params.Size;

						const FRHITextureCreateDesc Desc =
							FRHITextureCreateDesc::Create2D(TEXT("VideoTexture"), LocalSize, PF_R8G8B8A8)
							.SetFlags(ETextureCreateFlags::External);

						VideoTexture = RHICreateTexture(Desc);
#else
						FRHIResourceCreateInfo CreateInfo(TEXT("VideoTexture"));
						FIntPoint LocalSize = Params.Size;

						VideoTexture = RHICreateTextureExternal2D(LocalSize.X, LocalSize.Y, PF_R8G8B8A8, 1, 1, TexCreate_None, CreateInfo);
#endif

						PinnedJavaWebBrowser->SetVideoTexture(VideoTexture);
						if (VideoTexture == nullptr)
						{
#if UE_VERSION >= 501
							UE_LOG(LogAndroid, Warning, TEXT("RHICreateTexture failed!"));
#else
							UE_LOG(LogAndroid, Warning, TEXT("CreateTextureExternal2D failed!"));
#endif
							return;
						}

						PinnedJavaWebBrowser->SetVideoTextureValid(false);
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: Created VideoTexture: %d - %s (%d, %d)"), *reinterpret_cast<int32*>(VideoTexture->GetNativeResource()), *Params.PlayerGuid.ToString(), LocalSize.X, LocalSize.Y);
					}

					int32 TextureId = *reinterpret_cast<int32*>(VideoTexture->GetNativeResource());
					bool bRegionChanged = false;
					if (PinnedJavaWebBrowser->UpdateVideoFrame(TextureId, &bRegionChanged))
					{
						// if region changed, need to reregister UV scale/offset
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("UpdateVideoFrame RT: %s"), *Params.PlayerGuid.ToString());
						if (bRegionChanged)
						{
							PinnedJavaWebBrowser->SetVideoTextureValid(false);
							FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: %s"), *Params.PlayerGuid.ToString());
						}
					}

					if (!PinnedJavaWebBrowser->IsVideoTextureValid())
					{
						FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
						FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
						FExternalTextureRegistry::Get().RegisterExternalTexture(Params.PlayerGuid, VideoTexture, SamplerStateRHI);
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: Register Guid: %s"), *Params.PlayerGuid.ToString());

						PinnedJavaWebBrowser->SetVideoTextureValid(true);
					}
				});
		}
		else
		{
			// create new video sample
			auto NewTextureSample = TextureSamplePool->AcquireShared();

			if (!NewTextureSample->Initialize(viewportSize))
			{
				return;
			}

			// populate & add sample (on render thread)
			struct FWriteWebInterfaceBrowserParams
			{
				TWeakPtr<FJavaAndroidWebInterfaceBrowser, ESPMode::ThreadSafe> JavaWebBrowserPtr;
				TWeakPtr<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSampleQueuePtr;
				TSharedRef<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe> NewTextureSamplePtr;
				int32 SampleCount;
			}
			WriteWebBrowserParams = { JavaWebBrowser, WebBrowserTextureSamplesQueue, NewTextureSample, (int32)(viewportSize.X * viewportSize.Y * sizeof(int32)) };

			ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
				[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
				{
					auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();
					auto PinnedSamples = Params.WebBrowserTextureSampleQueuePtr.Pin();

					if (!PinnedJavaWebBrowser.IsValid() || !PinnedSamples.IsValid())
					{
						return;
					}

					// write frame into texture
#if UE_VERSION >= 505
					FRHITexture* Texture = Params.NewTextureSamplePtr->InitializeTexture();
#else
					FRHITexture2D* Texture = Params.NewTextureSamplePtr->InitializeTexture();
#endif

					if (Texture != nullptr)
					{
						int32 Resource = *reinterpret_cast<int32*>(Texture->GetNativeResource());
						if (!PinnedJavaWebBrowser->GetVideoLastFrame(Resource))
						{
							return;
						}
					}

					PinnedSamples->RequestFlush();
					PinnedSamples->Enqueue(Params.NewTextureSamplePtr);
				});
		}
	}
}


int32 SAndroidWebInterfaceBrowserWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	bool bIsVisible = !WebBrowserWindowPtr.IsValid() || WebBrowserWindowPtr.Pin()->IsVisible();

	if (bIsVisible && IsAndroid3DBrowser && WebBrowserBrush.IsValid())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WebBrowserBrush.Get(), ESlateDrawEffect::None);
	}
	return LayerId;
}

FVector2D SAndroidWebInterfaceBrowserWidget::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(640, 480);
}

void SAndroidWebInterfaceBrowserWidget::ExecuteJavascript(const FString& Script)
{
	JavaWebBrowser->ExecuteJavascript(Script);
}

void SAndroidWebInterfaceBrowserWidget::LoadURL(const FString& NewURL)
{
	JavaWebBrowser->LoadURL(NewURL);
}

void SAndroidWebInterfaceBrowserWidget::LoadString(const FString& Contents, const FString& BaseUrl)
{
	JavaWebBrowser->LoadString(Contents, BaseUrl);
}

void SAndroidWebInterfaceBrowserWidget::StopLoad()
{
	JavaWebBrowser->StopLoad();
}

void SAndroidWebInterfaceBrowserWidget::Reload()
{
	JavaWebBrowser->Reload();
}

void SAndroidWebInterfaceBrowserWidget::Close()
{
	JavaWebBrowser->Release();
	WebBrowserWindowPtr.Reset();
}

void SAndroidWebInterfaceBrowserWidget::GoBack()
{
	JavaWebBrowser->GoBack();
}

void SAndroidWebInterfaceBrowserWidget::GoForward()
{
	JavaWebBrowser->GoForward();
}

bool SAndroidWebInterfaceBrowserWidget::CanGoBack()
{
	return HistoryPosition > 1;
}

bool SAndroidWebInterfaceBrowserWidget::CanGoForward()
{
	return HistoryPosition < HistorySize-1;
}

void SAndroidWebInterfaceBrowserWidget::SendTouchDown(FVector2D Position)
{
	FVector2D WidgetSize = GetCachedGeometry().GetLocalSize();
	JavaWebBrowser->SendTouchDown(Position.X / WidgetSize.X, Position.Y / WidgetSize.Y);
}

void SAndroidWebInterfaceBrowserWidget::SendTouchUp(FVector2D Position)
{
	FVector2D WidgetSize = GetCachedGeometry().GetLocalSize();
	JavaWebBrowser->SendTouchUp(Position.X / WidgetSize.X, Position.Y / WidgetSize.Y);
}

void SAndroidWebInterfaceBrowserWidget::SendTouchMove(FVector2D Position)
{
	FVector2D WidgetSize = GetCachedGeometry().GetLocalSize();
	JavaWebBrowser->SendTouchMove(Position.X / WidgetSize.X, Position.Y / WidgetSize.Y);
}

FVector2D SAndroidWebInterfaceBrowserWidget::ConvertMouseEventToLocal(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FGeometry MouseGeometry = MyGeometry;

	float DPIScale = MouseGeometry.Scale;
	FVector2D LocalPos = MouseGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()) * DPIScale;

	return LocalPos;
}

FReply SAndroidWebInterfaceBrowserWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (IsAndroid3DBrowser)
	{
		FKey Button = MouseEvent.GetEffectingButton();
		bool bSupportedButton = (Button == EKeys::LeftMouseButton); // || Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton);

		if (bSupportedButton)
		{
			Reply = FReply::Handled();
			SendTouchDown(ConvertMouseEventToLocal(MyGeometry, MouseEvent));
			bMouseCapture = true;
		}
	}

	return Reply;
}

FReply SAndroidWebInterfaceBrowserWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (IsAndroid3DBrowser)
	{
		FKey Button = MouseEvent.GetEffectingButton();
		bool bSupportedButton = (Button == EKeys::LeftMouseButton); // || Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton);

		if (bSupportedButton)
		{
			Reply = FReply::Handled();
			SendTouchUp(ConvertMouseEventToLocal(MyGeometry, MouseEvent));
			bMouseCapture = false;
		}
	}

	return Reply;
}

FReply SAndroidWebInterfaceBrowserWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (IsAndroid3DBrowser && bMouseCapture)
	{
		Reply = FReply::Handled();
		SendTouchMove(ConvertMouseEventToLocal(MyGeometry, MouseEvent));
	}

	return Reply;
}

FReply SAndroidWebInterfaceBrowserWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!IsAndroid3DBrowser)
	{
		return FReply::Unhandled();
	}

//	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::OnKeyDown: %d"), InKeyEvent.GetCharacter());
	return JavaWebBrowser->SendKeyDown(InKeyEvent.GetCharacter()) ? FReply::Handled() : FReply::Unhandled();
}

FReply SAndroidWebInterfaceBrowserWidget::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!IsAndroid3DBrowser)
	{
		return FReply::Unhandled();
	}

//	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::OnKeyUp: %d"), InKeyEvent.GetCharacter());
	return JavaWebBrowser->SendKeyUp(InKeyEvent.GetCharacter()) ? FReply::Handled() : FReply::Unhandled();
}

FReply SAndroidWebInterfaceBrowserWidget::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
//	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SAndroidWebInterfaceBrowserWidget::OnKeyChar: %d"), (int32)InCharacterEvent.GetCharacter());
	if (IsAndroid3DBrowser)
	{
		if (JavaWebBrowser->SendKeyDown(InCharacterEvent.GetCharacter()))
		{
			JavaWebBrowser->SendKeyUp(InCharacterEvent.GetCharacter());
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SAndroidWebInterfaceBrowserWidget::SetWebBrowserVisibility(bool InIsVisible)
{
	JavaWebBrowser->SetVisibility(InIsVisible);
}

jbyteArray SAndroidWebInterfaceBrowserWidget::HandleShouldInterceptRequest(jstring JUrl, jobject JResponse)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);

	FString Response;
	bool bOverrideResponse = false;
	int32 Position = Url.Find(*FMobileInterfaceJSScripting::JSMessageTag, ESearchCase::CaseSensitive);
	if (Position >= 0)
	{
		AsyncTask(ENamedThreads::GameThread, [Url, Position, this]()
		{
			if (WebBrowserWindowPtr.IsValid())
			{
				TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
				if (BrowserWindow.IsValid())
				{
					FString Origin = Url.Left(Position);
					FString Message = Url.RightChop(Position + FMobileInterfaceJSScripting::JSMessageTag.Len());

					TArray<FString> Params;
					Message.ParseIntoArray(Params, TEXT("/"), false);
					if (Params.Num() > 0)
					{
						for (int I = 0; I < Params.Num(); I++)
						{
							Params[I] = FPlatformHttp::UrlDecode(Params[I]);
						}

						FString Command = Params[0];
						Params.RemoveAt(0, 1);
						BrowserWindow->OnJsMessageReceived(Command, Params, Origin);
					}
					else
					{
						GLog->Logf(ELogVerbosity::Error, TEXT("Invalid message from browser view: %s"), *Message);
					}
				}
			}
		});
		bOverrideResponse = true;
	}
	else
	{
	    FGraphEventRef OnLoadUrl = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
	    {
			if (WebBrowserWindowPtr.IsValid())
			{
				TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
				if (BrowserWindow.IsValid() && BrowserWindow->OnLoadUrl().IsBound())
				{
					bOverrideResponse = BrowserWindow->OnLoadUrl().Execute("", Url, Response);
				}
			}
		}, TStatId(), NULL, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnLoadUrl);
	}
	
	FString MimeType = "";
	FString Encoding = "";

	int32 StatusCode = 0;
	TMap<FString, FString> Headers;

	TArray<uint8> Content;
	if (bOverrideResponse)
	{
		FTCHARToUTF8 Converter(*Response);
		Content.SetNum(Converter.Length());
		FMemory::Memcpy(Content.GetData(), (uint8*)(ANSICHAR*)Converter.Get(), Content.Num());
	}
	else
	{
		FString FilePath = Url;
		if (FilePath.Contains("://"))
		{
			FString Scheme;
			if (FParse::SchemeNameFromURI(*FilePath, Scheme))
				FilePath = FilePath.RightChop(Scheme.Len() + 3);
		}

		if (FilePath.ToLower().StartsWith("game.local/"))
		{
			FilePath   = FilePath.RightChop(11);
			StatusCode = FWebInterfaceBrowserSchemeHandler::ProcessPath(FilePath, Content, MimeType, Encoding, Headers);

			bOverrideResponse = true;
		}
	}

	if (!bOverrideResponse)
		return nullptr;

	FScopedJavaObject<jstring> JMimeType = FJavaHelper::ToJavaString(JEnv, MimeType);
	FScopedJavaObject<jstring> JEncoding = FJavaHelper::ToJavaString(JEnv, Encoding);
	
	JEnv->SetObjectField(JResponse, InterceptedResponseHeaders_MimeType, *JMimeType);
	if (Encoding.Len() > 0)
		JEnv->SetObjectField(JResponse, InterceptedResponseHeaders_Encoding, *JEncoding);
	else
		JEnv->SetObjectField(JResponse, InterceptedResponseHeaders_Encoding, nullptr);

	if (StatusCode != 0)
	{
		FString ReasonPhrase = "Unknown";
		switch (StatusCode)
		{
			case 200: ReasonPhrase = "OK";                    break;
			case 204: ReasonPhrase = "No Content";            break;
			case 400: ReasonPhrase = "Bad Request";           break;
			case 401: ReasonPhrase = "Unauthorized";          break;
			case 403: ReasonPhrase = "Forbidden";             break;
			case 404: ReasonPhrase = "Not Found";             break;
			case 500: ReasonPhrase = "Internal Server Error"; break;
			case 501: ReasonPhrase = "Not Implemented";       break;
			case 502: ReasonPhrase = "Bad Gateway";           break;
			case 503: ReasonPhrase = "Service Unavailable";   break;
		}

		FScopedJavaObject<jstring> JReasonPhrase = FJavaHelper::ToJavaString(JEnv, ReasonPhrase);
		JEnv->SetObjectField(JResponse, InterceptedResponseHeaders_ReasonPhrase, *JReasonPhrase);
		JEnv->SetIntField   (JResponse, InterceptedResponseHeaders_StatusCode,   StatusCode);
	}

	if (Headers.Num() > 0)
	{
		static jclass    JMapClass       = FJavaWrapper::FindClassGlobalRef(JEnv, "java/util/HashMap", false);
		static jmethodID JMapConstructor = FJavaWrapper::FindMethod(JEnv, JMapClass, "<init>", "()V", false);
		static jmethodID JMap_put        = FJavaWrapper::FindMethod(JEnv, JMapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", false);

		FScopedJavaObject<jobject> JHeaders = NewScopedJavaObject(JEnv, JEnv->NewObject(JMapClass, JMapConstructor));
		for (const TPair<FString, FString>& Header : Headers)
		{
			FScopedJavaObject<jstring> JKey   = FJavaHelper::ToJavaString(JEnv, Header.Key);
			FScopedJavaObject<jstring> JValue = FJavaHelper::ToJavaString(JEnv, Header.Value);

			JEnv->CallObjectMethod(*JHeaders, JMap_put, *JKey, *JValue);
		}

		JEnv->SetObjectField(JResponse, InterceptedResponseHeaders_Headers, *JHeaders);
	}

	jbyteArray JContent = JEnv->NewByteArray(Content.Num());
	if (Content.Num() > 0)
		JEnv->SetByteArrayRegion(JContent, 0, Content.Num(), reinterpret_cast<const jbyte*>(Content.GetData()));

	return JContent;
}

bool SAndroidWebInterfaceBrowserWidget::HandleShouldOverrideUrlLoading(jstring JUrl)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);
	bool Retval = false;
	FGraphEventRef OnBeforeBrowse = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid())
			{
				if (BrowserWindow->OnBeforeBrowse().IsBound())
				{
					FWebNavigationRequest RequestDetails;
					RequestDetails.bIsRedirect = false;
					RequestDetails.bIsMainFrame = true; // shouldOverrideUrlLoading is only called on the main frame

					Retval = BrowserWindow->OnBeforeBrowse().Execute(Url, RequestDetails);
				}
			}
		}
	}, TStatId(), NULL, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnBeforeBrowse);

	return Retval;
}

bool SAndroidWebInterfaceBrowserWidget::HandleJsDialog(TSharedPtr<IWebInterfaceBrowserDialog>& Dialog)
{
	bool Retval = false;
	FGraphEventRef OnShowDialog = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid() && BrowserWindow->OnShowDialog().IsBound())
			{
				EWebInterfaceBrowserDialogEventResponse EventResponse = BrowserWindow->OnShowDialog().Execute(TWeakPtr<IWebInterfaceBrowserDialog>(Dialog));
				switch (EventResponse)
				{
				case EWebInterfaceBrowserDialogEventResponse::Handled:
					Retval = true;
					break;
				case EWebInterfaceBrowserDialogEventResponse::Continue:
					Dialog->Continue(true, (Dialog->GetType() == EWebInterfaceBrowserDialogType::Prompt) ? Dialog->GetDefaultPrompt() : FText::GetEmpty());
					Retval = true;
					break;
				case EWebInterfaceBrowserDialogEventResponse::Ignore:
					Dialog->Continue(false);
					Retval = true;
					break;
				case EWebInterfaceBrowserDialogEventResponse::Unhandled:
				default:
					Retval = false;
					break;
				}
			}
		}
	}, TStatId(), NULL, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnShowDialog);

	return Retval;
}

void SAndroidWebInterfaceBrowserWidget::HandleReceivedTitle(jstring JTitle)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Title = FJavaHelper::FStringFromParam(JEnv, JTitle);

	if (WebBrowserWindowPtr.IsValid())
	{
		TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid())
		{
			FGraphEventRef OnSetTitle = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				BrowserWindow->SetTitle(Title);
			}, TStatId(), NULL, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnSetTitle);
		}
	}
}

void SAndroidWebInterfaceBrowserWidget::HandlePageLoad(jstring JUrl, bool bIsLoading, int InHistorySize, int InHistoryPosition)
{
	HistorySize = InHistorySize;
	HistoryPosition = InHistoryPosition;

	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);
	if (WebBrowserWindowPtr.IsValid())
	{
		TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid())
		{
			FGraphEventRef OnNotifyDocumentLoadingStateChange = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				BrowserWindow->NotifyDocumentLoadingStateChange(Url, bIsLoading);
			}, TStatId(), NULL, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnNotifyDocumentLoadingStateChange);
		}
	}
}

void SAndroidWebInterfaceBrowserWidget::HandleReceivedError(jint ErrorCode, jstring /* ignore */, jstring JUrl)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);
	if (WebBrowserWindowPtr.IsValid())
	{
		TSharedPtr<FAndroidWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid())
		{
			FGraphEventRef OnNotifyDocumentError = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				BrowserWindow->NotifyDocumentError(Url, ErrorCode);
			}, TStatId(), NULL, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnNotifyDocumentError);
		}
	}
}

// Native method implementations:

JNI_METHOD jbyteArray Java_com_epicgames_unreal_WebInterfaceViewControl_00024ViewClient_shouldInterceptRequestImpl(JNIEnv* JEnv, jobject Client, jstring JUrl, jobject JResponse)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleShouldInterceptRequest(JUrl, JResponse);
	}
	else
	{
		return nullptr;
	}
}

JNI_METHOD jboolean Java_com_epicgames_unreal_WebInterfaceViewControl_00024ViewClient_shouldOverrideUrlLoading(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleShouldOverrideUrlLoading(JUrl);
	}
	else
	{
		return false;
	}
}

JNI_METHOD void Java_com_epicgames_unreal_WebInterfaceViewControl_00024ViewClient_onPageLoad(JNIEnv* JEnv, jobject Client, jstring JUrl, jboolean bIsLoading, jint HistorySize, jint HistoryPosition)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		Widget->HandlePageLoad(JUrl, bIsLoading, HistorySize, HistoryPosition);
	}
}

JNI_METHOD void Java_com_epicgames_unreal_WebInterfaceViewControl_00024ViewClient_onReceivedError(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jint ErrorCode, jstring Description, jstring JUrl)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		Widget->HandleReceivedError(ErrorCode, Description, JUrl);
	}
}

JNI_METHOD jboolean Java_com_epicgames_unreal_WebInterfaceViewControl_00024ChromeClient_onJsAlert(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jobject Result)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsDialog(EWebInterfaceBrowserDialogType::Alert, JUrl, Message, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD jboolean Java_com_epicgames_unreal_WebInterfaceViewControl_00024ChromeClient_onJsBeforeUnload(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jobject Result)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsDialog(EWebInterfaceBrowserDialogType::Unload, JUrl, Message, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD jboolean Java_com_epicgames_unreal_WebInterfaceViewControl_00024ChromeClient_onJsConfirm(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jobject Result)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsDialog(EWebInterfaceBrowserDialogType::Confirm, JUrl, Message, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD jboolean Java_com_epicgames_unreal_WebInterfaceViewControl_00024ChromeClient_onJsPrompt(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jstring DefaultValue, jobject Result)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsPrompt(JUrl, Message, DefaultValue, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD void Java_com_epicgames_unreal_WebInterfaceViewControl_00024ChromeClient_onReceivedTitle(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring Title)
{
	TSharedPtr<SAndroidWebInterfaceBrowserWidget> Widget = SAndroidWebInterfaceBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		Widget->HandleReceivedTitle(Title);
	}
}

#endif // USE_ANDROID_JNI
