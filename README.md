# ITB Wildlife (work in progress)

Standalone animal-behaviour plugin for Skyrim Special Edition, part of the Instincts The Beastmaster project. No dependencies beyond the base game and its official DLC.

## Status
Plugin (`Plugin/ITB Wildlife.esp`; masters: the five official files plus two Creation Club files):
- Animals, dragons, atronachs, spriggans, trolls and giants with their full behaviour: 2,288 records, 166 races, 611 creatures, 581 schedule packages, 93 factions, 87 spells, 244 skins and 1,694 placed spawns.
- Young variants for 33 more species plus the existing ones (cubs, pups, calves, kits, chicks and so on), following their mother; wolves in packs of 3 to 5 adults with pups; the player's six stable horses have names.
- Undead variants only in vanilla dungeons; giants and dragons at vanilla spawn points.
- Stagger and knockdown never exceed vanilla; wolves, bears, sabre cats, horkers and dogs are at 5% of vanilla with no knockdown.
- 883 meshes, textures and sounds under `meshes/itb`, `textures/itb` and `sound/fx/itb` (not in the repository because of size; list in `Docs/assets-list.txt`).

SKSE DLL (`DLL/`, GPL-3.0, CommonLibSSE-NG): `ITBWildlife.dll` re-applies the plugin's schedules, prey behaviour, stagger caps and survival foods at game start, even if a later mod overwrites them. It also runs the herd spawn points (256) and the fear of torches and fire for predators and prey. Rules: `DLL/dist/SKSE/Plugins/ITBWildlife/rules.json`.

Not play-tested yet. Design rules: `SPEC.md`.

## Layout
- `Plugin/ITB Wildlife.esp`: built plugin.
- `Source/yaml/`: the plugin as Spriggit YAML (Spriggit 0.41, Skyrim SE).
- `DLL/`: DLL source (`src/`, `ng.cmake`, `LICENSE`) and the built DLL with its rules in `DLL/dist/`.
