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
#include "NavigationSystem.h"

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
		return InvalidLocation;
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

bool UNavGen3DSubsystem::GenerateNavMesh3DConnections(int32 InAgentIndex)
{
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(InAgentIndex, true));

	NavVolumeConnectionMap.Remove(InAgentIndex);

	for (const auto& Pair : NavMeshSolutionMap)
	{
		FindNavMeshVolumeConnections(InAgentIndex, Sphere, Pair.Value);

		TMap<uint64, TArray<NavVolumeConnection>>& AgentMap = NavVolumeConnectionMap.FindOrAdd(InAgentIndex);
		if (const TArray<NavVolumeConnection>* ConnectionsPtr = AgentMap.Find(Pair.Key))
		{
			const TArray<NavVolumeConnection> Connections = *ConnectionsPtr;
			for (const NavVolumeConnection& Conn : Connections)
			{
				TArray<NavVolumeConnection>& NeighborConns = AgentMap.FindOrAdd(Conn.ID);
				bool bAlreadyMirrored = false;
				for (const NavVolumeConnection& NC : NeighborConns)
				{
					if (NC.ID == Pair.Key) { bAlreadyMirrored = true; break; }
				}
				if (!bAlreadyMirrored)
				{
					NavVolumeConnection Reverse;
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

NavMeshVolume* UNavGen3DSubsystem::FindNavMeshVolumeByID(uint64 InID)
{
	return NavMeshSolutionMap.Find(InID);
}

void UNavGen3DSubsystem::FindNavMeshVolumeConnections(int32 InAgentIndex, const FCollisionShape& InSphere, const NavMeshVolume& InSourceVolume)
{
	TArray<NavVolumeConnection>& Connections = NavVolumeConnectionMap.FindOrAdd(InAgentIndex).FindOrAdd(InSourceVolume.ID);

	const float Stride = (float)GetDefault<UNavGen3DSettings>()->MaxVolumeSize;
	const FVector Center = InSourceVolume.Bounds.GetCenter();
	TSet<uint64> FoundIDs;
	for (const NavVolumeConnection& Existing : Connections)
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
			const NavMeshVolume* Curr = NavMeshSolutionMap.Find(CurrID);
			if (!Curr) break;
			if (Curr->Bounds.Min[InSortAxis] > BreakValue) break;
			if (Curr->ID != InSourceVolume.ID && !FoundIDs.Contains(Curr->ID))
			{
				FVector Location;
				int32 ConnAxis = 0;
				if (FindNavMeshVolumeConnection(InAgentIndex, InSphere, InSourceVolume, *Curr, InSortAxis, Location, ConnAxis))
				{
					FoundIDs.Add(Curr->ID);
					NavVolumeConnection Conn;
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

const TArray<NavVolumeConnection>* UNavGen3DSubsystem::GetVolumeConnections(int32 InAgentIndex, uint64 InVolumeID) const
{
	if (const TMap<uint64, TArray<NavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(InAgentIndex))
	{
		return AgentMap->Find(InVolumeID);
	}
	return nullptr;
}

bool UNavGen3DSubsystem::FindNavMeshVolumeConnection(int32 InAgentIndex, const FCollisionShape& InSphere, const NavMeshVolume& InSourceVolume, const NavMeshVolume& InNeighborVolume, int32 InAxis, FVector& OutLocation, int32& OutConnectionAxis)
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
	for (int32 AgentIndex = 0; AgentIndex < GetSupportedAgentCount(); ++AgentIndex)
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
		if (DebugDrawTime > 0.0f)
		{
			if (UWorld* World = FindWorld())
			{
				DrawDebugBox(World, RefVolume.Bounds.GetCenter(), Extent, FColor::Red, false, DebugDrawTime, 0, 5.0f);
			}
		}
		return false;
	}

	const UNavGen3DSettings* Settings = GetDefault<UNavGen3DSettings>();

	if (NavMeshSolutionMap.Num() >= Settings->MaxVolumeCount)
	{
		if (DebugDrawTime > 0.0f)
		{
			if (UWorld* World = FindWorld())
			{
				DrawDebugBox(World, RefVolume.Bounds.GetCenter(), Extent, FColor::Red, false, DebugDrawTime, 0, 5.0f);
			}
		}
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

	// Check for merge with an existing adjacent volume that shares an exact face
	{
		const FVector Center = RefVolume.Bounds.GetCenter();
		const float MaxVolumeSize_f = (float)Settings->MaxVolumeSize;

		auto CheckAxisMerge = [&](const TMap<uint64, uint64>& InMap, uint64 InHash, int32 InAxis) -> bool
		{
			const uint64* HeadID = InMap.Find(InHash);
			if (!HeadID || *HeadID == 0) return false;

			const int32 A1 = (InAxis + 1) % 3;
			const int32 A2 = (InAxis + 2) % 3;

			uint64 CurrID = *HeadID;
			while (CurrID != 0)
			{
				const NavMeshVolume* Curr = NavMeshSolutionMap.Find(CurrID);
				if (!Curr) break;

				if (FMath::IsNearlyEqual(Curr->Bounds.Min[A1], RefVolume.Bounds.Min[A1], Epsilon) &&
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
							NavMeshVolume Merged;
							Merged.Bounds.Min = RefVolume.Bounds.Min;
							Merged.Bounds.Max = RefVolume.Bounds.Max;
							Merged.ParentBoundsVolume = RefVolume.ParentBoundsVolume.IsValid()
								? RefVolume.ParentBoundsVolume : Curr->ParentBoundsVolume;
							Merged.Bounds.Min[InAxis] = FMath::Min(Curr->Bounds.Min[InAxis], RefVolume.Bounds.Min[InAxis]);
							Merged.Bounds.Max[InAxis] = FMath::Max(Curr->Bounds.Max[InAxis], RefVolume.Bounds.Max[InAxis]);

							const uint64 ExistingID = Curr->ID;
							RemoveNavMeshVolume(ExistingID);
							return AddNavMeshVolume(Merged);
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

	RefVolume.ID = ++NavMeshVolumeIDGenerator;
	NavMeshSolutionMap.Add(RefVolume.ID, RefVolume);

	const uint64 LocationHash = CalculateHash3D(RefVolume.Bounds.GetCenter());
	NavMeshSolutionMapByLocation.FindOrAdd(LocationHash).Add(RefVolume.ID);

	NavMeshVolume* Stored = NavMeshSolutionMap.Find(RefVolume.ID);

	const FVector Center = Stored->Bounds.GetCenter();

	// X bucket: zero out X, sort by Min.X
	{
		const uint64 Hash = CalculateHash3D(FVector(0.0f, Center.Y, Center.Z));
		uint64& HeadID = NavMeshVolumeMap_X.FindOrAdd(Hash, 0);
		uint64* CurrID = &HeadID;
		while (*CurrID != 0)
		{
			NavMeshVolume* CurrVol = NavMeshSolutionMap.Find(*CurrID);
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
			NavMeshVolume* CurrVol = NavMeshSolutionMap.Find(*CurrID);
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
			NavMeshVolume* CurrVol = NavMeshSolutionMap.Find(*CurrID);
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
			DrawDebugBox(World, Center, Stored->Bounds.GetExtent(), FColor::Green, false, DebugDrawTime, 0, 5.0f);
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
						if (NavMeshVolume* Volume = NavMeshSolutionMap.Find(VolumeID))
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

NavMeshVolume* UNavGen3DSubsystem::FindClosestVolumeContainingLocation(int32 InAgentIndex, const FVector& InLocation, int32 InConnectivityID)
{
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(InAgentIndex, true));

	const TMap<uint64, TArray<NavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(InAgentIndex);
	UWorld* World = FindWorld();

	const float MaxDist = (float)GetDefault<UNavGen3DSettings>()->MaxVolumeSize;
	NavMeshVolume* ClosestVolume = nullptr;
	float ClosestDistSq = MaxDist * MaxDist;

	for (auto& Pair : NavMeshSolutionMap)
	{
		NavMeshVolume& Volume = Pair.Value;

		if (AgentMap && !AgentMap->Contains(Volume.ID))
		{
			continue;
		}

		if (InConnectivityID > 0)
		{
			const int32* ConnID = Volume.ConnectivityIDByAgent.Find(InAgentIndex);
			if (!ConnID || *ConnID != InConnectivityID)
			{
				continue;
			}
		}

		const FVector VolumeCenter = Volume.Bounds.GetCenter();
		const float DistSq = FVector::DistSquared(VolumeCenter, InLocation);
		if (DistSq >= ClosestDistSq)
		{
			continue;
		}

		if (Volume.Bounds.IsInsideOrOn(InLocation))
		{
			ClosestDistSq = DistSq;
			ClosestVolume = &Volume;
		}
		else if (World)
		{
			FHitResult HitResult;
			if (!World->SweepSingleByChannel(HitResult, InLocation, VolumeCenter, FQuat::Identity, ECC_WorldStatic, Sphere))
			{
				ClosestDistSq = DistSq;
				ClosestVolume = &Volume;
			}
		}
	}

	return ClosestVolume;
}

bool UNavGen3DSubsystem::PathFind(FPathSearchSpace& InSearchSpace)
{
	if (InSearchSpace.Status != EPathSearchStatus::Initialized)
	{
		return false;
	}

	InSearchSpace.Status = EPathSearchStatus::InProgress;
	InSearchSpace.NodeHeap.Reset();
	InSearchSpace.PathSolution.Reset();
	InSearchSpace.PathVolumeIDs.Reset();
	InSearchSpace.PathConnectionAxes.Reset();

	const TMap<uint64, TArray<NavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(InSearchSpace.AgentIndex);
	if (!AgentMap)
	{
		InSearchSpace.Status = EPathSearchStatus::Fail;
		return false;
	}

	const NavMeshVolume* DestVolume = NavMeshSolutionMap.Find(InSearchSpace.DestinationID);
	const NavMeshVolume* OriginVolume = NavMeshSolutionMap.Find(InSearchSpace.OriginID);
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
				if (!Prev) break;
				TraceID = *Prev;
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

		const TArray<NavVolumeConnection>* Connections = AgentMap->Find(CurrentID);
		if (!Connections) continue;

		const NavMeshVolume* CurrentVolume = NavMeshSolutionMap.Find(CurrentID);
		if (!CurrentVolume) continue;

		const float CurrentGCost = GCost.FindRef(CurrentID);

		for (const NavVolumeConnection& Conn : *Connections)
		{
			const uint64 NeighborID = Conn.ID;
			if (ClosedSet.Contains(NeighborID)) continue;

			const NavMeshVolume* NeighborVolume = NavMeshSolutionMap.Find(NeighborID);
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
		if (const NavMeshVolume* EnteredVol = NavMeshSolutionMap.Find(InSearchSpace.PathVolumeIDs[i]))
		{
			const FVector VolumeCenter = EnteredVol->Bounds.GetCenter();
			const float RightAngleDist = FVector::DotProduct(ConnDir, VolumeCenter - ConnPoint);
			const float InsertDist = FMath::Max(PaddedRadius, RightAngleDist);
			Path.Insert(ConnPoint + ConnDir * InsertDist, i + 1);
		}

		// Before-connection node: into the volume being left.
		if (const NavMeshVolume* LeavingVol = NavMeshSolutionMap.Find(InSearchSpace.PathVolumeIDs[i - 1]))
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

void FPathSearchSpace::DrawPath(float InDrawTime) const
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
	AgentIndex = InAgentIndex;
	Origin = PathOrigin;
	Destination = PathDestination;

	if (InSubsystem)
	{
		int32 OriginConnectivityID = 0;
		if (const NavMeshVolume* OriginVolume = InSubsystem->FindClosestVolumeContainingLocation(InAgentIndex, PathOrigin))
		{
			OriginID = OriginVolume->ID;
			if (const int32* ConnID = OriginVolume->ConnectivityIDByAgent.Find(InAgentIndex))
			{
				OriginConnectivityID = *ConnID;
			}
		}

		if (const NavMeshVolume* DestinationVolume = InSubsystem->FindClosestVolumeContainingLocation(InAgentIndex, PathDestination, OriginConnectivityID))
		{
			DestinationID = DestinationVolume->ID;
		}
	}

	Status = (OriginID != 0 && DestinationID != 0) ? EPathSearchStatus::Initialized : EPathSearchStatus::Fail;
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

TOptional<NavMeshVolume> UNavGen3DSubsystem::PopNextProcessingVolume()
{
	if (ProcessVolumesList.IsEmpty()) return TOptional<NavMeshVolume>();
	NavMeshVolume Result = ProcessVolumesList[0];
	ProcessVolumesList.RemoveAt(0);
	return Result;
}

void UNavGen3DSubsystem::RemoveNavMeshVolume(uint64 InID)
{
	if (NavMeshVolume* Volume = NavMeshSolutionMap.Find(InID))
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
				NavMeshVolume* Curr = NavMeshSolutionMap.Find(CurrID);
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
		ProcessNavMeshVolume(*InVolume);

		if (InVolume->ID != 0)
		{
			const int32 AgentCount = GetSupportedAgentCount();
			for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
			{
				const FCollisionShape Sphere = FCollisionShape::MakeSphere(GetAgentCollisionRadius(AgentIndex, true));
				if (TMap<uint64, TArray<NavVolumeConnection>>* AgentMap = NavVolumeConnectionMap.Find(AgentIndex))
				{
					AgentMap->Remove(InVolume->ID);
				}
				FindNavMeshVolumeConnections(AgentIndex, Sphere, *InVolume);
			}
		}

		return false;
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
		if (const TArray<NavVolumeConnection>* Connections = GetVolumeConnections(DebugNavMeshAgentIndex, InVolume->ID))
		{
			for (const NavVolumeConnection& Conn : *Connections)
			{
				const NavMeshVolume* NeighborVol = NavMeshSolutionMap.Find(Conn.ID);
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
			const TArray<NavVolumeConnection>* Connections = AgentPair.Value.Find(VolumeID);
			if (Connections && Connections->Num() > 0)
			{
				bHasConnections = true;
				break;
			}
		}

		if (!bHasConnections)
		{
			const FVector Extent = Pair.Value.Bounds.GetExtent();
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
		Pair.Value.ConnectivityIDByAgent.Reset();
	}

	const int32 AgentCount = GetSupportedAgentCount();
	for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
	{
		const TMap<uint64, TArray<NavVolumeConnection>>* AgentConnectionMap = NavVolumeConnectionMap.Find(AgentIndex);
		if (!AgentConnectionMap) continue;

		int32 CurrentConnectivityID = 1;

		while (true)
		{
			uint64 SeedVolumeID = 0;
			for (const auto& Pair : NavMeshSolutionMap)
			{
				const TArray<NavVolumeConnection>* Connections = AgentConnectionMap->Find(Pair.Key);
				if (Connections && Connections->Num() > 0 && !Pair.Value.ConnectivityIDByAgent.Contains(AgentIndex))
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

				NavMeshVolume* CurrentVolume = NavMeshSolutionMap.Find(CurrentID);
				if (!CurrentVolume || CurrentVolume->ConnectivityIDByAgent.Contains(AgentIndex)) continue;

				CurrentVolume->ConnectivityIDByAgent.Add(AgentIndex, CurrentConnectivityID);

				const TArray<NavVolumeConnection>* Connections = AgentConnectionMap->Find(CurrentID);
				if (Connections)
				{
					for (const NavVolumeConnection& Connection : *Connections)
					{
						NavMeshVolume* Neighbor = NavMeshSolutionMap.Find(Connection.ID);
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
	UWorld* World = FindWorld();
	const float DrawTime = World ? World->GetDeltaSeconds() * 1.1f : 0.0f;

	switch (DebugDrawMode)
	{
		case ENavGen3DDrawMode::NavBounds3D:
		{
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
				DrawDebugBox(World, Origin, Extent, Volume->GetActorRotation().Quaternion(), DrawColor, false, DrawTime, 0, 5.0f);
			}
			break;
		}

		case ENavGen3DDrawMode::NavMesh3D:
		{
			if (World)
			{
				const TMap<uint64, TArray<NavVolumeConnection>>* AgentConnections =
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
					const NavMeshVolume& Volume = Pair.Value;
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
					DrawDebugBox(World, Center, Volume.Bounds.GetExtent(), BoxColor, false, DrawTime, 0, 3.0f);

					if (AgentConnections)
					{
						if (const TArray<NavVolumeConnection>* Connections = AgentConnections->Find(Volume.ID))
						{
							for (const NavVolumeConnection& Conn : *Connections)
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

	if (DrawVolumeID != 0 && World)
	{
		if (NavMeshVolume* Volume = FindNavMeshVolumeByID(DrawVolumeID))
		{
			DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::White, false, DrawTime, 0, 5.0f);

			if (const TMap<uint64, TArray<NavVolumeConnection>>* AgentConnections = NavVolumeConnectionMap.Find(DebugNavMeshAgentIndex))
			{
				if (const TArray<NavVolumeConnection>* Connections = AgentConnections->Find(Volume->ID))
				{
					for (const NavVolumeConnection& Conn : *Connections)
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

		if (NavMeshVolume* Volume = FindVolumeContainingLocation(CameraLocation))
		{
			DrawDebugBox(World, Volume->Bounds.GetCenter(), Volume->Bounds.GetExtent(), FColor::Green, false, DrawTime, 0, 5.0f);

			if (const TMap<uint64, TArray<NavVolumeConnection>>* AgentConnections = NavVolumeConnectionMap.Find(DebugNavMeshAgentIndex))
			{
				if (const TArray<NavVolumeConnection>* Connections = AgentConnections->Find(Volume->ID))
				{
					for (const NavVolumeConnection& Conn : *Connections)
					{
						DrawDebugSphere(World, Conn.Location, 5.0f, 9, FColor::Orange, false, DrawTime);
					}
				}
			}
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

	if (DebugPathOrigin != InvalidLocation && World)
	{
		DrawDebugSphere(World, DebugPathOrigin, 20.0f, 12, FColor(128, 0, 128), false, DrawTime);
		DrawDebugString(World, DebugPathOrigin, *FString::Printf(TEXT("Origin: %s"), *FVectorToString(DebugPathOrigin)), nullptr, FColor::White, DrawTime);

		if (DebugPathSearchSpace.Status == EPathSearchStatus::Success)
		{
			DebugPathSearchSpace.DrawPath(DrawTime);
		}
	}

	if (DebugPathDestination != InvalidLocation && World)
	{
		DrawDebugSphere(World, DebugPathDestination, 20.0f, 12, FColor::Cyan, false, DrawTime);

		if (DebugPathOrigin != InvalidLocation &&
			DebugPathSearchSpace.Origin == DebugPathOrigin &&
			DebugPathSearchSpace.Destination == DebugPathDestination &&
			DebugPathSearchSpace.Status == EPathSearchStatus::Fail)
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
