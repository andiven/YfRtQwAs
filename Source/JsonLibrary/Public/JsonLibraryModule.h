// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Modules/ModuleManager.h"

class IJsonLibraryModule : public IModuleInterface
{
public:

	static inline IJsonLibraryModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IJsonLibraryModule>( "JsonLibrary" );
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "JsonLibrary" );
	}
};
