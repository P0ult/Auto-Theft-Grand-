# Auto Theft Grand — Unreal Engine 5 version

A C++ port of the browser game to **Unreal Engine 5.8**. Like the original, it ships no 3D models, textures
or levels. When you press Play, the whole state is generated from the same seed and comes out identical to
the browser version: the terrain, the road network, Los Soles, San Aurelio, the towns, the buildings, the
props, the trees and the cars. The materials are built in C++ as well, the first time the editor opens the
project.

The game itself runs in plain C++ that knows nothing about Unreal (`Private/Sim`): a line-by-line port of
the browser game's logic, including its own collision world, so it behaves the same. Unreal draws what the
simulation says and feeds it the keyboard, mouse and pad.

So far you can walk, sprint, jump and crouch, get into any car (the walk to the door, the door opening, the
sit-down), drive with the browser game's full car physics (drifts, jumps, crashes, rollovers, dents, panels
torn off, fire and explosions), and the parked cars stream in round you. Traffic, pedestrians, weapons, the
police, missions and the rest are still to come; [PORTING.md](PORTING.md) has the plan and the progress.

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

The keys are the browser game's (`Sim/Input.cpp` has the same bindings as `src/core/input.js`). What works so
far:

| On foot | | In a vehicle | |
|---|---|---|---|
| W A S D / left stick | Move | W / S, RT / LT | Accelerate / brake and reverse |
| Mouse / right stick | Look | A / D, left stick | Steer |
| Shift / A (hold) | Sprint | Space / RB | Handbrake (drift) |
| Space / X | Jump | H / LS click | Horn (Shift+H: siren in police cars) |
| C / Ctrl / LS click | Crouch | G / D-pad ↑ | Hydraulics (lowriders) |
| F / Enter / Y | Get in a car (carjack the driver) | F / Enter / Y | Get out (bail out at speed) |
| M / D-pad ↓ | Map | V / View | Camera distance |
| Esc / P / Menu | Pause | B / RS click | Look behind |
| | | X / B (hold) | Cinematic camera |

## How it is put together

```
Source/AutoTheftGrand/Private/Gen/    the world generator and mesh builders: plain C++ with no Unreal code,
                                      ported line for line from src/world/*.js, src/entities/*models.js,
                                      loft.js, vehicledefs.js and src/ui/mapimage.js
Source/AutoTheftGrand/Private/Sim/    the game: plain C++ with no Unreal code, ported line for line from
                                      src/game and src/entities (Game, Input, Collision, Character, Player,
                                      Animator, Ragdoll, Vehicle, Vehicles, Camera, Env, Weapons)
Source/AutoTheftGrand/Private/Game/   the Unreal side: draws the simulation and feeds it input
  ATGWorld          runs the generator on a worker thread, then builds the world over a few frames:
                    procedural meshes for terrain, roads, ground, buildings and water; instanced meshes
                    for props, trees, containers and sleepers; hidden collision; the sky, sun, moon,
                    fog, clock and street lamps; fine terrain with collision streamed round the player
  ATGMaterials      the browser game's GLSL ported to HLSL custom nodes, built as material assets in C++
  ATGMeshUtil       generator buffers -> procedural mesh sections and runtime static meshes
  ATGCar            draws a simulated vehicle: its parts on the sprung body, doors and lids on their hinges,
                    wheels, lights, dents, lost panels
  ATGPerson         draws a simulated person (for now the segmented body, placed on the animator's bones)
  ATGPlayerController  keyboard, mouse and gamepad into the simulation's input; shows its camera
  ATGGameMode       runs the simulation each frame and keeps an actor for each of its vehicles and people
  ATGHUD            loading screen, radar, full-screen map, zone name, clock, speedometer
  ATGTest           test hooks: ATG.* console commands and scripts (see below)
Tools/              command-line checks that need no Unreal (see below)
```

The generator runs in the game's own axes: metres, y up, the same numbers as the JavaScript. It is mapped to
Unreal's axes (centimetres, Z up) only at the edges; `Game/ATGCoords.h` has the mapping.

### Testing

`Tools/build.sh` builds the editor target from Git Bash. `Tools/run.sh Tools/tests/smoke.txt` runs the game off
screen with fixed 1/30 s frames and a script of console commands (`ATG.Teleport`, `ATG.Press KeyW`,
`ATG.Spawn zenith`, `ATG.Enter`, `ATG.Time 21`, `ATG.State`, `shot name`, `wait 2`, `quit`); screenshots go to
`Saved/Screenshots/WindowsEditor/ATG/`.

The simulation is tested without Unreal. `Tools/native.sh` builds a tool with MSVC from Git Bash:

```bash
cd Tools
./native.sh simtest.exe simtest.cpp && ./simtest.exe          # walking, driving, crashes, parked cars
./simtest.exe vehcompare > cppveh.txt                          # the car physics against the browser game's:
node --import ./three-hook.mjs vehcompare.mjs > jsveh.txt      # identical to 4 decimals
diff jsveh.txt cppveh.txt
```

### Checking the generator without Unreal

```bash
cd Tools
./native.sh gentest.exe gentest.cpp && ./gentest.exe > cpp.txt
node --import ./three-hook.mjs dumpworld.mjs > js.txt && diff js.txt cpp.txt   # C++ world vs the JavaScript one
./native.sh carstest.exe carstest.cpp && ./carstest.exe > cppcars.txt          # car models vs vehiclemodels.js
node --import ./three-hook.mjs dumpcars.mjs > jscars.txt && diff jscars.txt cppcars.txt
./native.sh meshpreview.exe meshpreview.cpp && mkdir -p out
./meshpreview.exe out       # software renders of props, cars, parts of the city
./meshpreview.exe out map   # the radar map
```

*Auto Theft Grand is an original fan-made parody. All names, places and characters are fictional.*
