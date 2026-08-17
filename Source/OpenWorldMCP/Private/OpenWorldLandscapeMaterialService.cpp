#include "OpenWorldLandscapeMaterialService.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeLayerInfoObject.h"
#include "UObject/SavePackage.h"
#include "LandscapeDataAccess.h"
#include "Math/UnrealMathUtility.h"

static FOpenWorldMaterialResult MaterialError(const FString& Message)
{
	FOpenWorldMaterialResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = Message;
	return Result;
}

static FOpenWorldMaterialResult MaterialOk(const FString& AssetPath = TEXT(""))
{
	FOpenWorldMaterialResult Result;
	Result.bSuccess = true;
	Result.AssetPath = AssetPath;
	return Result;
}

namespace
{
	/** The active edit-layer GUID that layer edits are written to. */
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

	/** Pushes pending weightmap updates so the painted weights become visible. */
	void FlushWeightmapEdits(ALandscape* Landscape)
	{
		if (!Landscape)
		{
			return;
		}

		if (ALandscape* MainActor = Landscape->GetLandscapeActor())
		{
			MainActor->ForceUpdateLayersContent();
		}

		Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All);
		Landscape->PostEditChange();
	}
}

ALandscape* UOpenWorldLandscapeMaterialService::FindLandscape(const FString& LandscapeName)
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

UMaterial* UOpenWorldLandscapeMaterialService::CreateMaterialWithBlend(
	const FString& MaterialName,
	const FString& FolderPath,
	const TArray<FString>& LayerNames,
	const TArray<FLinearColor>& LayerColors)
{
	const FString PackagePath = FolderPath / MaterialName;

	// Create the material package and asset via the factory (editor only).
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return nullptr;
	}

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = static_cast<UMaterial*>(
		Factory->FactoryCreateNew(
			UMaterial::StaticClass(),
			Package,
			FName(*MaterialName),
			RF_Standalone | RF_Public,
			nullptr,
			GWarn));

	if (!Material)
	{
		return nullptr;
	}

	// LandscapeLayerBlend node
	UMaterialExpressionLandscapeLayerBlend* BlendNode =
		NewObject<UMaterialExpressionLandscapeLayerBlend>(Material);
	Material->GetExpressionCollection().AddExpression(BlendNode);

	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		FLayerBlendInput BlendInput;
		BlendInput.LayerName = FName(*LayerNames[i]);
		BlendInput.BlendType = ELandscapeLayerBlendType::LB_WeightBlend;
		BlendInput.PreviewWeight = 1.0f;

		const FLinearColor Color = (i < LayerColors.Num()) ? LayerColors[i] : FLinearColor(0.5f, 0.5f, 0.5f, 1.f);

		UMaterialExpressionConstant4Vector* ColorNode = NewObject<UMaterialExpressionConstant4Vector>(Material);
		ColorNode->Constant = Color;
		ColorNode->MaterialExpressionEditorX = -400;
		ColorNode->MaterialExpressionEditorY = i * 100;
		Material->GetExpressionCollection().AddExpression(ColorNode);

		BlendInput.LayerInput.Expression = ColorNode;

		BlendNode->Layers.Add(BlendInput);
	}

	// Connect the blend node to the base color
	BlendNode->MaterialExpressionEditorX = -200;
	BlendNode->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(BlendNode);

	// Connect blend node output to the material's base color
	Material->GetEditorOnlyData()->BaseColor.Expression = BlendNode;

	// Basic PBR defaults
	Material->SetScalarParameterValueEditorOnly(TEXT("Metallic"), 0.0f);
	Material->SetScalarParameterValueEditorOnly(TEXT("Roughness"), 0.9f);

	Material->PostEditChange();
	Package->MarkPackageDirty();

	return Material;
}

FOpenWorldMaterialResult UOpenWorldLandscapeMaterialService::CreateLandscapeMaterial(
	const FString& MaterialName,
	const FString& FolderPath,
	const TArray<FString>& LayerNames,
	const TArray<FLinearColor>& LayerColors)
{
	if (MaterialName.IsEmpty() || LayerNames.Num() == 0)
	{
		return MaterialError(TEXT("MaterialName empty or no layers specified"));
	}

	UMaterial* Material = CreateMaterialWithBlend(MaterialName, FolderPath, LayerNames, LayerColors);
	if (!Material)
	{
		return MaterialError(TEXT("Material creation failed"));
	}

	const FString Path = FolderPath / MaterialName;
	UPackage* Package = Material->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone | RF_Public;
		UPackage::SavePackage(Package, Material, *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), SaveArgs);
	}

	return MaterialOk(Path);
}

FOpenWorldMaterialResult UOpenWorldLandscapeMaterialService::CreateLayerInfo(
	const FString& LayerName,
	const FString& FolderPath)
{
	if (LayerName.IsEmpty())
	{
		return MaterialError(TEXT("LayerName empty"));
	}

	const FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	const FString PackagePath = FolderPath / AssetName;

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return MaterialError(TEXT("Package creation failed"));
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, FName(*AssetName), RF_Standalone | RF_Public);

	if (!LayerInfo)
	{
		return MaterialError(TEXT("LayerInfo creation failed"));
	}

	LayerInfo->SetLayerName(FName(*LayerName), true);
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone | RF_Public;
		UPackage::SavePackage(Package, LayerInfo, *FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension()), SaveArgs);

	return MaterialOk(PackagePath);
}

FOpenWorldMaterialResult UOpenWorldLandscapeMaterialService::AssignMaterialToLandscape(
	const FString& LandscapeName,
	const FString& MaterialPath,
	const TArray<FString>& LayerPaths)
{
	ALandscape* Landscape = FindLandscape(LandscapeName);
	if (!Landscape)
	{
		return MaterialError(TEXT("Landscape not found"));
	}

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		return MaterialError(TEXT("Material not found: ") + MaterialPath);
	}

	// Assign material
	Landscape->LandscapeMaterial = Material;

	// Add layers (5.8 edit-layer system)
	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (Info)
	{
		for (const FString& LayerPath : LayerPaths)
		{
			ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *LayerPath);
			if (LayerInfo)
			{
				Info->CreateTargetLayerSettingsFor(LayerInfo);
			}
		}
		Info->UpdateLayerInfoMap(Landscape, false);
	}

	Landscape->PostEditChange();
	Landscape->MarkPackageDirty();

	return MaterialOk(MaterialPath);
}

FOpenWorldMaterialResult UOpenWorldLandscapeMaterialService::PaintLayerRect(
	const FString& LandscapeName,
	const FString& LayerPath,
	float WorldMinX,
	float WorldMinY,
	float WorldMaxX,
	float WorldMaxY,
	float Weight)
{
	ALandscape* Landscape = FindLandscape(LandscapeName);
	if (!Landscape)
	{
		return MaterialError(TEXT("Landscape not found"));
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		return MaterialError(TEXT("LandscapeInfo not available"));
	}

	ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *LayerPath);
	if (!LayerInfo)
	{
		return MaterialError(TEXT("LayerInfo not found: ") + LayerPath);
	}

	const FVector Origin = Landscape->GetActorLocation();
	const FVector Scale = Landscape->GetActorScale3D();

	int32 LandMinX, LandMinY, LandMaxX, LandMaxY;
	if (!Info->GetLandscapeExtent(LandMinX, LandMinY, LandMaxX, LandMaxY))
	{
		return MaterialError(TEXT("Could not resolve landscape extent"));
	}

	const int32 X1 = FMath::Clamp(FMath::FloorToInt((WorldMinX - Origin.X) / FMath::Max(Scale.X, 0.01f)), LandMinX, LandMaxX);
	const int32 Y1 = FMath::Clamp(FMath::FloorToInt((WorldMinY - Origin.Y) / FMath::Max(Scale.Y, 0.01f)), LandMinY, LandMaxY);
	const int32 X2 = FMath::Clamp(FMath::CeilToInt((WorldMaxX - Origin.X) / FMath::Max(Scale.X, 0.01f)), LandMinX, LandMaxX);
	const int32 Y2 = FMath::Clamp(FMath::CeilToInt((WorldMaxY - Origin.Y) / FMath::Max(Scale.Y, 0.01f)), LandMinY, LandMaxY);

	if (X1 > X2 || Y1 > Y2)
	{
		return MaterialError(TEXT("Invalid paint region"));
	}

	const int32 Width = X2 - X1 + 1;
	const int32 Height = Y2 - Y1 + 1;
	const int32 Stride = Width;

	TArray<uint8> Data;
	Data.SetNumUninitialized(Width * Height);
	const uint8 WeightValue = static_cast<uint8>(FMath::Clamp(Weight, 0.f, 1.f) * 255.f);
	FMemory::Memset(Data.GetData(), WeightValue, Data.Num());

	const FGuid LayerGuid = ResolveTargetLayer(Landscape);

	FScopedSetLandscapeEditingLayer EditScope(
		Landscape,
		LayerGuid,
		[Landscape]()
		{
			if (Landscape)
			{
				Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All);
			}
		});

	TAlphamapAccessor<false> Accessor(Info, LayerInfo);
	Accessor.SetData(X1, Y1, X2, Y2, Data.GetData(), ELandscapeLayerPaintingRestriction::None);
	Accessor.Flush();

	FlushWeightmapEdits(Landscape);

	Landscape->MarkPackageDirty();
	return MaterialOk(LayerPath);
}
