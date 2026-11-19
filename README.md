# ITB Wildlife (work in progress)

Standalone animal-behaviour plugin for Skyrim Special Edition, part of the Instincts The Beastmaster project. No dependencies beyond the base game and its official DLC.

## Status
Plugin (`Plugin/ITB Wildlife.esp`, 1,300 records, masters: Skyrim.esm, Update.esm, Dawnguard.esm, Dragonborn.esm only):
- 540 AI packages, 76 factions, 65 form lists, 12 keywords, 7 combat styles: daily schedules (resting, wandering, grouping up, foraging, hunting, mating seasons, hibernation) and who fears or hunts whom.
- 21 vanilla animal races with new speeds, acceleration, stats and abilities (fear of fire and torches, bleeding attacks, tiredness), plus 12 spells, 5 magic effects and a swim movement type.
- 82 vanilla creature records with their confidence, aggression, factions, schedules and combat styles.
- Stagger and knockdown: every attack is capped at the vanilla value; wolves, bears, sabre cats, horkers and dogs are at 5% of vanilla with no knockdown, so a pack cannot keep you on the ground. No forced stagger or throw spells. See `Docs/stagger-caps.csv`.

Skyrim SE DLL (`DLL/`, GPL-3.0, SKSE + CommonLibSSE-NG): `ITBWildlife.dll` re-applies, at game start, what the plugin gives, even if a later mod overwrites it. Rules in `DLL/dist/SKSE/Plugins/ITBWildlife/rules.json`:
- `attack_caps`: the stagger and knockdown limits above, for 19 races.
- `ai_by_race`: deer, elk and mudcrabs flee instead of fighting.
- `package_list_by_race`: gives other mods' animals and creatures (72 races) the matching schedule list.
Log: `Documents/My Games/Skyrim Special Edition/SKSE/ITBWildlife.log`.

Not play-tested yet.

## Layout
- `Plugin/ITB Wildlife.esp`: built plugin.
- `Source/yaml/`: the plugin as Spriggit YAML (Spriggit 0.41, Skyrim SE).
- `DLL/`: DLL source (`src/`, `ng.cmake`, `LICENSE`) and the built DLL with its rules in `DLL/dist/`.
