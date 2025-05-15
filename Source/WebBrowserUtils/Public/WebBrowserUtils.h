// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"

#if WITH_CEF3
namespace WebBrowserUtils
{
	/**
	 * Load the required modules for CEF3, returns false if we fail to load the cef library
	 */
	WEBBROWSERUTILS_API bool LoadCEF3Modules(bool bIsMainApp);

	/**
	 * Unload the required modules for CEF3
	 */
	WEBBROWSERUTILS_API void UnloadCEF3Modules();

#if PLATFORM_WINDOWS
	/**
	 * Get the module (dll) handle to the loaded CEF3 module, will be null if not loaded
	 */
	WEBBROWSERUTILS_API void* GetCEF3ModuleHandle();
#endif

	/**
	 * Move the current cef3.log file to a backup file, so CEF makes a new log when it starts up.
	 * This backup file is then cleaned up by the logic in FMaintenance::DeleteOldLogs()
	 */
	WEBBROWSERUTILS_API void BackupCEF3Logfile(const FString& LogFilePath);

#if UE_EDITOR
	/**
	 * Checks if the required modules exist for CEF3, returns false if they are not found 
	 */
	WEBBROWSERUTILS_API bool CheckCEF3Modules();

	/**
	 * Checks if the subprocess (exe) exists, returns false if it's not found 
	 */
	WEBBROWSERUTILS_API bool CheckCEF3Helper();
#endif

	/**
	 * Check if WebUI plugin supports accelerated paint
	 */
	WEBBROWSERUTILS_API bool CanSupportAcceleratedPaint();

	/**
	 * Find the WebUI plugin directory
	 */
	WEBBROWSERUTILS_API FString FindPluginDirectory();

	/**
	 * Fixup a WebUI plugin path
	 */
	WEBBROWSERUTILS_API FString FixupPluginPath(const FString& Path);

	/**
	 * Check if WebUI plugin has any keys
	 */
	WEBBROWSERUTILS_API bool HasKey();

	/**
	 * Get the WebUI license key
	 */
	WEBBROWSERUTILS_API FString GetLicenseKey();

	/**
	 * Get the WebUI project key
	 */
	WEBBROWSERUTILS_API FString GetProjectKey();

	/**
	 * Register a WebUI project license
	 */
	WEBBROWSERUTILS_API void RegisterLicenseForProject();

	/**
	 * Check if WebUI license is valid for project
	 */
	WEBBROWSERUTILS_API bool VerifyLicenseForProject();
};
#endif // WITH_CEF3
