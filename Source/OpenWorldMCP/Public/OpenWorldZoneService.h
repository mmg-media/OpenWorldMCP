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

	/**
	 * Scatter multiple meshes inside a zone with relative weights (e.g. a flower meadow:
	 * grass 80%, flowers 10%, trees 10%). Each mesh is spawned as its own ISM actor.
	 *
	 * @param ZoneIdOrLabel - Zone id (e.g. "Zone_3") or zone label (e.g. "Wiese")
	 * @param MeshWeightsCsv - "MeshPath1:Weight1;MeshPath2:Weight2;..." (e.g. "/PCG/.../Grass:80;/PCG/.../Flower:10;/PCG/.../Tree:10")
	 * @param Count - Total number of instances to spawn across all meshes
	 * @param MinScale - Minimum random scale
	 * @param MaxScale - Maximum random scale
	 * @param bAlignToNormal - Align instances to the surface normal
	 * @param bRandomYaw - Random yaw rotation
	 * @param Seed - Random seed for reproducibility (0 = random)
	 * @param ActorLabel - Base label for the spawned ISM actors (suffixed per mesh)
	 * @return Scatter result with spawned instance count
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldScatterResult ScatterWeightedInZone(
		const FString& ZoneIdOrLabel,
		const FString& MeshWeightsCsv,
		int32 Count,
		float MinScale = 0.8f,
		float MaxScale = 1.2f,
		bool bAlignToNormal = true,
		bool bRandomYaw = true,
		int32 Seed = 0,
		const FString& ActorLabel = TEXT("ScatteredMesh"));

	/**
	 * Move a zone by a world-space offset. Shifts all zone points and re-anchors the actor.
	 *
	 * @param ZoneIdOrLabel - Zone id or label
	 * @param DeltaX - X offset in world units
	 * @param DeltaY - Y offset in world units
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldZoneResult MoveZone(const FString& ZoneIdOrLabel, float DeltaX, float DeltaY);

	/**
	 * Sculpt the terrain inside a zone footprint with a selected operation.
	 * Only vertices inside the zone (rect bounds or polygon) are affected; the zone edge
	 * can be feathered with EdgeSoftness.
	 *
	 * Supported operations:
	 *   "Flatten" - set height to TargetHeight (or the zone's average height if 0)
	 *   "Raise"   - add HeightDelta (positive) / subtract (negative)
	 *   "Noise"   - add Perlin-like noise (Amplitude/Frequency/Seed)
	 *   "Ramp"    - tilt the zone linearly from TargetHeight at one edge to TargetHeight+HeightDelta at the other
	 *   "Smooth"  - average each vertex with its neighbours (HeightDelta = number of passes)
	 *
	 * @param LandscapeName - Label or name of the landscape actor (empty = first found)
	 * @param ZoneIdOrLabel - Zone id or label
	 * @param Operation - One of: Flatten, Raise, Noise, Ramp, Smooth
	 * @param HeightDelta - Raise amount / ramp height change / smooth passes
	 * @param TargetHeight - Flatten/Ramp target height in world units (0 = auto/current average)
	 * @param Amplitude - Noise amplitude in world units
	 * @param Frequency - Noise frequency
	 * @param EdgeSoftness - World-unit feather band around the zone edge (0 = hard edge)
	 * @param Seed - Noise seed (0 = random)
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Zone")
	static FOpenWorldLandscapeResult SculptInZone(
		const FString& LandscapeName,
		const FString& ZoneIdOrLabel,
		const FString& Operation,
		float HeightDelta = 0.f,
		float TargetHeight = 0.f,
		float Amplitude = 50.f,
		float Frequency = 0.005f,
		float EdgeSoftness = 100.f,
		int32 Seed = 0);

private:
	static class UWorld* GetEditorWorld();
};
