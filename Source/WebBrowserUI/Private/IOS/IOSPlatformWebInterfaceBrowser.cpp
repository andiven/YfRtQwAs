// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "IOSPlatformWebInterfaceBrowser.h"

#if PLATFORM_IOS
#include "IOS/IOSView.h"
#include "IOS/IOSAppDelegate.h"
#include "Widgets/SLeafWidget.h"
#include "MobileJS/MobileInterfaceJSScripting.h"
#include "PlatformHttp.h"
#include "HAL/PlatformProcess.h"

#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#include "ExternalTexture.h"
#include "WebInterfaceBrowserModule.h"
#include "WebInterfaceBrowserSchemeHandler.h"
#include "IWebInterfaceBrowserSingleton.h"


class SIOSWebInterfaceBrowserWidget : public SLeafWidget
{
	SLATE_BEGIN_ARGS(SIOSWebInterfaceBrowserWidget)
		: _InitialURL("about:blank")
		, _UseTransparency(false)
	{ }

	SLATE_ARGUMENT(FString, InitialURL);
	SLATE_ARGUMENT(bool, UseTransparency);
	SLATE_ARGUMENT(TSharedPtr<FWebInterfaceBrowserWindow>, WebBrowserWindow);

	SLATE_END_ARGS()

		SIOSWebInterfaceBrowserWidget()
		: WebViewWrapper(nil)
	{}

	void Construct(const FArguments& Args)
	{
		bool bSupportsMetalMRT = false;
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetalMRT"), bSupportsMetalMRT, GEngineIni);

		bool bSupportsMetal = false;
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetal"), bSupportsMetal, GEngineIni);
		// At this point we MUST be a Metal renderer.
		check(bSupportsMetal);

		WebViewWrapper = [IOSWebInterfaceViewWrapper alloc];
		[WebViewWrapper create : TSharedPtr<SIOSWebInterfaceBrowserWidget>(this) useTransparency : Args._UseTransparency supportsMetal : bSupportsMetal supportsMetalMRT : bSupportsMetalMRT];

		WebBrowserWindowPtr = Args._WebBrowserWindow;
		IsIOS3DBrowser = false;

#if !PLATFORM_TVOS
		TextureSamplePool = new FWebInterfaceBrowserTextureSamplePool();
		WebBrowserTextureSamplesQueue = MakeShared<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe>();
		WebBrowserTexture = nullptr;
		WebBrowserMaterial = nullptr;
		WebBrowserBrush = nullptr;

		// create external texture
		WebBrowserTexture = NewObject<UWebInterfaceBrowserTexture>((UObject*)GetTransientPackage(), NAME_None, RF_Transient | RF_Public);

		if (WebBrowserTexture != nullptr)
		{
			WebBrowserTexture->UpdateResource();
			WebBrowserTexture->AddToRoot();
		}

		// create wrapper material
		IWebInterfaceBrowserSingleton* WebBrowserSingleton = IWebInterfaceBrowserModule::Get().GetSingleton();

		UMaterialInterface* DefaultWBMaterial = Args._UseTransparency ? WebBrowserSingleton->GetDefaultTranslucentMaterial() : WebBrowserSingleton->GetDefaultMaterial();
		if (WebBrowserSingleton && DefaultWBMaterial)
		{
			// create wrapper material
			WebBrowserMaterial = UMaterialInstanceDynamic::Create(DefaultWBMaterial, nullptr);

			if (WebBrowserMaterial)
			{
				WebBrowserMaterial->SetTextureParameterValue("SlateUI", WebBrowserTexture);
				WebBrowserMaterial->AddToRoot();

				// create Slate brush
				WebBrowserBrush = MakeShareable(new FSlateBrush());
				{
					WebBrowserBrush->SetResourceObject(WebBrowserMaterial);
				}
			}
		}
#endif
		LoadURL(Args._InitialURL);
	}


	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
	{
		if (WebViewWrapper != nil)
		{
			if (WebBrowserWindowPtr.IsValid())
			{
				WebBrowserWindowPtr.Pin()->SetTickLastFrame();
				if (WebBrowserWindowPtr.Pin()->GetParentWindow().IsValid())
				{
					bool ShouldSet3DBrowser = WebBrowserWindowPtr.Pin()->GetParentWindow().Get()->IsVirtualWindow();
					if (IsIOS3DBrowser != ShouldSet3DBrowser)
					{
						IsIOS3DBrowser = ShouldSet3DBrowser;
						[WebViewWrapper set3D : IsIOS3DBrowser];
					}
				}
			}

			UIView* View = [IOSAppDelegate GetDelegate].IOSView;
			CGFloat contentScaleFactor = View.contentScaleFactor;
			FVector2D Position = AllottedGeometry.GetAccumulatedRenderTransform().GetTranslation() / contentScaleFactor;
			FVector2D Size = TransformVector(AllottedGeometry.GetAccumulatedRenderTransform(), AllottedGeometry.GetLocalSize()) / contentScaleFactor;
			CGRect NewFrame;
			NewFrame.origin.x = FMath::RoundToInt(Position.X);
			NewFrame.origin.y = FMath::RoundToInt(Position.Y);
			NewFrame.size.width = FMath::RoundToInt(Size.X);
			NewFrame.size.height = FMath::RoundToInt(Size.Y);

			[WebViewWrapper updateframe : NewFrame];

#if !PLATFORM_TVOS
			if (IsIOS3DBrowser)
			{
				if (WebBrowserTexture)
				{
					TSharedPtr<FWebInterfaceBrowserTextureSample, ESPMode::ThreadSafe> WebBrowserTextureSample;
					WebBrowserTextureSamplesQueue->Peek(WebBrowserTextureSample);

					WebBrowserTexture->TickResource(WebBrowserTextureSample);
				}

				if (WebBrowserTexture != nullptr)
				{
					struct FWriteWebInterfaceBrowserParams
					{
						IOSWebInterfaceViewWrapper* NativeWebBrowserPtr;
						FGuid PlayerGuid;
						FIntPoint Size;
					};

					FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();

					FWriteWebInterfaceBrowserParams Params = { WebViewWrapper, WebBrowserTexture->GetExternalTextureGuid(), viewportSize };

					ENQUEUE_RENDER_COMMAND(WriteWebInterfaceBrowser)(
						[Params](FRHICommandListImmediate& RHICmdList)
						{
							IOSWebInterfaceViewWrapper* NativeWebBrowser = Params.NativeWebBrowserPtr;

							if (NativeWebBrowser == nil)
							{
								return;
							}

							FTextureRHIRef VideoTexture = [NativeWebBrowser GetVideoTexture];
							if (VideoTexture == nullptr)
							{
#if UE_VERSION >= 501
								const FRHITextureCreateDesc Desc =
									FRHITextureCreateDesc::Create2D(TEXT("SIOSWebInterfaceBrowserWidget_VideoTexture"), Params.Size, PF_R8G8B8A8)
									.SetFlags(ETextureCreateFlags::External);

								VideoTexture = RHICreateTexture(Desc);
#else
#if UE_VERSION >= 500
								FRHIResourceCreateInfo CreateInfo(TEXT("SIOSWebInterfaceBrowserWidget_VideoTexture"));
#else
								FRHIResourceCreateInfo CreateInfo;
#endif
								FIntPoint Size = Params.Size;
								VideoTexture = RHICreateTextureExternal2D(Size.X, Size.Y, PF_R8G8B8A8, 1, 1, TexCreate_None, CreateInfo);
#endif
								[NativeWebBrowser SetVideoTexture : VideoTexture];
								//UE_LOG(LogIOS, Log, TEXT("NativeWebBrowser SetVideoTexture:VideoTexture!"));

								if (VideoTexture == nullptr)
								{
#if UE_VERSION >= 501
									UE_LOG(LogIOS, Warning, TEXT("RHICreateTexture failed!"));
#else
									UE_LOG(LogIOS, Warning, TEXT("CreateTextureExternal2D failed!"));
#endif
									return;
								}

								[NativeWebBrowser SetVideoTextureValid : false];

							}

							if ([NativeWebBrowser UpdateVideoFrame : VideoTexture->GetNativeResource()])
							{
								// if region changed, need to reregister UV scale/offset
								//UE_LOG(LogIOS, Log, TEXT("UpdateVideoFrame RT: %s"), *Params.PlayerGuid.ToString());
							}

							if (![NativeWebBrowser IsVideoTextureValid])
							{
								FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
								FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
								FExternalTextureRegistry::Get().RegisterExternalTexture(Params.PlayerGuid, VideoTexture, SamplerStateRHI);
								//UE_LOG(LogIOS, Log, TEXT("Fetch RT: Register Guid: %s"), *Params.PlayerGuid.ToString());

								[NativeWebBrowser SetVideoTextureValid : true];
							}
						});
				}
			}
#endif
		}

	}

	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
	{
#if !PLATFORM_TVOS
		bool bIsVisible = !WebBrowserWindowPtr.IsValid() || WebBrowserWindowPtr.Pin()->IsVisible();
		
		if (bIsVisible && IsIOS3DBrowser && WebBrowserBrush.IsValid())
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WebBrowserBrush.Get(), ESlateDrawEffect::None);
		}
#endif
		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(640, 480);
	}

	void LoadURL(const FString& InNewURL)
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper loadurl : [NSURL URLWithString : [NSString stringWithUTF8String : TCHAR_TO_UTF8(*InNewURL)]]];
		}
	}

	void LoadString(const FString& InContents, const FString& InDummyURL)
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper loadstring : [NSString stringWithUTF8String : TCHAR_TO_UTF8(*InContents)] dummyurl : [NSURL URLWithString : [NSString stringWithUTF8String : TCHAR_TO_UTF8(*InDummyURL)]]];
		}
	}
	
	void StopLoad()
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper stopLoading];
		}
	}

	void Reload()
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper reload];
		}
	}

	void Close()
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper close];
			WebViewWrapper = nil;
		}
		WebBrowserWindowPtr.Reset();
	}

	void GoBack()
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper goBack];
		}
	}

	void GoForward()
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper goForward];
		}
	}


	bool CanGoBack()
	{
		if (WebViewWrapper != nil)
		{
			return [WebViewWrapper canGoBack];
		}
		return false;
	}

	bool CanGoForward()
	{
		if (WebViewWrapper != nil)
		{
			return [WebViewWrapper canGoForward];
		}
		return false;
	}

	void SetWebBrowserVisibility(bool InIsVisible)
	{
		if (WebViewWrapper != nil)
		{
			UE_LOG(LogIOS, Warning, TEXT("SetWebBrowserVisibility %d!"), InIsVisible);

			[WebViewWrapper setVisibility : InIsVisible];
		}
	}

	bool HandleOnBeforePopup(const FString& UrlStr, const FString& FrameName)
	{
		TSharedPtr<FWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid() && BrowserWindow->OnBeforePopup().IsBound())
		{
			return BrowserWindow->OnBeforePopup().Execute(UrlStr, FrameName);
		}
		return false;
	}

	bool HandleShouldOverrideUrlLoading(const FString& Url)
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			// Capture vars needed for AsyncTask
			NSString* UrlString = [NSString stringWithUTF8String : TCHAR_TO_UTF8(*Url)];
			TWeakPtr<FWebInterfaceBrowserWindow> AsyncWebBrowserWindowPtr = WebBrowserWindowPtr;

			// Notify on the game thread
			[FIOSAsyncTask CreateTaskWithBlock : ^ bool(void)
			{
				TSharedPtr<FWebInterfaceBrowserWindow> BrowserWindow = AsyncWebBrowserWindowPtr.Pin();
				if (BrowserWindow.IsValid())
				{
					if (BrowserWindow->OnBeforeBrowse().IsBound())
					{
						FWebNavigationRequest RequestDetails;
						RequestDetails.bIsRedirect = false;
						RequestDetails.bIsMainFrame = true; // shouldOverrideUrlLoading is only called on the main frame

						BrowserWindow->OnBeforeBrowse().Execute(UrlString, RequestDetails);
						BrowserWindow->SetTitle("");
					}
				}
				return true;
			}];
		}
		return true;
	}

	void HandleReceivedTitle(const FString& Title)
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid() && !BrowserWindow->GetTitle().Equals(Title))
			{
				BrowserWindow->SetTitle(Title);
			}
		}
	}

	void ProcessScriptMessage(const FString& InMessage)
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			FString Message = InMessage;
			TWeakPtr<FWebInterfaceBrowserWindow> AsyncWebBrowserWindowPtr = WebBrowserWindowPtr;

			[FIOSAsyncTask CreateTaskWithBlock : ^ bool(void)
			{
				TSharedPtr<FWebInterfaceBrowserWindow> BrowserWindow = AsyncWebBrowserWindowPtr.Pin();
				if (BrowserWindow.IsValid())
				{
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
						BrowserWindow->OnJsMessageReceived(Command, Params, "");
					}
					else
					{
						GLog->Logf(ELogVerbosity::Error, TEXT("Invalid message from browser view: %s"), *Message);
					}
				}
				return true;
			}];
		}
	}

	void HandlePageLoad(const FString& InCurrentUrl, bool bIsLoading)
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid())
			{
				BrowserWindow->NotifyDocumentLoadingStateChange(InCurrentUrl, bIsLoading);
			}
		}
	}

	void HandleReceivedError(int ErrorCode, const FString& InCurrentUrl)
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FWebInterfaceBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid())
			{
				BrowserWindow->NotifyDocumentError(InCurrentUrl, ErrorCode);
			}
		}
	}

	void ExecuteJavascript(const FString& Script)
	{
		if (WebViewWrapper != nil)
		{
			[WebViewWrapper executejavascript : [NSString stringWithUTF8String : TCHAR_TO_UTF8(*Script)]];
		}
	}


	~SIOSWebInterfaceBrowserWidget()
	{
		Close();
	}

protected:
	mutable __strong IOSWebInterfaceViewWrapper* WebViewWrapper;
private:
	TWeakPtr<FWebInterfaceBrowserWindow> WebBrowserWindowPtr;

	/** Enable 3D appearance */
	bool IsIOS3DBrowser;

#if !PLATFORM_TVOS
	/** The external texture to render the webbrowser output. */
	UWebInterfaceBrowserTexture* WebBrowserTexture;

	/** The material for the external texture. */
	UMaterialInstanceDynamic* WebBrowserMaterial;

	/** The Slate brush that renders the material. */
	TSharedPtr<FSlateBrush> WebBrowserBrush;

	/** The sample queue. */
	TSharedPtr<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSamplesQueue;

	/** Texture sample object pool. */
	FWebInterfaceBrowserTextureSamplePool* TextureSamplePool;
#endif
};

@implementation IOSWebInterfaceViewWrapper

#if !PLATFORM_TVOS
@synthesize WebView;
@synthesize WebViewContainer;
#endif
@synthesize NextURL;
@synthesize NextContent;

-(void)create:(TSharedPtr<SIOSWebInterfaceBrowserWidget>)InWebBrowserWidget useTransparency : (bool)InUseTransparency
supportsMetal : (bool)InSupportsMetal supportsMetalMRT : (bool)InSupportsMetalMRT;
{
	WebBrowserWidget = InWebBrowserWidget;
	NextURL = nil;
	NextContent = nil;
	VideoTexture = nil;
	bNeedsAddToView = true;
	IsIOS3DBrowser = false;
	bVideoTextureValid = false;
	bSupportsMetal = InSupportsMetal;
	bSupportsMetalMRT = InSupportsMetalMRT;

#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		WebViewContainer = [[UIView alloc]initWithFrame:CGRectMake(1, 1, 100, 100)];
		[self.WebViewContainer setOpaque : NO];
		[self.WebViewContainer setBackgroundColor : [UIColor clearColor]];

		WKWebViewConfiguration *theConfiguration = [[WKWebViewConfiguration alloc] init];
		NSString* MessageHandlerName = [NSString stringWithFString : FMobileInterfaceJSScripting::JSMessageHandler];
		[theConfiguration.userContentController addScriptMessageHandler:self name: MessageHandlerName];

		IOSWebInterfaceSchemeHandler *schemeHandler = [[IOSWebInterfaceSchemeHandler alloc] init];
		[theConfiguration setURLSchemeHandler:schemeHandler forURLScheme:@"http"];

		WebView = [[WKWebView alloc]initWithFrame:CGRectMake(1, 1, 100, 100)  configuration : theConfiguration];
		[self.WebViewContainer addSubview : WebView];
		WebView.navigationDelegate = self;
		WebView.UIDelegate = self;

		WebView.scrollView.bounces = NO;
	  //WebView.scrollView.alwaysBounceHorizontal = NO;
	  //WebView.scrollView.pinchGestureRecognizer.enabled = NO;

		if (InUseTransparency)
		{
			[self.WebView setOpaque : NO];
			[self.WebView setBackgroundColor : [UIColor clearColor]];
		}
		else
		{
			[self.WebView setOpaque : YES];
		}

		[theConfiguration release];
		[self setDefaultVisibility];
	});
#endif
}

-(void)close;
{
#if !PLATFORM_TVOS
	WebView.navigationDelegate = nil;
	dispatch_async(dispatch_get_main_queue(), ^
	{
		[self.WebViewContainer removeFromSuperview];
		[self.WebView removeFromSuperview];
		
		[WebView release];
		[WebViewContainer release];

		WebView = nil;
		WebViewContainer = nil;
	});
#endif
}

-(void)dealloc;
{
#if !PLATFORM_TVOS
	if (WebView != nil)
	{
		WebView.navigationDelegate = nil;
		[WebView release];
		WebView = nil;
	};

	[WebViewContainer release];
	WebViewContainer = nil;
#endif
	[NextContent release];
	NextContent = nil;
	[NextURL release];
	NextURL = nil;

	[super dealloc];
}

-(void)updateframe:(CGRect)InFrame;
{
	self.DesiredFrame = InFrame;

#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		if (WebView != nil)
		{
			WebViewContainer.frame = self.DesiredFrame;
			WebView.frame = WebViewContainer.bounds;
			if (bNeedsAddToView)
			{
				bNeedsAddToView = false;
				[[IOSAppDelegate GetDelegate].IOSView addSubview : WebViewContainer];
			}
			else
			{
				if (NextContent != nil)
				{
					// Load web content from string
					[self.WebView loadHTMLString : NextContent baseURL : NextURL];
					NextContent = nil;
					NextURL = nil;
				}
				else
					if (NextURL != nil)
					{
						// Load web content from URL
						NSURLRequest *nsrequest = [NSURLRequest requestWithURL : NextURL];
						[self.WebView loadRequest : nsrequest];
						NextURL = nil;
					}
			}
		}
	});
#endif
}

-(NSString *)UrlDecode:(NSString *)stringToDecode
{
	NSString *result = [stringToDecode stringByReplacingOccurrencesOfString : @"+" withString:@" "];
	result = [result stringByRemovingPercentEncoding];
	return result;
}

#if !PLATFORM_TVOS
-(void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage : (WKScriptMessage *)message
{
	if ([message.body isKindOfClass : [NSString class]])
	{
		NSString *Message = message.body;
		if (Message != nil)
		{
			//NSLog(@"Received message %@", Message);
			WebBrowserWidget->ProcessScriptMessage(Message);
		}

	}
}
#endif

-(void)executejavascript:(NSString*)InJavaScript
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
	//	NSLog(@"executejavascript %@", InJavaScript);
		[self.WebView evaluateJavaScript : InJavaScript completionHandler : nil];
	});
#endif
}

-(void)loadurl:(NSURL*)InURL;
{
	dispatch_async(dispatch_get_main_queue(), ^
	{
		self.NextURL = InURL;
	});
}

-(void)loadstring:(NSString*)InString dummyurl : (NSURL*)InURL;
{
	dispatch_async(dispatch_get_main_queue(), ^
	{
		self.NextContent = InString;
		self.NextURL = InURL;
	});
}

-(void)set3D:(bool)InIsIOS3DBrowser;
{
	dispatch_async(dispatch_get_main_queue(), ^
	{
		if (IsIOS3DBrowser != InIsIOS3DBrowser)
		{
			//default is 2D
			IsIOS3DBrowser = InIsIOS3DBrowser;
			[self setDefaultVisibility];
		}
	});
}

-(void)setDefaultVisibility;
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		if (IsIOS3DBrowser)
		{
			[self.WebViewContainer setHidden : YES];
		}
		else
		{
			[self.WebViewContainer setHidden : NO];
		}
	});
#endif
}

-(void)setVisibility:(bool)InIsVisible;
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		if (InIsVisible)
		{
			[self setDefaultVisibility];
		}
		else
		{
			[self.WebViewContainer setHidden : YES];
		}
	});
#endif
}

-(void)stopLoading;
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		[self.WebView stopLoading];
	});
#endif
}

-(void)reload;
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		[self.WebView reload];
	});
#endif
}

-(void)goBack;
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		[self.WebView goBack];
	});
#endif
}

-(void)goForward;
{
#if !PLATFORM_TVOS
	dispatch_async(dispatch_get_main_queue(), ^
	{
		[self.WebView goForward];
	});
#endif
}

-(bool)canGoBack;
{
#if PLATFORM_TVOS
	return false;
#else
	return [self.WebView canGoBack];
#endif
}

-(bool)canGoForward;
{
#if PLATFORM_TVOS
	return false;
#else
	return [self.WebView canGoForward];
#endif
}

-(FTextureRHIRef)GetVideoTexture;
{
	return VideoTexture;
}

-(void)SetVideoTexture:(FTextureRHIRef)Texture;
{
	VideoTexture = Texture;
}

-(void)SetVideoTextureValid:(bool)Condition;
{
	bVideoTextureValid = Condition;
}

-(bool)IsVideoTextureValid;
{
	return bVideoTextureValid;
}

-(bool)UpdateVideoFrame:(void*)ptr;
{
#if !PLATFORM_TVOS
	@synchronized(self) // Briefly block render thread
	{
		id<MTLTexture> ptrToMetalTexture = (id<MTLTexture>)ptr;
		NSUInteger width = [ptrToMetalTexture width];
		NSUInteger height = [ptrToMetalTexture height];

		[self updateWebViewMetalTexture : ptrToMetalTexture];
	}
#endif
	return true;
}

-(void)updateWebViewMetalTexture:(id<MTLTexture>)texture
{
#if !PLATFORM_TVOS
	@autoreleasepool {
		UIGraphicsBeginImageContextWithOptions(WebView.frame.size, NO, 1.0f);
		[WebView drawViewHierarchyInRect : WebView.bounds afterScreenUpdates : NO];
		UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
		UIGraphicsEndImageContext();
		NSUInteger width = [texture width];
		NSUInteger height = [texture height];
		CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
		CGContextRef context = CGBitmapContextCreate(NULL, width, height, 8, 4 * width, colorSpace, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
		CGContextDrawImage(context, CGRectMake(0, 0, width, height), image.CGImage);
		[texture replaceRegion : MTLRegionMake2D(0, 0, width, height)
			mipmapLevel : 0
			withBytes : CGBitmapContextGetData(context)
			bytesPerRow : 4 * width];
		CGColorSpaceRelease(colorSpace);
		CGContextRelease(context);
		image = nil;
	}
#endif
}

#if !PLATFORM_TVOS
- (nullable WKWebView *)webView:(WKWebView *)InWebView createWebViewWithConfiguration:(WKWebViewConfiguration *)InConfiguration forNavigationAction:(WKNavigationAction *)InNavigationAction windowFeatures:(WKWindowFeatures *)InWindowFeatures
{
	NSURLRequest *request = InNavigationAction.request;
	FString UrlStr([[request URL] absoluteString]);

	if (InNavigationAction.targetFrame == nil && !UrlStr.IsEmpty() && FPlatformProcess::CanLaunchURL(*UrlStr))
	{
		if (WebBrowserWidget->HandleOnBeforePopup(UrlStr, TEXT("_blank")))
		{
			// Launched the URL in external browser, don't create a new webview
			return nil;
		}
	}
	return nil;
}

- (void)webView:(WKWebView*)InWebView decidePolicyForNavigationAction : (WKNavigationAction*)InNavigationAction decisionHandler : (void(^)(WKNavigationActionPolicy))InDecisionHandler
{
	NSURLRequest *request = InNavigationAction.request;
	FString UrlStr([[request URL] absoluteString]);

	if (InNavigationAction.targetFrame == nil && !UrlStr.IsEmpty() && FPlatformProcess::CanLaunchURL(*UrlStr))
	{
		if (WebBrowserWidget->HandleOnBeforePopup(UrlStr, TEXT("_blank")))
		{
			// Launched the URL in external browser, don't open the link here too
			InDecisionHandler(WKNavigationActionPolicyCancel);
			return;
		}
	}
	
	bool bOverride = WebBrowserWidget->HandleShouldOverrideUrlLoading(UrlStr);
  //if (bOverride)
  //	InDecisionHandler(WKNavigationActionPolicyCancel);
  //else
		InDecisionHandler(WKNavigationActionPolicyAllow);
}

-(void)webView:(WKWebView *)InWebView didCommitNavigation : (WKNavigation *)InNavigation
{
	NSString* CurrentUrl = [self.WebView URL].absoluteString;
	NSString* Title = [self.WebView title];
	
//	NSLog(@"didCommitNavigation: %@", CurrentUrl);
	WebBrowserWidget->HandleReceivedTitle(Title);
	WebBrowserWidget->HandlePageLoad(CurrentUrl, true);
}

-(void)webView:(WKWebView *)InWebView didFinishNavigation : (WKNavigation *)InNavigation
{
	NSString* CurrentUrl = [self.WebView URL].absoluteString;
	NSString* Title = [self.WebView title];
	// NSLog(@"didFinishNavigation: %@", CurrentUrl);
	WebBrowserWidget->HandleReceivedTitle(Title);
	WebBrowserWidget->HandlePageLoad(CurrentUrl, false);
}
-(void)webView:(WKWebView *)InWebView didFailNavigation : (WKNavigation *)InNavigation withError : (NSError*)InError
{
	if (InError.domain == NSURLErrorDomain && InError.code == NSURLErrorCancelled)
	{
		//ignore this one, interrupted load
		return;
	}
	NSString* CurrentUrl = [InError.userInfo objectForKey : @"NSErrorFailingURLStringKey"];
//	NSLog(@"didFailNavigation: %@, error %@", CurrentUrl, InError);
	WebBrowserWidget->HandleReceivedError(InError.code, CurrentUrl);
}
-(void)webView:(WKWebView *)InWebView didFailProvisionalNavigation : (WKNavigation *)InNavigation withError : (NSError*)InError
{
	NSString* CurrentUrl = [InError.userInfo objectForKey : @"NSErrorFailingURLStringKey"];
//	NSLog(@"didFailProvisionalNavigation: %@, error %@", CurrentUrl, InError);
	WebBrowserWidget->HandleReceivedError(InError.code, CurrentUrl);
}
#endif
@end

#if !PLATFORM_TVOS
@implementation IOSWebInterfaceSchemeHandler

-(void)webView:(WKWebView *)InWebView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask
{
    NSURLRequest *request = urlSchemeTask.request;
    NSString *host = request.URL.host;
    
    if ([host isEqualToString:@"game.local"])
    {
        NSString *path = [request.URL.path stringByRemovingPercentEncoding];
        FString FilePath(path);

        TArray<uint8> Content;
        TMap<FString, FString> Headers;

        FString MimeType = "text/html";
        FString Encoding = "utf-8";

        int32 StatusCode = FWebInterfaceBrowserSchemeHandler::ProcessPath(FilePath, Content, MimeType, Encoding, Headers);
        if (StatusCode > 0)
        {
            NSData *data = [NSData dataWithBytes:Content.GetData() length:Content.Num()];
            NSMutableDictionary *responseHeaders = [NSMutableDictionary dictionary];

            [responseHeaders setObject:[NSString stringWithFString:MimeType] forKey:@"Content-Type"];
            [responseHeaders setObject:[NSString stringWithFString:Encoding] forKey:@"Content-Encoding"];
            [responseHeaders setObject:[NSString stringWithFormat:@"%lu", (unsigned long)data.length] forKey:@"Content-Length"];

            for (const TPair<FString, FString>& Header : Headers)
            {
                [responseHeaders setObject:[NSString stringWithFString:Header.Value] forKey:[NSString stringWithFString:Header.Key]];
            }

            NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:request.URL
                                                                      statusCode:StatusCode
                                                                     HTTPVersion:@"HTTP/1.1"
                                                                    headerFields:responseHeaders];
            
            [urlSchemeTask didReceiveResponse:response];
            [urlSchemeTask didReceiveData:data];
            [urlSchemeTask didFinish];
        }
		else
		{
			NSError *error = [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil];
            [urlSchemeTask didFailWithError:error];
		}
    }
}

-(void)webView:(WKWebView *)InWebView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask
{
	//
}

@end
#endif

namespace {
	static const FString JSGetSourceCommand = TEXT("GetSource");
	static const FString JSMessageGetSourceScript =
		TEXT("	window.webkit.messageHandlers.") + FMobileInterfaceJSScripting::JSMessageHandler + TEXT(".postMessage('")+ JSGetSourceCommand +
		TEXT("/' + encodeURIComponent(document.documentElement.innerHTML));");

}

FWebInterfaceBrowserWindow::FWebInterfaceBrowserWindow(FString InUrl, TOptional<FString> InContentsToLoad, bool InShowErrorMessage, bool InThumbMouseButtonNavigation, bool InUseTransparency, bool bInJSBindingToLoweringEnabled)
	: CurrentUrl(MoveTemp(InUrl))
	, ContentsToLoad(MoveTemp(InContentsToLoad))
	, bUseTransparency(InUseTransparency)
	, DocumentState(EWebInterfaceBrowserDocumentState::NoDocument)
	, ErrorCode(0)
	, Scripting(new FMobileInterfaceJSScripting(bInJSBindingToLoweringEnabled))
	, IOSWindowSize(FIntPoint(500, 500))
	, bIsDisabled(false)
	, bIsVisible(true)
	, bTickedLastFrame(true)
{
}

FWebInterfaceBrowserWindow::~FWebInterfaceBrowserWindow()
{
	CloseBrowser(true, false);
}

void FWebInterfaceBrowserWindow::LoadURL(FString NewURL)
{
	BrowserWidget->LoadURL(NewURL);
}

void FWebInterfaceBrowserWindow::LoadString(FString Contents, FString DummyURL)
{
	BrowserWidget->LoadString(Contents, DummyURL);
}

TSharedRef<SWidget> FWebInterfaceBrowserWindow::CreateWidget()
{
	TSharedRef<SIOSWebInterfaceBrowserWidget> BrowserWidgetRef =
		SNew(SIOSWebInterfaceBrowserWidget)
		.UseTransparency(bUseTransparency)
		.InitialURL(CurrentUrl)
		.WebBrowserWindow(SharedThis(this));

	BrowserWidget = BrowserWidgetRef;

	Scripting->SetWindow(SharedThis(this));

	return BrowserWidgetRef;
}

void FWebInterfaceBrowserWindow::SetViewportSize(FIntPoint WindowSize, FIntPoint WindowPos)
{
	IOSWindowSize = WindowSize;
}

FIntPoint FWebInterfaceBrowserWindow::GetViewportSize() const
{
	return IOSWindowSize;
}

FSlateShaderResource* FWebInterfaceBrowserWindow::GetTexture(bool bIsPopup /*= false*/)
{
	return nullptr;
}

bool FWebInterfaceBrowserWindow::IsValid() const
{
	return false;
}

bool FWebInterfaceBrowserWindow::IsInitialized() const
{
	return true;
}

bool FWebInterfaceBrowserWindow::IsClosing() const
{
	return false;
}

EWebInterfaceBrowserDocumentState FWebInterfaceBrowserWindow::GetDocumentLoadingState() const
{
	return DocumentState;
}

FString FWebInterfaceBrowserWindow::GetTitle() const
{
	return Title;
}

FString FWebInterfaceBrowserWindow::GetUrl() const
{
	return CurrentUrl;
}

bool FWebInterfaceBrowserWindow::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	return false;
}

bool FWebInterfaceBrowserWindow::OnKeyUp(const FKeyEvent& InKeyEvent)
{
	return false;
}

bool FWebInterfaceBrowserWindow::OnKeyChar(const FCharacterEvent& InCharacterEvent)
{
	return false;
}

FReply FWebInterfaceBrowserWindow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FWebInterfaceBrowserWindow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FWebInterfaceBrowserWindow::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FWebInterfaceBrowserWindow::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

void FWebInterfaceBrowserWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
}

void FWebInterfaceBrowserWindow::SetSupportsMouseWheel(bool bValue)
{

}

bool FWebInterfaceBrowserWindow::GetSupportsMouseWheel() const
{
	return false;
}

FReply FWebInterfaceBrowserWindow::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FWebInterfaceBrowserWindow::OnTouchGesture(const FGeometry& MyGeometry, const FPointerEvent& GestureEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}


void FWebInterfaceBrowserWindow::OnFocus(bool SetFocus, bool bIsPopup)
{
}

void FWebInterfaceBrowserWindow::OnCaptureLost()
{
}

bool FWebInterfaceBrowserWindow::CanGoBack() const
{
	return BrowserWidget->CanGoBack();
}

void FWebInterfaceBrowserWindow::GoBack()
{
	BrowserWidget->GoBack();
}

bool FWebInterfaceBrowserWindow::CanGoForward() const
{
	return BrowserWidget->CanGoForward();
}

void FWebInterfaceBrowserWindow::GoForward()
{
	BrowserWidget->GoForward();
}

bool FWebInterfaceBrowserWindow::IsLoading() const
{
	return DocumentState != EWebInterfaceBrowserDocumentState::Loading;
}

void FWebInterfaceBrowserWindow::Reload()
{
	BrowserWidget->Reload();
}

void FWebInterfaceBrowserWindow::StopLoad()
{
	BrowserWidget->StopLoad();
}

void FWebInterfaceBrowserWindow::GetSource(TFunction<void(const FString&)> Callback) const
{
	//@todo: decide what to do about multiple pending requests
	GetPageSourceCallback.Emplace(Callback);

	// Ugly hack: Work around the fact that ExecuteJavascript is non-const.
	const_cast<FWebInterfaceBrowserWindow*>(this)->ExecuteJavascript(JSMessageGetSourceScript);
}

int FWebInterfaceBrowserWindow::GetLoadError()
{
	return ErrorCode;
}

void FWebInterfaceBrowserWindow::NotifyDocumentError(const FString& InCurrentUrl, int InErrorCode)
{
	if (!CurrentUrl.Equals(InCurrentUrl, ESearchCase::CaseSensitive))
	{
		CurrentUrl = InCurrentUrl;
		UrlChangedEvent.Broadcast(CurrentUrl);
	}

	ErrorCode = InErrorCode;
	DocumentState = EWebInterfaceBrowserDocumentState::Error;
	DocumentStateChangedEvent.Broadcast(DocumentState);
}

void FWebInterfaceBrowserWindow::NotifyDocumentLoadingStateChange(const FString& InCurrentUrl, bool IsLoading)
{
	// Ignore a load completed notification if there was an error.
	// For load started, reset any errors from previous page load.
	if (IsLoading || DocumentState != EWebInterfaceBrowserDocumentState::Error)
	{
		if (!CurrentUrl.Equals(InCurrentUrl, ESearchCase::CaseSensitive))
		{
			CurrentUrl = InCurrentUrl;
			UrlChangedEvent.Broadcast(CurrentUrl);
		}

		if (!IsLoading && !InCurrentUrl.StartsWith("javascript:"))
		{
			Scripting->PageLoaded(SharedThis(this));
		}
		ErrorCode = 0;
		DocumentState = IsLoading
			? EWebInterfaceBrowserDocumentState::Loading
			: EWebInterfaceBrowserDocumentState::Completed;
		DocumentStateChangedEvent.Broadcast(DocumentState);
	}

}

void FWebInterfaceBrowserWindow::SetIsDisabled(bool bValue)
{
	bIsDisabled = bValue;
}

TSharedPtr<SWindow> FWebInterfaceBrowserWindow::GetParentWindow() const
{
	return ParentWindow;
}

void FWebInterfaceBrowserWindow::SetParentWindow(TSharedPtr<SWindow> Window)
{
	ParentWindow = Window;
}

void FWebInterfaceBrowserWindow::ExecuteJavascript(const FString& Script)
{
	BrowserWidget->ExecuteJavascript(Script);
}

void FWebInterfaceBrowserWindow::CloseBrowser(bool bForce, bool bBlockTillClosed /* ignored */)
{
	BrowserWidget->Close();
}

bool FWebInterfaceBrowserWindow::OnJsMessageReceived(const FString& Command, const TArray<FString>& Params, const FString& Origin)
{
	if (Command.Equals(JSGetSourceCommand, ESearchCase::CaseSensitive) && GetPageSourceCallback.IsSet() && Params.Num() == 1)
	{
		GetPageSourceCallback.GetValue()(Params[0]);
		GetPageSourceCallback.Reset();
		return true;
	}
	return Scripting->OnJsMessageReceived(Command, Params, Origin);
}

void FWebInterfaceBrowserWindow::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent /*= true*/)
{
	Scripting->BindUObject(Name, Object, bIsPermanent);
}

void FWebInterfaceBrowserWindow::UnbindUObject(const FString& Name, UObject* Object /*= nullptr*/, bool bIsPermanent /*= true*/)
{
	Scripting->UnbindUObject(Name, Object, bIsPermanent);
}

void FWebInterfaceBrowserWindow::CheckTickActivity()
{
	if (bIsVisible != bTickedLastFrame)
	{
		bIsVisible = bTickedLastFrame;
		BrowserWidget->SetWebBrowserVisibility(bIsVisible);
	}

	bTickedLastFrame = false;
}

void FWebInterfaceBrowserWindow::SetTickLastFrame()
{
	bTickedLastFrame = !bIsDisabled;
}

bool FWebInterfaceBrowserWindow::IsVisible()
{
	return bIsVisible;
}

#endif
