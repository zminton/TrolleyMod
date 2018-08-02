// Copyright (c) 2017 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla.h"
#include "VehicleSpawnerBase.h"
#include "Util/RandomEngine.h"
#include "Vehicle/CarlaWheeledVehicle.h"
#include "Vehicle/WheeledVehicleAIController.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"

// =============================================================================
// -- Static local methods -----------------------------------------------------
// =============================================================================

static bool VehicleIsValid(const ACarlaWheeledVehicle *Vehicle)
{
  return ((Vehicle != nullptr) && !Vehicle->IsPendingKill());
}

static AWheeledVehicleAIController *GetController(ACarlaWheeledVehicle *Vehicle)
{
  return (VehicleIsValid(Vehicle) ? Cast<AWheeledVehicleAIController>(Vehicle->GetController()) : nullptr);
}

// =============================================================================
// -- AVehicleSpawnerBase ------------------------------------------------------
// =============================================================================

// Sets default values
AVehicleSpawnerBase::AVehicleSpawnerBase(const FObjectInitializer& ObjectInitializer): Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = false;
}

void AVehicleSpawnerBase::BeginPlay()
{
  Super::BeginPlay();

  NumberOfVehicles = FMath::Max(0, NumberOfVehicles);

  // Allocate space for walkers.
  Vehicles.Reserve(NumberOfVehicles);

  // Find spawn points present in level.
  for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) {
    SpawnPoints.Add(*It);
  }

  UE_LOG(LogCarla, Log, TEXT("Found %d PlayerStart positions for spawning vehicles"), SpawnPoints.Num());

  if (SpawnPoints.Num() < NumberOfVehicles && SpawnPoints.Num()>0) 
  {
    UE_LOG(LogCarla, Warning, TEXT("We don't have enough spawn points (PlayerStart) for vehicles!"));
	if(SpawnPoints.Num()==0)
	{
	  UE_LOG(LogCarla, Error, TEXT("At least one spawn point (PlayerStart) is needed to spawn vehicles!"));	
	} else
	{
	  UE_LOG(LogCarla, Log, 
	    TEXT("To cover the %d vehicles to spawn after beginplay, it will spawn one new vehicle each %f seconds"),
	    NumberOfVehicles - SpawnPoints.Num(),
		TimeBetweenSpawnAttemptsAfterBegin
	  )
;
	}
  }
  
  if(NumberOfVehicles==0||SpawnPoints.Num()==0) bSpawnVehicles = false;

  if (bSpawnVehicles) 
  {
	GetRandomEngine()->Shuffle(SpawnPoints); //to get a random spawn point from the map
    const int32 MaximumNumberOfAttempts = SpawnPoints.Num(); 
    int32 NumberOfAttempts = 0; 
    int32 SpawnIndexCount = 0;
    while ((NumberOfVehicles > Vehicles.Num()) && (NumberOfAttempts < MaximumNumberOfAttempts)) 
	{
      if(SpawnPoints.IsValidIndex(SpawnIndexCount))
      {
	     if(SpawnVehicleAtSpawnPoint(*SpawnPoints[SpawnIndexCount])){
            SpawnIndexCount++;
         }
      }
      NumberOfAttempts++;
    }
	bool bAllSpawned = false;
    if (NumberOfVehicles > SpawnIndexCount) 
	{
      UE_LOG(LogCarla, Warning, TEXT("Requested %d vehicles, but we were only able to spawn %d"), NumberOfVehicles, SpawnIndexCount);
    } else
    {
	  if(SpawnIndexCount == NumberOfVehicles)
	  {
        bAllSpawned = true;
	  } 
    }
    if(!bAllSpawned)
    {
      UE_LOG(LogCarla, Log, 
	    TEXT("Starting the timer to spawn the other %d vehicles, one per %f seconds"), 
	    NumberOfVehicles - SpawnIndexCount,
	    TimeBetweenSpawnAttemptsAfterBegin
	  );
	  GetWorld()->GetTimerManager().SetTimer(AttemptTimerHandle,this, &AVehicleSpawnerBase::SpawnVehicleAttempt, TimeBetweenSpawnAttemptsAfterBegin,false,-1);
    } else
    {
      UE_LOG(LogCarla, Log, TEXT("Spawned all %d requested vehicles"), NumberOfVehicles);
    }
  }
}

void AVehicleSpawnerBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
}

void AVehicleSpawnerBase::SetNumberOfVehicles(const int32 Count)
{
  if (Count > 0) 
  {
    bSpawnVehicles = true;
    NumberOfVehicles = Count;
  } else {
    bSpawnVehicles = false;
  }
}

void AVehicleSpawnerBase::TryToSpawnRandomVehicle()
{
  auto SpawnPoint = GetRandomSpawnPoint();
  if (SpawnPoint != nullptr) 
  {
      SpawnVehicleAtSpawnPoint(*SpawnPoint);
  } else {
    UE_LOG(LogCarla, Error, TEXT("Unable to find spawn point"));
  }
}

ACarlaWheeledVehicle* AVehicleSpawnerBase::SpawnVehicleAtSpawnPoint(
    const APlayerStart &SpawnPoint)
{
  ACarlaWheeledVehicle *Vehicle;
  SpawnVehicle(SpawnPoint.GetActorTransform(), Vehicle);
  if ((Vehicle != nullptr) && !Vehicle->IsPendingKill())
  {
    Vehicle->AIControllerClass = AWheeledVehicleAIController::StaticClass();
    Vehicle->SpawnDefaultController();
    auto Controller = GetController(Vehicle);
    if (Controller != nullptr) 
	{ // Sometimes fails...
      Controller->GetRandomEngine()->Seed(GetRandomEngine()->GenerateSeed());
      Controller->SetRoadMap(GetRoadMap());
      Controller->SetAutopilot(true);
      Vehicles.Add(Vehicle);
    } else {

      UE_LOG(LogCarla, Error, TEXT("Something went wrong creating the controller for the new vehicle"));
      Vehicle->Destroy();
    }
  }
  return Vehicle;
}

void AVehicleSpawnerBase::SpawnVehicleAttempt()
{
	if(Vehicles.Num()>=NumberOfVehicles) 
	{
	  UE_LOG(LogCarla, Log, TEXT("All vehicles spawned correctly"));
	  return;
	}
	
	APlayerStart* spawnpoint = GetRandomSpawnPoint();
	APawn* playerpawn = UGameplayStatics::GetPlayerPawn(GetWorld(),0);
	const float DistanceToPlayer = playerpawn&&spawnpoint? FVector::Distance(playerpawn->GetActorLocation(),spawnpoint->GetActorLocation()):0.0f;
	float NextTime = TimeBetweenSpawnAttemptsAfterBegin;
	if(DistanceToPlayer>DistanceToPlayerBetweenSpawnAttemptsAfterBegin)
	{
	  if(SpawnVehicleAtSpawnPoint(*spawnpoint)!=nullptr)
	  {
	      UE_LOG(LogCarla, Log, TEXT("Vehicle %d/%d late spawned"), Vehicles.Num(), NumberOfVehicles);
	  }
	} else
	{
	  NextTime /= 2.0f;
	}
	
	if(Vehicles.Num()<NumberOfVehicles)
	{
	  auto &timemanager = GetWorld()->GetTimerManager();
	  if(AttemptTimerHandle.IsValid()) timemanager.ClearTimer(AttemptTimerHandle);
	  timemanager.SetTimer(AttemptTimerHandle,this, &AVehicleSpawnerBase::SpawnVehicleAttempt,NextTime,false,-1);
	} else
	{
	  UE_LOG(LogCarla, Log, TEXT("All vehicles spawned correctly"));
	}

}

APlayerStart *AVehicleSpawnerBase::GetRandomSpawnPoint()
{
  return (SpawnPoints.Num() > 0 ? GetRandomEngine()->PickOne(SpawnPoints) : nullptr);
}


/***************************************************************************************
*    Title: KantanCodeExamples
*    Author: Cameron Angus
*    Date: Oct 27, 2017
*    Code version: 4.16
*    Availability: 
*    https://github.com/kamrann/KantanCodeExamples/tree/master/Source/A1_GatherSubclasses/Runtime
*
***************************************************************************************/

//Find all subclasses of the base class to load in later.
TArray<TSoftClassPtr<UObject>> AVehicleSpawnerBase::FindClasses(UClass* Base)
{
  TArray<TSoftClassPtr<UObject>> Subclasses;

  for(TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
  {
    UClass* Class = *ClassIt;

    // Only interested in native C++ classes
    if(!Class->IsNative())
    {
        continue;
    }

    // Ignore deprecated
    if(Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        continue;
    }

    /*
    #if WITH_EDITOR
    // Ignore skeleton classes (semi-compiled versions that only exist in-editor)
    if(FKismetEditorUtilities::IsClassABlueprintSkeleton(Class))
    {
        continue;
    }
    #endif
    */

    // Check this class is a subclass of Base
    if(!Class->IsChildOf(Base))
    {
        continue;
    }

    // Add this class
    Subclasses.Add(Class);
  }

  // Load the asset registry module
  FAssetRegistryModule& AssetRegistryModule = 
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
  IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
  TArray<FString> ContentPaths;
  ContentPaths.Add(TEXT("/Game"));
  AssetRegistry.ScanPathsSynchronous(ContentPaths);

  FName BaseClassName = Base->GetFName();

  TSet<FName> DerivedNames;
  {
    TArray< FName > BaseNames;
    BaseNames.Add(BaseClassName);

    TSet< FName > Excluded;
    AssetRegistry.GetDerivedClassNames(BaseNames, Excluded, DerivedNames);
  }

  FARFilter Filter;
  Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
  Filter.bRecursiveClasses = true;

  for(FString x : ContentPaths)
  {
    if(!x.IsEmpty())
    {
      Filter.PackagePaths.Add(*x);
    }
  }
  
  Filter.bRecursivePaths = true;

  TArray<FAssetData> AssetList;
  AssetRegistry.GetAssets(Filter, AssetList);

  // Iterate over retrieved blueprint assets
  for(auto const& Asset : AssetList)
  {
    // Get the the class this blueprint generates (this is stored as a full path)
    if(auto GeneratedClassPathPtr = Asset.TagsAndValues.Find(TEXT("GeneratedClass")))
    {
      // Convert path to just the name part
      const FString ClassObjectPath = 
        FPackageName::ExportTextPathToObjectPath(*GeneratedClassPathPtr);
      const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);

      // Check if this class is in the derived set
      if(!DerivedNames.Contains(*ClassName))
      {
          continue;
      }

      // Store using the path to the generated class
      Subclasses.Add(TSoftClassPtr<UObject>(FStringAssetReference(ClassObjectPath)));
    }
  }

  return Subclasses;
}
