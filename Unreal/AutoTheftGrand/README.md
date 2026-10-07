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
torn off, fire and explosions), and the parked cars stream in round you. The city is alive: traffic drives
its lanes, stops at the traffic lights and gives way at junctions and roundabouts, and people walk the
pavements, with gang members standing guard on their turf. Everyone is the browser game's smooth-skinned
person, built for each look and posed by the same animator: walking, running, sitting, ragdolls. Die and
the screen goes black and white in slow motion under the WASTED shard before you wake up at the hospital;
get arrested and it is BUSTED and the police station. Drifts and big jumps pay a cash bonus. Wrecked cars
smoke and burn, crashes throw sparks, guns, knives, bats, grenades, molotovs and the RPG work as in the
browser game, knocked-over street furniture and torn-off panels fly, tyres leave skid
marks, and explosions bloom into fireballs and smoke columns. Crimes bring the police: witnessed crimes
raise the stars, patrol cars turn into pursuers that ram you, cops shoot or come to arrest you, and at three
stars the helicopter circles overhead with its searchlight and roadblocks close the road ahead, with a spike
strip at four stars. Stay out of sight long enough and they give up. Motorbikes and bicycles lean through the corners and throw you off in a hard crash.
Planes, jets and helicopters fly as they do in the browser game, and the Mammoth tank turns its turret
toward the crosshair and fires shells. The Skipper waits at Fern Creek Airfield, a Skylark on the hospital
roof, and Fort Carver keeps Raptors, a Hercules, a Warhawk, Mammoths and army trucks behind its fence,
guarded by soldiers: trespass and the army opens fire. At five stars the army joins the chase with troop
trucks, jeeps, the Warhawk gunship and a tank. Skateboards push along, carve, ollie and flip (land it clean
for cash), and the Santa Luz skatepark has boards lying about and locals skating laps. Boats tie up at the
marinas, plane across the swell leaving a wake, cruise the coast, and police boats ram you and open up with
their bow gun when you're wanted out on the water. The city has its own crime too:
jaywalkers, speeders, road rage, muggings and car thefts, with stars over the culprit and a patrol that
writes a ticket, gives chase or makes an arrest. Every few minutes an armoured van does its rounds nearby:
shoot its back doors open, grab the cash and fight off the guards. The phone calls Lester (lose the cops),
Benny (your last car delivered), Merryweather (three armed contractors), takes the GTA V cheat codes, and has
Snapmatic, the map, the weather and your stats.
Health, armour, cash and weapons lie about the city, dropped by the dead or waiting to respawn, with 30
hidden packages to find; the Spray Shack repaints and repairs your car and loses the police for $100. Eight shops
have a way in and a clerk behind the counter: buy guns and armour at the Gun Barn, a Big Bun combo, snacks and
scratch cards, drinks that make the room sway, and coffee; point a gun at a clerk and the till is yours (and the
cops are called), except at the Gun Barn, where the owner reaches for his shotgun. The Sol Line's passenger and freight trains run
their timetable; board one at a platform with F, or climb into the cab and drive it. Everything is heard as
in the browser game: synthesised gunshots, explosions and crashes placed round you with a city reverb, the
engine, tyres and wind of whatever you drive, aircraft engines, sirens, the helicopter's rotor, traffic, birds,
crickets, waves, rain and thunder, and three generated radio stations (N or D-pad right changes station).
Wildlife spawns by district: pigeons, gulls and crows, cats and stray dogs, deer, rabbits, coyotes and cows.
They graze, peck and wander, flee the player and gunfire, and birds take off and land elsewhere. People walk
their dogs. Bullets, melee, explosions and cars can kill animals. Pet Palace has dogs and cats in its pens,
and sells all nine pet breeds at the browser's prices. Your pet follows, sits when you stop and rides in a
free passenger seat. Whistle with K (right-stick click on foot) to tell it to stay or come; aim at someone
and whistle to send your dog after them. Dogs also defend you against attackers. Pet treats heal your pet,
and the HUD shows its name and state. Save data and online pet replication come with those systems.
The mission engine runs sequential scripts with dialogue, cutscenes, objectives, timers, counters, GPS
routes, target arrows, failure conditions, rewards and contact progression. Chapter I is playable:
Welcome Home, Old Friends, Clean Sweep, Tooling Up and Drive-By. Chapter II is playable too: Burning Rubber,
Hot Wheels, Blood Money, The Snitch and Family Ties. Chapter III adds Evidence, Beach Party, Snake in the
Grass, Ambush and Rush to All Saints. The stealth tail uses distance, time spent too close, collisions and
gunfire to detect you; Ambush and the hospital run start automatically. The scripts keep the original cast,
dialogue, encounters, restrictions and rewards. Chapter IV adds Harbor Heist, Vistawood Nights, Taking Back
the Streets and Kingpin, including truck condition, the timed stunt run and the gang-density changes.
The other 16 missions are still to come;
[PORTING.md](PORTING.md) has the plan and the progress.

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
   (`M_ATG_*_5` and `MPC_ATG_5`; the number goes up when the materials change). The Output Log shows a
   `LogATG` line for each one. After that, shaders compile once, which takes a while.
3. Press **Play**. The world takes a few seconds to generate behind a loading screen. You start outside the
   Castillo house in Cedar Row, with traffic on the streets and people on the pavements.

The project opens on the engine's empty `Entry` map. Everything else is spawned by the game mode, so no level
needs to be saved. To package the game, use **Platforms → Windows → Package Project**. The generated
materials are cooked because `/Game/ATG` is set to always cook.

To start the story from PowerShell, add `-ATGStory` to the game launch:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\ATG\Unreal\AutoTheftGrand\AutoTheftGrand.uproject" -game -windowed -resx=1280 -resy=720 -ATGStory -log
```

The game opens with Welcome Home. After each mission, yellow contact letters show the next ones. Chapter IV
ends after Kingpin; Chapter V has not been ported yet. Without `-ATGStory`, the game starts at the safehouse
as before. Save/load and the title screen are still pending.

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
| F / Enter / Y | Get in a car (carjack the driver), board a train | F / Enter / Y | Get out (bail out at speed) |
| Left mouse / RT | Punch (jab-cross-kick combo) / fire | Right + left mouse, LB + RB | Drive-by |
| Right mouse / LT | Aim (people put their hands up) | Q / E, wheel | Switch drive-by weapon |
| R | Reload | | |
| K / RS click | Whistle: pet stay / come (aiming at someone: dog attack) | | |
| Q / E, wheel, 1-9, RB / LB tap | Switch weapon | | |
| Tab / LB (hold) | Weapon wheel (the game slows; point with the mouse or right stick, let go to pick) | Tab (hold) | Weapon wheel |
| Caps Lock / Z / LS + RS click | Special ability: slow motion (the yellow bar under the map) | Caps Lock / Z | Special ability: slow motion with extra grip |
| I / D-pad ↑ | Phone (arrows / D-pad, Enter / A, Backspace / B) | I | Phone |
| M / D-pad ↓ | Map | V / View | Camera distance |
| | | N / D-pad → | Next radio station |
| Esc / P / Menu | Pause | B / RS click | Look behind |
| | | X / B (hold) | Cinematic camera |

| Planes & jets | | Helicopters | | Tank | |
|---|---|---|---|---|---|
| W / S, RT / LT | Throttle up / down | Space / Shift, RT / LT | Climb / descend | W / S | Drive / reverse |
| Mouse or ↑ ↓, left stick | Pitch (↓ pulls up) | W / S, left stick | Nose down / up (fly forward / back) | A / D | Turn on the spot |
| A / D, left stick | Roll (bank to turn) | A / D, left stick | Turn | Mouse | Aim the turret |
| Q / E | Rudder | Q / E | Strafe | Left mouse / RB | Fire the cannon |
| Space / B | Wheel brakes | Mouse / right stick | Camera | | |
| Left / right mouse, RB / LB | Cannon / homing missile (Raptor) | Left / right mouse, RB / LB | Minigun / rockets (Warhawk) | | |
| F / Y | Bail out (the parachute opens by itself, or press Space) | F / Y | Bail out | F / Y | Climb out |

On a skateboard: W push, S foot-brake, A / D carve, Space ollie; in the air A / D kickflip / heelflip and
S shove-it; F step off.
Boats drive like cars (W / S, A / D); the police boat's bow gun fires with the left mouse button / RB.
In a shop, step onto the marker at the counter to open its menu: click a button, or move the highlight with
↑ ↓ / W S / the D-pad and buy with Enter / Space / A; Esc, Backspace or B leaves.

## How it is put together

```
Source/AutoTheftGrand/Private/Gen/    the world generator and mesh builders: plain C++ with no Unreal code,
                                      ported line for line from src/world/*.js, src/entities/*models.js,
                                      loft.js, vehicledefs.js and src/ui/mapimage.js
Source/AutoTheftGrand/Private/Sim/    the game: plain C++ with no Unreal code, ported line for line from
                                      src/game and src/entities (Game, Input, Collision, Character, Player,
                                      Animator, Ragdoll, Vehicle, Vehicles, Camera, Env, Weapons, Peds,
                                      Traffic, Rail, Train, Humanoid, Gameplay, Hud, Effects, Combat); Setup puts the
                                      systems together as main.js does
Source/AutoTheftGrand/Private/Game/   the Unreal side: draws the simulation and feeds it input
  ATGWorld          runs the generator on a worker thread, then builds the world over a few frames:
                    procedural meshes for terrain, roads, ground, buildings and water; instanced meshes
                    for props, trees, containers and sleepers; hidden collision; the sky, sun, moon,
                    fog, clock and street lamps; fine terrain with collision streamed round the player
  ATGMaterials      the browser game's GLSL ported to HLSL custom nodes, built as material assets in C++
  ATGMeshUtil       generator buffers -> procedural mesh sections and runtime static meshes
  ATGCar            draws a simulated vehicle: its parts on the sprung body, doors and lids on their hinges,
                    wheels, lights, dents, lost panels; trains with their carriages and wagons; bikes; and
                    (ATGCarAir.cpp) aircraft and the tank: props and rotors with their blur discs, the gear,
                    canopy, afterburners, nav lights, the chin gun, the turret, gun and road wheels
  ATGHumanMesh      builds a person's skinned mesh at runtime (Sim/Humanoid, the port of humanoid.js)
  ATGPerson         draws a simulated person: that mesh on a poseable component that copies the pose's bones
  ATGEffects        draws the effects: particles as camera-facing quads, decals, skid marks, tracers, the flash
                    lights, and debris
  ATGPoliceView     draws the police helicopter (body, rotors, the searchlight's cone and spot light) and the
                    roadblocks' spike strips
  ATGPickups        draws the pickups over their glow and the markers' glowing cylinders and arrows
  ATGAnimals        draws the animal models with the simulation's legs, head, tail and wings; meshes are
                    shared between animals of the same breed
  ATGPlayerController  keyboard, mouse and gamepad into the simulation's input; shows its camera
  ATGGameMode       runs the simulation each frame and keeps an actor for each of its vehicles and people
  ATGHUD            the HUD: the GTA V style minimap (turning with the camera, blips, police flashes and
                    search cones) with health, armour and special bars, the wanted stars, cash and weapon,
                    zone and vehicle names, the speedometer, and what the simulation's HUD model holds: help, big messages,
                    subtitles, the bar, the fade, the damage vignette, the WASTED / BUSTED shard; the
                    loading screen and the full-screen map
  ATGPainter        a small 2D canvas over UCanvas (paths, arcs, fill, stroke, transforms) so hud.js's
                    canvas drawing ports line for line
  ATGTest           test hooks: ATG.* console commands and scripts (see below)
Tools/              command-line checks that need no Unreal (see below)
```

The generator runs in the game's own axes: metres, y up, the same numbers as the JavaScript. It is mapped to
Unreal's axes (centimetres, Z up) only at the edges; `Game/ATGCoords.h` has the mapping.

### Testing

`Tools/build.sh` builds the editor target from Git Bash. `Tools/run.sh Tools/tests/smoke.txt` runs the game off
screen with fixed 1/30 s frames and a script of console commands (`ATG.Teleport`, `ATG.Press KeyW`,
`ATG.Spawn zenith`, `ATG.Enter`, `ATG.Time 21`, `ATG.State`, `ATG.City`, `ATG.Station union`, `ATG.Kill`,
`ATG.Bust`, `ATG.Explode`, `ATG.Wreck`, `ATG.Fx`, `ATG.Give rpg 5`, `ATG.Wanted 3`, `ATG.NoBust 1`, `ATG.Police`,
`ATG.CamHeli`, `ATG.Roadblock`, `ATG.CamRoadblock`, `ATG.CamArmy heli`, `ATG.Crime mug`, `ATG.Crimes`, `ATG.MouseMove 60 40`, `ATG.Heist here`, `ATG.CamAt 3 1.6 2 0 0.7 5.5`, `ATG.Press MouseRight MouseLeft`, `shot name`,
`wait 2`, `quit`); screenshots go to `Saved/Screenshots/WindowsEditor/ATG/`. `Tools/tests/city.txt` and
`city2.txt` look at the traffic, the people, the traffic lights and the trains; `human.txt` at a person
up close, walking and running; `wasted.txt` at WASTED and BUSTED; `effects.txt` at an explosion and a burning
car; `hud.txt` at the HUD on foot and in a car; `combat.txt` at aiming, shooting and the RPG; `police.txt` at a
pursuit and the helicopter by night and by day; `pickups.txt` at a pickup and the save and Spray Shack
markers; `roadblocks.txt` at a roadblock and its spike strip; `npccrime.txt` at a
suspect's stars, the patrol that comes for them and a staged car theft; `wheel.txt` at the weapon wheel; `special.txt` at the
special ability; `heist.txt` at a robbed armoured van; `phone.txt` at the phone; `bikes.txt` and `bikes2.txt` at
the motorbikes and bicycles; `aircraft.txt` at each aircraft and the tank, parked, flying and firing; `army.txt` at the army at five stars and
Fort Carver's gate and restricted area; `skate.txt` at a skateboard, a kickflip and the skatepark; `boats.txt` at the Santa Luz marina, a speedboat's
wake and the police boats; `shops.txt` inside each kind of shop; `shopmenu.txt` at the Gun Barn's clerk and menu
and a drink at the bar. The runs are silent and step at a fixed 1/30 s; `SOUND=1 Tools/run.sh
Tools/tests/audio.txt` runs at real speed with the sound on, and `ATG.Audio` logs the sound's clock and live
node count.

The simulation is tested without Unreal. `Tools/native.sh` builds a tool with MSVC from Git Bash:

```bash
cd Tools
./native.sh simtest.exe simtest.cpp && ./simtest.exe          # walking, driving, crashes, parked cars,
                                                               # traffic and people, trains, boarding,
                                                               # WASTED, effects, combat, police, pickups,
                                                               # roadblocks, street crime, the wheel, special,
                                                               # armoured vans, the phone, bikes,
                                                               # aircraft and the tank, the army, skateboards,
                                                               # boats
DEBUG=1 ./native.sh simtestd.exe simtest.cpp                   # with symbols: a crash prints a stack trace
./native.sh audiotest.exe audiotest.cpp && ./audiotest.exe wav # every sound: peak and loudness per 50 ms
                                                               # (and WAV files in wav/); audiocmp.mjs prints
                                                               # the browser's, within a few dB
./simtest.exe vehcompare > cppveh.txt                          # the car physics against the browser game's:
node --import ./three-hook.mjs vehcompare.mjs > jsveh.txt      # identical to 4 decimals
diff jsveh.txt cppveh.txt
./simtest.exe wildlife                                       # spawning, fleeing, damage and walked dogs
./simtest.exe pets                                           # adoption, commands, bites, rides and treats
./simtest.exe missions                                       # mission lifecycle, waits, failure and routes
./simtest.exe story                                          # Chapters I-IV and their failure rules
node --import ./three-hook.mjs storycompare.mjs               # story metadata against story.js
node --import ./three-hook.mjs animalcompare.mjs --check      # 17 breeds' rigs against animals.js
./simtest.exe aircmp heli > cpp_heli.txt                       # a run (plane, heli, tank, skate or boat) against the
MODE=heli node ../../../tools/browser-test/run.mjs "http://localhost:8080/index.html?manual&autostart=free&q=low" \
  out ../../../tools/browser-test/tests/aircmp.mjs | grep -E '^[0-9.]+ x' > js_heli.txt   # browser game's: identical
```

The browser game gives the numbers the `city` and `rail` tests compare against (from the repository root,
with `npm start` running): `node tools/browser-test/run.mjs "http://localhost:8080/index.html?manual&autostart=free&q=low" out/city tools/browser-test/tests/citycompare.mjs`.
The trains match to the metre: both reach Fern Creek 110.7 s after leaving Union Station.

`Tools/tests/wildlife.txt` checks animal rendering, fleeing and district populations in Unreal. Test commands:
`ATG.Animal breed [dx dz]` spawns an animal, `ATG.Animals` logs nearby animals, and `ATG.Animals clear` or
`ATG.Animals scare` clears or frightens ambient wildlife.
`Tools/tests/pets.txt` visits Pet Palace, buys a pet, whistles and takes it for a car ride. `ATG.Pet breed
name` adopts directly for tests; `ATG.Pet command`, `ATG.Pet release` and `ATG.Pet hurt amount` exercise its
commands and damage. `ATG.Pet` logs its state. `ATG.ShopView petshop` looks at the kennels.

`Tools/tests/missions.txt` exercises the mission engine with a test fixture (dialogue, GPS, target arrows,
pass, abort and failure). `ATG.Missions` logs progress; `ATG.Missions id` starts a registered mission.
`ATG.MissionTest` starts the fixture; `goal` reaches its checkpoint and `finish` kills its target.
`Tools/tests/missions_shutdown.txt` exits during a suspended cutscene to check shutdown cleanup.
`Tools/tests/story.txt` runs the actual Chapter I missions in Unreal with objective assistance, like the
browser's mission runner. `ATG.StoryStart id` positions the player at a mission's start, and
`ATG.StoryAdvance` reaches its current checkpoint, seats followers and removes hostile targets for tests.
`Tools/tests/story2.txt` runs Chapter II. `ATG.StoryStart id unlock` also seeds that mission's prerequisites
for isolated tests. Race failure restores ambient traffic and pedestrians during cleanup.
`Tools/tests/story3.txt` checks Chapter III, including Deacon's actual route and the automatic ambush and
hospital sequence. Scripts can use `repeat count interval command` to repeat objective assistance or state
checks without interrupting the simulation.
`Tools/tests/story4.txt` checks Chapter IV's truck delivery, timed stunt run, gang crews and Salazar encounter.

### Checking the generator without Unreal

```bash
cd Tools
./native.sh gentest.exe gentest.cpp && ./gentest.exe > cpp.txt
node --import ./three-hook.mjs dumpworld.mjs > js.txt && diff js.txt cpp.txt   # C++ world vs the JavaScript one
./native.sh carstest.exe carstest.cpp && ./carstest.exe > cppcars.txt          # car models vs vehiclemodels.js
node --import ./three-hook.mjs dumpcars.mjs > jscars.txt && diff jscars.txt cppcars.txt
./native.sh humantest.exe humantest.cpp && ./humantest.exe > cpphuman.txt      # people vs humanoid.js
node --import ./three-hook.mjs dumphuman.mjs > jshuman.txt && diff --strip-trailing-cr jshuman.txt cpphuman.txt
./native.sh meshpreview.exe meshpreview.cpp && mkdir -p out
./meshpreview.exe out       # software renders of props, cars, parts of the city
./meshpreview.exe out map   # the radar map
```

*Auto Theft Grand is an original fan-made parody. All names, places and characters are fictional.*
