#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class AZoneRegionActor;

/**
 * Simple zone marking mode: drag the left mouse button to draw an axis-aligned rectangle,
 * or click points to build a polygon (finish by clicking the first point, double-click, or Enter).
 * Each finished mark spawns a persistent AZoneRegionActor in the level.
 *
 * Usage inside the mode:
 *   R  - rectangle mode (default)
 *   P  - polygon mode
 *   T  - toggle top-down (ortho) camera
 *   Esc - cancel the current polygon (or exit mode when empty)
 */
class FOpenWorldZoneMarkMode : public FEdMode
{
public:
	static const FEditorModeID EditorModeID;

	FOpenWorldZoneMarkMode();
	virtual ~FOpenWorldZoneMarkMode() override;

	//~ Begin FEdMode
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool DisallowMouseDeltaTracking() const override;
	//~ End FEdMode

	/** Current tool mode: rectangle vs polygon. */
	bool bPolygonMode = false;

private:
	/** Project the mouse cursor onto the ground (landscape trace, fallback to Z=0 plane). */
	bool GetMouseGroundPoint(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& OutPoint);

	/** Finish the currently in-progress mark (rectangle or polygon) and spawn an actor. */
	void CommitCurrentMark(FEditorViewportClient* ViewportClient);

	/** Cancel the in-progress polygon/rectangle. */
	void CancelCurrentMark();

	/** Set the active level editor viewport to a top-down orthographic view. */
	void ToggleTopDownView(FEditorViewportClient* ViewportClient);

	/** Id of the next spawned region (unique within the level). */
	static int32 NextRegionId;

	/** Rectangle drag state. */
	FVector RectStart;
	FVector RectEnd;
	bool bDraggingRect = false;

	/** Polygon point accumulation. */
	TArray<FVector> PolygonPoints;
	FVector CurrentMousePoint;
	bool bHasCurrentMousePoint = false;

	/** Ground Z for drawing flat markers. */
	float MarkerZ = 0.f;
};
