#include "OpenWorldLandscapeService.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeDataAccess.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "ScopedTransaction.h"
#include "Math/UnrealMathUtility.h"

namespace
{
	FOpenWorldLandscapeResult LandscapeError(const FString& Message)
	{
		FOpenWorldLandscapeResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = Message;
		return Result;
	}

	FOpenWorldLandscapeResult LandscapeOk()
	{
		FOpenWorldLandscapeResult Result;
		Result.bSuccess = true;
		return Result;
	}

	/** Returns the landscape extent clipped to a brush circle around (CenterX, CenterY). */
	bool ComputeBrushBounds(ULandscapeInfo* Info, float CenterX, float CenterY, float Radius,
		int32& OutMinX, int32& OutMinY, int32& OutMaxX, int32& OutMaxY)
	{
		int32 LandMinX, LandMinY, LandMaxX, LandMaxY;
		if (!Info->GetLandscapeExtent(LandMinX, LandMinY, LandMaxX, LandMaxY))
		{
			return false;
		}

		OutMinX = FMath::Max(FMath::FloorToInt(CenterX - Radius), LandMinX);
		OutMinY = FMath::Max(FMath::FloorToInt(CenterY - Radius), LandMinY);
		OutMaxX = FMath::Min(FMath::CeilToInt(CenterX + Radius), LandMaxX);
		OutMaxY = FMath::Min(FMath::CeilToInt(CenterY + Radius), LandMaxY);
		return OutMinX <= OutMaxX && OutMinY <= OutMaxY;
	}

	/** Smooth circular falloff: 1.0 in the center, 0.0 at the edge. */
	float BrushFalloff(float Distance, float Radius)
	{
		if (Radius <= 0.f)
		{
			return 1.f;
		}
		const float T = FMath::Clamp(Distance / Radius, 0.f, 1.f);
		return FMath::Clamp(0.5f + 0.5f * FMath::Cos(PI * T), 0.f, 1.f);
	}

	/** The active edit-layer GUID that edits are written to. */
	FGuid ResolveTargetLayer(ALandscape* Landscape)
	{
		FGuid LayerGuid = Landscape->GetEditingLayer();
		if (!LayerGuid.IsValid())
		{
			const TArray<ULandscapeEditLayerBase*> Layers = Landscape->GetEditLayers();
			if (Layers.Num() > 0 && Layers[0])
			{
				LayerGuid = Layers[0]->GetGuid();
			}
		}
		return LayerGuid;
	}

	/** Applies the pending layer updates and rebuilds collision for every matching proxy. */
	void FinalizeHeightEdit(ALandscape* Landscape)
	{
		if (!Landscape)
		{
			return;
		}

		if (ALandscape* MainActor = Landscape->GetLandscapeActor())
		{
			MainActor->ForceUpdateLayersContent();
		}

		UWorld* World = Landscape->GetWorld();
		if (!World)
		{
			return;
		}

		const FGuid LandscapeGuid = Landscape->GetLandscapeGuid();
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Proxy = *It;
			if (!Proxy || Proxy->GetLandscapeGuid() != LandscapeGuid)
			{
				continue;
			}
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				if (!Component)
				{
					continue;
				}
				ULandscapeHeightfieldCollisionComponent* Collision = Component->GetCollisionComponent();
				if (Collision)
				{
					Collision->RecreateCollision();
				}
			}
		}
	}

	/**
	 * Writes a height rectangle through the landscape edit-layer system. Writing through an edit
	 * layer is required so the change survives the layer-resolve step instead of being reverted.
	 */
	void CommitHeights(ALandscape* Landscape, ULandscapeInfo* Info,
		int32 MinX, int32 MinY, int32 MaxX, int32 MaxY, const uint16* Heights)
	{
		const FGuid LayerGuid = ResolveTargetLayer(Landscape);

		FScopedSetLandscapeEditingLayer EditScope(
			Landscape,
			LayerGuid,
			[Landscape]()
			{
				if (Landscape)
				{
					Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
				}
			});

		FHeightmapAccessor<false> Accessor(Info);
		Accessor.SetData(MinX, MinY, MaxX, MaxY, Heights);
		Accessor.Flush();
	}

	/** Converts a world-space height offset into uint16 heightmap units. */
	float HeightDeltaToUnits(float WorldDelta, const FVector& LandscapeScale)
	{
		const float ZScale = LandscapeScale.Z;
		return WorldDelta / (LANDSCAPE_ZSCALE * ZScale);
	}

	/** Reads the current heightmap rectangle into OutHeights. */
	void ReadHeights(ULandscapeInfo* Info, int32 MinX, int32 MinY, int32 MaxX, int32 MaxY,
		TArray<uint16>& OutHeights)
	{
		const int32 SizeX = MaxX - MinX + 1;
		const int32 SizeY = MaxY - MinY + 1;
		OutHeights.SetNumUninitialized(SizeX * SizeY);
		FLandscapeEditDataInterface EditInterface(Info);
		EditInterface.GetHeightData(MinX, MinY, MaxX, MaxY, OutHeights.GetData(), 0);
	}

	/** Applies a per-vertex height transform and commits the result. */
	template <typename FHeightOp>
	FOpenWorldLandscapeResult ApplyHeightOp(ALandscape* Landscape, ULandscapeInfo* Info,
		int32 MinX, int32 MinY, int32 MaxX, int32 MaxY, FHeightOp Op)
	{
		const int32 SizeX = MaxX - MinX + 1;
		const int32 SizeY = MaxY - MinY + 1;

		TArray<uint16> Heights;
		ReadHeights(Info, MinX, MinY, MaxX, MaxY, Heights);

		for (int32 Y = 0; Y < SizeY; ++Y)
		{
			for (int32 X = 0; X < SizeX; ++X)
			{
				const int32 Index = Y * SizeX + X;
				const float Current = static_cast<float>(Heights[Index]);
				const float NewValue = FMath::Clamp(Op(Current, MinX + X, MinY + Y), 0.f, 65535.f);
				Heights[Index] = static_cast<uint16>(FMath::RoundToInt(NewValue));
			}
		}

		CommitHeights(Landscape, Info, MinX, MinY, MaxX, MaxY, Heights.GetData());
		Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
		FinalizeHeightEdit(Landscape);
		return LandscapeOk();
	}
}

ALandscape* UOpenWorldLandscapeService::FindLandscape(const FString& LandscapeName)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		ALandscape* Landscape = *It;
		const FString Label = Landscape->GetActorLabel(false);
		if (LandscapeName.IsEmpty() || Label == LandscapeName || Landscape->GetName() == LandscapeName)
		{
			return Landscape;
		}
	}
	return nullptr;
}

float UOpenWorldLandscapeService::Noise2D(float X, float Y, int32 Seed)
{
	auto Hash = [](int32 A, int32 B, int32 S) -> float
	{
		const int64 N = (static_cast<int64>(A) * 374761393 + static_cast<int64>(B) * 668265263 + static_cast<int64>(S) * 1442695040888963407LL + 1597334677);
		const int32 H = static_cast<int32>((N ^ (N >> 13)) * 1274126177);
		return static_cast<float>((H ^ (H >> 16)) & 0x7FFFFFFF) / 1073741824.f - 1.f;
	};

	const int32 Xi = FMath::FloorToInt(X);
	const int32 Yi = FMath::FloorToInt(Y);
	const float Xf = X - static_cast<float>(Xi);
	const float Yf = Y - static_cast<float>(Yi);
	const float U = Xf * Xf * (3.f - 2.f * Xf);
	const float V = Yf * Yf * (3.f - 2.f * Yf);

	const float A = Hash(Xi, Yi, Seed);
	const float B = Hash(Xi + 1, Yi, Seed);
	const float C = Hash(Xi, Yi + 1, Seed);
	const float D = Hash(Xi + 1, Yi + 1, Seed);

	return FMath::Lerp(FMath::Lerp(A, B, U), FMath::Lerp(C, D, U), V);
}

TArray<FString> UOpenWorldLandscapeService::ListLandscapes()
{
	TArray<FString> Labels;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return Labels;
	}

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		Labels.Add((*It)->GetActorLabel(false));
	}
	return Labels;
}

FOpenWorldHeightResult UOpenWorldLandscapeService::GetHeightAtLocation(float WorldX, float WorldY)
{
	FOpenWorldHeightResult Result;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return Result;
	}

	const FVector Start(WorldX, WorldY, 100000.f);
	const FVector End(WorldX, WorldY, -100000.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OpenWorldHeightTrace), true);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscape>())
		{
			Result.bSuccess = true;
			Result.Height = Hit.ImpactPoint.Z;
		}
	}
	return Result;
}

FOpenWorldLandscapeResult UOpenWorldLandscapeService::SculptAtLocation(
	const FString& LandscapeName,
	float WorldX,
	float WorldY,
	float Radius,
	float HeightDelta,
	bool bSmoothFalloff)
{
	ALandscape* Landscape = FindLandscape(LandscapeName);
	if (!Landscape)
	{
		return LandscapeError(TEXT("Landscape not found"));
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		return LandscapeError(TEXT("LandscapeInfo not available"));
	}

	const FVector Location = Landscape->GetActorLocation();
	const FVector Scale = Landscape->GetActorScale3D();

	const float LocalX = (WorldX - Location.X) / Scale.X;
	const float LocalY = (WorldY - Location.Y) / Scale.Y;
	const float LocalRadius = Radius / Scale.X;

	int32 MinX, MinY, MaxX, MaxY;
	if (!ComputeBrushBounds(Info, LocalX, LocalY, LocalRadius, MinX, MinY, MaxX, MaxY))
	{
		return LandscapeOk();
	}

	FScopedTransaction Transaction(NSLOCTEXT("OpenWorldMCP", "Sculpt", "Sculpt Landscape"));
	const float StrengthInUnits = HeightDeltaToUnits(HeightDelta, Scale);

	return ApplyHeightOp(Landscape, Info, MinX, MinY, MaxX, MaxY,
		[&](float Current, int32 VertexX, int32 VertexY) -> float
		{
			const float Distance = FMath::Sqrt(FMath::Square(static_cast<float>(VertexX) - LocalX) + FMath::Square(static_cast<float>(VertexY) - LocalY));
			const float Falloff = bSmoothFalloff ? BrushFalloff(Distance, LocalRadius) : ((Distance <= LocalRadius) ? 1.f : 0.f);
			return Current + StrengthInUnits * Falloff;
		});
}

FOpenWorldLandscapeResult UOpenWorldLandscapeService::FlattenAtLocation(
	const FString& LandscapeName,
	float WorldX,
	float WorldY,
	float Radius,
	float TargetHeight,
	bool bSmoothFalloff)
{
	ALandscape* Landscape = FindLandscape(LandscapeName);
	if (!Landscape)
	{
		return LandscapeError(TEXT("Landscape not found"));
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		return LandscapeError(TEXT("LandscapeInfo not available"));
	}

	const FVector Location = Landscape->GetActorLocation();
	const FVector Scale = Landscape->GetActorScale3D();

	const float LocalX = (WorldX - Location.X) / Scale.X;
	const float LocalY = (WorldY - Location.Y) / Scale.Y;
	const float LocalRadius = Radius / Scale.X;

	int32 MinX, MinY, MaxX, MaxY;
	if (!ComputeBrushBounds(Info, LocalX, LocalY, LocalRadius, MinX, MinY, MaxX, MaxY))
	{
		return LandscapeOk();
	}

	FScopedTransaction Transaction(NSLOCTEXT("OpenWorldMCP", "Flatten", "Flatten Landscape"));

	const float ZScale = Scale.Z;
	const uint16 TargetTex = static_cast<uint16>(FMath::RoundToInt(TargetHeight / (LANDSCAPE_ZSCALE * ZScale) + LandscapeDataAccess::MidValue));

	return ApplyHeightOp(Landscape, Info, MinX, MinY, MaxX, MaxY,
		[&](float Current, int32 VertexX, int32 VertexY) -> float
		{
			const float Distance = FMath::Sqrt(FMath::Square(static_cast<float>(VertexX) - LocalX) + FMath::Square(static_cast<float>(VertexY) - LocalY));
			const float Falloff = bSmoothFalloff ? BrushFalloff(Distance, LocalRadius) : ((Distance <= LocalRadius) ? 1.f : 0.f);
			return FMath::Lerp(Current, static_cast<float>(TargetTex), Falloff);
		});
}

FOpenWorldLandscapeResult UOpenWorldLandscapeService::ApplyNoise(
	const FString& LandscapeName,
	float WorldCenterX,
	float WorldCenterY,
	float Radius,
	float Amplitude,
	float Frequency,
	int32 Seed)
{
	ALandscape* Landscape = FindLandscape(LandscapeName);
	if (!Landscape)
	{
		return LandscapeError(TEXT("Landscape not found"));
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		return LandscapeError(TEXT("LandscapeInfo not available"));
	}

	const FVector Location = Landscape->GetActorLocation();
	const FVector Scale = Landscape->GetActorScale3D();

	const float LocalX = (WorldCenterX - Location.X) / Scale.X;
	const float LocalY = (WorldCenterY - Location.Y) / Scale.Y;
	const float LocalRadius = Radius / Scale.X;

	int32 MinX, MinY, MaxX, MaxY;
	if (!ComputeBrushBounds(Info, LocalX, LocalY, LocalRadius, MinX, MinY, MaxX, MaxY))
	{
		return LandscapeOk();
	}

	FScopedTransaction Transaction(NSLOCTEXT("OpenWorldMCP", "Noise", "Apply Noise"));

	const float ZScale = Scale.Z;
	const float LocalAmp = Amplitude / (LANDSCAPE_ZSCALE * ZScale);
	const float Freq = Frequency * Scale.X;

	return ApplyHeightOp(Landscape, Info, MinX, MinY, MaxX, MaxY,
		[&](float Current, int32 VertexX, int32 VertexY) -> float
		{
			const float Distance = FMath::Sqrt(FMath::Square(static_cast<float>(VertexX) - LocalX) + FMath::Square(static_cast<float>(VertexY) - LocalY));
			if (Distance > LocalRadius)
			{
				return Current;
			}
			const float Falloff = BrushFalloff(Distance, LocalRadius);
			const float Noise = Noise2D(static_cast<float>(VertexX) * Freq, static_cast<float>(VertexY) * Freq, Seed);
			return Current + LocalAmp * Noise * Falloff;
		});
}

FOpenWorldLandscapeResult UOpenWorldLandscapeService::CreateMountain(
	const FString& LandscapeName,
	float CenterX,
	float CenterY,
	float Radius,
	float Height,
	float Sharpness)
{
	ALandscape* Landscape = FindLandscape(LandscapeName);
	if (!Landscape)
	{
		return LandscapeError(TEXT("Landscape not found"));
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		return LandscapeError(TEXT("LandscapeInfo not available"));
	}

	const FVector Location = Landscape->GetActorLocation();
	const FVector Scale = Landscape->GetActorScale3D();

	const float LocalX = (CenterX - Location.X) / Scale.X;
	const float LocalY = (CenterY - Location.Y) / Scale.Y;
	const float LocalRadius = Radius / Scale.X;

	int32 MinX, MinY, MaxX, MaxY;
	if (!ComputeBrushBounds(Info, LocalX, LocalY, LocalRadius, MinX, MinY, MaxX, MaxY))
	{
		return LandscapeOk();
	}

	FScopedTransaction Transaction(NSLOCTEXT("OpenWorldMCP", "Mountain", "Create Mountain"));

	const float ZScale = Scale.Z;
	const float LocalHeight = Height / (LANDSCAPE_ZSCALE * ZScale);
	const float SharpnessClamped = FMath::Max(0.1f, Sharpness);

	return ApplyHeightOp(Landscape, Info, MinX, MinY, MaxX, MaxY,
		[&](float Current, int32 VertexX, int32 VertexY) -> float
		{
			const float Distance = FMath::Sqrt(FMath::Square(static_cast<float>(VertexX) - LocalX) + FMath::Square(static_cast<float>(VertexY) - LocalY));
			if (Distance > LocalRadius)
			{
				return Current;
			}
			const float T = Distance / LocalRadius;
			const float Profile = FMath::Pow(FMath::Clamp(1.f - T, 0.f, 1.f), SharpnessClamped);
			return Current + LocalHeight * Profile;
		});
}