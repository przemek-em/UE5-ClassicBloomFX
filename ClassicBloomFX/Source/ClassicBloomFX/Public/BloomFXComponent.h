// Licensed under the MIT License. See LICENSE file in the project root.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BloomFXComponent.generated.h"

/** Post-process pass to apply bloom after */
UENUM(BlueprintType)
enum class EBloomPostProcessPass : uint8
{
	/** Apply after tone mapping (best color/highlight preservation, works in editor and most games) */
	Tonemap UMETA(DisplayName = "Tonemap (Recommended)"),
	/** Apply after motion blur (more compatible but may wash out highlights in some cases) */
	MotionBlur UMETA(DisplayName = "Motion Blur"),
	/** Apply after FXAA (only works if FXAA is enabled) */
	FXAA UMETA(DisplayName = "FXAA"),
	/** Apply after visualize depth of field */
	VisualizeDepthOfField UMETA(DisplayName = "Visualize DOF")
};

/** Blend mode for compositing bloom back onto the scene */
UENUM(BlueprintType)
enum class EBloomBlendMode : uint8
{
	/** Screen blend - Photographic glow effect (recommended) */
	Screen UMETA(DisplayName = "Screen (Recommended)"),
	/** Overlay blend - High contrast glow */
	Overlay UMETA(DisplayName = "Overlay"),
	/** Soft light blend - Gentle, subtle glow */
	SoftLight UMETA(DisplayName = "Soft Light"),
	/** Hard light blend - Intense, punchy glow */
	HardLight UMETA(DisplayName = "Hard Light"),
	/** Lighten blend - Only brightens, never darkens */
	Lighten UMETA(DisplayName = "Lighten"),
	/** Multiply blend - Darkens scene with bloom */
	Multiply UMETA(DisplayName = "Multiply")
};

/** Bloom effect mode */
UENUM(BlueprintType)
enum class EBloomMode : uint8
{
	/** Standard Gaussian blur bloom */
	Standard UMETA(DisplayName = "Standard (Gaussian)"),
	/** Directional glare - star/cross streaks from bright areas */
	DirectionalGlare UMETA(DisplayName = "Directional Glare (Streaks)"),
	/** Kawase bloom - Progressive pyramid blur */
	Kawase UMETA(DisplayName = "Kawase"),
	/** Soft Focus - Dreamy full-scene glow effect */
	SoftFocus UMETA(DisplayName = "Soft Focus (Dreamy Glow)")
};

/**
 * Component that enables custom bloom effects in the scene
 * Place this component in your level to enable custom bloom
 */
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent), hidecategories=(Object, LOD, Lighting, TextureStreaming, Activation, "Components|Activation"))
class CLASSICBLOOMFX_API UBloomFXComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBloomFXComponent();

	// ========================================================================
	// Bloom Mode
	// ========================================================================
	
	/** Bloom effect mode - Standard Gaussian, Directional Glare, Kawase, or Soft Focus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Mode")
	EBloomMode BloomMode = EBloomMode::Standard;

	// ========================================================================
	// Bloom Settings (shared across modes)
	// ========================================================================
	
	/** Overall intensity of the bloom effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "8.0"))
	float BloomIntensity = 2.0f;

	/** Threshold for bloom - only pixels brighter than this will bloom (not used in Soft Focus mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0", EditCondition = "BloomMode != EBloomMode::SoftFocus"))
	float BloomThreshold = 0.8f;

	/** Size of the bloom effect (Standard and Glare modes only) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "64.0", EditCondition = "BloomMode == EBloomMode::Standard || BloomMode == EBloomMode::DirectionalGlare || BloomMode == EBloomMode::SoftFocus"))
	float BloomSize = 4.0f;

	/** Use scene colors for bloom (realistic) or apply tint color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings")
	bool bUseSceneColor = true;

	/** Tint color for the bloom (only used when Use Scene Color is disabled) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings", meta = (EditCondition = "!bUseSceneColor"))
	FLinearColor BloomTint = FLinearColor::White;

	/** Blend mode for compositing bloom onto the scene */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings")
	EBloomBlendMode BloomBlendMode = EBloomBlendMode::Screen;

	/** Saturation boost for bloom colors (1.0 = normal, >1.0 = more vibrant, <1.0 = desaturated) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "2.0"))
	float BloomSaturation = 1.0f;

	/** Protect highlights from over-brightening (prevents bloom from washing out to white) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings")
	bool bProtectHighlights = false;

	/** Highlight protection strength (higher = more protection, 0.0 = none, 1.0 = maximum) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bProtectHighlights"))
	float HighlightProtection = 0.5f;

	// ========================================================================
	// Bloom Quality (for Standard and Soft Focus modes)
	// ========================================================================

	/** Downsample scale (higher = better quality but slower). 1.0 = half res, 2.0 = full res */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Quality", meta = (ClampMin = "0.25", ClampMax = "2.0", UIMin = "0.5", UIMax = "2.0", EditCondition = "BloomMode == EBloomMode::Standard || BloomMode == EBloomMode::SoftFocus", EditConditionHides))
	float DownsampleScale = 1.0f;

	/** Number of blur passes (more passes = smoother bloom but slower) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Quality", meta = (ClampMin = "1", ClampMax = "4", UIMin = "1", UIMax = "4", EditCondition = "BloomMode == EBloomMode::Standard || BloomMode == EBloomMode::SoftFocus", EditConditionHides))
	int32 BlurPasses = 1;

	/** Blur quality - number of samples per tap (5, 9, or 13) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Quality", meta = (ClampMin = "5", ClampMax = "13", UIMin = "5", UIMax = "13", EditCondition = "BloomMode == EBloomMode::Standard || BloomMode == EBloomMode::SoftFocus", EditConditionHides))
	int32 BlurSamples = 5;

	/** Use high quality upsampling (slower but reduces pixelation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom Quality", meta = (EditCondition = "BloomMode == EBloomMode::Standard || BloomMode == EBloomMode::SoftFocus", EditConditionHides))
	bool bHighQualityUpsampling = false;

	// ========================================================================
	// Directional Glare Settings (only for DirectionalGlare mode)
	// ========================================================================

	/** Number of directional streaks (4-6 recommended for star patterns) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Glare Settings", meta = (ClampMin = "2", ClampMax = "16", UIMin = "2", UIMax = "12", EditCondition = "BloomMode == EBloomMode::DirectionalGlare", EditConditionHides))
	int32 GlareStreakCount = 6;

	/** Length of each streak in pixels (at full resolution) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Glare Settings", meta = (ClampMin = "5", ClampMax = "200", UIMin = "10", UIMax = "120", EditCondition = "BloomMode == EBloomMode::DirectionalGlare", EditConditionHides))
	int32 GlareStreakLength = 40;

	/** Rotation offset for streak directions in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Glare Settings", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "90.0", EditCondition = "BloomMode == EBloomMode::DirectionalGlare", EditConditionHides))
	float GlareRotationOffset = 0.0f;

	/** Exponential falloff rate for streak intensity (higher = faster falloff) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Glare Settings", meta = (ClampMin = "0.5", ClampMax = "10.0", UIMin = "1.0", UIMax = "5.0", EditCondition = "BloomMode == EBloomMode::DirectionalGlare", EditConditionHides))
	float GlareFalloff = 3.0f;

	// ========================================================================
	// Kawase Bloom Settings (only for Kawase mode)
	// ========================================================================

	/** Number of mip levels in the bloom pyramid (more = larger blur radius, 5-6 recommended) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawase Bloom Settings", meta = (ClampMin = "3", ClampMax = "8", UIMin = "4", UIMax = "7", EditCondition = "BloomMode == EBloomMode::Kawase", EditConditionHides))
	int32 KawaseMipCount = 5;

	/** Upsample filter radius (higher = softer bloom) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawase Bloom Settings", meta = (ClampMin = "0.0001", ClampMax = "0.01", UIMin = "0.001", UIMax = "0.005", EditCondition = "BloomMode == EBloomMode::Kawase", EditConditionHides))
	float KawaseFilterRadius = 0.002f;

	/** Apply soft color threshold instead of hard brightness cutoff */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawase Bloom Settings", meta = (EditCondition = "BloomMode == EBloomMode::Kawase", EditConditionHides))
	bool bKawaseSoftThreshold = true;

	/** Threshold knee - controls the smoothness of the threshold transition (0 = hard, 1 = very soft) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawase Bloom Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "BloomMode == EBloomMode::Kawase && bKawaseSoftThreshold", EditConditionHides))
	float KawaseThresholdKnee = 0.5f;

	// ========================================================================
	// Soft Focus Tuning (deprecated - soft focus now uses standard bloom settings)
	// These are kept for backward compatibility but hidden from UI
	// ========================================================================

	/** Soft focus overlay intensity multiplier (deprecated) */
	UPROPERTY()
	float SoftFocusOverlayMultiplier = 0.5f;

	/** Soft focus blend strength (deprecated) */
	UPROPERTY()
	float SoftFocusBlendStrength = 0.33f;

	/** Soft focus light intensity (deprecated) */
	UPROPERTY()
	float SoftFocusSoftLightMultiplier = 0.4f;

	/** Soft focus final blend factor (deprecated) */
	UPROPERTY()
	float SoftFocusFinalBlend = 0.25f;

	// ========================================================================
	// Advanced Settings
	// ========================================================================

	/** Post-process pass to apply effects after */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	EBloomPostProcessPass PostProcessPass = EBloomPostProcessPass::Tonemap;

	/** Use adaptive brightness scaling to normalize bloom between editor and game mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bUseAdaptiveBrightnessScaling = false;

	/** Manual game mode bloom compensation (1.0 = no change, <1.0 = reduce bloom in PIE) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ClampMin = "0.1", ClampMax = "2.0", UIMin = "0.5", UIMax = "1.5"))
	float GameModeBloomScale = 1.0f;

	// ========================================================================
	// Debug Settings
	// ========================================================================

	/** Enable debug logging to output log */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebugLogging = false;

	/** Show only bloom buffer (for debugging) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowBloomOnly = false;

	/** Show gamma compensation visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowGammaCompensation = false;

	/** Auto-reinitialize viewport rect on timer (workaround for viewport rect bugs) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bAutoReinitializeRect = false;

	/** Seconds between auto-reinitialize (only used if bAutoReinitializeRect is true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.5", UIMax = "5.0", EditCondition = "bAutoReinitializeRect"))
	float ReinitializeInterval = 1.0f;

	/** Manually trigger viewport rect reinitialize (click to fix misalignment) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bReinitializeRect = false;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();
	
	// Timer for auto-reinitialize
	float ReinitializeTimer = 0.0f;
};
