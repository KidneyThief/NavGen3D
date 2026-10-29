// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DSettings.h"
#include "NavMesh3DVolume.h"
#include "SNavGen3DWindow.h"
#include "Misc/CoreDelegates.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "LevelEditorViewport.h"
#include "Editor.h"
#include "NavigationSystem.h"

// -- defined in NavGen3DSettings.h
NavGen3D_DISABLE_OPTIMIZATION

namespace NavGen3DClip
{
	// Normals within this tolerance of a cardinal axis are treated as axis aligned
	constexpr float AxisAlignedTolerance = 1.0e-3f;
	constexpr float ClipArrowLength = 50.0f;
	// Larger than the orange trace impact spheres so it isn't hidden by them
	constexpr float ClipPointRadius = 15.0f;
	// Distance (uu) within which an impact point counts as lying on a surface plane
	constexpr float CornerPlaneTolerance = 1.0f;
	// Two surfaces form a corner only if their normals differ by more than ~8 degrees
	constexpr float CornerMaxNormalDot = 0.99f;
	// Steps across a volume when searching inward from a face that starts inside geometry for the
	// first clear slice
	constexpr int32 ClearSliceSteps = 64;
	// Inset (uu) of the plane traces from each side of the volume, so touching geometry is ignored
	constexpr float TraceInset = 1.0f;
	// Generation gives up after this many processed volumes per allowed solution volume
	constexpr int32 MaxProcessIterationsPerVolume = 16;

	bool IsAxisAlignedNormal(const FVector& InNormal)
	{
		const FVector Normal = InNormal.GetSafeNormal();
		for (int32 i = 0; i < 3; ++i)
		{
			if (FMath::IsNearlyEqual(FMath::Abs(Normal[i]), 1.0f, AxisAlignedTolerance))
			{
				return true;
			}
		}
		return false;
	}

	bool IsClippedVolume(const NavMeshVolumeBase& InVolume)
	{
		const NavMesh3DVolume* Volume3D = InVolume.AsNavMesh3DVolume();
		return Volume3D && Volume3D->IsClipped();
	}

	void GetBoxCorners(const FBox& InBox, FVector OutCorners[8])
	{
		for (int32 i = 0; i < 8; ++i)
		{
			OutCorners[i] = FVector(
				(i & 1) ? InBox.Max.X : InBox.Min.X,
				(i & 2) ? InBox.Max.Y : InBox.Min.Y,
				(i & 4) ? InBox.Max.Z : InBox.Min.Z);
		}
	}

	// Calls InFunc(A, B) for each of the 12 box edges
	template <typename FuncType>
	void ForEachBoxEdge(const FVector InCorners[8], FuncType InFunc)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			for (int32 Bit = 1; Bit <= 4; Bit <<= 1)
			{
				if (!(i & Bit))
				{
					InFunc(InCorners[i], InCorners[i | Bit]);
				}
			}
		}
	}

	// Point on the line where two planes meet, nearest InNearPoint. False if the planes are parallel.
	bool GetPlaneIntersectionPoint(const FVector& InPointA, const FVector& InNormalA, const FVector& InPointB, const FVector& InNormalB,
		const FVector& InNearPoint, FVector& OutPoint)
	{
		const FVector LineDir = FVector::CrossProduct(InNormalA, InNormalB);
		const float LineDirSizeSq = LineDir.SizeSquared();
		if (LineDirSizeSq < KINDA_SMALL_NUMBER) return false;

		const float DistA = InNormalA | InPointA;
		const float DistB = InNormalB | InPointB;
		const FVector PointOnLine = (DistA * FVector::CrossProduct(InNormalB, LineDir) + DistB * FVector::CrossProduct(LineDir, InNormalA)) / LineDirSizeSq;

		const FVector UnitDir = LineDir / FMath::Sqrt(LineDirSizeSq);
		OutPoint = PointOnLine + UnitDir * ((InNearPoint - PointOnLine) | UnitDir);
		return true;
	}

	// <= 0 is the valid side of the clip plane
	float GetClipSide(const FVector& InPoint, const FVector& InClipPoint, const FVector& InClipVector)
	{
		return FVector::DotProduct(InPoint - InClipPoint, InClipVector);
	}

	// Bounds of the portion of InBox on the valid side of the clip plane (invalid box if none)
	FBox GetClippedBounds(const FBox& InBox, const FVector& InClipPoint, const FVector& InClipVector)
	{
		FVector Corners[8];
		GetBoxCorners(InBox, Corners);

		FBox Result(ForceInit);
		for (const FVector& Corner : Corners)
		{
			if (GetClipSide(Corner, InClipPoint, InClipVector) <= 0.0f)
			{
				Result += Corner;
			}
		}
		ForEachBoxEdge(Corners, [&](const FVector& A, const FVector& B)
		{
			const float SideA = GetClipSide(A, InClipPoint, InClipVector);
			const float SideB = GetClipSide(B, InClipPoint, InClipVector);
			if ((SideA < 0.0f && SideB > 0.0f) || (SideA > 0.0f && SideB < 0.0f))
			{
				Result += FMath::Lerp(A, B, SideA / (SideA - SideB));
			}
		});
		return Result;
	}

	// Fits the volume's bounds to its valid side, dropping the clip if it no longer cuts the
	// bounds. Returns false if nothing of the volume remains valid.
	bool FitBoundsToClip(NavMesh3DVolume& RefVolume)
	{
		if (!RefVolume.IsClipped()) return true;

		RefVolume.Bounds = GetClippedBounds(RefVolume.Bounds, RefVolume.ClipPoint, RefVolume.ClipVector);
		if (!RefVolume.Bounds.IsValid) return false;

		FVector Corners[8];
		GetBoxCorners(RefVolume.Bounds, Corners);
		for (const FVector& Corner : Corners)
		{
			if (GetClipSide(Corner, RefVolume.ClipPoint, RefVolume.ClipVector) > 0.0f)
			{
				return true;
			}
		}
		RefVolume.ClipPoint = FVector::ZeroVector;
		RefVolume.ClipVector = FVector::ZeroVector;
		return true;
	}

	// Box edges are green on the valid side of the clip plane and red on the clipped side,
	// with the clip plane outlined in yellow and an arrow toward the clipped side
	void DrawClippedVolume(const UWorld* InWorld, const NavMesh3DVolume& InVolume, float InDrawTime, float InThickness)
	{
		FVector Corners[8];
		GetBoxCorners(InVolume.Bounds, Corners);

		TArray<FVector> PlanePoints;
		ForEachBoxEdge(Corners, [&](const FVector& A, const FVector& B)
		{
			const float SideA = GetClipSide(A, InVolume.ClipPoint, InVolume.ClipVector);
			const float SideB = GetClipSide(B, InVolume.ClipPoint, InVolume.ClipVector);
			const FColor ColorA = SideA <= 0.0f ? FColor::Green : FColor::Red;
			const FColor ColorB = SideB <= 0.0f ? FColor::Green : FColor::Red;
			if (ColorA == ColorB)
			{
				DrawDebugLine(InWorld, A, B, ColorA, false, InDrawTime, 0, InThickness);
				return;
			}
			const FVector Mid = FMath::Lerp(A, B, SideA / (SideA - SideB));
			DrawDebugLine(InWorld, A, Mid, ColorA, false, InDrawTime, 0, InThickness);
			DrawDebugLine(InWorld, Mid, B, ColorB, false, InDrawTime, 0, InThickness);
			PlanePoints.Add(Mid);
		});

		if (!InVolume.IsClipped()) return;

		const FVector Normal = InVolume.ClipVector.GetSafeNormal();
		if (PlanePoints.Num() >= 3)
		{
			FVector Centroid = FVector::ZeroVector;
			for (const FVector& Point : PlanePoints)
			{
				Centroid += Point;
			}
			Centroid /= PlanePoints.Num();

			// Order the edge crossings around the plane normal so they form the outline
			FVector AxisU, AxisV;
			Normal.FindBestAxisVectors(AxisU, AxisV);
			PlanePoints.Sort([&](const FVector& A, const FVector& B)
			{
				const FVector OffsetA = A - Centroid;
				const FVector OffsetB = B - Centroid;
				return FMath::Atan2(OffsetA | AxisV, OffsetA | AxisU) < FMath::Atan2(OffsetB | AxisV, OffsetB | AxisU);
			});

			for (int32 i = 0; i < PlanePoints.Num(); ++i)
			{
				DrawDebugLine(InWorld, PlanePoints[i], PlanePoints[(i + 1) % PlanePoints.Num()], FColor::Yellow, false, InDrawTime, 0, InThickness);
			}
		}

		// Clip point, with the clip vector pointing toward the clipped side
		DrawDebugSphere(InWorld, InVolume.ClipPoint, ClipPointRadius, 12, FColor::Purple, false, InDrawTime, 0, InThickness);
		DrawDebugDirectionalArrow(InWorld, InVolume.ClipPoint, InVolume.ClipPoint + Normal * ClipArrowLength,
			ClipArrowLength * 0.25f, FColor::Purple, false, InDrawTime, 0, InThickness);
	}
}

void UNavGen3DSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	FCoreDelegates::OnEndFrame.AddUObject(this, &UNavGen3DSubsystem::OnEndFrame);
#if WITH_EDITOR
	FEditorDelegates::MapChange.AddUObject(this, &UNavGen3DSubsystem::OnMapChanged);
	FEditorDelegates::PreBeginPIE.AddUObject(this, &UNavGen3DSubsystem::OnBeginPIE);
	FEditorDelegates::EndPIE.AddUObject(this, &UNavGen3DSubsystem::OnEndPIE);
#endif
}

void UNavGen3DSubsystem::Deinitialize()
{
	FCoreDelegates::OnEndFrame.RemoveAll(this);
#if WITH_EDITOR
	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif
	ClearSolutionVolumes();
	ClearProcessVolumes();
	ClearDebugUndoState();
	Super::Deinitialize();
}

#if WITH_EDITOR
void UNavGen3DSubsystem::OnMapChanged(uint32 InMapChangeFlags)
{
	if (InMapChangeFlags & MapChangeEventFlags::WorldTornDown)
	{
		// Drop strong references to level actors so GC can collect the old world
		BoundsVolumes.Reset();
		ClearSolutionVolumes();
		NavMeshSolutionMapByLocation.Reset();
		ClearProcessVolumes();
		ClearDebugUndoState();
		NavMeshVolumeMap_X.Reset();
		NavMeshVolumeMap_Y.Reset();
		NavMeshVolumeMap_Z.Reset();
		NavVolumeConnectionMap.Reset();
		NavMeshVolumeIDGenerator = 0;
		DebugPathSearchSpace.Reset();
		return;
	}
	InitializeNavMesh3D();
}

void UNavGen3DSubsystem::OnBeginPIE(bool bIsSimulating)
{
	InitializeNavMesh3D();
}

void UNavGen3DSubsystem::OnEndPIE(bool bIsSimulating)
{
	InitializeNavMesh3D();
}
#endif

UWorld* UNavGen3DSubsystem::FindWorld() const
{
	UWorld* EditorWorld = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!World) continue;
		if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
		{
			return World;
		}
		if (World->WorldType == EWorldType::Editor)
		{
			EditorWorld = World;
		}
	}
	return EditorWorld;
}

FVector UNavGen3DSubsystem::GetCameraLocation(int32 InAgentIndex) const
{
	FVector Result = InvalidLocation;

	if (!IsPlayMode())
	{
		if (GEditor)
		{
			for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
			{
				if (LevelVC && LevelVC->IsPerspective())
				{
					Result = LevelVC->GetViewLocation();
					break;
				}
			}
		}
	}
	else if (UWorld* World = FindWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Result = Pawn->GetActorLocation();
			}
		}
	}

	return Result;
}

bool UNavGen3DSubsystem::GenerateNavMesh3DConnections(int32 InAgentIndex)
{
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(InAgentIndex, true));

	NavVolumeConnectionMap.Remove(InAgentIndex);

	for (const auto& Pair : NavMeshSolutionMap)
	{
		FindNavMeshVolumeConnections(InAgentIndex, Sphere, *Pair.Value);

		TMap<uint64, TArray<FNavVolumeConnection>>& AgentMap = NavVolumeConnectionMap.FindOrAdd(InAgentIndex);
		if (const TArray<FNavVolumeConnection>* ConnectionsPtr = AgentMap.Find(Pair.Key))
		{
			const TArray<FNavVolumeConnection> Connections = *ConnectionsPtr;
			for (const FNavVolumeConnection& Conn : Connections)
			{
				TArray<FNavVolumeConnection>& NeighborConns = AgentMap.FindOrAdd(Conn.ID);
				bool bAlreadyMirrored = false;
				for (const FNavVolumeConnection& NC : NeighborConns)
				{
					if (NC.ID == Pair.Key) { bAlreadyMirrored = true; break; }
				}
				if (!bAlreadyMirrored)
				{
					FNavVolumeConnection Reverse;
					Reverse.ID = Pair.Key;
					Reverse.Location = Conn.Location;
					Reverse.ConnectionAxis = -Conn.ConnectionAxis;
					NeighborConns.Add(Reverse);
				}
			}
		}
	}

	DetermineConnectivity();

	return true;
}

NavMeshVolumeBase* UNavGen3DSubsystem::FindNavMeshVolumeByID(uint64 InID) const
{
	NavMeshVolumeBase* const* Found = NavMeshSolutionMap.Find(InID);
	return Found ? *Found : nullptr;
}

NavMeshVolumeBase* UNavGen3DSubsystem::CreateNavMeshVolume() const
{
	if (AxisAligned)
	{
		return new NavMeshVolume_AA();
	}
	return new NavMesh3DVolume();
}

void UNavGen3DSubsystem::ClearSolutionVolumes()
{
	for (auto& Pair : NavMeshSolutionMap)
	{
		delete Pair.Value;
	}
	NavMeshSolutionMap.Reset();
}

void UNavGen3DSubsystem::ClearProcessVolumes()
{
	for (NavMeshVolumeBase* Volume : ProcessVolumesList)
	{
		delete Volume;
	}
	ProcessVolumesList.Reset();
}

void UNavGen3DSubsystem::ClearDebugUndoState()
{
	delete DebugUndoState.ProcessedVolume;
	DebugUndoState.ProcessedVolume = nullptr;
	for (auto& Pair : DebugUndoState.SolutionMapSnapshot)
	{
		delete Pair.Value;
	}
	DebugUndoState.SolutionMapSnapshot.Reset();
	DebugUndoState.bIsValid = false;
}

void UNavGen3DSubsystem::FindNavMeshVolumeConnections(int32 InAgentIndex, const FCollisionShape& InSphere, const NavMeshVolumeBase& InSourceVolume)
{
	TArray<FNavVolumeConnection>& Connections = NavVolumeConnectionMap.FindOrAdd(InAgentIndex).FindOrAdd(InSourceVolume.ID);

	const float Stride = (float)GetDefault<UNavGen3DSettings>()->MaxVolumeSize;
	const FVector Center = InSourceVolume.Bounds.GetCenter();
	TSet<uint64> FoundIDs;
	for (const FNavVolumeConnection& Existing : Connections)
	{
		FoundIDs.Add(Existing.ID);
	}

	auto CheckBucket = [&](const TMap<uint64, uint64>& InMap, uint64 InHash, int32 InSortAxis)
	{
		const uint64* Head = InMap.Find(InHash);
		if (!Head || *Head == 0) return;
		const float BreakValue = InSourceVolume.Bounds.Max[InSortAxis];
		uint64 CurrID = *Head;
		while (CurrID != 0)
		{
			const NavMeshVolumeBase* Curr = FindNavMeshVolumeByID(CurrID);
			if (!Curr) break;
			if (Curr->Bounds.Min[InSortAxis] > BreakValue) break;
			if (Curr->ID != InSourceVolume.ID && !FoundIDs.Contains(Curr->ID))
			{
				FVector Location;
				int32 ConnAxis = 0;
				if (FindNavMeshVolumeConnection(InAgentIndex, InSphere, InSourceVolume, *Curr, InSortAxis, Location, ConnAxis))
				{
					FoundIDs.Add(Curr->ID);
					FNavVolumeConnection Conn;
					Conn.ID = Curr->ID;
					Conn.Location = Location;
					Conn.ConnectionAxis = ConnAxis;
					Connections.Add(Conn);
				}
			}
			switch (InSortAxis)
			{
				case 0: CurrID = Curr->Next_X; break;
				case 1: CurrID = Curr->Next_Y; break;
				case 2: CurrID = Curr->Next_Z; break;
				default: CurrID = 0; break;
			}
		}
	};

	for (int32 s1 = -1; s1 <= 1; ++s1)
	{
		for (int32 s2 = -1; s2 <= 1; ++s2)
		{
			CheckBucket(NavMeshVolumeMap_X, CalculateHash3D(FVector(0.0f, Center.Y + s1 * Stride, Center.Z + s2 * Stride)), 0);
			CheckBucket(NavMeshVolumeMap_Y, CalculateHash3D(FVector(Center.X + s1 * Stride, 0.0f, Center.Z + s2 * Stride)), 1);
			CheckBucket(NavMeshVolumeMap_Z, CalculateHash3D(FVector(Center.X + s1 * Stride, Center.Y + s2 * Stride, 0.0f)), 2);
		}
	}
}

const TArray<FNavVolumeConnection>* UNavGen3DSubsystem::GetVolumeConnections(int32 InAgentIndex, uint64 InVolumeID) const
{
	if (const TMap<uint64, TArray<FNavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(InAgentIndex))
	{
		return AgentMap->Find(InVolumeID);
	}
	return nullptr;
}

bool UNavGen3DSubsystem::FindNavMeshVolumeConnection(int32 InAgentIndex, const FCollisionShape& InSphere, const NavMeshVolumeBase& InSourceVolume, const NavMeshVolumeBase& InNeighborVolume, int32 InAxis, FVector& OutLocation, int32& OutConnectionAxis)
{
	const FBox& S = InSourceVolume.Bounds;
	const FBox& N = InNeighborVolume.Bounds;

	const float SMin = S.Min[InAxis], SMax = S.Max[InAxis];
	const float NMin = N.Min[InAxis], NMax = N.Max[InAxis];

	float SharedValue;
	if      (FMath::IsNearlyEqual(SMin, NMax, Epsilon)) { SharedValue = SMin; OutConnectionAxis = -(InAxis + 1); }
	else if (FMath::IsNearlyEqual(SMax, NMin, Epsilon)) { SharedValue = SMax; OutConnectionAxis =  (InAxis + 1); }
	else return false;

	const int32 A1 = (InAxis + 1) % 3;
	const int32 A2 = (InAxis + 2) % 3;

	const float OMin1 = FMath::Max(S.Min[A1], N.Min[A1]);
	const float OMax1 = FMath::Min(S.Max[A1], N.Max[A1]);
	const float OMin2 = FMath::Max(S.Min[A2], N.Min[A2]);
	const float OMax2 = FMath::Min(S.Max[A2], N.Max[A2]);

	if (OMax1 > OMin1 && OMax2 > OMin2)
	{
		OutLocation[InAxis] = SharedValue;
		OutLocation[A1]     = (OMin1 + OMax1) * 0.5f;
		OutLocation[A2]     = (OMin2 + OMax2) * 0.5f;

		const float SphereRadius = InSphere.GetSphereRadius();
		FVector TraceEnd = OutLocation;
		TraceEnd[InAxis] = (OutConnectionAxis > 0) ? NMax - SphereRadius : NMin + SphereRadius;

		return ValidateConnectionCollision(InSphere, OutLocation, TraceEnd);
	}

	return false;
}

int32 UNavGen3DSubsystem::GetSupportedAgentCount() const
{
	return GetDefault<UNavigationSystemV1>()->GetSupportedAgents().Num();
}

bool UNavGen3DSubsystem::GetAgentSettings(int32 InIndex, float& OutRadius, float& OutHeight) const
{
	const TArray<FNavDataConfig>& Agents = GetDefault<UNavigationSystemV1>()->GetSupportedAgents();
	if (!Agents.IsValidIndex(InIndex))
	{
		return false;
	}
	OutRadius = Agents[InIndex].AgentRadius;
	OutHeight = Agents[InIndex].AgentHeight;
	return true;
}

float UNavGen3DSubsystem::GetAgentCollisionRadius(int32 InAgentIndex, bool bPadded) const
{
	float Radius, Height;
	if (!GetAgentSettings(InAgentIndex, Radius, Height))
	{
		return 50.0f;
	}
	const float BaseRadius = FMath::Max(Radius, Height * 0.5f);
	return bPadded ? 1.1f * BaseRadius : BaseRadius;
}

bool UNavGen3DSubsystem::IsPlayMode() const
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
	BoundsVolumes.AddUnique(TWeakObjectPtr<ANavGen3DBoundsVolume>(InVolume));
}

void UNavGen3DSubsystem::RemoveBoundsVolume(ANavGen3DBoundsVolume* InVolume)
{
	BoundsVolumes.RemoveAll([InVolume](const TWeakObjectPtr<ANavGen3DBoundsVolume>& V) { return V.Get() == InVolume; });
}

void UNavGen3DSubsystem::SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> InWindow)
{
	NavGen3DWindowPtr = InWindow;
}

void UNavGen3DSubsystem::AddLogMessage(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage)
{
	const FString ResolvedName = InActorName.IsEmpty() ? TEXT("<unknown>") : InActorName;
	const FString FormattedMessage = FString::Printf(TEXT("%s: %s"), *ResolvedName, *InMessage);
	switch (InCategory)
	{
		case ENavGen3DLogCategory::Warning: UE_LOG(LogNavGen3D, Warning, TEXT("%s"), *FormattedMessage); break;
		case ENavGen3DLogCategory::Error:   UE_LOG(LogNavGen3D, Error,   TEXT("%s"), *FormattedMessage); break;
		default:                            UE_LOG(LogNavGen3D, Display, TEXT("%s"), *FormattedMessage); break;
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

bool UNavGen3DSubsystem::AddNavMeshVolume(NavMeshVolumeBase& RefVolume)
{
	for (int32 i = 0; i < 3; ++i)
	{
		RefVolume.Bounds.Min[i] = FMath::FloorToFloat(RefVolume.Bounds.Min[i] / Epsilon) * Epsilon;
		RefVolume.Bounds.Max[i] = FMath::FloorToFloat(RefVolume.Bounds.Max[i] / Epsilon) * Epsilon;
	}

	const FVector Size = RefVolume.Bounds.GetSize();
	if (Size.X < Epsilon || Size.Y < Epsilon || Size.Z < Epsilon)
	{
		return false;
	}

	const FVector Extent = RefVolume.Bounds.GetExtent();
	bool bAnyAgentFits = false;
	if (MinSolutionVolumeSize >= 0.0f)
	{
		// Fixed limit from the panel replaces the agent size: at least two sides must reach it
		int32 SidesMeetingLimit = 0;
		for (int32 i = 0; i < 3; ++i)
		{
			if (Size[i] >= MinSolutionVolumeSize)
			{
				++SidesMeetingLimit;
			}
		}
		bAnyAgentFits = SidesMeetingLimit >= 2;
	}
	for (int32 AgentIndex = 0; AgentIndex < GetSupportedAgentCount() && MinSolutionVolumeSize < 0.0f; ++AgentIndex)
	{
		const float PaddedRadius = GetAgentCollisionRadius(AgentIndex, true);
		int32 ExtentsMeetingRadius = 0;
		for (int32 i = 0; i < 3; ++i)
		{
			if (Extent[i] >= PaddedRadius)
			{
				++ExtentsMeetingRadius;
			}
		}
		if (ExtentsMeetingRadius >= 2)
		{
			bAnyAgentFits = true;
			break;
		}
	}
	if (!bAnyAgentFits)
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

		NavMeshVolumeBase* Half1 = CreateNavMeshVolume();
		Half1->Bounds.Min = RefVolume.Bounds.Min;
		Half1->Bounds.Max = RefVolume.Bounds.Max;

		NavMeshVolumeBase* Half2 = CreateNavMeshVolume();
		Half2->Bounds.Min = RefVolume.Bounds.Min;
		Half2->Bounds.Max = RefVolume.Bounds.Max;

		switch (LongestAxis)
		{
			case EAxis::X: Half1->Bounds.Max.X = MidValue; Half2->Bounds.Min.X = MidValue; break;
			case EAxis::Y: Half1->Bounds.Max.Y = MidValue; Half2->Bounds.Min.Y = MidValue; break;
			case EAxis::Z: Half1->Bounds.Max.Z = MidValue; Half2->Bounds.Min.Z = MidValue; break;
			default: break;
		}

		// Clipped volumes pass their clip plane to both halves; a half entirely on the
		// clipped side has nothing valid left and is dropped
		bool bHalf1Valid = true;
		bool bHalf2Valid = true;
		if (const NavMesh3DVolume* Source3D = RefVolume.AsNavMesh3DVolume())
		{
			NavMesh3DVolume* Half1_3D = Half1->AsNavMesh3DVolume();
			NavMesh3DVolume* Half2_3D = Half2->AsNavMesh3DVolume();
			if (Source3D->IsClipped() && Half1_3D && Half2_3D)
			{
				Half1_3D->ClipPoint = Half2_3D->ClipPoint = Source3D->ClipPoint;
				Half1_3D->ClipVector = Half2_3D->ClipVector = Source3D->ClipVector;
				bHalf1Valid = NavGen3DClip::FitBoundsToClip(*Half1_3D);
				bHalf2Valid = NavGen3DClip::FitBoundsToClip(*Half2_3D);
			}
		}

		const bool bAdded = (!bHalf1Valid || AddNavMeshVolume(*Half1)) && (!bHalf2Valid || AddNavMeshVolume(*Half2));
		delete Half1;
		delete Half2;
		return bAdded;
	}

	// Check for merge with an existing adjacent volume that shares an exact face
	{
		const FVector Center = RefVolume.Bounds.GetCenter();
		const float MaxVolumeSize_f = (float)Settings->MaxVolumeSize;
		// Merging would discard the clip plane, so clipped volumes never merge
		const bool bRefClipped = NavGen3DClip::IsClippedVolume(RefVolume);

		auto CheckAxisMerge = [&](const TMap<uint64, uint64>& InMap, uint64 InHash, int32 InAxis) -> bool
		{
			if (bRefClipped) return false;
			const uint64* HeadID = InMap.Find(InHash);
			if (!HeadID || *HeadID == 0) return false;

			const int32 A1 = (InAxis + 1) % 3;
			const int32 A2 = (InAxis + 2) % 3;

			uint64 CurrID = *HeadID;
			while (CurrID != 0)
			{
				const NavMeshVolumeBase* Curr = FindNavMeshVolumeByID(CurrID);
				if (!Curr) break;

				if (!NavGen3DClip::IsClippedVolume(*Curr) &&
					FMath::IsNearlyEqual(Curr->Bounds.Min[A1], RefVolume.Bounds.Min[A1], Epsilon) &&
					FMath::IsNearlyEqual(Curr->Bounds.Max[A1], RefVolume.Bounds.Max[A1], Epsilon) &&
					FMath::IsNearlyEqual(Curr->Bounds.Min[A2], RefVolume.Bounds.Min[A2], Epsilon) &&
					FMath::IsNearlyEqual(Curr->Bounds.Max[A2], RefVolume.Bounds.Max[A2], Epsilon))
				{
					const bool bSharesFace =
						FMath::IsNearlyEqual(Curr->Bounds.Max[InAxis], RefVolume.Bounds.Min[InAxis], Epsilon) ||
						FMath::IsNearlyEqual(Curr->Bounds.Min[InAxis], RefVolume.Bounds.Max[InAxis], Epsilon);

					if (bSharesFace)
					{
						const float MergedSize = Curr->Bounds.GetSize()[InAxis] + RefVolume.Bounds.GetSize()[InAxis];
						if (MergedSize <= MaxVolumeSize_f + Epsilon)
						{
							NavMeshVolumeBase* Merged = CreateNavMeshVolume();
							Merged->Bounds.Min = RefVolume.Bounds.Min;
							Merged->Bounds.Max = RefVolume.Bounds.Max;
							Merged->ParentBoundsVolume = RefVolume.ParentBoundsVolume.IsValid()
								? RefVolume.ParentBoundsVolume : Curr->ParentBoundsVolume;
							Merged->Bounds.Min[InAxis] = FMath::Min(Curr->Bounds.Min[InAxis], RefVolume.Bounds.Min[InAxis]);
							Merged->Bounds.Max[InAxis] = FMath::Max(Curr->Bounds.Max[InAxis], RefVolume.Bounds.Max[InAxis]);

							// Curr is deleted by RemoveNavMeshVolume, don't touch it after this
							const uint64 ExistingID = Curr->ID;
							RemoveNavMeshVolume(ExistingID);
							const bool bAdded = AddNavMeshVolume(*Merged);
							delete Merged;
							return bAdded;
						}
					}
				}

				switch (InAxis)
				{
					case 0: CurrID = Curr->Next_X; break;
					case 1: CurrID = Curr->Next_Y; break;
					case 2: CurrID = Curr->Next_Z; break;
					default: CurrID = 0; break;
				}
			}
			return false;
		};

		if (CheckAxisMerge(NavMeshVolumeMap_X, CalculateHash3D(FVector(0.0f, Center.Y, Center.Z)), 0)) return true;
		if (CheckAxisMerge(NavMeshVolumeMap_Y, CalculateHash3D(FVector(Center.X, 0.0f, Center.Z)), 1)) return true;
		if (CheckAxisMerge(NavMeshVolumeMap_Z, CalculateHash3D(FVector(Center.X, Center.Y, 0.0f)), 2)) return true;
	}

	// The solution map stores its own copy; RefVolume stays owned by the caller
	RefVolume.ID = ++NavMeshVolumeIDGenerator;
	NavMeshVolumeBase* Stored = RefVolume.Clone();
	NavMeshSolutionMap.Add(Stored->ID, Stored);

	const uint64 LocationHash = CalculateHash3D(RefVolume.Bounds.GetCenter());
	NavMeshSolutionMapByLocation.FindOrAdd(LocationHash).Add(RefVolume.ID);

	const FVector Center = Stored->Bounds.GetCenter();

	// X bucket: zero out X, sort by Min.X
	{
		const uint64 Hash = CalculateHash3D(FVector(0.0f, Center.Y, Center.Z));
		uint64& HeadID = NavMeshVolumeMap_X.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolumeBase* CurrVol = FindNavMeshVolumeByID(*CurrID);
			if (!CurrVol || CurrVol->Bounds.Min.X >= Stored->Bounds.Min.X) break;
			CurrID = &CurrVol->Next_X;
		}
		Stored->Next_X = *CurrID;
		*CurrID = Stored->ID;
	}

	// Y bucket: zero out Y, sort by Min.Y
	{
		const uint64 Hash = CalculateHash3D(FVector(Center.X, 0.0f, Center.Z));
		uint64& HeadID = NavMeshVolumeMap_Y.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolumeBase* CurrVol = FindNavMeshVolumeByID(*CurrID);
			if (!CurrVol || CurrVol->Bounds.Min.Y >= Stored->Bounds.Min.Y) break;
			CurrID = &CurrVol->Next_Y;
		}
		Stored->Next_Y = *CurrID;
		*CurrID = Stored->ID;
	}

	// Z bucket: zero out Z, sort by Min.Z
	{
		const uint64 Hash = CalculateHash3D(FVector(Center.X, Center.Y, 0.0f));
		uint64& HeadID = NavMeshVolumeMap_Z.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolumeBase* CurrVol = FindNavMeshVolumeByID(*CurrID);
			if (!CurrVol || CurrVol->Bounds.Min.Z >= Stored->Bounds.Min.Z) break;
			CurrID = &CurrVol->Next_Z;
		}
		Stored->Next_Z = *CurrID;
		*CurrID = Stored->ID;
	}

	if (DebugDrawTime > 0.0f)
	{
		if (UWorld* World = FindWorld())
		{
			if (NavGen3DClip::IsClippedVolume(*Stored))
			{
				NavGen3DClip::DrawClippedVolume(World, *Stored->AsNavMesh3DVolume(), DebugDrawTime, 5.0f);
			}
			else
			{
				DrawDebugBox(World, Center, Stored->Bounds.GetExtent(), FColor::Green, false, DebugDrawTime, 0, 5.0f);
			}
		}
	}

	return true;
}

void UNavGen3DSubsystem::AddGeneratedVolumeToSolution(const FNavGen3DGeneratedVolume& InGeneratedVolume, ANavGen3DBoundsVolume* InParent)
{
	NavMeshVolumeBase* Stored = CreateNavMeshVolume();
	Stored->ID     = InGeneratedVolume.ID;
	Stored->Bounds = FBox(InGeneratedVolume.BoundsMin, InGeneratedVolume.BoundsMax);
	Stored->ParentBoundsVolume = InParent;

	for (int32 AgentIndex = 0; AgentIndex < InGeneratedVolume.ConnectivityIDs.Num(); ++AgentIndex)
	{
		const int32 ConnID = InGeneratedVolume.ConnectivityIDs[AgentIndex];
		if (ConnID != 0)
		{
			Stored->ConnectivityIDByAgent.Add(AgentIndex, ConnID);
		}
	}

	// Add() below replaces any existing entry with the same ID, so free the old one
	delete FindNavMeshVolumeByID(Stored->ID);

	NavMeshVolumeIDGenerator = FMath::Max(NavMeshVolumeIDGenerator, Stored->ID);
	NavMeshSolutionMap.Add(Stored->ID, Stored);

	const uint64 LocationHash = CalculateHash3D(Stored->Bounds.GetCenter());
	NavMeshSolutionMapByLocation.FindOrAdd(LocationHash).Add(Stored->ID);

	const FVector Center = Stored->Bounds.GetCenter();

	// X bucket — sorted by Min.X
	{
		const uint64 Hash = CalculateHash3D(FVector(0.0f, Center.Y, Center.Z));
		uint64& HeadID = NavMeshVolumeMap_X.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolumeBase* CurrVol = FindNavMeshVolumeByID(*CurrID);
			if (!CurrVol || CurrVol->Bounds.Min.X >= Stored->Bounds.Min.X) break;
			CurrID = &CurrVol->Next_X;
		}
		Stored->Next_X = *CurrID;
		*CurrID = Stored->ID;
	}

	// Y bucket — sorted by Min.Y
	{
		const uint64 Hash = CalculateHash3D(FVector(Center.X, 0.0f, Center.Z));
		uint64& HeadID = NavMeshVolumeMap_Y.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolumeBase* CurrVol = FindNavMeshVolumeByID(*CurrID);
			if (!CurrVol || CurrVol->Bounds.Min.Y >= Stored->Bounds.Min.Y) break;
			CurrID = &CurrVol->Next_Y;
		}
		Stored->Next_Y = *CurrID;
		*CurrID = Stored->ID;
	}

	// Z bucket — sorted by Min.Z
	{
		const uint64 Hash = CalculateHash3D(FVector(Center.X, Center.Y, 0.0f));
		uint64& HeadID = NavMeshVolumeMap_Z.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolumeBase* CurrVol = FindNavMeshVolumeByID(*CurrID);
			if (!CurrVol || CurrVol->Bounds.Min.Z >= Stored->Bounds.Min.Z) break;
			CurrID = &CurrVol->Next_Z;
		}
		Stored->Next_Z = *CurrID;
		*CurrID = Stored->ID;
	}

	for (const auto& ConnPair : InGeneratedVolume.Connections)
	{
		TArray<FNavVolumeConnection>& RuntimeConns = NavVolumeConnectionMap.FindOrAdd(ConnPair.Key).FindOrAdd(Stored->ID);
		RuntimeConns.Reserve(ConnPair.Value.Connections.Num());
		for (const FNavGen3DStoredConnection& S : ConnPair.Value.Connections)
		{
			FNavVolumeConnection& C = RuntimeConns.AddDefaulted_GetRef();
			C.ID             = S.ID;
			C.Location       = S.Location;
			C.ConnectionAxis = S.ConnectionAxis;
		}
	}
}

float UNavGen3DSubsystem::GetProcessMinVolumeSize(const FBox& InBounds, const TWeakObjectPtr<ANavGen3DBoundsVolume>& InParent) const
{
	float MinVolumeSize = InParent.IsValid() ? InParent->MinVolumeSize : 0.0f;
	for (const TWeakObjectPtr<ANavGen3DBoundsVolume>& WeakVol : BoundsVolumes)
	{
		ANavGen3DBoundsVolume* EmbeddedVolume = WeakVol.Get();
		if (!EmbeddedVolume || !EmbeddedVolume->Embedded || !EmbeddedVolume->Enabled) continue;
		FVector EmbeddedOrigin, EmbeddedExtent;
		EmbeddedVolume->GetActorBounds(false, EmbeddedOrigin, EmbeddedExtent);
		if (InBounds.Intersect(FBox::BuildAABB(EmbeddedOrigin, EmbeddedExtent)))
			MinVolumeSize = FMath::Min(MinVolumeSize, EmbeddedVolume->MinVolumeSize);
	}
	return MinVolumeSize;
}

void UNavGen3DSubsystem::SplitAlongLongestAxis(NavMeshVolumeBase& RefVolume, bool InDrawDebug, const FString& InActorName)
{
	const FVector Size = RefVolume.Bounds.GetSize();
	EAxis::Type LongestAxis = EAxis::X;
	float MaxDim = Size.X;
	if (Size.Y > MaxDim) { MaxDim = Size.Y; LongestAxis = EAxis::Y; }
	if (Size.Z > MaxDim) { MaxDim = Size.Z; LongestAxis = EAxis::Z; }

	const float MidValue = (RefVolume.Bounds.Min.GetComponentForAxis(LongestAxis) + RefVolume.Bounds.Max.GetComponentForAxis(LongestAxis)) * 0.5f;

	NavMeshVolumeBase* Half1 = CreateNavMeshVolume();
	Half1->Bounds.Min = RefVolume.Bounds.Min;
	Half1->Bounds.Max = RefVolume.Bounds.Max;
	Half1->ParentBoundsVolume = RefVolume.ParentBoundsVolume;

	NavMeshVolumeBase* Half2 = CreateNavMeshVolume();
	Half2->Bounds.Min = RefVolume.Bounds.Min;
	Half2->Bounds.Max = RefVolume.Bounds.Max;
	Half2->ParentBoundsVolume = RefVolume.ParentBoundsVolume;

	switch (LongestAxis)
	{
		case EAxis::X: Half1->Bounds.Max.X = MidValue; Half2->Bounds.Min.X = MidValue; break;
		case EAxis::Y: Half1->Bounds.Max.Y = MidValue; Half2->Bounds.Min.Y = MidValue; break;
		case EAxis::Z: Half1->Bounds.Max.Z = MidValue; Half2->Bounds.Min.Z = MidValue; break;
		default: break;
	}

	auto LongestDim = [](const NavMeshVolumeBase& V) -> float
	{
		const FVector S = V.Bounds.GetSize();
		return FMath::Max3(S.X, S.Y, S.Z);
	};

	// Ownership of each half passes to ProcessVolumesList if queued, otherwise it is deleted
	auto QueueOrDelete = [&](NavMeshVolumeBase* InHalf)
	{
		if (LongestDim(*InHalf) >= GetProcessMinVolumeSize(InHalf->Bounds, InHalf->ParentBoundsVolume))
		{
			ProcessVolumesList.Add(InHalf);
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, InActorName,
					FString::Printf(TEXT("Queued remainder volume Extent:%s"), *InHalf->Bounds.GetExtent().ToString()));
			}
		}
		else
		{
			delete InHalf;
		}
	};

	QueueOrDelete(Half1);
	QueueOrDelete(Half2);
}

bool UNavGen3DSubsystem::ProcessNavMeshVolumeInterior(NavMeshVolumeBase& RefVolume, bool InDrawDebug)
{
	UWorld* World = FindWorld();
	if (!World) return false;

	const FVector Center = RefVolume.Bounds.GetCenter();
	const FString ActorName = RefVolume.ParentBoundsVolume.IsValid() ? RefVolume.ParentBoundsVolume->GetActorLabel() : TEXT("<unknown>");

	FVector InteriorMin = RefVolume.Bounds.Min;
	FVector InteriorMax = RefVolume.Bounds.Max;
	bool bFoundAnyHit = false;

	auto TraceTo = [&](const FVector& InTarget, int32 InAxis, bool bIsMax)
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Center, InTarget, ECC_WorldStatic) && !Hit.bStartPenetrating)
		{
			if (bIsMax)
				InteriorMax[InAxis] = Hit.ImpactPoint[InAxis];
			else
				InteriorMin[InAxis] = Hit.ImpactPoint[InAxis];
			bFoundAnyHit = true;
		}
	};

	TraceTo(FVector(RefVolume.Bounds.Max.X, Center.Y, Center.Z), 0, true);
	TraceTo(FVector(RefVolume.Bounds.Min.X, Center.Y, Center.Z), 0, false);
	TraceTo(FVector(Center.X, RefVolume.Bounds.Max.Y, Center.Z), 1, true);
	TraceTo(FVector(Center.X, RefVolume.Bounds.Min.Y, Center.Z), 1, false);
	TraceTo(FVector(Center.X, Center.Y, RefVolume.Bounds.Max.Z), 2, true);
	TraceTo(FVector(Center.X, Center.Y, RefVolume.Bounds.Min.Z), 2, false);

	if (!bFoundAnyHit) return false;

	// Snap interior bounds inward to the epsilon grid
	for (int32 i = 0; i < 3; ++i)
	{
		InteriorMin[i] = FMath::CeilToFloat(InteriorMin[i] / Epsilon) * Epsilon;
		InteriorMax[i] = FMath::FloorToFloat(InteriorMax[i] / Epsilon) * Epsilon;
	}

	const FVector InteriorSize = InteriorMax - InteriorMin;
	if (InteriorSize.X <= Epsilon || InteriorSize.Y <= Epsilon || InteriorSize.Z <= Epsilon)
		return false;

	// Verify the interior is clear along all 3 axes before committing it to the solution
	FVector PlaneImpactPoint;
	bool bPlaneStartPenetrating;
	if (PlaneTrace(InteriorMin, InteriorMax, EAxis::X, PlaneImpactPoint, bPlaneStartPenetrating)) return false;
	if (PlaneTrace(InteriorMin, InteriorMax, EAxis::Y, PlaneImpactPoint, bPlaneStartPenetrating)) return false;
	if (PlaneTrace(InteriorMin, InteriorMax, EAxis::Z, PlaneImpactPoint, bPlaneStartPenetrating)) return false;

	NavMeshVolumeBase* InteriorVolume = CreateNavMeshVolume();
	InteriorVolume->Bounds.Min = InteriorMin;
	InteriorVolume->Bounds.Max = InteriorMax;
	InteriorVolume->ParentBoundsVolume = RefVolume.ParentBoundsVolume;
	AddNavMeshVolume(*InteriorVolume);

	if (InDrawDebug)
	{
		AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
			FString::Printf(TEXT("Added interior volume ID:%llu Extent:%s"), InteriorVolume->ID, *InteriorVolume->Bounds.GetExtent().ToString()));
	}
	delete InteriorVolume;

	auto TryAddRemainder = [&](const FBox& RemainderBounds)
	{
		const FVector S = RemainderBounds.GetSize();
		if (S.X <= Epsilon || S.Y <= Epsilon || S.Z <= Epsilon) return;
		if (FMath::Max3(S.X, S.Y, S.Z) < GetProcessMinVolumeSize(RemainderBounds, RefVolume.ParentBoundsVolume)) return;
		NavMeshVolumeBase* Remainder = CreateNavMeshVolume();
		Remainder->Bounds = RemainderBounds;
		Remainder->ParentBoundsVolume = RefVolume.ParentBoundsVolume;
		ProcessVolumesList.Add(Remainder);
	};

	// Sort axes by their thinnest slab thickness (ascending) so that the thinnest
	// slabs peel first and retain full cross-section, avoiding skinny shapes.
	// Each subsequent axis is trimmed by already-peeled axes, guaranteeing no overlap.
	const float ThicknessPerAxis[3] = {
		FMath::Min(InteriorMin.X - RefVolume.Bounds.Min.X, RefVolume.Bounds.Max.X - InteriorMax.X),
		FMath::Min(InteriorMin.Y - RefVolume.Bounds.Min.Y, RefVolume.Bounds.Max.Y - InteriorMax.Y),
		FMath::Min(InteriorMin.Z - RefVolume.Bounds.Min.Z, RefVolume.Bounds.Max.Z - InteriorMax.Z),
	};

	int32 AxisOrder[3] = { 0, 1, 2 };
	if (ThicknessPerAxis[AxisOrder[0]] > ThicknessPerAxis[AxisOrder[1]]) Swap(AxisOrder[0], AxisOrder[1]);
	if (ThicknessPerAxis[AxisOrder[1]] > ThicknessPerAxis[AxisOrder[2]]) Swap(AxisOrder[1], AxisOrder[2]);
	if (ThicknessPerAxis[AxisOrder[0]] > ThicknessPerAxis[AxisOrder[1]]) Swap(AxisOrder[0], AxisOrder[1]);

	for (int32 PeelIdx = 0; PeelIdx < 3; ++PeelIdx)
	{
		const int32 Axis = AxisOrder[PeelIdx];

		FVector SlabMin = RefVolume.Bounds.Min;
		FVector SlabMax = RefVolume.Bounds.Max;

		// Trim dimensions already peeled to avoid overlap with previously placed slabs
		for (int32 PrevIdx = 0; PrevIdx < PeelIdx; ++PrevIdx)
		{
			const int32 PrevAxis = AxisOrder[PrevIdx];
			SlabMin[PrevAxis] = InteriorMin[PrevAxis];
			SlabMax[PrevAxis] = InteriorMax[PrevAxis];
		}

		// -side slab
		FBox SlabMinus(SlabMin, SlabMax);
		SlabMinus.Max[Axis] = InteriorMin[Axis];
		TryAddRemainder(SlabMinus);

		// +side slab
		FBox SlabPlus(SlabMin, SlabMax);
		SlabPlus.Min[Axis] = InteriorMax[Axis];
		TryAddRemainder(SlabPlus);
	}

	return true;
}

bool UNavGen3DSubsystem::ProcessNavMeshVolume(NavMeshVolumeBase& RefVolume, bool InDrawDebug)
{
	if (NavMesh3DVolume* Volume3D = RefVolume.AsNavMesh3DVolume())
	{
		return ProcessNavMesh3DVolume(*Volume3D, InDrawDebug);
	}

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
		const bool bFoundHit = PlaneTrace(TraceFirst, TraceSecond, Axis, ImpactPoint, bStartPenetrating);
		if (bFoundHit)
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

		if (DebugLevel >= 2 && World)
		{
			FVector TraceEnd = BoundsCenter;
			switch (Axis)
			{
				case EAxis::X: TraceEnd.X = bFromMin ? RefVolume.Bounds.Max.X : RefVolume.Bounds.Min.X; break;
				case EAxis::Y: TraceEnd.Y = bFromMin ? RefVolume.Bounds.Max.Y : RefVolume.Bounds.Min.Y; break;
				case EAxis::Z: TraceEnd.Z = bFromMin ? RefVolume.Bounds.Max.Z : RefVolume.Bounds.Min.Z; break;
				default: break;
			}
			const FColor LineColor = bStartPenetrating ? FColor::Red : (bFoundHit ? FColor::Yellow : FColor::Green);
			DrawDebugLine(World, TraceStart, TraceEnd, LineColor, false, DebugDrawTime, 0, 2.0f);
			if (bFoundHit)
			{
				DrawDebugSphere(World, ImpactPoint, 8.0f, 6, FColor::Orange, false, DebugDrawTime);
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

		NavMeshVolumeBase* FoundPtr = CreateNavMeshVolume();
		NavMeshVolumeBase& NavVolumeFound = *FoundPtr;
		NavVolumeFound.Bounds.Min = RefVolume.Bounds.Min;
		NavVolumeFound.Bounds.Max = RefVolume.Bounds.Max;
		NavVolumeFound.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		NavMeshVolumeBase* RemainderPtr = CreateNavMeshVolume();
		NavMeshVolumeBase& NavVolumeRemainder = *RemainderPtr;
		NavVolumeRemainder.Bounds.Min = RefVolume.Bounds.Min;
		NavVolumeRemainder.Bounds.Max = RefVolume.Bounds.Max;
		NavVolumeRemainder.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		// Clear space lies between the starting plane and the impact point.
		// Split so NavVolumeFound takes that clear slice and NavVolumeRemainder takes what's left.
		const float HalfEpsilon = Epsilon * 0.5f;
		if (bFarthestFromMin)
		{
			// Traced from Min side: clear space is Min → SplitValue
			switch (FarthestAxis)
			{
				case EAxis::X: NavVolumeFound.Bounds.Max.X = SplitValue - HalfEpsilon; NavVolumeRemainder.Bounds.Min.X = SplitValue; break;
				case EAxis::Y: NavVolumeFound.Bounds.Max.Y = SplitValue - HalfEpsilon; NavVolumeRemainder.Bounds.Min.Y = SplitValue; break;
				case EAxis::Z: NavVolumeFound.Bounds.Max.Z = SplitValue - HalfEpsilon; NavVolumeRemainder.Bounds.Min.Z = SplitValue; break;
				default: break;
			}
		}
		else
		{
			// Traced from Max side: clear space is SplitValue → Max
			switch (FarthestAxis)
			{
				case EAxis::X: NavVolumeFound.Bounds.Min.X = SplitValue + HalfEpsilon; NavVolumeRemainder.Bounds.Max.X = SplitValue; break;
				case EAxis::Y: NavVolumeFound.Bounds.Min.Y = SplitValue + HalfEpsilon; NavVolumeRemainder.Bounds.Max.Y = SplitValue; break;
				case EAxis::Z: NavVolumeFound.Bounds.Min.Z = SplitValue + HalfEpsilon; NavVolumeRemainder.Bounds.Max.Z = SplitValue; break;
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

		const FVector RemainderSize = NavVolumeRemainder.Bounds.GetSize();
		const float OriginalAxisSize = RefVolume.Bounds.GetSize().GetComponentForAxis(FarthestAxis);
		const float RemainderAxisSize = RemainderSize.GetComponentForAxis(FarthestAxis);
		const bool bMadeProgress = RemainderAxisSize < OriginalAxisSize;
		if (bMadeProgress && FMath::Max3(RemainderSize.X, RemainderSize.Y, RemainderSize.Z) >= GetProcessMinVolumeSize(NavVolumeRemainder.Bounds, NavVolumeRemainder.ParentBoundsVolume))
		{
			ProcessVolumesList.Add(RemainderPtr);
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
		else
		{
			delete RemainderPtr;
		}
		delete FoundPtr;
	}
	else if (StartPenetratingCount == 6)
	{
		SplitAlongLongestAxis(RefVolume, InDrawDebug, ActorName);
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
	else
	{
		// Partial penetration: some traces start inside geometry, none found a clear split
		SplitAlongLongestAxis(RefVolume, InDrawDebug, ActorName);
	}

	return false;
}

bool UNavGen3DSubsystem::ProcessNavMesh3DVolume(NavMesh3DVolume& RefVolume, bool InDrawDebug)
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
	FVector ImpactNormal = FVector::ZeroVector;
	const FVector BoundsCenter = (RefVolume.Bounds.Min + RefVolume.Bounds.Max) * 0.5f;

	EAxis::Type FarthestAxis = EAxis::None;
	bool bFarthestFromMin = false;
	FVector FarthestImpactPoint = FVector::ZeroVector;
	FVector FarthestImpactNormal = FVector::ZeroVector;
	int32 StartPenetratingCount = 0;

	// Farthest hit found so far, tracked separately for clean (axis aligned) and angled surfaces
	struct FSplitCandidate
	{
		float Distance = -1.0f;
		EAxis::Type Axis = EAxis::None;
		bool bFromMin = false;
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
	};
	FSplitCandidate CleanCandidate;
	FSplitCandidate AngledCandidate;

	struct FTraceHit
	{
		int32 AxisIndex;
		bool bFromMin;
		FVector Point;
		// Surface normal, oriented back toward the side the trace started from
		FVector Normal;
	};
	TArray<FTraceHit, TInlineAllocator<6>> TraceHits;

	// Sides whose trace started inside geometry, so something touches that face
	struct FPenetratingSide
	{
		int32 AxisIndex;
		bool bFromMin;
	};
	TArray<FPenetratingSide, TInlineAllocator<6>> PenetratingSides;

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

		// Traced on the volume inset slightly on every side, so geometry merely touching a face
		// (common, since remainders are cut exactly at impact points) neither starts the trace
		// inside geometry nor stops it
		FBox TraceBounds = RefVolume.Bounds.ExpandBy(-NavGen3DClip::TraceInset);
		if (!TraceBounds.IsValid || TraceBounds.Min.X >= TraceBounds.Max.X || TraceBounds.Min.Y >= TraceBounds.Max.Y || TraceBounds.Min.Z >= TraceBounds.Max.Z)
		{
			TraceBounds = RefVolume.Bounds;
		}
		FVector TraceFirst  = bFromMin ? TraceBounds.Min : TraceBounds.Max;
		FVector TraceSecond = bFromMin ? TraceBounds.Max : TraceBounds.Min;

		bool bStartPenetrating = false;
		const bool bFoundHit = PlaneTrace(TraceFirst, TraceSecond, Axis, ImpactPoint, bStartPenetrating, &ImpactNormal);
		if (bFoundHit)
		{
			const float Distance = FVector::Dist(ImpactPoint, TraceStart);
			const bool bCleanHit = ImpactNormal.IsNearlyZero() || NavGen3DClip::IsAxisAlignedNormal(ImpactNormal);
			FSplitCandidate& Candidate = bCleanHit ? CleanCandidate : AngledCandidate;
			if (Distance > Candidate.Distance)
			{
				Candidate.Distance = Distance;
				Candidate.Axis = Axis;
				Candidate.bFromMin = bFromMin;
				Candidate.Point = ImpactPoint;
				Candidate.Normal = ImpactNormal;
			}

			const int32 HitAxisIndex = (int32)Axis - (int32)EAxis::X;
			FVector OrientedNormal = ImpactNormal.GetSafeNormal();
			if (OrientedNormal[HitAxisIndex] * (bFromMin ? 1.0f : -1.0f) > 0.0f)
			{
				OrientedNormal = -OrientedNormal;
			}
			TraceHits.Add({ HitAxisIndex, bFromMin, ImpactPoint, OrientedNormal });
		}

		if (bStartPenetrating)
		{
			++StartPenetratingCount;
			PenetratingSides.Add({ (int32)Axis - (int32)EAxis::X, bFromMin });
		}

		if (DebugLevel >= 2 && World)
		{
			FVector TraceEnd = BoundsCenter;
			switch (Axis)
			{
				case EAxis::X: TraceEnd.X = bFromMin ? RefVolume.Bounds.Max.X : RefVolume.Bounds.Min.X; break;
				case EAxis::Y: TraceEnd.Y = bFromMin ? RefVolume.Bounds.Max.Y : RefVolume.Bounds.Min.Y; break;
				case EAxis::Z: TraceEnd.Z = bFromMin ? RefVolume.Bounds.Max.Z : RefVolume.Bounds.Min.Z; break;
				default: break;
			}
			const FColor LineColor = bStartPenetrating ? FColor::Red : (bFoundHit ? FColor::Yellow : FColor::Green);
			DrawDebugLine(World, TraceStart, TraceEnd, LineColor, false, DebugDrawTime, 0, 2.0f);
			if (bFoundHit)
			{
				DrawDebugSphere(World, ImpactPoint, 8.0f, 6, FColor::Orange, false, DebugDrawTime);
			}
		}
	};

	CheckTrace(EAxis::X, true);
	CheckTrace(EAxis::Y, true);
	CheckTrace(EAxis::Z, true);
	CheckTrace(EAxis::X, false);
	CheckTrace(EAxis::Y, false);
	CheckTrace(EAxis::Z, false);

	// Prefer a clean axis aligned split over a larger one that needs a clip plane, so the clipped
	// section of a solution volume never extends into empty space
	{
		const FSplitCandidate& Chosen = CleanCandidate.Axis != EAxis::None ? CleanCandidate : AngledCandidate;
		FarthestAxis = Chosen.Axis;
		bFarthestFromMin = Chosen.bFromMin;
		FarthestImpactPoint = Chosen.Point;
		FarthestImpactNormal = Chosen.Normal;
	}

	if (FarthestAxis == EAxis::None && StartPenetratingCount == 0)
	{
		// No collision in any direction: volume is clear, add to solution
		AddNavMeshVolume(RefVolume);
		if (InDrawDebug)
		{
			AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
				FString::Printf(TEXT("Added solution volume ID:%llu Extent:%s"), RefVolume.ID, *RefVolume.Bounds.GetExtent().ToString()));
		}
	}
	else if (FarthestAxis != EAxis::None)
	{
		const int32 AxisIndex = (int32)FarthestAxis - (int32)EAxis::X;

		NavMesh3DVolume SolutionVolume;
		SolutionVolume.Bounds = RefVolume.Bounds;
		SolutionVolume.ParentBoundsVolume = RefVolume.ParentBoundsVolume;

		// Set for clean axis-aligned splits and corner splits; owned here until queued.
		// RemainderAxis is the axis the processing volume was split on.
		NavMeshVolumeBase* Remainder = nullptr;
		int32 RemainderAxis = AxisIndex;

		// Queues a box of the processing volume that is not covered by the solution volume
		auto QueueRemainderBounds = [&](const FBox& InBounds)
		{
			const FVector Size = InBounds.GetSize();
			if (Size.X <= Epsilon || Size.Y <= Epsilon || Size.Z <= Epsilon) return;
			if (FMath::Max3(Size.X, Size.Y, Size.Z) < GetProcessMinVolumeSize(InBounds, RefVolume.ParentBoundsVolume)) return;

			NavMeshVolumeBase* Volume = CreateNavMeshVolume();
			Volume->Bounds = InBounds;
			Volume->ParentBoundsVolume = RefVolume.ParentBoundsVolume;
			ProcessVolumesList.Add(Volume);
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("Queued remainder volume Extent:%s"), *InBounds.GetExtent().ToString()));
				if (World)
				{
					DrawDebugBox(World, InBounds.GetCenter(), InBounds.GetExtent(), FColor::Red, false, DebugDrawTime, 0, 5.0f);
				}
			}
		};

		bool bHasValidVolume = true;
		if (FarthestImpactNormal.IsNearlyZero() || NavGen3DClip::IsAxisAlignedNormal(FarthestImpactNormal))
		{
			// Impact surface is parallel to a side of the volume: split cleanly so the clear
			// slab between the starting plane and the impact goes to the solution, and the
			// rest of the volume is queued for further processing
			const float SplitValue = FarthestImpactPoint[AxisIndex];
			const float HalfEpsilon = Epsilon * 0.5f;

			Remainder = CreateNavMeshVolume();
			Remainder->Bounds = RefVolume.Bounds;
			Remainder->ParentBoundsVolume = RefVolume.ParentBoundsVolume;

			if (bFarthestFromMin)
			{
				SolutionVolume.Bounds.Max[AxisIndex] = SplitValue - HalfEpsilon;
				Remainder->Bounds.Min[AxisIndex] = SplitValue;
			}
			else
			{
				SolutionVolume.Bounds.Min[AxisIndex] = SplitValue + HalfEpsilon;
				Remainder->Bounds.Max[AxisIndex] = SplitValue;
			}
		}
		else
		{
			// Impact surface is at an angle: split along the impact plane. The surface normal
			// faces back toward the clear side the trace started from, so the clip vector
			// points the opposite way, into the geometry.
			FVector ClearDirection = FarthestImpactNormal.GetSafeNormal();
			const float SweepSign = bFarthestFromMin ? 1.0f : -1.0f;
			if (ClearDirection[AxisIndex] * SweepSign > 0.0f)
			{
				ClearDirection = -ClearDirection;
			}
			SolutionVolume.ClipPoint = FarthestImpactPoint;
			SolutionVolume.ClipVector = -ClearDirection;

			// The part of the processing volume the clipped solution volume is cut from; the
			// corner split below can trim it
			FBox ClipRegion = RefVolume.Bounds;

			// If the surface ends at a corner poking into the volume, the trace that approached the
			// corner tip-first stopped exactly on it, so its impact lies on the clip plane. Bring the
			// side of the volume past the corner in to the corner, so the volume stops where the
			// clipping surface ends.
			const FTraceHit* CornerHit = nullptr;
			for (const FTraceHit& Hit : TraceHits)
			{
				if (FVector::Dist(Hit.Point, FarthestImpactPoint) <= NavGen3DClip::CornerPlaneTolerance) continue;
				if (FMath::Abs((Hit.Point - SolutionVolume.ClipPoint) | ClearDirection) <= NavGen3DClip::CornerPlaneTolerance)
				{
					CornerHit = &Hit;
					break;
				}
			}

			// Diagnostics for corner detection: every trace hit and its distance from the clip plane
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("Clip plane point:%s normal:%s"), *FVectorToString(FarthestImpactPoint), *FVectorToString(ClearDirection)));
				for (const FTraceHit& Hit : TraceHits)
				{
					AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
						FString::Printf(TEXT("  Trace axis:%d point:%s normal:%s plane dist:%.2f%s"),
							Hit.AxisIndex, *FVectorToString(Hit.Point), *FVectorToString(Hit.Normal),
							(Hit.Point - FarthestImpactPoint) | ClearDirection,
							&Hit == CornerHit ? TEXT(" <- corner") : TEXT("")));
				}
				if (!CornerHit)
				{
					AddLogMessage(ENavGen3DLogCategory::Info, ActorName, TEXT("  No corner hit found on the clip plane"));
				}
			}
			bool bFellBackToSlab = false;

			// Corner: where the clipping surface ends inside the volume. The processing volume is split
			// there with an axis aligned plane; the clipped solution volume takes the side the surface
			// is on, and the other side is queued for processing. The corner is either a trace that
			// stopped on the tip, or, when no trace reached the tip (its side started inside geometry),
			// the line where the clipping surface meets another angled surface it forms a convex
			// corner with.
			const float PlaneTolerance = NavGen3DClip::CornerPlaneTolerance;
			bool bHasCorner = false;
			FVector CornerPoint = FVector::ZeroVector;
			int32 CornerAxis = INDEX_NONE;

			if (CornerHit)
			{
				// The side to move is across an axis other than the corner trace's own, the one along
				// which the surface's impact is furthest from the corner
				float BestOffset = PlaneTolerance;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (Axis == CornerHit->AxisIndex) continue;
					const float Offset = FMath::Abs(FarthestImpactPoint[Axis] - CornerHit->Point[Axis]);
					if (Offset > BestOffset)
					{
						BestOffset = Offset;
						CornerAxis = Axis;
					}
				}
				if (CornerAxis != INDEX_NONE)
				{
					bHasCorner = true;
					CornerPoint = CornerHit->Point;
				}
			}
			else
			{
				// The other surface of a convex corner: an angled hit at a clear angle to the clipping
				// surface, with each impact on the solid side of the other's surface
				const FTraceHit* OtherFace = nullptr;
				float BestDot = NavGen3DClip::CornerMaxNormalDot;
				for (const FTraceHit& Hit : TraceHits)
				{
					if (FVector::Dist(Hit.Point, FarthestImpactPoint) <= PlaneTolerance) continue;
					if (Hit.Normal.IsNearlyZero() || NavGen3DClip::IsAxisAlignedNormal(Hit.Normal)) continue;
					const float Dot = Hit.Normal | ClearDirection;
					if (Dot >= BestDot) continue;
					if (((Hit.Point - FarthestImpactPoint) | ClearDirection) > PlaneTolerance) continue;
					if (((FarthestImpactPoint - Hit.Point) | Hit.Normal) > PlaneTolerance) continue;
					BestDot = Dot;
					OtherFace = &Hit;
				}

				FVector Intersection;
				if (OtherFace && NavGen3DClip::GetPlaneIntersectionPoint(FarthestImpactPoint, ClearDirection, OtherFace->Point, OtherFace->Normal, BoundsCenter, Intersection))
				{
					// Split across the axis that best separates the two impacts onto opposite sides of the corner
					float BestSeparation = PlaneTolerance;
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						const float OffsetA = FarthestImpactPoint[Axis] - Intersection[Axis];
						const float OffsetB = OtherFace->Point[Axis] - Intersection[Axis];
						if (OffsetA * OffsetB >= 0.0f) continue;
						const float Separation = FMath::Min(FMath::Abs(OffsetA), FMath::Abs(OffsetB));
						if (Separation > BestSeparation)
						{
							BestSeparation = Separation;
							CornerAxis = Axis;
						}
					}
					if (CornerAxis != INDEX_NONE &&
						Intersection[CornerAxis] > RefVolume.Bounds.Min[CornerAxis] + Epsilon &&
						Intersection[CornerAxis] < RefVolume.Bounds.Max[CornerAxis] - Epsilon)
					{
						bHasCorner = true;
						CornerPoint = Intersection;
					}
					else
					{
						CornerAxis = INDEX_NONE;
					}
				}
			}

			if (bHasCorner)
			{
				const float CornerValue = CornerPoint[CornerAxis];
				Remainder = CreateNavMeshVolume();
				Remainder->Bounds = RefVolume.Bounds;
				Remainder->ParentBoundsVolume = RefVolume.ParentBoundsVolume;
				RemainderAxis = CornerAxis;

				if (FarthestImpactPoint[CornerAxis] < CornerValue)
				{
					SolutionVolume.Bounds.Max[CornerAxis] = FMath::Min(SolutionVolume.Bounds.Max[CornerAxis], CornerValue);
					ClipRegion.Max[CornerAxis] = SolutionVolume.Bounds.Max[CornerAxis];
					Remainder->Bounds.Min[CornerAxis] = CornerValue;
				}
				else
				{
					SolutionVolume.Bounds.Min[CornerAxis] = FMath::Max(SolutionVolume.Bounds.Min[CornerAxis], CornerValue);
					ClipRegion.Min[CornerAxis] = SolutionVolume.Bounds.Min[CornerAxis];
					Remainder->Bounds.Max[CornerAxis] = CornerValue;
				}
				if (InDrawDebug)
				{
					AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
						FString::Printf(TEXT("Split at %s corner %s on axis %d"),
							CornerHit ? TEXT("tip") : TEXT("surface"), *FVectorToString(CornerPoint), CornerAxis));
				}
			}

			// When the clipped volume would contain geometry the clip doesn't exclude, fall back to the
			// slab the deciding trace proved clear, and queue the rest of the processing volume so it
			// is split cleanly in later iterations
			auto FallBackToTracedSlab = [&](const FString& InReason)
			{
				bFellBackToSlab = true;

				// The corner split's remainder is replaced by the one below
				delete Remainder;
				Remainder = CreateNavMeshVolume();
				Remainder->Bounds = RefVolume.Bounds;
				Remainder->ParentBoundsVolume = RefVolume.ParentBoundsVolume;
				RemainderAxis = AxisIndex;

				const float SplitValue = FarthestImpactPoint[AxisIndex];
				const float HalfEpsilon = Epsilon * 0.5f;
				SolutionVolume.Bounds = RefVolume.Bounds;
				SolutionVolume.ClipPoint = FVector::ZeroVector;
				SolutionVolume.ClipVector = FVector::ZeroVector;
				if (bFarthestFromMin)
				{
					SolutionVolume.Bounds.Max[AxisIndex] = SplitValue - HalfEpsilon;
					Remainder->Bounds.Min[AxisIndex] = SplitValue;
				}
				else
				{
					SolutionVolume.Bounds.Min[AxisIndex] = SplitValue + HalfEpsilon;
					Remainder->Bounds.Max[AxisIndex] = SplitValue;
				}
				bHasValidVolume = true;

				if (InDrawDebug)
				{
					AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
						FString::Printf(TEXT("%s; using the traced slab instead"), *InReason));
				}
			};

			// Other geometry hit by a trace: an impact on the valid side of the clipping surface,
			// inside the solution volume
			const FTraceHit* IntrudingHit = nullptr;
			for (const FTraceHit& Hit : TraceHits)
			{
				if (&Hit == CornerHit) continue;
				if (FVector::Dist(Hit.Point, FarthestImpactPoint) <= PlaneTolerance) continue;
				if (((Hit.Point - FarthestImpactPoint) | ClearDirection) <= PlaneTolerance) continue;
				if (!SolutionVolume.Bounds.IsInsideOrOn(Hit.Point)) continue;
				IntrudingHit = &Hit;
				break;
			}

			if (IntrudingHit)
			{
				FallBackToTracedSlab(FString::Printf(TEXT("Trace on axis %d hit other geometry at %s on the valid side"),
					IntrudingHit->AxisIndex, *FVectorToString(IntrudingHit->Point)));
			}
			else
			{
				bHasValidVolume = NavGen3DClip::FitBoundsToClip(SolutionVolume);

				// Other geometry no trace could see: a side whose trace started inside geometry has
				// something touching that face, with no impact reported. Sweep in reverse, from the
				// solution volume's opposite face toward that side, over only the part of the
				// cross-section on the valid side of the clip, so the sweep passes beside the clipping
				// surface. Its first hit is the near surface of that geometry; the solution volume is
				// limited there, and what's cut off goes to the remainder slabs below.
				auto LimitByReverseSweep = [&](const FPenetratingSide& InSide)
				{
					const int32 Axis = InSide.AxisIndex;
					const int32 AxisU = (Axis + 1) % 3;
					const int32 AxisV = (Axis + 2) % 3;
					FBox& Bounds = SolutionVolume.Bounds;

					// Only while the solution volume still reaches that side of the processing volume
					const float SideValue = InSide.bFromMin ? Bounds.Min[Axis] : Bounds.Max[Axis];
					const float RefSideValue = InSide.bFromMin ? RefVolume.Bounds.Min[Axis] : RefVolume.Bounds.Max[Axis];
					if (!FMath::IsNearlyEqual(SideValue, RefSideValue, Epsilon)) return;
					const float StartValue = InSide.bFromMin ? Bounds.Max[Axis] : Bounds.Min[Axis];

					// Cross-section, cut back on one face axis until it is on the valid side at both ends
					// of the sweep. Validity is linear along the sweep, so it then holds throughout.
					float MinU = Bounds.Min[AxisU], MaxU = Bounds.Max[AxisU];
					float MinV = Bounds.Min[AxisV], MaxV = Bounds.Max[AxisV];
					if (SolutionVolume.IsClipped())
					{
						const FVector& ClipPoint = SolutionVolume.ClipPoint;
						const FVector& ClipVec = SolutionVolume.ClipVector;
						// Cut across the face axis the clip plane's trace on the face is most perpendicular to
						const bool bCutV = FMath::Abs(ClipVec[AxisV]) >= FMath::Abs(ClipVec[AxisU]);
						const int32 CutAxis = bCutV ? AxisV : AxisU;
						const int32 SpanAxis = bCutV ? AxisU : AxisV;
						float& CutMin = bCutV ? MinV : MinU;
						float& CutMax = bCutV ? MaxV : MaxU;
						const float SpanMin = bCutV ? MinU : MinV;
						const float SpanMax = bCutV ? MaxU : MaxV;

						for (const float PlaneValue : { StartValue, SideValue })
						{
							// Valid where Dot(P - ClipPoint, ClipVec) <= -PlaneTolerance. Take the worst case
							// across the span axis, then solve for the cut axis.
							const float Fixed = ClipVec[Axis] * (PlaneValue - ClipPoint[Axis])
								+ FMath::Max(ClipVec[SpanAxis] * (SpanMin - ClipPoint[SpanAxis]), ClipVec[SpanAxis] * (SpanMax - ClipPoint[SpanAxis]));
							if (FMath::Abs(ClipVec[CutAxis]) < KINDA_SMALL_NUMBER)
							{
								// The clip plane is parallel to the cut axis: the whole face is valid or none is
								if (Fixed > -PlaneTolerance)
								{
									CutMax = CutMin;
								}
								continue;
							}
							const float Limit = ClipPoint[CutAxis] + (-PlaneTolerance - Fixed) / ClipVec[CutAxis];
							if (ClipVec[CutAxis] > 0.0f)
							{
								CutMax = FMath::Min(CutMax, Limit);
							}
							else
							{
								CutMin = FMath::Max(CutMin, Limit);
							}
						}
					}
					if (MaxU - MinU <= PlaneTolerance * 2.0f || MaxV - MinV <= PlaneTolerance * 2.0f)
					{
						if (InDrawDebug)
						{
							AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
								FString::Printf(TEXT("Reverse sweep toward axis %d (%s) skipped: no cross-section clear of the clip"),
									Axis, InSide.bFromMin ? TEXT("min") : TEXT("max")));
						}
						return;
					}

					FVector SweepFirst, SweepSecond;
					SweepFirst[Axis] = StartValue;
					SweepSecond[Axis] = SideValue;
					SweepFirst[AxisU] = MinU + PlaneTolerance;
					SweepSecond[AxisU] = MaxU - PlaneTolerance;
					SweepFirst[AxisV] = MinV + PlaneTolerance;
					SweepSecond[AxisV] = MaxV - PlaneTolerance;

					FVector ReverseImpact;
					bool bReverseStartPenetrating = false;
					const bool bReverseHit = PlaneTrace(SweepFirst, SweepSecond, (EAxis::Type)((int32)EAxis::X + Axis), ReverseImpact, bReverseStartPenetrating);
					const float PlaneDist = bReverseHit ? (ReverseImpact - FarthestImpactPoint) | ClearDirection : 0.0f;

					FString Outcome;
					if (bReverseHit && (!SolutionVolume.IsClipped() || PlaneDist > PlaneTolerance))
					{
						const float HalfEpsilon = Epsilon * 0.5f;
						if (InSide.bFromMin)
						{
							Bounds.Min[Axis] = FMath::Max(Bounds.Min[Axis], ReverseImpact[Axis] + HalfEpsilon);
						}
						else
						{
							Bounds.Max[Axis] = FMath::Min(Bounds.Max[Axis], ReverseImpact[Axis] - HalfEpsilon);
						}
						bHasValidVolume = NavGen3DClip::FitBoundsToClip(SolutionVolume);
						Outcome = FString::Printf(TEXT("hit geometry at %s, limited the volume there"), *FVectorToString(ReverseImpact));
					}
					else if (bReverseHit)
					{
						Outcome = FString::Printf(TEXT("hit the clipping surface at %s (plane dist %.2f)"), *FVectorToString(ReverseImpact), PlaneDist);
					}
					else if (bReverseStartPenetrating)
					{
						Outcome = TEXT("starts inside geometry");
					}
					else
					{
						Outcome = TEXT("no hit, clear");
					}

					if (InDrawDebug)
					{
						AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
							FString::Printf(TEXT("Reverse sweep toward axis %d (%s): %s"),
								Axis, InSide.bFromMin ? TEXT("min") : TEXT("max"), *Outcome));
						if (World)
						{
							const FBox SweepBox(SweepFirst.ComponentMin(SweepSecond), SweepFirst.ComponentMax(SweepSecond));
							DrawDebugBox(World, SweepBox.GetCenter(), SweepBox.GetExtent(), FColor::Cyan, false, DebugDrawTime, 0, 2.0f);
						}
					}
				};

				for (const FPenetratingSide& Side : PenetratingSides)
				{
					if (!bHasValidVolume) break;
					LimitByReverseSweep(Side);
				}
			}

			// Fitting to the clip shrinks the solution bounds to the clear side. Whatever is left of
			// the clip region outside those bounds is queued for processing as up to six slabs,
			// peeled one axis at a time so they don't overlap.
			if (bHasValidVolume && !bFellBackToSlab)
			{
				const FBox& Inner = SolutionVolume.Bounds;
				FBox Outer = ClipRegion;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (Inner.Min[Axis] > Outer.Min[Axis])
					{
						FBox Slab = Outer;
						Slab.Max[Axis] = Inner.Min[Axis];
						QueueRemainderBounds(Slab);
						Outer.Min[Axis] = Inner.Min[Axis];
					}
					if (Inner.Max[Axis] < Outer.Max[Axis])
					{
						FBox Slab = Outer;
						Slab.Min[Axis] = Inner.Max[Axis];
						QueueRemainderBounds(Slab);
						Outer.Max[Axis] = Inner.Max[Axis];
					}
				}
			}
		}

		const bool bAdded = bHasValidVolume && AddNavMeshVolume(SolutionVolume);

		if (InDrawDebug)
		{
			AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
				FString::Printf(TEXT("%s %s solution volume ID:%llu Extent:%s Normal:%s"),
					bAdded ? TEXT("Added") : TEXT("Rejected"),
					SolutionVolume.IsClipped() ? TEXT("clipped") : TEXT("axis aligned"),
					SolutionVolume.ID, *SolutionVolume.Bounds.GetExtent().ToString(), *FVectorToString(FarthestImpactNormal)));
		}

		if (Remainder)
		{
			const FVector RemainderSize = Remainder->Bounds.GetSize();
			const bool bMadeProgress = RemainderSize[RemainderAxis] < RefVolume.Bounds.GetSize()[RemainderAxis];
			if (bMadeProgress && FMath::Max3(RemainderSize.X, RemainderSize.Y, RemainderSize.Z) >= GetProcessMinVolumeSize(Remainder->Bounds, Remainder->ParentBoundsVolume))
			{
				ProcessVolumesList.Add(Remainder);
				if (InDrawDebug)
				{
					AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
						FString::Printf(TEXT("Queued remainder volume Extent:%s"), *Remainder->Bounds.GetExtent().ToString()));
					if (World)
					{
						DrawDebugBox(World, Remainder->Bounds.GetCenter(), Remainder->Bounds.GetExtent(), FColor::Red, false, DebugDrawTime, 0, 5.0f);
					}
				}
			}
			else
			{
				delete Remainder;
			}
		}
	}
	else
	{
		// Every trace that found anything started inside geometry, so there is no impact to split
		// on. For each such side, step inward from the face testing a thin slice across the whole
		// cross-section, to find where the geometry touching that face ends: the first clear slice.
		// Split the volume at the shallowest one and queue both parts. The part beyond the split
		// now has a clear face on that side, so its trace finds real impacts on the next pass.
		const FVector Size = RefVolume.Bounds.GetSize();
		const float PlaneTolerance = NavGen3DClip::CornerPlaneTolerance;

		auto FindClearSlice = [&](const FPenetratingSide& InSide, float& OutSliceValue) -> bool
		{
			if (!World) return false;
			const int32 Axis = InSide.AxisIndex;
			const float Step = FMath::Max(Epsilon, Size[Axis] / NavGen3DClip::ClearSliceSteps);
			const float FaceValue = InSide.bFromMin ? RefVolume.Bounds.Min[Axis] : RefVolume.Bounds.Max[Axis];
			const float Direction = InSide.bFromMin ? 1.0f : -1.0f;

			FVector SliceHalfExtent = (Size * 0.5f - FVector(PlaneTolerance)).ComponentMax(FVector(KINDA_SMALL_NUMBER));
			SliceHalfExtent[Axis] = FMath::Min(PlaneTolerance * 0.5f, Step * 0.25f);
			const FCollisionShape SliceShape = FCollisionShape::MakeBox(SliceHalfExtent);

			FVector SliceCenter = RefVolume.Bounds.GetCenter();
			for (float Depth = Step; Depth < Size[Axis] - Step * 0.5f; Depth += Step)
			{
				SliceCenter[Axis] = FaceValue + Direction * Depth;
				// Blocking only, matching what PlaneTrace's sweeps stop on
				if (!World->OverlapBlockingTestByChannel(SliceCenter, FQuat::Identity, ECC_WorldStatic, SliceShape))
				{
					OutSliceValue = SliceCenter[Axis];
					return true;
				}
			}
			return false;
		};

		const FPenetratingSide* BestSide = nullptr;
		float BestSliceValue = 0.0f;
		float BestDepth = FLT_MAX;
		for (const FPenetratingSide& Side : PenetratingSides)
		{
			float SliceValue;
			if (!FindClearSlice(Side, SliceValue)) continue;
			const float FaceValue = Side.bFromMin ? RefVolume.Bounds.Min[Side.AxisIndex] : RefVolume.Bounds.Max[Side.AxisIndex];
			const float Depth = FMath::Abs(SliceValue - FaceValue);
			if (Depth < BestDepth)
			{
				BestDepth = Depth;
				BestSide = &Side;
				BestSliceValue = SliceValue;
			}
		}

		if (BestSide)
		{
			auto QueuePart = [&](const FBox& InBounds)
			{
				const FVector PartSize = InBounds.GetSize();
				if (PartSize.X <= Epsilon || PartSize.Y <= Epsilon || PartSize.Z <= Epsilon) return;
				if (FMath::Max3(PartSize.X, PartSize.Y, PartSize.Z) < GetProcessMinVolumeSize(InBounds, RefVolume.ParentBoundsVolume)) return;
				NavMeshVolumeBase* Part = CreateNavMeshVolume();
				Part->Bounds = InBounds;
				Part->ParentBoundsVolume = RefVolume.ParentBoundsVolume;
				ProcessVolumesList.Add(Part);
			};

			const int32 Axis = BestSide->AxisIndex;
			FBox LowPart = RefVolume.Bounds;
			FBox HighPart = RefVolume.Bounds;
			LowPart.Max[Axis] = BestSliceValue;
			HighPart.Min[Axis] = BestSliceValue;
			QueuePart(LowPart);
			QueuePart(HighPart);

			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("All traces start inside geometry; side on axis %d (%s) clears at depth %.1f, split there"),
						Axis, BestSide->bFromMin ? TEXT("min") : TEXT("max"), BestDepth));
				if (World)
				{
					FVector PlaneExtent = RefVolume.Bounds.GetExtent();
					PlaneExtent[Axis] = 0.0f;
					FVector PlaneCenter = RefVolume.Bounds.GetCenter();
					PlaneCenter[Axis] = BestSliceValue;
					DrawDebugBox(World, PlaneCenter, PlaneExtent, FColor::Cyan, false, DebugDrawTime, 0, 3.0f);
				}
			}
		}
		else
		{
			// No clear slice on any side, e.g. geometry passing right through the volume: halve it
			// along its longest axis so later passes can isolate the geometry
			if (InDrawDebug)
			{
				AddLogMessage(ENavGen3DLogCategory::Info, ActorName,
					FString::Printf(TEXT("All traces start inside geometry and no side clears, halving Extent:%s"), *RefVolume.Bounds.GetExtent().ToString()));
			}
			SplitAlongLongestAxis(RefVolume, InDrawDebug, ActorName);
		}
	}

	return false;
}

NavMeshVolumeBase* UNavGen3DSubsystem::FindVolumeContainingLocation(const FVector& InLocation)
{
	const int32 GridSize = GetDefault<UNavGen3DSettings>()->MaxVolumeSize;
	const int32 IX = FMath::FloorToInt(InLocation.X / GridSize);
	const int32 IY = FMath::FloorToInt(InLocation.Y / GridSize);
	const int32 IZ = FMath::FloorToInt(InLocation.Z / GridSize);

	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DZ = -1; DZ <= 1; ++DZ)
			{
				const uint64 Hash = ((uint64)((IX + DX) & 0xFFFF) << 32)
								  | ((uint64)((IY + DY) & 0xFFFF) << 16)
								  |  (uint64)((IZ + DZ) & 0xFFFF);
				if (const TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(Hash))
				{
					for (uint64 VolumeID : *VolumeIDs)
					{
						if (NavMeshVolumeBase* Volume = FindNavMeshVolumeByID(VolumeID))
						{
							if (Volume->Bounds.IsInsideOrOn(InLocation))
							{
								return Volume;
							}
						}
					}
				}
			}
		}
	}
	return nullptr;
}

NavMeshVolumeBase* UNavGen3DSubsystem::FindClosestVolumeContainingLocation(int32 InAgentIndex, const FVector& InLocation, int32 InConnectivityID, bool bPaddedRadius)
{
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(InAgentIndex, bPaddedRadius));

	const TMap<uint64, TArray<FNavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(InAgentIndex);
	UWorld* World = FindWorld();

	const int32 GridSize = GetDefault<UNavGen3DSettings>()->MaxVolumeSize;
	NavMeshVolumeBase* ClosestVolume = nullptr;
	float ClosestDistSq = (float)GridSize * (float)GridSize;

	const int32 IX = FMath::FloorToInt(InLocation.X / GridSize);
	const int32 IY = FMath::FloorToInt(InLocation.Y / GridSize);
	const int32 IZ = FMath::FloorToInt(InLocation.Z / GridSize);

	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DZ = -1; DZ <= 1; ++DZ)
			{
				const uint64 Hash = ((uint64)((IX + DX) & 0xFFFF) << 32)
				                  | ((uint64)((IY + DY) & 0xFFFF) << 16)
				                  |  (uint64)((IZ + DZ) & 0xFFFF);

				const TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(Hash);
				if (!VolumeIDs) { continue; }

				for (uint64 VolumeID : *VolumeIDs)
				{
					NavMeshVolumeBase* Volume = FindNavMeshVolumeByID(VolumeID);
					if (!Volume) { continue; }

					if (AgentMap && !AgentMap->Contains(Volume->ID))
					{
						continue;
					}

					if (InConnectivityID > 0)
					{
						const int32* ConnID = Volume->ConnectivityIDByAgent.Find(InAgentIndex);
						if (!ConnID || *ConnID != InConnectivityID)
						{
							continue;
						}
					}

					const FVector VolumeCenter = Volume->Bounds.GetCenter();
					const float DistSq = FVector::DistSquared(VolumeCenter, InLocation);
					if (DistSq >= ClosestDistSq)
					{
						continue;
					}

					if (Volume->Bounds.IsInsideOrOn(InLocation))
					{
						ClosestDistSq = DistSq;
						ClosestVolume = Volume;
					}
					else if (World)
					{
						FHitResult HitResult;
						if (!World->SweepSingleByChannel(HitResult, InLocation, VolumeCenter, FQuat::Identity, ECC_WorldStatic, Sphere))
						{
							ClosestDistSq = DistSq;
							ClosestVolume = Volume;
						}
						else
						{
							// Ignore hits whose impact point is away from the volume center —
							// geometry behind InLocation should not block access to the volume.
							const FVector ToCenter = VolumeCenter - InLocation;
							const FVector ToImpact = HitResult.ImpactPoint - InLocation;
							if (FVector::DotProduct(ToCenter, ToImpact) <= 0.0f)
							{
								ClosestDistSq = DistSq;
								ClosestVolume = Volume;
							}
						}
					}
				}
			}
		}
	}

	return ClosestVolume;
}

bool UNavGen3DSubsystem::PathFind(FPathSearchSpace& InSearchSpace)
{
	if (InSearchSpace.Status != EPathSearchStatus::InProgress)
	{
		return false;
	}

	InSearchSpace.Status = EPathSearchStatus::InProgress;
	InSearchSpace.NodeHeap.Reset();
	InSearchSpace.PathSolution.Reset();
	InSearchSpace.PathVolumeIDs.Reset();
	InSearchSpace.PathConnectionAxes.Reset();

	const TMap<uint64, TArray<FNavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(InSearchSpace.AgentIndex);
	if (!AgentMap)
	{
		InSearchSpace.Status = EPathSearchStatus::Fail;
		return false;
	}

	const NavMeshVolumeBase* DestVolume = FindNavMeshVolumeByID(InSearchSpace.DestinationID);
	const NavMeshVolumeBase* OriginVolume = FindNavMeshVolumeByID(InSearchSpace.OriginID);
	if (!DestVolume || !OriginVolume)
	{
		InSearchSpace.Status = EPathSearchStatus::Fail;
		return false;
	}

	const FVector DestCenter = DestVolume->Bounds.GetCenter();

	TMap<uint64, uint64> CameFrom;
	TMap<uint64, FVector> ConnectionLocations;
	TMap<uint64, int32> ConnectionAxisMap;
	TMap<uint64, float> GCost;
	TSet<uint64> ClosedSet;

	auto HeapPredicate = [](const FPathSearchNode& A, const FPathSearchNode& B)
	{
		return (A.DistFromSource + A.EstDistToTarget) < (B.DistFromSource + B.EstDistToTarget);
	};

	GCost.Add(InSearchSpace.OriginID, 0.0f);

	FPathSearchNode StartNode;
	StartNode.VolumeID = InSearchSpace.OriginID;
	StartNode.DistFromSource = 0.0f;
	StartNode.EstDistToTarget = FVector::Dist(OriginVolume->Bounds.GetCenter(), DestCenter);
	InSearchSpace.NodeHeap.HeapPush(StartNode, HeapPredicate);

	while (!InSearchSpace.NodeHeap.IsEmpty())
	{
		FPathSearchNode Current;
		InSearchSpace.NodeHeap.HeapPop(Current, HeapPredicate);

		const uint64 CurrentID = Current.VolumeID;

		if (ClosedSet.Contains(CurrentID))
		{
			continue;
		}
		ClosedSet.Add(CurrentID);

		if (CurrentID == InSearchSpace.DestinationID)
		{
			uint64 TraceID = InSearchSpace.DestinationID;
			bool bPathIntact = true;
			while (TraceID != InSearchSpace.OriginID)
			{
				if (const FVector* Loc = ConnectionLocations.Find(TraceID))
				{
					InSearchSpace.PathSolution.Insert(*Loc, 0);
				}
				InSearchSpace.PathVolumeIDs.Insert(TraceID, 0);
				if (const int32* Axis = ConnectionAxisMap.Find(TraceID))
				{
					InSearchSpace.PathConnectionAxes.Insert(*Axis, 0);
				}
				const uint64* Prev = CameFrom.Find(TraceID);
				if (!Prev)
				{
					bPathIntact = false;
					break;
				}
				TraceID = *Prev;
			}
			if (!bPathIntact)
			{
				InSearchSpace.PathSolution.Reset();
				InSearchSpace.PathVolumeIDs.Reset();
				InSearchSpace.PathConnectionAxes.Reset();
				InSearchSpace.Status = EPathSearchStatus::Fail;
				return false;
			}
			InSearchSpace.PathSolution.Insert(InSearchSpace.Origin, 0);
			InSearchSpace.PathVolumeIDs.Insert(InSearchSpace.OriginID, 0);
			InSearchSpace.PathConnectionAxes.Insert(0, 0);
			InSearchSpace.PathSolution.Add(InSearchSpace.Destination);
			InSearchSpace.PathVolumeIDs.Add(InSearchSpace.DestinationID);
			InSearchSpace.PathConnectionAxes.Add(0);
			InSearchSpace.Status = EPathSearchStatus::Success;
			FindPathPostProcess(InSearchSpace);
			return true;
		}

		const TArray<FNavVolumeConnection>* Connections = AgentMap->Find(CurrentID);
		if (!Connections) continue;

		const NavMeshVolumeBase* CurrentVolume = FindNavMeshVolumeByID(CurrentID);
		if (!CurrentVolume) continue;

		const float CurrentGCost = GCost.FindRef(CurrentID);

		for (const FNavVolumeConnection& Conn : *Connections)
		{
			const uint64 NeighborID = Conn.ID;
			if (ClosedSet.Contains(NeighborID)) continue;

			const NavMeshVolumeBase* NeighborVolume = FindNavMeshVolumeByID(NeighborID);
			if (!NeighborVolume) continue;

			const float NewGCost = CurrentGCost + FVector::Dist(CurrentVolume->Bounds.GetCenter(), Conn.Location);
			const float* ExistingGCost = GCost.Find(NeighborID);
			if (ExistingGCost && NewGCost >= *ExistingGCost) continue;

			GCost.Add(NeighborID, NewGCost);
			CameFrom.Add(NeighborID, CurrentID);
			ConnectionLocations.Add(NeighborID, Conn.Location);
			ConnectionAxisMap.Add(NeighborID, Conn.ConnectionAxis);

			FPathSearchNode NeighborNode;
			NeighborNode.VolumeID = NeighborID;
			NeighborNode.DistFromSource = NewGCost;
			NeighborNode.EstDistToTarget = FVector::Dist(NeighborVolume->Bounds.GetCenter(), DestCenter);
			InSearchSpace.NodeHeap.HeapPush(NeighborNode, HeapPredicate);
		}
	}

	InSearchSpace.Status = EPathSearchStatus::Fail;
	return false;
}

bool UNavGen3DSubsystem::RegisterPathSearchSpace(const TSharedPtr<FPathSearchSpace>& InSearchSpace)
{
	if (!ensureAlways(IsInGameThread()))
	{
		return false;
	}

	if (!InSearchSpace.IsValid())
	{
		return false;
	}

	if (InSearchSpace->Status != EPathSearchStatus::Initialized)
	{
		return false;
	}

	// Reject duplicates — compare by pointer identity.
	for (const TSharedPtr<FPathSearchSpace>& Existing : PathSearchList)
	{
		if (Existing == InSearchSpace)
		{
			return false;
		}
	}

	InSearchSpace->Status = EPathSearchStatus::InProgress;
	PathSearchList.Add(InSearchSpace);
	return true;
}

void UNavGen3DSubsystem::ProcessPathSearchList()
{
	// Collect InProgress entries into a local array; drop everything else from the member list.
	TArray<TSharedPtr<FPathSearchSpace>> LocalList;
	for (int32 i = PathSearchList.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<FPathSearchSpace>& Entry = PathSearchList[i];
		if (Entry.IsValid() && Entry->Status == EPathSearchStatus::InProgress)
		{
			LocalList.Add(Entry);
		}
		else
		{
			PathSearchList.RemoveAtSwap(i);
		}
	}

	if (LocalList.IsEmpty())
	{
		return;
	}

	// Run PathFind for each search space in parallel on the thread pool.
	// PathFind only reads the shared navmesh data; each FPathSearchSpace is written
	// exclusively by its own thread, so no locking is needed.
	ParallelFor(LocalList.Num(), [this, &LocalList](int32 Index)
	{
		PathFind(*LocalList[Index]);
	});

	// Remove entries whose status is no longer InProgress after the parallel pass.
	for (int32 i = PathSearchList.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<FPathSearchSpace>& Entry = PathSearchList[i];
		if (!Entry.IsValid() || Entry->Status != EPathSearchStatus::InProgress)
		{
			PathSearchList.RemoveAtSwap(i);
		}
	}
}

void UNavGen3DSubsystem::FindPathPostProcess(FPathSearchSpace& InSearchSpace)
{
	if (!PathSmoothingEnabled)
	{
		return;
	}
	if (InSearchSpace.Status != EPathSearchStatus::Success || InSearchSpace.PathSolution.Num() < 2)
	{
		return;
	}

	const float PaddedRadius = GetAgentCollisionRadius(InSearchSpace.AgentIndex, true);

	TArray<FVector>& Path = InSearchSpace.PathSolution;

	// Capture before the loop — Path grows but PathConnectionAxes/PathVolumeIDs stay fixed.
	const int32 LastConnIndex = InSearchSpace.PathConnectionAxes.Num() - 2;

	// Iterate backwards so insertions don't shift indices we haven't visited yet.
	// Per connection: insert after-node first (at i+1), then before-node (at i).
	// This order keeps the connection at index i stable when we insert the before-node.
	for (int32 i = LastConnIndex; i >= 1; --i)
	{
		const int32 ConnAxis = InSearchSpace.PathConnectionAxes[i];
		if (ConnAxis == 0) continue;

		const int32 AxisIdx = FMath::Abs(ConnAxis) - 1;
		FVector ConnDir = FVector::ZeroVector;
		ConnDir[AxisIdx] = ConnAxis > 0 ? 1.0f : -1.0f;

		const FVector ConnPoint = Path[i];

		// After-connection node: into the volume being entered.
		if (const NavMeshVolumeBase* EnteredVol = FindNavMeshVolumeByID(InSearchSpace.PathVolumeIDs[i]))
		{
			const FVector VolumeCenter = EnteredVol->Bounds.GetCenter();
			const float RightAngleDist = FVector::DotProduct(ConnDir, VolumeCenter - ConnPoint);
			const float InsertDist = FMath::Max(PaddedRadius, RightAngleDist);
			Path.Insert(ConnPoint + ConnDir * InsertDist, i + 1);
		}

		// Before-connection node: into the volume being left.
		if (const NavMeshVolumeBase* LeavingVol = FindNavMeshVolumeByID(InSearchSpace.PathVolumeIDs[i - 1]))
		{
			const FVector VolumeCenter = LeavingVol->Bounds.GetCenter();
			const float RightAngleDist = FVector::DotProduct(ConnDir, ConnPoint - VolumeCenter);
			const float InsertDist = FMath::Min(PaddedRadius, RightAngleDist);
			Path.Insert(ConnPoint - ConnDir * InsertDist, i);
		}
	}

	// Simplification pass: backward string-pull with unpadded sphere trace.
	UWorld* World = FindWorld();
	if (!World || Path.Num() < 3)
	{
		return;
	}

	const FCollisionShape UnpaddedSphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(InSearchSpace.AgentIndex, false));

	int32 ThreadEndIdx = Path.Num() - 1;

	while (ThreadEndIdx >= 2)
	{
		int32 ThreadStartIdx = ThreadEndIdx - 2;
		int32 LastValidThreadStart = ThreadEndIdx - 1;

		while (ThreadStartIdx >= 0)
		{
			FHitResult Hit;
			const bool bHit = World->SweepSingleByChannel(Hit, Path[ThreadEndIdx], Path[ThreadStartIdx], FQuat::Identity, ECC_WorldStatic, UnpaddedSphere);

			if (!bHit)
			{
				LastValidThreadStart = ThreadStartIdx;
				--ThreadStartIdx;
			}
			else
			{
				break;
			}
		}

		const int32 NumToRemove = ThreadEndIdx - LastValidThreadStart - 1;
		if (NumToRemove > 0)
		{
			Path.RemoveAt(LastValidThreadStart + 1, NumToRemove);
		}

		ThreadEndIdx = LastValidThreadStart;
	}
}

void UNavGen3DSubsystem::DebugFindPath(FVector InOrigin, FVector InDestination)
{
	if (InOrigin != InvalidLocation)
	{
		DebugPathOrigin = InOrigin;
	}

	if (DebugPathOrigin == InvalidLocation)
	{
		AddLogMessage(ENavGen3DLogCategory::Warning, TEXT(""), TEXT("Set DebugPathOrigin"));
		return;
	}

	if (InDestination != InvalidLocation)
	{
		DebugPathDestination = InDestination;
	}
	else if (DebugPathDestination == InvalidLocation)
	{
		DebugPathDestination = GetCameraLocation();
	}

	DebugPathSearchSpace.Initialize(this, DebugNavMeshAgentIndex, DebugPathOrigin, DebugPathDestination);
	PathFind(DebugPathSearchSpace);
}

bool UNavGen3DSubsystem::DebugValidatePath(const FPathSearchSpace& InSearchSpace)
{
	if (InSearchSpace.Status != EPathSearchStatus::Success)
	{
		return false;
	}

	UWorld* World = FindWorld();
	if (!World)
	{
		return false;
	}

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(InSearchSpace.AgentIndex, false));
	bool bValid = true;

	const TArray<FVector>& Path = InSearchSpace.PathSolution;
	for (int32 i = 0; i < Path.Num() - 1; ++i)
	{
		FHitResult HitResult;
		if (World->SweepSingleByChannel(HitResult, Path[i], Path[i + 1], FQuat::Identity, ECC_WorldStatic, Sphere))
		{
			AddLogMessage(ENavGen3DLogCategory::Warning, TEXT("PathValidation"),
				FString::Printf(TEXT("Collision at %s"), *FVectorToString(HitResult.ImpactPoint)));
			DrawDebugSphere(World, HitResult.ImpactPoint, 15.0f, 8, FColor::Red, false, 30.0f);
			bValid = false;
		}
	}

	return bValid;
}

void FPathSearchSpace::DebugDrawPath(float InDrawTime) const
{
	if (PathSolution.IsEmpty())
	{
		return;
	}

	UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>();
	UWorld* World = Subsystem ? Subsystem->FindWorld() : nullptr;
	if (!World)
	{
		return;
	}

	for (int32 i = 0; i < PathSolution.Num(); ++i)
	{
		const bool bIntermediate = bIsIntermediate.IsValidIndex(i) && bIsIntermediate[i];
		const FColor NodeColor = bIntermediate ? FColor::Orange : FColor::Cyan;
		DrawDebugSphere(World, PathSolution[i], 10.0f, 8, NodeColor, false, InDrawTime);
		if (i > 0)
		{
			DrawDebugLine(World, PathSolution[i - 1], PathSolution[i], FColor::Cyan, false, InDrawTime, 0, 3.0f);
		}
	}
}

void FPathSearchSpace::Reset()
{
	AgentIndex = -1;
	OriginID = 0;
	DestinationID = 0;
	Origin = UNavGen3DSubsystem::InvalidLocation;
	Destination = UNavGen3DSubsystem::InvalidLocation;
	PathSolution.Reset();
	PathVolumeIDs.Reset();
	PathConnectionAxes.Reset();
	bIsIntermediate.Reset();
	NodeHeap.Reset();
	Status = EPathSearchStatus::None;
}

void FPathSearchSpace::Initialize(UNavGen3DSubsystem* InSubsystem, int32 InAgentIndex, const FVector& PathOrigin, const FVector& PathDestination)
{
	AgentIndex       = InAgentIndex;
	Origin           = PathOrigin;
	Destination      = PathDestination;
	OriginID         = 0;
	DestinationID    = 0;
	PathSolution.Reset();
	PathVolumeIDs.Reset();
	PathConnectionAxes.Reset();
	bIsIntermediate.Reset();
	NodeHeap.Reset();

	if (InSubsystem)
	{
		int32 OriginConnectivityID = 0;
		if (const NavMeshVolumeBase* OriginVolume = InSubsystem->FindClosestVolumeContainingLocation(InAgentIndex, PathOrigin))
		{
			OriginID = OriginVolume->ID;
			if (const int32* ConnID = OriginVolume->ConnectivityIDByAgent.Find(InAgentIndex))
			{
				OriginConnectivityID = *ConnID;
			}
		}

		// If the destination lands inside a known solution volume, check connectivity before
		// spending time on A*. A mismatch means the two regions are provably disconnected.
		if (const NavMeshVolumeBase* DirectDestVolume = InSubsystem->FindVolumeContainingLocation(PathDestination))
		{
			const int32* DestConnID = DirectDestVolume->ConnectivityIDByAgent.Find(InAgentIndex);
			if (!DestConnID || (OriginConnectivityID > 0 && *DestConnID != OriginConnectivityID))
			{
				Status = EPathSearchStatus::Fail;
				return;
			}
			DestinationID = DirectDestVolume->ID;
		}
		else
		{
			// Destination is not inside any volume — find the closest connected volume in the
			// same connectivity group. Use unpadded radius for the sweep so we accept volumes
			// that are only reachable to the agent's actual body, not its padded collision.
			if (const NavMeshVolumeBase* DestinationVolume = InSubsystem->FindClosestVolumeContainingLocation(InAgentIndex, PathDestination, OriginConnectivityID, false))
			{
				DestinationID = DestinationVolume->ID;
			}
		}
	}

	Status = (OriginID != 0 && DestinationID != 0) ? EPathSearchStatus::Initialized : EPathSearchStatus::Fail;
}

NavMeshVolumeBase* UNavGen3DSubsystem::FindGenerationVolumeContainingLocation(const FVector& InLocation, bool InRemoveFromProcessing)
{
	for (int32 i = 0; i < ProcessVolumesList.Num(); ++i)
	{
		if (ProcessVolumesList[i]->Bounds.IsInsideOrOn(InLocation))
		{
			if (InRemoveFromProcessing)
			{
				NavMeshVolumeBase* Result = ProcessVolumesList[i];
				ProcessVolumesList.RemoveAtSwap(i);
				return Result;
			}
			return ProcessVolumesList[i]->Clone();
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
				NavMeshVolumeBase* Result = CreateNavMeshVolume();
				Result->Bounds = FBox::BuildAABB(Origin, Extent);
				Result->ParentBoundsVolume = BoundsVolume;
				return Result;
			}
		}
	}

	return nullptr;
}

NavMeshVolumeBase* UNavGen3DSubsystem::FindClosestGenerationVolume(const FVector& InLocation, bool InRemoveFromProcessing)
{
	if (!ProcessVolumesList.IsEmpty())
	{
		float ClosestDistSq = FLT_MAX;
		int32 ClosestIdx = INDEX_NONE;
		for (int32 i = 0; i < ProcessVolumesList.Num(); ++i)
		{
			const float DistSq = FVector::DistSquared(ProcessVolumesList[i]->Bounds.GetCenter(), InLocation);
			if (DistSq < ClosestDistSq)
			{
				ClosestDistSq = DistSq;
				ClosestIdx = i;
			}
		}

		if (ClosestIdx != INDEX_NONE)
		{
			if (InRemoveFromProcessing)
			{
				NavMeshVolumeBase* Result = ProcessVolumesList[ClosestIdx];
				ProcessVolumesList.RemoveAtSwap(ClosestIdx);
				return Result;
			}
			return ProcessVolumesList[ClosestIdx]->Clone();
		}
		return nullptr;
	}

	float ClosestDistSq = FLT_MAX;
	FBox ClosestBounds;
	ANavGen3DBoundsVolume* ClosestBoundsVolume = nullptr;
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
			ClosestBounds = Box;
			ClosestBoundsVolume = BoundsVolume;
			bFound = true;
		}
	}

	if (!bFound) return nullptr;
	NavMeshVolumeBase* Result = CreateNavMeshVolume();
	Result->Bounds = ClosestBounds;
	Result->ParentBoundsVolume = ClosestBoundsVolume;
	return Result;
}

NavMeshVolumeBase* UNavGen3DSubsystem::PopNextProcessingVolume()
{
	if (ProcessVolumesList.IsEmpty()) return nullptr;
	NavMeshVolumeBase* Result = ProcessVolumesList[0];
	ProcessVolumesList.RemoveAt(0);
	return Result;
}

void UNavGen3DSubsystem::DebugProcessSingleVolume(NavMeshVolumeBase& InVolume, bool bDrawDebug)
{
	// Snapshots are deep copies so the undo state owns its volumes independently
	ClearDebugUndoState();
	DebugUndoState.ProcessedVolume = InVolume.Clone();
	for (const auto& Pair : NavMeshSolutionMap)
	{
		DebugUndoState.SolutionMapSnapshot.Add(Pair.Key, Pair.Value->Clone());
	}
	DebugUndoState.SolutionByLocationSnapshot = NavMeshSolutionMapByLocation;
	DebugUndoState.VolumeMap_X_Snapshot = NavMeshVolumeMap_X;
	DebugUndoState.VolumeMap_Y_Snapshot = NavMeshVolumeMap_Y;
	DebugUndoState.VolumeMap_Z_Snapshot = NavMeshVolumeMap_Z;
	DebugUndoState.IDGeneratorSnapshot = NavMeshVolumeIDGenerator;
	DebugUndoState.ProcessListCountBefore = ProcessVolumesList.Num();
	DebugUndoState.bIsValid = false;

	ProcessNavMeshVolume(InVolume, bDrawDebug);

	DebugUndoState.bIsValid = true;
}

void UNavGen3DSubsystem::DebugUndoLastProcess()
{
	if (!DebugUndoState.bIsValid) return;

	// Snapshot volumes are handed back to the solution map, so the undo state no longer owns them
	ClearSolutionVolumes();
	NavMeshSolutionMap = MoveTemp(DebugUndoState.SolutionMapSnapshot);
	DebugUndoState.SolutionMapSnapshot.Reset();
	NavMeshSolutionMapByLocation = DebugUndoState.SolutionByLocationSnapshot;
	NavMeshVolumeMap_X = DebugUndoState.VolumeMap_X_Snapshot;
	NavMeshVolumeMap_Y = DebugUndoState.VolumeMap_Y_Snapshot;
	NavMeshVolumeMap_Z = DebugUndoState.VolumeMap_Z_Snapshot;
	NavMeshVolumeIDGenerator = DebugUndoState.IDGeneratorSnapshot;

	const int32 AddedCount = ProcessVolumesList.Num() - DebugUndoState.ProcessListCountBefore;
	if (AddedCount > 0)
	{
		for (int32 i = ProcessVolumesList.Num() - AddedCount; i < ProcessVolumesList.Num(); ++i)
		{
			delete ProcessVolumesList[i];
		}
		ProcessVolumesList.RemoveAt(ProcessVolumesList.Num() - AddedCount, AddedCount);
	}

	ProcessVolumesList.Insert(DebugUndoState.ProcessedVolume, 0);
	DebugUndoState.ProcessedVolume = nullptr;

	DebugUndoState.bIsValid = false;
}

void UNavGen3DSubsystem::RemoveNavMeshVolume(uint64 InID)
{
	if (NavMeshVolumeBase* Volume = FindNavMeshVolumeByID(InID))
	{
		const FVector Center = Volume->Bounds.GetCenter();
		const uint64 NextX = Volume->Next_X;
		const uint64 NextY = Volume->Next_Y;
		const uint64 NextZ = Volume->Next_Z;

		const uint64 LocationHash = CalculateHash3D(Center);
		if (TArray<uint64>* VolumeIDs = NavMeshSolutionMapByLocation.Find(LocationHash))
		{
			VolumeIDs->Remove(InID);
			if (VolumeIDs->IsEmpty())
			{
				NavMeshSolutionMapByLocation.Remove(LocationHash);
			}
		}

		auto RemoveFromAxisList = [&](TMap<uint64, uint64>& InMap, uint64 InHash, int32 InAxis, uint64 InNextID)
		{
			uint64* HeadID = InMap.Find(InHash);
			if (!HeadID) return;
			if (*HeadID == InID) { *HeadID = InNextID; return; }
			uint64 CurrID = *HeadID;
			while (CurrID != 0)
			{
				NavMeshVolumeBase* Curr = FindNavMeshVolumeByID(CurrID);
				if (!Curr) break;
				uint64 CurrNext;
				switch (InAxis)
				{
					case 0: CurrNext = Curr->Next_X; break;
					case 1: CurrNext = Curr->Next_Y; break;
					case 2: CurrNext = Curr->Next_Z; break;
					default: return;
				}
				if (CurrNext == InID)
				{
					switch (InAxis)
					{
						case 0: Curr->Next_X = InNextID; break;
						case 1: Curr->Next_Y = InNextID; break;
						case 2: Curr->Next_Z = InNextID; break;
					}
					return;
				}
				CurrID = CurrNext;
			}
		};

		RemoveFromAxisList(NavMeshVolumeMap_X, CalculateHash3D(FVector(0.0f, Center.Y, Center.Z)), 0, NextX);
		RemoveFromAxisList(NavMeshVolumeMap_Y, CalculateHash3D(FVector(Center.X, 0.0f, Center.Z)), 1, NextY);
		RemoveFromAxisList(NavMeshVolumeMap_Z, CalculateHash3D(FVector(Center.X, Center.Y, 0.0f)), 2, NextZ);

		delete Volume;
	}

	NavMeshSolutionMap.Remove(InID);
}

bool UNavGen3DSubsystem::ValidateConnectionCollision(const FCollisionShape& InSphere, const FVector& InStart, const FVector& InEnd)
{
	UWorld* World = FindWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(Hit, InStart, InEnd, FQuat::Identity, ECC_WorldStatic, InSphere);

	return !bHit;
}

bool UNavGen3DSubsystem::SweepAgentLOS(int32 InAgentIndex, const FVector& InFrom, const FVector& InTo, AActor* InIgnoreActor) const
{
	UWorld* World = FindWorld();
	if (!World)
	{
		return false;
	}

	const float AgentRadius = GetAgentCollisionRadius(InAgentIndex, /*bPadded=*/false);
	const FVector Dir   = (InTo - InFrom).GetSafeNormal();
	const FVector Start = InFrom + Dir * AgentRadius;
	const FVector End   = InTo   - Dir * AgentRadius;

	FCollisionQueryParams QueryParams;
	if (InIgnoreActor)
	{
		QueryParams.AddIgnoredActor(InIgnoreActor);
	}
	for (const TWeakObjectPtr<ANavGen3DBoundsVolume>& BoundsVolume : BoundsVolumes)
	{
		if (BoundsVolume.IsValid())
		{
			QueryParams.AddIgnoredActor(BoundsVolume.Get());
		}
	}

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(AgentRadius), QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.Component.IsValid())
		{
			continue;
		}
		// Skip query-only components (trigger volumes, overlap zones) — only solid
		// geometry with physics collision can physically block the agent.
		const ECollisionEnabled::Type CollisionType = Hit.Component->GetCollisionEnabled();
		if (CollisionType != ECollisionEnabled::QueryAndPhysics && CollisionType != ECollisionEnabled::PhysicsOnly)
		{
			continue;
		}
		return false;
	}

	return true;
}

bool UNavGen3DSubsystem::PlaneTrace(FVector InMin, FVector InMax, EAxis::Type InAxis, FVector& OutImpactPoint, bool& OutStartPenetrating, FVector* OutImpactNormal)
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

			// Verification: reverse-sweep from just before the impact back to Start.
			// If the original Start was embedded in geometry, the forward sweep can
			// incorrectly report a non-penetrating hit further along. Sweeping back
			// toward Start will detect any geometry that sits between Start and the
			// reported impact, which the forward sweep should have caught first.
			const float AxisStart = Start.GetComponentForAxis(InAxis);
			const float AxisHit   = OutImpactPoint.GetComponentForAxis(InAxis);
			const float VerifyAxisStart = AxisHit - Epsilon;
			if (VerifyAxisStart > AxisStart)
			{
				FVector VerifyStart = Start;
				VerifyStart.SetComponentForAxis(InAxis, VerifyAxisStart);

				FHitResult VerifyHit;
				if (World->SweepSingleByChannel(VerifyHit, VerifyStart, Start, FQuat::Identity, ECC_WorldStatic, BoxShape))
				{
					OutStartPenetrating = true;
					return false;
				}
			}

			if (OutImpactNormal)
			{
				*OutImpactNormal = HitResult.ImpactNormal;
			}
			return true;
		}
	}

	return false;
}

bool UNavGen3DSubsystem::GenerateNavMesh3D(NavMeshVolumeBase* InVolume)
{
	if (InVolume)
	{
		ProcessNavMeshVolume(*InVolume);

		if (InVolume->ID != 0)
		{
			const int32 AgentCount = GetSupportedAgentCount();
			for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
			{
				const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(AgentIndex, true));
				if (TMap<uint64, TArray<FNavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(AgentIndex))
				{
					AgentMap->Remove(InVolume->ID);
				}
				FindNavMeshVolumeConnections(AgentIndex, Sphere, *InVolume);
			}
		}

		return false;
	}

	// Safety cap so runaway splitting can't hang the editor
	const int32 MaxIterations = FMath::Max(GetDefault<UNavGen3DSettings>()->MaxVolumeCount, 1) * NavGen3DClip::MaxProcessIterationsPerVolume;
	int32 Iterations = 0;
	while (!ProcessVolumesList.IsEmpty())
	{
		if (++Iterations > MaxIterations)
		{
			AddLogMessage(ENavGen3DLogCategory::Warning, TEXT(""),
				FString::Printf(TEXT("Generation stopped after %d iterations with %d volumes still queued"), MaxIterations, ProcessVolumesList.Num()));
			break;
		}

		NavMeshVolumeBase* CurrentVolume = ProcessVolumesList[0];
		ProcessVolumesList.RemoveAtSwap(0);
		ProcessNavMeshVolume(*CurrentVolume);
		delete CurrentVolume;
	}

	return false;
}

void UNavGen3DSubsystem::InitializeNavMesh3D()
{
	ClearSolutionVolumes();
	NavMeshSolutionMapByLocation.Reset();
	ClearProcessVolumes();
	ClearDebugUndoState();
	NavMeshVolumeMap_X.Reset();
	NavMeshVolumeMap_Y.Reset();
	NavMeshVolumeMap_Z.Reset();
	NavVolumeConnectionMap.Reset();
	NavMeshVolumeIDGenerator = 0;

	DebugPathSearchSpace.Reset();

	ValidateEmbeddedBoundsVolumes();

	AddLogMessage(ENavGen3DLogCategory::Info, TEXT(""), TEXT("NavMesh3D initialized"));
}

bool UNavGen3DSubsystem::GenerateNavMesh3DFromBoundsVolume(ANavGen3DBoundsVolume* InVolume)
{
	InitializeNavMesh3D();

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

		NavMeshVolumeBase* NavVolume = CreateNavMeshVolume();
		NavVolume->Bounds = FBox::BuildAABB(Origin, Extent);
		NavVolume->ParentBoundsVolume = InVolume;
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

bool UNavGen3DSubsystem::ValidateNavMesh3D(NavMeshVolumeBase* InVolume)
{
	if (!InVolume)
	{
		bool bAllValid = true;
		for (auto& Pair : NavMeshSolutionMap)
		{
			if (!ValidateNavMesh3D(Pair.Value))
			{
				bAllValid = false;
			}
		}
		return bAllValid;
	}

	const FString ActorName = InVolume->ParentBoundsVolume.IsValid()
		? InVolume->ParentBoundsVolume->GetActorLabel()
		: TEXT("<unknown>");

	bool bValid = true;

	FVector ImpactPoint;
	bool bIgnored = false;
	if (PlaneTrace(InVolume->Bounds.Min, InVolume->Bounds.Max, EAxis::X, ImpactPoint, bIgnored) ||
		PlaneTrace(InVolume->Bounds.Min, InVolume->Bounds.Max, EAxis::Y, ImpactPoint, bIgnored) ||
		PlaneTrace(InVolume->Bounds.Min, InVolume->Bounds.Max, EAxis::Z, ImpactPoint, bIgnored))
	{
		AddLogMessage(ENavGen3DLogCategory::Warning, ActorName,
			FString::Printf(TEXT("Validation failed for volume ID %llu"), InVolume->ID));
		bValid = false;
	}

	{
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(DebugNavMeshAgentIndex, true));
		if (const TArray<FNavVolumeConnection>* Connections = GetVolumeConnections(DebugNavMeshAgentIndex, InVolume->ID))
		{
			for (const FNavVolumeConnection& Conn : *Connections)
			{
				const NavMeshVolumeBase* NeighborVol = FindNavMeshVolumeByID(Conn.ID);
				if (!NeighborVol) continue;

				const int32 AxisIdx = FMath::Abs(Conn.ConnectionAxis) - 1;
				const float SphereRadius = Sphere.GetSphereRadius();
				FVector TraceEnd = Conn.Location;
				TraceEnd[AxisIdx] = (Conn.ConnectionAxis > 0)
					? NeighborVol->Bounds.Max[AxisIdx] - SphereRadius
					: NeighborVol->Bounds.Min[AxisIdx] + SphereRadius;

				if (!ValidateConnectionCollision(Sphere, Conn.Location, TraceEnd))
				{
					AddLogMessage(ENavGen3DLogCategory::Warning, ActorName,
						FString::Printf(TEXT("Connection validation failed for volume ID %llu, connection to ID %llu"), InVolume->ID, Conn.ID));
					bValid = false;
				}
			}
		}
	}

	return bValid;
}

void UNavGen3DSubsystem::PruneNavMesh3D()
{
	TArray<uint64> VolumesToRemove;

	for (const auto& Pair : NavMeshSolutionMap)
	{
		const uint64 VolumeID = Pair.Key;
		bool bHasConnections = false;

		for (const auto& AgentPair : NavVolumeConnectionMap)
		{
			const TArray<FNavVolumeConnection>* Connections = AgentPair.Value.Find(VolumeID);
			if (Connections && Connections->Num() > 0)
			{
				bHasConnections = true;
				break;
			}
		}

		if (!bHasConnections)
		{
			const FVector Extent = Pair.Value->Bounds.GetExtent();
			bool bHasSmallExtent = false;
			for (int32 AgentIndex = 0; AgentIndex < GetSupportedAgentCount() && !bHasSmallExtent; ++AgentIndex)
			{
				const float PaddedRadius = GetAgentCollisionRadius(AgentIndex, true);
				for (int32 i = 0; i < 3; ++i)
				{
					if (Extent[i] < PaddedRadius)
					{
						bHasSmallExtent = true;
						break;
					}
				}
			}

			if (bHasSmallExtent)
			{
				VolumesToRemove.Add(VolumeID);
			}
		}
	}

	for (uint64 ID : VolumesToRemove)
	{
		RemoveNavMeshVolume(ID);
	}

	AddLogMessage(ENavGen3DLogCategory::Info, TEXT(""),
		FString::Printf(TEXT("Pruned %d disconnected volumes"), VolumesToRemove.Num()));
}

void UNavGen3DSubsystem::DetermineConnectivity()
{
	for (auto& Pair : NavMeshSolutionMap)
	{
		Pair.Value->ConnectivityIDByAgent.Reset();
	}

	const int32 AgentCount = GetSupportedAgentCount();
	for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
	{
		const TMap<uint64, TArray<FNavVolumeConnection>>* AgentConnectionMap = NavVolumeConnectionMap.Find(AgentIndex);
		if (!AgentConnectionMap) continue;

		int32 CurrentConnectivityID = 1;

		while (true)
		{
			uint64 SeedVolumeID = 0;
			for (const auto& Pair : NavMeshSolutionMap)
			{
				const TArray<FNavVolumeConnection>* Connections = AgentConnectionMap->Find(Pair.Key);
				if (Connections && Connections->Num() > 0 && !Pair.Value->ConnectivityIDByAgent.Contains(AgentIndex))
				{
					SeedVolumeID = Pair.Key;
					break;
				}
			}

			if (SeedVolumeID == 0) break;

			TArray<uint64> Queue;
			Queue.Add(SeedVolumeID);

			while (!Queue.IsEmpty())
			{
				const uint64 CurrentID = Queue.Pop();

				NavMeshVolumeBase* CurrentVolume = FindNavMeshVolumeByID(CurrentID);
				if (!CurrentVolume || CurrentVolume->ConnectivityIDByAgent.Contains(AgentIndex)) continue;

				CurrentVolume->ConnectivityIDByAgent.Add(AgentIndex, CurrentConnectivityID);

				const TArray<FNavVolumeConnection>* Connections = AgentConnectionMap->Find(CurrentID);
				if (Connections)
				{
					for (const FNavVolumeConnection& Connection : *Connections)
					{
						NavMeshVolumeBase* Neighbor = FindNavMeshVolumeByID(Connection.ID);
						if (Neighbor && !Neighbor->ConnectivityIDByAgent.Contains(AgentIndex))
						{
							Queue.Add(Connection.ID);
						}
					}
				}
			}

			++CurrentConnectivityID;
		}
	}
}

void UNavGen3DSubsystem::ValidateEmbeddedBoundsVolumes()
{
	if (IsPlayMode())
	{
		return;
	}

	TArray<ANavGen3DBoundsVolume*> Volumes = GetBoundsVolumes();

	for (ANavGen3DBoundsVolume* Volume : Volumes)
	{
		if (!IsValid(Volume) || !Volume->Enabled)
		{
			continue;
		}

		Volume->Embedded = false;
		const FBox VolumeBox = Volume->GetComponentsBoundingBox();

		for (ANavGen3DBoundsVolume* OtherVolume : Volumes)
		{
			if (!IsValid(OtherVolume) || !OtherVolume->Enabled || OtherVolume == Volume)
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

TArray<ANavGen3DBoundsVolume*> UNavGen3DSubsystem::GetBoundsVolumes()
{
	TArray<ANavGen3DBoundsVolume*> Result;

	if (IsPlayMode())
	{
		for (const TWeakObjectPtr<ANavGen3DBoundsVolume>& WeakVol : BoundsVolumes)
		{
			if (ANavGen3DBoundsVolume* Vol = WeakVol.Get())
			{
				Result.Add(Vol);
			}
		}
		return Result;
	}

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!IsValid(World))
		{
			continue;
		}

		for (TActorIterator<ANavGen3DBoundsVolume> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Result.Add(*It);
			}
		}
	}
	return Result;
}

void UNavGen3DSubsystem::OnEndFrame()
{
	DebugDrawUpdate();
	ProcessPathSearchList();
}

void UNavGen3DSubsystem::DebugDrawUpdate()
{
	UWorld* World = FindWorld();
	const float DrawTime = World ? World->GetDeltaSeconds() * 1.1f : 0.0f;

	bCameraLocationValidForAgent = false;
	if (DebugLevel >= 1 && World)
	{
		const float UnpaddedRadius = GetAgentCollisionRadius(DebugNavMeshAgentIndex, false);
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(UnpaddedRadius);
		bCameraLocationValidForAgent = !World->OverlapBlockingTestByChannel(
			GetCameraLocation(), FQuat::Identity, ECC_WorldStatic, Sphere);
	}

	switch (DebugDrawMode)
	{
		case ENavGen3DDrawMode::NavBounds3D:
		{
			if (!World)
			{
				break;
			}

			for (const TWeakObjectPtr<ANavGen3DBoundsVolume>& WeakVol : BoundsVolumes)
			{
				ANavGen3DBoundsVolume* Volume = WeakVol.Get();
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
			if (World)
			{
				const TMap<uint64, TArray<FNavVolumeConnection>>* AgentConnections =
					DebugDrawConnections ? NavVolumeConnectionMap.Find(DebugNavMeshAgentIndex) : nullptr;

				static const TArray<FColor> VolumeColors = {
					FColor(255, 0,   0),   // red
					FColor(0,   255, 0),   // green
					FColor(0,   0,   255), // blue
					FColor(255, 255, 0),   // yellow
					FColor(255, 0,   255), // magenta
					FColor(255, 128, 0),   // orange
					FColor(0,   255, 128), // spring green
					FColor(128, 0,   255), // violet
				};

				for (const auto& Pair : NavMeshSolutionMap)
				{
					const NavMeshVolumeBase& Volume = *Pair.Value;
					const FVector Center = Volume.Bounds.GetCenter();
					FColor BoxColor = VolumeColors[Volume.ID % VolumeColors.Num()];
					if (DrawConnectivity)
					{
						if (const int32* ConnID = Volume.ConnectivityIDByAgent.Find(DebugNavMeshAgentIndex))
						{
							BoxColor = VolumeColors[*ConnID % VolumeColors.Num()];
						}
						else
						{
							BoxColor = FColor(160, 160, 160);
						}
					}
					if (NavGen3DClip::IsClippedVolume(Volume))
					{
						NavGen3DClip::DrawClippedVolume(World, *Volume.AsNavMesh3DVolume(), DrawTime, 3.0f);
					}
					else
					{
						DrawDebugBox(World, Center, Volume.Bounds.GetExtent(), BoxColor, false, DrawTime, 0, 3.0f);
					}

					if (AgentConnections)
					{
						if (const TArray<FNavVolumeConnection>* Connections = AgentConnections->Find(Volume.ID))
						{
							for (const FNavVolumeConnection& Conn : *Connections)
							{
								DrawDebugSphere(World, Conn.Location, 5.0f, 9, FColor::Orange, false, DrawTime);
							}
						}
					}
				}
			}
			break;
		}

		case ENavGen3DDrawMode::UnprocessedVolumes:
		{
			if (World)
			{
				for (const NavMeshVolumeBase* Volume : ProcessVolumesList)
				{
					DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::Yellow, false, DrawTime, 0, 3.0f);
				}
			}
			break;
		}

		default:
			break;
	}

	if (DrawVolumeID != 0 && World)
	{
		if (NavMeshVolumeBase* Volume = FindNavMeshVolumeByID(DrawVolumeID))
		{
			DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::White, false, DrawTime, 0, 5.0f);

			if (const TMap<uint64, TArray<FNavVolumeConnection>>* AgentConnections = NavVolumeConnectionMap.Find(DebugNavMeshAgentIndex))
			{
				if (const TArray<FNavVolumeConnection>* Connections = AgentConnections->Find(Volume->ID))
				{
					for (const FNavVolumeConnection& Conn : *Connections)
					{
						DrawDebugSphere(World, Conn.Location, 5.0f, 9, FColor::Orange, false, DrawTime);
					}
				}
			}
		}
	}

	if (DrawCameraVolume && World)
	{
		const FVector CameraLocation = GetCameraLocation();

		if (NavMeshVolumeBase* Volume = FindVolumeContainingLocation(CameraLocation))
		{
			DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::Green, false, DrawTime, 0, 5.0f);

			if (const TMap<uint64, TArray<FNavVolumeConnection>>* AgentConnections = NavVolumeConnectionMap.Find(DebugNavMeshAgentIndex))
			{
				if (const TArray<FNavVolumeConnection>* Connections = AgentConnections->Find(Volume->ID))
				{
					for (const FNavVolumeConnection& Conn : *Connections)
					{
						DrawDebugSphere(World, Conn.Location, 5.0f, 9, FColor::Orange, false, DrawTime);
					}
				}
			}
		}
		else
		{
			if (NavMeshVolumeBase* GenVolume = FindGenerationVolumeContainingLocation(CameraLocation, false))
			{
				DrawDebugBox(World, GenVolume->Bounds.GetCenter(), GenVolume->Bounds.GetExtent(), FColor::Orange, false, DrawTime, 0, 5.0f);
				delete GenVolume;
			}
		}
	}

	if (DebugPathOrigin != InvalidLocation && World)
	{
		DrawDebugSphere(World, DebugPathOrigin, GetAgentCollisionRadius(DebugNavMeshAgentIndex, false), 12, FColor(128, 0, 128), false, DrawTime);
		DrawDebugString(World, DebugPathOrigin, *FString::Printf(TEXT("Origin: %s"), *FVectorToString(DebugPathOrigin)), nullptr, FColor::White, DrawTime);

		if (DebugPathSearchSpace.Status == EPathSearchStatus::Success)
		{
			DebugPathSearchSpace.DebugDrawPath(DrawTime);
		}
	}

	if (DebugPathDestination != InvalidLocation && World)
	{
		const bool bDestFailed = DebugPathOrigin != InvalidLocation &&
			DebugPathSearchSpace.Origin == DebugPathOrigin &&
			DebugPathSearchSpace.Destination == DebugPathDestination &&
			DebugPathSearchSpace.Status == EPathSearchStatus::Fail;
		DrawDebugSphere(World, DebugPathDestination, GetAgentCollisionRadius(DebugNavMeshAgentIndex, false), 12, bDestFailed ? FColor::Red : FColor::Cyan, false, DrawTime);

		if (bDestFailed)
		{
			DrawDebugLine(World, DebugPathOrigin, DebugPathDestination, FColor::Red, false, DrawTime, 0, 3.0f);
		}
	}

	if (DebugRepathContinuous && World && DebugPathOrigin != InvalidLocation)
	{
		DebugRepathTimer += World->GetDeltaSeconds();
		if (DebugRepathTimer >= DebugRepathFrequency)
		{
			DebugRepathTimer = 0.0f;

			float AgentRadius, AgentHeight;
			if (GetAgentSettings(DebugNavMeshAgentIndex, AgentRadius, AgentHeight))
			{
				const float Threshold = FMath::Min(AgentRadius, AgentHeight * 0.5f);
				const FVector CurrentDestination = GetCameraLocation();

				const bool bOriginMoved = FVector::Dist(DebugPathOrigin, DebugPathSearchSpace.Origin) >= Threshold;
				const bool bDestinationMoved = FVector::Dist(CurrentDestination, DebugPathSearchSpace.Destination) >= Threshold;

				if (bOriginMoved || bDestinationMoved)
				{
					DebugFindPath(InvalidLocation, CurrentDestination);
				}
			}
		}
	}
}

NavGen3D_ENABLE_OPTIMIZATION
