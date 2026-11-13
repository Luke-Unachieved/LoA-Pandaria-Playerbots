#pragma once
// Offline candidate, one process-wide controller on one serialized thread.
// Map threads supply only primitive snapshots. Never store/access Player* here.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace PlayerbotTrainingCandidate {
struct Actor {
 std::string id;bool verifiedDummy,humanControlled,connected;
 Actor():verifiedDummy(false),humanControlled(true),connected(false){}
};
struct Snapshot {
 std::string runId,buildId,sessionId,instanceId,epochId,groupId;
 bool runtimeKnown,verifiedDummyOnly,ready;unsigned humanMembers;std::vector<Actor> actors;
 Snapshot():runtimeKnown(false),verifiedDummyOnly(false),ready(false),humanMembers(1){}
};
struct Request {
 std::string commandId,authorizationNonce;Snapshot snapshot;double issuedAt,expiresAt;
 Request():issuedAt(0),expiresAt(0){}
};
// Desired native-controller roster plan; this header does not create groups.
struct DefaultTrainingPlan { enum { DungeonMembers=5, Raid10Members=10, Raid25Members=25, Teams=3, Actors=40 }; };
struct Config {
 bool enabled,shadowOnly;std::size_t capacity,concurrency;
 Config():enabled(false),shadowOnly(true),capacity(8),concurrency(3){}
};
struct Advice {
 enum Kind{Start,Hold,Stop};Kind kind;std::string runId,reason;unsigned workUnits;bool shadowOnly;
 Advice(Kind k,std::string const& r,std::string const& why,unsigned work=0)
  :kind(k),runId(r),reason(why),workUnits(work),shadowOnly(true){}
};
struct Result {bool accepted;std::vector<Advice> advice;explicit Result(bool ok=false):accepted(ok){}};
class BackgroundTrainingQueue {
public:
 enum{HARD_CAPACITY=16,HARD_CONCURRENCY=3,HARD_ACTORS=25,HARD_LEDGER=4096,MAX_ID_BYTES=128,MAX_TTL_SECONDS=120,
      ONLINE_ACTOR_BUDGET=40,OFFLINE_ACTOR_BUDGET=40};
 explicit BackgroundTrainingQueue(Config const& cfg=Config()):config_(cfg),hasClock_(false),lastClock_(0){}
 Result enqueue(Request const& request,double now){
  Result r;if(!observeClock(now,r))return r;expire(now,r);
  if(!configurationAllowed()||entries_.size()>=config_.capacity
     ||!text(request.commandId)||!text(request.authorizationNonce)
     ||commands_.count(request.commandId)||nonces_.count(request.authorizationNonce)
     ||commands_.size()>=HARD_LEDGER||nonces_.size()>=HARD_LEDGER
     ||!validSnapshot(request.snapshot)||find(request.snapshot.runId)!=entries_.end()
     ||!std::isfinite(request.issuedAt)||!std::isfinite(request.expiresAt)
     ||request.issuedAt<0||request.issuedAt>now||now>=request.expiresAt||request.expiresAt<=request.issuedAt
     ||request.expiresAt-request.issuedAt>MAX_TTL_SECONDS)return r;
  const std::vector<std::string> ids=actorIds(request.snapshot);
  for(std::size_t i=0;i<ids.size();++i)if(owners_.count(ids[i]))return r;
  Entry e;e.request=request;e.active=false;entries_.push_back(e);
  for(std::size_t i=0;i<ids.size();++i)owners_[ids[i]]=request.snapshot.runId;
  commands_.insert(request.commandId);nonces_.insert(request.authorizationNonce);r.accepted=true;
  r.advice.push_back(Advice(Advice::Hold,request.snapshot.runId,"queued_shadow_only"));return r;
 }
 // Heartbeat for the same run; never authorizes a new job or new game actions.
 Result renew(std::string const& runId,std::string const& commandId,std::string const& nonce,
              double expiresAt,Snapshot const& fresh,double now){
  Result r;if(!observeClock(now,r))return r;expire(now,r);
  if(!configurationAllowed()){stopAll("disabled_or_invalid_configuration",r);return r;}
  if(!text(runId))return r;
  std::vector<Entry>::iterator it=find(runId);if(it==entries_.end())return r;
  if(!validSnapshot(fresh)||!sameBinding(it->request.snapshot,fresh)){
   r.advice.push_back(Advice(Advice::Stop,runId,"renewal_context_not_dummy_only_or_changed"));
   releaseActors(it->request.snapshot);entries_.erase(it);return r;
  }
  if(!text(commandId)||!text(nonce)||commands_.count(commandId)||nonces_.count(nonce)
     ||commands_.size()>=HARD_LEDGER||nonces_.size()>=HARD_LEDGER
     ||!std::isfinite(expiresAt)||expiresAt<=now||expiresAt-now>MAX_TTL_SECONDS)return r;
  commands_.insert(commandId);nonces_.insert(nonce);
  // Preserve the original scope/actors and active state; only the lease changes.
  it->request.issuedAt=now;it->request.expiresAt=expiresAt;r.accepted=true;
  r.advice.push_back(Advice(Advice::Hold,runId,"lease_renewed_shadow_only"));return r;
 }
 Result evaluate(std::vector<Snapshot> const& current,double now,bool playersOnline,bool busy){
  Result r;if(!observeClock(now,r))return r;
  if(!configurationAllowed()){stopAll("disabled_or_invalid_configuration",r);return r;}
  if(current.size()>HARD_CAPACITY){stopAll("snapshot_budget_exceeded",r);return r;}
  std::map<std::string,Snapshot const*> snapshots;
  for(std::size_t i=0;i<current.size();++i)
   if(!text(current[i].runId)||!snapshots.insert(std::make_pair(current[i].runId,&current[i])).second){
    stopAll("ambiguous_native_snapshot",r);return r;
   }
  expire(now,r);const std::size_t limit=config_.concurrency;
  const std::size_t actorBudget=playersOnline?ONLINE_ACTOR_BUDGET:OFFLINE_ACTOR_BUDGET;
  std::size_t active=0,usedActors=0;
  for(std::vector<Entry>::iterator it=entries_.begin();it!=entries_.end();){
   const Snapshot& bound=it->request.snapshot;
   std::map<std::string,Snapshot const*>::const_iterator seen=snapshots.find(bound.runId);
   if(seen==snapshots.end()||!validSnapshot(*seen->second)||!sameBinding(bound,*seen->second)){
    r.advice.push_back(Advice(Advice::Stop,bound.runId,"native_context_not_dummy_only_or_changed"));
    releaseActors(bound);it=entries_.erase(it);continue;
   }
   const std::size_t roster=bound.actors.size();
   if(busy||!seen->second->ready||active>=limit||roster>actorBudget-usedActors){
    it->active=false;r.advice.push_back(Advice(Advice::Hold,bound.runId,busy?"host_busy":
     (!seen->second->ready?"not_ready":(active>=limit?"concurrency_budget":"actor_budget"))));
   }else{++active;usedActors+=roster;it->active=true;
    // Only roster budget units, never a measured CPU share or game command.
    r.advice.push_back(Advice(Advice::Start,bound.runId,playersOnline?"shadow_budget_available_player_priority":"shadow_budget_available",static_cast<unsigned>((roster+4)/5)));}
   ++it;
  }
  r.accepted=true;return r;
 }
 Result finish(std::string const& runId,std::string const& commandId,double now){
  Result r;if(!observeClock(now,r))return r;expire(now,r);
  if(!text(runId)||!text(commandId)||commands_.count(commandId)||commands_.size()>=HARD_LEDGER)return r;
  std::vector<Entry>::iterator it=find(runId);if(it==entries_.end())return r;
  commands_.insert(commandId);releaseActors(it->request.snapshot);entries_.erase(it);r.accepted=true;
  r.advice.push_back(Advice(Advice::Stop,runId,"run_finished"));return r;
 }
 Result disable(double now){Result r;if(!observeClock(now,r))return r;config_.enabled=false;stopAll("disabled",r);r.accepted=true;return r;}
 std::size_t queued()const{return entries_.size();}
 std::size_t reservedActors()const{return owners_.size();}
 std::size_t consumedNonces()const{return nonces_.size();}
private:
 struct Entry{Request request;bool active;};Config config_;bool hasClock_;double lastClock_;
 std::vector<Entry> entries_;std::map<std::string,std::string> owners_;
 // Never evict spent nonces; ledger exhaustion refuses new admission.
 std::set<std::string> commands_,nonces_;
 static bool text(std::string const& s){
  if(s.empty()||s.size()>MAX_ID_BYTES)return false;
  for(std::size_t i=0;i<s.size();++i)if(static_cast<unsigned char>(s[i])<32)return false;
  return true;
 }
 bool configurationAllowed()const{return config_.enabled&&config_.shadowOnly&&config_.capacity>0&&config_.capacity<=HARD_CAPACITY
  &&config_.concurrency>0&&config_.concurrency<=HARD_CONCURRENCY&&config_.concurrency<=config_.capacity;}
 static std::vector<std::string> actorIds(Snapshot const& s){
  std::vector<std::string> ids;for(std::size_t i=0;i<s.actors.size();++i)ids.push_back(s.actors[i].id);
  std::sort(ids.begin(),ids.end());return ids;
 }
 static bool validSnapshot(Snapshot const& s){
  if(!s.runtimeKnown||!s.verifiedDummyOnly||s.humanMembers!=0||!text(s.runId)||!text(s.buildId)
     ||!text(s.sessionId)||!text(s.instanceId)||!text(s.epochId)||!text(s.groupId)||s.actors.empty()||s.actors.size()>HARD_ACTORS)return false;
  std::set<std::string> ids;
  for(std::size_t i=0;i<s.actors.size();++i){const Actor& a=s.actors[i];
   if(!text(a.id)||!a.verifiedDummy||a.humanControlled||!a.connected||!ids.insert(a.id).second)return false;}
  return true;
 }
 static bool sameBinding(Snapshot const& a,Snapshot const& b){return a.runId==b.runId&&a.buildId==b.buildId&&a.sessionId==b.sessionId
  &&a.instanceId==b.instanceId&&a.epochId==b.epochId&&a.groupId==b.groupId&&actorIds(a)==actorIds(b);}
 std::vector<Entry>::iterator find(std::string const& id){std::vector<Entry>::iterator it=entries_.begin();
  for(;it!=entries_.end();++it){if(it->request.snapshot.runId==id)break;}
  return it;}
 void releaseActors(Snapshot const& s){for(std::size_t i=0;i<s.actors.size();++i){
  std::map<std::string,std::string>::iterator it=owners_.find(s.actors[i].id);if(it!=owners_.end()&&it->second==s.runId)owners_.erase(it);}}
 void stopAll(std::string const& why,Result& r){
  for(std::size_t i=0;i<entries_.size();++i){r.advice.push_back(Advice(Advice::Stop,entries_[i].request.snapshot.runId,why));}
  entries_.clear();owners_.clear();}
 bool observeClock(double now,Result& r){if(!std::isfinite(now)||now<0||(hasClock_&&now<lastClock_)){
   stopAll("invalid_or_rolled_back_clock",r);return false;}lastClock_=now;hasClock_=true;return true;}
 void expire(double now,Result& r){for(std::vector<Entry>::iterator it=entries_.begin();it!=entries_.end();){
  if(now>=it->request.expiresAt){r.advice.push_back(Advice(Advice::Stop,it->request.snapshot.runId,"lease_expired"));
   releaseActors(it->request.snapshot);it=entries_.erase(it);}else ++it;}}
};
}
