# Auto Theft Grand — Los Soles

An open-world crime game that runs in your browser, inspired by the San Andreas era of the genre.
It's built with Three.js and WebGL 2 and needs no build step. The world, buildings, cars, aircraft, people,
animations, sounds and radio music are all generated procedurally when you load the page. The game ships no 3D
models or textures; the only asset file is the sound clip played on the WASTED screen.

You play Andre "Dre" Castillo. He comes home to the sunny, smoggy city of **Los Soles** after his little brother
Tino is gunned down. Within an hour of landing, a crooked detective has robbed him and dumped him in rival gang
territory. Later the story heads up the coast to a second city, **San Aurelio**.

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

## Unreal Engine version

A C++ port to Unreal Engine 5.8 is under way in [`Unreal/AutoTheftGrand`](Unreal/AutoTheftGrand/README.md). It
generates the same world from the same seed. So far you can walk and drive around all of it; the rest of the
game is being ported in phases ([plan](Unreal/AutoTheftGrand/PORTING.md)).

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
| R | Reload | Right + left mouse | Drive-by (a passenger leans out of the window with any gun) |
| Q / E, wheel, 1-9 | Switch weapon | G | Hydraulics (lowriders) |
| F / Enter | Enter or steal a car (carjack drivers) | F | Exit (bail out at speed) |
| K | Whistle for your pet (stay / come; aiming at someone: set your dog on them) | Q / E, wheel | Switch drive-by weapon |
| Wheel (sniper scope) | Zoom | Shift | Nitrous (fitted at a Customs garage) |
| **Tab** (hold) | **Weapon wheel** (the game slows; point with the mouse, let go to pick) | **X** (hold) | **Cinematic camera** |
| **Caps Lock** / Z | **Special ability**: slow motion (the yellow bar under the map) | Caps Lock / Z | Special ability: slow motion with extra grip |
| **I** | **Phone** (arrows, Enter, Backspace) | I | Phone |

Motorbikes and bicycles drive like cars (W / S / A / D, Space for the handbrake on a motorbike); crash hard
into something and you're thrown off. Boats and jet skis steer the same way. On a **skateboard**, W pushes off,
Space ollies, A / D flip the board and S does a shove-it; land clean for cash. A rolled car can be rocked back
onto its wheels with A / D.

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
| J (driving a police car) | Vigilante patrol on / off |
| F by the train | Board a carriage, or climb into the cab at the front to drive it (W / S) |
| I | Phone: Contacts (cab, Benny's car delivery, Lester, Merryweather, Pegasus), Cheats, Snapmatic, Map, Weather, Stats |
| T (free roam) | Teleport menu |
| ` (backtick, free roam) | Admin console (type `help`) |
| / (multiplayer) | Chat |

Esc or P opens the pause menu: map (right-click to set a waypoint with GPS route), mission brief (in free
roam: Teleport, Vehicles and Admin instead), Online, stats, settings and controls. M opens the map directly.

### Controller

Xbox (One / Series), Logitech (F310 / F510 / F710; the X switch position is best) and PlayStation pads work,
best in Chrome or Edge. The layout follows GTA V; the Controls tab lists it with Xbox or PlayStation button
names, and on-screen hints switch to controller buttons while you play with one.

| On foot | | In a vehicle | |
|---|---|---|---|
| Left / right stick | Move / look | RT / LT | Accelerate / brake-reverse |
| RT | Fire, punch, throw (hip fire) | Left stick | Steer |
| LT (hold) | Aim (RT to shoot) | RB | Handbrake |
| LB tap / RB | Previous / next weapon | LB (hold) + RB | Drive-by |
| LB (hold) | Weapon wheel (point with the right stick) | B (hold) | Cinematic camera |
| A (hold) / X | Sprint / jump (parachute) | LS / RS click | Horn / look behind |
| B / Y | Reload / enter vehicle | View / Y | Camera / exit |
| LS click | Crouch | D-pad → / ↑ / ← | Radio / phone (hydraulics in a lowrider) / taxi job |
| LS + RS click | Special ability | LS + RS click | Special ability |
| RS click | Whistle for your pet | | |
| D-pad ↑ / → / ← / ↓ | Phone / hail a cab / ride as passenger / map | Menu | Pause |

Aircraft and the tank: RT / LT throttle (planes) or climb / descend (helicopters), RB guns, LB missiles or
rockets, B wheel brakes. In menus the D-pad or left stick moves, A selects, B backs out and LB / RB switch tabs.

## Free roam

Pick **Free Roam** on the title screen for the whole map with every weapon, **unlimited cash and ammo**, and a
**Teleport** tab (or press **T**) that jumps to any town, station, airfield, the military base, city landmarks or
your map waypoint, taking your car or aircraft along. Free roam never touches your story save.

- **Vehicles tab:** spawn any car, truck, army vehicle, tank, plane, jet or helicopter, with a paint job, and
  you're put straight in the driver's seat. Planes and helicopters can start in the air. This works online too.
- **Admin tab** (single player only): god mode, a bulletproof vehicle, never wanted, super jump, super speed,
  endless sprint, moon gravity, explosive bullets, one-hit kills, freeze time, slow motion and a street riot.
  There are also buttons to heal, get every weapon, repair or flip your vehicle, clear the area, blow up nearby
  cars, call a bodyguard, send enemies, or skydive from 400 m. You can set the wanted level, time of day,
  weather and traffic / pedestrian density too.
- **Admin console:** press **`** (backtick) and type commands: `help`, `god`, `car zenith`, `wanted 3`,
  `time 22`, `weather storm`, `tp beach`, `traffic heavy`, `gravity`, `boom`, `car razor`, `pet husky Blue`,
  `crime high`, `crime now mug` and more.

## Multiplayer

Open the **Online** tab in the pause menu (or pick **Multiplayer** on the title screen). Set a name and colour,
then join the **public world** or a **room code**: press **New room** and share the code with friends. Everyone
sees each other's characters, cars, aircraft, gunfire and explosions, can ride in each other's cars (G), chat
(press **/**) and, if player damage is on, fight.

It's one shared world: everyone near each other sees the **same pedestrians, traffic and police**, so you can
shoot, run over or carjack NPCs that another player's game is running. The **trains** run to the same
timetable for everyone, and the longest-connected player's clock and weather are shared.

- **On claude.ai:** the published version of the game uses the page's live room, so anyone with the page open
  can join.
- **Self-hosted:** `node server.mjs` includes a small WebSocket relay. Friends on your network open
  `http://<your-computer>:8080` and join the same room code. Up to 16 players per room.

## Features

- **World.** A GTA-style map of about 7 × 7 km: two cities (Los Soles on the south coast and San Aurelio in the
  north-east), nine towns (Fern Creek, Pine Hollow, the desert town of Dry Wells, lakeside Mirador, the harbour
  town of Port Hale with its pier, the desert crossroads of Puerto Seco, and up north the beach town of Gull Bay,
  Cedar Ridge on the mountain pass and the logging town of Timberline), farmland, pine-forested mountains with a
  lake, a river with bridges, and a red-rock desert with mesas. The terrain is a streamed heightfield with level of detail,
  biome shading and instanced vegetation (pines, oaks, saguaros, dead trees, boulders).
  - **Roads.** A real road graph rather than a pure grid: the elevated six-lane Sol Freeway with on/off ramps and
    diamond interchanges, winding country highways, dirt tracks, roundabouts in town and in the city, bridges
    and viaducts, and superblocks that break up the grid (a stadium, a mall, a golf club and a park).
  - **The Sol Line.** A railway from Union Station on the edge of the city, past Fern Creek to Dry Wells, with
    level crossings where traffic waits for the train, bridges and underpasses at the highways, and three
    stations. A passenger train and a long freight train (box cars, tankers, hoppers and containers) run the
    line all day and pass each other on the loop at Fern Creek. Ride the passenger train, or take either cab and
    drive.
  - **Taxis.** Whistle for a cab, ride with the meter running and skip the trip, or drive a cab yourself and
    pick up fares for cash (with tips for speed and a bonus every fifth fare in a row).
  - **Fort Carver.** A walled military base in the desert with a runway, hangars, a control tower, barracks,
    helipads and a tank yard. You can steal the **Raptor** fighter jet, the **Hercules** cargo plane, the
    **Warhawk** attack helicopter, the **Mammoth** tank and army trucks. It's a restricted zone: after a
    warning, soldiers open fire and the police send a three-star response. The **Skipper** light plane waits at
    Fern Creek Airfield, and a **Skylark** helicopter sits on the hospital roof in the city.
- **San Aurelio.** A bay city on a coastal plain in the north-east, and nothing like a grid: three wobbly ring
  roads circle the Plaza de Aurelio and its fountain, crossed by seven curving avenues and side streets, so every
  block is a different lopsided, curved shape. Glass and art deco towers crowd Centro; brick and mid-rise fill
  Harborside, Mission, Cathedral Hill and Northgate; low stucco shops and houses make up Bayview and Aurelio
  Heights. Four-lane avenues with zebra crossings and raised, kerbed pavements (you walk on them, cars bump up
  onto them), roundabouts, back-lot courtyards, car parks and gardens, the Cathedral of San Aurelio, Bayview
  Park, the Aurelio Arena, and Harbor Drive along a palm-lined beach with a marina. The Aurelio Highway runs up
  the coast from Bayshore Road, Ridge Road climbs Cedar Valley, and Timber Road heads up to Timberline. The
  Harbor Saints run the waterfront.
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
  - Street lights cast wide warm pools of light, with glowing halos and soft light cones (stronger in the
    rain). Real point lights follow the nearest lamps. Headlights light the road.
  - **Screen-space ray-traced reflections**: wet streets, puddles, window glass, car paint and water mirror
    the buildings, cars, people and lights around them.
  - **Ambient occlusion** (contact shadows under cars, in corners and doorways).
  - **Rain**: roads, lots and flat roofs fill with puddles as the ground gets wetter, dirt turns to mud, and
    raindrops send ripples across the water. An animated ocean with foam where it meets the shore.
  - Reflections and occlusion follow the quality preset and can be switched on or off in Settings.
- **Models.** Characters are one smooth-skinned body each (shoulders, elbows and knees bend instead of
  splitting), with faces (eyes, nose, lips, brows), fingers, clothing details and fabric textures. Cars have
  slatted grilles, headlight housings with projector lenses, indicators, number plates, mirrors, wipers, panel
  lines, door handles and detailed wheels with tyres, spokes and brake discs. New on the streets: the Buffalo S
  sports saloon, the Baller luxury SUV and the Tempest supercar; and for the law, the Sheriff SUV, the SWAT
  Enforcer van and the Stockade cash van, whose twin back doors swing open on a hold full of cash bags.
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
  - 12 car types, remodelled with lofted bodies: proper roofs and pillars, cabins with headliners, curved
    glass, fender flares, per-model grilles, lamps, bumpers and rims (wire, mesh, split-spoke, steel, alloy).
    Includes a police cruiser with a light bar and siren, a lowrider with hydraulics, a supercar, a pickup, a
    van, a box truck and a city hatchback, plus army trucks.
  - **Crash physics:** hard hits and high-speed swerves roll cars over as a rigid body (they tumble, slide on
    their roofs, bounce and settle). Doors, hoods and bumpers dent and fly open; glass shatters. A car left on its
    roof catches fire.
  - **Explosions scale with what blew up:** a scooter pops, a truck booms, and a plane or the Hercules goes up
    in a huge fireball with flying wreckage, a smoke column and follow-up blasts.
  - **Motorbikes and bicycles:** the Razor 600 sport bike and the Trailblazer dirt bike (happy off-road), a
    BMX and a road bike. Bikes lean into corners, riders sit astride with their hands on the grips and pedal
    (the cranks turn), and a hard crash throws everyone off. They turn up in traffic and parked at the kerb.
  - **Drive-bys:** a passenger who aims climbs half out of the window onto the door frame, turns towards the
    target and shoots with any gun; the driver uses a pistol or SMG. On a bike you twist at the waist.
  - An analogue speedometer with gear and damage readouts (airspeed, altitude and throttle in aircraft).
  - Drift and stunt-jump cash bonuses.
  - **Skateboards** and a skatepark in Santa Luz: push, ollie, kickflips and shove-its, grind the ramps, locals
    skating the bowl.
  - **Boats:** dinghies, speedboats, jet skis and cruisers, moored at the marinas (Santa Luz, Port Morena, Port
    Hale, Lake Mirador and Aurelio Marina) or cruising the coast, with planing hulls, waves, wakes and spray.
    Wanted out on the water, **police boats** come after you: they ram, and the bow gunner opens up.
  - **Customs garages** (Los Soles, Corona and Aurelio Customs): three engine stages, three armour levels,
    nitrous (Shift), any paint colour, or a respray that loses the cops. Upgrades stay with that car.
- **Flying and armour.** Arcade flight physics: stall and nose drop, banked turns, loops, gear, crash
  detection and afterburners. Helicopters hover on their own and flare as they land. The Raptor has a cannon
  and heat-seeking missiles that lock on to vehicles and the police helicopter. The Warhawk has a chin-turret
  minigun and rocket pods, both aimed with the camera. The tank shrugs off bullets and flattens cars, and its
  turret tracks where you look. Bail out of anything that flies and a parachute opens.
- **Combat.** Fists, knife, bat, pistol, SMG, shotgun, assault rifle, **sniper rifle** (a zoom scope,
  one-shot headshots), **minigun** (spins up, slows you down), rocket launcher, grenades and **molotovs** (a
  pool of fire that sets people running and cooks cars). Headshots,
  tracers, muzzle flashes, blood, bullet holes, scorch marks and bodies that pile up. You can punch, stab, shoot
  or run people over.
- **Walk-in shops.** The **Gun Barn**, **Big Bun Burgers** and **Ray's Liquor** are real interiors: gun racks and
  display cases, a diner with a kitchen line and menu board, aisles of shelves and drinks fridges. The clerk behind
  the counter serves you when you step up to the marker. Point a gun at them and the burger bar or liquor store
  empties the till (and calls the cops), while the Gun Barn's owner reaches for his shotgun. Kill the clerk and
  the shop stays shut until you've been gone a while. Five more street shops open onto the pavement: **Pet
  Palace** (kennels of dogs and cats, an aquarium wall), two **24/7** stores, **The Rusty Anchor** bar (a
  backlit bottle wall, pool table and booths; drinks make the room sway) and **Bean Scene** coffee shop.
- **Wildlife and pets.** Pigeons on the pavements and plazas, gulls on the beach and the docks, cats in the back
  streets and the odd stray dog; deer, rabbits and crows in the country and the forest, coyotes in the desert
  and cows on the farmland. They graze, peck and wander, and bolt or take off when you get close, shoot or
  drive at them (cars, bullets and blasts kill them). People walk their dogs. At **Pet Palace** you can adopt a
  Labrador, German Shepherd, Husky, Rottweiler, Pug, Poodle, Tabby, Black Cat or Siamese: your pet follows you,
  sits when you stop, rides in the passenger seat, goes for anyone who hurts you, attacks whoever you aim at
  when you whistle (K), and is saved with the story. Other players see it too.
- **Living city.**
  - Pedestrians walk the sidewalks, cross at crosswalks, chat, flee gunfire, cower, fight back and shout.
  - Traffic follows lanes, obeys the same traffic-light cycle the signals display, brakes for pedestrians,
    honks at you and steers around wrecks and stalled cars.
  - Gangs hold their turf: the Cedar Row Kings are friendly, while the Vipers, Los Cuervos and San Aurelio's
    Harbor Saints are hostile.
- **GTA V-style HUD.** A rectangular minimap with health, armour and special-ability bars under it; it flashes
  red and blue while the police can see you, and while they're searching it shows their vision cones (blue
  cones for cars and officers on foot, a circle under the helicopter) so you can slip between them. Stars and
  cash sit top right, the weapon with its ammo under them, the district and vehicle names bottom right. The
  older round radar is still there: Settings → HUD style → Classic.
- **Weapon wheel.** Hold Tab (or LB) and time slows right down while every weapon you carry fans out in a ring;
  point at one and let go. In a car it offers what you can shoot from a seat.
- **Special ability.** The yellow bar fills as you drive fast, drift, get air and land headshots. Caps Lock (or
  both sticks) slows the world down: bullet time on foot, and in a car a slowed world with extra grip to thread
  the traffic. Press again to stop early. (On a Mac, use Z: Caps Lock only sends every other press there.)
- **Phone.** I (or D-pad up) takes out the iFruit. **Contacts:** Downtown Cab Co. sends a cab; Benny's
  Motorworks delivers your last car (or a Kestrel) to the kerb nearby, with a blip; Lester makes the police lose
  your file, for $500 a star; Merryweather Security sends three armed contractors in black who follow you for
  ten minutes; Pegasus drops a Skylark helicopter off (free roam). **Cheats:** PAINKILLER, TURTLE, TOOLUP,
  LAWYERUP, FUGITIVE, SKYFALL, BUZZOFF, COMET, OFFROAD, HOPTOIT, CATCHME, HIGHEX, POWERUP, SLOWMO, MAKEITRAIN
  (in story mode the timed ones last five minutes; cheats are off online). **Snapmatic** hides the HUD for
  photos; also Map, Weather and Stats.
- **Police.** Five-star wanted levels with witnesses and line-of-sight evasion (the stars flash while the cops
  have lost you). Patrol cars route through the grid, then ram you. Cops on foot chase, shoot or arrest you
  (**BUSTED**), and a helicopter with a searchlight joins at three stars. Spray Shacks repaint your car and clear
  your wanted level.
- **The army at five stars.** Hit five stars and the National Guard joins in: Barracks trucks and Ranger jeeps
  run you down and unload squads of soldiers, a Warhawk gunship circles overhead strafing you with its chin gun
  and rocket pods, and a Mammoth tank hunts you down and shells you. They pull out when the stars drop.
- **Roadblocks.** From three stars, while you drive, the police close the road ahead of you with a line of
  cruisers and officers firing from behind them (Sheriff SUVs out in the country). At four stars the SWAT
  Enforcer joins the line and a spike strip goes down in front of it: run over it and your tyres burst, so you
  limp on at reduced speed and grip, with sparks, until a Customs garage fixes them.
- **Armoured vans.** Every few minutes a Stockade cash-in-transit van does its rounds nearby (a green $ on the
  radar). It's bullet resistant and the two guards are armed. Shoot the back doors (or blow them) and the cash
  bags spill out; the guards bail out and fight, the van may make a run for it, and the alarm brings two stars.
- **Cinematic camera.** Hold X (or B) while driving and the camera cuts between a roadside camera you blast
  past, a low tracking shot, a high chase and a head-on shot, with letterbox bars.
- **Street crime.** The people of Los Soles break the law too:
  - Pedestrians jaywalk across the middle of the block, and drivers honk at them.
  - Some drivers speed and run red lights.
  - Bumps between cars end in a shouting match, a fist fight or a hit-and-run.
  - Muggers and car thieves work the streets, more of them at night and in the rougher districts.
  - A crime a patrol sees, or that somebody calls in, puts **wanted stars on the NPC**. They show over their
    head and as an orange blip on the radar, and a **DISPATCH** line tells you about crimes nearby.
  - The nearest patrol responds. Small stuff gets a ticket: a jaywalker gets a talking-to, a speeder is pulled
    over. For the rest suspects surrender, run or (rarely, if they're holding a gun) shoot it out. Cars get
    chased and rammed, runners get tackled, and arrested suspects ride off in the back of the cruiser.
  - Your own wanted level always comes first: while you're wanted every unit is after you, and the suspects
    get a head start.
  - **Settings → Street crime** sets it to Off, Normal or High.
- **The MV Pacific Star.** The container ship at Port Morena can be raided: walk up the gangway and security
  tells you to get off. Draw a gun, go near the bridge or hang around and the crew (hi-vis deckhands, masked
  security in plate carriers, the captain) turn on you, and the captain radios the coast guard. Crack the
  captain's safe on the bridge for the payroll and break open the contraband containers for cash, weapons and
  armour. The ship restocks after a while.
- **Side activities.**
  - **Street races:** Sol Sprint, Bayshore Run, Aurelio Ring and the Cedar Valley Climb. Pay the entry, race three
    locals through the checkpoints, win the pot. Best times are kept.
  - **Vigilante:** in a police car press J; chase down carloads of suspects against the clock, level after level.
  - **Property:** eight safehouses and businesses to buy (Hotel Aurelio, the Arena Skybox, the Clam Shack, a beach
    house...). Safehouses save and heal you; businesses earn money into a till you collect.
- **Story.** 35 missions across eight chapters, with cutscenes and dialogue: races, a stealth tail, chases, a
  kidnapping rescue, a heist, drive-bys, turf wars, a mansion assault, a rooftop showdown and a finale on the
  pier. After the credits, **Chapter VI: Out of Town** takes you beyond Los Soles after the desert cartel Los
  Secos: a taxi run to Mirador and Port Hale, hijacking the Sol Line gun train, a low-level flight through the
  canyons, and a gunship raid on Puerto Seco. **Chapters VII and VIII** move up the coast to San Aurelio and
  the Harbor Saints: a long drive north with an ambush at Gull Bay, skateboard courier runs against the clock
  (tricks buy time), a night-time speedboat interception with the coast guard on your tail, defending the
  cathedral, the arena box office heist, a full raid on the Pacific Star with a boat run up the coast, stopping
  log trucks on the Timber Road, a sniper ambush in Cedar Valley and a boat chase finale.
- **WASTED / BUSTED.** The death screen follows modern GTA: slow motion, a white flash and a black-and-white
  blur while the camera drifts away from your body. The "wasted" banner lands on the hit of the stinger. In Free
  Roam you respawn with all your weapons and cash.
- **Extras**
  - HUD in the style of the era: clock, weapon and ammo, health and armor, money, wanted stars.
  - A rotating radar with blips and a GPS route.
  - Zone and vehicle names, safehouse saving, 30 hidden packages and a stats screen.
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
src/game/                 (also) railway timetable, signalling & crossings, taxis, free roam teleport,
                          admin tools & vehicle spawner, shopkeepers
src/world/interiors.js    walk-in shop interiors (layout, colliders, meshes)
src/entities/bikes.js     motorbikes and bicycles (models, lean, rider pose, crashes)
src/entities/animals.js   animal models and procedural rig (dogs, cats, deer, rabbits, coyotes, cows, birds)
src/game/npccrime.js      street crime: jaywalkers, speeders, road rage, muggers and car thieves, NPC wanted
                          stars and the police response (tickets, pull-overs, chases, arrests)
src/game/wildlife.js      wildlife spawning and behaviour; src/game/pets.js: pets
src/world/northcity.js    San Aurelio: ring-and-avenue street plan, pavements, buildings, walk graph
src/world/cargoship.js    the MV Pacific Star (hull, decks, containers, superstructure); src/game/shipraid.js
src/entities/loft.js      lofted geometry for car bodies and boat hulls; boat.js, skateboard.js
src/game/boats.js         marinas, cruising boats and police boats; skatepark.js (world), story_north.js
src/game/modshop.js       Customs garages; races.js, vigilante.js, properties.js
src/net/                  multiplayer: transports (claude.ai live room, WebSocket relay), remote players,
                          shared NPCs / traffic / trains, Online tab
src/game/army.js          five-star army response; roadblocks.js (roadblocks & spike strips), heists.js
                          (armoured vans), special.js (special ability)
src/ui/                   HUD, radar, pause menu & map, controller menu navigation, weaponwheel.js, phone.js
vendor/three/             Three.js r186 (MIT), bundled
server.mjs                zero-dependency static server + multiplayer relay
tools/bundle.mjs          bundles the game into one page + game.js (npm run bundle -> dist/)
tools/browser-test/       headless Chromium test runner and test scripts (see CLAUDE.md)
CLAUDE.md                 working notes: how the game is wired, testing, the Unreal port's status
Unreal/AutoTheftGrand/    the Unreal Engine 5 port (C++)
```

## Tips

- In Free Roam you have every weapon with unlimited cash and ammo, and you keep them when you die. Press ` for
  the admin console.
- Short of cash in the story? Rob Ray's Liquor — but have a getaway car ready.
- Long way to a mission? Whistle for a taxi (H) and press Space to skip the ride.
- Want to fly without the army on your tail? Take the Skipper at Fern Creek Airfield or the Skylark on the
  hospital roof.
- Walk into the green marker at your house in Cedar Row to save, or buy a safehouse.
- Fit nitrous at a Customs garage before a street race.
- The Aurelio Highway up the coast is the quickest way to San Aurelio; a speedboat is faster still.
- Yellow letter blips on the radar are story missions.
- If the stars are flashing, stay out of sight.

*Auto Theft Grand is an original fan-made parody. All names, places and characters are fictional.*
