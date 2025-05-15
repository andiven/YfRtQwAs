// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IImageWrapper.h"
#include "WebInterfaceBrowserHelpers.generated.h"

UCLASS()
class WEBBROWSERUI_API UWebInterfaceBrowserHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	static TArray64<FColor> ResizeImage( int32 SrcWidth, int32 SrcHeight, const TArray64<FColor>& SrcData, int32 DstWidth, int32 DstHeight, bool bLinearSpace = false, bool bForceOpaqueOutput = false );

	static IImageWrapper* FindOrCreateImageWrapper( EImageFormat Format );
	static bool           SetImageWrapper( IImageWrapper* Wrapper, const TArray64<FColor>& Pixels, FIntPoint Size );
	static void           ReturnImageWrapper( IImageWrapper* Wrapper );

	static FIntPoint GenerateImageFromRenderTarget( TArray64<FColor>& OutPixels, class UTextureRenderTarget2D* RenderTarget );
	static FIntPoint GenerateImageFromTexture( TArray64<FColor>& OutPixels, class UTexture2D* Texture );
	static FIntPoint GenerateImageFromMaterial( TArray64<FColor>& OutPixels, class UMaterialInterface* Material, int32 Width, int32 Height );

private:

	static bool GenerateImageFromCamera( TArray64<FColor>& OutPixels, UObject* WorldContextObject, const FTransform& CameraTransform, int32 Width, int32 Height, float FOVDegrees = 50.0f, float MinZ = 50.0f, float Gamma = 2.2f );
	static bool GenerateImageFromScene( TArray64<FColor>& OutPixels, class FSceneInterface* Scene, const FVector& ViewOrigin, const FMatrix& ViewRotationMatrix, const FMatrix& ProjectionMatrix, FIntPoint TargetSize, float TargetGamma );
};
