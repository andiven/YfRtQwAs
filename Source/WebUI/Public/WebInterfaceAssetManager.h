// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "UObject/SoftObjectPtr.h"
#include "Materials/Material.h"
#include "WebInterfaceAssetManager.generated.h"

class UMaterial;

UCLASS()
class WEBUI_API UWebInterfaceAssetManager : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	void LoadDefaultMaterials();

	UMaterial* GetDefaultMaterial(); 
	UMaterial* GetDefaultTranslucentMaterial(); 

protected:

	UPROPERTY()
	TSoftObjectPtr<UMaterial> DefaultMaterial;
	TSoftObjectPtr<UMaterial> DefaultTranslucentMaterial;
};
