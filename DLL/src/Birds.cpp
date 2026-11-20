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
		RE::FormID                       g_lastCell{ 0 };
		float                            g_sinceScan{ 1000.0f };
		float                            g_sinceSpawn{ 0.0f };
		int                              g_eventFails{ 0 };

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
			for (const auto& s : j.value("species", nlohmann::json::array())) {
				Species sp;
				sp.id = s.value("id", std::string("bird"));
				sp.plugin = s.value("plugin", std::string("ITB Wildlife.esp"));
				sp.form = static_cast<std::uint32_t>(std::stoul(s.value("form", std::string("0")), nullptr, 16));
				sp.weight = std::max(1, s.value("weight", 1));
				sp.dayOnly = s.value("dayOnly", true);
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
			if (Contains(a_model, "fence") && a_dims.z < 400.0f) return Kind::kFence;
			if ((Contains(a_model, "rock") || Contains(a_model, "boulder")) && !Contains(a_model, "rockwall") && a_dims.z > 40.0f && a_dims.z < 600.0f) return Kind::kRock;
			if (Contains(a_model, "architecture") && !Contains(a_model, "dungeon") && !Contains(a_model, "interior") && !Contains(a_model, "cave") &&
				!Contains(a_model, "mine") && !Contains(a_model, "ruin") && !Contains(a_model, "fort") && !Contains(a_model, "tomb") && !Contains(a_model, "crypt") &&
				a_dims.z >= 220.0f && a_dims.x >= 260.0f && a_dims.y >= 260.0f) {
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
				const auto kind = Classify(base, Lower(model->GetModel()), dims);
				if (!kind) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				++perchable;
				const RE::NiPoint3 ctr{ (bd.boundMax.x + bd.boundMin.x) * 0.5f * scale, (bd.boundMax.y + bd.boundMin.y) * 0.5f * scale, static_cast<float>(bd.boundMax.z) * scale };
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

		bool Send(RE::TESObjectREFR* a_ref, const char* a_event)
		{
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return false;
			}
			const bool ok = actor->NotifyAnimationGraph(a_event);
			if (g_diagLines < 60) {
				++g_diagLines;
				RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
				const bool hasMgr = actor->GetAnimationGraphManager(mgr) && mgr;
				SKSE::log::info("birds: event '{}' on {:08X}: {} (3D {}, graph manager {}, graphs {}, process level {})", a_event, actor->GetFormID(),
					ok ? "ACCEPTED" : "rejected", actor->Is3DLoaded(), hasMgr, hasMgr ? mgr->graphs.size() : 0, static_cast<int>(actor->GetProcessLevel()));
			}
			return ok;
		}

		void FaceAndPlace(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_towards)
		{
			a_ref->SetPosition(a_pos);
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
				b.idleTimer = Rand(0.5f, 3.0f);
				g_birds.push_back(b);
				SKSE::log::info("birds: spawned {} {:08X} on a {} at {:.0f},{:.0f},{:.0f} ({:.0f} away), {} birds", sp.id, ref->GetFormID(), KindName(perch.kind),
					perch.pos.x, perch.pos.y, perch.pos.z, d, g_birds.size());
				return true;
			}
			return false;
		}

		// Starts a flight to a perch of the bird's kind, or away from the player when it is scared or it is night.
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
			a_b.dur = std::max(1.6f, dist / g_cfg.flightSpeed);
			a_b.arc = a_b.leaving ? 0.0f : std::min(260.0f, 50.0f + dist * 0.09f);
			a_b.t = 0.0f;
			a_b.state = State::kFlying;
			a_b.landSent = false;
			a_b.flapSent = false;
			Send(a_ref, "takeoff");
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
					// a bird that was killed stays where it fell; the world cleans it up
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
					if (!it->started) {
						// wait until the bird's 3D and behavior graph exist; then start its idle and turn its AI off (it is moved by this module only)
						auto* actor = ref->As<RE::Actor>();
						if (!Ready(actor)) {
							++it;
							continue;
						}
						Send(ref.get(), "startGrndLook");
						actor->EnableAI(false);
						it->started = true;
						it->timer = Rand(4.0f, 12.0f);
					}
					const bool scared = dist < g_cfg.scareDist || (inCombat && dist < g_cfg.scareDist * 4.0f);
					it->timer -= a_dt;
					it->idleTimer -= a_dt;
					if (scared || it->timer <= 0.0f || a_night) {
						const bool nightLeave = a_night && g_cfg.species[static_cast<std::size_t>(it->species)].dayOnly;
						if (scared || nightLeave || RandInt(0, 99) < 70) {
							StartFlight(*it, ref.get(), pp, scared || nightLeave, a_night);
						} else {
							it->timer = Rand(5.0f, 14.0f);
						}
					} else if (it->idleTimer <= 0.0f) {
						const int roll = RandInt(0, 99);
						Send(ref.get(), roll < 50 ? "startGrndLook" : (roll < 88 ? "startGrndPeck" : "startGrndFlap"));
						it->idleTimer = Rand(2.5f, 7.0f);
					}
					++it;
					continue;
				}
				// flying
				it->t += a_dt / it->dur;
				if (!it->flapSent && it->t * it->dur > 0.3f) {
					Send(ref.get(), "startAirFlap");
					it->flapSent = true;
				}
				const float        u = std::min(1.0f, it->t);
				const float        ease = u * u * (3.0f - 2.0f * u);
				const RE::NiPoint3 p{ it->from.x + (it->to.x - it->from.x) * ease, it->from.y + (it->to.y - it->from.y) * ease,
					it->from.z + (it->to.z - it->from.z) * ease + it->arc * std::sin(3.14159265f * u) };
				FaceAndPlace(ref.get(), p, it->to);
				it->pos = p;
				if (!it->landSent && !it->leaving && (it->t * it->dur) > (it->dur - 0.45f)) {
					Send(ref.get(), "land");
					it->landSent = true;
				}
				if (it->t >= 1.0f) {
					if (it->leaving) {
						Retire(ref.get());
						it = g_birds.erase(it);
						continue;
					}
					it->state = State::kPerched;
					it->pos = it->to;
					it->timer = Rand(6.0f, 20.0f);
					it->idleTimer = Rand(1.0f, 4.0f);
					Send(ref.get(), "startGrndLook");
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
			const float hour = cal->GetHour();
			const bool  night = !(hour >= g_cfg.dayStart && hour < g_cfg.dayEnd);

			g_sinceScan += a_dt;
			g_sinceSpawn += a_dt;
			const RE::FormID cellId = cell->GetFormID();
			if (g_sinceScan >= g_cfg.perchRefreshSeconds || cellId != g_lastCell) {
				g_lastCell = cellId;
				g_sinceScan = 0.0f;
				RefreshPerches(player);
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
		// dynamic birds from an earlier session are not tracked any more: remove them
		SKSE::GetTaskInterface()->AddTask([] {
			g_birds.clear();
			g_perches.clear();
			g_lastCell = 0;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* tes = RE::TES::GetSingleton();
			if (!player || !tes || !player->GetParentCell()) {
				return;
			}
			std::vector<RE::FormID> bases;
			for (const auto& s : g_cfg.species) {
				if (auto* f = RE::TESDataHandler::GetSingleton()->LookupForm(s.form, s.plugin)) bases.push_back(f->GetFormID());
			}
			int removed = 0;
			tes->ForEachReferenceInRange(player, 20000.0f, [&](auto&& a_ref) {
				auto* ref = compat::Ptr(a_ref);
				if (ref && ref->GetFormID() >= 0xFF000000 && ref->GetBaseObject() &&
					std::find(bases.begin(), bases.end(), ref->GetBaseObject()->GetFormID()) != bases.end()) {
					Retire(ref);
					++removed;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			SKSE::log::info("birds: game loaded, removed {} old birds", removed);
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
