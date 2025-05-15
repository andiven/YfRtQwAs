// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "WebInterfaceJSFunction.h"
#include "WebInterfaceJSScripting.h"

typedef TSharedRef<class FNativeInterfaceJSScripting> FNativeInterfaceJSScriptingRef;
typedef TSharedPtr<class FNativeInterfaceJSScripting> FNativeInterfaceJSScriptingPtr;

class FNativeWebInterfaceBrowserProxy;

/**
 * Implements handling of bridging UObjects client side with JavaScript renderer side.
 */
class FNativeInterfaceJSScripting
	: public FWebInterfaceJSScripting
	, public TSharedFromThis<FNativeInterfaceJSScripting>
{
public:
	//static const FString JSMessageTag;

	FNativeInterfaceJSScripting(bool bJSBindingToLoweringEnabled, TSharedRef<FNativeWebInterfaceBrowserProxy> Window);

	virtual void BindUObject(const FString& Name, UObject* Object, bool bIsPermanent = true) override;
	virtual void UnbindUObject(const FString& Name, UObject* Object = nullptr, bool bIsPermanent = true) override;

	bool OnJsMessageReceived(const FString& Message);

	FString ConvertStruct(UStruct* TypeInfo, const void* StructPtr);
	FString ConvertObject(UObject* Object);

	virtual void InvokeJSFunction(FGuid FunctionId, int32 ArgCount, FWebInterfaceJSParam Arguments[], bool bIsError=false) override;
	virtual void InvokeJSErrorResult(FGuid FunctionId, const FString& Error) override;
	void PageLoaded();

private:
	FString GetInitializeScript();
	void InvokeJSFunctionRaw(FGuid FunctionId, const FString& JSValue, bool bIsError=false);
	bool IsValid()
	{
		return WindowPtr.Pin().IsValid();
	}

	/** Message handling helpers */
	bool HandleExecuteUObjectMethodMessage(const TArray<FString>& Params);
	void ExecuteJavascript(const FString& Javascript);

	TWeakPtr<FNativeWebInterfaceBrowserProxy> WindowPtr;
	bool bLoaded;
};
