#include "OpenWorldZoneService.h"

#include "OpenWorldFoliageService.h"
#include "Editor.h"
#include "EditorModeRegistry.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "JsonObjectConverter.h"
#include "LevelEditorViewport.h"
#include "Math/UnrealMathUtility.h"
#include "Serialization/JsonSerializer.h"
#include "Zone/ZoneMarkMode.h"
#include "Zone/ZoneRegionActor.h"

static UWorld* GetZoneEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

UWorld* UOpenWorldZoneService::GetEditorWorld()
{
	return GetZoneEditorWorld();
}

static FOpenWorldZoneResult ZoneError(const FString& Message)
{
	FOpenWorldZoneResult Result;
	Result.bSuccess = false;
	Result.Message = Message;
	return Result;
}

static FOpenWorldZoneResult ZoneOk(const FString& Message)
{
	FOpenWorldZoneResult Result;
	Result.bSuccess = true;
	Result.Message = Message;
	return Result;
}

static TArray<AZoneRegionActor*> FindAllZones(UWorld* World)
{
	TArray<AZoneRegionActor*> Zones;
	if (World)
	{
		for (TActorIterator<AZoneRegionActor> It(World); It; ++It)
		{
			Zones.Add(*It);
		}
	}
	return Zones;
}

FOpenWorldZoneResult UOpenWorldZoneService::EnableZoneMarking(bool bPolygon)
{
	if (GEditor)
	{
		// The mode is registered by the module at startup. Register only if somehow missing
		// (RegisterMode asserts when the ID already exists).
		if (!FEditorModeRegistry::Get().GetFactoryMap().Contains(FOpenWorldZoneMarkMode::EditorModeID))
		{
			FEditorModeRegistry::Get().RegisterMode<FOpenWorldZoneMarkMode>(
				FOpenWorldZoneMarkMode::EditorModeID,
				FText::FromString(TEXT("Zone Mark")));
		}

		GLevelEditorModeTools().ActivateMode(FOpenWorldZoneMarkMode::EditorModeID);

		if (FEdMode* Mode = GLevelEditorModeTools().GetActiveMode(FOpenWorldZoneMarkMode::EditorModeID))
		{
			FOpenWorldZoneMarkMode* ZoneMode = static_cast<FOpenWorldZoneMarkMode*>(Mode);
			ZoneMode->bPolygonMode = bPolygon;
		}
		return ZoneOk(TEXT("Zone marking enabled"));
	}
	return ZoneError(TEXT("No editor available"));
}

FOpenWorldZoneResult UOpenWorldZoneService::DisableZoneMarking()
{
	if (GEditor)
	{
		GLevelEditorModeTools().DeactivateMode(FOpenWorldZoneMarkMode::EditorModeID);
		return ZoneOk(TEXT("Zone marking disabled"));
	}
	return ZoneError(TEXT("No editor available"));
}

FString UOpenWorldZoneService::ListZones(bool bJson)
{
	UWorld* World = GetZoneEditorWorld();
	TArray<AZoneRegionActor*> Zones = FindAllZones(World);

	TArray<TSharedPtr<FJsonValue>> Array;
	for (const AZoneRegionActor* Region : Zones)
	{
		const FOpenWorldZone& Z = Region->Zone;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), Z.Id);
		Obj->SetStringField(TEXT("label"), Z.Label);
		Obj->SetBoolField(TEXT("bIsPolygon"), Z.bIsPolygon);
		Obj->SetNumberField(TEXT("ground_z"), Z.GroundZ);
		Obj->SetStringField(TEXT("color"), Z.Color.ToString());

		TArray<TSharedPtr<FJsonValue>> Points;
		for (const FVector2D& P : Z.Points)
		{
			TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
			PObj->SetNumberField(TEXT("x"), P.X);
			PObj->SetNumberField(TEXT("y"), P.Y);
			Points.Add(MakeShared<FJsonValueObject>(PObj));
		}
		Obj->SetArrayField(TEXT("points"), Points);
		Array.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Array, Writer);
	Writer->Close();
	return Json;
}

FOpenWorldZoneResult UOpenWorldZoneService::ClearZones()
{
	UWorld* World = GetZoneEditorWorld();
	if (!World)
	{
		return ZoneError(TEXT("No editor world available"));
	}

	int32 Count = 0;
	for (TActorIterator<AZoneRegionActor> It(World); It; ++It)
	{
		It->Destroy();
		++Count;
	}
	return ZoneOk(FString::Printf(TEXT("Removed %d zone(s)"), Count));
}

FOpenWorldZoneResult UOpenWorldZoneService::CreateRectZone(float MinX, float MinY, float MaxX, float MaxY, const FString& Label)
{
	UWorld* World = GetZoneEditorWorld();
	if (!World)
	{
		return ZoneError(TEXT("No editor world available"));
	}
	if (MaxX <= MinX || MaxY <= MinY)
	{
		return ZoneError(TEXT("Invalid rectangle bounds (MaxX > MinX and MaxY > MinY required)"));
	}

	FOpenWorldZone Zone;
	Zone.Id = FString::Printf(TEXT("Zone_%lld"), (long long)GFrameCounter);
	Zone.Label = Label.IsEmpty() ? TEXT("Rect") : Label;
	Zone.bIsPolygon = false;
	Zone.Points.Add(FVector2D(MinX, MinY));
	Zone.Points.Add(FVector2D(MaxX, MaxY));
	Zone.Color = FLinearColor(0.2f, 0.9f, 0.3f);

	// Estimate ground height at the rectangle center.
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OpenWorldZoneTrace), /*bTraceComplex=*/true);
		FHitResult Hit;
		const FVector Center((MinX + MaxX) * 0.5f, (MinY + MaxY) * 0.5f, 50000.f);
		if (World->LineTraceSingleByChannel(Hit, Center, Center - FVector(0, 0, 100000.f), ECC_WorldStatic, Params))
		{
			Zone.GroundZ = Hit.ImpactPoint.Z;
		}
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
		return ZoneError(TEXT("Failed to spawn zone actor"));
	}

	Actor->Zone = MoveTemp(Zone);
	Actor->SetActorLabel(Actor->Zone.Label, false);
	Actor->SyncToZone();
	return ZoneOk(FString::Printf(TEXT("Created rect zone %s"), *Actor->Zone.Id));
}

FOpenWorldZoneResult UOpenWorldZoneService::CreatePolygonZone(const FString& PointsCsv, const FString& Label)
{
	UWorld* World = GetZoneEditorWorld();
	if (!World)
	{
		return ZoneError(TEXT("No editor world available"));
	}

	// Parse "X,Y;X,Y;X,Y"
	TArray<FString> Pairs;
	PointsCsv.ParseIntoArray(Pairs, TEXT(";"), true);

	TArray<FVector2D> Points;
	for (const FString& Pair : Pairs)
	{
		TArray<FString> XY;
		Pair.ParseIntoArray(XY, TEXT(","), true);
		if (XY.Num() != 2)
		{
			continue;
		}
		Points.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
	}

	if (Points.Num() < 3)
	{
		return ZoneError(TEXT("At least 3 points required (format: \"X1,Y1;X2,Y2;X3,Y3\")"));
	}

	FOpenWorldZone Zone;
	Zone.Id = FString::Printf(TEXT("Zone_%lld"), (long long)GFrameCounter);
	Zone.Label = Label.IsEmpty() ? TEXT("Polygon") : Label;
	Zone.bIsPolygon = true;
	Zone.Points = MoveTemp(Points);
	Zone.Color = FLinearColor(1.0f, 0.6f, 0.0f);

	// Estimate ground height at the polygon centroid.
	FBox2D Bounds(ForceInit);
	for (const FVector2D& P : Zone.Points)
	{
		Bounds += P;
	}
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OpenWorldZoneTrace), /*bTraceComplex=*/true);
		FHitResult Hit;
		const FVector2D C = Bounds.GetCenter();
		const FVector Center(C.X, C.Y, 50000.f);
		if (World->LineTraceSingleByChannel(Hit, Center, Center - FVector(0, 0, 100000.f), ECC_WorldStatic, Params))
		{
			Zone.GroundZ = Hit.ImpactPoint.Z;
		}
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
		return ZoneError(TEXT("Failed to spawn zone actor"));
	}

	Actor->Zone = MoveTemp(Zone);
	Actor->SetActorLabel(Actor->Zone.Label, false);
	Actor->SyncToZone();
	return ZoneOk(FString::Printf(TEXT("Created polygon zone %s"), *Actor->Zone.Id));
}

FOpenWorldZoneResult UOpenWorldZoneService::SetTopDownView()
{
	class FLevelEditorViewportClient* LevelVC = nullptr;
	if (GEditor)
	{
		for (const FLevelEditorViewportClient* VC : GEditor->GetLevelViewportClients())
		{
			LevelVC = const_cast<FLevelEditorViewportClient*>(VC);
			break;
		}
	}
	if (!LevelVC)
	{
		return ZoneError(TEXT("No level editor viewport found"));
	}

	if (LevelVC->GetViewportType() != LVT_OrthoXY)
	{
		LevelVC->SetViewportType(LVT_OrthoXY);
		LevelVC->SetViewRotation(FRotator(-90.f, -90.f, 0.f));
		return ZoneOk(TEXT("Top-down view enabled"));
	}
	LevelVC->SetViewportType(LVT_Perspective);
	return ZoneOk(TEXT("Top-down view disabled"));
}

/**
 * Ray-casting point-in-polygon test in 2D (X/Y world space).
 */
static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	if (Polygon.Num() < 3)
	{
		return false;
	}

	bool bInside = false;
	int32 J = Polygon.Num() - 1;
	for (int32 I = 0; I < Polygon.Num(); ++I)
	{
		const FVector2D& A = Polygon[I];
		const FVector2D& B = Polygon[J];
		if ((A.Y > Point.Y) != (B.Y > Point.Y) &&
			Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
		{
			bInside = !bInside;
		}
		J = I;
	}
	return bInside;
}

static TArray<FVector2D> GenerateZoneCandidates(const AZoneRegionActor* Region, const FRandomStream& Stream, int32 Desired)
{
	TArray<FVector2D> Candidates;
	if (!Region)
	{
		return Candidates;
	}

	const FOpenWorldZone& Z = Region->Zone;
	if (Z.Points.Num() == 0)
	{
		return Candidates;
	}

	if (!Z.bIsPolygon)
	{
		// Rectangle: first point = min, second point = max.
		const FVector2D Min = Z.Points[0];
		const FVector2D Max = Z.Points.Num() > 1 ? Z.Points[1] : Min;
		for (int32 I = 0; I < Desired; ++I)
		{
			Candidates.Add(FVector2D(
				Stream.FRandRange(Min.X, Max.X),
				Stream.FRandRange(Min.Y, Max.Y)));
		}
		return Candidates;
	}

	// Polygon: generate points inside the bounding box and keep those inside the shape.
	FBox2D Bounds(ForceInit);
	for (const FVector2D& P : Z.Points)
	{
		Bounds += P;
	}

	for (int32 I = 0; I < Desired * 4; ++I)
	{
		FVector2D Candidate(
			Stream.FRandRange(Bounds.Min.X, Bounds.Max.X),
			Stream.FRandRange(Bounds.Min.Y, Bounds.Max.Y));
		if (IsPointInPolygon(Candidate, Z.Points))
		{
			Candidates.Add(Candidate);
		}
		if (Candidates.Num() >= Desired)
		{
			break;
		}
	}

	return Candidates;
}

FOpenWorldScatterResult UOpenWorldZoneService::ScatterInZone(
	const FString& MeshPath,
	const FString& ZoneIdOrLabel,
	int32 Count,
	float MinScale,
	float MaxScale,
	bool bAlignToNormal,
	bool bRandomYaw,
	int32 Seed,
	const FString& ActorLabel)
{
	UWorld* World = GetZoneEditorWorld();
	if (!World)
	{
		FOpenWorldScatterResult R;
		R.bSuccess = false;
		R.ErrorMessage = TEXT("No editor world available");
		return R;
	}

	if (MeshPath.IsEmpty() || Count <= 0)
	{
		FOpenWorldScatterResult R;
		R.bSuccess = false;
		R.ErrorMessage = TEXT("MeshPath and Count > 0 required");
		return R;
	}

	// Find the zone by id or label (case-insensitive).
	AZoneRegionActor* Target = nullptr;
	for (TActorIterator<AZoneRegionActor> It(World); It; ++It)
	{
		AZoneRegionActor* Region = *It;
		if (Region->Zone.Id.Equals(ZoneIdOrLabel, ESearchCase::IgnoreCase) ||
			Region->Zone.Label.Equals(ZoneIdOrLabel, ESearchCase::IgnoreCase))
		{
			Target = Region;
			break;
		}
	}

	if (!Target)
	{
		FOpenWorldScatterResult R;
		R.bSuccess = false;
		R.ErrorMessage = FString::Printf(TEXT("No zone found with id/label \"%s\""), *ZoneIdOrLabel);
		return R;
	}

	FRandomStream Stream(Seed != 0 ? Seed : FMath::Rand());
	TArray<FVector2D> Candidates = GenerateZoneCandidates(Target, Stream, Count * 2);
	if (Candidates.Num() == 0)
	{
		FOpenWorldScatterResult R;
		R.bSuccess = false;
		R.ErrorMessage = TEXT("No positions generated inside the zone");
		return R;
	}

	return UOpenWorldFoliageService::ScatterInternal(
		MeshPath, Candidates, Count, MinScale, MaxScale, bAlignToNormal, bRandomYaw, Seed, ActorLabel);
}
