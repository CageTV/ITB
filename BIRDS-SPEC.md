# ITB Birds: design (clean-room)

Goal: ambient pigeons, sparrows and crows that perch on buildings, fences, rocks and trees, take off when the player comes close, fly to another perch and land. Part of ITB Wildlife, replaces the old walking pigeons.

## Clean-room rule
Nothing from any other bird mod is used: no records, names, scripts, meshes, textures or sounds. The idea (ambient birds that fly between perches) is not owned by anyone. Everything below is built from Bethesda's own files and our own work. Masters stay: the official files and Creation Club only.

## What vanilla already gives us (verified 2026-11-20)
- `Meshes\Critters\Birds\SmallBird01.nif` (SSE format): one 168-vertex skinned bird on 5 bones (Bone01 to Bone05) with 6 embedded animation sequences: airFlap, airToGrnd, grndFlap, grndLook, grndPeck, grndToAir. It names the behavior graph `Critters\SmallBird01.hkx`.
- The behavior graph `SmallBirdBehavior.hkb` has the states airFlap, airToGrnd, grndFlap, grndLook, grndPeck, grndToAir and the events takeoff, land, startAirFlap, startGrndFlap, startGrndLook, startGrndPeck.
- So wing flapping, pecking, looking around, taking off and landing already exist. What is missing is the movement between perches, and the actor records.

## Design
1. **Actors, one race and NPC per species** (pigeon, sparrow, crow, plus a young pigeon if wanted). Race: behavior graph = the vanilla critter graph, skeleton model = our own copy of the vanilla bird mesh (scaled, own texture path). Template: ITB_hummingbirdrace for the field layout.
2. **Textures**: recolours of the vanilla bird texture made by us (grey pigeon, brown sparrow, black crow). Own paths under `itb\fauna\birds`.
3. **DLL (not Papyrus)**: a Birds module in ITBWildlife.dll.
   - Perches: scan the loaded exterior cells for reference models by name (rooftops, fences, rocks, trees), find the real top surface with a downward ray cast, keep a per-cell list.
   - Flight: a small state machine per bird: perched (look / peck / flap), takeoff, fly along an arc to another perch, land. The DLL sets the position each tick and sends the graph events.
   - Population: a cap near the player, spawn out of view, remove when far, quiet at night (pigeons and sparrows roost, crows less so), species by place (pigeons in towns, sparrows on farms and in villages, crows in the wild and near carrion).
   - Reaction: take off when the player is closer than a set distance, when a predator or combat is near, or when hit.
4. **Settings**: `birds.json` next to points.json (caps, distances, per-species weights). No new MCM.
5. **Sound**: reuse vanilla bird SNDR records and vanilla wing-flap sounds by reference to vanilla forms only if no other plugin can change them; otherwise copy into our plugin (Own Your Records).
6. **Replaces** ITB walking pigeons (record, spawn points, rules). Birds are ambient: not huntable for food, no loot.

## Milestones
- M1 data and look: bird nif (3 species), races, NPCs, textures, a few placed test birds. Test: they appear, are the right size, animate (look, peck) and do not block the player.
- M2 DLL perching and flight: perch finder, state machine, one species.
- M3 all species, population rules, day and night, reactions.
- M4 replace the walking pigeons, tidy, credits (Bethesda only), release notes (new save required).

## Risks to check early
- An actor on the critter graph and a nif that contains its own geometry: the engine must accept it (to be proven in M1 in game).
- Birds have no pathing or collision: perch positions must come from ray casts, and birds must never block the player (BSBound and ObjectBounds sized to the bird).
- Cost: few birds, short ticks, only near the player.
- Saves: spawned birds are removed on cell change like the existing point spawner.
