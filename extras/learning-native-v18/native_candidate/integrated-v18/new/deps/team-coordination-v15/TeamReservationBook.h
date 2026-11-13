#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
namespace TeamDecisionV15 {
// Opaque native-proven identity: map identity, instance, epoch, target identity,
// native event generation, task kind. No pointer dereference or authority here.
using TaskKey=std::array<std::uint64_t,6>;
using Owner=std::array<std::uint64_t,3>; // actor identity, AI identity, session identity
class ReservationBook {
 struct Row { Owner owner; std::uint64_t token,issued,deadline; bool submitted; };
 std::map<TaskKey,Row> rows;std::mutex mutex;std::uint64_t serial=0;
 static bool Valid(TaskKey const&k,Owner const&o){for(auto v:k)if(!v)return false;for(auto v:o)if(!v)return false;return true;}
public:
 // Scores cannot call this directly. Caller must first revalidate native legality.
 std::uint64_t Claim(TaskKey const&k,Owner const&o,std::uint64_t now,std::uint64_t ttl){
  if(!Valid(k,o)||!ttl||ttl>5000||now>std::numeric_limits<std::uint64_t>::max()-ttl)return 0;
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(k);
  if(it!=rows.end()) {if(it->second.submitted||now<it->second.deadline)return 0;rows.erase(it);}
  if(rows.size()>=256||serial==std::numeric_limits<std::uint64_t>::max())return 0;
  auto token=++serial;rows.emplace(k,Row{o,token,now,now+ttl,false});return token;
 }
 bool CancelBeforeSubmit(TaskKey const&k,Owner const&o,std::uint64_t token){
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(k);
  if(it==rows.end()||it->second.submitted||it->second.owner!=o||it->second.token!=token)return false;
  rows.erase(it);return true;
 }
 bool Submit(TaskKey const&k,Owner const&o,std::uint64_t token,std::uint64_t now){
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(k);
  if(it==rows.end()||it->second.submitted||it->second.owner!=o||it->second.token!=token||now<it->second.issued||now>=it->second.deadline)return false;
  it->second.submitted=true;return true;
 }
 // Only a trusted native receipt proving this exact submission never took effect
 // may release it for a teammate. CastSpell(false) or timeout alone is NOT proof.
 bool ReleaseProvenNotExecuted(TaskKey const&k,Owner const&o,std::uint64_t token){
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(k);
  if(it==rows.end()||!it->second.submitted||it->second.owner!=o||it->second.token!=token)return false;
  rows.erase(it);return true;
 }
 // Native event end/reset retires its exact generation, never guessed from time.
 void ObserveEventEnd(TaskKey const&k){std::lock_guard<std::mutex>g(mutex);rows.erase(k);}
 void ForgetMap(std::uint64_t map){std::lock_guard<std::mutex>g(mutex);for(auto it=rows.begin();it!=rows.end();)if(it->first[0]==map)it=rows.erase(it);else ++it;}
};
}

