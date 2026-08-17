#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "OpenWorldLandscapeMaterialService.generated.h"

USTRUCT(BlueprintType)
struct FOpenWorldMaterialResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|LandscapeMaterial")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|LandscapeMaterial")
	FString AssetPath;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|LandscapeMaterial")
	FString ErrorMessage;
};

UCLASS(BlueprintType)
class OPENWORLDMCP_API UOpenWorldLandscapeMaterialService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Create a simple landscape material with a landscape layer blend node
	 * and the given layer names. Each layer gets a constant color input.
	 *
	 * @param MaterialName - Name for the new material asset
	 * @param FolderPath - Content folder for the material (e.g. "/Game/Terrain")
	 * @param LayerNames - Array of layer names (e.g. ["Grass", "Rock", "Sand"])
	 * @param LayerColors - Array of linear colors (R,G,B in 0-1), must match LayerNames count
	 * @return Result with material asset path
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|LandscapeMaterial")
	static FOpenWorldMaterialResult CreateLandscapeMaterial(
		const FString& MaterialName,
		const FString& FolderPath,
		const TArray<FString>& LayerNames,
		const TArray<FLinearColor>& LayerColors);

	/**
	 * Create a ULandscapeLayerInfoObject asset for a layer.
	 *
	 * @param LayerName - Layer name
	 * @param FolderPath - Content folder (e.g. "/Game/Terrain")
	 * @return Result with layer info asset path
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|LandscapeMaterial")
	static FOpenWorldMaterialResult CreateLayerInfo(
		const FString& LayerName,
		const FString& FolderPath);

	/**
	 * Assign a landscape material and its layer infos to a landscape actor.
	 *
	 * @param LandscapeName - Label or name of the landscape
	 * @param MaterialPath - Path to the landscape material
	 * @param LayerPaths - Layer info asset paths in order (e.g. ["/Game/Terrain/LI_Grass"])
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|LandscapeMaterial")
	static FOpenWorldMaterialResult AssignMaterialToLandscape(
		const FString& LandscapeName,
		const FString& MaterialPath,
		const TArray<FString>& LayerPaths);

	/**
	 * Paint a layer weight across a world-space rectangular region.
	 *
	 * @param LandscapeName - Label or name of the landscape
	 * @param LayerPath - Layer info asset path (e.g. "/Game/Terrain/LI_Grass")
	 * @param WorldMinX - Min X
	 * @param WorldMinY - Min Y
	 * @param WorldMaxX - Max X
	 * @param WorldMaxY - Max Y
	 * @param Weight - Target weight (0-1)
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|LandscapeMaterial")
	static FOpenWorldMaterialResult PaintLayerRect(
		const FString& LandscapeName,
		const FString& LayerPath,
		float WorldMinX,
		float WorldMinY,
		float WorldMaxX,
		float WorldMaxY,
		float Weight = 1.0f);

private:
	static class ALandscape* FindLandscape(const FString& LandscapeName);
	static class UMaterial* CreateMaterialWithBlend(
		const FString& MaterialName,
		const FString& FolderPath,
		const TArray<FString>& LayerNames,
		const TArray<FLinearColor>& LayerColors);
};
