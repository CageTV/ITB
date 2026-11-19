#pragma once

namespace Flee
{
    struct Settings
    {
        static Settings* GetSingleton();

        // General
        bool  bEnabled = true;
        float fDetectionRadius = 1024.0f; // trigger range: how close a lit torch/lantern must be
        float fStalkDistance = 2048.0f;   // how far the creature is allowed to drift while stalking
        float fCooldown = 5.0f;           // seconds before the same actor can be re-triggered after an attack
        float fSpellCastLinger = 3.0f;     // seconds a burst spell cast (Firebolt/Fireball/Lightning
                                           // Bolt/Poison Spray, not a held concentration effect)
                                           // still counts as "deterred" after the cast ends -- an
                                           // instant cast is shorter than our 500ms poll, so without
                                           // this the flee would collapse the very next tick

        bool bUseKeywordFilter = false; // if true, ONLY actors with the none keyword are affected

        // Spell-cast deterrents, in addition to a held torch/lantern (all default on, matching the
        // existing fire-only behavior; Lightning simulates a fear of storms/loud noise, Poison
        // simulates a reaction to the smell of decay)
        bool bAffectFireSpells = true;
        bool bAffectLightningSpells = true;
        bool bAffectPoisonSpells = true;

        // Predator (naturally hostile -- resumes its own vanilla aggression once the light is gone)
        bool bAffectWolves = true;
        bool bAffectBears = true;
        bool bAffectSabreCats = true;
        bool bAffectTrolls = true;
        bool bAffectIceWraiths = true;
        bool bAffectSpriggans = true; // gated on vanilla SprigganFaction, not PredatorFaction/RKF

        // Prey (naturally passive -- attack is approximated by forcing Aggression; many prey
        // races have no vanilla combat animations/packages at all, so this may not visibly attack)
        bool bAffectDeer = true;
        bool bAffectGoats = true;
        bool bAffectHorkers = true;
        bool bAffectMammoths = true;
        bool bAffectSkeevers = true;

        // Spiders (naturally hostile, same resume-aggression path as predators)
        bool bAffectSpiders = true;



    };
}

#include <unordered_map>

namespace Flee
{
    enum class SpeciesCategory
    {
        kNone,
        kPredator,
        kPrey,
        kSpider
    };

    enum class FleeBehaviorState
    {
        kNormal,
        kStalking
    };

    struct ActorTracker
    {
        FleeBehaviorState state = FleeBehaviorState::kNormal;
        RE::FormID         lightHolderID = 0;
        float              cooldownUntil = 0.0f; // game-time hours
        float              cachedConfidence = 2.0f; // Confidence AV to restore once the light is gone
        float              deterrentUntil = 0.0f;   // game-time hours; keeps "still deterred" true
                                                      // briefly past the last confirmed torch/fire tick
    };

    // Driven by a worker-thread-paced task (see main.cpp), not an event sink -- no
    // BSTEventSink base is needed here.
    class FleeManager
    {
    public:
        static FleeManager* GetSingleton();

        void OnTick();

    private:
        std::unordered_map<RE::FormID, ActorTracker> trackers;

        RE::TESObjectREFR* FindNearestLitLightHolder(RE::TESObjectCELL* cell, const RE::NiPoint3& origin, float radius);
        RE::TESObjectLIGH*  GetHeldLight(RE::Actor* actor);
        bool                IsCastingElementalSpell(RE::Actor* actor, RE::ActorValue resistType);
        bool                HasDeterrent(RE::Actor* actor);
        bool                HasKeywordCascade(RE::Actor* actor, RE::BGSKeyword* keyword);
        SpeciesCategory     GetSpeciesCategory(RE::Actor* actor);
        bool                IsExcluded(RE::Actor* actor);
        bool                IsOnCooldown(RE::Actor* actor);
        void                SetCooldown(RE::Actor* actor);
        void                ResumeAggression(RE::Actor* actor, SpeciesCategory category, float confidenceToRestore);
    };
}
