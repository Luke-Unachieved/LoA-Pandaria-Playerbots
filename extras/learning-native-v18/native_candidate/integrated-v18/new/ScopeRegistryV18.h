#ifndef SCOPE_REGISTRY_V18_H
#define SCOPE_REGISTRY_V18_H
#include "EngineShadowObserverV17.h"
#include <array>
#include <map>
#include <limits>
#include <cstdint>
namespace ShadowCollectorV18 {
// Numeric addresses are lookup keys only. This sidecar never dereferences them,
// exports them, or interprets allocation addresses as incarnation identifiers.
struct NativeFacts {
    std::uintptr_t ai, actorAddress, sessionAddress, groupAddress, mapAddress;
    std::uint64_t actor, group, groupLeader, epoch;
    std::uint32_t map, instance, memberCount;
    EngineShadowV17::ScopeKind kind;
    std::array<std::uint64_t,10> members;
};
inline bool SameFacts(NativeFacts const& a, NativeFacts const& b) {
    return a.ai==b.ai && a.actorAddress==b.actorAddress && a.sessionAddress==b.sessionAddress &&
        a.groupAddress==b.groupAddress && a.mapAddress==b.mapAddress && a.actor==b.actor &&
        a.group==b.group && a.groupLeader==b.groupLeader && a.epoch==b.epoch && a.map==b.map && a.instance==b.instance &&
        a.memberCount==b.memberCount && a.members==b.members && a.kind==b.kind;
}
// All methods require the caller's registry mutex. Capacities bound allocation;
// Resolve reuses existing actor/engine entries; the first native dungeon
// observation may allocate one bounded map entry. Later resolves do not allocate.
class ScopeRegistry {
    struct ActorEntry {
        std::uintptr_t address;
        std::uint64_t actor, incarnation, aiIncarnation, session, mapRevision;
        NativeFacts facts;
    };
    struct EngineEntry { std::uintptr_t ai; std::uint64_t id, generation; };
    struct MapEntry { std::uint64_t incarnation, revision; };
    std::map<std::uintptr_t,ActorEntry> actors_;
    std::map<std::uintptr_t,EngineEntry> engines_;
    std::map<std::uintptr_t,MapEntry> maps_;
    std::uint64_t serial_;
    bool exhausted_;
public:
    static const std::size_t ActorLimit=4096, EngineLimit=16384, MapLimit=256;
    ScopeRegistry():serial_(0),exhausted_(false) {}
    std::uint64_t Next() {
        if (serial_==std::numeric_limits<std::uint64_t>::max()) { exhausted_=true; return 0; }
        return ++serial_;
    }
    bool CreateActor(std::uintptr_t ai, std::uintptr_t actor, std::uint64_t guid) {
        if (!ai || !actor || !guid) return false;
        if (!actors_.count(ai) && actors_.size()>=ActorLimit) return false;
        ActorEntry e={}; e.address=actor; e.actor=guid; e.incarnation=Next(); e.aiIncarnation=Next();
        if(exhausted_) return false;
        actors_[ai]=e; return true;
    }
    void ForgetActor(std::uintptr_t ai) { actors_.erase(ai); }
    void ResetActor(std::uintptr_t ai) { auto i=actors_.find(ai); if(i!=actors_.end()) i->second.session=0; }
    bool CreateEngine(std::uintptr_t engine,std::uintptr_t ai) {
        if(!engine || !ai || (!engines_.count(engine) && engines_.size()>=EngineLimit)) return false;
        EngineEntry e={ai,Next(),Next()}; if(exhausted_) return false; engines_[engine]=e; return true;
    }
    void ForgetEngine(std::uintptr_t engine) { engines_.erase(engine); }
    void ResetEngine(std::uintptr_t engine) { auto i=engines_.find(engine); if(i!=engines_.end()) i->second.generation=Next(); }
    bool TouchMap(std::uintptr_t map) {
        if(!map || (!maps_.count(map) && maps_.size()>=MapLimit)) return false;
        auto i=maps_.find(map);
        if(i==maps_.end()) { MapEntry e={Next(),Next()}; if(exhausted_) return false; maps_[map]=e; }
        else i->second.revision=Next();
        return !exhausted_;
    }
    bool EnsureMap(std::uintptr_t map) {
        if(maps_.count(map))return true;
        return TouchMap(map); // native first-observed map residency; no encounter epoch
    }
    void ForgetMap(std::uintptr_t map) { maps_.erase(map); }
    void InvalidateMap(std::uintptr_t map) { auto i=maps_.find(map); if(i!=maps_.end()) i->second.revision=Next(); }
    bool Resolve(NativeFacts const& f,std::uintptr_t engine,std::uint64_t run,
                 std::uint64_t stream,std::uint64_t tick,EngineShadowV17::Scope& out) {
        if(exhausted_ || !run || !stream || !tick || !f.sessionAddress ||
           !f.memberCount || f.memberCount>10 ||
           !((f.kind==EngineShadowV17::SupportedRaidEncounter && f.epoch) ||
             (f.kind==EngineShadowV17::DungeonMapSession && !f.epoch && f.memberCount<=5))) return false;
        auto a=actors_.find(f.ai); auto e=engines_.find(engine);
        if(a==actors_.end() || e==engines_.end() || e->second.ai!=f.ai ||
           a->second.address!=f.actorAddress || a->second.actor!=f.actor || !e->second.generation) return false;
        if(f.kind==EngineShadowV17::DungeonMapSession && !EnsureMap(f.mapAddress))return false;
        auto m=maps_.find(f.mapAddress);if(m==maps_.end())return false;
        ActorEntry& actor=a->second;
        if(!actor.session || !SameFacts(actor.facts,f) || actor.mapRevision!=m->second.revision) {
            actor.session=Next(); actor.facts=f; actor.mapRevision=m->second.revision;
        }
        if(exhausted_ || !actor.session) return false;
        out={}; out.run=run; out.session=actor.session; out.actor=f.actor;
        out.actorIncarnation=actor.incarnation; out.aiIncarnation=actor.aiIncarnation;
        out.group=f.group; out.epoch=f.epoch; out.mapIncarnation=m->second.incarnation;
        out.stream=stream; out.engine=e->second.id; out.engineIncarnation=e->second.generation;
        out.tick=tick; out.map=f.map; out.instance=f.instance;
        out.kind=f.kind;out.observedRoster=f.memberCount;return true;
    }
};
}
#endif
