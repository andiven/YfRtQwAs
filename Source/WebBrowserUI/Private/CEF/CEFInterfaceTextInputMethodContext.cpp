// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "CEF/CEFInterfaceTextInputMethodContext.h"

#if WITH_CEF3 && !PLATFORM_LINUX

#include "CEF/CEFWebInterfaceBrowserWindow.h"
#include "CEFInterfaceImeHandler.h"
#include "Framework/Application/SlateApplication.h"


TSharedRef<FCEFInterfaceTextInputMethodContext> FCEFInterfaceTextInputMethodContext::Create(const TSharedRef<FCEFInterfaceImeHandler>& InOwner)
{
	return MakeShareable(new FCEFInterfaceTextInputMethodContext(InOwner));
}

FCEFInterfaceTextInputMethodContext::FCEFInterfaceTextInputMethodContext(const TSharedRef<FCEFInterfaceImeHandler>& InOwner)
	: Owner(InOwner)
	, bIsComposing(false)
	, CompositionBeginIndex(0)
	, CompositionLength(0)
	, SelectionRangeBeginIndex(0)
	, SelectionRangeLength(0)
	, SelectionCaretPosition(ECaretPosition::Ending)
{

}

void FCEFInterfaceTextInputMethodContext::AbortComposition()
{
	bIsComposing = false;
	Owner->InternalCefBrowser->GetHost()->ImeCancelComposition();
	ResetComposition();
}

bool FCEFInterfaceTextInputMethodContext::UpdateCachedGeometry(const FGeometry& AllottedGeometry)
{
	bool bCachedGeometryUpdated = false;
	if (CachedGeometry != AllottedGeometry)
	{
		CachedGeometry = AllottedGeometry;
		bCachedGeometryUpdated = true;
	}

	return bCachedGeometryUpdated;
}

bool FCEFInterfaceTextInputMethodContext::CEFCompositionRangeChanged(const CefRange& SelectionRange, const CefRenderHandler::RectList& CharacterBounds)
{
	if (bIsComposing)
	{
		if (CharacterBounds != CefCompositionBounds)
		{
			CefCompositionBounds = CharacterBounds;
			return true;
		}
	}
	return false;
}

bool FCEFInterfaceTextInputMethodContext::IsComposing()
{
	return bIsComposing;
}

bool FCEFInterfaceTextInputMethodContext::IsReadOnly()
{
	return false;
}

uint32 FCEFInterfaceTextInputMethodContext::GetTextLength()
{
	return CompositionString.Len();
}

void FCEFInterfaceTextInputMethodContext::GetSelectionRange(uint32& BeginIndex, uint32& Length, ECaretPosition& CaretPosition)
{
	BeginIndex = SelectionRangeBeginIndex;
	Length = SelectionRangeLength;
	CaretPosition = SelectionCaretPosition;
}

void FCEFInterfaceTextInputMethodContext::SetSelectionRange(const uint32 BeginIndex, const uint32 Length, const ECaretPosition CaretPosition)
{
	SelectionRangeBeginIndex = BeginIndex;
	SelectionRangeLength = Length;
	SelectionCaretPosition = CaretPosition;

	CefString Str = TCHAR_TO_WCHAR(*CompositionString);
	std::vector<CefCompositionUnderline> underlines;
	Owner->InternalCefBrowser->GetHost()->ImeSetComposition(
		Str,
		underlines,
		CefRange(UINT32_MAX, UINT32_MAX),
		CefRange(SelectionRangeBeginIndex, SelectionRangeLength));
}

void FCEFInterfaceTextInputMethodContext::GetTextInRange(const uint32 BeginIndex, const uint32 Length, FString& OutString)
{
	OutString = CompositionString.Mid(BeginIndex, Length);
}

void FCEFInterfaceTextInputMethodContext::SetTextInRange(const uint32 BeginIndex, const uint32 Length, const FString& InString)
{
	FString NewString;
	if (BeginIndex > 0)
	{
		NewString = CompositionString.Mid(0, BeginIndex);
	}

	NewString += InString;

	if ((int32)(BeginIndex + Length) < CompositionString.Len())
	{
		NewString += CompositionString.Mid(BeginIndex + Length, CompositionString.Len() - (BeginIndex + Length));
	}
	CompositionString = NewString;

	CefString Str = TCHAR_TO_WCHAR(*CompositionString);
	std::vector<CefCompositionUnderline> underlines;
	Owner->InternalCefBrowser->GetHost()->ImeSetComposition(
		Str,
		underlines,
		CefRange(UINT32_MAX, UINT32_MAX),
		CefRange(0, Str.length()));
}

int32 FCEFInterfaceTextInputMethodContext::GetCharacterIndexFromPoint(const FVector2D& Point)
{
	int32 ResultIdx = INDEX_NONE;

	const FVector2D LocalPoint = CachedGeometry.AbsoluteToLocal(Point);
	CefPoint CefLocalPoint = CefPoint(FMath::RoundToInt(LocalPoint.X), FMath::RoundToInt(LocalPoint.Y));

	for (uint32 CharIdx = 0; CharIdx < CefCompositionBounds.size(); CharIdx++)
	{
		if (CefCompositionBounds[CharIdx].Contains(CefLocalPoint))
		{
			ResultIdx = CharIdx;
			break;
		}
	}
	return ResultIdx;
}

bool FCEFInterfaceTextInputMethodContext::GetTextBounds(const uint32 BeginIndex, const uint32 Length, FVector2D& Position, FVector2D& Size)
{
	if (CefCompositionBounds.size() < BeginIndex ||
		CefCompositionBounds.size() < BeginIndex + Length)
	{
		if (CefCompositionBounds.size() > 0)
		{
			// Fall back to the start of the composition
			Position = CachedGeometry.LocalToAbsolute(FVector2D(CefCompositionBounds[0].x, CefCompositionBounds[0].y));
			Size = FVector2D(CefCompositionBounds[0].width, CefCompositionBounds[0].height);
			return false;
		}
		else
		{
			// We  don't have any updated composition bounds so we'll just default to the window bounds and say we are clipped.
			GetScreenBounds(Position, Size);
			return true;
		}
	}

	FVector2D LocalSpaceMin(FLT_MAX, FLT_MAX);
	FVector2D LocalSpaceMax(-FLT_MAX, -FLT_MAX);

	for (uint32 CharIdx = BeginIndex; CharIdx < BeginIndex + Length; CharIdx++)
	{
		if (LocalSpaceMin.X > CefCompositionBounds[CharIdx].x)
		{
			LocalSpaceMin.X = CefCompositionBounds[CharIdx].x;
		}

		if (LocalSpaceMax.X < CefCompositionBounds[CharIdx].x + CefCompositionBounds[CharIdx].width)
		{
			LocalSpaceMax.X = CefCompositionBounds[CharIdx].x + CefCompositionBounds[CharIdx].width;
		}

		if (LocalSpaceMin.Y > CefCompositionBounds[CharIdx].y)
		{
			LocalSpaceMin.Y = CefCompositionBounds[CharIdx].y;
		}

		if (LocalSpaceMax.Y < CefCompositionBounds[CharIdx].y + CefCompositionBounds[CharIdx].height)
		{
			LocalSpaceMax.Y = CefCompositionBounds[CharIdx].y + CefCompositionBounds[CharIdx].height;
		}
	}

	Position = CachedGeometry.LocalToAbsolute(LocalSpaceMin);
	Size = LocalSpaceMax - LocalSpaceMin;

	return false; // false means "not clipped"
}

void FCEFInterfaceTextInputMethodContext::GetScreenBounds(FVector2D& Position, FVector2D& Size)
{
	Position = CachedGeometry.GetAccumulatedRenderTransform().GetTranslation();
	Size = TransformVector(CachedGeometry.GetAccumulatedRenderTransform(), CachedGeometry.GetLocalSize());
}

TSharedPtr<FGenericWindow> FCEFInterfaceTextInputMethodContext::GetWindow()
{
	if (CachedSlateWindow.IsValid())
	{
		return CachedSlateWindow.Pin()->GetNativeWindow();
	}

	const TSharedPtr<SWidget> CachedSlateWidgetPtr = Owner->InternalBrowserSlateWidget.Pin();
	if (!CachedSlateWidgetPtr.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<SWindow> SlateWindow = FSlateApplication::Get().FindWidgetWindow(CachedSlateWidgetPtr.ToSharedRef());
	CachedSlateWindow = SlateWindow;
	return SlateWindow.IsValid() ? SlateWindow->GetNativeWindow() : nullptr;
}

void FCEFInterfaceTextInputMethodContext::BeginComposition()
{
	if (!bIsComposing)
	{
		bIsComposing = true;
	}
}

void FCEFInterfaceTextInputMethodContext::UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength)
{
	CompositionBeginIndex = InBeginIndex;
	CompositionLength = InLength;
}

void FCEFInterfaceTextInputMethodContext::EndComposition()
{
	if (bIsComposing)
	{
		bIsComposing = false;

		if (CompositionString.Len() > 0)
		{
			CefString Result = TCHAR_TO_WCHAR(*CompositionString);
			Owner->InternalCefBrowser->GetHost()->ImeCommitText(Result, CefRange(UINT32_MAX, UINT32_MAX), 0);
		}
		else
		{
			Owner->InternalCefBrowser->GetHost()->ImeCancelComposition();
		}
		ResetComposition();
	}
}

void FCEFInterfaceTextInputMethodContext::ResetComposition()
{
	CompositionString.Empty();
	CefCompositionBounds.clear();
	CompositionBeginIndex = 0;
	CompositionLength = 0;

	SelectionRangeBeginIndex = 0;
	SelectionRangeLength = 0;
}

#endif


