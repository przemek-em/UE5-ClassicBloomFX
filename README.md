# ClassicBloomFX

A custom bloom post-process plugin for **Unreal Engine 5.6+** that provides classic bloom effects.

![UE5](https://img.shields.io/badge/Unreal%20Engine-5.6+-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Experimental](https://img.shields.io/badge/Status-Experimental-orange.svg)

## Features

- **4 Bloom Modes:**
  - **Standard (Gaussian)** – Classic highlight glow
  - **Directional Glare** – Star/cross streaks from bright areas
  - **Kawase** – Physically-based pyramid blur bloom
  - **Soft Focus** – Full-scene dreamy glow effect

- **6 Blend Modes:** Screen, Overlay, Soft Light, Hard Light, Lighten, Multiply

- **Quality Controls:** Downsample scale, blur passes, blur samples, high-quality upsampling

- **Advanced Options:** Saturation boost, highlight protection, adaptive brightness scaling

## Installation

1. Copy the `ClassicBloomFX` folder to your project's or editor `Plugins/Experimental` directory
2. Enable the plugin in Edit → Plugins → "Classic Bloom FX"
3. Restart the editor

## Quick Start

Disable default Unreal Bloom in Project → Engine - Rendering → Bloom, or place PostProcessVolume with Bloom Intensity set to 0, then:
1. Place a **BloomFX Component** on any actor in your level (i recommend using just simple blueprint with component enabled)
2. Adjust **Bloom Mode** and **Bloom Intensity** in the Details panel
3. The effect applies automatically to the viewport

## Bloom Modes

| Mode | Description | Best For |
|------|-------------|----------|
| **Standard** | Gaussian blur bloom from highlights | Realistic light glow |
| **Directional Glare** | Star-shaped streaks | Anamorphic lens flares |
| **Kawase** | Progressive pyramid blur | Soft, natural bloom |
| **Soft Focus** | Full-scene dreamy glow | Cinematic, romantic scenes |


## Example setup
Set Bloom Mode to Soft Focus - it will automatically change Bloom Blend Mode to Overlay - you can use for example Soft light that will work better in some cases. 
- Bloom Intensity to 1.5, Size to 32.0
- Downsample Scale 2.0
- Blur Passes 1
- Blur Samples 5
- Use Adaptive Brightness Scaling True

Be careful with Kawase mode, it works good with lower bloom intensity values, Bloom Blend mode set to Screen.


## Key Properties

| Property | Description |
|----------|-------------|
| `BloomIntensity` | Overall bloom strength (0–8) |
| `BloomThreshold` | Brightness cutoff for bloom (0–10) |
| `BloomSize` | Blur radius / glow size |
| `BloomBlendMode` | How bloom composites onto scene |
| `BloomSaturation` | Color vibrancy of bloom |
| `DownsampleScale` | Quality vs performance (0.25–2.0) |

## Requirements

- Unreal Engine 5.6 or later
- Shader Model 5 compatible GPU

## Building
Edit cleanbuild_classicbloomfx.ps1 inside project root, replace with your engine directories then build.

## License

MIT License – See [LICENSE](LICENSE) for details.

### Screenshots
![Image](images/screen_1.png)
![Image](images/screen4.png)
![Image](images/screen5.png)
![Image](images/screen6.png)
![Image](images/screen7.png)
![Image](images/screen8.png)
![Image](images/meadow2.png)
![Image](images/screen_2.png)
![Image](images/screen_3.png)
