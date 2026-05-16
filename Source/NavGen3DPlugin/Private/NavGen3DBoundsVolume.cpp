// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DBoundsVolume.h"
#include "NavGen3DBoundsVolumeAsset.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DSettings.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

// -- defined in NavGen3DSettings.h
NavGen3D_DISABLE_OPTIMIZATION

ANavGen3DBoundsVolume::ANavGen3DBoundsVolume(const FObjectInitializer& InObjectInitializer)
	: Super(InObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && GEngine)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->AddBoundsVolume(this);
		}
	}
}

ANavGen3DBoundsVolume::~ANavGen3DBoundsVolume()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && GEngine)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->RemoveBoundsVolume(this);
		}
	}
}

void ANavGen3DBoundsVolume::PostActorCreated()
{
	Super::PostActorCreated();
	const UNavGen3DSettings* Settings = GetDefault<UNavGen3DSettings>();
	MinVolumeSize = FMath::Max((float)Settings->DefaultMinVolumeSize, Settings->GeneratedMinVolumeSize);
#if WITH_EDITOR
	CreateGeneratedAsset();
#endif
}

void ANavGen3DBoundsVolume::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	// Recover the asset reference if the level was saved before it was set
	if (!NavMesh3DAsset)
	{
		CreateGeneratedAsset();
	}
#endif
}

#if WITH_EDITOR
void ANavGen3DBoundsVolume::CreateGeneratedAsset()
{
	if (NavMesh3DAsset)
	{
		return;
	}

	const ULevel* Level = GetLevel();
	if (!Level)
	{
		return;
	}

	const FString LevelPackagePath = FPackageName::GetLongPackagePath(Level->GetOutermost()->GetPathName());
	const FString LevelName = FPackageName::GetShortName(Level->GetOutermost()->GetPathName());
	const FString GuidString = ActorGuid.ToString(EGuidFormats::Digits);
	const FString AssetName = FString::Printf(TEXT("%s_NavGen3D_%s"), *LevelName, *GuidString);
	const FString PackagePath = FString::Printf(TEXT("%s/NavGen3DGenerated/%s"), *LevelPackagePath, *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package->IsFullyLoaded())
	{
		Package->FullyLoad();
	}

	// Reuse the existing asset object if the package was already on disk
	UNavGen3DBoundsVolumeAsset* NewAsset = FindObject<UNavGen3DBoundsVolumeAsset>(Package, *AssetName);
	if (!NewAsset)
	{
		NewAsset = NewObject<UNavGen3DBoundsVolumeAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(NewAsset);
	}

	Package->MarkPackageDirty();

	NavMesh3DAsset = NewAsset;
}

void ANavGen3DBoundsVolume::SaveGeneratedAsset()
{
	if (!NavMesh3DAsset)
	{
		CreateGeneratedAsset();
		if (!NavMesh3DAsset)
		{
			return;
		}
	}

	UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;

	NavMesh3DAsset->GeneratedVolumes.Reset();

	if (Subsystem)
	{
		const int32 AgentCount = Subsystem->GetSupportedAgentCount();

		for (auto& Pair : Subsystem->GetNavMeshSolutionMap())
		{
			const NavMeshVolume& Volume = Pair.Value;
			if (Volume.ParentBoundsVolume != this)
			{
				continue;
			}

			FNavGen3DGeneratedVolume Generated;
			Generated.Version   = UNavGen3DSubsystem::NavMeshAssetVersion;
			Generated.ID        = Volume.ID;
			Generated.BoundsMin = Volume.Bounds.Min;
			Generated.BoundsMax = Volume.Bounds.Max;

			Generated.ConnectivityIDs.SetNum(AgentCount);
			const TMap<int32, TMap<uint64, TArray<FNavVolumeConnection>>>& ConnectionMap = Subsystem->GetNavVolumeConnectionMap();
			for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
			{
				const int32* ConnID = Volume.ConnectivityIDByAgent.Find(AgentIndex);
				Generated.ConnectivityIDs[AgentIndex] = ConnID ? *ConnID : 0;

				if (const TMap<uint64, TArray<FNavVolumeConnection>>* AgentMap = ConnectionMap.Find(AgentIndex))
				{
					if (const TArray<FNavVolumeConnection>* VolumeConns = AgentMap->Find(Volume.ID))
					{
						TArray<FNavGen3DStoredConnection>& Stored = Generated.Connections.FindOrAdd(AgentIndex).Connections;
						Stored.Reserve(VolumeConns->Num());
						for (const FNavVolumeConnection& Conn : *VolumeConns)
						{
							FNavGen3DStoredConnection& S = Stored.AddDefaulted_GetRef();
							S.ID             = Conn.ID;
							S.Location       = Conn.Location;
							S.ConnectionAxis = Conn.ConnectionAxis;
						}
					}
				}
			}

			NavMesh3DAsset->GeneratedVolumes.Add(Generated);
		}
	}

	FVector BoundsOrigin, BoundsExtent;
	GetActorBounds(false, BoundsOrigin, BoundsExtent);
	NavMesh3DAsset->SavedVersion      = UNavGen3DSubsystem::NavMeshAssetVersion;
	NavMesh3DAsset->SavedBoundsOrigin = BoundsOrigin;
	NavMesh3DAsset->SavedBoundsExtent = BoundsExtent;

	UPackage* Package = NavMesh3DAsset->GetPackage();
	if (!Package)
	{
		return;
	}

	if (!Package->IsFullyLoaded())
	{
		Package->FullyLoad();
	}

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Package->GetPathName(), FPackageName::GetAssetPackageExtension());

	if (PackageFilename.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("NavGen3D: Could not resolve package filename for %s"), *Package->GetPathName());
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), /*Tree=*/true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags     = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, NavMesh3DAsset, *PackageFilename, SaveArgs);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("NavGen3D: SavePackage failed for %s"), *PackageFilename);
	}
}

bool ANavGen3DBoundsVolume::ValidateGeneratedAsset()
{
	if (!NavMesh3DAsset)
	{
		return false;
	}

	bool bValid = NavMesh3DAsset->SavedVersion == UNavGen3DSubsystem::NavMeshAssetVersion;

	if (bValid)
	{
		FVector CurrentOrigin, CurrentExtent;
		GetActorBounds(false, CurrentOrigin, CurrentExtent);
		bValid = CurrentOrigin.Equals(NavMesh3DAsset->SavedBoundsOrigin) &&
		         CurrentExtent.Equals(NavMesh3DAsset->SavedBoundsExtent);
	}

	if (!bValid)
	{
		NavMesh3DAsset->GeneratedVolumes.Reset();
		NavMesh3DAsset->SavedVersion = UNavGen3DSubsystem::NavMeshAssetVersion;
		NavMesh3DAsset->GetPackage()->MarkPackageDirty();
	}

	return bValid;
}
#endif

void ANavGen3DBoundsVolume::LoadNavMesh3DAsset()
{
	AddAssetToSolution();
}

void ANavGen3DBoundsVolume::AddAssetToSolution()
{
	if (!NavMesh3DAsset)
	{
		// UPROPERTY reference was not saved with the level — try to locate the asset by its deterministic path
		if (const ULevel* Level = GetLevel())
		{
			const FString LevelPackagePath = FPackageName::GetLongPackagePath(Level->GetOutermost()->GetPathName());
			const FString LevelName        = FPackageName::GetShortName(Level->GetOutermost()->GetPathName());
			const FString AssetName        = FString::Printf(TEXT("%s_NavGen3D_%s"), *LevelName, *ActorGuid.ToString(EGuidFormats::Digits));
			const FString AssetPath        = FString::Printf(TEXT("%s/NavGen3DGenerated/%s.%s"), *LevelPackagePath, *AssetName, *AssetName);
			NavMesh3DAsset = LoadObject<UNavGen3DBoundsVolumeAsset>(nullptr, *AssetPath);
		}
	}

	if (!NavMesh3DAsset || NavMesh3DAsset->GeneratedVolumes.IsEmpty())
	{
		return;
	}

	UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	for (const FNavGen3DGeneratedVolume& GeneratedVolume : NavMesh3DAsset->GeneratedVolumes)
	{
		Subsystem->AddGeneratedVolumeToSolution(GeneratedVolume, this);
	}
}

void ANavGen3DBoundsVolume::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
	const float GeneratedMin = GetDefault<UNavGen3DSettings>()->GeneratedMinVolumeSize;
	MinVolumeSize = FMath::Max(MinVolumeSize, GeneratedMin);
}

void ANavGen3DBoundsVolume::GenerateNavMesh3D()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->InitializeNavMesh3D();
		Subsystem->GenerateNavMesh3DFromBoundsVolume(this);
		const int32 AgentCount = Subsystem->GetSupportedAgentCount();
		for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
		{
			Subsystem->GenerateNavMesh3DConnections(AgentIndex);
		}
		Subsystem->PruneNavMesh3D();
#if WITH_EDITOR
		SaveGeneratedAsset();
#endif
	}
}

void ANavGen3DBoundsVolume::BeginPlay()
{
	Super::BeginPlay();
	Status = ENavGen3DBoundsVolumeStatus::InPlay;
	AddAssetToSolution();
}

void ANavGen3DBoundsVolume::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
	Status = ENavGen3DBoundsVolumeStatus::None;
	Super::EndPlay(InEndPlayReason);
}

NavGen3D_ENABLE_OPTIMIZATION
