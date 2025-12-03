// Licensed under the MIT License. See LICENSE file in the project root.

#include "BloomFXComponent.h"
#include "ClassicBloomSubsystem.h"
#include "Engine/World.h"

UBloomFXComponent::UBloomFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;  // Enable tick for auto-reinitialize
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;  // Tick late to avoid interfering with rendering
	bAutoActivate = true;
}

void UBloomFXComponent::OnRegister()
{
	Super::OnRegister();
	RegisterWithSubsystem();
}

void UBloomFXComponent::OnUnregister()
{
	UnregisterFromSubsystem();
	Super::OnUnregister();
}

void UBloomFXComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterWithSubsystem();
}

void UBloomFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSubsystem();
	Super::EndPlay(EndPlayReason);
}

void UBloomFXComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// Handle manual reinitialize button
	if (bReinitializeRect)
	{
		bReinitializeRect = false;  // Reset the button
		
		// Force unregister and re-register to reinitialize the render state
		UnregisterFromSubsystem();
		RegisterWithSubsystem();
		
		if (bEnableDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("ClassicBloom: Manual viewport rect reinitialize triggered"));
		}
	}
	
	// Handle auto-reinitialize timer
	if (bAutoReinitializeRect && ReinitializeInterval > 0.0f)
	{
		ReinitializeTimer += DeltaTime;
		
		if (ReinitializeTimer >= ReinitializeInterval)
		{
			ReinitializeTimer = 0.0f;
			
			// Force unregister and re-register to reinitialize the render state
			UnregisterFromSubsystem();
			RegisterWithSubsystem();
			
			if (bEnableDebugLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("ClassicBloom: Auto viewport rect reinitialize (interval: %.2f seconds)"), ReinitializeInterval);
			}
		}
	}
	else
	{
		// Reset timer if auto-reinit is disabled
		ReinitializeTimer = 0.0f;
	}
}

void UBloomFXComponent::RegisterWithSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UClassicBloomSubsystem* Subsystem = World->GetSubsystem<UClassicBloomSubsystem>())
		{
			Subsystem->RegisterBloomComponent(this);
		}
	}
}

void UBloomFXComponent::UnregisterFromSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UClassicBloomSubsystem* Subsystem = World->GetSubsystem<UClassicBloomSubsystem>())
		{
			Subsystem->UnregisterBloomComponent(this);
		}
	}
}

#if WITH_EDITOR
void UBloomFXComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	// When switching to Soft Focus mode, auto-select Overlay blend mode
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UBloomFXComponent, BloomMode))
	{
		if (BloomMode == EBloomMode::SoftFocus)
		{
			BloomBlendMode = EBloomBlendMode::Overlay;
		}
	}
}
#endif
