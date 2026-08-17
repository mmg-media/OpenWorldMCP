#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "OpenWorldZoneService.generated.h"

USTRUCT(BlueprintType)
struct FOpenWorldZoneResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	FString Message;
};

/**
 * Zone marking tools: mark regions in the level (mouse-drag rectangles or click-point
 * polygons via the zone marking editor mode) and expose them to the AI. Regions are
 * persistent AZoneRegionActor instances stored in the level.
 */
UCLASS(BlueprintType)
class OPENWORLDMCP_API UOpenWorldZoneService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Activate the zone marking editor mode. While active, drag the left mouse button to draw
	 * a rectangle, or press P for polygon mode and click points (finish: click first point,
	 * double-click, or Enter). Press T for a top-down orthographic camera, R to switch back
	 * to rectangle mode, Esc to cancel. Each finished mark creates a persistent region actor.
	 *
	 * @param bPolygon - true to start in polygon mode, false for rectangle mode
	 * @return Result with success flag and message
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult EnableZoneMarking(bool bPolygon = false);

	/**
	 * Deactivate the zone marking editor mode.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult DisableZoneMarking();

	/**
	 * List all marked zones in the level as a JSON array of {id,label,bIsPolygon,points[],color}.
	 *
	 * @param bJson - always true; included for the schema
	 * @return JSON string of zones
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FString ListZones(bool bJson = true);

	/**
	 * Remove all zone region actors from the level.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult ClearZones();

	/**
	 * Create a zone programmatically (rectangular region).
	 *
	 * @param MinX - Min world X of the rectangle
	 * @param MinY - Min world Y of the rectangle
	 * @param MaxX - Max world X of the rectangle
	 * @param MaxY - Max world Y of the rectangle
	 * @param Label - Optional label
	 * @return Result with success flag
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult CreateRectZone(float MinX, float MinY, float MaxX, float MaxY, const FString& Label = TEXT(""));

	/**
	 * Create a zone programmatically (polygon region). Points are world-space X/Y pairs passed
	 * as "X,Y;X,Y;..." (semicolon separated). At least 3 points are required.
	 *
	 * @param PointsCsv - "X1,Y1;X2,Y2;X3,Y3;..."
	 * @param Label - Optional label
	 * @return Result with success flag
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult CreatePolygonZone(const FString& PointsCsv, const FString& Label = TEXT(""));

	/**
	 * Switch the active level editor viewport to a top-down orthographic view (useful for
	 * pixel-accurate zone marking).
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult SetTopDownView();

	/**
	 * Scatter static meshes inside a marked zone (rectangle or polygon). Only positions that
	 * fall inside the zone footprint are used; instances are traced onto the landscape surface.
	 *
	 * @param MeshPath - Path to the UStaticMesh asset (e.g. "/PCG/SampleContent/SimpleForest/Meshes/PCG_Tree_01")
	 * @param ZoneIdOrLabel - Zone id (e.g. "Zone_3") or zone label (e.g. "DorfGebiet")
	 * @param Count - Number of instances to spawn
	 * @param MinScale - Minimum random scale
	 * @param MaxScale - Maximum random scale
	 * @param bAlignToNormal - Align instances to the surface normal
	 * @param bRandomYaw - Random yaw rotation
	 * @param Seed - Random seed for reproducibility (0 = random)
	 * @param ActorLabel - Label for the spawned ISM actor
	 * @return Scatter result with spawned instance count
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldScatterResult ScatterInZone(
		const FString& MeshPath,
		const FString& ZoneIdOrLabel,
		int32 Count,
		float MinScale = 0.8f,
		float MaxScale = 1.2f,
		bool bAlignToNormal = true,
		bool bRandomYaw = true,
		int32 Seed = 0,
		const FString& ActorLabel = TEXT("ScatteredMesh"));

private:
	static class UWorld* GetEditorWorld();
};
