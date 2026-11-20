/*
 * ITB Wildlife
 * Copyright (c) 2026 CageTV
 *
 * Licensed under the GNU General Public License, version 3 or later; see LICENSE.
 */
#include "PCH.h"
#include "Birds.h"

#include <array>
#include <optional>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

// Ambient birds. Own design, built on Bethesda's critter behavior graph (Critters\SmallBird01.hkx), whose events are
//   takeoff, land, startAirFlap, startGrndFlap, startGrndLook, startGrndPeck.
// The DLL finds perches in the loaded exterior cells, spawns a few bird actors on them near the player, lets them look,
// peck and flap, and flies them along an arc to another perch. No Papyrus. Everything is logged so a test run can be read.
namespace Birds
{
	namespace
	{
		enum class Kind { kRoof, kFence, kRock, kTree };
		enum class State { kPerched, kFlying };

		struct Species
		{
			std::string       id;
			std::string       plugin;
			std::uint32_t     form{ 0 };
			int               weight{ 1 };
			std::vector<Kind> perches;
			bool              dayOnly{ true };
			std::uint32_t     call{ 0 };            // sound descriptor of this species' call (local FormID in `plugin`), 0 = silent
			float             callMin{ 15.0f };     // seconds between two calls of one perched bird
			float             callMax{ 45.0f };
			RE::BGSSoundDescriptorForm* callForm{ nullptr };
			bool              callResolved{ false };
		};

		struct Config
		{
			bool  enabled{ true };
			float tickSeconds{ 0.033f };
			int   maxBirds{ 10 };
			float scanRadius{ 6000.0f };
			float perchRefreshSeconds{ 12.0f };
			float spawnMinDist{ 1400.0f };
			float spawnMaxDist{ 4200.0f };
			float despawnDist{ 7500.0f };
			float scareDist{ 450.0f };
			float flightSpeed{ 420.0f };
			float hopMinDist{ 700.0f };
			float hopMaxDist{ 2600.0f };
			float spawnEverySeconds{ 3.0f };
			float dayStart{ 6.0f };
			float dayEnd{ 20.0f };
			int   maxRaycastsPerScan{ 40 };
			bool  sounds{ true };           // bird calls and the wing flutter
			float soundDist{ 2000.0f };     // nothing is played when the player is farther than this (the sound's own model carries to about 2000)
			float soundVolume{ 1.0f };
			float minCallGap{ 1.5f };       // seconds between any two calls, so a flock does not become a wall of sound
			std::uint32_t flap{ 0 };        // sound descriptor of the wing flutter at take-off
			RE::BGSSoundDescriptorForm* flapForm{ nullptr };
			bool  flapResolved{ false };
			std::vector<Species> species;
		};

		struct Perch
		{
			RE::NiPoint3 pos;
			Kind         kind;
		};

		struct Bird
		{
			RE::ObjectRefHandle ref;
			int                 species{ 0 };
			State               state{ State::kPerched };
			RE::NiPoint3        pos;
			RE::NiPoint3        from;
			RE::NiPoint3        to;
			float               t{ 0.0f };      // 0..1 along the flight
			float               dur{ 1.0f };    // seconds
			float               arc{ 60.0f };
			float               timer{ 5.0f };  // seconds until the next hop
			float               idleTimer{ 3.0f };
			bool                leaving{ false };  // flies off and is removed on arrival
			bool                landSent{ false };
			bool                flapSent{ false };
			bool                started{ false };  // graph ready, first look sent, AI off
			bool                landAccepted{ false };
			int                 landPhase{ 0 };    // 0 none, 1 trying to land, 2 waiting for the landing clip, then back to looking around
			float               landClock{ 0.0f };
			float               landRetry{ 0.0f };
			bool                grounded{ false };  // sits on the ground: hops around instead of standing still
			bool                hopping{ false };
			float               hopT{ 0.0f }, hopDur{ 0.35f }, hopTimer{ 2.0f };
			RE::NiPoint3        hopFrom, hopTo, home;
			bool                logged{ false };  // the "player came near" diagnostic was written
			bool                motionSet{ false };  // keyframed (no gravity), like the reference mod does for its birds
			float               holdTimer{ 0.0f };
			float               callTimer{ 8.0f };   // seconds until this bird calls
			float               elapsed{ 0.0f };     // seconds since the flight began
			float               t1{ 0.0f };          // seconds until the second leg starts
			RE::NiPoint3        apex;                // end of the first leg: halfway and higher than both ends
			bool                seg2Sent{ false };
			bool                manual{ false };     // this flight is moved by SetPosition (the engine spline did not move the bird)
			bool                movedChecked{ false };
		};

		Config                           g_cfg;
		std::atomic<bool>                g_run{ false };
		std::mt19937                     g_rng{ std::random_device{}() };
		std::vector<Perch>               g_perches;
		std::vector<Bird>                g_birds;
		struct Pending
		{
			RE::ObjectRefHandle h;
			float               ttl{ 4.0f };
		};
		std::vector<Pending>             g_pendingDelete;
		int                              g_diagLines{ 0 };
		int                              g_rejectLogged{ 0 };
		int                              g_landLogs{ 0 };
		RE::FormID                       g_lastCell{ 0 };
		float                            g_sinceScan{ 1000.0f };
		float                            g_sinceSpawn{ 0.0f };
		float                            g_sinceLoad{ 0.0f };  // seconds since the game was loaded: the bird module waits for the world to settle
		int                              g_eventFails{ 0 };
		bool                             g_manualFlight{ true };  // flights are moved by SetPosition (the Papyrus spline needs a script object on the bird, which these actors do not have)
		int                              g_flightLogs{ 0 };
		float                            g_sinceCall{ 100.0f };  // seconds since any bird was heard
		int                              g_soundLogs{ 0 };

		float Rand(float a_lo, float a_hi) { return std::uniform_real_distribution<float>(a_lo, a_hi)(g_rng); }
		int   RandInt(int a_lo, int a_hi) { return std::uniform_int_distribution<int>(a_lo, a_hi)(g_rng); }
		float Dist2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		bool Contains(const std::string& a_s, const char* a_part) { return a_s.find(a_part) != std::string::npos; }

		std::optional<Kind> ParseKind(const std::string& a_s)
		{
			if (a_s == "roof") return Kind::kRoof;
			if (a_s == "fence") return Kind::kFence;
			if (a_s == "rock") return Kind::kRock;
			if (a_s == "tree") return Kind::kTree;
			return std::nullopt;
		}

		const char* KindName(Kind a_k)
		{
			switch (a_k) {
			case Kind::kRoof: return "roof";
			case Kind::kFence: return "fence";
			case Kind::kRock: return "rock";
			default: return "tree";
			}
		}

		std::filesystem::path ConfigPath() { return std::filesystem::path("Data/SKSE/Plugins/ITBWildlife/birds.json"); }

		void Load()
		{
			g_cfg = Config{};
			std::ifstream in(ConfigPath());
			if (!in) {
				SKSE::log::warn("birds: no {} (birds idle)", ConfigPath().string());
				g_cfg.enabled = false;
				return;
			}
			const auto j = nlohmann::json::parse(in, nullptr, false, true);
			if (j.is_discarded()) {
				SKSE::log::error("birds: {} is not valid JSON (birds idle)", ConfigPath().string());
				g_cfg.enabled = false;
				return;
			}
			g_cfg.enabled = j.value("enabled", true);
			g_cfg.tickSeconds = std::max(0.016f, j.value("tickSeconds", 0.033f));
			g_cfg.maxBirds = std::clamp(j.value("maxBirds", 10), 0, 40);
			g_cfg.scanRadius = j.value("scanRadius", 6000.0f);
			g_cfg.perchRefreshSeconds = j.value("perchRefreshSeconds", 12.0f);
			g_cfg.spawnMinDist = j.value("spawnMinDist", 1400.0f);
			g_cfg.spawnMaxDist = j.value("spawnMaxDist", 4200.0f);
			g_cfg.despawnDist = j.value("despawnDist", 7500.0f);
			g_cfg.scareDist = j.value("scareDist", 450.0f);
			g_cfg.flightSpeed = std::max(120.0f, j.value("flightSpeed", 420.0f));
			g_cfg.hopMinDist = j.value("hopMinDist", 700.0f);
			g_cfg.hopMaxDist = j.value("hopMaxDist", 2600.0f);
			g_cfg.spawnEverySeconds = std::max(0.5f, j.value("spawnEverySeconds", 3.0f));
			g_cfg.dayStart = j.value("dayStartHour", 6.0f);
			g_cfg.dayEnd = j.value("dayEndHour", 20.0f);
			g_cfg.maxRaycastsPerScan = std::clamp(j.value("maxRaycastsPerScan", 40), 5, 200);
			if (j.contains("sounds") && j["sounds"].is_object()) {
				const auto& so = j["sounds"];
				g_cfg.sounds = so.value("enabled", true);
				g_cfg.soundDist = std::clamp(so.value("maxDistance", 2000.0f), 300.0f, 6000.0f);
				g_cfg.soundVolume = std::clamp(so.value("volume", 1.0f), 0.0f, 2.0f);
				g_cfg.minCallGap = std::clamp(so.value("minGapSeconds", 1.5f), 0.0f, 30.0f);
				g_cfg.flap = static_cast<std::uint32_t>(std::stoul(so.value("flutter", std::string("0")), nullptr, 16));
			}
			for (const auto& s : j.value("species", nlohmann::json::array())) {
				Species sp;
				sp.id = s.value("id", std::string("bird"));
				sp.plugin = s.value("plugin", std::string("ITB Wildlife.esp"));
				sp.form = static_cast<std::uint32_t>(std::stoul(s.value("form", std::string("0")), nullptr, 16));
				sp.weight = std::max(1, s.value("weight", 1));
				sp.dayOnly = s.value("dayOnly", true);
				sp.call = static_cast<std::uint32_t>(std::stoul(s.value("call", std::string("0")), nullptr, 16));
				sp.callMin = std::clamp(s.value("callMin", 15.0f), 2.0f, 300.0f);
				sp.callMax = std::max(sp.callMin + 1.0f, s.value("callMax", 45.0f));
				for (const auto& k : s.value("perch", nlohmann::json::array())) {
					if (auto kind = ParseKind(k.get<std::string>())) {
						sp.perches.push_back(*kind);
					}
				}
				if (sp.form != 0 && !sp.perches.empty()) {
					g_cfg.species.push_back(std::move(sp));
				}
			}
			SKSE::log::info("birds: config: {} species, max {} birds, speed {}, scan {} (enabled {})", g_cfg.species.size(), g_cfg.maxBirds,
				g_cfg.flightSpeed, g_cfg.scanRadius, g_cfg.enabled);
		}

		// ---------------------------------------------------------------- ray casts and perches

		// One ray straight down through the physics world; true and the hit point when it hits something.
		bool CastDown(RE::bhkWorld* a_world, const RE::NiPoint3& a_from, float a_depth, RE::NiPoint3& a_hit)
		{
			if (!a_world) {
				return false;
			}
			const float        scale = RE::bhkWorld::GetWorldScale();
			const RE::NiPoint3 to{ a_from.x, a_from.y, a_from.z - a_depth };
			RE::bhkPickData    pick;
			pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
			pick.rayInput.enableShapeCollectionFilter = false;
			pick.rayInput.filterInfo.filter = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			a_world->PickObject(pick);
			if (pick.pickFailed || !pick.rayOutput.HasHit()) {
				return false;
			}
			const float f = pick.rayOutput.hitFraction;
			a_hit = { a_from.x, a_from.y, a_from.z - a_depth * f };
			return true;
		}

		RE::NiPoint3 RotateZ(float a_x, float a_y, float a_angle)
		{
			const float c = std::cos(a_angle), s = std::sin(a_angle);
			return { a_x * c + a_y * s, -a_x * s + a_y * c, 0.0f };  // Skyrim yaw is clockwise
		}

		std::optional<Kind> Classify(RE::TESBoundObject* a_base, const std::string& a_model, const RE::NiPoint3& a_dims)
		{
			const auto type = a_base->GetFormType();
			if (type == RE::FormType::Tree) {
				return Kind::kTree;
			}
			if (type != RE::FormType::Static && type != RE::FormType::MovableStatic) {
				return std::nullopt;
			}
			if ((Contains(a_model, "fence") || Contains(a_model, "palisade") || Contains(a_model, "stonewall") ||
					(Contains(a_model, "wall") && (Contains(a_model, "farm") || Contains(a_model, "exterior")))) && a_dims.z < 400.0f && a_dims.z > 25.0f) {
				return Kind::kFence;
			}
			if ((Contains(a_model, "rock") || Contains(a_model, "boulder")) && !Contains(a_model, "rockwall") && a_dims.z > 40.0f && a_dims.z < 600.0f) return Kind::kRock;
			const bool outsideOnly = !Contains(a_model, "dungeon") && !Contains(a_model, "interior") && !Contains(a_model, "cave") && !Contains(a_model, "tomb") && !Contains(a_model, "crypt");
			if (outsideOnly && (Contains(a_model, "roof") || Contains(a_model, "thatch")) && a_dims.x >= 150.0f && a_dims.y >= 150.0f) return Kind::kRoof;
			if (Contains(a_model, "architecture") && !Contains(a_model, "dungeon") && !Contains(a_model, "interior") && !Contains(a_model, "cave") &&
				!Contains(a_model, "mine") && !Contains(a_model, "ruin") && !Contains(a_model, "fort") && !Contains(a_model, "tomb") && !Contains(a_model, "crypt") &&
				a_dims.z >= 180.0f && a_dims.x >= 200.0f && a_dims.y >= 200.0f) {
				return Kind::kRoof;
			}
			return std::nullopt;
		}

		struct Cand
		{
			RE::NiPoint3 pos;
			Kind         kind;
			RE::NiPoint3 dims;
			float        yaw;
			RE::NiPoint3 center;  // centre of the bounds in x and y, top of the bounds in z (object space, scaled)
		};

		void RefreshPerches(RE::PlayerCharacter* a_player)
		{
			g_perches.clear();
			auto* cell = a_player->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			auto* tes = RE::TES::GetSingleton();
			if (!tes || !world) {
				return;
			}
			std::vector<Cand> cands;
			std::set<std::string> rejected;
			int               seen = 0, perchable = 0;
			tes->ForEachReferenceInRange(a_player, g_cfg.scanRadius, [&](auto&& a_ref) {
				auto* ref = compat::Ptr(a_ref);
				++seen;
				if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* base = ref->GetBaseObject();
				auto* model = base ? base->template As<RE::TESModel>() : nullptr;
				if (!base || !model || !model->GetModel()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float  scale = ref->GetScale();
				const auto&  bd = base->boundData;
				const RE::NiPoint3 dims{ std::abs(static_cast<float>(bd.boundMax.x - bd.boundMin.x)) * scale, std::abs(static_cast<float>(bd.boundMax.y - bd.boundMin.y)) * scale,
					std::abs(static_cast<float>(bd.boundMax.z - bd.boundMin.z)) * scale };
				const std::string lowModel = Lower(model->GetModel());
				const auto kind = Classify(base, lowModel, dims);
				if (!kind) {
					if (g_rejectLogged < 3 && dims.z >= 120.0f && dims.x >= 150.0f && Contains(lowModel, "architecture") && rejected.size() < 30) {
						rejected.insert(lowModel + " " + std::to_string(static_cast<int>(dims.x)) + "x" + std::to_string(static_cast<int>(dims.y)) + "x" + std::to_string(static_cast<int>(dims.z)));
					}
					return RE::BSContainer::ForEachResult::kContinue;
				}
				RE::NiPoint3 ctr{ (bd.boundMax.x + bd.boundMin.x) * 0.5f * scale, (bd.boundMax.y + bd.boundMin.y) * 0.5f * scale, static_cast<float>(bd.boundMax.z) * scale };
				if (*kind == Kind::kTree) {
					// the bounds of a tree base record are tiny (every "tree" perch was at the foot of the trunk): use the loaded 3D, whose bound covers the whole tree
					auto* tree3d = ref->Get3D();
					if (!tree3d) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					const float top = tree3d->worldBound.center.z + tree3d->worldBound.radius * 0.8f - ref->GetPosition().z;
					if (top < 260.0f) {
						return RE::BSContainer::ForEachResult::kContinue;  // a bush or a sapling
					}
					ctr.z = top;
				}
				++perchable;
				cands.push_back({ ref->GetPosition(), *kind, dims, ref->GetAngleZ(), ctr });
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::shuffle(cands.begin(), cands.end(), g_rng);
			int                casts = 0;
			std::array<int, 4> found{ 0, 0, 0, 0 };
			for (const auto& c : cands) {
				if (g_perches.size() >= 300) {
					break;
				}
				if (c.kind == Kind::kTree) {
					const float        a = Rand(0.0f, 6.2831f), r = Rand(25.0f, 70.0f);
					const RE::NiPoint3 p{ c.pos.x + std::cos(a) * r, c.pos.y + std::sin(a) * r, c.pos.z + c.center.z * Rand(0.55f, 0.8f) };
					g_perches.push_back({ p, c.kind });
					++found[3];
					continue;
				}
				if (casts >= g_cfg.maxRaycastsPerScan) {
					continue;
				}
				// a point over the object: the centre of its bounds, spread over the middle part of large objects
				const float spreadX = c.kind == Kind::kRoof ? c.dims.x * 0.25f : c.dims.x * 0.15f;
				const float spreadY = c.kind == Kind::kRoof ? c.dims.y * 0.25f : c.dims.y * 0.15f;
				const auto  off = RotateZ(c.center.x + Rand(-spreadX, spreadX), c.center.y + Rand(-spreadY, spreadY), c.yaw);
				const RE::NiPoint3 from{ c.pos.x + off.x, c.pos.y + off.y, c.pos.z + c.center.z + 150.0f };
				RE::NiPoint3       hit;
				++casts;
				if (!CastDown(world, from, c.dims.z + 400.0f, hit)) {
					continue;
				}
				// a roof or a rock must be hit near the top of the object, not on the ground beside it or on a wall
				if (hit.z < c.pos.z + c.center.z * 0.55f) {
					continue;
				}
				g_perches.push_back({ { hit.x, hit.y, hit.z + 2.0f }, c.kind });
				++found[static_cast<std::size_t>(c.kind)];
			}
			if (!rejected.empty()) {
				++g_rejectLogged;
				std::string list;
				for (const auto& r : rejected) list += std::string("\n    ") + r;
				SKSE::log::info("birds: large building models NOT used as perches:{}", list);
			}
			SKSE::log::info("birds: perch scan: {} refs seen, {} perchable, {} ray casts -> {} perches (roof {}, fence {}, rock {}, tree {})", seen, perchable, casts,
				g_perches.size(), found[0], found[1], found[2], found[3]);
		}

		// ---------------------------------------------------------------- birds

		void Retire(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return;
			}
			a_ref->Disable();
			g_pendingDelete.push_back({ a_ref->GetHandle(), 4.0f });  // deleted a few seconds later: the movement code may still be using it
		}

		std::set<RE::FormID> g_queued;  // birds already queued for deletion (a corpse is queued once)

		// A killed bird stays a while (the player may want to look), then goes: dynamic corpses are never cleaned up by the game.
		void Bury(RE::TESObjectREFR* a_ref)
		{
			if (a_ref && g_queued.insert(a_ref->GetFormID()).second) {
				g_pendingDelete.push_back({ a_ref->GetHandle(), 30.0f });
			}
		}

		// Birds left from an earlier session (or lost track of): none of them is in g_birds, so nothing else would ever remove them.
		void SweepOrphans(RE::PlayerCharacter* a_player)
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) {
				return;
			}
			std::vector<std::pair<RE::FormID, int>> bases;  // base form -> species index
			for (std::size_t i = 0; i < g_cfg.species.size(); ++i) {
				const auto& s = g_cfg.species[i];
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(s.form, s.plugin)) bases.push_back({ f->GetFormID(), static_cast<int>(i) });
			}
			auto* cell = a_player->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			int   budget = 2;  // removing many actors in one frame crashed the movement code (2026-11-20 03:03 and 03:04): a couple per pass
			tes->ForEachReferenceInRange(a_player, 8000.0f, [&](auto&& a_ref) {
				if (budget <= 0) {
					return RE::BSContainer::ForEachResult::kStop;
				}
				auto* ref = compat::Ptr(a_ref);
				if (!ref || ref->GetFormID() < 0xFF000000 || !ref->Is3DLoaded() || !ref->GetBaseObject() || g_queued.count(ref->GetFormID())) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				int species = -1;
				for (const auto& [id, idx] : bases) {
					if (id == ref->GetBaseObject()->GetFormID()) {
						species = idx;
					}
				}
				if (species < 0) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				for (const auto& b : g_birds) {
					if (const auto r = b.ref.get(); r && r->GetFormID() == ref->GetFormID()) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
				}
				const auto* actor = ref->As<RE::Actor>();
				if (actor && actor->IsDead()) {
					--budget;
					Bury(ref);
					SKSE::log::info("birds: sweep: {:X} was left over (dead), removing in 30 s", ref->GetFormID());
				} else if (!ref->IsDisabled()) {
					// a bird saved from an earlier session: nothing tracks it, so it would stand frozen for ever. Take it over (touches nothing,
					// so it is safe at any time): it starts its idle, hops or flees like a new bird and leaves after its time.
					Bird b;
					b.ref = ref->GetHandle();
					b.species = species;
					b.pos = ref->GetPosition();
					b.timer = Rand(8.0f, 30.0f);
					b.idleTimer = Rand(0.5f, 3.0f);
					RE::NiPoint3 hit;
					if (CastDown(world, { b.pos.x, b.pos.y, b.pos.z + 40.0f }, 600.0f, hit) && b.pos.z - hit.z < 120.0f) {
						b.grounded = true;
						b.pos.z = hit.z + 2.0f;
						b.home = b.pos;
						b.hopTimer = Rand(0.5f, 2.5f);
					}
					g_birds.push_back(b);
					SKSE::log::info("birds: sweep: {:X} ({}) was left over from an earlier session, taken over ({})", ref->GetFormID(), g_cfg.species[static_cast<std::size_t>(species)].id,
						b.grounded ? "on the ground" : "perched");
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void FlushPending(float a_dt)
		{
			for (auto it = g_pendingDelete.begin(); it != g_pendingDelete.end();) {
				it->ttl -= a_dt;
				if (it->ttl <= 0.0f) {
					if (auto r = it->h.get()) {
						r->SetDelete(true);
					}
					it = g_pendingDelete.erase(it);
				} else {
					++it;
				}
			}
		}

		// A bird can take animation events only when its 3D and its behavior graph are loaded.
		bool Ready(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->Is3DLoaded()) {
				return false;
			}
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
			return a_actor->GetAnimationGraphManager(mgr) && mgr && mgr->graphs.size() > 0;
		}

		// The game's own Papyrus natives, called through the script VM the way a script calls them. The reference mod (SkyBirds) animates and moves its
		// birds with exactly these: PlayGamebryoAnimation, SplineTranslateTo and SetMotionType.
		template <class... Args>
		bool Native(RE::TESObjectREFR* a_ref, const char* a_func, Args... a_args)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!vm || !policy || !a_ref) {
				return false;
			}
			const auto                                               handle = policy->GetHandleForObject(static_cast<std::uint32_t>(a_ref->GetFormType()), a_ref);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			return vm->DispatchMethodCall2(handle, "ObjectReference", a_func, RE::MakeFunctionArguments(std::move(a_args)...), none);
		}

		RE::NiControllerManager* FindManager(RE::NiAVObject* a_root)
		{
			if (!a_root) {
				return nullptr;
			}
			for (auto* c = a_root->GetControllers(); c; c = c->GetNext()) {
				if (auto* m = netimmerse_cast<RE::NiControllerManager*>(c)) {
					return m;
				}
			}
			return nullptr;
		}

		std::unordered_map<RE::FormID, std::string> g_curSeq;  // by bird: the clip that is playing now
		int                                         g_seqFails{ 0 };

		// Plays one of the clips inside the bird's own nif (airFlap, airToGrnd, grndFlap, grndLook, grndPeck, grndToAir) directly on its
		// NiControllerManager. That is what PlayGamebryoAnimation does, and it needs no script on the bird (the Papyrus native only works on a
		// reference that already has a script object bound).
		bool PlaySeq(RE::TESObjectREFR* a_ref, const char* a_name)
		{
			const char* why = nullptr;
			auto*       root = a_ref ? a_ref->Get3D() : nullptr;
			auto*       mgr = FindManager(root);
			RE::NiControllerSequence* seq = nullptr;
			if (!root) {
				why = "no 3D";
			} else if (!mgr) {
				why = "no NiControllerManager on the 3D root";
			} else if (!(seq = mgr->GetSequenceByName(a_name))) {
				why = "the nif has no clip of that name";
			}
			if (why) {
				if (g_seqFails < 12) {
					++g_seqFails;
					SKSE::log::info("birds: clip '{}' not played on {:08X}: {}", a_name, a_ref ? a_ref->GetFormID() : 0u, why);
				}
				return false;
			}
			auto& cur = g_curSeq[a_ref->GetFormID()];
			if (!cur.empty() && cur != a_name) {
				if (auto* prev = mgr->GetSequenceByName(cur)) {
					prev->Deactivate(0.12f, false);
				}
			}
			cur = a_name;
			return seq->Activate(0, true, 1.0f, 0.12f, nullptr, false);
		}

		// What an event means for the bird: the behavior graph is told (so that its state follows and cannot undo the clip) and the clip of
		// the same name is played directly. Returns true when the clip was handed to the game.
		bool Send(RE::TESObjectREFR* a_ref, const char* a_event)
		{
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return false;
			}
			const bool             graphOk = actor->NotifyAnimationGraph(a_event);
			const std::string_view ev{ a_event };
			const char*            clip = ev == "startGrndLook" ? "grndLook" : ev == "startGrndPeck" ? "grndPeck" : ev == "startGrndFlap" ? "grndFlap" : ev == "takeoff" ? "grndToAir" : ev == "land" ? "airToGrnd" : nullptr;
			const bool             clipOk = clip && PlaySeq(a_ref, clip);
			if (g_diagLines < 60) {
				++g_diagLines;
				SKSE::log::info("birds: event '{}' on {:08X}: clip '{}' {}, graph {} (3D {})", a_event, actor->GetFormID(), clip ? clip : "-", clipOk ? "played" : "NOT played",
					graphOk ? "accepted" : "rejected", actor->Is3DLoaded());
			}
			return clipOk;
		}

		// Moves a bird. An actor has its own SetPosition(pos, updateCharController): the plain reference SetPosition leaves the character controller where it was,
		// which then drags the bird back every frame (the bird "turned, flapped and stayed").
		void MoveBird(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos)
		{
			if (auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr) {
				actor->SetPosition(a_pos, true);
			} else if (a_ref) {
				a_ref->SetPosition(a_pos);
			}
		}

		void FaceAndPlace(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_towards)
		{
			MoveBird(a_ref, a_pos);
			const float dx = a_towards.x - a_pos.x, dy = a_towards.y - a_pos.y;
			if (std::abs(dx) + std::abs(dy) > 1.0f) {
				a_ref->SetAngle({ 0.0f, 0.0f, std::atan2(dx, dy) });
			}
		}

		int PickSpecies(Kind a_kind, bool a_night)
		{
			int total = 0;
			for (const auto& s : g_cfg.species) {
				if (a_night && s.dayOnly) continue;
				if (std::find(s.perches.begin(), s.perches.end(), a_kind) != s.perches.end()) total += s.weight;
			}
			if (total <= 0) return -1;
			int roll = RandInt(1, total);
			for (std::size_t i = 0; i < g_cfg.species.size(); ++i) {
				const auto& s = g_cfg.species[i];
				if (a_night && s.dayOnly) continue;
				if (std::find(s.perches.begin(), s.perches.end(), a_kind) == s.perches.end()) continue;
				roll -= s.weight;
				if (roll <= 0) return static_cast<int>(i);
			}
			return -1;
		}

		bool Spawn(RE::PlayerCharacter* a_player, bool a_night)
		{
			if (g_perches.empty() || g_cfg.species.empty()) {
				return false;
			}
			const auto pp = a_player->GetPosition();
			for (int tries = 0; tries < 8; ++tries) {
				const auto  perch = g_perches[static_cast<std::size_t>(RandInt(0, static_cast<int>(g_perches.size()) - 1))];
				const float d = Dist2D(perch.pos, pp);
				if (d < g_cfg.spawnMinDist || d > g_cfg.spawnMaxDist) continue;
				bool taken = false;
				for (const auto& b : g_birds) {
					if (Dist2D(b.pos, perch.pos) < 90.0f) {
						taken = true;
						break;
					}
				}
				if (taken) continue;
				const int si = PickSpecies(perch.kind, a_night);
				if (si < 0) continue;
				const auto& sp = g_cfg.species[static_cast<std::size_t>(si)];
				auto*       form = RE::TESDataHandler::GetSingleton()->LookupForm(sp.form, sp.plugin);
				auto*       base = form ? form->As<RE::TESBoundObject>() : nullptr;
				if (!base) {
					SKSE::log::error("birds: {}|{:06X} not found", sp.plugin, sp.form);
					return false;
				}
				const RE::NiPoint3 rot{ 0.0f, 0.0f, Rand(0.0f, 6.2831f) };
				auto               handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(base, perch.pos, rot, a_player->GetParentCell(),
					a_player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
				auto               ref = handle.get();
				if (!ref) {
					SKSE::log::error("birds: CreateReferenceAtLocation returned nothing for {}", sp.id);
					return false;
				}
				Bird b;
				b.ref = handle;
				b.species = si;
				b.pos = perch.pos;
				b.timer = Rand(5.0f, 16.0f);
				b.callTimer = Rand(3.0f, 14.0f);
				{
					// a perch within 120 units of the ground is a ground perch: the bird stands on the ground and hops about
					auto*        cell = a_player->GetParentCell();
					RE::NiPoint3 hit;
					if (CastDown(cell ? cell->GetbhkWorld() : nullptr, { perch.pos.x, perch.pos.y, perch.pos.z + 40.0f }, 600.0f, hit) && perch.pos.z - hit.z < 120.0f) {
						b.grounded = true;
						b.pos.z = hit.z + 2.0f;
						b.home = b.pos;
						b.timer = Rand(25.0f, 60.0f);  // ground birds stay and hop about for a good while
						b.hopTimer = Rand(0.5f, 2.5f);
					}
				}
				b.idleTimer = Rand(0.5f, 3.0f);
				g_birds.push_back(b);
				SKSE::log::info("birds: spawned {} {:08X} on a {} at {:.0f},{:.0f},{:.0f} ({:.0f} away), {} birds", sp.id, ref->GetFormID(), KindName(perch.kind),
					perch.pos.x, perch.pos.y, perch.pos.z, d, g_birds.size());
				return true;
			}
			return false;
		}

		RE::BGSSoundDescriptorForm* ResolveSound(const std::string& a_plugin, std::uint32_t a_form)
		{
			auto* f = a_form ? RE::TESDataHandler::GetSingleton()->LookupForm(a_form, a_plugin) : nullptr;
			auto* d = f ? f->As<RE::BGSSoundDescriptorForm>() : nullptr;
			if (a_form && !d) {
				SKSE::log::warn("birds: sound {}|{:06X} not found", a_plugin, a_form);
			}
			return d;
		}

		// Plays a sound at a bird. Nothing is played when the player is too far away to hear it, or (a call) when another bird was heard a moment ago.
		bool PlayAt(RE::BGSSoundDescriptorForm* a_sound, RE::TESObjectREFR* a_ref, float a_playerDist, bool a_ignoreGap)
		{
			if (!g_cfg.sounds || !a_sound || !a_sound->soundDescriptor || !a_ref || a_playerDist > g_cfg.soundDist) {
				return false;
			}
			if (!a_ignoreGap && g_sinceCall < g_cfg.minCallGap) {
				return false;
			}
			auto* am = RE::BSAudioManager::GetSingleton();
			if (!am) {
				return false;
			}
			RE::BSSoundHandle h;
			if (!am->GetSoundHandle(h, a_sound->soundDescriptor)) {
				return false;
			}
			h.SetPosition(a_ref->GetPosition());
			if (auto* n = a_ref->Get3D()) {
				h.SetObjectToFollow(n);
			}
			h.SetVolume(g_cfg.soundVolume);
			if (!h.Play()) {
				return false;
			}
			g_sinceCall = 0.0f;
			return true;
		}

		constexpr float kRadToDeg = 57.2957795f;

		// Starts a flight to a perch of the bird's kind, or away from the player when it is scared or it is night.
		// The flight is two legs moved by the engine (SplineTranslateTo): up and across to a point halfway, then down onto the perch.
		void StartFlight(Bird& a_b, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_playerPos, bool a_scared, bool a_night)
		{
			const auto&  sp = g_cfg.species[static_cast<std::size_t>(a_b.species)];
			RE::NiPoint3 dest = a_b.pos;
			bool         ok = false;
			if (!a_scared && !a_night && !g_perches.empty()) {
				for (int tries = 0; tries < 12 && !ok; ++tries) {
					const auto& perch = g_perches[static_cast<std::size_t>(RandInt(0, static_cast<int>(g_perches.size()) - 1))];
					if (std::find(sp.perches.begin(), sp.perches.end(), perch.kind) == sp.perches.end()) continue;
					const float d = Dist2D(perch.pos, a_b.pos);
					if (d < g_cfg.hopMinDist || d > g_cfg.hopMaxDist) continue;
					if (Dist2D(perch.pos, a_playerPos) < 260.0f) continue;
					dest = perch.pos;
					ok = true;
				}
			}
			a_b.leaving = !ok;
			if (!ok) {
				// fly off: away from the player, up and out of the way, removed on arrival
				const float dx = a_b.pos.x - a_playerPos.x, dy = a_b.pos.y - a_playerPos.y;
				const float len = std::max(1.0f, std::hypot(dx, dy));
				const float a = Rand(-0.6f, 0.6f);
				const float ca = std::cos(a), sa = std::sin(a);
				const float ux = (dx * ca - dy * sa) / len, uy = (dx * sa + dy * ca) / len;
				dest = { a_b.pos.x + ux * 2600.0f, a_b.pos.y + uy * 2600.0f, a_b.pos.z + 700.0f };
			}
			a_b.from = a_b.pos;
			a_b.to = dest;
			const float dist = std::max(100.0f, std::hypot(dest.x - a_b.pos.x, dest.y - a_b.pos.y));
			const float heading = std::atan2(dest.x - a_b.pos.x, dest.y - a_b.pos.y);  // Skyrim yaw: clockwise from +Y
			if (a_b.leaving) {
				a_b.apex = dest;
				a_b.t1 = dist / g_cfg.flightSpeed;
				a_b.dur = a_b.t1 + 0.4f;
			} else {
				const float up = std::min(320.0f, 90.0f + dist * 0.12f);
				a_b.apex = { (a_b.pos.x + dest.x) * 0.5f, (a_b.pos.y + dest.y) * 0.5f, std::max(a_b.pos.z, dest.z) + up };
				const float d1 = std::hypot(a_b.apex.x - a_b.pos.x, a_b.apex.y - a_b.pos.y, a_b.apex.z - a_b.pos.z);
				const float d2 = std::hypot(dest.x - a_b.apex.x, dest.y - a_b.apex.y, dest.z - a_b.apex.z);
				a_b.t1 = d1 / g_cfg.flightSpeed;
				a_b.dur = a_b.t1 + d2 / (g_cfg.flightSpeed * 0.75f) + 0.4f;
			}
			a_b.arc = a_b.leaving ? 0.0f : std::min(260.0f, 50.0f + dist * 0.09f);  // only the manual fallback uses this
			a_b.t = 0.0f;
			a_b.elapsed = 0.0f;
			a_b.state = State::kFlying;
			a_b.landSent = false;
			a_b.flapSent = false;
			a_b.seg2Sent = false;
			a_b.movedChecked = false;
			a_b.manual = g_manualFlight;
			a_ref->SetAngle({ 0.0f, 0.0f, heading });
			Send(a_ref, "takeoff");
			if (g_cfg.flap != 0) {
				if (!g_cfg.flapResolved) {
					g_cfg.flapResolved = true;
					g_cfg.flapForm = ResolveSound(sp.plugin, g_cfg.flap);
				}
				const bool heard = PlayAt(g_cfg.flapForm, a_ref, Dist2D(a_b.pos, a_playerPos), true);
				if (g_soundLogs < 25) {
					++g_soundLogs;
					SKSE::log::info("birds: wing flutter at take-off of {:08X}: {}", a_ref->GetFormID(), heard ? "played" : "not played (too far, or no sound)");
				}
			}
			bool dispatched = false;
			if (!a_b.manual) {
				dispatched = Native(a_ref, "SplineTranslateTo", a_b.apex.x, a_b.apex.y, a_b.apex.z, 0.0f, 0.0f, heading * kRadToDeg, dist * 0.3f, g_cfg.flightSpeed, 0.0f);
			}
			if (g_flightLogs < 30) {
				++g_flightLogs;
				SKSE::log::info("birds: flight {:08X} ({}): {:.0f} units to {:.0f},{:.0f},{:.0f}{}, first leg {:.1f}s, total {:.1f}s, {}", a_ref->GetFormID(), sp.id, dist, dest.x, dest.y, dest.z,
					a_b.leaving ? " (flying off)" : "", a_b.t1, a_b.dur, a_b.manual ? "MANUAL (SetPosition)" : (dispatched ? "SplineTranslateTo dispatched" : "SplineTranslateTo NOT dispatched"));
			}
		}

		void UpdateBirds(RE::PlayerCharacter* a_player, float a_dt, bool a_night)
		{
			const auto pp = a_player->GetPosition();
			const bool inCombat = a_player->IsInCombat();
			for (auto it = g_birds.begin(); it != g_birds.end();) {
				auto ref = it->ref.get();
				if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
					it = g_birds.erase(it);
					continue;
				}
				if (auto* actor = ref->As<RE::Actor>(); actor && actor->IsDead()) {
					// a bird that was killed stays where it fell for a while, then is removed
					Bury(ref.get());
					it = g_birds.erase(it);
					continue;
				}
				const float dist = Dist2D(it->pos, pp);
				if (dist > g_cfg.despawnDist && it->state == State::kPerched) {
					Retire(ref.get());
					it = g_birds.erase(it);
					continue;
				}
				if (it->state == State::kPerched) {
					// a perched bird is held on its perch a few times a second (it is keyframed, so gravity does not pull it down)
					it->holdTimer -= a_dt;
					if (it->holdTimer <= 0.0f) {
						it->holdTimer = 0.25f;
						MoveBird(ref.get(), it->pos);
					}
					if (it->landPhase != 0) {
						it->landClock += a_dt;
						it->landRetry -= a_dt;
						if (it->landPhase == 1) {
							if (it->landRetry <= 0.0f) {
								it->landRetry = 0.25f;
								const bool ok = Send(ref.get(), "land");
								if (g_landLogs < 40) {
									++g_landLogs;
									SKSE::log::info("birds: landing {:08X}: land {} after {:.2f}s", ref->GetFormID(), ok ? "ACCEPTED" : "rejected", it->landClock);
								}
								if (ok) {
									it->landAccepted = true;
								}
							}
							if (it->landAccepted || it->landClock > 3.0f) {
								it->landPhase = 2;
								it->landClock = 0.0f;
								it->landRetry = 0.0f;
							}
						} else {
							if (it->landClock >= 0.9f && it->landRetry <= 0.0f) {
								it->landRetry = 0.3f;
								const bool ok = Send(ref.get(), "startGrndLook");
								if (g_landLogs < 40) {
									++g_landLogs;
									SKSE::log::info("birds: landed {:08X}: startGrndLook {} after {:.2f}s", ref->GetFormID(), ok ? "ACCEPTED" : "rejected", it->landClock);
								}
								if (ok || it->landClock > 3.0f) {
									it->landPhase = 0;
								}
							}
						}
						++it;
						continue;
					}
					if (!it->started) {
						// wait until the bird's 3D and behavior graph exist; then start its idle and turn its AI off (it is moved by this module only)
						auto* actor = ref->As<RE::Actor>();
						if (!Ready(actor)) {
							// not ready yet: wait, but a bird the player walks up to must still fly off
							if (dist >= g_cfg.scareDist) {
								++it;
								continue;
							}
						} else {
							Send(ref.get(), "startGrndLook");
							// the bird's AI stays ON: an actor with its AI disabled stops updating its animation (like "tai" in the console), which froze every clip
							if (auto* root3d = ref->Get3D()) {
								it->motionSet = root3d->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true, false, true);  // no gravity
							}
							if (g_diagLines < 70) {
								++g_diagLines;
								SKSE::log::info("birds: {:08X} set keyframed: {}", ref->GetFormID(), it->motionSet ? "dispatched" : "NOT dispatched");
							}
							it->started = true;
							it->timer = it->grounded ? Rand(25.0f, 60.0f) : Rand(4.0f, 12.0f);
						}
					}
					if (dist < 700.0f && !it->logged) {
						it->logged = true;
						SKSE::log::info("birds: player near {:08X} ({}): {:.0f} away, started {}, grounded {}, ready {}, scare distance {:.0f}", ref->GetFormID(),
							g_cfg.species[static_cast<std::size_t>(it->species)].id, dist, it->started, it->grounded, Ready(ref->As<RE::Actor>()), g_cfg.scareDist);
					}
					const bool scared = dist < g_cfg.scareDist || (inCombat && dist < g_cfg.scareDist * 4.0f);
					it->timer -= a_dt;
					it->idleTimer -= a_dt;
					it->callTimer -= a_dt;
					if (it->callTimer <= 0.0f) {
						auto& csp = g_cfg.species[static_cast<std::size_t>(it->species)];
						it->callTimer = Rand(csp.callMin, csp.callMax);
						if (csp.call != 0) {
							if (!csp.callResolved) {
								csp.callResolved = true;
								csp.callForm = ResolveSound(csp.plugin, csp.call);
							}
							const bool heard = PlayAt(csp.callForm, ref.get(), dist, false);
							if (g_soundLogs < 25) {
								++g_soundLogs;
								SKSE::log::info("birds: {} call from {:08X}, {:.0f} units away: {}", csp.id, ref->GetFormID(), dist, heard ? "played" : "not played (too far, another bird just called, or no sound)");
							}
						}
					}
					if (scared || it->timer <= 0.0f || a_night) {
						const bool nightLeave = a_night && g_cfg.species[static_cast<std::size_t>(it->species)].dayOnly;
						if (scared || nightLeave || RandInt(0, 99) < 70) {
							StartFlight(*it, ref.get(), pp, scared || nightLeave, a_night);
						} else {
							it->timer = Rand(5.0f, 14.0f);
						}
					} else if (it->idleTimer <= 0.0f && !it->hopping) {
						const int roll = RandInt(0, 99);
						Send(ref.get(), roll < 50 ? "startGrndLook" : (roll < 88 ? "startGrndPeck" : "startGrndFlap"));
						it->idleTimer = Rand(2.5f, 7.0f);
					}
					if (it->grounded && it->state == State::kPerched) {
						// ground birds hop about: a short arc to a spot near where they are, never far from the place they started
						if (it->hopping) {
							it->hopT += a_dt / it->hopDur;
							const float        u = std::min(1.0f, it->hopT);
							const RE::NiPoint3 p{ it->hopFrom.x + (it->hopTo.x - it->hopFrom.x) * u, it->hopFrom.y + (it->hopTo.y - it->hopFrom.y) * u,
								it->hopFrom.z + (it->hopTo.z - it->hopFrom.z) * u + 22.0f * std::sin(3.14159265f * u) };
							FaceAndPlace(ref.get(), p, it->hopTo);
							it->pos = p;
							if (u >= 1.0f) {
								it->hopping = false;
								it->pos = it->hopTo;
								it->hopTimer = Rand(0.8f, 3.5f);
							}
						} else {
							it->hopTimer -= a_dt;
							if (it->hopTimer <= 0.0f) {
								float        ang = Rand(0.0f, 6.2831f);
								const float  step = Rand(45.0f, 110.0f);
								RE::NiPoint3 to{ it->pos.x + std::sin(ang) * step, it->pos.y + std::cos(ang) * step, it->pos.z };
								if (Dist2D(to, it->home) > 300.0f) {
									ang = std::atan2(it->home.x - it->pos.x, it->home.y - it->pos.y);
									to = { it->pos.x + std::sin(ang) * step, it->pos.y + std::cos(ang) * step, it->pos.z };
								}
								RE::NiPoint3 hit;
								auto*        hcell = a_player->GetParentCell();
								if (CastDown(hcell ? hcell->GetbhkWorld() : nullptr, { to.x, to.y, to.z + 60.0f }, 220.0f, hit)) {
									to.z = hit.z + 2.0f;
								}
								it->hopFrom = it->pos;
								it->hopTo = to;
								it->hopT = 0.0f;
								it->hopping = true;
							}
						}
					}
					++it;
					continue;
				}
				// flying: the engine moves the bird along its spline; this code starts the second leg, plays the clips and checks that it really moves
				it->elapsed += a_dt;
				const float u = std::min(1.0f, it->elapsed / it->dur);
				const float heading = std::atan2(it->to.x - it->from.x, it->to.y - it->from.y);
				if (!it->manual) {
					const auto cur = ref->GetPosition();
					if (!it->movedChecked && it->elapsed > 0.8f) {
						it->movedChecked = true;
						const float moved = std::hypot(cur.x - it->from.x, cur.y - it->from.y, cur.z - it->from.z);
						if (moved < 25.0f) {
							// the spline did not move this bird: stop it and fly by SetPosition from now on
							Native(ref.get(), "StopTranslation");
							g_manualFlight = true;
							it->manual = true;
							SKSE::log::info("birds: SplineTranslateTo did not move {:08X} (moved {:.1f} in {:.1f}s): this and all later flights use SetPosition", ref->GetFormID(), moved, it->elapsed);
						} else if (g_flightLogs < 30) {
							SKSE::log::info("birds: {:08X} is flying: moved {:.0f} units in {:.1f}s (the engine spline works)", ref->GetFormID(), moved, it->elapsed);
						}
					}
					if (!it->manual) {
						if (!it->seg2Sent && !it->leaving && it->elapsed >= it->t1 - 0.05f) {
							it->seg2Sent = true;
							const float dist2 = std::max(100.0f, std::hypot(it->to.x - it->apex.x, it->to.y - it->apex.y));
							Native(ref.get(), "SplineTranslateTo", it->to.x, it->to.y, it->to.z, 0.0f, 0.0f, heading * kRadToDeg, dist2 * 0.3f, g_cfg.flightSpeed * 0.75f, 0.0f);
						}
						it->pos = cur;
					}
				}
				if (it->manual) {
					const float        ease = u * u * (3.0f - 2.0f * u);
					const RE::NiPoint3 p{ it->from.x + (it->to.x - it->from.x) * ease, it->from.y + (it->to.y - it->from.y) * ease,
						it->from.z + (it->to.z - it->from.z) * ease + it->arc * std::sin(3.14159265f * u) };
					FaceAndPlace(ref.get(), p, it->to);
					it->pos = p;
					if (!it->movedChecked && it->elapsed > 1.0f) {
						it->movedChecked = true;
						const auto  act = ref->GetPosition();
						const float off = std::hypot(act.x - p.x, act.y - p.y, act.z - p.z);
						if (g_flightLogs < 40) {
							++g_flightLogs;
							SKSE::log::info("birds: manual flight {:08X}: after {:.1f}s the bird is {:.0f} units from where it was put ({})", ref->GetFormID(), it->elapsed, off, off < 30.0f ? "it follows" : "it is being pulled back");
						}
					}
				}
				if (!it->flapSent && it->elapsed > 0.45f) {
					it->flapSent = true;
					PlaySeq(ref.get(), "airFlap");
				}
				if (!it->landSent && !it->leaving && it->elapsed > it->dur - 0.9f) {
					it->landAccepted = Send(ref.get(), "land");
					it->landSent = true;
					if (g_landLogs < 40) {
						++g_landLogs;
						SKSE::log::info("birds: approaching {:08X}: land clip {}", ref->GetFormID(), it->landAccepted ? "played" : "NOT played");
					}
				}
				if (u >= 1.0f) {
					if (it->leaving) {
						Retire(ref.get());
						it = g_birds.erase(it);
						continue;
					}
					it->state = State::kPerched;
					it->pos = it->to;
					MoveBird(ref.get(), it->pos);
					it->timer = Rand(6.0f, 20.0f);
					it->idleTimer = Rand(1.0f, 4.0f);
					it->landPhase = it->landAccepted ? 2 : 1;
					it->landClock = 0.0f;
					it->landRetry = 0.0f;
				}
				++it;
			}
		}

		void Tick(float a_dt)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cal = RE::Calendar::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			if (!player || !cal || !player->Is3DLoaded() || !player->GetParentCell() || (ui && ui->GameIsPaused())) {
				return;
			}
			auto* cell = player->GetParentCell();
			if (cell->IsInteriorCell() || !player->GetWorldspace()) {
				// indoors (or in a cell change): touch nothing. Removing actors while the exterior unloads crashed the movement code (2026-11-20).
				g_lastCell = 0;
				return;
			}
			FlushPending(a_dt);
			g_sinceLoad += a_dt;
			if (g_sinceLoad < 25.0f) {
				return;
			}
			const float hour = cal->GetHour();
			const bool  night = !(hour >= g_cfg.dayStart && hour < g_cfg.dayEnd);

			g_sinceScan += a_dt;
			g_sinceCall += a_dt;
			g_sinceSpawn += a_dt;
			const RE::FormID cellId = cell->GetFormID();
			if (g_sinceScan >= g_cfg.perchRefreshSeconds || cellId != g_lastCell) {
				g_lastCell = cellId;
				g_sinceScan = 0.0f;
				RefreshPerches(player);
				SweepOrphans(player);
			}
			UpdateBirds(player, a_dt, night);
			if (!night && static_cast<int>(g_birds.size()) < g_cfg.maxBirds && g_sinceSpawn >= g_cfg.spawnEverySeconds) {
				g_sinceSpawn = 0.0f;
				Spawn(player, night);
			}
		}

		void Loop()
		{
			auto last = std::chrono::steady_clock::now();
			while (g_run.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(g_cfg.tickSeconds * 1000.0f)));
				const auto  now = std::chrono::steady_clock::now();
				const float dt = std::min(0.25f, std::chrono::duration<float>(now - last).count());
				last = now;
				if (const auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([dt] { Tick(dt); });
				}
			}
		}
	}

	void OnGameLoaded()
	{
		if (g_cfg.species.empty()) {
			return;
		}
		// Birds from an earlier session are not tracked any more. They are NOT touched here (removing actors while the save is still
		// loading crashed the game); the sweep removes them a couple at a time once the world has settled.
		SKSE::GetTaskInterface()->AddTask([] {
			g_birds.clear();
			g_perches.clear();
			g_queued.clear();
			g_pendingDelete.clear();
			g_curSeq.clear();
			g_lastCell = 0;
			g_sinceLoad = 0.0f;
			g_sinceSpawn = 0.0f;
			SKSE::log::info("birds: game loaded, waiting 25 s before the first scan");
		});
	}

	void Init()
	{
		Load();
		if (!g_cfg.enabled || g_cfg.species.empty() || g_cfg.maxBirds <= 0) {
			SKSE::log::info("birds: not started");
			return;
		}
		g_run = true;
		std::thread(Loop).detach();
		SKSE::log::info("birds: started");
	}
}
