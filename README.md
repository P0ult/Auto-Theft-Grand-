# Auto Theft Grand — Los Soles

An open-world crime game that runs in your browser, inspired by the San Andreas era of the genre.
It's built with Three.js and WebGL 2 and needs no build step. The world, buildings, cars, aircraft, people,
animations, sounds and radio music are all generated procedurally when you load the page. The game ships no 3D
models or textures; the only asset file is the sound clip played on the WASTED screen.

You play Andre "Dre" Castillo. He comes home to the sunny, smoggy city of **Los Soles** after his little brother
Tino is gunned down. Within an hour of landing, a crooked detective has robbed him and dumped him in rival gang
territory.

## Play

Any static web server works. With Node.js installed:

```bash
npm start            # or: node server.mjs
# open http://localhost:8080
```

You can also use `npx serve .` or `python3 -m http.server 8080` (single player only: multiplayer needs
`server.mjs` or the claude.ai version). Opening `index.html` straight from disk won't work, because browsers block
ES modules on `file://`.

Chrome, Edge or Firefox with hardware acceleration turned on is recommended. If your frame rate is low, change
**Graphics quality** in Settings (Low / Medium / High / Ultra), or add `?q=low` to the URL.

## Controls

| On foot | | In a vehicle | |
|---|---|---|---|
| WASD | Move | W / S | Accelerate / brake & reverse |
| Mouse | Look | A / D | Steer |
| Shift | Sprint | **Space** | **Handbrake (drift)** |
| Space | Jump | H | Horn (Shift+H: siren in police cars) |
| C / Ctrl | Crouch | N | Next radio station |
| Left mouse | Punch (jab-cross-kick combo) / fire | V | Camera distance |
| Right mouse | Aim (people put their hands up) | B | Look behind |
| R | Reload | Right + left mouse | Drive-by with a pistol / SMG |
| Q / E, wheel, 1-9 | Switch weapon | G | Hydraulics (lowriders) |
| F / Enter | Enter or steal a car (carjack drivers) | F | Exit (bail out at speed) |

| Planes & jets | | Helicopters | | Tank | |
|---|---|---|---|---|---|
| W / S | Throttle up / down | Space / Shift | Climb / descend | W / S | Drive / reverse |
| Mouse or ↑ ↓ | Pitch (↓ pulls up) | W / S | Nose down / up (fly forward / back) | A / D | Turn on the spot |
| A / D | Roll (bank to turn) | A / D | Turn | Mouse | Aim the turret |
| Q / E | Rudder | Q / E | Strafe | Left mouse | Fire the cannon |
| Space | Wheel brakes | Mouse | Camera | | |
| Left / right mouse | Cannon / homing missile (jet) | Left / right mouse | Minigun / rockets (Warhawk) | | |
| F | Bail out (parachute opens by itself, or press Space) | F | Bail out | F | Climb out |

To take off, open the throttle, build speed down the runway and pull up once the speedometer passes rotation
speed. The landing gear retracts and deploys by itself. Touch down gently with the wings level: hitting the
ground hard, nose-first or with the gear up tears the aircraft apart.

| Taxis, trains & more | |
|---|---|
| H (on foot) | Whistle for a taxi |
| F by a cab you called | Get in the back; pick a destination (or use your map waypoint) |
| Space (in a cab's back seat) | Skip the trip (pay the estimated fare) |
| G (on foot) | Ride as a passenger in any car with a driver (or another player's car) |
| J (driving a cab) | Taxi driver side job on / off |
| F by the train | Board a carriage, or climb into the cab at the front to drive it (W / S) |
| T (free roam) | Teleport menu |
| / (multiplayer) | Chat |

Esc or P opens the pause menu: map (right-click to set a waypoint with GPS route), mission brief (teleport in
free roam), Online, stats, settings and controls. M opens the map directly. Standard-layout gamepads are
supported.

## Free roam

Pick **Free Roam** on the title screen for the whole map with every weapon, **unlimited cash and ammo**, and a
**Teleport** tab (or press **T**) that jumps to any town, station, airfield, the military base, city landmarks or
your map waypoint, taking your car or aircraft along. Free roam never touches your story save.

## Multiplayer

Open the **Online** tab in the pause menu (or pick **Multiplayer** on the title screen). Set a name and colour,
then join the **public world** or a **room code**: press **New room** and share the code with friends. Everyone
sees each other's characters, cars, aircraft, gunfire and explosions, can ride in each other's cars (G), chat
(press **/**) and, if player damage is on, fight. Each player's world keeps its own traffic and cops, and the
longest-connected player's clock and weather are shared.

- **On claude.ai:** the published version of the game uses the page's live room, so anyone with the page open
  can join.
- **Self-hosted:** `node server.mjs` includes a small WebSocket relay. Friends on your network open
  `http://<your-computer>:8080` and join the same room code. Up to 16 players per room.

## Features

- **World.** A GTA-style map of about 7 × 7 km: the city of Los Soles on the coast, six towns (Fern Creek, Pine
  Hollow, the desert town of Dry Wells, lakeside Mirador, the harbour town of Port Hale with its pier, and the
  desert crossroads of Puerto Seco), farmland, pine-forested mountains with a lake, a river with bridges, and a
  red-rock desert with mesas. The terrain is a streamed heightfield with level of detail,
  biome shading and instanced vegetation (pines, oaks, saguaros, dead trees, boulders).
  - **Roads.** A real road graph rather than a pure grid: the elevated six-lane Sol Freeway with on/off ramps and
    diamond interchanges, winding country highways, dirt tracks, roundabouts in town and in the city, bridges
    and viaducts, and superblocks that break up the grid (a stadium, a mall, a golf club and a park).
  - **The Sol Line.** A railway from Union Station on the edge of the city, past Fern Creek to Dry Wells, with
    level crossings where traffic waits for the train, bridges and underpasses at the highways, and three
    stations. The train runs the timetable on its own: ride it as a passenger, or take the cab and drive.
  - **Taxis.** Whistle for a cab, ride with the meter running and skip the trip, or drive a cab yourself and
    pick up fares for cash (with tips for speed and a bonus every fifth fare in a row).
  - **Fort Carver.** A walled military base in the desert with a runway, hangars, a control tower, barracks,
    helipads and a tank yard. You can steal the **Raptor** fighter jet, the **Hercules** cargo plane, the
    **Warhawk** attack helicopter, the **Mammoth** tank and army trucks. It's a restricted zone: after a
    warning, soldiers open fire and the police send a three-star response. The **Skipper** light plane waits at
    Fern Creek Airfield, and a **Skylark** helicopter sits on the hospital roof in the city.
- **City.** Eight districts (Cedar Row, Downtown, Market District, Rosewood, El Corona, Port Morena docks,
  Santa Luz Beach and Vistawood Hills) on a 1.7 km road grid, surrounded by hills and ocean. About 1,700 buildings
  with setback towers, gable-roof houses, warehouses and mansions. The Santa Luz pier has a working Ferris wheel.
  There is also a VISTAWOOD sign on the hill, cranes and a container ship at the docks, rooftop billboards,
  palms, street furniture, pools and a basketball court.
- **Graphics**
  - Atmospheric-scattering sky with a full day/night cycle: sunrises, sunsets, stars, moon, drifting clouds and
    weather (clear, cloudy, rain, storms with lightning, fog).
  - HDR pipeline with bloom, god rays, ACES tone mapping, colour grading, vignette and film grain.
  - Height fog that glows toward the sun. Shadows follow the player. Sky reflections on car paint and glass.
  - Building windows are generated in a shader with *interior mapping* (fake 3D rooms behind the glass). At night
    they light up, along with neon shop signs.
  - Street lights cast light pools, and real point lights follow the nearest lamps. Headlights light the road.
  - **Screen-space ray-traced reflections**: wet streets, puddles, window glass, car paint and water mirror
    the buildings, cars, people and lights around them.
  - **Ambient occlusion** (contact shadows under cars, in corners and doorways).
  - **Rain**: roads, lots and flat roofs fill with puddles as the ground gets wetter, dirt turns to mud, and
    raindrops send ripples across the water. An animated ocean with foam where it meets the shore.
  - Reflections and occlusion follow the quality preset and can be switched on or off in Settings.
- **Models.** Characters are one smooth-skinned body each (shoulders, elbows and knees bend instead of
  splitting), with faces (eyes, nose, lips, brows), fingers, clothing details and fabric textures. Cars have
  slatted grilles, headlight housings with projector lenses, indicators, number plates, mirrors, wipers, panel
  lines, door handles and detailed wheels with tyres, spokes and brake discs.
- **Animation.** Characters use one skinned mesh each. Walk, run and sprint gaits use leg IK with planted feet,
  including strafing and backpedalling, heel-to-toe foot roll, hip sway and leaning into turns. Characters can crouch, jump, fall, swim, sit and drive with their hands
  on the wheel. Other animations include pistol and rifle aiming (guns point at the crosshair), reloading, a
  punch combo with a kick, knife stabs, bat swings and grenade throws. Characters also flinch, cower, put their
  hands up, gesture while talking and get back up after being knocked down. A **verlet ragdoll** handles deaths
  and knockdowns, including being hit or run over by cars.
- **Driving.** Each car uses a slip-angle tire model with weight transfer and traction circles, so flooring it
  mid-corner kicks the tail out, and the **handbrake** drops rear grip so you can drift. Other features:
  - Suspension that sways as you drive, jumps and airtime, and crash physics.
  - Dents that deform the body, engine smoke, fire and **explosions** with chain reactions.
  - Tire smoke and skid marks. Smashable lamp posts, hydrants (they spray water), benches and phone booths.
  - 11 car types, including a police cruiser with a light bar and siren, a lowrider with hydraulics, a
    supercar and a box truck, plus army trucks.
  - An analogue speedometer with gear and damage readouts (airspeed, altitude and throttle in aircraft).
  - Drift and stunt-jump cash bonuses.
- **Flying and armour.** Arcade flight physics: stall and nose drop, banked turns, loops, gear, crash
  detection and afterburners. Helicopters hover on their own and flare as they land. The Raptor has a cannon
  and heat-seeking missiles that lock on to vehicles and the police helicopter. The Warhawk has a chin-turret
  minigun and rocket pods, both aimed with the camera. The tank shrugs off bullets and flattens cars, and its
  turret tracks where you look. Bail out of anything that flies and a parachute opens.
- **Combat.** Fists, knife, bat, pistol, SMG, shotgun, assault rifle, rocket launcher and grenades. Headshots,
  tracers, muzzle flashes, blood, bullet holes, scorch marks and bodies that pile up. You can punch, stab, shoot
  or run people over.
- **Living city.**
  - Pedestrians walk the sidewalks, cross at crosswalks, chat, flee gunfire, cower, fight back and shout.
  - Traffic follows lanes, obeys the same traffic-light cycle the signals display, brakes for pedestrians and
    honks at you.
  - Gangs hold their turf: the Cedar Row Kings are friendly, while the Vipers and Los Cuervos are hostile.
- **Police.** Five-star wanted levels with witnesses and line-of-sight evasion (the stars flash while the cops
  have lost you). Patrol cars route through the grid, then ram you. Cops on foot chase, shoot or arrest you
  (**BUSTED**), and a helicopter with a searchlight joins at three stars. Spray Shacks repaint your car and clear
  your wanted level.
- **Story.** 26 missions across six chapters, with cutscenes and dialogue: races, a stealth tail, chases, a
  kidnapping rescue, a heist, drive-bys, turf wars, a mansion assault, a rooftop showdown and a finale on the
  pier. After the credits, **Chapter VI: Out of Town** takes you beyond Los Soles after the desert cartel Los
  Secos: a taxi run to Mirador and Port Hale, hijacking the Sol Line gun train, a low-level flight through the
  canyons, and a gunship raid on Puerto Seco.
- **WASTED / BUSTED.** The death screen follows modern GTA: slow motion, a white flash and a black-and-white
  blur while the camera drifts away from your body. The "wasted" banner lands on the hit of the stinger. In Free
  Roam you respawn with all your weapons and cash.
- **Extras**
  - HUD in the style of the era: clock, weapon and ammo, health and armor, money, wanted stars.
  - A rotating radar with blips and a GPS route.
  - Zone and vehicle names, a Gun Barn shop, burger joint, safehouse saving, 30 hidden packages and a stats
    screen.
  - Three procedurally composed radio stations (West Coast G-funk, synthwave, slow jams).
  - Synthesized sound effects with city reverb.

## Project layout

```
index.html, css/          page shell, HUD styles, fonts
src/main.js               loading screen, title screen with live city flyover, new game / continue
src/core/                 input (keyboard, mouse, gamepad), events, math/noise utils
src/render/               sky shader, post-processing, material patching (fog/atmosphere)
src/world/                worldgen (heightfield, regions, towns), road graph + layout + meshes,
                          terrain & vegetation streaming, countryside & Fort Carver, city layout,
                          city mesh builder, shaders, props, collision world, environment
src/entities/             humanoid generator, animator (IK gait + actions), ragdoll, character,
                          car models & physics, aircraft / tank models & physics, vehicle catalogue
src/game/                 game loop, player (+ parachute), camera, vehicles, peds & gangs, lane-following
                          traffic, police, military base, combat, effects, pickups & shops,
                          audio & radio, missions engine, story, save
assets/audio/             the WASTED stinger
src/game/                 (also) railway timetable & crossings, taxis, free roam teleport
src/net/                  multiplayer: transports (claude.ai live room, WebSocket relay), remote players,
                          Online tab
src/ui/                   HUD, radar, pause menu & map
vendor/three/             Three.js r186 (MIT), bundled
server.mjs                zero-dependency static server + multiplayer relay
```

## Tips

- In Free Roam you have every weapon with unlimited cash and ammo, and you keep them when you die.
- Long way to a mission? Whistle for a taxi (H) and press Space to skip the ride.
- Want to fly without the army on your tail? Take the Skipper at Fern Creek Airfield or the Skylark on the
  hospital roof.
- Walk into the green marker at your house in Cedar Row to save.
- Yellow letter blips on the radar are story missions.
- If the stars are flashing, stay out of sight.

*Auto Theft Grand is an original fan-made parody. All names, places and characters are fictional.*
