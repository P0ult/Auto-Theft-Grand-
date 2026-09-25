# Auto Theft Grand — Los Soles

An open-world crime game that runs in your browser, inspired by the San Andreas era of the genre.
It's built with Three.js and WebGL 2 and needs no build step. The city, buildings, cars, people, animations,
sounds and radio music are all generated procedurally when you load the page, so the game ships no 3D models,
textures or audio files.

You play Andre "Dre" Castillo. He comes home to the sunny, smoggy city of **Los Soles** after his little brother
Tino is gunned down. Within an hour of landing, a crooked detective has robbed him and dumped him in rival gang
territory.

## Play

Any static web server works. With Node.js installed:

```bash
npm start            # or: node server.mjs
# open http://localhost:8080
```

You can also use `npx serve .` or `python3 -m http.server 8080`. Opening `index.html` straight from disk won't
work, because browsers block ES modules on `file://`.

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

Esc or P opens the pause menu: map (right-click to set a waypoint with GPS route), mission brief, stats,
settings and controls. M opens the map directly. Standard-layout gamepads are supported.

## Features

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
  - Wet roads with puddles in rain, and an animated ocean with foam where it meets the shore.
- **Animation.** Characters use one skinned mesh each. Walk, run and sprint gaits use leg IK with planted feet,
  including strafing and backpedalling. Characters can crouch, jump, fall, swim, sit and drive with their hands
  on the wheel. Other animations include pistol and rifle aiming (guns point at the crosshair), reloading, a
  punch combo with a kick, knife stabs, bat swings and grenade throws. Characters also flinch, cower, put their
  hands up, gesture while talking and get back up after being knocked down. A **verlet ragdoll** handles deaths
  and knockdowns, including being hit or run over by cars.
- **Driving.** Each car uses a slip-angle tire model with weight transfer and traction circles, so flooring it
  mid-corner kicks the tail out, and the **handbrake** drops rear grip so you can drift. Other features:
  - Suspension that sways as you drive, jumps and airtime, and crash physics.
  - Dents that deform the body, engine smoke, fire and **explosions** with chain reactions.
  - Tire smoke and skid marks. Smashable lamp posts, hydrants (they spray water), benches and phone booths.
  - 11 vehicle types, including a police cruiser with a light bar and siren, a lowrider with hydraulics, a
    supercar and a box truck.
  - Drift and stunt-jump cash bonuses.
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
- **Story.** 22 missions across five chapters, with cutscenes and dialogue: races, a stealth tail, chases, a
  kidnapping rescue, a heist, drive-bys, turf wars, a mansion assault, a rooftop showdown and a finale on the
  pier. Credits roll at the end, then free roam continues.
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
src/world/                city layout & districts, city mesh builder, building/road shaders, props,
                          landmarks, collision world, environment (time of day, weather, lighting)
src/entities/             humanoid generator, animator (IK gait + actions), ragdoll, character,
                          vehicle models & physics, vehicle catalogue
src/game/                 game loop, player, camera, vehicles, peds & gangs, traffic, police,
                          combat, effects, pickups & shops, audio & radio, missions engine, story, save
src/ui/                   HUD, radar, pause menu & map
vendor/three/             Three.js r186 (MIT), bundled
server.mjs                zero-dependency static server
```

## Tips

- In Free Roam you start with every weapon and $5000.
- Walk into the green marker at your house in Cedar Row to save.
- Yellow letter blips on the radar are story missions.
- If the stars are flashing, stay out of sight.

*Auto Theft Grand is an original fan-made parody. All names, places and characters are fictional.*
