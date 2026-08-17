#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "OpenWorldFoliageService.generated.h"

USTRUCT(BlueprintType)
struct FOpenWorldScatterResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Foliage")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Foliage")
	int32 InstancesSpawned = 0;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Foliage")
	FString ErrorMessage;
};

UCLASS(BlueprintType)
class OPENWORLDMCP_API UOpenWorldFoliageService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Scatter static meshes as InstancedStaticMesh components on the landscape.
	 * Traces to the landscape surface for height and normal alignment.
	 *
	 * @param MeshPath - Path to the UStaticMesh asset (e.g. "/PCG/SampleContent/SimpleForest/Meshes/PCG_Tree_01")
	 * @param WorldCenterX - Center X of the scatter region
	 * @param WorldCenterY - Center Y of the scatter region
	 * @param Radius - Radius of the scatter region in world units
	 * @param Count - Number of instances to spawn
	 * @param MinScale - Minimum random scale
	 * @param MaxScale - Maximum random scale
	 * @param bAlignToNormal - Align instances to the surface normal
	 * @param bRandomYaw - Random yaw rotation
	 * @param Seed - Random seed for reproducibility (0 = random)
	 * @param ActorLabel - Label for the spawned ISM actor (default "ScatteredMesh")
	 * @return Result with spawned instance count
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Foliage")
	static FOpenWorldScatterResult ScatterOnLandscape(
		const FString& MeshPath,
		float WorldCenterX,
		float WorldCenterY,
		float Radius,
		int32 Count,
		float MinScale = 0.8f,
		float MaxScale = 1.2f,
		bool bAlignToNormal = true,
		bool bRandomYaw = true,
		int32 Seed = 0,
		const FString& ActorLabel = TEXT("ScatteredMesh"));

	/**
	 * Scatter static meshes in a rectangular region on the landscape.
	 *
	 * @param MeshPath - Path to the UStaticMesh asset
	 * @param WorldMinX - Min X of the region
	 * @param WorldMinY - Min Y of the region
	 * @param WorldMaxX - Max X of the region
	 * @param WorldMaxY - Max Y of the region
	 * @param Count - Number of instances to spawn
	 * @param MinScale - Minimum random scale
	 * @param MaxScale - Maximum random scale
	 * @param bAlignToNormal - Align instances to the surface normal
	 * @param bRandomYaw - Random yaw rotation
	 * @param Seed - Random seed for reproducibility
	 * @param ActorLabel - Label for the spawned ISM actor
	 * @return Result with spawned instance count
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Foliage")
	static FOpenWorldScatterResult ScatterRectOnLandscape(
		const FString& MeshPath,
		float WorldMinX,
		float WorldMinY,
		float WorldMaxX,
		float WorldMaxY,
		int32 Count,
		float MinScale = 0.8f,
		float MaxScale = 1.2f,
		bool bAlignToNormal = true,
		bool bRandomYaw = true,
		int32 Seed = 0,
		const FString& ActorLabel = TEXT("ScatteredMesh"));

private:
	static class UWorld* GetEditorWorld();
	static bool TraceToLandscape(class UWorld* World, float X, float Y, FVector& OutLocation, FVector& OutNormal);
	static FOpenWorldScatterResult ScatterInternal(
		const FString& MeshPath,
		const TArray<FVector2D>& Candidates,
		int32 Count,
		float MinScale,
		float MaxScale,
		bool bAlignToNormal,
		bool bRandomYaw,
		int32 Seed,
		const FString& ActorLabel);
};
