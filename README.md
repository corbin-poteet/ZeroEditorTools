# Zero Editor Tools

An Unreal Engine 5.7 editor plugin that imports Star Wars: Battlefront (2004) and Star Wars: Battlefront II (2005) munged LVL files into native UE assets.

Drop an LVL file into the Content Browser to import textured meshes, advanced material instances, terrain, world layout actors, lighting, regions, skydome geometry, barriers, hint nodes, and per-asset SWBF metadata.

---

## Features

### Static Meshes

- Full LOD support
- Collision geometry from LVL collision data

### Textures

- Extraction and creation of `UTexture2D` assets from LVL texture data

### Materials

- Auto-created `UMaterialInstanceConstant` assets from SWBF material data
- Single master parent material (`M_SWBF_MasterMaterial`) covers all SWBF render flag combinations

### Terrain

- **Landscape mode:** Imports SWBF terrain as `ALandscape` with heightmap conversion, layered material (LandscapeLayerBlend), painted weightmaps, and per-layer `ULandscapeLayerInfoObject` assets
- **StaticMesh mode:** Imports terrain as a `UStaticMesh` using the FMeshDescription pipeline
- Import mode configurable per-project in Project Settings

### World Layout

- Spawns actors at original SWBF world positions with correct coordinate conversion

### Lighting

- Directional, point, and spot lights
- Ambient top/bottom colors mapped to `ASkyLight`, `AExponentialHeightFog`, and `ASkyAtmosphere`

### Regions

- Box, sphere, and cylinder trigger volumes at original positions and sizes
- Typed region spawning based on SWBF name prefix:
  - `soundspace` - Audio Volume
  - `deathregion` - Kill Z Volume
  - `soundstream` - Ambient Sound actor
  - `mapbounds` - Level Bounds actor
  - `damageregion` - Pain Causing Volume
  - Unrecognized prefixes fall back to generic trigger volumes

### Skydome

- Equirectangular UV dome geometry with seamless panoramic rendering

### Barriers

- `ASWBFBarrierActor` with `UNavModifierComponent` using a non-navigable nav area

### Hint Nodes

- Billboard markers with `UTextRenderComponent` showing type name

### DataLayers

- All spawned actors assigned to their correct DataLayer

### Metadata

- `USWBFAssetUserData` subclasses attached to all imported assets
- SWBF source name, level name, and LVL file path stored per asset
- Asset Registry tags (`SourceName`, `SourceLevel`) exposed as Content Browser columns

### Settings

- Import mode (Landscape vs StaticMesh for terrain) configurable via `Edit > Project Settings > ZeroEditorTools`
- Settings persist per-project via `UDeveloperSettings`

---

## Requirements

- **Unreal Engine 5.7** (Previous UE versions *should* work, but it was developed on 5.7)
- Windows only (Win64 target)
- All dependencies are vendored so no external installs required

**Vendored dependencies:**
- [LibSWBF2](https://github.com/LibSWBF2/LibSWBF2) - C++ library for reading SWBF munged files
- [glm](https://github.com/g-truc/glm) - Math library (included with LibSWBF2)
- [fmt](https://github.com/fmtlib/fmt) - Formatting library, header-only mode

---

## Installation

1. Clone or download this repository
2. Place the `ZeroEditorTools` folder inside your project's `Plugins/` directory:
   ```
   YourProject/
     Plugins/
       ZeroEditorTools/    <-- place here
         ZeroEditorTools.uplugin
         Source/
         Content/
   ```
3. Open (or restart) the Unreal Editor
4. Enable the plugin via `Edit > Plugins` if it is not already enabled

---

## Usage

### Importing an LVL File

Two methods are supported:

**Method 1 - File menu:**
1. In the Unreal Editor, go to `File > Import Into Level...`
2. Select your munged `.lvl` file
3. Choose the destination folder in the Content Browser
4. Click Import

**Method 2 - Drag and drop:**
1. Drag the `.lvl` file directly into the Content Browser
2. Choose the destination folder in the dialog that appears
3. Click Import

### Configuring Import Mode

A few configuration settings exist in `Edit > Project Settings > Plugins > ZeroEditorTools`

---

## Credits

- [LibSWBF2](https://github.com/LibSWBF2/LibSWBF2)
- The SWBF modding community for documentation of the LVL format and material flag semantics
