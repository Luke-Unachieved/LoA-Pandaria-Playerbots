#pragma once
// GPL-2.0-or-later. Pure selection helpers used by the native adapter, not a second queue.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
namespace NativeBackgroundTraining {
namespace Detail {
using Guid = std::uint64_t;
struct Observation {
    Guid guid = 0, session = 0, group = 0;
    std::vector<Guid> members;
    double at = 0;
    unsigned map = 0, instance = 0, difficulty = 0, team = 0, phase = 0, level = 0, role = 0;
    bool nativeBot = false, hasMaster = true, occupied = true, alive = false;
    bool combat = false, encounter = false, nearbyHuman = true, instanceable = true;
};
inline bool Fresh(Observation const& x, double now) { return x.session && now >= x.at && now-x.at <= 2.5; }
inline bool Selectable(Observation const& x, double now) {
    return Fresh(x,now) && x.nativeBot && !x.hasMaster && !x.occupied && !x.group &&
        x.alive && !x.combat && !x.encounter && !x.nearbyHuman && !x.instanceable;
}
// Roles are actual LFG role bits: tank 2, heal 4, damage 8. No respec/role invention.
inline std::vector<Observation> SelectFive(std::vector<Observation> const& pool, double now,
                                         unsigned minLevel, unsigned maxLevel) {
    for (auto const& leader : pool) {
        if (leader.role != 2 || !Selectable(leader,now)) continue;
        if (leader.level < minLevel || leader.level > maxLevel) continue;
        std::vector<Observation> result(1,leader);
        unsigned healing = 0, damage = 0;
        for (auto const& x : pool) {
            if (x.guid == leader.guid || !Selectable(x,now) || x.team != leader.team ||
                x.map != leader.map || x.phase != leader.phase || x.level < minLevel || x.level > maxLevel) continue;
            if (x.role == 4 && healing == 0) { ++healing; result.push_back(x); }
            else if (x.role == 8 && damage < 3) { ++damage; result.push_back(x); }
            if (healing == 1 && damage == 3) return result;
        }
    }
    return {};
}
inline bool ExactMembers(std::vector<Guid> members, std::vector<Observation> const& roster) {
    std::vector<Guid> expected;
    for (auto const& x : roster) expected.push_back(x.guid);
    std::sort(expected.begin(),expected.end()); std::sort(members.begin(),members.end());
    return expected == members && std::adjacent_find(expected.begin(),expected.end()) == expected.end();
}
// 'Busy' only suppresses admission/new-run work. Native combat updates are never throttled.
inline bool MayAdmit(bool enabled, bool admission, bool busy, bool pending, bool hasRun,
                     double now, double nextRunAt) {
    return enabled && admission && !busy && !pending && !hasRun && now >= nextRunAt;
}
}
}
