# Auto Theft Grand — Unreal Engine 5 version

A C++ port of the browser game to **Unreal Engine 5.8**. Like the original, it ships no 3D models, textures
or levels. When you press Play, the whole state is generated from the same seed and comes out identical to
the browser version: the terrain, the road network, Los Soles, San Aurelio, the towns, the buildings, the
props, the trees and the cars. The materials are built in C++ as well, the first time the editor opens the
project.

This is **phase 1** of the port. You can walk and drive around the whole world. Traffic, pedestrians,
weapons, the police, missions and the rest are still to come; [PORTING.md](PORTING.md) has the plan.

## Build and run

You need:

- Unreal Engine 5.8 from the Epic Games Launcher.
- Visual Studio with the **Game development with C++** workload. Use the version Epic lists for 5.8.

Steps:

1. Double-click `AutoTheftGrand.uproject`. When Unreal asks to rebuild the missing `AutoTheftGrand` module,
   click **Yes**. To build from Visual Studio instead, right-click the `.uproject` file, choose
   **Generate Visual Studio project files**, then build the `AutoTheftGrandEditor` target in
   `Development Editor`.
2. The first time the editor starts, it creates the game's materials in `Content/ATG/Materials`
   (`M_ATG_*_1` and `MPC_ATG_1`). The Output Log shows a `LogATG` line for each one. After that, shaders
   compile once, which takes a while.
3. Press **Play**. The world takes a few seconds to generate behind a loading screen. You start outside the
   Castillo house in Cedar Row, with parked cars appearing along the streets.

The project opens on the engine's empty `Entry` map. Everything else is spawned by the game mode, so no level
needs to be saved. To package the game, use **Platforms → Windows → Package Project**. The generated
materials are cooked because `/Game/ATG` is set to always cook.

### Graphics

The renderer uses Lumen for lighting and reflections and virtual shadow maps. The world's meshes are built
at runtime, and meshes built at runtime have no distance fields, so Lumen traces them with **hardware ray
tracing**. That is switched on in `Config/DefaultEngine.ini`.

On a GPU without ray tracing, Unreal falls back to software Lumen, which lights the world from the screen and
the sky only. To force that everywhere, set `r.RayTracing=False` and `r.Lumen.HardwareRayTracing=False`.

While playing in the editor, select `ATGWorld` in the Outliner to adjust the sun and moon brightness, the
exposure limits, the street lamp brightness and the speed of the clock.

## Controls

| On foot | | In a car | |
|---|---|---|---|
| W A S D / left stick | move | W / right trigger | accelerate |
| Mouse / right stick | look | S / left trigger | brake, reverse |
| Shift / L3 | sprint | A D / left stick | steer |
| Space / A | jump | Space / RB / B | handbrake |
| F / Enter / Y | get in a car | F / Enter / Y | get out |
| V | spawn a car next to you | C / R3 | camera distance |
| M / Tab / View | map | Esc / Start | pause |
| T | skip an hour | | |

## How it is put together

```
Source/AutoTheftGrand/Private/Gen/    the world generator and mesh builders: plain C++ with no Unreal code,
                                      ported line for line from src/world/*.js, src/entities/*models.js and
                                      src/ui/mapimage.js
Source/AutoTheftGrand/Private/Game/   the Unreal side
  ATGWorld          runs the generator on a worker thread, then builds the world over a few frames:
                    procedural meshes for terrain, roads, ground, buildings and water; instanced meshes
                    for props, trees, containers and sleepers; hidden collision; the sky, sun, moon,
                    fog, clock and street lamps; fine terrain with collision streamed round the player
  ATGMaterials      the browser game's GLSL ported to HLSL custom nodes, built as material assets in C++
  ATGMeshUtil       generator buffers -> procedural mesh sections and runtime static meshes
  ATGCar            vehicle.js physics: bicycle-model tyres with slip angles, weight transfer, traction
                    circles, handbrake, jumps, walls, street furniture you can smash, other cars
  ATGCharacter      walking, running and jumping, with a segmented procedural body and walk cycle
  ATGPlayerController  keyboard, mouse and gamepad; camera.js's orbit and chase cameras
  ATGGameMode       spawns the world and the player, streams parked cars (vehicles.js rules)
  ATGHUD            loading screen, radar, full-screen map, zone name, clock, speedometer
Tools/              command-line checks that need no Unreal (see below)
```

The generator runs in the game's own axes: metres, y up, the same numbers as the JavaScript. It is mapped to
Unreal's axes (centimetres, Z up) only at the edges; `Game/ATGCoords.h` has the mapping.

### Checking the generator without Unreal

```bash
cd Tools
g++ -std=c++20 -O2 -I../Source/AutoTheftGrand/Private/Gen gentest.cpp ../Source/AutoTheftGrand/Private/Gen/*.cpp -o gentest
./gentest > cpp.txt && node dumpworld.mjs > js.txt && diff js.txt cpp.txt    # C++ world vs the JavaScript one
g++ -std=c++20 -O2 -I../Source/AutoTheftGrand/Private/Gen meshpreview.cpp ../Source/AutoTheftGrand/Private/Gen/*.cpp -o meshpreview
./meshpreview out       # software renders of props, cars, parts of the city
./meshpreview out map   # the radar map
./meshpreview out seat  # a driver in a car (checks the seated pose)
```

*Auto Theft Grand is an original fan-made parody. All names, places and characters are fictional.*
