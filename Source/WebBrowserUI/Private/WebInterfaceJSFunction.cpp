// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceJSFunction.h"
#include "WebInterfaceJSScripting.h"

#if UE_VERSION >= 501
#include UE_INLINE_GENERATED_CPP_BY_NAME(WebInterfaceJSFunction)
#endif

#if WITH_CEF3
#if PLATFORM_WINDOWS
#if UE_VERSION < 504
#include "Windows/WindowsHWrapper.h"
#endif
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#endif
#pragma push_macro("OVERRIDE")
#undef OVERRIDE // cef headers provide their own OVERRIDE macro
THIRD_PARTY_INCLUDES_START
#if PLATFORM_APPLE
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#endif
#include "include/cef_values.h"
#if PLATFORM_APPLE
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("OVERRIDE")
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#endif

FWebInterfaceJSParam::~FWebInterfaceJSParam()
{
	// Since the FString, StructWrapper, TArray, and TMap members are in a union, they may or may not be valid, so we have to call the destructors manually.
	switch (Tag)
	{
		case PTYPE_STRING:
			delete StringValue;
			break;
		case PTYPE_STRUCT:
			delete StructValue;
			break;
		case PTYPE_ARRAY:
			delete ArrayValue;
			break;
		case PTYPE_MAP:
			delete MapValue;
			break;
		default:
			break;
	}
}

FWebInterfaceJSParam::FWebInterfaceJSParam(const FWebInterfaceJSParam& Other)
	: Tag(Other.Tag)
{
	switch (Other.Tag)
	{
		case PTYPE_BOOL:
			BoolValue = Other.BoolValue;
			break;
		case PTYPE_DOUBLE:
			DoubleValue = Other.DoubleValue;
			break;
		case PTYPE_INT:
			IntValue = Other.IntValue;
			break;
		case PTYPE_STRING:
			StringValue = new FString(*Other.StringValue);
			break;
		case PTYPE_NULL:
			break;
		case PTYPE_OBJECT:
			ObjectValue = Other.ObjectValue;
			break;
		case PTYPE_STRUCT:
			StructValue = Other.StructValue->Clone();
			break;
		case PTYPE_ARRAY:
			ArrayValue = new TArray<FWebInterfaceJSParam>(*Other.ArrayValue);
			break;
		case PTYPE_MAP:
			MapValue = new TMap<FString, FWebInterfaceJSParam>(*Other.MapValue);
			break;
	}
}

FWebInterfaceJSParam::FWebInterfaceJSParam(FWebInterfaceJSParam&& Other)
	: Tag(Other.Tag)
{
	switch (Other.Tag)
	{
	case PTYPE_BOOL:
		BoolValue = Other.BoolValue;
		break;
	case PTYPE_DOUBLE:
		DoubleValue = Other.DoubleValue;
		break;
	case PTYPE_INT:
		IntValue = Other.IntValue;
		break;
	case PTYPE_STRING:
		StringValue = Other.StringValue;
		Other.StringValue = nullptr;
		break;
	case PTYPE_NULL:
		break;
	case PTYPE_OBJECT:
		ObjectValue = Other.ObjectValue;
		Other.ObjectValue = nullptr;
		break;
	case PTYPE_STRUCT:
		StructValue = Other.StructValue;
		Other.StructValue = nullptr;
		break;
	case PTYPE_ARRAY:
		ArrayValue = Other.ArrayValue;
		Other.ArrayValue = nullptr;
		break;
	case PTYPE_MAP:
		MapValue = Other.MapValue;
		Other.MapValue = nullptr;
		break;
	}

	Other.Tag = PTYPE_NULL;
}

void FWebInterfaceJSCallbackBase::Invoke(int32 ArgCount, FWebInterfaceJSParam Arguments[], bool bIsError) const
{
	TSharedPtr<FWebInterfaceJSScripting> Scripting = ScriptingPtr.Pin();
	if (Scripting.IsValid())
	{
		Scripting->InvokeJSFunction(CallbackId, ArgCount, Arguments, bIsError);
	}
}
