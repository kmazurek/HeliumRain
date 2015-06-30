
#include "../Flare.h"
#include "FlareGame.h"
#include "FlareSaveGame.h"
#include "FlareAsteroid.h"
#include "../Player/FlareMenuManager.h"
#include "../Player/FlareHUD.h"
#include "../Player/FlarePlayerController.h"
#include "../Spacecrafts/FlareShell.h"


/*----------------------------------------------------
	Constructor
----------------------------------------------------*/

AFlareGame::AFlareGame(const class FObjectInitializer& PCIP)
	: Super(PCIP)
	, CurrentImmatriculationIndex(0)
	, LoadedOrCreated(false)
	, SaveSlotCount(3)
{
	// Game classes
	HUDClass = AFlareHUD::StaticClass();
	PlayerControllerClass = AFlarePlayerController::StaticClass();
	DefaultWeaponIdentifer = FName("weapon-eradicator");
	DefaultTurretIdentifer = FName("weapon-artemis");

	// Menu pawn
	static ConstructorHelpers::FObjectFinder<UBlueprint> MenuPawnBPClass(TEXT("/Game/Gameplay/Menu/BP_MenuPawn"));
	if (MenuPawnBPClass.Object != NULL)
	{
		MenuPawnClass = (UClass*)MenuPawnBPClass.Object->GeneratedClass;
	}

	// Planetary system
	static ConstructorHelpers::FObjectFinder<UBlueprint> PlanetariumBPClass(TEXT("/Game/Environment/BP_Planetarium"));
	if (PlanetariumBPClass.Object != NULL)
	{
		PlanetariumClass = (UClass*)PlanetariumBPClass.Object->GeneratedClass;
	}

	// Data catalogs
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UFlareSpacecraftCatalog> SpacecraftCatalog;
		ConstructorHelpers::FObjectFinder<UFlareSpacecraftComponentsCatalog> ShipPartsCatalog;
		ConstructorHelpers::FObjectFinder<UFlareCustomizationCatalog> CustomizationCatalog;
		ConstructorHelpers::FObjectFinder<UFlareAsteroidCatalog> AsteroidCatalog;

		FConstructorStatics()
			: SpacecraftCatalog(TEXT("/Game/Gameplay/Catalog/SpacecraftCatalog"))
			, ShipPartsCatalog(TEXT("/Game/Gameplay/Catalog/ShipPartsCatalog"))
			, CustomizationCatalog(TEXT("/Game/Gameplay/Catalog/CustomizationCatalog"))
			, AsteroidCatalog(TEXT("/Game/Gameplay/Catalog/AsteroidCatalog"))
		{}
	};
	static FConstructorStatics ConstructorStatics;

	// Push catalog data into storage
	SpacecraftCatalog = ConstructorStatics.SpacecraftCatalog.Object;
	ShipPartsCatalog = ConstructorStatics.ShipPartsCatalog.Object;
	CustomizationCatalog = ConstructorStatics.CustomizationCatalog.Object;
	AsteroidCatalog = ConstructorStatics.AsteroidCatalog.Object;
}


/*----------------------------------------------------
	Gameplay
----------------------------------------------------*/

void AFlareGame::StartPlay()
{
	FLOG("AFlareGame::StartPlay");
	Super::StartPlay();

	// Spawn planet
	Planetarium = GetWorld()->SpawnActor<AFlarePlanetarium>(PlanetariumClass, FVector::ZeroVector, FRotator::ZeroRotator);
	if (Planetarium)
	{
		Planetarium->SetAltitude(10000);
		Planetarium->SetSunRotation(100);
	}
	else
	{
		FLOG("AFlareGame::StartPlay failed (no planetarium)");
	}
}

void AFlareGame::PostLogin(APlayerController* Player)
{
	FLOG("AFlareGame::PostLogin");
	Super::PostLogin(Player);
}

void AFlareGame::Logout(AController* Player)
{
	FLOG("AFlareGame::Logout");

	// Save the world, literally
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(Player);
	SaveWorld(PC);
	PC->PrepareForExit();

	Super::Logout(Player);
}


/*----------------------------------------------------
	Save slots
----------------------------------------------------*/

void AFlareGame::ReadAllSaveSlots()
{
	// Setup
	SaveSlots.Empty();
	FVector2D EmblemSize = 128 * FVector2D::UnitVector;
	UMaterial* BaseEmblemMaterial = Cast<UMaterial>(FFlareStyleSet::GetIcon("CompanyEmblem")->GetResourceObject());

	// Get all saves
	for (int32 Index = 1; Index <= SaveSlotCount; Index++)
	{
		FFlareSaveSlotInfo SaveSlotInfo;
		SaveSlotInfo.EmblemBrush.ImageSize = EmblemSize;
		UFlareSaveGame* Save = AFlareGame::ReadSaveSlot(Index);
		SaveSlotInfo.Save = Save;

		if (Save)
		{
			// Basic setup
			UFlareCustomizationCatalog* Catalog = GetCustomizationCatalog();
			FLOGV("AFlareGame::ReadAllSaveSlots : found valid save data in slot %d", Index);

			// Count player ships
			SaveSlotInfo.CompanyShipCount = 0;
			for (int32 ShipIndex = 0; ShipIndex < Save->ShipData.Num(); ShipIndex++)
			{
				const FFlareSpacecraftSave& Spacecraft = Save->ShipData[ShipIndex];
				if (Spacecraft.CompanyIdentifier == Save->PlayerData.CompanyIdentifier)
				{
					SaveSlotInfo.CompanyShipCount++;
				}
			}

			// Find company
			FFlareCompanySave* PlayerCompany = NULL;
			for (int32 CompanyIndex = 0; CompanyIndex < Save->CompanyData.Num(); CompanyIndex++)
			{
				const FFlareCompanySave& Company = Save->CompanyData[CompanyIndex];
				if (Company.Identifier == Save->PlayerData.CompanyIdentifier)
				{
					PlayerCompany = &(Save->CompanyData[CompanyIndex]);
				}
			}

			// Company info
			if (PlayerCompany)
			{
				// Money and general infos
				SaveSlotInfo.CompanyMoney = PlayerCompany->Money;
				SaveSlotInfo.CompanyName = FText::FromString(PlayerCompany->Name);

				// Emblem material
				SaveSlotInfo.Emblem = UMaterialInstanceDynamic::Create(BaseEmblemMaterial, GetWorld());
				SaveSlotInfo.Emblem->SetVectorParameterValue("BasePaintColor", Catalog->GetColor(PlayerCompany->CustomizationBasePaintColorIndex));
				SaveSlotInfo.Emblem->SetVectorParameterValue("PaintColor", Catalog->GetColor(PlayerCompany->CustomizationPaintColorIndex));
				SaveSlotInfo.Emblem->SetVectorParameterValue("OverlayColor", Catalog->GetColor(PlayerCompany->CustomizationOverlayColorIndex));
				SaveSlotInfo.Emblem->SetVectorParameterValue("GlowColor", Catalog->GetColor(PlayerCompany->CustomizationLightColorIndex));

				// Create the brush dynamically
				SaveSlotInfo.EmblemBrush.SetResourceObject(SaveSlotInfo.Emblem);
			}
		}
		else
		{
			SaveSlotInfo.Save = NULL;
			SaveSlotInfo.Emblem = NULL;
			SaveSlotInfo.EmblemBrush = FSlateNoResource();
			SaveSlotInfo.CompanyShipCount = 0;
			SaveSlotInfo.CompanyMoney = 0;
			SaveSlotInfo.CompanyName = FText::FromString("");
		}

		SaveSlots.Add(SaveSlotInfo);
	}

	FLOG("AFlareGame::ReadAllSaveSlots : all slots found");
}

int32 AFlareGame::GetSaveSlotCount() const
{
	return SaveSlotCount;
}

int32 AFlareGame::GetCurrentSaveSlot() const
{
	return CurrentSaveIndex;
}

bool AFlareGame::DoesSaveSlotExist(int32 Index) const
{
	int32 RealIndex = Index - 1;
	return RealIndex < SaveSlots.Num() && SaveSlots[RealIndex].Save;
}

const FFlareSaveSlotInfo& AFlareGame::GetSaveSlotInfo(int32 Index)
{
	int32 RealIndex = Index - 1;
	return SaveSlots[RealIndex];
}

UFlareSaveGame* AFlareGame::ReadSaveSlot(int32 Index)
{
	FString SaveFile = "SaveSlot" + FString::FromInt(Index);
	if (UGameplayStatics::DoesSaveGameExist(SaveFile, 0))
	{
		UFlareSaveGame* Save = Cast<UFlareSaveGame>(UGameplayStatics::CreateSaveGameObject(UFlareSaveGame::StaticClass()));
		Save = Cast<UFlareSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveFile, 0));
		return Save;
	}
	else
	{
		return NULL;
	}
}

bool AFlareGame::DeleteSaveSlot(int32 Index)
{
	FString SaveFile = "SaveSlot" + FString::FromInt(Index);
	if (UGameplayStatics::DoesSaveGameExist(SaveFile, 0))
	{
		return UGameplayStatics::DeleteGameInSlot(SaveFile, 0);
	}
	else
	{
		return false;
	}
}


/*----------------------------------------------------
	Save
----------------------------------------------------*/

void AFlareGame::CreateWorld(AFlarePlayerController* PC)
{
	FFlarePlayerSave PlayerData;

	// Player company
	UFlareCompany* Company = CreateCompany("Player Inc");
	PlayerData.CompanyIdentifier = Company->GetIdentifier();
	PC->SetCompany(Company);

	// Enemy
	CreateCompany("Evil Corp");

	// Player ship
	AFlareSpacecraft* ShipPawn = CreateShipForMe(FName("ship-ghoul"));
	PlayerData.CurrentShipName = ShipPawn->GetImmatriculation();

	// Load
	PC->Load(PlayerData);
	LoadedOrCreated = true;
	PC->OnLoadComplete();
}

AFlareSpacecraft* AFlareGame::CreateStation(FName StationClass, FName CompanyIdentifier, FVector TargetPosition)
{
	FFlareSpacecraftDescription* Desc = GetSpacecraftCatalog()->Get(StationClass);

	if (!Desc)
	{
		Desc = GetSpacecraftCatalog()->Get(FName(*("station-" + StationClass.ToString())));
	}

	if (Desc)
	{
		return CreateShip(Desc, CompanyIdentifier, TargetPosition);
	}
	return NULL;
}

AFlareSpacecraft* AFlareGame::CreateShip(FName ShipClass, FName CompanyIdentifier, FVector TargetPosition)
{
	FFlareSpacecraftDescription* Desc = GetSpacecraftCatalog()->Get(ShipClass);

	if (!Desc)
	{
		Desc = GetSpacecraftCatalog()->Get(FName(*("ship-" + ShipClass.ToString())));
	}

	if (Desc)
	{
		return CreateShip(Desc, CompanyIdentifier, TargetPosition);
	}
	return NULL;
}

AFlareSpacecraft* AFlareGame::CreateShip(FFlareSpacecraftDescription* ShipDescription, FName CompanyIdentifier, FVector TargetPosition)
{
	AFlareSpacecraft* ShipPawn = NULL;
	UFlareCompany* Company = FindCompany(CompanyIdentifier);

	if (ShipDescription && Company)
	{
		// Default data
		FFlareSpacecraftSave ShipData;
		ShipData.Location = TargetPosition;
		ShipData.Rotation = FRotator::ZeroRotator;
		ShipData.LinearVelocity = FVector::ZeroVector;
		ShipData.AngularVelocity = FVector::ZeroVector;
		Immatriculate(Company, ShipDescription->Identifier, &ShipData);
		ShipData.Identifier = ShipDescription->Identifier;
		ShipData.Heat = 600 * ShipDescription->HeatCapacity;
		ShipData.PowerOutageDelay = 0;
		ShipData.PowerOutageAcculumator = 0;

		FName RCSIdentifier;
		FName OrbitalEngineIdentifier;

		// Size selector
		if (ShipDescription->Size == EFlarePartSize::S)
		{
			RCSIdentifier = FName("rcs-piranha");
			OrbitalEngineIdentifier = FName("engine-octopus");
		}
		else if (ShipDescription->Size == EFlarePartSize::L)
		{
			RCSIdentifier = FName("rcs-rift");
			OrbitalEngineIdentifier = FName("pod-surtsey");
		}
		else
		{
			// TODO
		}

		for (int32 i = 0; i < ShipDescription->RCSCount; i++)
		{
			FFlareSpacecraftComponentSave ComponentData;
			ComponentData.ComponentIdentifier = RCSIdentifier;
			ComponentData.ShipSlotIdentifier = FName(*("rcs-" + FString::FromInt(i)));
			ComponentData.Damage = 0.f;
			ShipData.Components.Add(ComponentData);
		}

		for (int32 i = 0; i < ShipDescription->OrbitalEngineCount; i++)
		{
			FFlareSpacecraftComponentSave ComponentData;
			ComponentData.ComponentIdentifier = OrbitalEngineIdentifier;
			ComponentData.ShipSlotIdentifier = FName(*("engine-" + FString::FromInt(i)));
			ComponentData.Damage = 0.f;
			ShipData.Components.Add(ComponentData);
		}

		for (int32 i = 0; i < ShipDescription->GunSlots.Num(); i++)
		{
			FFlareSpacecraftComponentSave ComponentData;
			ComponentData.ComponentIdentifier = DefaultWeaponIdentifer;
			ComponentData.ShipSlotIdentifier = ShipDescription->GunSlots[i].SlotIdentifier;
			ComponentData.Damage = 0.f;
			ComponentData.Weapon.FiredAmmo = 0;
			ShipData.Components.Add(ComponentData);
		}

		for (int32 i = 0; i < ShipDescription->TurretSlots.Num(); i++)
		{
			FFlareSpacecraftComponentSave ComponentData;
			ComponentData.ComponentIdentifier = DefaultTurretIdentifer;
			ComponentData.ShipSlotIdentifier = ShipDescription->TurretSlots[i].SlotIdentifier;
			ComponentData.Turret.BarrelsAngle = 0;
			ComponentData.Turret.TurretAngle = 0;
			ComponentData.Weapon.FiredAmmo = 0;
			ComponentData.Damage = 0.f;
			ShipData.Components.Add(ComponentData);
		}

		for (int32 i = 0; i < ShipDescription->InternalComponentSlots.Num(); i++)
		{
			FFlareSpacecraftComponentSave ComponentData;
			ComponentData.ComponentIdentifier = ShipDescription->InternalComponentSlots[i].ComponentIdentifier;
			ComponentData.ShipSlotIdentifier = ShipDescription->InternalComponentSlots[i].SlotIdentifier;
			ComponentData.Damage = 0.f;
			ShipData.Components.Add(ComponentData);
		}

		// Init pilot
		ShipData.Pilot.Identifier = "chewie";
		ShipData.Pilot.Name = "Chewbaca";

		// Init company
		ShipData.CompanyIdentifier = CompanyIdentifier;

		// Create the ship
		ShipPawn = LoadShip(ShipData);
		FLOGV("AFlareGame::CreateShip : Created ship '%s' at %s", *ShipPawn->GetImmatriculation(), *TargetPosition.ToString());
	}

	return ShipPawn;
}

bool AFlareGame::LoadWorld(AFlarePlayerController* PC, int32 Index)
{
	FLOGV("AFlareGame::LoadWorld : loading from slot %d", Index);
	UFlareSaveGame* Save = ReadSaveSlot(Index);
	CurrentSaveIndex = Index;

	// Load from save
	if (PC && Save)
	{
		// Load
		CurrentImmatriculationIndex = Save->CurrentImmatriculationIndex;

		// Load all companies
		for (int32 i = 0; i < Save->CompanyData.Num(); i++)
		{
			LoadCompany(Save->CompanyData[i]);
		}

		// Load the player
		PC->Load(Save->PlayerData);

		// Load all stations
		for (int32 i = 0; i < Save->StationData.Num(); i++)
		{
			LoadShip(Save->StationData[i]);
		}

		// Load all ships
		for (int32 i = 0; i < Save->ShipData.Num(); i++)
		{
			LoadShip(Save->ShipData[i]);
		}

		// Load all bombs
		for (int32 i = 0; i < Save->BombData.Num(); i++)
		{
			LoadBomb(Save->BombData[i]);
		}

		// Load all asteroids
		for (int32 i = 0; i < Save->AsteroidData.Num(); i++)
		{
			LoadAsteroid(Save->AsteroidData[i]);
		}

		LoadedOrCreated = true;
		PC->OnLoadComplete();
		return true;
	}

	// No file existing
	else
	{
		FLOGV("AFlareGame::LoadWorld : could lot load slot %d", Index);
		return false;
	}
}

UFlareCompany* AFlareGame::LoadCompany(const FFlareCompanySave& CompanyData)
{
	UFlareCompany* Company = NULL;
	FLOGV("AFlareGame::LoadCompany ('%s')", *CompanyData.Name);

	// Create the new company
	Company = NewObject<UFlareCompany>(this, UFlareCompany::StaticClass(), CompanyData.Identifier);
	Company->Load(CompanyData);
	Companies.AddUnique(Company);

	return Company;
}

AFlareAsteroid* AFlareGame::LoadAsteroid(const FFlareAsteroidSave& AsteroidData)
{
	FActorSpawnParameters Params;
	Params.bNoFail = true;

	AFlareAsteroid* Asteroid = GetWorld()->SpawnActor<AFlareAsteroid>(AFlareAsteroid::StaticClass(), AsteroidData.Location, AsteroidData.Rotation, Params);
	Asteroid->Load(AsteroidData);
	return Asteroid;
}

AFlareSpacecraft* AFlareGame::LoadShip(const FFlareSpacecraftSave& ShipData)
{
	AFlareSpacecraft* Ship = NULL;
	FLOGV("AFlareGame::LoadShip ('%s')", *ShipData.Immatriculation.ToString());

	if (SpacecraftCatalog)
	{
		FFlareSpacecraftDescription* Desc = SpacecraftCatalog->Get(ShipData.Identifier);
		if (Desc)
		{
			// Spawn parameters
			FActorSpawnParameters Params;
			Params.bNoFail = true;

			// Create and configure the ship
			Ship = GetWorld()->SpawnActor<AFlareSpacecraft>(Desc->Template->GeneratedClass, ShipData.Location, ShipData.Rotation, Params);
			if (Ship)
			{
				Ship->Load(ShipData);
				UPrimitiveComponent* RootComponent = Cast<UPrimitiveComponent>(Ship->GetRootComponent());
				RootComponent->SetPhysicsLinearVelocity(ShipData.LinearVelocity, false);
				RootComponent->SetPhysicsAngularVelocity(ShipData.AngularVelocity, false);
			}
			else
			{
				FLOG("AFlareGame::LoadShip fail to create AFlareSpacecraft");
			}
		}
		else
		{
			FLOG("AFlareGame::LoadShip failed (no description available)");
		}
	}
	else
	{
		FLOG("AFlareGame::LoadShip failed (no catalog data)");
	}

	return Ship;
}

AFlareBomb* AFlareGame::LoadBomb(const FFlareBombSave& BombData)
{
	AFlareBomb* Bomb = NULL;
	FLOG("AFlareGame::LoadBomb");

	AFlareSpacecraft* ParentSpacecraft = NULL;

	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AFlareSpacecraft* SpacecraftCandidate = Cast<AFlareSpacecraft>(*ActorItr);
		if (SpacecraftCandidate && SpacecraftCandidate->GetImmatriculation() == BombData.ParentSpacecraft)
		{
			ParentSpacecraft = SpacecraftCandidate;
			break;
		}
	}

	if (ParentSpacecraft)
	{
		UFlareWeapon* ParentWeapon = NULL;
		TArray<UActorComponent*> Components = ParentSpacecraft->GetComponentsByClass(UFlareSpacecraftComponent::StaticClass());
		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
		{
			UFlareWeapon* WeaponCandidate = Cast<UFlareWeapon>(Components[ComponentIndex]);
			if (WeaponCandidate && WeaponCandidate->SlotIdentifier == BombData.WeaponSlotIdentifier)
			{

				ParentWeapon = WeaponCandidate;
				break;
			}
		}

		if (ParentWeapon)
		{
			// Spawn parameters
			FActorSpawnParameters Params;
			Params.bNoFail = true;

			// Create and configure the ship
			Bomb = GetWorld()->SpawnActor<AFlareBomb>(AFlareBomb::StaticClass(), BombData.Location, BombData.Rotation, Params);
			if (Bomb)
			{
				Bomb->Initialize(&BombData, ParentWeapon);

				UPrimitiveComponent* RootComponent = Cast<UPrimitiveComponent>(Bomb->GetRootComponent());

				RootComponent->SetPhysicsLinearVelocity(BombData.LinearVelocity, false);
				RootComponent->SetPhysicsAngularVelocity(BombData.AngularVelocity, false);
			}
			else
			{
				FLOG("AFlareGame::LoadBomb fail to create AFlareBom");
			}
		}
		else
		{
			FLOG("AFlareGame::LoadBomb failed (no parent weapon)");
		}
	}
	else
	{
		FLOG("AFlareGame::LoadBomb failed (no parent ship)");
	}

	return Bomb;
}

bool AFlareGame::SaveWorld(AFlarePlayerController* PC)
{
	FLOGV("AFlareGame::SaveWorld : saving to slot %d", CurrentSaveIndex);
	UFlareSaveGame* Save = Cast<UFlareSaveGame>(UGameplayStatics::CreateSaveGameObject(UFlareSaveGame::StaticClass()));

	// Save process
	if (PC && Save)
	{
		// Save the player
		PC->Save(Save->PlayerData);
		Save->ShipData.Empty();
		Save->CurrentImmatriculationIndex = CurrentImmatriculationIndex;

		// Save all physical ships
		for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
		{
			// Tentative casts
			AFlareMenuPawn* MenuPawn = PC->GetMenuPawn();
			AFlareSpacecraft* Ship = Cast<AFlareSpacecraft>(*ActorItr);
			AFlareAsteroid* Asteroid = Cast<AFlareAsteroid>(*ActorItr);

			// Ship
			if (Ship && Ship->GetDescription() && !Ship->IsStation() && (MenuPawn == NULL || Ship != MenuPawn->GetCurrentSpacecraft()))
			{
				FLOGV("AFlareGame::SaveWorld : saving ship ('%s')", *Ship->GetImmatriculation());
				FFlareSpacecraftSave* TempData = Ship->Save();
				Save->ShipData.Add(*TempData);
			}

			// Station
			else if (Ship && Ship->GetDescription() && Ship->IsStation() && (MenuPawn == NULL || Ship != MenuPawn->GetCurrentSpacecraft()))
			{
				FLOGV("AFlareGame::SaveWorld : saving station ('%s')", *Ship->GetImmatriculation());
				FFlareSpacecraftSave* TempData = Ship->Save();
				Save->StationData.Add(*TempData);
			}

			// Asteroid
			else if (Asteroid)
			{
				FLOGV("AFlareGame::SaveWorld : saving asteroid ('%s')", *Asteroid->GetName());
				FFlareAsteroidSave* TempData = Asteroid->Save();
				Save->AsteroidData.Add(*TempData);
			}
		}

		// Companies
		for (TObjectIterator<UFlareCompany> ObjectItr; ObjectItr; ++ObjectItr)
		{
			UFlareCompany* Company = Cast<UFlareCompany>(*ObjectItr);
			if (Company)
			{
				FLOGV("AFlareGame::SaveWorld : saving company ('%s')", *Company->GetName());
				FFlareCompanySave* TempData = Company->Save();
				Save->CompanyData.Add(*TempData);
			}
		}

		// Bombs
		for (TObjectIterator<AFlareBomb> ObjectItr; ObjectItr; ++ObjectItr)
		{
			AFlareBomb* Bomb = Cast<AFlareBomb>(*ObjectItr);
			if (Bomb && Bomb->IsDropped())
			{
				FLOGV("AFlareGame::SaveWorld : saving bomb ('%s')", *Bomb->GetName());
				FFlareBombSave* TempData = Bomb->Save();
				Save->BombData.Add(*TempData);
			}
		}

		// Save
		UGameplayStatics::SaveGameToSlot(Save, "SaveSlot" + FString::FromInt(CurrentSaveIndex), 0);
		return true;
	}

	// No PC
	else
	{
		FLOG("AFlareGame::SaveWorld failed");
		return false;
	}
}

void AFlareGame::DeleteWorld()
{
	FLOG("AFlareGame::DeleteWorld");
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AFlareSpacecraft* SpacecraftCandidate = Cast<AFlareSpacecraft>(*ActorItr);
		if (SpacecraftCandidate && !SpacecraftCandidate->IsPresentationMode())
		{
			SpacecraftCandidate->Destroy();
		}

		AFlareBomb* BombCandidate = Cast<AFlareBomb>(*ActorItr);
		if (BombCandidate)
		{
			BombCandidate->Destroy();
		}

		AFlareShell* ShellCandidate = Cast<AFlareShell>(*ActorItr);
		if (ShellCandidate)
		{
			ShellCandidate->Destroy();
		}

		AFlareAsteroid* AsteroidCandidate = Cast<AFlareAsteroid>(*ActorItr);
		if (AsteroidCandidate)
		{
			AsteroidCandidate->Destroy();
		}
	}

	Companies.Empty();
	LoadedOrCreated = false;
}


/*----------------------------------------------------
	Creation tools
----------------------------------------------------*/

UFlareCompany* AFlareGame::CreateCompany(FString CompanyName)
{
	UFlareCompany* Company = NULL;
	FFlareCompanySave CompanyData;

	// Generate identifier
	CurrentImmatriculationIndex++;
	FString Immatriculation = FString::Printf(TEXT("CPNY-%06d"), CurrentImmatriculationIndex);

	// Generate arbitrary save data
	CompanyData.Name = CompanyName.ToUpper();
	CompanyData.ShortName = *CompanyName.Left(3).ToUpper();
	CompanyData.Identifier = *Immatriculation;
	CompanyData.Money = 100000;
	CompanyData.CustomizationBasePaintColorIndex = 0;
	CompanyData.CustomizationPaintColorIndex = 4;
	CompanyData.CustomizationOverlayColorIndex = FMath::RandRange(1, 15);
	CompanyData.CustomizationLightColorIndex = CompanyData.CustomizationOverlayColorIndex;
	CompanyData.CustomizationPatternIndex = 0;

	// Create company
	Company = LoadCompany(CompanyData);
	FLOGV("AFlareGame::CreateCompany : Created company '%s'", *Company->GetName());

	return Company;
}

AFlareSpacecraft* AFlareGame::CreateStationForMe(FName StationClass)
{
	AFlareSpacecraft* StationPawn = NULL;
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());

	// Parent company
	if (PC && PC->GetCompany())
	{
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		FVector TargetPosition = FVector::ZeroVector;
		if (ExistingShipPawn)
		{
			TargetPosition = ExistingShipPawn->GetActorLocation() + ExistingShipPawn->GetActorRotation().RotateVector(10000 * FVector(1, 0, 0));
		}

		StationPawn = CreateStation(StationClass, PC->GetCompany()->GetIdentifier(), TargetPosition);
	}
	return StationPawn;
}

AFlareSpacecraft* AFlareGame::CreateStationInCompany(FName StationClass, FName CompanyShortName, float Distance)
{
	AFlareSpacecraft* StationPawn = NULL;
	FVector TargetPosition = FVector::ZeroVector;

	// Get target position
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());
	if (PC)
	{
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		if (ExistingShipPawn)
		{
			TargetPosition = ExistingShipPawn->GetActorLocation() + ExistingShipPawn->GetActorRotation().RotateVector(Distance * 100 * FVector(1, 0, 0));
		}
	}

	// Find company
	for (TObjectIterator<UFlareCompany> ObjectItr; ObjectItr; ++ObjectItr)
	{
		UFlareCompany* Company = Cast<UFlareCompany>(*ObjectItr);
		if (Company && Company->GetShortName() == CompanyShortName)
		{
			StationPawn = CreateStation(StationClass, Company->GetIdentifier(), TargetPosition);
			break;
		}
	}
	return StationPawn;
} 

AFlareSpacecraft* AFlareGame::CreateShipForMe(FName ShipClass)
{
	AFlareSpacecraft* ShipPawn = NULL;
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());

	// Parent company
	if (PC && PC->GetCompany())
	{
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		FVector TargetPosition = FVector::ZeroVector;
		if (ExistingShipPawn)
		{
			TargetPosition = ExistingShipPawn->GetActorLocation() + ExistingShipPawn->GetActorRotation().RotateVector(10000 * FVector(1, 0, 0));
		}

		ShipPawn = CreateShip(ShipClass, PC->GetCompany()->GetIdentifier(), TargetPosition);
	}
	return ShipPawn;
}


AFlareSpacecraft* AFlareGame::CreateShipInCompany(FName ShipClass, FName CompanyShortName, float Distance)
{
	AFlareSpacecraft* ShipPawn = NULL;
	FVector TargetPosition = FVector::ZeroVector;

	// Get target position
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());
	if (PC)
	{
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		if (ExistingShipPawn)
		{
			TargetPosition = ExistingShipPawn->GetActorLocation() + ExistingShipPawn->GetActorRotation().RotateVector(Distance * 100 * FVector(1, 0, 0));
		}
	}

	// Find company
	for (TObjectIterator<UFlareCompany> ObjectItr; ObjectItr; ++ObjectItr)
	{
		UFlareCompany* Company = Cast<UFlareCompany>(*ObjectItr);
		if (Company && Company->GetShortName() == CompanyShortName)
		{
			ShipPawn = CreateShip(ShipClass, Company->GetIdentifier(), TargetPosition);
			break;
		}
	}
	return ShipPawn;
}

void AFlareGame::CreateShipsInCompany(FName ShipClass, FName CompanyShortName, float Distance, int32 Count)
{
	AFlareSpacecraft* ShipPawn = NULL;
	FVector TargetPosition = FVector::ZeroVector;
	FVector BaseShift = FVector::ZeroVector;

	// Get target position
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());
	if (PC)
	{
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		if (ExistingShipPawn)
		{
			TargetPosition = ExistingShipPawn->GetActorLocation() + ExistingShipPawn->GetActorRotation().RotateVector(Distance * 100 * FVector(1, 0, 0));
			BaseShift = ExistingShipPawn->GetActorRotation().RotateVector(10000 * FVector(0, 1, 0)); // 100m
		}
	}

	// Find company
	for (TObjectIterator<UFlareCompany> ObjectItr; ObjectItr; ++ObjectItr)
	{
		UFlareCompany* Company = Cast<UFlareCompany>(*ObjectItr);
		if (Company && Company->GetShortName() == CompanyShortName)
		{
			for (int32 ShipIndex = 0; ShipIndex < Count; ShipIndex++)
			{
				FVector Shift = (BaseShift * (ShipIndex + 1) / 2) * (ShipIndex % 2 == 0 ? 1:-1);
				CreateShip(ShipClass, Company->GetIdentifier(), TargetPosition + Shift);
			}

			break;
		}
	}
}

void AFlareGame::CreateQuickBattle(float Distance, FName Company1, FName Company2, FName ShipClass1, int32 ShipClass1Count, FName ShipClass2, int32 ShipClass2Count)
{
	FVector BasePosition = FVector::ZeroVector;
	FVector BaseOffset = FVector(1.f, 0.f, 0.f) * Distance / 50.f; // Half the distance in cm
	FVector BaseShift = FVector(0.f, 30000.f, 0.f);  // 100 m
	FVector BaseDeep = FVector(30000.f, 0.f, 0.f); // 100 m

	FName Company1Identifier;
	FName Company2Identifier;

	for (TObjectIterator<UFlareCompany> ObjectItr; ObjectItr; ++ObjectItr)
	{
		UFlareCompany* Company = Cast<UFlareCompany>(*ObjectItr);
		if (Company && Company->GetShortName() == Company1)
		{
			Company1Identifier = Company->GetIdentifier();
		}

		if (Company && Company->GetShortName() == Company2)
		{
			Company2Identifier = Company->GetIdentifier();
		}
	}


	// Get target position
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());
	if (PC)
	{
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		if (ExistingShipPawn)
		{
			BasePosition = ExistingShipPawn->GetActorLocation();
			BaseOffset = ExistingShipPawn->GetActorRotation().RotateVector(Distance * 50.f * FVector(1, 0, 0)); // Half the distance in cm
			BaseDeep = ExistingShipPawn->GetActorRotation().RotateVector(FVector(30000.f, 0, 0)); // 100 m in cm
			BaseShift = ExistingShipPawn->GetActorRotation().RotateVector(FVector(0, 30000.f, 0)); // 300 m in cm
		}
	}


	for (int32 ShipIndex = 0; ShipIndex < ShipClass1Count; ShipIndex++)
	{
		FVector Shift = (BaseShift * (ShipIndex + 1) / 2) * (ShipIndex % 2 == 0 ? 1 : -1);
		CreateShip(ShipClass1, Company1Identifier, BasePosition + BaseOffset + Shift);
		CreateShip(ShipClass1, Company2Identifier, BasePosition - BaseOffset - Shift);
	}

	for (int32 ShipIndex = 0; ShipIndex < ShipClass2Count; ShipIndex++)
	{
		FVector Shift = (BaseShift * (ShipIndex + 1) / 2) * (ShipIndex % 2 == 0 ? 1 : -1);
		CreateShip(ShipClass2, Company1Identifier, BasePosition + BaseOffset + Shift + BaseDeep);
		CreateShip(ShipClass2, Company2Identifier, BasePosition - BaseOffset - Shift - BaseDeep);
	}
}

void AFlareGame::SetDefaultWeapon(FName NewDefaultWeaponIdentifier)
{
	FFlareSpacecraftComponentDescription* ComponentDescription = ShipPartsCatalog->Get(NewDefaultWeaponIdentifier);

	if (ComponentDescription && ComponentDescription->WeaponCharacteristics.IsWeapon)
	{
		DefaultWeaponIdentifer = NewDefaultWeaponIdentifier;
	}
	else
	{
		FLOGV("Bad weapon identifier: %s", *NewDefaultWeaponIdentifier.ToString())
	}
}

void AFlareGame::SetDefaultTurret(FName NewDefaultTurretIdentifier)
{
	FFlareSpacecraftComponentDescription* ComponentDescription = ShipPartsCatalog->Get(NewDefaultTurretIdentifier);

	if (ComponentDescription && ComponentDescription->WeaponCharacteristics.IsWeapon && ComponentDescription->WeaponCharacteristics.TurretCharacteristics.IsTurret)
	{
		DefaultTurretIdentifer = NewDefaultTurretIdentifier;
	}
	else
	{
		FLOGV("Bad weapon identifier: %s", *NewDefaultTurretIdentifier.ToString())
	}
}

void AFlareGame::CreateAsteroid(int32 ID)
{
	AFlarePlayerController* PC = Cast<AFlarePlayerController>(GetWorld()->GetFirstPlayerController());

	if(ID >= GetAsteroidCatalog()->Asteroids.Num())
	{
		FLOGV("Astroid create fail : Asteroid max ID is %d", GetAsteroidCatalog()->Asteroids.Num() -1);
		return;
	}

	if (PC)
	{


		// Location
		AFlareSpacecraft* ExistingShipPawn = PC->GetShipPawn();
		FVector TargetPosition = FVector::ZeroVector;
		if (ExistingShipPawn)
		{
			TargetPosition = ExistingShipPawn->GetActorLocation() + ExistingShipPawn->GetActorRotation().RotateVector(20000 * FVector(1, 0, 0));
		}

		// Spawn parameters
		FActorSpawnParameters Params;
		Params.bNoFail = true;
		FFlareAsteroidSave Data;
		Data.AsteroidMeshID = ID;
		Data.LinearVelocity = FVector::ZeroVector;
		Data.AngularVelocity = FMath::VRand() * FMath::FRandRange(-1.f,1.f);
		Data.Scale = FVector(1,1,1) * FMath::FRandRange(0.9,1.1);
		FRotator Rotation = FRotator(FMath::FRandRange(0,360), FMath::FRandRange(0,360), FMath::FRandRange(0,360));

		// Spawn and setup
		AFlareAsteroid* Asteroid = GetWorld()->SpawnActor<AFlareAsteroid>(AFlareAsteroid::StaticClass(), TargetPosition, Rotation, Params);
		Asteroid->Load(Data);
	}
}


/*----------------------------------------------------
	Immatriculations
----------------------------------------------------*/

void AFlareGame::Immatriculate(UFlareCompany* Company, FName TargetClass, FFlareSpacecraftSave* SpacecraftSave)
{
	FString Immatriculation;
	FString NickName;
	CurrentImmatriculationIndex++;
	FFlareSpacecraftDescription* SpacecraftDesc = SpacecraftCatalog->Get(TargetClass);
	bool IsStation = IFlareSpacecraftInterface::IsStation(SpacecraftDesc);

	// Company name
	Immatriculation += Company->GetShortName().ToString();
	Immatriculation += "-";

	// Class
	if (IsStation)
	{
		Immatriculation += SpacecraftDesc->Name.ToString();
	}
	else
	{
		Immatriculation += SpacecraftDesc->ImmatriculationCode.ToString();
	}

	// Name
	if (SpacecraftDesc->Size == EFlarePartSize::L && !IsStation)
	{
		NickName = PickCapitalShipName().ToString();
	}
	else
	{
		NickName = FString::Printf(TEXT("%04d"), CurrentImmatriculationIndex);
	}
	Immatriculation += FString::Printf(TEXT("-%s"), *NickName);

	// Update data
	FLOGV("AFlareGame::Immatriculate (%s) : %s", *TargetClass.ToString(), *Immatriculation);
	SpacecraftSave->Immatriculation = FName(*Immatriculation);
	SpacecraftSave->NickName = FName(*NickName);
}

// convertToRoman:
//   In:  val: value to convert.
//        res: buffer to hold result.
//   Out: n/a
//   Cav: caller responsible for buffer size.

static FString ConvertToRoman(unsigned int val)
{
	FString Roman;

	const char *huns[] = {"", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"};
	const char *tens[] = {"", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"};
	const char *ones[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
	int size []  = { 0,   1,    2,    3,     2,   1,    2,     3,      4,    2};

	//  Add 'M' until we drop below 1000.
	while (val >= 1000) {
		Roman += FString("M");
		val -= 1000;
	}

	// Add each of the correct elements, adjusting as we go.

	Roman += FString(huns[val/100]);
	val = val % 100;
	Roman += FString(tens[val/10]);
	val = val % 10;
	Roman += FString(ones[val]);
	return Roman;
}

FName AFlareGame::PickCapitalShipName()
{
	if (BaseImmatriculationNameList.Num() == 0)
	{
		InitCapitalShipNameDatabase();
	}
	int32 PickIndex = FMath::RandRange(0,BaseImmatriculationNameList.Num()-1);

	FName BaseName = BaseImmatriculationNameList[PickIndex];

	// Check unicity
	bool Unique;
	int32 NameIncrement = 1;
	FName CandidateName;
	do
	{
		Unique = true;
		FLOGV("Pass %d with %s", NameIncrement, *CandidateName.ToString());
		FString Suffix;
		if (NameIncrement > 1)
		{
			FString Roman = ConvertToRoman(NameIncrement);
			FLOGV("ConvertToRoman %s", *Roman);
			Suffix = FString("-") + Roman;
		}
		else
		{
			Suffix = FString("");
		}
		FLOGV("Suffix %s", *Suffix);

		CandidateName = FName(*(BaseName.ToString()+Suffix));
		FLOGV("CandidateName %s", *CandidateName.ToString());

		for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
		{
			AFlareSpacecraft* SpacecraftCandidate = Cast<AFlareSpacecraft>(*ActorItr);
			if (SpacecraftCandidate && SpacecraftCandidate->GetNickName() == CandidateName)
			{
				FLOGV("Not unique %s", *CandidateName.ToString());
				Unique = false;
				break;
			}
		}
		NameIncrement++;
	} while(!Unique);

	FLOGV("OK for %s", *CandidateName.ToString());

	return CandidateName;
}

void AFlareGame::InitCapitalShipNameDatabase()
{
	BaseImmatriculationNameList.Empty();
	BaseImmatriculationNameList.Add("Revenge");
	BaseImmatriculationNameList.Add("Sovereign");
	BaseImmatriculationNameList.Add("Stalker");
	BaseImmatriculationNameList.Add("Leviathan");
	BaseImmatriculationNameList.Add("Resolve");
	BaseImmatriculationNameList.Add("Explorer");
	BaseImmatriculationNameList.Add("Arrow");
	BaseImmatriculationNameList.Add("Intruder");
	BaseImmatriculationNameList.Add("Goliath");
	BaseImmatriculationNameList.Add("Shrike");
	BaseImmatriculationNameList.Add("Thunder");
	BaseImmatriculationNameList.Add("Enterprise");
	BaseImmatriculationNameList.Add("Sahara");
}
