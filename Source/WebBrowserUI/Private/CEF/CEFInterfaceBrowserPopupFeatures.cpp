// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "CEF/CEFInterfaceBrowserPopupFeatures.h"

#if WITH_CEF3

FCEFInterfaceBrowserPopupFeatures::FCEFInterfaceBrowserPopupFeatures()
	: X(0)
	, bXSet(false)
	, Y(0)
	, bYSet(false)
	, Width(0)
	, bWidthSet(false)
	, Height(0)
	, bHeightSet(false)
	, bMenuBarVisible(true)
	, bStatusBarVisible(false)
	, bToolBarVisible(true)
	, bLocationBarVisible(true)
	, bScrollbarsVisible(true)
	, bResizable(true)
	, bIsFullscreen(false)
	, bIsDialog(false)
{
}


FCEFInterfaceBrowserPopupFeatures::FCEFInterfaceBrowserPopupFeatures( const CefPopupFeatures& PopupFeatures )
{
	X = PopupFeatures.x;
	bXSet = PopupFeatures.xSet ? true : false;
	Y = PopupFeatures.y;
	bYSet = PopupFeatures.ySet ? true : false;
	Width = PopupFeatures.width;
	bWidthSet = PopupFeatures.widthSet ? true : false;
	Height = PopupFeatures.height;
	bHeightSet = PopupFeatures.heightSet ? true : false;
#if PLATFORM_WINDOWS
	bMenuBarVisible = PopupFeatures.isPopup ? false : true;
	bStatusBarVisible = PopupFeatures.isPopup ? false : true;
	bToolBarVisible = PopupFeatures.isPopup ? false : true;
	bScrollbarsVisible = PopupFeatures.isPopup ? false : true;
#else
	bMenuBarVisible = PopupFeatures.menuBarVisible ? true : false;
	bStatusBarVisible = PopupFeatures.statusBarVisible ? true : false;
	bToolBarVisible = PopupFeatures.toolBarVisible ? true : false;
	bScrollbarsVisible = PopupFeatures.scrollbarsVisible ? true : false;
#endif

	// no longer set by the CEF API so default them here to their historic value
	bLocationBarVisible = false;
	bResizable = false;
	bIsFullscreen = false;
	bIsDialog = false;
}

FCEFInterfaceBrowserPopupFeatures::~FCEFInterfaceBrowserPopupFeatures()
{
}

void FCEFInterfaceBrowserPopupFeatures::SetResizable(const bool bResize)
{
	bResizable = bResize;
}

int FCEFInterfaceBrowserPopupFeatures::GetX() const
{
	return X;
}

bool FCEFInterfaceBrowserPopupFeatures::IsXSet() const
{
	return bXSet;
}

int FCEFInterfaceBrowserPopupFeatures::GetY() const
{
	return Y;
}

bool FCEFInterfaceBrowserPopupFeatures::IsYSet() const
{
	return bYSet;
}

int FCEFInterfaceBrowserPopupFeatures::GetWidth() const
{
	return Width;
}

bool FCEFInterfaceBrowserPopupFeatures::IsWidthSet() const
{
	return bWidthSet;
}

int FCEFInterfaceBrowserPopupFeatures::GetHeight() const
{
	return Height;
}

bool FCEFInterfaceBrowserPopupFeatures::IsHeightSet() const
{
	return bHeightSet;
}

bool FCEFInterfaceBrowserPopupFeatures::IsMenuBarVisible() const
{
	return bMenuBarVisible;
}

bool FCEFInterfaceBrowserPopupFeatures::IsStatusBarVisible() const
{
	return bStatusBarVisible;
}

bool FCEFInterfaceBrowserPopupFeatures::IsToolBarVisible() const
{
	return bToolBarVisible;
}

bool FCEFInterfaceBrowserPopupFeatures::IsLocationBarVisible() const
{
	return bLocationBarVisible;
}

bool FCEFInterfaceBrowserPopupFeatures::IsScrollbarsVisible() const
{
	return bScrollbarsVisible;
}

bool FCEFInterfaceBrowserPopupFeatures::IsResizable() const
{
	return bResizable;
}

bool FCEFInterfaceBrowserPopupFeatures::IsFullscreen() const
{
	return bIsFullscreen;
}

bool FCEFInterfaceBrowserPopupFeatures::IsDialog() const
{
	return bIsDialog;
}

TArray<FString> FCEFInterfaceBrowserPopupFeatures::GetAdditionalFeatures() const
{
	TArray<FString> Empty;
	return Empty;
}

#endif
