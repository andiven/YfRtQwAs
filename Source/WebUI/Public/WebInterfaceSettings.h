// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "WebInterfaceSettings.generated.h"

UCLASS(config=Game, defaultconfig)
class UWebInterfaceSettings : public UObject
{
	GENERATED_BODY()

public:

	UWebInterfaceSettings()
	{
		bCopySubresourceRegion = false;
		bForceDisableAcceleratedPaint = false;
	}

	UPROPERTY(config, EditAnywhere, Category="Performance", meta=(DisplayName="Subresource Region Copying"))
	bool bCopySubresourceRegion;

	UPROPERTY(config, EditAnywhere, Category="Licensed Project (tracerinteractive.com)", meta=(EditCondition="!bForceDisableAcceleratedPaint"))
	FString LicenseKey;

	UPROPERTY(config, EditAnywhere, Category="Licensed Project (tracerinteractive.com)", meta=(EditCondition="!bForceDisableAcceleratedPaint"))
	FString ProjectKey;

	UPROPERTY(config, EditAnywhere, Category="Unlicensed Project")
	bool bForceDisableAcceleratedPaint;
};
