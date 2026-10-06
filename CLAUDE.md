# Auto Theft Grand: notes for Claude

This repo holds two things:

1. **The browser game** (`index.html`, `css/`, `src/`, `assets/`, `vendor/three/`). It is a complete GTA-style
   open-world game in plain ES modules on Three.js, with no build step. It's about 34k lines of JS.
2. **The Unreal Engine 5.8 port** (`Unreal/AutoTheftGrand/`), in C++. Only phase 1 is written: the generated
   world, walking, driving, radar and map. The job now is porting the **whole game** to UE 5.8.3 on the
   user's Windows PC. Visual Studio and UE 5.8.3 are installed there.

Read `README.md` (features, controls, project layout), `Unreal/AutoTheftGrand/README.md` and
`Unreal/AutoTheftGrand/PORTING.md` before starting. This file holds what those don't: how the game is wired
together, how to test it, and where the port stands.

## Working rules

- The working branch is `claude/awesome-meitner-reqkrv`; it is the only branch on GitHub. Commit in small,
  working steps and push to it. Don't open pull requests unless asked.
- Every gameplay or controls change updates `README.md`. Controls live in three places that must agree:
  the README tables, the in-game Controls tab (`src/ui/hud.js`, `_tab_controls` / the pad rows) and
  `src/core/input.js` (`BINDINGS`, `GP_BIND`).
- `assets/audio/wasted.mp3` is the user's own upload of Rockstar's "wasted" sound. Keep it out of anything
  published widely, and remind the user to swap in their own clip before sharing the game publicly.
- No AI model names in commits, code or docs.
- Write prose plainly: a short sentence per idea, no marketing tone. The README has its own tone; match it.

## The browser game

### Run it

```
npm install                      # esbuild + playwright (dev tools only; the game itself needs nothing)
npm start                        # node server.mjs -> http://localhost:8080 (PORT env to change)
```

URL parameters: `?autostart=free` (free roam), `?autostart=new` (story), `?autostart=multi&room=CODE`,
`?q=low|medium|high` (quality), `?manual` (test mode: the game doesn't run its own frame loop, see below).

### Test it headlessly

`tools/browser-test/run.mjs` loads the game in Chromium, waits for `window.__ready`, then runs a test script:

```
npx playwright install chromium   # once
node tools/browser-test/run.mjs "http://localhost:8080/index.html?manual&autostart=free&q=low" out/phone tools/browser-test/tests/phone.mjs
```

With `?manual` the page exposes these hooks:

- `window.__game`: the Game object.
- `window.__step(seconds, dt = 1/30, render = true)`: advances the simulation deterministically. Pass
  `render = false` for speed and render only the frame before a screenshot.
- `window.__press(code)` / `window.__release(code)`: KeyboardEvent codes, e.g. `KeyI`, `Tab`, `CapsLock`.

A test exports `async function (page, shot, ev)`. Put the game logic in `ev(() => { ... })` and return plain
JSON. `shot(name)` writes a PNG; look at it. The tests in `tools/browser-test/tests/` cover:

- the phone, roadblocks, armoured vans, the army, the weapon wheel, the special ability, the HUD, the
  cinematic camera and the new vehicles;
- older checks: free roam, death, respawn and the story missions (`ONLY=welcome,oldfriends node ...` limits
  which missions run).

Run the relevant ones after a change, and the death, respawn and missions ones before committing anything big.

Test gotchas:

- God mode in tests is `g.admin.cheats.god = true`. `g.cheatsOn` is overwritten every frame.
- Free roam is the flag `g.freeRoam`. `g.freeroam.active` is a getter (`freeRoam && !missions.active`), so
  assigning to it does nothing.
- The camera follows `g.rig`, not `player.yaw`. For a set shot, use
  `g.rig.setCinematic(posVec3, targetVec3, fov)` with `window.THREE`.

### Publish it

`npm run bundle` writes `dist/` (`index.html` with the CSS inlined, `game.js`, `assets/`). The published
copy is the claude.ai artifact https://claude.ai/artifact/KnLeaGEJBFnXXVSxXRWzFW. To update it, publish
`dist/index.html` with `game.js` as a supporting file to that URL. Keep its `room` capability, which
multiplayer uses. Read the live version first: the artifact tool refuses blind overwrites. This needs a
Claude session that has the Artifact tool; a plain local CLI may not have it.

### How the game is wired (what a port must reproduce)

- **Axes:** metres, y up, x east, z south.
  - Heading: forward = (sin yaw, cos yaw), and increasing yaw turns left.
  - Right vector = (-cos yaw, sin yaw).
- **Systems:** `game.addSystem(name, sys)` in `src/main.js` sets `game[name]` and keeps registration order.
  Each system may have:
  - `earlyInput(dt)`: before the player reads input (the phone uses this to take the arrow keys).
  - `preUpdate(dt)`: after player control, before vehicles.
  - `update(dt)`
  - `reset()`
- **`Game.update` order:**
  1. systems' `earlyInput`
  2. player control (on foot: `player.control`; driving: `vehicle.playerControl`)
  3. `tryEnterExit`
  4. drive-by aiming
  5. systems' `preUpdate`
  6. `vehicles.update`: AI for cars that aren't traffic, police or army, then physics for every vehicle
  7. `player.update`
  8. systems' `update`, in registration order
  9. environment, camera rig (real dt), city

  `hud.update` runs after `game.update`.
- **Time:** sim dt = frame dt × `game.timeScale` × `game.fxScale`. `fxScale` is the minimum of the values
  in `game.slowmo`:
  - weapon wheel: 0.18
  - special ability: 0.38 on foot, 0.5 driving
  - SLOWMO cheat: 0.5

  The wheel and special-ability meters use the real frame time (`input.frameDt`).
- **Events** (`game.events.on/emit`): `kill(killer, victim, weapon, part)`, `gunshot`, `explosion(pos, r, src)`,
  `vehicleShot(v, shooter, point)`, `carCrash(A, B, impact)`, `carjack(by, victim, veh)`,
  `enteredVehicle`, `exitedVehicle`, `pedHitByCar`, `melee`, `wantedUp(level)`, `wantedCleared`,
  `playerDied`, `busted`, `missionStart/Passed/Failed`, `armyDeployed`, `specialOn`, and others (grep `emit(`).
- **Wanted level** (`src/game/police.js`):
  - Heat thresholds per star: `[0, 1, 3, 7, 14, 24]`. `crime(amount, pos, severe, noise)` adds heat; a
    witnessing cop or a severe crime forces at least one star.
  - The stars clear after `10 + 5 × level` seconds unseen. They flash after 2 s unseen.
  - Pursuit cars = one per star. The helicopter comes at 3+ stars.
  - Roadblocks come at 3+ stars while you drive (`roadblocks.js`): every 24 / 17 / 14 s at 3 / 4 / 5 stars,
    1-2 standing at once. At 4 stars, the Enforcer van and a spike strip join them.
  - The army comes at 5 stars (`army.js`):
    - two troop vehicles at a time
    - the Warhawk gunship after 8 s
    - the Mammoth tank after 18 s
    - they pull out below five stars
    - the gunship and tank hold fire when their own units are near the target
- **People** (`src/game/peds.js`):
  - Brains: `cop`, `gang`, `civilian`, `script`.
  - States include `wander`, `attack` (with `threat`), `follow` (with `follow` and `followSlot`) and `guard`.
  - Gangs are in `GANGS`. Soldiers are gang `army`, hostile when `peds.gangAggro.army` is set.
- **Cheats:** everything reads `game.cheatsOn`. In free roam that is `admin.cheats`; elsewhere it is the
  phone's timed cheats (`phone.activeCheats()`), and nothing online.
- **Input:**
  - `input.key/keyHit(code)`, `input.down/hit(action)`, `gpDown/gpHit(button)`.
  - `input.mask` hides pad buttons for one frame (the phone takes the D-pad, A and B while it's open).
- **Vehicles:**
  - Catalogue: `src/entities/vehicledefs.js`. Models: `vehiclemodels.js`, lofted bodies. Physics:
    `vehicle.js`, a bicycle-model tyre with slip angles.
  - Flags: `v.flat` (burst tyres), `v.stable` (harder to roll), `v.armyUnit`, `v.heistVan`, `v.roadblock`.

## The Unreal port

### Where it stands

- **Phase 1 builds and runs on the user's PC** (UE 5.8.3, Visual Studio 2026 Insiders, MSVC 14.51). The game
  logic now lives in `Private/Sim/` (plain C++, ported line by line, with its own port of `collision.js`);
  Unreal only draws it and feeds it input. `PORTING.md` has the status per phase, and
  `Unreal/AutoTheftGrand/README.md` the build, run and test commands (`Tools/build.sh`, `Tools/run.sh`,
  `Tools/native.sh` + `Tools/simtest.cpp`, the node comparisons with `Tools/three-hook.mjs`).
- The user's earlier build failed with "Some Platforms were skipped due to invalid SDK setup: Win64" and
  "Unexpected ProjectFileFormat 'Default'". That was Visual Studio missing, and it's installed now.
  - If the ProjectFileFormat error comes back, look for a `ProjectFileFormat` entry in
    `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`.
  - Also check Editor Preferences → Source Code (it should name Visual Studio).
  - Keep the checkout on a short path such as `C:\ATG`: long paths break UBT.

### First steps on the user's PC

1. Build from the command line and fix errors until it's clean. Adjust the engine path to the install,
   usually `C:\Program Files\Epic Games\UE_5.8`:
   ```
   "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" AutoTheftGrandEditor Win64 Development -Project="C:\ATG\Unreal\AutoTheftGrand\AutoTheftGrand.uproject" -WaitMutex
   ```
2. Open the editor once, by double-clicking the `.uproject` or running `UnrealEditor.exe <uproject>`.
   `ATGMaterials` creates the material assets in `Content/ATG/Materials` on that first start, and shaders
   compile. Check that it works headless before relying on it.
3. Run the game outside the editor and read the log:
   ```
   UnrealEditor.exe C:\ATG\Unreal\AutoTheftGrand\AutoTheftGrand.uproject -game -windowed -resx=1280 -resy=720 -log
   ```
   The log is in `Saved/Logs/AutoTheftGrand.log`. Screenshots: the `HighResShot 1280x720` console command
   (or `-ExecCmds="HighResShot 1280x720"`) writes to `Saved/Screenshots/`. Look at them.
4. Add test hooks early, the Unreal equivalent of `__step`, `__press` and `__game`:
   - console commands (`UFUNCTION(Exec)` on the controller or cheat manager) such as `ATG.Teleport x z`,
     `ATG.Wanted 5`, `ATG.Spawn zenith`, `ATG.God 1`, `ATG.Shot name`;
   - fixed-step runs, e.g. `-benchmark -fps=30` or a command that ticks N frames.

   With these, a script can launch the game, run commands and check the log or screenshots, the way the
   browser tests do.
5. Commit once phase 1 builds and runs: the world appears, you can walk, enter a car and drive.

### How to port the rest

- **Port behaviour, not approximations.** The JS is the spec: same numbers, same rules, same dialogue text.
  Read the JS file, then write the C++.
- **Keep game logic engine-free where it's logic-heavy**, the way `Private/Gen/` is: traffic lane
  following, ped brains, the wanted and police logic, the army, roadblock and heist rules, weapons data, the
  mission engine. Put it under a new `Private/Sim/` and give it a native test runner like
  `Tools/gentest.cpp` that builds with MSVC or clang without Unreal. Unreal actors then render it, feed it
  collision and line-of-sight queries through a small interface, and play sound. This keeps most of the code
  testable without launching the engine.
- **Geometry:** generate everything procedurally as phase 1 does (`ProceduralMeshComponent` and runtime
  static meshes, built by `ATGMeshUtil`). The game ships no art assets. The models live in
  `src/entities/vehiclemodels.js`, `humanoid.js`, `aircraftmodels.js`, `animals.js`, `bikes.js`, `boat.js`,
  `src/world/*`.
- **Coordinates:** `Game/ATGCoords.h`. World: x, y, z → X = x, Y = z, Z = y, in centimetres. Things
  modelled facing +z: X = z, Y = -x. Headings use `HeadingYaw` and `HeadingFromUE`.
- **Characters:** phase 1 has a segmented body. The browser game uses one smooth-skinned mesh with a
  procedural animator: IK gait, actions and ragdoll (`humanoid.js`, `animator.js`, `ragdoll.js`,
  `character.js`). Port that as a `USkeletalMesh` built at runtime, or a `UPoseableMeshComponent` driven
  from C++ with the same bone set.
- **HUD:** the browser HUD is HTML and CSS. Draw it with `UCanvas` in `ATGHUD`, as phase 1 does, or with
  UMG built in C++. Match the modern GTA V layout from the browser:
  - rectangular minimap with health, armour and special bars under it
  - stars, cash and weapon top right
  - zone and vehicle name bottom right
  - the weapon wheel and the phone
- **Audio:** the browser synthesises its sounds with WebAudio (`src/game/audio.js`): engines, sirens, guns,
  radio. Port them as procedural `USoundWaveProcedural`, or use MetaSounds built in C++.

Phase order (update `PORTING.md` as phases land):

1. **Phase 1:** get it compiling and running (above).
2. **Phase 2, a living city:**
   - traffic (`traffic.js`)
   - pedestrians and gangs (`peds.js`)
   - traffic lights
   - trains (`railsystem.js`, `train.js`)
   - the skinned humanoid, animator and ragdoll
3. **Phase 3, action:**
   - weapons and melee (`combat.js`, `weapondefs.js`) and effects (`effects.js`)
   - pickups and shops
   - the wanted level and police (`police.js`), roadblocks (`roadblocks.js`), the army at five stars
     (`army.js`)
   - car damage, fire, explosions and tumbling
   - the WASTED and BUSTED screens
   - street crime (`npccrime.js`)
4. **Phase 3b, the GTA V layer:**
   - the modern HUD and minimap with police vision cones
   - the weapon wheel (`weaponwheel.js`)
   - the special ability (`special.js`)
   - the phone with contacts and cheats (`phone.js`)
   - armoured van heists (`heists.js`)
   - the cinematic camera (`camera.js` `_vehCine`)
5. **Phase 4, vehicles:** motorbikes and bicycles, boats, planes, jets, helicopters, the tank, skateboards.
6. **Phase 5, story and features:**
   - the mission engine (`missions.js`) and both stories (`story.js`, `story_north.js`, 35 missions)
   - the ship raid, taxis, races, vigilante, Customs, properties
   - free roam and admin tools
   - save and load
7. **Phase 6, the finish:** interiors, landmarks, weather and puddles, audio and radio, wildlife and pets,
   multiplayer (with Unreal replication).

After each subsystem:

1. Build it.
2. Run it.
3. Check it in the log or a screenshot.
4. Commit and push.
5. Update `Unreal/AutoTheftGrand/README.md` (controls, what works) and `PORTING.md` (status).

Be honest in those docs about what's done and what isn't.

### Phase 1 facts worth knowing

- **World generator:** bit-exact with the JS (Mulberry32, float32 where the JS used Float32Array, stable
  sorts, V8's `Math.hypot`). Never "tidy" it in a way that changes numbers. Check with:
  ```
  cd Unreal/AutoTheftGrand/Tools
  g++ -std=c++20 -O2 -I../Source/AutoTheftGrand/Private/Gen gentest.cpp ../Source/AutoTheftGrand/Private/Gen/*.cpp -o gentest
  ./gentest > cpp.txt && node dumpworld.mjs > js.txt && diff js.txt cpp.txt
  ```
- **`ATGWorld`:**
  - generates on a worker thread, then builds the meshes over several frames
  - owns the sky, sun, moon, fog, clock and street lamps
  - streams fine terrain collision around the player
- **Rendering:** Lumen with hardware ray tracing. The runtime meshes have no distance fields.
  `Config/DefaultEngine.ini` explains the software fallback.
- **The game starts on `/Engine/Maps/Entry`.** `ATGGameMode` spawns everything, so no level asset exists.
