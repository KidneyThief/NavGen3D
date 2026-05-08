// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DSettings.h"
#include "SNavGen3DWindow.h"
#include "Misc/CoreDelegates.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "LevelEditorViewport.h"
#include "Editor.h"

void UNavGen3DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FCoreDelegates::OnEndFrame.AddUObject(this, &UNavGen3DSubsystem::OnEndFrame);
}

void UNavGen3DSubsystem::Deinitialize()
{
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	Super::Deinitialize();
}

UWorld* UNavGen3DSubsystem::FindWorld() const
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.World())
		{
			return WorldContext.World();
		}
	}
	return nullptr;
}

FVector UNavGen3DSubsystem::GetCameraLocation()
{
	if (!IsPlayMode())
	{
		if (GEditor)
		{
			for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
			{
				if (LevelVC && LevelVC->IsPerspective())
				{
					return LevelVC->GetViewLocation();
				}
			}
		}
		return FVector(FLT_MAX);
	}

	if (UWorld* World = FindWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				return Pawn->GetActorLocation();
			}
		}
	}
	return FVector::ZeroVector;
}

bool UNavGen3DSubsystem::IsPlayMode()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		const UWorld* World = WorldContext.World();
		if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
		{
			return true;
		}
	}
	return false;
}

void UNavGen3DSubsystem::AddBoundsVolume(ANavGen3DBoundsVolume* Volume)
{
	BoundsVolumes.AddUnique(Volume);
}

void UNavGen3DSubsystem::RemoveBoundsVolume(ANavGen3DBoundsVolume* Volume)
{
	BoundsVolumes.Remove(Volume);
}

void UNavGen3DSubsystem::SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> Window)
{
	NavGen3DWindowPtr = Window;
}

void UNavGen3DSubsystem::AddLogMessage(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message)
{
	static const TMap<ENavGen3DLogCategory, FString> CategoryLabels = {
		{ ENavGen3DLogCategory::Info,    TEXT("Info")    },
		{ ENavGen3DLogCategory::Warning, TEXT("Warning") },
		{ ENavGen3DLogCategory::Error,   TEXT("Error")   }
	};
	const FString ResolvedName = ActorName.IsEmpty() ? TEXT("<unknown>") : ActorName;
	const FString FormattedMessage = FString::Printf(TEXT("[%s] %s: %s"), *CategoryLabels[Category], *ResolvedName, *Message);
	if (TSharedPtr<SNavGen3DWindow> Window = NavGen3DWindowPtr.Pin())
	{
		Window->AddLogEntry(Category, ResolvedName, FormattedMessage);
	}
}

uint64 UNavGen3DSubsystem::CalculateHash3D(const FVector& Vec) const
{
	const int32 GridSize = GetDefault<UNavGen3DSettings>()->MaxVolumeSize;

	auto ComponentHash = [GridSize](float Value) -> uint64
	{
		return (uint64)(FMath::FloorToInt(Value / GridSize) & 0xFFFF);
	};

	return (ComponentHash(Vec.X) << 32) | (ComponentHash(Vec.Y) << 16) | ComponentHash(Vec.Z);
}

bool UNavGen3DSubsystem::AddNavMeshVolume(NavMeshVolume& Volume)
{
	const UNavGen3DSettings* Settings = GetDefault<UNavGen3DSettings>();

	if (NavMeshSolutionMap.Num() >= Settings->MaxVolumeCount)
	{
		return false;
	}

	const FVector Size = Volume.Bounds.GetSize();

	float MaxSize = Size.X;
	EAxis::Type LongestAxis = EAxis::X;
	if (Size.Y > MaxSize) { MaxSize = Size.Y; LongestAxis = EAxis::Y; }
	if (Size.Z > MaxSize) { MaxSize = Size.Z; LongestAxis = EAxis::Z; }

	if (MaxSize > Settings->MaxVolumeSize)
	{
		const float MidValue = (Volume.Bounds.Min.GetComponentForAxis(LongestAxis) + Volume.Bounds.Max.GetComponentForAxis(LongestAxis)) * 0.5f;

		NavMeshVolume Half1;
		Half1.Bounds.Min = Volume.Bounds.Min;
		Half1.Bounds.Max = Volume.Bounds.Max;

		NavMeshVolume Half2;
		Half2.Bounds.Min = Volume.Bounds.Min;
		Half2.Bounds.Max = Volume.Bounds.Max;

		switch (LongestAxis)
		{
			case EAxis::X: Half1.Bounds.Max.X = MidValue; Half2.Bounds.Min.X = MidValue; break;
			case EAxis::Y: Half1.Bounds.Max.Y = MidValue; Half2.Bounds.Min.Y = MidValue; break;
			case EAxis::Z: Half1.Bounds.Max.Z = MidValue; Half2.Bounds.Min.Z = MidValue; break;
			default: break;
		}

		return AddNavMeshVolume(Half1) && AddNavMeshVolume(Half2);
	}

	Volume.ID = ++NavMeshVolumeIDGenerator;
	NavMeshSolutionMap.Add(Volume.ID, Volume);

	const uint64 LocationHash = CalculateHash3D(Volume.Bounds.GetCenter());
	NavMeshSolutionMapByLocation.FindOrAdd(LocationHash).Add(Volume.ID);

	if (DebugDrawTime > 0.0f)
	{
		if (UWorld* World = FindWorld())
		{
			DrawDebugBox(World, Volume.Bounds.GetCenter(), Volume.Bounds.GetExtent(), FColor::Green, false, DebugDrawTime, 0, 5.0f);
		}
	}

	return true;
}

bool UNavGen3DSubsystem::ProcessNavMeshVolume(NavMeshVolume& Volume)
{
	UWorld* World = FindWorld();

	FVector ImpactPoint;
	const FVector BoundsCenter = (Volume.Bounds.Min + Volume.Bounds.Max) * 0.5f;

	float GreatestDistance = -1.0f;
	FVector FarthestTraceStart = FVector::ZeroVector;
	EAxis::Type FarthestAxis = EAxis::None;
	bool bFarthestFromMin = false;
	FVector FarthestImpactPoint = FVector::ZeroVector;

	auto CheckTrace = [&](EAxis::Type Axis, bool bFromMin)
	{
		FVector TraceStart = BoundsCenter;
		switch (Axis)
		{
			case EAxis::X: TraceStart.X = bFromMin ? Volume.Bounds.Min.X : Volume.Bounds.Max.X; break;
			case EAxis::Y: TraceStart.Y = bFromMin ? Volume.Bounds.Min.Y : Volume.Bounds.Max.Y; break;
			case EAxis::Z: TraceStart.Z = bFromMin ? Volume.Bounds.Min.Z : Volume.Bounds.Max.Z; break;
			default: break;
		}

		FVector TraceFirst  = bFromMin ? Volume.Bounds.Min : Volume.Bounds.Max;
		FVector TraceSecond = bFromMin ? Volume.Bounds.Max : Volume.Bounds.Min;

		if (PlaneTrace(TraceFirst, TraceSecond, Axis, ImpactPoint))
		{
			float Distance = FVector::Dist(ImpactPoint, TraceStart);
			if (Distance > GreatestDistance)
			{
				GreatestDistance = Distance;
				FarthestTraceStart = TraceStart;
				FarthestAxis = Axis;
				bFarthestFromMin = bFromMin;
				FarthestImpactPoint = ImpactPoint;
			}
		}
	};

	CheckTrace(EAxis::X, true);
	CheckTrace(EAxis::Y, true);
	CheckTrace(EAxis::Z, true);
	CheckTrace(EAxis::X, false);
	CheckTrace(EAxis::Y, false);
	CheckTrace(EAxis::Z, false);

	if (FarthestAxis != EAxis::None)
	{
		const float SplitValue = FarthestImpactPoint.GetComponentForAxis(FarthestAxis);

		NavMeshVolume NavVolumeFound;
		NavVolumeFound.Bounds.Min = Volume.Bounds.Min;
		NavVolumeFound.Bounds.Max = Volume.Bounds.Max;
		NavVolumeFound.ParentBoundsVolume = Volume.ParentBoundsVolume;

		NavMeshVolume NavVolumeRemainder;
		NavVolumeRemainder.Bounds.Min = Volume.Bounds.Min;
		NavVolumeRemainder.Bounds.Max = Volume.Bounds.Max;
		NavVolumeRemainder.ParentBoundsVolume = Volume.ParentBoundsVolume;

		switch (FarthestAxis)
		{
			case EAxis::X:
				NavVolumeFound.Bounds.Min.X = SplitValue;
				NavVolumeRemainder.Bounds.Max.X = SplitValue;
				break;
			case EAxis::Y:
				NavVolumeFound.Bounds.Min.Y = SplitValue;
				NavVolumeRemainder.Bounds.Max.Y = SplitValue;
				break;
			case EAxis::Z:
				NavVolumeFound.Bounds.Min.Z = SplitValue;
				NavVolumeRemainder.Bounds.Max.Z = SplitValue;
				break;
			default:
				break;
		}

		AddNavMeshVolume(NavVolumeFound);
		ProcessVolumesList.Add(NavVolumeRemainder);

		if (World && DebugDrawTime > 0.0f)
		{
			DrawDebugSphere(World, FarthestImpactPoint, 10.0f, 8, FColor::Orange, false, DebugDrawTime);
			DrawDebugBox(World, NavVolumeRemainder.Bounds.GetCenter(), NavVolumeRemainder.Bounds.GetExtent(), FColor::Red, false, DebugDrawTime, 0, 5.0f);
		}
	}

	return false;
}

NavMeshVolume* UNavGen3DSubsystem::FindVolumeContainingLocation(const FVector& Location)
{
	const uint64 LocationHash = CalculateHash3D(Location);
	if (const TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(LocationHash))
	{
		for (uint64 VolumeID : *VolumeIDs)
		{
			if (NavMeshVolume* Volume = NavMeshSolutionMap.Find(VolumeID))
			{
				if (Volume->Bounds.IsInsideOrOn(Location))
				{
					return Volume;
				}
			}
		}
	}
	return nullptr;
}

TOptional<NavMeshVolume> UNavGen3DSubsystem::FindGenerationVolumeContainingLocation(const FVector& Location, bool bRemoveFromProcessing)
{
	for (int32 i = 0; i < ProcessVolumesList.Num(); ++i)
	{
		if (ProcessVolumesList[i].Bounds.IsInsideOrOn(Location))
		{
			NavMeshVolume Result = ProcessVolumesList[i];
			if (bRemoveFromProcessing)
			{
				ProcessVolumesList.RemoveAtSwap(i);
			}
			return Result;
		}
	}

	if (ProcessVolumesList.IsEmpty())
	{
		for (ANavGen3DBoundsVolume* BoundsVolume : GetBoundsVolumes())
		{
			if (!BoundsVolume || !BoundsVolume->Enabled || BoundsVolume->Embedded)
			{
				continue;
			}

			FVector Origin;
			FVector Extent;
			BoundsVolume->GetActorBounds(false, Origin, Extent);
			if (FBox::BuildAABB(Origin, Extent).IsInsideOrOn(Location))
			{
				NavMeshVolume Result;
				Result.Bounds = FBox::BuildAABB(Origin, Extent);
				Result.ParentBoundsVolume = BoundsVolume;
				return Result;
			}
		}
	}

	return TOptional<NavMeshVolume>();
}

void UNavGen3DSubsystem::RemoveNavMeshVolume(uint64 ID)
{
	if (NavMeshVolume* Volume = NavMeshSolutionMap.Find(ID))
	{
		const uint64 LocationHash = CalculateHash3D(Volume->Bounds.GetCenter());
		if (TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(LocationHash))
		{
			VolumeIDs->Remove(ID);
			if (VolumeIDs->IsEmpty())
			{
				NavMeshSolutionMapByLocation.Remove(LocationHash);
			}
		}
	}
	NavMeshSolutionMap.Remove(ID);
}

bool UNavGen3DSubsystem::PlaneTrace(FVector Min, FVector Max, EAxis::Type Axis, FVector& OutImpactPoint)
{
	FVector Center = (Min + Max) * 0.5f;
	FVector HalfExtent = (Max - Min) * 0.5f;
	FVector Start = Center;
	FVector End = Center;

	switch (Axis)
	{
		case EAxis::X:
			HalfExtent.X = 0.0f;
			Start.X = Min.X;
			End.X = Max.X;
			break;
		case EAxis::Y:
			HalfExtent.Y = 0.0f;
			Start.Y = Min.Y;
			End.Y = Max.Y;
			break;
		case EAxis::Z:
			HalfExtent.Z = 0.0f;
			Start.Z = Min.Z;
			End.Z = Max.Z;
			break;
		default:
			return false;
	}

	HalfExtent = HalfExtent.GetAbs();

	UWorld* World = FindWorld();

	if (!World)
	{
		return false;
	}

	FHitResult HitResult;
	FCollisionShape BoxShape = FCollisionShape::MakeBox(HalfExtent);
	if (World->SweepSingleByChannel(HitResult, Start, End, FQuat::Identity, ECC_WorldStatic, BoxShape))
	{
		if (HitResult.bBlockingHit && !HitResult.bStartPenetrating)
		{
			OutImpactPoint = HitResult.ImpactPoint;
			return true;
		}
	}

	return false;
}

bool UNavGen3DSubsystem::GenerateNavMesh3D(NavMeshVolume* Volume)
{
	if (Volume)
	{
		return ProcessNavMeshVolume(*Volume);
	}

	while (!ProcessVolumesList.IsEmpty())
	{
		NavMeshVolume CurrentVolume = ProcessVolumesList[0];
		ProcessVolumesList.RemoveAtSwap(0);
		ProcessNavMeshVolume(CurrentVolume);
	}

	return false;
}

void UNavGen3DSubsystem::InitializeNavMesh3D()
{
	NavMeshSolutionMap.Reset();
	NavMeshSolutionMapByLocation.Reset();
	ProcessVolumesList.Reset();
	NavMeshVolumeIDGenerator = 0;

	ValidateEmbeddedBoundsVolumes();

	AddLogMessage(ENavGen3DLogCategory::Info, TEXT(""), TEXT("NavMesh3D initialized"));
}

bool UNavGen3DSubsystem::GenerateNavMesh3DFromBoundsVolume(ANavGen3DBoundsVolume* Volume)
{
	ProcessVolumesList.Reset();

	const FString ActorName = Volume ? Volume->GetActorLabel() : TEXT("<unknown>");
	AddLogMessage(ENavGen3DLogCategory::Info, ActorName, TEXT("generating NavMesh3D..."));

	bool bSuccess = Volume != nullptr;
	FString Result = bSuccess ? TEXT("success") : TEXT("fail");
	if (IsPlayMode())
	{
		bSuccess = false;
		Result = TEXT("Generation only available from Editor");
	}

	ValidateEmbeddedBoundsVolumes();

	if (bSuccess && Volume->Embedded)
	{
		bSuccess = false;
		Result = TEXT("Embedded volume invalid for generation.  Generate from the encompassing bounds volume.");
	}

	if (bSuccess)
	{
		FVector Origin;
		FVector Extent;
		Volume->GetActorBounds(false, Origin, Extent);

		NavMeshVolume NavVolume;
		NavVolume.Bounds = FBox::BuildAABB(Origin, Extent);
		NavVolume.ParentBoundsVolume = Volume;
		ProcessVolumesList.Add(NavVolume);

		bSuccess = GenerateNavMesh3D(nullptr);
		if (!bSuccess)
		{
			Result = TEXT("fail");
		}
	}

	AddLogMessage(ENavGen3DLogCategory::Info, ActorName, FString::Printf(TEXT("generation complete: %s"), *Result));
	return bSuccess;
}

bool UNavGen3DSubsystem::ValidateNavMesh3D(NavMeshVolume* Volume)
{
	if (!Volume)
	{
		bool bAllValid = true;
		for (auto& Pair : NavMeshSolutionMap)
		{
			if (!ValidateNavMesh3D(&Pair.Value))
			{
				bAllValid = false;
			}
		}
		return bAllValid;
	}

	FVector ImpactPoint;
	if (PlaneTrace(Volume->Bounds.Min, Volume->Bounds.Max, EAxis::X, ImpactPoint) ||
		PlaneTrace(Volume->Bounds.Min, Volume->Bounds.Max, EAxis::Y, ImpactPoint) ||
		PlaneTrace(Volume->Bounds.Min, Volume->Bounds.Max, EAxis::Z, ImpactPoint))
	{
		const FString ActorName = Volume->ParentBoundsVolume.IsValid()
			? Volume->ParentBoundsVolume->GetActorLabel()
			: TEXT("<unknown>");
		AddLogMessage(ENavGen3DLogCategory::Warning, ActorName,
			FString::Printf(TEXT("Validation failed for volume ID %llu"), Volume->ID));
		return false;
	}

	return true;
}

void UNavGen3DSubsystem::ValidateEmbeddedBoundsVolumes()
{
	if (IsPlayMode())
	{
		return;
	}

	TArray<TObjectPtr<ANavGen3DBoundsVolume>> Volumes = GetBoundsVolumes();

	for (ANavGen3DBoundsVolume* Volume : Volumes)
	{
		if (!Volume || !Volume->Enabled)
		{
			continue;
		}

		Volume->Embedded = false;
		const FBox VolumeBox = Volume->GetComponentsBoundingBox();

		for (ANavGen3DBoundsVolume* OtherVolume : Volumes)
		{
			if (!OtherVolume || !OtherVolume->Enabled || OtherVolume == Volume)
			{
				continue;
			}

			const FBox OtherBox = OtherVolume->GetComponentsBoundingBox();
			const bool bIsContained = OtherBox.IsInside(VolumeBox);
			const bool bOverlapsAndSmaller = VolumeBox.Intersect(OtherBox) && VolumeBox.GetVolume() < OtherBox.GetVolume();

			if (bIsContained || bOverlapsAndSmaller)
			{
				Volume->Embedded = true;
				break;
			}
		}
	}
}

TArray<TObjectPtr<ANavGen3DBoundsVolume>> UNavGen3DSubsystem::GetBoundsVolumes()
{
	if (IsPlayMode())
	{
		return BoundsVolumes;
	}

	TArray<TObjectPtr<ANavGen3DBoundsVolume>> Result;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!World)
		{
			continue;
		}

		for (TActorIterator<ANavGen3DBoundsVolume> It(World); It; ++It)
		{
			Result.Add(*It);
		}
	}
	return Result;
}

void UNavGen3DSubsystem::OnEndFrame()
{
	switch (DebugDrawMode)
	{
		case ENavGen3DDrawMode::NavBounds3D:
		{
			UWorld* World = FindWorld();
			if (!World)
			{
				break;
			}

			for (const TObjectPtr<ANavGen3DBoundsVolume>& Volume : BoundsVolumes)
			{
				if (!Volume)
				{
					continue;
				}

				FColor DrawColor;
				if (!Volume->Enabled)
				{
					DrawColor = FColor::Red;
				}
				else if (!Volume->Embedded)
				{
					DrawColor = FColor(128, 0, 128);
				}
				else
				{
					DrawColor = FColor::Cyan;
				}

				FVector Origin;
				FVector Extent;
				Volume->GetActorBounds(false, Origin, Extent);
				DrawDebugBox(World, Origin, Extent, Volume->GetActorRotation().Quaternion(), DrawColor, false, -1.0f, 0, 5.0f);
			}
			break;
		}

		case ENavGen3DDrawMode::NavMesh3D:
		{
			if (UWorld* World = FindWorld())
			{
				for (const auto& Pair : NavMeshSolutionMap)
				{
					const NavMeshVolume& Volume = Pair.Value;
					DrawDebugBox(World, Volume.Bounds.GetCenter(), Volume.Bounds.GetExtent(), FColor::Cyan, false, -1.0f, 0, 3.0f);
				}
			}
			break;
		}

		case ENavGen3DDrawMode::UnprocessedVolumes:
		{
			if (UWorld* World = FindWorld())
			{
				for (const NavMeshVolume& Volume : ProcessVolumesList)
				{
					DrawDebugBox(World, Volume.Bounds.GetCenter(), Volume.Bounds.GetExtent(), FColor::Yellow, false, -1.0f, 0, 3.0f);
				}
			}
			break;
		}

		default:
			break;
	}

	if (DrawCameraVolume)
	{
		if (UWorld* World = FindWorld())
		{
			const FVector CameraLocation = GetCameraLocation();

			if (NavMeshVolume* Volume = FindVolumeContainingLocation(CameraLocation))
			{
				DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::Green, false, -1.0f, 0, 5.0f);
			}
			else
			{
				TOptional<NavMeshVolume> GenVolume = FindGenerationVolumeContainingLocation(CameraLocation, false);
				if (GenVolume.IsSet())
				{
					DrawDebugBox(World, GenVolume->Bounds.GetCenter(), GenVolume->Bounds.GetExtent(), FColor::Orange, false, -1.0f, 0, 5.0f);
				}
			}
		}
	}
}
