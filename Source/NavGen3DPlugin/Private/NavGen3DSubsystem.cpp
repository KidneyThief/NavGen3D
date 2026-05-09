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

void UNavGen3DSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
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

void UNavGen3DSubsystem::AddBoundsVolume(ANavGen3DBoundsVolume* InVolume)
{
	BoundsVolumes.AddUnique(InVolume);
}

void UNavGen3DSubsystem::RemoveBoundsVolume(ANavGen3DBoundsVolume* InVolume)
{
	BoundsVolumes.Remove(InVolume);
}

void UNavGen3DSubsystem::SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> InWindow)
{
	NavGen3DWindowPtr = InWindow;
}

void UNavGen3DSubsystem::AddLogMessage(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage)
{
	static const TMap<ENavGen3DLogCategory, FString> CategoryLabels = {
		{ ENavGen3DLogCategory::Info,    TEXT("Info")    },
		{ ENavGen3DLogCategory::Warning, TEXT("Warning") },
		{ ENavGen3DLogCategory::Error,   TEXT("Error")   }
	};
	const FString ResolvedName = InActorName.IsEmpty() ? TEXT("<unknown>") : InActorName;
	const FString FormattedMessage = FString::Printf(TEXT("[%s] %s: %s"), *CategoryLabels[InCategory], *ResolvedName, *InMessage);
	if (TSharedPtr<SNavGen3DWindow> Window = NavGen3DWindowPtr.Pin())
	{
		Window->AddLogEntry(InCategory, ResolvedName, FormattedMessage);
	}
}

uint64 UNavGen3DSubsystem::CalculateHash3D(const FVector& InVec) const
{
	const int32 GridSize = GetDefault<UNavGen3DSettings>()->MaxVolumeSize;

	auto ComponentHash = [GridSize](float Value) -> uint64
	{
		return (uint64)(FMath::FloorToInt(Value / GridSize) & 0xFFFF);
	};

	return (ComponentHash(InVec.X) << 32) | (ComponentHash(InVec.Y) << 16) | ComponentHash(InVec.Z);
}

bool UNavGen3DSubsystem::AddNavMeshVolume(NavMeshVolume& RefVolume)
{
	const FVector Size = RefVolume.Bounds.GetSize();
	if (Size.X < Epsilon || Size.Y < Epsilon || Size.Z < Epsilon)
	{
		return false;
	}

	const UNavGen3DSettings* Settings = GetDefault<UNavGen3DSettings>();

	if (NavMeshSolutionMap.Num() >= Settings->MaxVolumeCount)
	{
		return false;
	}

	float MaxSize = Size.X;
	EAxis::Type LongestAxis = EAxis::X;
	if (Size.Y > MaxSize) { MaxSize = Size.Y; LongestAxis = EAxis::Y; }
	if (Size.Z > MaxSize) { MaxSize = Size.Z; LongestAxis = EAxis::Z; }

	if (MaxSize > Settings->MaxVolumeSize)
	{
		const float MidValue = (RefVolume.Bounds.Min.GetComponentForAxis(LongestAxis) + RefVolume.Bounds.Max.GetComponentForAxis(LongestAxis)) * 0.5f;

		NavMeshVolume Half1;
		Half1.Bounds.Min = RefVolume.Bounds.Min;
		Half1.Bounds.Max = RefVolume.Bounds.Max;

		NavMeshVolume Half2;
		Half2.Bounds.Min = RefVolume.Bounds.Min;
		Half2.Bounds.Max = RefVolume.Bounds.Max;

		switch (LongestAxis)
		{
			case EAxis::X: Half1.Bounds.Max.X = MidValue; Half2.Bounds.Min.X = MidValue; break;
			case EAxis::Y: Half1.Bounds.Max.Y = MidValue; Half2.Bounds.Min.Y = MidValue; break;
			case EAxis::Z: Half1.Bounds.Max.Z = MidValue; Half2.Bounds.Min.Z = MidValue; break;
			default: break;
		}

		return AddNavMeshVolume(Half1) && AddNavMeshVolume(Half2);
	}

	RefVolume.ID = ++NavMeshVolumeIDGenerator;
	NavMeshSolutionMap.Add(RefVolume.ID, RefVolume);

	const uint64 LocationHash = CalculateHash3D(RefVolume.Bounds.GetCenter());
	NavMeshSolutionMapByLocation.FindOrAdd(LocationHash).Add(RefVolume.ID);

	if (DebugDrawTime > 0.0f)
	{
		if (UWorld* World = FindWorld())
		{
			DrawDebugBox(World, RefVolume.Bounds.GetCenter(), RefVolume.Bounds.GetExtent(), FColor::Green, false, DebugDrawTime, 0, 5.0f);
		}
	}

	return true;
}

bool UNavGen3DSubsystem::ProcessNavMeshVolume(NavMeshVolume& RefVolume, bool InDrawDebug)
{
	{
		const FVector Size = RefVolume.Bounds.GetSize();
		if (Size.X < Epsilon || Size.Y < Epsilon || Size.Z < Epsilon)
		{
			return false;
		}
	}

	UWorld* World = FindWorld();
	const FString ActorName = RefVolume.ParentBoundsVolume.IsValid() ? RefVolume.ParentBoundsVolume->GetActorLabel() : TEXT("<unknown>");

	FVector ImpactPoint;
	const FVector BoundsCenter = (RefVolume.Bounds.Min + RefVolume.Bounds.Max) * 0.5f;

	float GreatestDistance = -1.0f;
	EAxis::Type FarthestAxis = EAxis::None;
	bool bFarthestFromMin = false;
	FVector FarthestImpactPoint = FVector::ZeroVector;
	int32 StartPenetratingCount = 0;

	auto CheckTrace = [&](EAxis::Type Axis, bool bFromMin)
	{
		FVector TraceStart = BoundsCenter;
		switch (Axis)
		{
			case EAxis::X: TraceStart.X = bFromMin ? RefVolume.Bounds.Min.X : RefVolume.Bounds.Max.X; break;
			case EAxis::Y: TraceStart.Y = bFromMin ? RefVolume.Bounds.Min.Y : RefVolume.Bounds.Max.Y; break;
			case EAxis::Z: TraceStart.Z = bFromMin ? RefVolume.Bounds.Min.Z : RefVolume.Bounds.Max.Z; break;
			default: break;
		}

		FVector TraceFirst  = bFromMin ? RefVolume.Bounds.Min : RefVolume.Bounds.Max;
		FVector TraceSecond = bFromMin ? RefVolume.Bounds.Max : RefVolume.Bounds.Min;

		bool bStartPenetrating = false;
		if (PlaneTrace(TraceFirst, TraceSecond, Axis, ImpactPoint, bStartPenetrating))
		{
			const float Distance = FVector::Dist(ImpactPoint, TraceStart);
			if (Distance > GreatestDistance)
			{
				GreatestDistance = Distance;
				FarthestAxis = Axis;
				bFarthestFromMin = bFromMin;
				FarthestImpactPoint = ImpactPoint;
			}
		}

		if (bStartPenetrating)
		{
			++StartPenetratingCount;
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
		NavVolumeFound.Bounds.Min = RefVolume.Bounds.Min;
		NavVolumeFound.Bounds.Max = RefVolume.Bounds.Max;
		NavVolumeFound.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		NavMeshVolume NavVolumeRemainder;
		NavVolumeRemainder.Bounds.Min = RefVolume.Bounds.Min;
		NavVolumeRemainder.Bounds.Max = RefVolume.Bounds.Max;
		NavVolumeRemainder.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		// Clear space lies between the starting plane and the impact point.
		// Split so NavVolumeFound takes that clear slice and NavVolumeRemainder takes what's left.
		if (bFarthestFromMin)
		{
			// Traced from Min side: clear space is Min → SplitValue
			switch (FarthestAxis)
			{
				case EAxis::X: NavVolumeFound.Bounds.Max.X = SplitValue; NavVolumeRemainder.Bounds.Min.X = SplitValue; break;
				case EAxis::Y: NavVolumeFound.Bounds.Max.Y = SplitValue; NavVolumeRemainder.Bounds.Min.Y = SplitValue; break;
				case EAxis::Z: NavVolumeFound.Bounds.Max.Z = SplitValue; NavVolumeRemainder.Bounds.Min.Z = SplitValue; break;
				default: break;
			}
		}
		else
		{
			// Traced from Max side: clear space is SplitValue → Max
			switch (FarthestAxis)
			{
				case EAxis::X: NavVolumeFound.Bounds.Min.X = SplitValue; NavVolumeRemainder.Bounds.Max.X = SplitValue; break;
				case EAxis::Y: NavVolumeFound.Bounds.Min.Y = SplitValue; NavVolumeRemainder.Bounds.Max.Y = SplitValue; break;
				case EAxis::Z: NavVolumeFound.Bounds.Min.Z = SplitValue; NavVolumeRemainder.Bounds.Max.Z = SplitValue; break;
				default: break;
			}
		}

		AddNavMeshVolume(NavVolumeFound);

		if (InDrawDebug)
		{
			AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
				FString::Printf(TEXT("Added solution volume ID:%llu Extent:%s"), NavVolumeFound.ID, *NavVolumeFound.Bounds.GetExtent().ToString()));
			if (World)
			{
				DrawDebugSphere(World, FarthestImpactPoint, 10.0f, 8, FColor::Orange, false, DebugDrawTime);
			}
		}

		const float MinVolumeSize = RefVolume.ParentBoundsVolume.IsValid() ? RefVolume.ParentBoundsVolume->MinVolumeSize : 0.0f;
		const FVector RemainderSize = NavVolumeRemainder.Bounds.GetSize();
		const float OriginalAxisSize = RefVolume.Bounds.GetSize().GetComponentForAxis(FarthestAxis);
		const float RemainderAxisSize = RemainderSize.GetComponentForAxis(FarthestAxis);
		const bool bMadeProgress = RemainderAxisSize < OriginalAxisSize;
		if (bMadeProgress && FMath::Max3(RemainderSize.X, RemainderSize.Y, RemainderSize.Z) >= MinVolumeSize)
		{
			ProcessVolumesList.Add(NavVolumeRemainder);
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("Queued remainder volume Extent:%s"), *NavVolumeRemainder.Bounds.GetExtent().ToString()));
				if (World)
				{
					DrawDebugBox(World, NavVolumeRemainder.Bounds.GetCenter(), NavVolumeRemainder.Bounds.GetExtent(), FColor::Red, false, DebugDrawTime, 0, 5.0f);
				}
			}
		}
	}
	else if (StartPenetratingCount == 6)
	{
		// All traces started penetrating: volume is fully inside geometry, split along longest axis
		const FVector Size = RefVolume.Bounds.GetSize();
		EAxis::Type LongestAxis = EAxis::X;
		float MaxDim = Size.X;
		if (Size.Y > MaxDim) { MaxDim = Size.Y; LongestAxis = EAxis::Y; }
		if (Size.Z > MaxDim) { MaxDim = Size.Z; LongestAxis = EAxis::Z; }

		const float MidValue = (RefVolume.Bounds.Min.GetComponentForAxis(LongestAxis) + RefVolume.Bounds.Max.GetComponentForAxis(LongestAxis)) * 0.5f;

		NavMeshVolume Half1;
		Half1.Bounds.Min = RefVolume.Bounds.Min;
		Half1.Bounds.Max = RefVolume.Bounds.Max;
		Half1.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		NavMeshVolume Half2;
		Half2.Bounds.Min = RefVolume.Bounds.Min;
		Half2.Bounds.Max = RefVolume.Bounds.Max;
		Half2.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		switch (LongestAxis)
		{
			case EAxis::X: Half1.Bounds.Max.X = MidValue; Half2.Bounds.Min.X = MidValue; break;
			case EAxis::Y: Half1.Bounds.Max.Y = MidValue; Half2.Bounds.Min.Y = MidValue; break;
			case EAxis::Z: Half1.Bounds.Max.Z = MidValue; Half2.Bounds.Min.Z = MidValue; break;
			default: break;
		}

		const float MinVolumeSize = RefVolume.ParentBoundsVolume.IsValid() ? RefVolume.ParentBoundsVolume->MinVolumeSize : 0.0f;

		auto LongestDim = [](const NavMeshVolume& V) -> float
		{
			const FVector S = V.Bounds.GetSize();
			return FMath::Max3(S.X, S.Y, S.Z);
		};

		if (LongestDim(Half1) >= MinVolumeSize)
		{
			ProcessVolumesList.Add(Half1);
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("Queued remainder volume Extent:%s"), *Half1.Bounds.GetExtent().ToString()));
			}
		}
		if (LongestDim(Half2) >= MinVolumeSize)
		{
			ProcessVolumesList.Add(Half2);
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("Queued remainder volume Extent:%s"), *Half2.Bounds.GetExtent().ToString()));
			}
		}
	}
	else if (StartPenetratingCount == 0)
	{
		// No collision in any direction: volume is clear, add to solution
		AddNavMeshVolume(RefVolume);
		if (InDrawDebug)
		{
			AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
				FString::Printf(TEXT("Added solution volume ID:%llu Extent:%s"), RefVolume.ID, *RefVolume.Bounds.GetExtent().ToString()));
		}
	}

	return false;
}

NavMeshVolume* UNavGen3DSubsystem::FindVolumeContainingLocation(const FVector& InLocation)
{
	const uint64 LocationHash = CalculateHash3D(InLocation);
	if (const TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(LocationHash))
	{
		for (uint64 VolumeID : *VolumeIDs)
		{
			if (NavMeshVolume* Volume = NavMeshSolutionMap.Find(VolumeID))
			{
				if (Volume->Bounds.IsInsideOrOn(InLocation))
				{
					return Volume;
				}
			}
		}
	}
	return nullptr;
}

TOptional<NavMeshVolume> UNavGen3DSubsystem::FindGenerationVolumeContainingLocation(const FVector& InLocation, bool InRemoveFromProcessing)
{
	for (int32 i = 0; i < ProcessVolumesList.Num(); ++i)
	{
		if (ProcessVolumesList[i].Bounds.IsInsideOrOn(InLocation))
		{
			NavMeshVolume Result = ProcessVolumesList[i];
			if (InRemoveFromProcessing)
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
			if (FBox::BuildAABB(Origin, Extent).IsInsideOrOn(InLocation))
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

TOptional<NavMeshVolume> UNavGen3DSubsystem::FindClosestGenerationVolume(const FVector& InLocation, bool InRemoveFromProcessing)
{
	if (!ProcessVolumesList.IsEmpty())
	{
		float ClosestDistSq = FLT_MAX;
		int32 ClosestIdx = INDEX_NONE;
		for (int32 i = 0; i < ProcessVolumesList.Num(); ++i)
		{
			const float DistSq = FVector::DistSquared(ProcessVolumesList[i].Bounds.GetCenter(), InLocation);
			if (DistSq < ClosestDistSq)
			{
				ClosestDistSq = DistSq;
				ClosestIdx = i;
			}
		}

		if (ClosestIdx != INDEX_NONE)
		{
			NavMeshVolume Result = ProcessVolumesList[ClosestIdx];
			if (InRemoveFromProcessing)
			{
				ProcessVolumesList.RemoveAtSwap(ClosestIdx);
			}
			return Result;
		}
		return TOptional<NavMeshVolume>();
	}

	float ClosestDistSq = FLT_MAX;
	NavMeshVolume ClosestVolume;
	bool bFound = false;
	for (ANavGen3DBoundsVolume* BoundsVolume : GetBoundsVolumes())
	{
		if (!BoundsVolume || !BoundsVolume->Enabled || BoundsVolume->Embedded)
		{
			continue;
		}

		FVector Origin;
		FVector Extent;
		BoundsVolume->GetActorBounds(false, Origin, Extent);
		const FBox Box = FBox::BuildAABB(Origin, Extent);
		const float DistSq = FVector::DistSquared(Box.GetCenter(), InLocation);
		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			ClosestVolume.Bounds = Box;
			ClosestVolume.ParentBoundsVolume = BoundsVolume;
			bFound = true;
		}
	}

	return bFound ? TOptional<NavMeshVolume>(ClosestVolume) : TOptional<NavMeshVolume>();
}

void UNavGen3DSubsystem::RemoveNavMeshVolume(uint64 InID)
{
	if (NavMeshVolume* Volume = NavMeshSolutionMap.Find(InID))
	{
		const uint64 LocationHash = CalculateHash3D(Volume->Bounds.GetCenter());
		if (TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(LocationHash))
		{
			VolumeIDs->Remove(InID);
			if (VolumeIDs->IsEmpty())
			{
				NavMeshSolutionMapByLocation.Remove(LocationHash);
			}
		}
	}
	NavMeshSolutionMap.Remove(InID);
}

bool UNavGen3DSubsystem::PlaneTrace(FVector InMin, FVector InMax, EAxis::Type InAxis, FVector& OutImpactPoint, bool& OutStartPenetrating)
{
	OutStartPenetrating = false;

	FVector Center = (InMin + InMax) * 0.5f;
	FVector HalfExtent = (InMax - InMin) * 0.5f;
	FVector Start = Center;
	FVector End = Center;

	switch (InAxis)
	{
		case EAxis::X:
			HalfExtent.X = 0.0f;
			Start.X = InMin.X;
			End.X = InMax.X;
			break;
		case EAxis::Y:
			HalfExtent.Y = 0.0f;
			Start.Y = InMin.Y;
			End.Y = InMax.Y;
			break;
		case EAxis::Z:
			HalfExtent.Z = 0.0f;
			Start.Z = InMin.Z;
			End.Z = InMax.Z;
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
		if (HitResult.bStartPenetrating)
		{
			OutStartPenetrating = true;
			return false;
		}
		if (HitResult.bBlockingHit)
		{
			OutImpactPoint = HitResult.ImpactPoint;
			if (FMath::IsNearlyEqual(OutImpactPoint.GetComponentForAxis(InAxis), Start.GetComponentForAxis(InAxis)))
			{
				OutStartPenetrating = true;
				return false;
			}
			return true;
		}
	}

	return false;
}

bool UNavGen3DSubsystem::GenerateNavMesh3D(NavMeshVolume* InVolume)
{
	if (InVolume)
	{
		return ProcessNavMeshVolume(*InVolume);
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

bool UNavGen3DSubsystem::GenerateNavMesh3DFromBoundsVolume(ANavGen3DBoundsVolume* InVolume)
{
	ProcessVolumesList.Reset();

	const FString ActorName = InVolume ? InVolume->GetActorLabel() : TEXT("<unknown>");
	AddLogMessage(ENavGen3DLogCategory::Info, ActorName, TEXT("generating NavMesh3D..."));

	bool bSuccess = InVolume != nullptr;
	FString Result = bSuccess ? TEXT("success") : TEXT("fail");
	if (IsPlayMode())
	{
		bSuccess = false;
		Result = TEXT("Generation only available from Editor");
	}

	ValidateEmbeddedBoundsVolumes();

	if (bSuccess && InVolume->Embedded)
	{
		bSuccess = false;
		Result = TEXT("Embedded volume invalid for generation.  Generate from the encompassing bounds volume.");
	}

	if (bSuccess)
	{
		FVector Origin;
		FVector Extent;
		InVolume->GetActorBounds(false, Origin, Extent);

		NavMeshVolume NavVolume;
		NavVolume.Bounds = FBox::BuildAABB(Origin, Extent);
		NavVolume.ParentBoundsVolume = InVolume;
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

bool UNavGen3DSubsystem::ValidateNavMesh3D(NavMeshVolume* InVolume)
{
	if (!InVolume)
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
	bool bIgnored = false;
	if (PlaneTrace(InVolume->Bounds.Min, InVolume->Bounds.Max, EAxis::X, ImpactPoint, bIgnored) ||
		PlaneTrace(InVolume->Bounds.Min, InVolume->Bounds.Max, EAxis::Y, ImpactPoint, bIgnored) ||
		PlaneTrace(InVolume->Bounds.Min, InVolume->Bounds.Max, EAxis::Z, ImpactPoint, bIgnored))
	{
		const FString ActorName = InVolume->ParentBoundsVolume.IsValid()
			? InVolume->ParentBoundsVolume->GetActorLabel()
			: TEXT("<unknown>");
		AddLogMessage(ENavGen3DLogCategory::Warning, ActorName,
			FString::Printf(TEXT("Validation failed for volume ID %llu"), InVolume->ID));
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

			const float DrawTime = World->GetDeltaSeconds();
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
				DrawDebugBox(World, Origin, Extent, Volume->GetActorRotation().Quaternion(), DrawColor, false, DrawTime, 0, 5.0f);
			}
			break;
		}

		case ENavGen3DDrawMode::NavMesh3D:
		{
			if (UWorld* World = FindWorld())
			{
				const float DrawTime = World->GetDeltaSeconds();
				for (const auto& Pair : NavMeshSolutionMap)
				{
					const NavMeshVolume& Volume = Pair.Value;
					DrawDebugBox(World, Volume.Bounds.GetCenter(), Volume.Bounds.GetExtent(), FColor::Cyan, false, DrawTime, 0, 3.0f);
				}
			}
			break;
		}

		case ENavGen3DDrawMode::UnprocessedVolumes:
		{
			if (UWorld* World = FindWorld())
			{
				const float DrawTime = World->GetDeltaSeconds();
				for (const NavMeshVolume& Volume : ProcessVolumesList)
				{
					DrawDebugBox(World, Volume.Bounds.GetCenter(), Volume.Bounds.GetExtent(), FColor::Yellow, false, DrawTime, 0, 3.0f);
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
			const float DrawTime = World->GetDeltaSeconds();
			const FVector CameraLocation = GetCameraLocation();

			if (NavMeshVolume* Volume = FindVolumeContainingLocation(CameraLocation))
			{
				DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::Green, false, DrawTime, 0, 5.0f);
			}
			else
			{
				TOptional<NavMeshVolume> GenVolume = FindGenerationVolumeContainingLocation(CameraLocation, false);
				if (GenVolume.IsSet())
				{
					DrawDebugBox(World, GenVolume->Bounds.GetCenter(), GenVolume->Bounds.GetExtent(), FColor::Orange, false, DrawTime, 0, 5.0f);
				}
			}
		}
	}
}
