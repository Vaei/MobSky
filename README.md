# Mob Sky

Sky for the mobile forward rendering path: volumetric clouds you fly through, an atmosphere that thins to space with altitude, and a space backdrop of stars, a galaxy, distant galaxies and nebulae. All of it is evaluated in materials.

Requires **MobLights**: it reuses MobLights' shader library, fog noise and material generator helpers, and takes the key light, ambient and clock from `MPC_MobFog`.

UE5.8+

## Setting up

1. Enable MobSky. Run **Light → Generate Fog Materials** once if MobLights' fog was never generated.
2. **Sky → Generate Materials.**
3. **Sky → Place → Sky**, then **Sky → Place → Cloud Layer** for each deck.
4. On the sky, set **GroundZ** and **VacuumZ** to your world's ground and the height where the air runs out.
5. Use a Directional Light **actor** for the sun. MobLights' fog subsystem follows the brightest one, and the sky and clouds read it from there.

A layer follows the camera, so where it is placed does not matter; set **BaseHeight** and **TopHeight**. A **Cloud Volume** stays where it is put. Give it an **EmissiveColor** and it becomes a nebula you can fly into.

Cloud layers render in a **render pass** by default: marched at half resolution before the base pass, accumulated over frames, and composited into the translucency pass by `M_MobCloudComposite`. Only the view being played through (or the realtime editor viewport) runs it; every other view, and anything with the pass off, marches the layers in their own material at full resolution. Volumes always use their material.

From above the air, the sky paints the heaviest deck onto the planet past the layer's reach, so storms carry on to the horizon.

## Troubleshooting

| Symptom | Cause |
|---|---|
| Clouds and sky lit from straight above | No directional light *actor*. A light component on some other actor is not found |
| Space is black or green in play but fine in the editor | The bake captured before the space shaders compiled. It retakes itself for the first few seconds; `mob.Sky.Bake 0` in DefaultEngine.ini `[ConsoleVariables]` marches space live to confirm |
| Stars show in daylight | **VacuumZ** is too low, so the camera is treated as being above the air |
| Clouds grainy at their edges | With `mob.Clouds.RenderPass 0`, the material march only averages its dither across 2x2 pixel quads, and only on D3D. Raise **StepCount**, or leave the pass on |
| Clouds trail behind a fast turn | Lower `mob.Clouds.History`, or `mob.Clouds.Checkerboard 0` |
| No clouds for the first second of play | The pass waits for its noise textures to finish building in an uncooked build |
| Clouds vanish in the distance | **MaxDistance** / **ReachPerHeight** on the layer |
| Nothing renders at all | Materials not generated, or MobLights' `MPC_MobFog` missing |

Console: `mob.Clouds.Enable`, `mob.Sky.Enable` (show/hide, for measuring cost), `mob.Sky.Bake`, `mob.Clouds.RenderPass`, `mob.Clouds.Resolution` (0.5 is half resolution), `mob.Clouds.History` (0.9), `mob.Clouds.Checkerboard` (1 marches one pixel of each 2x2 block a frame and rebuilds the rest from history). Set `mob.Clouds.RenderPass` in `[ConsoleVariables]` to A/B from the first frame; `-ExecCmds` arrives after BeginPlay.

## Tuning

**Cloud layer:**
- **Coverage** is the share of sky that is cloud: the weather map is equalised, so 0.45 covers about 45%. Drive it at runtime with `SetCoverage`.
- **Density** controls how quickly light dies inside the cloud.
- **CoverageScale** is the size of the weather tile (a few clusters of cloud cross it), **ShapeScale** how big a billow is.
- **BottomFade** and **TopFade** set a flat base or towering tops.
- **Powder** darkens thin edges seen away from the sun; **HazeDistance** fades distant cloud into the horizon.
- **MacroScale** and **MacroAmount** are the planet's weather: cyclones, fronts and clear regions that push Coverage up and down across hundreds of kilometres. 0 gives the same weather everywhere.
- **Detail** and **Shadow** are shader permutations. Turn them off for a thin, cheap layer such as cirrus.
- **StepCount** is the march budget.

**Sky:**
- Atmosphere colours, sun disc and halo, and the planet limb.
- Stars: cell counts, chance and magnitude.
- Galaxy: tilt, yaw, spin, arms, dust and bulge.
- **Nebulae**: up to two, placed by yaw, pitch and radius.
- **bBakeSpace** bakes the galaxy and nebulae into a cube at BeginPlay, so a game never marches them per pixel. The editor always marches them live.
- **DayAmbient**, **NightAmbient** and **SpaceAmbient** feed `MPC_MobFog.AmbientColor` while **bDrivesAmbient** is on.
