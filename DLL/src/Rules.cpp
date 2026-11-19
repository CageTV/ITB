/*
 * ITB Wildlife
 * Copyright (c) 2026 CageTV
 *
 * Licensed under the GNU General Public License, version 3 or later; see LICENSE.
 */
#include "PCH.h"
#include "Rules.h"

#include <nlohmann/json.hpp>

// rules.json:
//   package_list_by_race : [ { "race": "Plugin|ID", "list": "Plugin|ID", "_name": "..." } ]
//       every non-unique, non-templated NPC of the race that has no schedule list from the list's own plugin gets the list
//   ai_by_race           : [ { "race": "Plugin|ID", "aggression": n, "confidence": n, "assistance": n, "aggroRadius": bool } ]
//       prey that flees instead of fighting (applied to non-unique NPCs of the race)
//   attack_caps          : [ { "race": "Plugin|ID", "_name": "...", "caps": { "<attack event>": [stagger, knockdown] } } ]
//       no attack of the race staggers or knocks down more than the cap, whichever mod last overwrote the race.
//       Events missing from "caps" are held to 0.5 stagger (1.0 for power attacks) and no knockdown.
namespace Rules
{
	namespace
	{
		std::filesystem::path ConfigPath()
		{
			return std::filesystem::path("Data/SKSE/Plugins/ITBWildlife/rules.json");
		}

		RE::TESForm* Find(const std::string& a_ref)
		{
			const auto bar = a_ref.find('|');
			if (bar == std::string::npos) {
				return nullptr;
			}
			std::uint32_t id = 0;
			try {
				id = static_cast<std::uint32_t>(std::stoul(a_ref.substr(bar + 1), nullptr, 16));
			} catch (...) {
				return nullptr;
			}
			return RE::TESDataHandler::GetSingleton()->LookupForm(id, a_ref.substr(0, bar));
		}

		struct Tally
		{
			int rules{ 0 }, changed{ 0 }, already{ 0 }, missing{ 0 };
		};

		void PackageListByRace(const nlohmann::json& a_rules, Tally& t)
		{
			struct Rule
			{
				RE::TESRace*     race;
				RE::BGSListForm* list;
				int              applied{ 0 };
				std::string      name;
			};
			std::vector<Rule> rules;
			for (const auto& r : a_rules) {
				auto* f0 = Find(r.value("race", ""));
				auto* f1 = Find(r.value("list", ""));
				auto* race = f0 ? f0->As<RE::TESRace>() : nullptr;
				auto* list = f1 ? f1->As<RE::BGSListForm>() : nullptr;
				if (!race || !list) {
					++t.missing;
					continue;
				}
				rules.push_back({ race, list, 0, r.value("_name", "") });
			}
			if (rules.empty()) {
				return;
			}
			const auto owner = [](RE::BGSListForm* a_l) {
				const auto* file = a_l ? a_l->GetFile(0) : nullptr;
				return file ? std::string(file->GetFilename()) : std::string();
			};
			int total = 0, skippedUnique = 0;
			for (auto* npc : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>()) {
				// templated NPCs take race and AI from their template
				if (!npc || !npc->race || npc->baseTemplateForm) {
					continue;
				}
				for (auto& r : rules) {
					if (npc->race != r.race) {
						continue;
					}
					if (npc->IsUnique()) {
						++skippedUnique;
						break;
					}
					if (npc->defaultPackList == r.list) {
						++t.already;
						break;
					}
					const auto cur = owner(npc->defaultPackList);
					if (!cur.empty() && cur == owner(r.list)) {
						++t.already;
						break;
					}
					npc->defaultPackList = r.list;
					++r.applied;
					++total;
					++t.changed;
					break;
				}
			}
			SKSE::log::info("  package_list_by_race: {} creatures got a schedule list, {} unique creatures skipped", total, skippedUnique);
		}

		void AiByRace(const nlohmann::json& a_rules, Tally& t)
		{
			int total = 0;
			for (const auto& r : a_rules) {
				auto* f0 = Find(r.value("race", ""));
				auto* race = f0 ? f0->As<RE::TESRace>() : nullptr;
				if (!race) {
					++t.missing;
					continue;
				}
				const auto aggr = static_cast<RE::ACTOR_AGGRESSION>(r.value("aggression", 0));
				const auto conf = static_cast<RE::ACTOR_CONFIDENCE>(r.value("confidence", 0));
				const auto assi = static_cast<RE::ACTOR_ASSISTANCE>(r.value("assistance", 0));
				const bool setRadius = r.contains("aggroRadius");
				const bool radiusOn = r.value("aggroRadius", true);
				int        n = 0;
				for (auto* npc : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>()) {
					if (!npc || npc->race != race || npc->IsUnique()) {
						continue;
					}
					const bool radiusOk = !setRadius || npc->aiData.aggroRadiusBehaviour == radiusOn;
					if (npc->GetAggressionLevel() == aggr && npc->GetConfidenceLevel() == conf && npc->GetAssistanceLevel() == assi && radiusOk) {
						++t.already;
						continue;
					}
					npc->SetAggressionLevel(aggr);
					npc->SetConfidenceLevel(conf);
					npc->SetAssistanceLevel(assi);
					if (setRadius) {
						npc->aiData.aggroRadiusBehaviour = radiusOn;
					}
					++n;
					++t.changed;
				}
				total += n;
				SKSE::log::info("  ai_by_race: {} {} NPC(s) changed", r.value("_name", std::string("?")), n);
			}
			SKSE::log::info("  ai_by_race: {} NPC(s) changed in total", total);
		}

		void AttackCaps(const nlohmann::json& a_rules, Tally& t)
		{
			int total = 0;
			for (const auto& r : a_rules) {
				auto* f0 = Find(r.value("race", ""));
				auto* race = f0 ? f0->As<RE::TESRace>() : nullptr;
				if (!race || !race->attackDataMap || !r.contains("caps") || !r["caps"].is_object()) {
					++t.missing;
					continue;
				}
				int n = 0;
				for (auto& entry : race->attackDataMap->attackDataMap) {
					auto* atk = entry.second.get();
					if (!atk) {
						continue;
					}
					const std::string ev = atk->event.c_str() ? atk->event.c_str() : "";
					const bool        power = atk->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
					float             capS = power ? 1.0f : 0.5f;
					float             capK = 0.0f;
					const auto        it = r["caps"].find(ev);
					if (it != r["caps"].end() && it->is_array() && it->size() >= 2) {
						capS = (*it)[0].get<float>();
						capK = (*it)[1].get<float>();
					}
					bool changed = false;
					if (atk->data.staggerOffset > capS) {
						atk->data.staggerOffset = capS;
						changed = true;
					}
					if (atk->data.knockDown > capK) {
						atk->data.knockDown = capK;
						changed = true;
					}
					if (changed) {
						++n;
						++t.changed;
					} else {
						++t.already;
					}
				}
				total += n;
				if (n > 0) {
					SKSE::log::info("  attack_caps: {} {} attack(s) lowered", r.value("_name", std::string("?")), n);
				}
			}
			SKSE::log::info("  attack_caps: {} attack(s) lowered in total", total);
		}

		nlohmann::json Load()
		{
			std::ifstream in(ConfigPath());
			if (!in) {
				return nlohmann::json::value_t::discarded;
			}
			return nlohmann::json::parse(in, nullptr, false, true);
		}
	}

	void Apply()
	{
		const auto j = Load();
		if (j.is_discarded() || !j.is_object()) {
			SKSE::log::warn("rules: no usable {} (idle)", ConfigPath().string());
			return;
		}
		Tally t;
		if (j.contains("attack_caps") && j["attack_caps"].is_array()) {
			++t.rules;
			AttackCaps(j["attack_caps"], t);
		}
		if (j.contains("ai_by_race") && j["ai_by_race"].is_array()) {
			++t.rules;
			AiByRace(j["ai_by_race"], t);
		}
		if (j.contains("package_list_by_race") && j["package_list_by_race"].is_array()) {
			++t.rules;
			PackageListByRace(j["package_list_by_race"], t);
		}
		SKSE::log::info("rules: {} rule sets, {} changes applied, {} already in place, {} references missing", t.rules, t.changed, t.already, t.missing);
	}
}
