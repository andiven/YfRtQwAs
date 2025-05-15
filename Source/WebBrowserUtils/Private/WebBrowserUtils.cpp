// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebBrowserUtils.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDeviceFile.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "WebBrowserUtilsLog.h"
#if PLATFORM_WINDOWS
#include "TracerWebAcceleratedPaint.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if WITH_ENGINE
#include "RHI.h"
#endif

DEFINE_LOG_CATEGORY(LogWebBrowserUtils);

IMPLEMENT_MODULE(FDefaultModuleImpl, WebBrowserUtils);

#if WITH_CEF3
#	if PLATFORM_WINDOWS
#		define CEF3_BIN_DIR TEXT("Plugins/Marketplace/WebUI/Binaries")
#		define CEF3_BIN_DIR_THIRD_PARTY CEF3_BIN_DIR TEXT("/ThirdParty/CEF3")
#		define CEF3_SUBPROCES_EXE CEF3_BIN_DIR TEXT("/Win64/TracerWebHelper.exe")
#	endif
namespace WebBrowserUtils
{
#if PLATFORM_WINDOWS
    void* TracerHandle  = nullptr;
    void* CEF3DLLHandle = nullptr;
	void* ElfHandle     = nullptr;
	void* D3DHandle     = nullptr;
	void* DXHandle      = nullptr;
	void* DXILHandle    = nullptr;
	void* SwiftHandle   = nullptr;
	void* VulkanHandle  = nullptr;
	void* GLESHandle    = nullptr;
    void* EGLHandle     = nullptr;
#endif

	void* LoadDllCEF(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		void* Handle = FPlatformProcess::GetDllHandle(*Path);
		if (!Handle)
		{
			int32 ErrorNum = FPlatformMisc::GetLastError();
			TCHAR ErrorMsg[1024];
			FPlatformMisc::GetSystemErrorMessage(ErrorMsg, 1024, ErrorNum);
			UE_LOG(LogWebBrowserUtils, Error, TEXT("Failed to get CEF3 DLL handle for %s: %s (%d)"), *Path, ErrorMsg, ErrorNum);
		}
		return Handle;
	}

	bool LoadCEF3Modules(bool bIsMainApp)
	{
#if PLATFORM_WINDOWS
	#if PLATFORM_64BITS
		FString DllPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR_THIRD_PARTY TEXT("/Win64")));
	#else
		FString DllPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR_THIRD_PARTY TEXT("/Win32")));
	#endif

		if (bIsMainApp)
			DllPath = FixupPluginPath(DllPath);

		FPlatformProcess::PushDllDirectory(*DllPath);
		CEF3DLLHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libcef.dll")));
		if (CEF3DLLHandle)
		{
			ElfHandle    = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("chrome_elf.dll")));
			D3DHandle    = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("d3dcompiler_47.dll")));
			DXHandle     = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("dxcompiler.dll")));
			DXILHandle   = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("dxil.dll")));
			SwiftHandle  = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("vk_swiftshader.dll")));
			VulkanHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("vulkan-1.dll")));
			GLESHandle   = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libGLESv2.dll")));
			EGLHandle    = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libEGL.dll")));
		}
		FPlatformProcess::PopDllDirectory(*DllPath);

	#if PLATFORM_64BITS
		DllPath = FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR TEXT("/Win64"));
	#else
		DllPath = FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR TEXT("/Win32"));
	#endif

		if (bIsMainApp)
			DllPath = FixupPluginPath(DllPath);

		FPlatformProcess::PushDllDirectory(*DllPath);
		TracerHandle = FPlatformProcess::GetDllHandle(*FPaths::Combine(*DllPath, TEXT("TracerWebAcceleratedPaint.dll")));
		FPlatformProcess::PopDllDirectory(*DllPath);

		return CEF3DLLHandle != nullptr;
#else
		return false;
#endif
	}

	void UnloadCEF3Modules()
	{
#if PLATFORM_WINDOWS
		FPlatformProcess::FreeDllHandle(TracerHandle);  TracerHandle  = nullptr;
		FPlatformProcess::FreeDllHandle(CEF3DLLHandle); CEF3DLLHandle = nullptr;
		FPlatformProcess::FreeDllHandle(ElfHandle);     ElfHandle     = nullptr;
		FPlatformProcess::FreeDllHandle(D3DHandle);     D3DHandle     = nullptr;
		FPlatformProcess::FreeDllHandle(DXHandle);      DXHandle      = nullptr;
		FPlatformProcess::FreeDllHandle(DXILHandle);    DXILHandle    = nullptr;
		FPlatformProcess::FreeDllHandle(SwiftHandle);   SwiftHandle   = nullptr;
		FPlatformProcess::FreeDllHandle(VulkanHandle);  VulkanHandle  = nullptr;
		FPlatformProcess::FreeDllHandle(GLESHandle);    GLESHandle    = nullptr;
		FPlatformProcess::FreeDllHandle(EGLHandle);     EGLHandle     = nullptr;
#endif
	}

#if PLATFORM_WINDOWS
	WEBBROWSERUTILS_API void* GetCEF3ModuleHandle()
	{
		return CEF3DLLHandle;
	}
#endif

	void BackupCEF3Logfile(const FString& LogFilePath)
	{
		const FString Cef3LogFile = FPaths::Combine(*LogFilePath,TEXT("cef3.log"));
		IFileManager& FileManager = IFileManager::Get();
		if (FileManager.FileSize(*Cef3LogFile) > 0) // file exists and is not empty
		{
			FString Name, Extension;
			FString(Cef3LogFile).Split(TEXT("."), &Name, &Extension, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			FDateTime OriginalTime = FileManager.GetTimeStamp(*Cef3LogFile);
			FString BackupFilename = FString::Printf(TEXT("%s%s%s.%s"), *Name, BACKUP_LOG_FILENAME_POSTFIX, *OriginalTime.ToString(), *Extension);
			// do not retry resulting in an error if log still in use
			if (!FileManager.Move(*BackupFilename, *Cef3LogFile, false, false, false, true))
			{
				UE_LOG(LogWebBrowserUtils, Warning, TEXT("Failed to backup cef3.log"));
			}
		}
	}

#if UE_EDITOR
	bool CheckCEF3Modules()
	{
		if (FindPluginDirectory().IsEmpty())
			return false;

		FString ThirdPartyPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR_THIRD_PARTY));
		ThirdPartyPath = FixupPluginPath(FPaths::ConvertRelativePathToFull(ThirdPartyPath));
		
		IFileManager& FileManager = IFileManager::Get();
		return FileManager.DirectoryExists(*ThirdPartyPath);
	}

	bool CheckCEF3Helper()
	{
		if (FindPluginDirectory().IsEmpty())
			return false;

		FString SubProcessPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_SUBPROCES_EXE));
		SubProcessPath = FixupPluginPath(FPaths::ConvertRelativePathToFull(SubProcessPath));
		
		IFileManager& FileManager = IFileManager::Get();
		return FileManager.FileExists(*SubProcessPath);
	}
#endif

	bool CanSupportAcceleratedPaint()
	{
		bool bForceDisableAcceleratedPaint = false;
		if (GConfig->GetBool(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("bForceDisableAcceleratedPaint"), bForceDisableAcceleratedPaint, GGameIni))
		{
			if (bForceDisableAcceleratedPaint)
				return false;
		}

#if PLATFORM_WINDOWS
		if (!IsTracerAcceleratedPaintSupported())
			return false;
#endif

#if WITH_ENGINE && UE_VERSION >= 501
		switch (RHIGetInterfaceType())
		{
			case ERHIInterfaceType::D3D12:
			case ERHIInterfaceType::D3D11:
				return true;
			default:
				return false;
		}
#else
		static bool sbD3D12Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
		static bool sbD3D11Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D11")) == 0;
		return sbD3D12Renderer || sbD3D11Renderer;
#endif
	}
	
	FString FindPluginDirectory()
	{
		static FString PluginDir = "";
		if (PluginDir.IsEmpty())
		{
			PluginDir = IPluginManager::Get().FindPlugin(TEXT("WebUI"))->GetBaseDir();
			if (PluginDir.IsEmpty())
				return FString();
		}

		FString Search = TEXT("Engine/Plugins/Marketplace/");
		int32   Index  = PluginDir.Find(Search, ESearchCase::IgnoreCase);
		if (Index < 0)
			return FString();

		FString PluginFolder = PluginDir.Mid(Index + Search.Len());
		Index = PluginFolder.Find("/");
		if (Index > 0)
			PluginFolder = PluginFolder.Left(Index);

		return PluginFolder;
	}

	FString FixupPluginPath(const FString& Path)
	{
		FString PluginDir = FindPluginDirectory();
		if (PluginDir.IsEmpty())
			return Path;
		if (PluginDir == "WebUI")
			return Path;

		return Path.Replace(TEXT("Plugins/Marketplace/WebUI/"), *FString::Printf(TEXT("Plugins/Marketplace/%s/"), *PluginDir), ESearchCase::IgnoreCase);
	}

	bool HasKey()
	{
		FString LicenseKey = GetLicenseKey();
		if (!LicenseKey.IsEmpty())
			return true;

		FString ProjectKey = GetProjectKey();
		if (!ProjectKey.IsEmpty())
			return true;

		return false;
	}

	FString GetLicenseKey()
	{
		FString LicenseKey;
		if (GConfig->GetString(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("LicenseKey"), LicenseKey, GGameIni))
			return LicenseKey;

		return FString();
	}

	FString GetProjectKey()
	{
		FString ProjectKey;
		if (GConfig->GetString(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("ProjectKey"), ProjectKey, GGameIni))
			return ProjectKey;

		return FString();
	}

	void RegisterLicenseForProject()
	{
#if !UE_EDITOR
		FString LicenseKey = GetLicenseKey();
		FString ProjectKey = GetProjectKey();
		if (LicenseKey.IsEmpty())
			return;

#if PLATFORM_WINDOWS
		SetTracerAcceleratedPaintLicenseKey(TCHAR_TO_UTF8(*LicenseKey), TCHAR_TO_UTF8(*ProjectKey));
#endif
#endif
	}

	bool VerifyLicenseForProject()
	{
		FString LicenseKey = GetLicenseKey();
		FString ProjectKey = GetProjectKey();
		if (LicenseKey.IsEmpty() || ProjectKey.IsEmpty())
			return false;

		TArray<uint8> LicenseKeyBytes;
		LicenseKeyBytes.SetNumZeroed(LicenseKey.Len() / 2);
		if (HexToBytes(LicenseKey, LicenseKeyBytes.GetData()) != LicenseKeyBytes.Num())
			return false;

		TArray<uint8> ProjectKeyBytes;
		ProjectKeyBytes.SetNumZeroed(ProjectKey.Len() / 2);
		if (HexToBytes(ProjectKey, ProjectKeyBytes.GetData()) != ProjectKeyBytes.Num())
			return false;

		FString ProjectName;
		for (int32 i = 0; i < LicenseKeyBytes.Num() && i < ProjectKeyBytes.Num(); ++i)
		{
			TCHAR ProjectChar = LicenseKeyBytes[i] ^ ProjectKeyBytes[i];
			if (ProjectChar == '\0')
				break;

			ProjectName.AppendChar(ProjectChar);
		}

		if (ProjectName.IsEmpty())
			return false;

#if UE_EDITOR
		FString AppName = FApp::GetProjectName();
		if (AppName.IsEmpty())
			return true;
#else
		FString AppName = FPlatformProcess::ExecutableName(true);
		if (AppName.IsEmpty())
			return false;

		int32 Index;
		if (AppName.FindChar('-', Index))
			AppName = AppName.Left(Index);
		if (AppName.Len() > ProjectName.Len())
			AppName = AppName.Left(ProjectName.Len());
#endif

		return ProjectName == AppName;
	}
};
#endif // WITH_CEF3
