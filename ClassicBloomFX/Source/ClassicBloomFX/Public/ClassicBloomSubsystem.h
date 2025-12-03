// Licensed under the MIT License. See LICENSE file in the project root.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SceneViewExtension.h"
#include "ClassicBloomSubsystem.generated.h"

class UBloomFXComponent;

/**
 * Scene View Extension for Custom Bloom rendering
 */
class FClassicBloomSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FClassicBloomSceneViewExtension(const FAutoRegister& AutoRegister, UClassicBloomSubsystem* InSubsystem);
	virtual ~FClassicBloomSceneViewExtension() {}

	// ISceneViewExtension interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	
	virtual int32 GetPriority() const override { return 100; } // Higher priority to ensure it runs

private:
	TWeakObjectPtr<UClassicBloomSubsystem> WeakSubsystem;
	
	FScreenPassTexture PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);
};

/**
 * World Subsystem that manages custom bloom effects
 */
UCLASS()
class CLASSICBLOOMFX_API UClassicBloomSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Subsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Register/Unregister components
	void RegisterBloomComponent(UBloomFXComponent* Component);
	void UnregisterBloomComponent(UBloomFXComponent* Component);

	// Get all active bloom components
	const TArray<TWeakObjectPtr<UBloomFXComponent>>& GetBloomComponents() const { return BloomComponents; }

private:
	// Scene view extension for rendering
	TSharedPtr<FClassicBloomSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;

	// Registered bloom components
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UBloomFXComponent>> BloomComponents;
};
