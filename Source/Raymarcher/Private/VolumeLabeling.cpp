// (C) Technical University of Munich - Computer Aided Medical Procedures
// Developed by Tomas Bartipan (tomas.bartipan@tum.de)

#include "../Public/VolumeLabeling.h"
#include "AssetRegistryModule.h"
#include "RenderCore/Public/RenderUtils.h"
#include "Renderer/Public/VolumeRendering.h"
#include "TextureHelperFunctions.h"

#define LOCTEXT_NAMESPACE "RaymarchPlugin"

IMPLEMENT_SHADER_TYPE(, FWriteSphereToVolumeShader, TEXT("/Plugin/VolumeRaymarching/Private/WriteCuboidShader.usf"),
                      TEXT("MainComputeShader"), SF_Compute)

void WriteSphereToVolume_RenderThread(FRHICommandListImmediate & RHICmdList, FRHITexture3D * MarkedVolume, const FVector BrushWorldCenter,const float SphereRadiusWorld, const FRaymarchWorldParameters WorldParameters, FLinearColor WrittenValue)
{
	// Get local center.
	FVector localCenter = ((WorldParameters.VolumeTransform.InverseTransformPosition(BrushWorldCenter) /
		(WorldParameters.MeshMaxBounds)) / 2.0) + 0.5;
	// Get local center in integer space

	float localSphereDiameter = ((WorldParameters.VolumeTransform.InverseTransformVector(FVector(SphereRadiusWorld, 0, 0)) /
		(WorldParameters.MeshMaxBounds * 2))).Size() * 2;

	int32 x = localCenter.X * MarkedVolume->GetSizeX();
	int32 y = localCenter.Y * MarkedVolume->GetSizeY();
	int32 z = localCenter.Z * MarkedVolume->GetSizeZ();


	FIntVector localCenterIntCoords;
	localCenterIntCoords.X = x; localCenterIntCoords.Y = y; localCenterIntCoords.Z = z;

	//FString kkt = "a " + localCenter.ToString() + ", local = " + FString::FromInt(x)+ " " + FString::FromInt(y) + " "+ FString::FromInt(z) + ", local intvec = " + localCenterIntCoords.ToString();

	//GEngine->AddOnScreenDebugMessage(0, 10, FColor::Yellow, kkt);
	
	// Get shader ref from GlobalShaderMap
	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FWriteSphereToVolumeShader> ComputeShader(GlobalShaderMap);

	RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
	// RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, LightVolumeResource);
	FUnorderedAccessViewRHIRef MarkedVolumeUAV = RHICreateUnorderedAccessView(MarkedVolume);
	
	// Transfer from gfx to compute, otherwise the renderer might touch our textures while we're writing them.
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier,
		EResourceTransitionPipeline::EGfxToCompute, MarkedVolumeUAV);

	// Get brush size in texture space.
	FIntVector brush = FIntVector(localSphereDiameter * MarkedVolume->GetSizeX(),localSphereDiameter * MarkedVolume->GetSizeY(),localSphereDiameter * MarkedVolume->GetSizeZ() );

	FComputeShaderRHIParamRef cs = ComputeShader->GetComputeShader();
	ComputeShader->SetMarkedVolumeUAV(RHICmdList, cs, MarkedVolumeUAV);
	ComputeShader->SetParameters(RHICmdList, cs, localCenterIntCoords, brush, WrittenValue);

	DispatchComputeShader(RHICmdList, *ComputeShader, brush.X, brush.Y, brush.Z);
	
	ComputeShader->UnbindMarkedVolumeUAV(RHICmdList, cs);

	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable,
		EResourceTransitionPipeline::EComputeToGfx, MarkedVolumeUAV);
}


void ULabelVolumeLibrary::CreateNewLabelingVolumeAsset(FString AssetName, FIntVector Dimensions, UVolumeTexture*& OutTexture)
{
    EPixelFormat PixelFormat = PF_G8;
    const long TotalSize = Dimensions.X * Dimensions.Y * Dimensions.Z * GPixelFormats[PixelFormat].BlockBytes;
	
	uint8* dummy = (uint8*)FMemory::Malloc(TotalSize);
	FMemory::Memset(dummy, 0, TotalSize);

	if (!CreateVolumeTextureAsset(AssetName, PixelFormat, Dimensions, dummy, OutTexture, false, false, true)) {
		GEngine->AddOnScreenDebugMessage(0, 10, FColor::Yellow, "Failed creating the labeling volume.");
	}

	FMemory::Free(dummy);
}

void ULabelVolumeLibrary::InitLabelingVolume(UVolumeTexture* LabelVolumeAsset, FIntVector Dimensions)
{
  EPixelFormat PixelFormat = PF_G8;
  const long TotalSize = Dimensions.X * Dimensions.Y * Dimensions.Z * GPixelFormats[PixelFormat].BlockBytes;
  uint8* dummy = (uint8*)FMemory::Malloc(TotalSize);
	
	FMemory::Memset(dummy, 0, TotalSize);
	if (!UpdateVolumeTextureAsset(LabelVolumeAsset, PixelFormat, Dimensions, dummy, true, false, true)) {
		GEngine->AddOnScreenDebugMessage(0, 10, FColor::Yellow, "Failed initializing the labeling volume.");
	}

	FMemory::Free(dummy);

}

void ULabelVolumeLibrary::LabelSphereInVolumeWorld(UVolumeTexture* MarkedVolume, const FVector BrushWorldCenter, const float SphereRadiusWorld, const FRaymarchWorldParameters WorldParameters, const FLinearColor WrittenValue)
{
	if (MarkedVolume->Resource == NULL) return;
	// Call the actual rendering code on RenderThread.
	ENQUEUE_RENDER_COMMAND(CaptureCommand)
		([=](FRHICommandListImmediate& RHICmdList) {
		WriteSphereToVolume_RenderThread(RHICmdList, MarkedVolume->Resource->TextureRHI->GetTexture3D(), BrushWorldCenter, SphereRadiusWorld, WorldParameters, WrittenValue);
	});
}


#undef LOCTEXT_NAMESPACE