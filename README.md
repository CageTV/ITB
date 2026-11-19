# ITB Wildlife (work in progress)

Standalone animal-behaviour plugin for Skyrim Special Edition, part of the Instincts The Beastmaster project. No dependencies beyond the base game and its official DLC.

## Status
Phase 1: 694 records (538 AI packages, 76 factions, 65 form lists, 8 keywords, 7 combat styles).
Phase 2: 21 vanilla animal races with new movement, acceleration, stats, keywords and abilities (fear of fire, bleeding attacks, tiredness), plus the records they need (9 spells, 5 magic effects, 2 keywords, 1 movement type). Every attack's stagger and knockdown is capped at the vanilla value; wolves, bears, sabre cats, horkers and dogs are at 5% of vanilla with no knockdown, so a pack cannot keep you on the ground. The forced stagger and throw attack spells are not included. Changes: `Docs/stagger-caps.csv`.

Masters: Skyrim.esm, Update.esm, Dawnguard.esm, Dragonborn.esm.

Planned: assigning packages to creatures, and Beast Harmony DLL rules that keep them applied.

## Layout
- `Plugin/ITB Wildlife.esp`: built plugin.
- `Source/yaml/`: the plugin as Spriggit YAML (Spriggit 0.41, Skyrim SE).
