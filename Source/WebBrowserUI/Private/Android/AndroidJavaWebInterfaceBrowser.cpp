// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "AndroidJavaWebInterfaceBrowser.h"

#if USE_ANDROID_JNI

#include "Android/AndroidApplication.h"

#include <android/bitmap.h>

#if UE_BUILD_SHIPPING
// always clear any exceptions in SHipping
#define CHECK_JNI_RESULT(Id) if (Id == 0) { JEnv->ExceptionClear(); }
#else
#define CHECK_JNI_RESULT(Id) \
if (Id == 0) \
{ \
	if (bIsOptional) { JEnv->ExceptionClear(); } \
	else { JEnv->ExceptionDescribe(); checkf(Id != 0, TEXT("Failed to find " #Id)); } \
}
#endif

static jfieldID FindField(JNIEnv* JEnv, jclass Class, const ANSICHAR* FieldName, const ANSICHAR* FieldType, bool bIsOptional)
{
	jfieldID Field = Class == NULL ? NULL : JEnv->GetFieldID(Class, FieldName, FieldType);
	CHECK_JNI_RESULT(Field);
	return Field;
}

// From https://developer.android.com/reference/android/view/MotionEvent#constants_1
#define MOTIONEVENT_ACTION_DOWN		0
#define MOTIONEVENT_ACTION_UP		1
#define MOTIONEVENT_ACTION_MOVE		2

FJavaAndroidWebInterfaceBrowser::FJavaAndroidWebInterfaceBrowser(bool swizzlePixels, bool vulkanRenderer, int32 width, int32 height,
	jlong widgetPtr, bool bEnableRemoteDebugging, bool bUseTransparency, bool bEnableDomStorage, bool bShouldUseBitmapRender)
	: FJavaClassObject(GetClassName(), "(JIIZZZZZZ)V", widgetPtr, width, height, swizzlePixels, vulkanRenderer, bEnableRemoteDebugging, bUseTransparency, bEnableDomStorage, bShouldUseBitmapRender)
	, ReleaseMethod(GetClassMethod("release", "()V"))
	, GetVideoLastFrameBitmapMethod(GetClassMethod("getVideoLastFrameBitmap", "()Lcom/epicgames/unreal/WebInterfaceViewControl$FrameUpdateInfo;"))
	, GetVideoLastFrameDataMethod(GetClassMethod("getVideoLastFrameData", "()Lcom/epicgames/unreal/WebInterfaceViewControl$FrameUpdateInfo;"))
	, GetVideoLastFrameMethod(GetClassMethod("getVideoLastFrame", "(I)Lcom/epicgames/unreal/WebInterfaceViewControl$FrameUpdateInfo;"))
	, DidResolutionChangeMethod(GetClassMethod("didResolutionChange", "()Z"))
	, UpdateVideoFrameMethod(GetClassMethod("updateVideoFrame", "(I)Lcom/epicgames/unreal/WebInterfaceViewControl$FrameUpdateInfo;"))
	, UpdateMethod(GetClassMethod("Update", "(IIII)V"))
	, ExecuteJavascriptMethod(GetClassMethod("ExecuteJavascript", "(Ljava/lang/String;)V"))
	, LoadURLMethod(GetClassMethod("LoadURL", "(Ljava/lang/String;)V"))
	, LoadStringMethod(GetClassMethod("LoadString", "(Ljava/lang/String;Ljava/lang/String;)V"))
	, StopLoadMethod(GetClassMethod("StopLoad", "()V"))
	, ReloadMethod(GetClassMethod("Reload", "()V"))
	, CloseMethod(GetClassMethod("Close", "()V"))
	, GoBackOrForwardMethod(GetClassMethod("GoBackOrForward", "(I)V"))
	, SendTouchEventMethod(GetClassMethod("SendTouchEvent", "(IFF)V"))
	, SendKeyEventMethod(GetClassMethod("SendKeyEvent", "(ZI)Z"))
	, SetAndroid3DBrowserMethod(GetClassMethod("SetAndroid3DBrowser", "(Z)V"))
	, SetVisibilityMethod(GetClassMethod("SetVisibility", "(Z)V"))
{
	VideoTexture = nullptr;
	bVideoTextureValid = false;

	JNIEnv* JEnv = FAndroidApplication::GetJavaEnv();

	// get field IDs for FrameUpdateInfo class members
	FrameUpdateInfoClass = FAndroidApplication::FindJavaClassGlobalRef("com/epicgames/unreal/WebInterfaceViewControl$FrameUpdateInfo");
	FrameUpdateInfo_Buffer = FindField(JEnv, FrameUpdateInfoClass, "Buffer", "Ljava/nio/Buffer;", false);
	FrameUpdateInfo_Bitmap = FindField(JEnv, FrameUpdateInfoClass, "Bitmap", "Landroid/graphics/Bitmap;", false);
	FrameUpdateInfo_FrameReady = FindField(JEnv, FrameUpdateInfoClass, "FrameReady", "Z", false);
	FrameUpdateInfo_RegionChanged = FindField(JEnv, FrameUpdateInfoClass, "RegionChanged", "Z", false);
}

FJavaAndroidWebInterfaceBrowser::~FJavaAndroidWebInterfaceBrowser()
{
	if (auto Env = FAndroidApplication::GetJavaEnv())
	{
		Env->DeleteGlobalRef(FrameUpdateInfoClass);
	}
}

void FJavaAndroidWebInterfaceBrowser::Release()
{
	CallMethod<void>(ReleaseMethod);
}

bool FJavaAndroidWebInterfaceBrowser::GetVideoLastFrameBitmap(void* outPixels, int64 outCount)
{
	// This can return an exception in some cases
	JNIEnv* JEnv = FAndroidApplication::GetJavaEnv();
	auto Result = NewScopedJavaObject(JEnv, JEnv->CallObjectMethod(Object, GetVideoLastFrameBitmapMethod.Method));
	if (JEnv->ExceptionCheck())
	{
		JEnv->ExceptionDescribe();
		JEnv->ExceptionClear();
		return false;
	}

	if (!Result)
	{
		return false;
	}

	auto bitmap = NewScopedJavaObject(JEnv, JEnv->GetObjectField(*Result, FrameUpdateInfo_Bitmap));
	if (bitmap)
	{
		AndroidBitmapInfo BitmapInfo;

		AndroidBitmap_getInfo(JEnv, *bitmap, &BitmapInfo);
		if (BitmapInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
		{
			return false;
		}
		int32 BitmapBytes = BitmapInfo.stride * BitmapInfo.height;

		BitmapBytes = FMath::Min((int32)outCount, BitmapBytes);
		if (BitmapBytes > 0)
		{
			void* BitmapDataPtr;

			if (AndroidBitmap_lockPixels(JEnv, *bitmap, &BitmapDataPtr) >= 0)
			{
				// unfortunately the order is wrong (RGBA instead of BGRA)
				//memcpy(outPixels, BitmapDataPtr, BitmapBytes);
				int32 NumPixels = BitmapBytes >> 2;
				uint8* SrcData = (uint8*)BitmapDataPtr;
				uint8* DstData = (uint8*)outPixels;
				while (NumPixels--)
				{
					*DstData++ = SrcData[2];
					*DstData++ = SrcData[1];
					*DstData++ = SrcData[0];
					*DstData++ = SrcData[3];
					SrcData += 4;
				}
				AndroidBitmap_unlockPixels(JEnv, *bitmap);
				return true;
			}
		}

		return false;
	}

	return false;
}

bool FJavaAndroidWebInterfaceBrowser::GetVideoLastFrameData(void* & outPixels, int64 & outCount, bool *bRegionChanged)
{
	// This can return an exception in some cases
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();
	auto Result = NewScopedJavaObject(JEnv, JEnv->CallObjectMethod(Object, GetVideoLastFrameDataMethod.Method));
	if (JEnv->ExceptionCheck())
	{
		JEnv->ExceptionDescribe();
		JEnv->ExceptionClear();
		*bRegionChanged = false;
		return false;
	}

	if (!Result)
	{
		return false;
	}

	auto buffer = NewScopedJavaObject(JEnv, JEnv->GetObjectField(*Result, FrameUpdateInfo_Buffer));
	if (buffer)
	{
		bool bFrameReady = (bool)JEnv->GetBooleanField(*Result, FrameUpdateInfo_FrameReady);
		*bRegionChanged = (bool)JEnv->GetBooleanField(*Result, FrameUpdateInfo_RegionChanged);
		
		outPixels = JEnv->GetDirectBufferAddress(*buffer);
		outCount = JEnv->GetDirectBufferCapacity(*buffer);
		
		return !(nullptr == outPixels || 0 == outCount);
	}
	
	return false;
}

bool FJavaAndroidWebInterfaceBrowser::DidResolutionChange()
{
	return CallMethod<bool>(DidResolutionChangeMethod);
}

bool FJavaAndroidWebInterfaceBrowser::UpdateVideoFrame(int32 ExternalTextureId, bool *bRegionChanged)
{
	// This can return an exception in some cases
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();
	auto Result = NewScopedJavaObject(JEnv, JEnv->CallObjectMethod(Object, UpdateVideoFrameMethod.Method, ExternalTextureId));
	if (JEnv->ExceptionCheck())
	{
		JEnv->ExceptionDescribe();
		JEnv->ExceptionClear();
		*bRegionChanged = false;
		return false;
	}

	if (!Result)
	{
		*bRegionChanged = false;
		return false;
	}

	bool bFrameReady = (bool)JEnv->GetBooleanField(*Result, FrameUpdateInfo_FrameReady);
	*bRegionChanged = (bool)JEnv->GetBooleanField(*Result, FrameUpdateInfo_RegionChanged);
	
	return bFrameReady;
}

bool FJavaAndroidWebInterfaceBrowser::GetVideoLastFrame(int32 destTexture)
{
	// This can return an exception in some cases
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();
	auto Result = NewScopedJavaObject(JEnv, JEnv->CallObjectMethod(Object, GetVideoLastFrameMethod.Method, destTexture));
	if (JEnv->ExceptionCheck())
	{
		JEnv->ExceptionDescribe();
		JEnv->ExceptionClear();
		return false;
	}

	if (!Result)
	{
		return false;
	}

	bool bFrameReady = (bool)JEnv->GetBooleanField(*Result, FrameUpdateInfo_FrameReady);
	
	return bFrameReady;
}

FName FJavaAndroidWebInterfaceBrowser::GetClassName()
{
	return FName("com/epicgames/unreal/WebInterfaceViewControl");
}

void FJavaAndroidWebInterfaceBrowser::Update(const int posX, const int posY, const int sizeX, const int sizeY)
{
	CallMethod<void>(UpdateMethod, posX, posY, sizeX, sizeY);
}

void FJavaAndroidWebInterfaceBrowser::ExecuteJavascript(const FString& Script)
{
	auto JString = FJavaClassObject::GetJString(Script);
	CallMethod<void>(ExecuteJavascriptMethod, *JString);
}

void FJavaAndroidWebInterfaceBrowser::LoadURL(const FString& NewURL)
{
	auto JString = FJavaClassObject::GetJString(NewURL);
	CallMethod<void>(LoadURLMethod, *JString);
}

void FJavaAndroidWebInterfaceBrowser::LoadString(const FString& Contents, const FString& BaseUrl)
{
	auto JContents = FJavaClassObject::GetJString(Contents);
	auto JUrl = FJavaClassObject::GetJString(BaseUrl);
	CallMethod<void>(LoadStringMethod, *JContents, *JUrl);
}

void FJavaAndroidWebInterfaceBrowser::StopLoad()
{
	CallMethod<void>(StopLoadMethod);
}

void FJavaAndroidWebInterfaceBrowser::Reload()
{
	CallMethod<void>(ReloadMethod);
}

void FJavaAndroidWebInterfaceBrowser::Close()
{
	CallMethod<void>(CloseMethod);
}

void FJavaAndroidWebInterfaceBrowser::GoBack()
{
	CallMethod<void>(GoBackOrForwardMethod, -1);
}

void FJavaAndroidWebInterfaceBrowser::GoForward()
{
	CallMethod<void>(GoBackOrForwardMethod, 1);
}

void FJavaAndroidWebInterfaceBrowser::SendTouchDown(float x, float y)
{
	CallMethod<void>(SendTouchEventMethod, MOTIONEVENT_ACTION_DOWN, x, y);
}

void FJavaAndroidWebInterfaceBrowser::SendTouchUp(float x, float y)
{
	CallMethod<void>(SendTouchEventMethod, MOTIONEVENT_ACTION_UP, x, y);
}

void FJavaAndroidWebInterfaceBrowser::SendTouchMove(float x, float y)
{
	CallMethod<void>(SendTouchEventMethod, MOTIONEVENT_ACTION_MOVE, x, y);
}

bool FJavaAndroidWebInterfaceBrowser::SendKeyDown(int32 KeyCode)
{
	return CallMethod<bool>(SendKeyEventMethod, true, KeyCode);
}

bool FJavaAndroidWebInterfaceBrowser::SendKeyUp(int32 KeyCode)
{
	return CallMethod<bool>(SendKeyEventMethod, false, KeyCode);
}

void FJavaAndroidWebInterfaceBrowser::SetAndroid3DBrowser(bool InIsAndroid3DBrowser)
{
	CallMethod<void>(SetAndroid3DBrowserMethod, InIsAndroid3DBrowser);
}

void FJavaAndroidWebInterfaceBrowser::SetVisibility(bool InIsVisible)
{
	CallMethod<void>(SetVisibilityMethod, InIsVisible);
}

#endif // USE_ANDROID_JNI

