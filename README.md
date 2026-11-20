# ITB Wildlife

Standalone animal-behaviour plugin for Skyrim Special Edition, part of the Instincts The Beastmaster project. No other mod is required: only the base game, its official add-ons and one Creation Club file (Survival Mode).

## Status
Release 0.1.0. The author has tested in game: the ambient birds, herds, wolf packs with pups, young variants, prey that flees, the stagger caps, fire fear, farm animals, snakes, peacocks and turkeys. Other mods that change the same animals or their spawns can conflict with it. Design rules: `SPEC.md`; birds: `BIRDS-SPEC.md`.

Plugin (`Plugin/ITB Wildlife.esp`; masters: the five official files plus one Creation Club file, Survival Mode):
- Animals, dragons, atronachs, spriggans, trolls and giants with their full behaviour: races, creatures, schedule packages, factions, spells and skins, and placed spawns. Every animal species has a smaller young variant that follows its mother; wolves hunt in packs with pups.
- Ambient birds (pigeons, doves, sparrows, robins, starlings, crows) as records plus the DLL's bird module: they perch, call and fly. Settings in `birds.json`.
- Undead variants only in vanilla dungeons; giants and dragons at vanilla spawn points.
- Stagger and knockdown never exceed vanilla; wolves, bears, sabre cats, horkers and dogs are at 5% of vanilla with no knockdown.
- About 1,300 meshes, textures and sounds under `meshes/itb`, `textures/itb` and `sound/fx/itb` (not in the repository because of size; list in `Docs/assets-list.txt`).

SKSE DLL (`DLL/`, CommonLibSSE-NG): `ITBWildlife.dll` re-applies the plugin's schedules, prey behaviour and stagger caps at game start, even if a later mod overwrites them, spawns animals at the points in `points.json`, makes animals flee from torches and fire, and runs the ambient birds. Settings: `DLL/dist/SKSE/Plugins/ITBWildlife/` (`rules.json`, `points.json`, `birds.json`). It needs SKSE64 and the Address Library for SKSE Plugins (Skyrim SE / AE). The DLL's source code is in `DLL/` and is also a separate download with each release (`ITBWildlife-<version>-source.zip`); its licence and third-party notices are beside the DLL (`ITBWildlife-LICENSE.txt`, `ITBWildlife-THIRD-PARTY.txt`).

Everything the DLL does is written to `ITBWildlife.log` in the SKSE log folder.

## Mechanics
**Spawn system** (`points.json`, DLL). 257 fixed spawn points in the worldspaces. A point spawns its creature (or a leveled list of creatures, resolved by the engine) only when the player is outdoors, closer than 4500 units and farther than 1800 units, so nothing appears in view. One spawn per point at a time; after one is gone the point waits 24, 36 or 72 game hours before the next. A point can be limited to day (06:00 to 20:00), to night, or to months of the year (snakes: months 4 to 9). The check runs every 5 seconds. Spawns are removed when the player is more than 9000 units away, or when their window closes and the player is at least 3000 units away. Spawn points stay away from towns and cities; farm animals and city birds are the exception.

**Daily routines and feeding** (schedule packages in the plugin). Each species has its own day: hunting, looking for food (raw meat, plants, insects, fish), grazing, resting, grouping up with the herd or pack, and wandering, each at set hours. Examples: red wolves hunt at 02:00 and look for meat at 03:00; deer graze at 10:00 and 17:00 and group up at 01:00 and 13:00; foxes hunt hares at 06:00 and small prey at 20:00; horkers fish at 08:00, 14:00 and 20:00. Seasonal mating packages: foxes in April, snow bears in June, black and brown bears in July, elk in November, deer in December. Fawns and calves find and follow their mother; wolf packs hunt together with pups.

**Fear of fire and light** (DLL). A held lit torch or lantern, or a fire, lightning or poison spell cast within about 1024 units, makes predators (wolves, bears, sabre cats, trolls, ice wraiths, spriggans, spiders) back off. They return to their normal behaviour once the light is gone. Each group can be switched off in the DLL's settings.

**Stagger caps** (DLL). Every attack's stagger and knockdown is capped at the vanilla value; wolves, bears, sabre cats, horkers and dogs are at 5% of vanilla with no knockdown. The DLL re-applies the caps at every game start.

**Ambient birds** (DLL, `birds.json`). The DLL scans up to 6000 units around the player for roofs, fences, rocks and tree canopies with ray casts, and every 3 seconds may spawn a bird on one (at most 10 at a time, 900 to 2600 units away, by day, never indoors). A bird hops to another perch every so often, flies off when the player comes closer than 450 units, and is retired when the player is more than 5000 units away. Each species calls at its own interval and the calls are heard in 3D up to 2000 units.

## Layout
- `Plugin/ITB Wildlife.esp`: built plugin.
- `Source/yaml/`: the plugin as Spriggit YAML (Spriggit 0.41, Skyrim SE).
- `DLL/`: DLL source (`src/`, `ng.cmake`, `LICENSE`) and the built DLL with its rules in `DLL/dist/`.

## Credits
See `CREDITS.md`.
