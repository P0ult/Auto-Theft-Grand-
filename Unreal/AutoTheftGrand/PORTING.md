# Porting plan: browser game → Unreal Engine 5.8

The port is done in phases, and each phase leaves a game you can play. The rule throughout is to port the
JavaScript's behaviour rather than approximate it. The world generator matches the original number for
number, so the Unreal world has the same streets, buildings and parking spots as the browser one.

## How it is built

The game logic is plain C++ under `Private/Sim`, with no Unreal code, ported line by line from the
JavaScript: the game loop and its systems, input, the collision world (`collision.js`), characters, the
animator, ragdolls, vehicles and the camera rig. It builds and runs in a native test runner
(`Tools/simtest.cpp`), and its numbers can be checked against the browser game run in node: the car
physics comes out identical to 4 decimals (`Tools/vehcompare.mjs`). Unreal's own physics and collision are
not used. The Unreal side (`Private/Game`) draws the simulation's state, feeds it the keyboard, mouse and
pad, and plays its sounds.

## How the systems map

| Browser (src/…) | Unreal | Notes |
|---|---|---|
| world/worldgen.js, roadnet.js, roadlayout.js, northcity.js, railway.js, citymap.js, countryside.js | `Gen/*` (plain C++) | Bit-exact port: Mulberry32, float32 storage where JS used Float32Array, stable sorts, V8's `Math.hypot`. `Tools/gentest` diffs it against `Tools/dumpworld.mjs`. |
| world/terrainmesh.js, roadmesh.js, city.js, props.js, vegetation.js | `Gen/TerrainMesh.cpp`, `RoadMesh.cpp`, `CityMesh.cpp`, `PropMesh.cpp` | Same geometry. The four per-vertex channels carry what the shaders need (see `ATGMaterials.h`). |
| world/shaders.js, render/materials.js | `ATGMaterials` | GLSL → HLSL custom nodes, generated as assets in the editor. No screen-space derivatives (ray-tracing safe). |
| entities/humanoid.js | `Sim/Humanoid`, `ATGHumanMesh`, `ATGPerson` | The smooth-skinned mesh, identical to the JavaScript's (`Tools/humantest.cpp`), as a skeletal mesh built at runtime on a poseable component that copies the simulation's 17 bones. Its material is humanoidMaterial's (`ATGMaterials` Human): the knit, skin, hair and denim patterns from the bind-pose position, faded with distance, each part's roughness, and metal buckles and badges. |
| world/environment.js, render/sky.js | `ATGWorld` sky | SkyAtmosphere, a sun and a moon as atmosphere lights, a real-time sky light, height fog, the same clock and sun path. |
| entities/vehicledefs.js, vehiclemodels.js, loft.js | `Gen/VehicleDefs`, `Gen/VehicleModels`, `Gen/Loft` | The whole catalogue; car models identical to the JavaScript's (`Tools/carstest.cpp`). |
| world/collision.js (+ the colliders city.js, roadmesh.js and vegetation.js add) | `Sim/Collision`, `Gen/RoadMesh` | The same boxes, oriented boxes, circles and decks, in the same order (`Tools/coltest.cpp`). |
| game/game.js, core/input.js, core/events.js | `Sim/Game`, `Sim/Input`, `Sim/Events` | The frame order and slow motion as in the browser game. |
| entities/character.js, animator.js, ragdoll.js, game/player.js | `Sim/Character`, `Sim/Animator`, `Sim/Ragdoll`, `Sim/Player` | All of it; drawn with the skinned humanoid (`ATGPerson`). |
| entities/vehicle.js, game/vehicles.js | `Sim/Vehicle`, `Sim/Vehicles` | All of it: physics, tumbling, damage, dents, lost panels, fire, explosions, enter / exit / carjack sequences, parked cars. Drawn by `ATGCar`. |
| game/camera.js | `Sim/Camera` | All the cameras, including the cinematic and flight cameras. |
| world/environment.js | `Sim/Env` | The clock and the weather; `ATGWorld` lights the sky from it. |
| game/traffic.js | `Sim/Traffic` | Lane following, traffic lights (the shaders' clock is the simulation's, so the lamps and the cars agree), give way, roundabouts, merges, queues, passing broken-down cars, panic at gunfire, spawning and despawning. |
| game/peds.js | `Sim/Peds` | Spawning on the walk graph, wandering, fleeing, gangs on their turf, followers and guards. Cops think in the police system, people with a crime on in the street-crime system. |
| game/pickups.js | `Sim/Pickups`, `ATGPickups` | Health, armour, cash and weapon pickups (the world's respawn after 90 s; drops last 40 to 45 s), the 30 hidden packages and the safehouse rewards, the markers (the cylinder's shader evaluated per ring of vertices), the Spray Shack (paint, repairs, loses the police) and the save point (the save menu comes with save and load). Big Bun's `eat` is there for the shops. |
| ui/weaponwheel.js | `Sim/WeaponWheel`, `ATGHudModern.cpp` (DrawWheel) | All of it: Tab or a held LB opens it at 0.18 speed, the mouse or right stick picks, letting go draws the weapon, a quick LB tap goes back one weapon; in a car only what you can shoot from a seat. The ring, the icons (inverted on the highlighted slot), the name and ammo, the dimmed screen. |
| game/special.js | `Sim/Special`, `ATGHudModern.cpp` (the bar), `ATGWorld::SetFringe` | All of it: the meter (a trickle, speed, drifts, jumps, kills and headshots), Caps Lock / Z / both sticks, 0.38 on foot and 0.5 at the wheel with 1.25 grip, the real-time drain, the drained colour. The fringing maps postfx.js's uChroma onto Unreal's scene fringe by eye. |
| game/heists.js | `Sim/Heists`, `ATGCar` (the rear doors) | All of it: a Stockade with two armed guards every 150 to 260 s, the first sighting, alerts (shots, rams, a guard down, a carjack), the run for it, four shots in the back doors or a blast, the four cash bags, the guards out fighting, two stars, the robbed message, the clean-up. |
| ui/phone.js | `Sim/Phone` (with freeroam.js's `roadSpot`), `ATGHudModern.cpp` (DrawPhone) | The phone and its keys (it takes the arrows, Enter and Backspace while out; you still walk), the home screen, Contacts (Lester, Benny's delivery, Merryweather's contractors), the fifteen cheat codes with their five-minute timers, Snapmatic, the map, weather and stats. The cab, Pegasus and SKYFALL go through the taxis and the admin tools. The phone's glyphs are drawn as small shapes and words (the engine font lacks them). |
| entities/bikes.js | `Gen/BikeModels` (the models, identical to the JS: `Tools/dumpbikes.mjs`, `bikestest.cpp`), `Sim/Bike`, `ATGCar` | All of it: motorbikes and bicycles on the car tyre model, leaning into turns and onto the stand, the rider astride (feet on the pegs or the turning pedals), hard hits throwing the riders, knobbly tyres off-road; in traffic and parked. |
| entities/aircraft.js | `Gen/AircraftModels` (the models, identical to the JS: `Tools/dumpaircraft.mjs`, `aircrafttest.cpp`), `Sim/Aircraft`, `ATGCarAir.cpp` | All of it: planes and jets on the arcade flight model (the take-off roll, rotation, lift fading below stall speed, weathervaning, the hammerhead stall, bank-to-turn, gear that tucks away, belly landings, crashes into the ground), helicopters (the tilting rotor disc, the auto-hover, the flare near the ground, downwash dust and spray, the spin when shot down), the tank (tracks, the turret and gun turning toward the crosshair, the cannon, its recoil and the hull's rock, the turret blown off its ring), the cannons, missiles, minigun and rockets, wrecks tumbling down and sinking. A take-off and climb, a hover and a tank drive print the same numbers to two decimals as the browser game (`simtest aircmp` against `tools/browser-test/tests/aircmp.mjs`). |
| world/interiors.js | `Gen/CityMap` (`PlanInteriors`), `Gen/InteriorMesh` (identical to the JS: `Tools/dumpinteriors.mjs`, `interiorstest.cpp`), `ATGWorld`, `ATGPictures` | The walk-in shops: the doorways cut into the buildings, the rooms, their furniture and its colliders, the clerks' and tills' spots, and how they are drawn: the lit and glowing surfaces, the glass cases, the weapons on the Gun Barn's wall, the posters and menu boards (painted once into render targets; the engine's Roboto stands in for Impact, Arial and Georgia) and the room light that follows the shop nearest the camera. The shopkeepers and the shop menus come with shops.js. |
| game/shops.js, ui/hud.js (openShop, openStore), ui/padnav.js | `Sim/Shops`, `Sim/Hud` (the menu model), `ATGHudModern.cpp` (`DrawShopMenu`), `ATGPlayerController` (`MenuInput`) | The clerks with their looks, greetings and threats, serving at the counter's marker, hands up and the till's cash on the counter (and the cops called), the Gun Barn's owner fighting back, a dead clerk's shop shut until you've been away, the stocks (the Gun Barn's weapons and armour, Big Bun, the 24/7s, Ray's Liquor and its scratch cards, the bar's drinks and the sway, the cafe), the menus with the mouse, the keys and the pad. Pet Palace sells all nine breeds and treats and has dogs and cats in its kennels. `simtest shops` runs a visit to four of them. |
| entities/skateboard.js, world/skatepark.js | `Gen/BoardModel` (identical to the JS: `Tools/dumpboard.mjs`, `boardtest.cpp`), `Sim/Skateboard`, `Sim/Skateparks`, `ATGCar` (the deck), `ATGWorld` (the park) | All of it: pushing with the back foot, carving, ollies, kickflips, heelflips and shove-its, clean landings paying cash and crooked ones throwing you off, digging in off the pavement; the Santa Luz skatepark's ramps, tables, ledges and rail as collision decks and boxes, its mesh, the boards lying about and two locals skating laps. A ride with an ollie prints the same numbers as the browser game (`simtest aircmp skate`). |
| entities/boat.js, game/boats.js | `Gen/BoatModels` (identical to the JS: `Tools/dumpboats.mjs`, `boatstest.cpp`), `Sim/Boat`, `Sim/Boats`, `ATGCar` (the boats), `ATGWorld` (the pontoons), `Effects` (the wakes) | All of it: the hulls riding the swell (bigger in storms, smaller on the lake), planing, heeling into turns, running aground, burning and sinking, the bow gun; the marinas and their moored boats, the cruising routes, the harbour patrol and the police boats that ram and shoot when you're wanted at sea. A run prints the same numbers as the browser game (`simtest aircmp boat`). The wake's foam is drawn as soft strips at the shader's average cover rather than with its noise pattern. |
| game/audio.js | `Sim/WebAudio` (a small Web Audio work-alike), `Sim/Audio`, `Game/ATGAudio` | All of it: every synthesised effect with the same envelopes, filters and sweeps, the panners, the reverb send, the world muffle on WASTED / BUSTED, the compressor, the player's engine, tyres, wind and horn, the aircraft engines, the sirens and the helicopter's rotor, the ambience, birds, crickets and thunder, and the three radio stations. Each effect's loudness every 50 ms comes within a few dB of the browser's own Web Audio rendering of audio.js (`Tools/audiotest.cpp` against `tools/browser-test/tests/audiocmp.mjs`). Three differences: the panner is the equal-power model (the browser game asks for HRTF), the 2.2 s convolution reverb is an algorithmic reverb with the same decay, and the WASTED stinger is the synthesised one (the browser plays a recording when it has one). |
| entities/animals.js, game/wildlife.js | `Gen/AnimalModels`, `Sim/Animal`, `Sim/Wildlife`, `ATGAnimals` | The same breed models and procedural rigs; district mixes, group sizes, quality caps, valid-ground spawning, grazing, wandering, fleeing, bird flight and landing, walked dogs, cleanup, bullets, melee, blasts and roadkill. `simtest wildlife` checks behaviour; `animalcompare.mjs --check` compares 17 breeds' walk, run/flight and sit poses to the browser. |
| game/pets.js | `Sim/Pets`, `Sim/Shops`, `ATGAnimals`, `ATGHUD` | Adoption and names, triple health, following and sitting, stay/come and aimed attack commands, defensive dog bites, NPC reactions to animal damage, car boarding and alighting, death and cleanup, kennel animals, adoption prices and treats. The HUD shows the pet's name, status and low-health colour. The pet is saved with the game and travels in the multiplayer state. |
| game/missions.js, game/gps.js | `Sim/Missions`, `Sim/Hud`, `ATGHUD`, `ATGPerson` | C++20 coroutines preserve the async scripts' waits and nested cutscenes. The context manages dialogue, objectives, timers, counters, entities, target arrows, blips, checkpoints, GPS, drive-bys and failure predicates. The engine manages prerequisites, contacts, vehicle restrictions, rewards, chapter messages, death/arrest failures and cleanup. RouteDriver and RaceDriver use the browser's rules. Native and Unreal tests cover the lifecycle, including shutdown with a suspended script. |
| game/story.js | `Sim/Story`, `Sim/StoryChapterTwo/Three/Four/Five/Six`, `Sim/StoryNorth` (story_north.js), `ATGGameMode` (`-ATGStory`), `ATGMissionView` | Chapters I–VIII (35 scripts), from Welcome Home through Saints and Sinners. Same cast, dialogue, cutscenes, objectives, encounters, restrictions, rewards and logs. This includes the cab pickup, held Sol Line train and Fern Creek ambush, low-flight rings, compound buzz, cartel trucks and El Seco escape. The rings use the same torus geometry and radius x 1.15 threshold and clean up on completion or cancellation. Credits roll for 40 s and close at 42 s (or on input). `simtest story` completes each script and checks failures; `storycompare.mjs` checks metadata; `Tools/tests/story*.txt` verify Unreal. Chapter VII's San Aurelio missions keep the browser's quirks (the Saints' boats never get a gunner: the browser checks a seat list no boat has). Chapter VIII is ported in `StoryNorth.cpp`: Pacific Star, Timber!, High Ground and Saints and Sinners, including the ship raid. The native suite and `Tools/tests/story8.txt` cover all four Chapter VIII missions; the Unreal script completed with 33 total story missions recorded. |
| game/military.js | `Sim/Military` | All of it: the parked aircraft, tanks and trucks at Fern Creek Airfield, the hospital roof and Fort Carver streamed in within 1.1 km and respawned a while after they're taken or wrecked, their map icons, the garrison on its 15 posts and patrols, the restricted zone (the warning, eight seconds' grace, then the alert, three stars and soldiers opening fire), the lockdown lifted when the stars are gone. |
| game/army.js | `Sim/Army` | All of it: at five stars Barracks trucks and Ranger jeeps that run you down and unload troops, the Warhawk gunship's autopilot (circling at 60 m, its minigun bursts and rockets), the Mammoth tank that holds back and shells you, holding fire near their own, the radio, the radar blips, pulling out below five stars and the clean-up. |
| game/npccrime.js | `Sim/NpcCrime`, `ATGHUD` (stars over suspects, speech bubbles) | All of it: jaywalkers, reckless drivers, hit-and-runs, road rage, muggers and car thieves staged near you (more at night and in the rough districts), witnesses and phoned-in reports, stars on the culprit, dispatch, the responding unit (pull-overs, tickets, chases, tackles, the ride in the back) and the radar dots. |
| game/roadblocks.js | `Sim/Roadblocks`, `ATGPoliceView` (spike strips) | All of it: from three stars a line of cruisers (the Sheriff's out of town, the Enforcer at four stars) across the road 90 to 165 m ahead of a driving suspect, cops in cover behind, the stinger that bursts tyres (sparks while you drive on the rims), the dispatch call, the radar squares, the clean-up. The strip now lies on the road surface; in both games it used to sit at the terrain's height, a few centimetres under the road. |
| game/police.js | `Sim/Police`, `ATGPoliceView` | All of it: heat and the six thresholds, witnesses, evading (10 + 5 x stars seconds out of sight), patrol cars, pursuit drivers that route along the roads and then ram, cops on foot who shoot or come to arrest you, roadblock cops holding their line, BUSTED, and the helicopter with its searchlight and its sniper at four stars. |
| game/railsystem.js, entities/train.js | `Sim/Rail`, `Sim/Train`, `Gen/TrainModels` | The timetable, the single-track sections and the Fern Creek passing loop, level crossings, boarding, driving from the cab. The trains run to the metre as in the browser game. |
| game/gameplay.js | `Sim/Gameplay` | Stats, WASTED and BUSTED (slow motion, the death camera, the respawn at the hospital or the police station, the bill), drift and stunt bonuses, smoke and fire on damaged cars, skid marks, first-time hints. |
| game/combat.js | `Sim/Combat`, `ATGEffects` (projectiles) | Hitscan with spread and pellets (aimed from the camera, fired from the muzzle), hits on people, vehicles and the world, melee with its combos and the bat denting cars, explosions sized to the vehicle, rockets, homing missiles, tank shells, grenades, molotovs and their burning pools, people on fire, shooting down the police helicopter (and locking on to it). The animals join the ray with the wildlife. |
| game/effects.js | `Sim/Effects`, `ATGEffects`, `ATGMaterials` (FxAlpha, FxAdd) | The three particle pools, decals, skid marks, tracers, flash lights, explosions with their fire, flying wreckage, smoke columns and secondary blasts, prop and panel debris, broken props restored far away. The canvas textures (smoke, soft dot, the decal atlas) are drawn per pixel in the materials. Boat wakes and rain come with boats and the weather. |
| ui/hud.js (messages and overlays) | `Sim/Hud` (`HudModel`), `ATGHUD` | Help, big messages, subtitles, objectives, the bar, money pops, dispatch, the fade, the damage flash, the shard: the state and timers in the simulation, drawn by `ATGHUD`. |
| ui/hud.js (the modern and classic layouts) | `ATGHudModern.cpp`, `ATGPainter` | The minimap (`_drawRadar`: the map turned with the camera, the city layer clipped in, police flashes and search cones, blips and their icons, north, the player arrow, the GPS route), the health, armour and special bars, the stars, cash, weapon icon and ammo, the zone and vehicle names, the speedometer (`_drawSpeedo`: dial, red zone, gear, damage bar, the altimeter for aircraft). The map waypoint (`Sim/Hud` setWaypoint, the pause map, cabs and the Teleport tab use it) and the browser's fonts (`ATGFonts`, with the CSS fallbacks). The classic layout (Settings: HUD style): the round radar with its vignette and rim, the clock, the weapon badge and ammo, the armour and health bars, the cash and stars, the zone and vehicle names in the classic fonts. |
| net/net.js, avatar.js, netui.js, npcsync.js, transports.js (WsTransport) | `Sim/Net`, `Sim/NpcSync`, `Sim/Json`, `ATGNetTransport` (the WebSocket), `ATGHudNet.cpp` (name tags, the chat feed, the ONLINE badge), `ATGMenuSystem` (the Online tab), `ATGPlayerController` (the chat box) | The browser's JSON state at 12 Hz (3 when paused), the 150 ms interpolation, remote avatars posed from it, the cars and aircraft they drive as kinematic stand-ins, the moments (shots, hits, vehicle hits, explosions, chat, kills), player damage on or off, the host's clock and weather, pets. Shared NPCs (npcsync.js): the pedestrians and traffic near other players on the world channel at 6 Hz, drawn there as stand-ins whose damage goes back to their owner, stolen cars handed over, spawn budgets that count everyone's NPCs, and the trains on the host's timetable. It talks to `server.mjs`'s relay, so a browser and the Unreal game share a room (checked with a browser client: `Tools/tests/online-cross.txt`, `simtest net npcsync`). Not ported: the claude.ai room transport, which only exists inside the published page. |
| game/freeroam.js, admin.js, ui/hud.js (Admin and Vehicles tabs, openConsole) | `Sim/FreeRoam`, `ATGMenuSystem`, `ATGPlayerController` (the console) | Free roam's kit, cash and ammo, the destinations and teleports, the twelve cheat switches and their powers, the street density, the one-shot actions, the vehicle spawner (`summon`: into or beside you, planes in the air, helicopters in the air when it's cramped, boats in open water, five kept), and the console's commands (`run`). `simtest admin`, `simtest admintools`, `Tools/tests/admin.txt`. |
| game/taxi.js, races.js, vigilante.js, modshop.js, properties.js | `Sim/TaxiSystem`, `Sim/Races`, `Sim/Vigilante`, `Sim/ModShop`, `Sim/Properties` | The side jobs: cabs you hail or drive, the four street races, vigilante patrols, the Customs garages and the businesses for sale with their tills (`simtest jobs`, `Tools/tests/jobs.txt`). |
| game/shipraid.js, world/cargoship.js | `Sim/ShipRaid`, `Gen/CargoShip` (identical to the JS: `Tools/dumpship.mjs`) | The cargo ship raid. |
| game/save.js | `Sim/SaveSystem`, `UATGGameInstance` | The save file (`Saved/AutoTheftGrandSave.json`), Continue, the safehouse's save panel, and the settings (`Game.ini`). |
| game/weapondefs.js | `Sim/Weapons` | The weapons' numbers and their order (the network sends weapons by index). |
| main.js (the systems) | `Sim/Setup` | Registers the systems in main.js order and populates the streets. |
| ui/mapimage.js, hud.js | `Gen/MapImage.cpp`, `ATGHUD` | The map is drawn by a small software rasteriser. The HUD is a first cut. |

## Phases

**Status.** The UE 5.8 editor target builds with the installed Visual Studio toolchain. The generator and
simulation have native test runners and parity comparisons for the core geometry, models and gameplay paths.
The `Tools/tests/smoke.txt` Unreal run now completes: it generates and builds the world, starts the game,
walks, enters and drives a Zenith, changes to night, writes three screenshots, and exits with code 0. The
automated runner now waits for world construction before starting gameplay; this also avoids a startup render
thread crash caused by beginning the simulation while generated scene components were still being added.
Phases 2, 3, 3b and 4 are implemented. Phase 5 includes the mission engine and Chapters I–VIII.
The native suite passes, and the Unreal Chapter VIII script completed all four new missions (33 total story
missions recorded). Several optional single-player features and phase 6 items remain.

**Phase 1: the world and driving (done).**
- World generation.
- Every static mesh: terrain with streamed detail, roads, bridges, the railway, city ground, buildings,
  props, trees, water.
- Materials, the day and night cycle, street lamps.
- Walking, parked cars, getting in and out, the full car physics.
- Radar and map.

**Phase 2: a living city.**
- Traffic on the lane graph (traffic.js). Done.
- Pedestrians and gangs on the walk graph (peds.js). Done.
- Traffic lights. Done: the lamps read the simulation's clock (`SimTime` in the parameter collection).
- Trains on the timetable (railsystem.js, train.js). Done.
- A skinned humanoid with the procedural animator, and ragdolls. Done.

**Phase 3: action.**
- Weapons, melee, damage and effects (combat.js, effects.js). Done.
- Pickups and shops. Done, including Pet Palace.
- Street crime (npccrime.js). Done.
- Wanted level and police (police.js). Done.
- Car damage, fire and explosions; vehicle tumbling (vehicle.js `_tumble`). Done (with vehicle.js in phase 1).
- WASTED and BUSTED screens. Done.

**Phase 3b: the GTA V layer** (added to the browser game after phase 1).
- The modern HUD: a rectangular minimap with health, armour and special bars, and the police's vision cones
  while they search.
- The weapon wheel (weaponwheel.js) and the special ability (special.js), both with slow motion. Done.
- The phone (phone.js): contacts, timed cheats, photo mode. Done.
- Roadblocks and spike strips (roadblocks.js), and the army at five stars (army.js). Done.
- Armoured van heists (heists.js), the cinematic car camera, and the new vehicles: Stockade, Enforcer,
  Sheriff SUV, Buffalo S, Baller, Tempest. Done (the camera and the cars came with phase 1).

**Phase 4: the rest of the vehicles.**
- Motorbikes and bicycles, planes, jets, helicopters and the tank, Fort Carver (military.js), skateboards and
  the skatepark. Done.
- Boats and police boats. Done.

**Phase 5: story and features.**
- The mission engine (missions.js), including all 35 missions in Chapters I–VIII. Native story tests and
  the successful Unreal `Tools/tests/story8.txt` run cover Chapter VIII's four missions.
- The ship raid is implemented with Pacific Star; it has scripted mission coverage.
- Taxis, races, vigilante, Customs garages and property. Done: `simtest jobs` plays a race, a vigilante call,
  an upgrade, a business and its till, a cab ride and a save; `Tools/tests/jobs.txt` shows each in Unreal.
- Free roam and admin tools have simulation implementations. The pause-menu regression verifies Xbox
  input routing, settings in both directions, all twelve Admin toggles and both spawn-option toggles.
  The pause screen uses explicit ON/OFF badges and retains selection after changes. Remaining free-roam
  gameplay paths still need dedicated Unreal coverage.
- Save and load. Done: a mission's autosave writes `Saved/AutoTheftGrandSave.json`, and Continue restores it after
  a restart (`Tools/tests/save1.txt`, `save2.txt`).

**Phase 6: the finish.**
- Walk-in interiors with their furniture and shopkeepers. Done.
- Landmarks. Done: `Gen/Landmarks` builds the Santa Luz pier (planked deck, posts, rails, booths, lamps), the
  Ferris wheel, the Vistawood sign, the dock cranes and the aviation beacons on tall roofs and cranes, with their
  collision, as landmarks.js does (`Tools/dumplandmarks.mjs` against `landmarkstest.cpp`: identical). The wheel
  turns with its gondolas hanging level, the neon and pier lamps light up at night, the letters glow, and the
  beacons blink.
- Rain and puddles. Done: the streaks round the camera (effects.js Rain), wet ground with puddles on level
  surfaces (materials.js WET_GLSL) on the terrain, roads, streets, lots and flat roofs, and the weather in the sky
  (cloud and rain dim the sun, the fog thickens and greys, lightning, lamps on earlier), and raindrop ripples in the
  puddles on the terrain, roads, streets and lots (materials.js atgRipples / WET_NORMAL).
- Chain-link fences, billboards and the shops' name signs. Done: `Gen/CitySigns` places the 71 rooftop billboards
  (with their collision) and 74 signs as city.js does (`Tools/dumpsigns.mjs` against `signstest.cpp`: identical);
  `ATGPictures` paints the ad atlas and the signs, which glow at night; the fences get their see-through panels.
- Audio and the radio stations. Done.
- Wildlife and pets. Done.
- Multiplayer. Done as the browser does it (net.js and npcsync.js, not Unreal's replication, so the two can
  share a room): the state, avatars, vehicle stand-ins, moments, player damage, chat, the host's clock, weather
  and trains, shared pedestrians and traffic, the Online tab and the chat box.

## Known gaps

- The menus and the HUD use the browser's font stacks (`ATGFonts`), and the radar's letters its bold Arial. The
  Google fonts themselves are not in the repository: put them in `Content/Fonts/`, or the Windows fallbacks of
  the CSS stacks are used. Blurred text shadows (the mission title's 30 px glow, the subtitles' 8 px one) are
  drawn as a few offset copies.
- The WASTED look approximates postfx.js's grade with Unreal's post-process (saturation, gain, vignette); its
  ghosting blur is postfx.js's own (a post-process material), but the grade's smoothstep curve is not.
