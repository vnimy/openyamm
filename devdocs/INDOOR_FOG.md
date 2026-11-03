# Indoor fog (BLV)

A native indoor map may fade every world pixel into one colour between two distances from the eye. It is opt-in per map
through the scene file: a map without the block (every MM6–MM8 dungeon, Hesper's) renders exactly as before and pays
nothing. First user: MM9's Anskram Keep, whose `WorldProperties` set a black fog from 1 to 5000 LT.

## What MM9 has (all 45 DAT worlds)

| Source | What it is | Where | OpenYAMM |
| --- | --- | --- | --- |
| `WorldProperties` `EnableFog`, `FogColor`, `FogNearZ/FarZ` | fog for the whole map | 8 maps; indoors only Anskram Keep (black, 1–5000) | scene `fog` |
| `Fog` objects | **point controllers, not volumes**: no brush, just a fog state a `WeatherMan` fires (`FogOn`, `FogOff`, `TransitionTime`). `StartOn 1` makes one the map's fog from the start | 24 objects in 11 maps, list in [MM9 visual properties](mm9_rework/MM9_VISUAL_PROPERTIES.md) table C; `StartOn` in 6 | scene `fog` takes the `StartOn` state; the switching is not carried |
| Water volumes' `FogEnable` | fog while the camera is in the volume: the only real regions | 15 volumes | none: the party cannot dive indoors yet |
| `VFog` volumetric fog | a height band | 0 maps | none |

The Lich Lab has none of these. Its "white mist" was our own view-distance fade in the reference shots
(MM9 visual properties, "Correction").

## Data: the scene's `fog`

```yaml
fog:
  color_rgb: [0, 0, 0]     # display colour, 0..255
  near_distance: 2.56      # native units from the eye: clear up to here
  far_distance: 12800      # all fog from here on
```

Top level of `<map>.scene.yml` (`IndoorSceneYml`: `parseIndoorSceneFog`); a scene overlay's `fog` replaces the base's;
the editor's scene writer round-trips it. A missing key, a colour outside 0–255 or anything but `0 <= near < far` fails
the scene's load. The names are those of the outdoor authored fog (`weather.authored_fog`). The map keeps it as
`IndoorMapData::fog` (`game/indoor/IndoorFog.h`), map data, never saved.

**Regions.** The block is a map so that a later `regions:` key can sit beside the map-wide values, each with a box or
sector list and its own colour and distances. It is not built: the `Fog` objects turned out not to be regions, and the
one true region (water fog) waits for diving. What it would need: one per-frame choice of the fog at the eye in
`IndoorRenderer`, since every draw below reads `m_fog`; the programs would load for any map that has regions.

**Switching** (`WeatherMan`, `TransitionTime`) would be a Lua call that changes the same values at run time; no map
needs it indoors yet.

The MM9 converter writes the block itself: `dungeon/source.py` `Source.fog` reads `WorldProperties`, else a `StartOn`
`Fog` object, LT × 2.56, and `dungeon/build.py` puts it in the scene. `overrides.yml` `map: {fog: false}` leaves it out.

## Rendering

The fog is linear in the distance from the eye, `clamp((d - near) / (far - near))`, mixed in display space as the last
step of each shader, the same fade everywhere:

| Drawn | How |
| --- | --- |
| Faces (vertex-lit, baked, surface materials, each opaque and translucent) | `INDOOR_FOG` variants of `fs_indoor_textured_lit.sc`, six programs loaded only for a map with fog; uniforms `u_fogColor`, `u_fogDistances` per draw |
| Sky faces (`IndoorSky`) | stay clear (MM9 `SkyFog` 0 in every map with indoor fog) |
| Blood splats | the same vertex-lit fog program |
| Indoor water, lava, tar | `INDOOR_FOG` variants of `fs_indoor_water.sc` (`WaterRenderer::setIndoorFog`) |
| Decorations, actors, items, spell sprites as billboards | their shader's existing linear fog (`u_fogDensities.w` 1), `IndoorRenderer::setBillboardFog` |
| 3D models (creatures, decorations, items, projectiles) | the model shader's existing linear fog, `ModelRenderLighting` fog fields |

`IndoorFog.h` packs the uniforms; disabled, they are the constants those draws always passed, so the billboard and model
paths are unchanged on every other map.

**Cost with fog off.** No program, no uniform, one `bool` tested per face batch. With fog on: one `length` and `mix` per
pixel in the face shaders, no extra pass.

**Not fogged.** Particles and effect sprites, ribbons and shield domes (no fog term in those shaders outdoors either, and
glow reads through fog); the hover outline overlay. Billboards use view depth where faces and models use the distance
from the eye, so a sprite at the screen's edge is fogged a little less than the wall behind it; at Anskram's 12 800 range
the difference is small.

## Baked lighting

The bake ignores fog: it is applied at draw time, so changing it needs no rebake. A map calibrated without fog gets
darker with it; compare with a reference that shows the fog, or tune with it on.

## Tests

`./build/tests/openyamm_unit_tests --test-case='*indoor fog*'`: the disabled defaults equal the old constants, the linear
ramp, parsing and every refusal. MM7 `7d06` and MM8 `d06` captures before and after.
