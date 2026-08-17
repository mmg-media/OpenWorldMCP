#include "Zone/ZoneMarkMode.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "UnrealEdGlobals.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "Materials/MaterialInterface.h"
#include "SceneView.h"
#include "Math/UnrealMathUtility.h"
#include "Zone/ZoneRegionActor.h"
#include "EditorModeRegistry.h"

const FEditorModeID FOpenWorldZoneMarkMode::EditorModeID(TEXT("OpenWorldZoneMark"));

int32 FOpenWorldZoneMarkMode::NextRegionId = 1;

namespace
{
	// Draw a translucent filled quad (X/Y axis-aligned footprint) using the PDI.
	void DrawFootprintQuad(FPrimitiveDrawInterface* PDI, const FVector2D& A, const FVector2D& B, float Z,
	                       const FLinearColor& Color)
	{
		const FVector P0(A.X, A.Y, Z);
		const FVector P1(B.X, A.Y, Z);
		const FVector P2(B.X, B.Y, Z);
		const FVector P3(A.X, B.Y, Z);

		// Outline
		PDI->DrawLine(P0, P1, Color, SDPG_Foreground, 2.0f);
		PDI->DrawLine(P1, P2, Color, SDPG_Foreground, 2.0f);
		PDI->DrawLine(P2, P3, Color, SDPG_Foreground, 2.0f);
		PDI->DrawLine(P3, P0, Color, SDPG_Foreground, 2.0f);

		// Translucent fill (two triangles)
		PDI->SetHitProxy(nullptr);
		PDI->DrawLine(P0, P2, FLinearColor(Color.R, Color.G, Color.B, 0.15f), SDPG_Foreground, 0.0f);
		PDI->DrawLine(P1, P3, FLinearColor(Color.R, Color.G, Color.B, 0.15f), SDPG_Foreground, 0.0f);
	}

	void DrawFootprintPolygon(FPrimitiveDrawInterface* PDI, const TArray<FVector2D>& Points, float Z,
	                          const FLinearColor& Color)
	{
		const int32 N = Points.Num();
		if (N < 2)
		{
			return;
		}

		// Outline
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Points[i];
			const FVector2D& B = Points[(i + 1) % N];
			PDI->DrawLine(FVector(A.X, A.Y, Z), FVector(B.X, B.Y, Z), Color, SDPG_Foreground, 2.0f);
		}

		// Simple fan fill from the first point (good enough for a simple helper tool).
		if (N >= 3)
		{
			const FVector2D& First = Points[0];
			for (int32 i = 1; i < N - 1; ++i)
			{
				const FVector2D& A = Points[i];
				const FVector2D& B = Points[i + 1];
				PDI->DrawLine(FVector(First.X, First.Y, Z), FVector(A.X, A.Y, Z),
				              FLinearColor(Color.R, Color.G, Color.B, 0.15f), SDPG_Foreground, 0.0f);
				PDI->DrawLine(FVector(First.X, First.Y, Z), FVector(B.X, B.Y, Z),
				              FLinearColor(Color.R, Color.G, Color.B, 0.15f), SDPG_Foreground, 0.0f);
			}
		}
	}

	// Find the first AZoneRegionActor in the current editor world.
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	// Trace a ray from the mouse cursor down to the world; returns the impact point.
	bool TraceCursorToGround(UWorld* World, const FVector& RayOrigin, const FVector& RayDir, FVector& OutPoint)
	{
		if (!World)
		{
			return false;
		}

		const FVector End = RayOrigin + RayDir * 100000.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OpenWorldZoneTrace), /*bTraceComplex=*/true);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_WorldStatic, Params))
		{
			OutPoint = Hit.ImpactPoint;
			return true;
		}

		// Fallback: intersect the ray with the Z=0 plane.
		if (FMath::Abs(RayDir.Z) > 1e-6f)
		{
			const float T = -RayOrigin.Z / RayDir.Z;
			OutPoint = RayOrigin + RayDir * T;
			return true;
		}
		return false;
	}
}

FOpenWorldZoneMarkMode::FOpenWorldZoneMarkMode()
{
	// The registry assigns Info (ID, name) when the mode is activated.
}

FOpenWorldZoneMarkMode::~FOpenWorldZoneMarkMode()
{
}

void FOpenWorldZoneMarkMode::Enter()
{
	FEdMode::Enter();
	bPolygonMode = false;
	CancelCurrentMark();
	NextRegionId = 1;

	// Ensure a level exists to place markers into.
	if (UWorld* World = GetEditorWorld())
	{
		for (TActorIterator<AZoneRegionActor> It(World); It; ++It)
		{
			++NextRegionId;
		}
	}
}

void FOpenWorldZoneMarkMode::Exit()
{
	CancelCurrentMark();
	FEdMode::Exit();
}

bool FOpenWorldZoneMarkMode::GetMouseGroundPoint(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& OutPoint)
{
	if (!ViewportClient || !Viewport || !ViewportClient->GetWorld())
	{
		return false;
	}

	const int32 MouseX = Viewport->GetMouseX();
	const int32 MouseY = Viewport->GetMouseY();

	// Use the official editor subsystem deprojection (works in perspective and ortho).
	FVector RayOrigin;
	FVector RayDir;
	if (UUnrealEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr)
	{
		if (Sub->ScreenToWorld(FVector2D(MouseX, MouseY), RayOrigin, RayDir))
		{
			UWorld* World = ViewportClient->GetWorld();
			if (TraceCursorToGround(World, RayOrigin, RayDir, OutPoint))
			{
				MarkerZ = OutPoint.Z;
				return true;
			}
		}
	}

	// Fallback: intersect the Z=0 plane with the viewport client's camera direction.
	const FRotator Rot = ViewportClient->GetViewRotation();
	const FVector ViewDir = Rot.Vector();
	const FVector ViewLoc = ViewportClient->GetViewLocation();
	if (FMath::Abs(ViewDir.Z) > 1e-6f)
	{
		const float T = -ViewLoc.Z / ViewDir.Z;
		OutPoint = ViewLoc + ViewDir * T;
		MarkerZ = OutPoint.Z;
		return true;
	}
	return false;
}

bool FOpenWorldZoneMarkMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	const bool bLeftButton = (Key == EKeys::LeftMouseButton);
	const bool bRightButton = (Key == EKeys::RightMouseButton);

	// Keyboard: switch tool, toggle top-down, finish/cancel.
	if (Event == IE_Pressed)
	{
		if (Key == EKeys::R)
		{
			bPolygonMode = false;
			CancelCurrentMark();
			return true;
		}
		if (Key == EKeys::P)
		{
			bPolygonMode = true;
			CancelCurrentMark();
			return true;
		}
		if (Key == EKeys::T)
		{
			ToggleTopDownView(ViewportClient);
			return true;
		}
		if (Key == EKeys::Enter)
		{
			CommitCurrentMark(ViewportClient);
			return true;
		}
		if (Key == EKeys::Escape)
		{
			if (bPolygonMode && PolygonPoints.Num() > 0)
			{
				CancelCurrentMark();
				return true;
			}
			// Let the editor handle Esc (exit mode).
			return false;
		}
	}

	if (bLeftButton)
	{
		if (Event == IE_Pressed)
		{
			FVector Ground;
			if (GetMouseGroundPoint(ViewportClient, Viewport, Ground))
			{
				if (bPolygonMode)
				{
					// Start or extend the polygon. Finishing is handled below (double-click / close click).
					PolygonPoints.Add(Ground);
					return true;
				}
				else
				{
					// Begin rectangle drag.
					RectStart = Ground;
					RectEnd = Ground;
					bDraggingRect = true;
					return true;
				}
			}
			return false;
		}

		if (Event == IE_Released && !bPolygonMode)
		{
			if (bDraggingRect)
			{
				CommitCurrentMark(ViewportClient);
				bDraggingRect = false;
			}
			return true;
		}

		if (Event == IE_DoubleClick && bPolygonMode)
		{
			CommitCurrentMark(ViewportClient);
			return true;
		}
	}

	// Everything else passes through to the editor.
	return false;
}

bool FOpenWorldZoneMarkMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	if (bDraggingRect || bPolygonMode)
	{
		FVector Ground;
		if (GetMouseGroundPoint(InViewportClient, InViewport, Ground))
		{
			RectEnd = Ground;
			CurrentMousePoint = Ground;
			bHasCurrentMousePoint = true;
		}
		return true;
	}
	return false;
}

bool FOpenWorldZoneMarkMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y)
{
	FVector Ground;
	if (GetMouseGroundPoint(ViewportClient, Viewport, Ground))
	{
		CurrentMousePoint = Ground;
		bHasCurrentMousePoint = true;
	}
	return false; // do not block selection etc.
}

void FOpenWorldZoneMarkMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	// Draw all existing region actors.
	if (UWorld* World = GetEditorWorld())
	{
		for (TActorIterator<AZoneRegionActor> It(World); It; ++It)
		{
			const AZoneRegionActor* Region = *It;
			const FOpenWorldZone& Zone = Region->Zone;
			const float Z = Zone.GroundZ + 5.f;
			if (Zone.bIsPolygon)
			{
				DrawFootprintPolygon(PDI, Zone.Points, Z, Zone.Color);
			}
			else if (Zone.Points.Num() >= 2)
			{
				DrawFootprintQuad(PDI, Zone.Points[0], Zone.Points[1], Z, Zone.Color);
			}
		}
	}

	// Draw the in-progress mark.
	if (bPolygonMode)
	{
		if (PolygonPoints.Num() > 0)
		{
			TArray<FVector2D> Points;
			for (const FVector& P : PolygonPoints)
			{
				Points.Add(FVector2D(P.X, P.Y));
			}
			if (bHasCurrentMousePoint && PolygonPoints.Num() >= 1)
			{
				Points.Add(FVector2D(CurrentMousePoint.X, CurrentMousePoint.Y));
			}
			const FLinearColor Color = FLinearColor(1.0f, 0.6f, 0.0f);
			DrawFootprintPolygon(PDI, Points, MarkerZ + 5.f, Color);

			for (const FVector& P : PolygonPoints)
			{
				PDI->DrawPoint(FVector(P.X, P.Y, MarkerZ + 5.f), Color, 8.0f, SDPG_Foreground);
			}
		}
	}
	else if (bDraggingRect)
	{
		const FVector2D Min(FMath::Min(RectStart.X, RectEnd.X), FMath::Min(RectStart.Y, RectEnd.Y));
		const FVector2D Max(FMath::Max(RectStart.X, RectEnd.X), FMath::Max(RectStart.Y, RectEnd.Y));
		DrawFootprintQuad(PDI, Min, Max, MarkerZ + 5.f, FLinearColor(0.2f, 0.9f, 0.3f));
	}

	FEdMode::Render(View, Viewport, PDI);
}

void FOpenWorldZoneMarkMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	// Simple help text overlay.
	const FString Help = bPolygonMode
		? TEXT("Zone (Polygon): Click to add points. Finish: click first point, double-click, or Enter. R=rect P=poly T=top-down Esc=cancel")
		: TEXT("Zone (Rect): Drag left mouse to draw a rectangle. R=rect P=polygon T=top-down");

	if (Canvas)
	{
		FCanvasTextItem TextItem(FVector2D(10, 10), FText::FromString(Help), GEngine->GetSmallFont(), FLinearColor::White);
		TextItem.Draw(Canvas);
	}
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

bool FOpenWorldZoneMarkMode::DisallowMouseDeltaTracking() const
{
	// In the marking mode we interpret raw mouse clicks ourselves.
	return bDraggingRect || bPolygonMode;
}

void FOpenWorldZoneMarkMode::CommitCurrentMark(FEditorViewportClient* ViewportClient)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		CancelCurrentMark();
		return;
	}

	FOpenWorldZone Zone;
	Zone.Id = FString::Printf(TEXT("Zone_%d"), NextRegionId++);
	Zone.GroundZ = MarkerZ;

	if (bPolygonMode)
	{
		if (PolygonPoints.Num() < 3)
		{
			CancelCurrentMark();
			return;
		}
		// Drop the closing duplicate if the user clicked near the first point.
		Zone.bIsPolygon = true;
		for (const FVector& P : PolygonPoints)
		{
			Zone.Points.Add(FVector2D(P.X, P.Y));
		}
		Zone.Label = FString::Printf(TEXT("Polygon %d"), NextRegionId - 1);
		Zone.Color = FLinearColor(1.0f, 0.6f, 0.0f);
	}
	else
	{
		const FVector2D Min(FMath::Min(RectStart.X, RectEnd.X), FMath::Min(RectStart.Y, RectEnd.Y));
		const FVector2D Max(FMath::Max(RectStart.X, RectEnd.X), FMath::Max(RectStart.Y, RectEnd.Y));
		if (Max.X - Min.X < 1.f || Max.Y - Min.Y < 1.f)
		{
			CancelCurrentMark();
			return;
		}
		Zone.bIsPolygon = false;
		Zone.Points.Add(Min);
		Zone.Points.Add(Max);
		Zone.Label = FString::Printf(TEXT("Rect %d"), NextRegionId - 1);
		Zone.Color = FLinearColor(0.2f, 0.9f, 0.3f);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*Zone.Id);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AZoneRegionActor* Actor = World->SpawnActor<AZoneRegionActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!Actor)
	{
		Actor = World->SpawnActor<AZoneRegionActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
	if (!Actor)
	{
		CancelCurrentMark();
		return;
	}

	Actor->Zone = MoveTemp(Zone);
	Actor->SetActorLabel(Actor->Zone.Label, false);
	Actor->SyncToZone();

	if (GEditor)
	{
		GEditor->SelectActor(Actor, /*bSelected=*/true, /*bNotify=*/true);
	}

	CancelCurrentMark();
}

void FOpenWorldZoneMarkMode::CancelCurrentMark()
{
	bDraggingRect = false;
	RectStart = FVector::ZeroVector;
	RectEnd = FVector::ZeroVector;
	PolygonPoints.Reset();
	bHasCurrentMousePoint = false;
}

void FOpenWorldZoneMarkMode::ToggleTopDownView(FEditorViewportClient* ViewportClient)
{
	if (!ViewportClient)
	{
		return;
	}

	FLevelEditorViewportClient* LevelVC = static_cast<FLevelEditorViewportClient*>(ViewportClient);
	ELevelViewportType CurrentType = LevelVC->GetViewportType();
	if (CurrentType != LVT_OrthoXY)
	{
		LevelVC->SetViewportType(LVT_OrthoXY);
		LevelVC->SetViewRotation(FRotator(-90.f, -90.f, 0.f));
	}
	else
	{
		LevelVC->SetViewportType(LVT_Perspective);
	}
}