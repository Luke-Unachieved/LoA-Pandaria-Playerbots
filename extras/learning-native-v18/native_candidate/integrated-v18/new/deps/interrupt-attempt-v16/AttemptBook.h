#pragma once
#include "../team-coordination-v15/TeamReservationBook.h"
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
namespace InterruptAttemptV16 {
using TaskKey=TeamDecisionV15::TaskKey;
using Owner=TeamDecisionV15::Owner;
struct Binding {
 TaskKey task{}; Owner owner{};
 std::uint64_t reservationToken=0,targetCastToken=0;
 unsigned skillId=0;
 bool Valid() const {for(auto v:task)if(!v)return false;for(auto v:owner)if(!v)return false;
  return reservationToken&&targetCastToken&&task[4]==targetCastToken&&skillId;}
 bool operator==(Binding const& b)const{return task==b.task&&owner==b.owner&&reservationToken==b.reservationToken&&targetCastToken==b.targetCastToken&&skillId==b.skillId;}
};
struct Handle {
 Binding binding{};std::uintptr_t responseObject=0;std::uint64_t responseToken=0;
 bool Valid()const{return binding.Valid()&&responseObject&&responseToken;}
 bool operator==(Handle const&h)const{return binding==h.binding&&responseObject==h.responseObject&&responseToken==h.responseToken;}
};
struct InterruptEvidence {Handle response{};std::uint64_t targetCastToken=0;};
// The caller must independently prove these narrow native branches. Neither a
// CastSpell boolean nor finish(false), destruction or elapsed time is evidence.
enum class PreExecutionRejection { BeforePrepare, PrepareCheckRejected };
class AttemptBook {
 enum class Stage { Bound, Executing, ProvenNotExecuted, InterruptConfirmed };
 struct Row {Handle h;Stage stage=Stage::Bound;};
 std::map<std::uintptr_t,Row> rows;
 std::uint64_t serial=0;std::mutex mutex;
 bool Exact(std::map<std::uintptr_t,Row>::iterator it,Handle const&h)const {
  return it!=rows.end()&&h.Valid()&&it->second.h==h;
 }
public:
 explicit AttemptBook(std::uint64_t initial=0):serial(initial){}
 // Only actual allocated response objects, never a CheckCast probe. Duplicate
 // live addresses are rejected even with identical data; native End is required.
 Handle Bind(Binding const&b,std::uintptr_t object) {
  std::lock_guard<std::mutex>g(mutex);
  if(!b.Valid()||!object||rows.count(object)||rows.size()>=256||serial==UINT64_MAX)return {};
  // A reservation cannot be attached to multiple live response objects.
  for(auto const&v:rows)if(v.second.h.binding.task==b.task&&v.second.h.binding.reservationToken==b.reservationToken)return {};
  Handle h{b,object,++serial};rows.emplace(object,Row{h,Stage::Bound});return h;
 }
 bool EnterExecution(Handle const&h) {
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(h.responseObject);
  if(!Exact(it,h)||it->second.stage!=Stage::Bound)return false;
  it->second.stage=Stage::Executing;return true;
 }
 bool ProveNotExecuted(Handle const&h,PreExecutionRejection reason) {
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(h.responseObject);
  if(reason!=PreExecutionRejection::BeforePrepare&&reason!=PreExecutionRejection::PrepareCheckRejected)return false;
  if(!Exact(it,h)||it->second.stage!=Stage::Bound)return false;
  it->second.stage=Stage::ProvenNotExecuted;return true;
 }
 // Capture BEFORE native InterruptSpell can invalidate the hostile cast token.
 InterruptEvidence CaptureInterrupt(Handle const&h,std::uint64_t currentTargetToken) {
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(h.responseObject);
  if(!Exact(it,h)||it->second.stage!=Stage::Executing||!currentTargetToken||currentTargetToken!=h.binding.targetCastToken)return {};
  return {h,currentTargetToken};
 }
 // oldTargetSlotNoLongerCurrent must come from fresh native comparison after
 // actual EffectInterruptCast; it is not inferred from timeout or cast failure.
 bool ConfirmInterrupt(InterruptEvidence const&e,bool oldTargetSlotNoLongerCurrent) {
  std::lock_guard<std::mutex>g(mutex);auto it=rows.find(e.response.responseObject);
  if(!oldTargetSlotNoLongerCurrent||!Exact(it,e.response)||it->second.stage!=Stage::Executing||
     !e.targetCastToken||e.targetCastToken!=e.response.binding.targetCastToken)return false;
  it->second.stage=Stage::InterruptConfirmed;return true;
 }
 // Cleanup yields no outcome and never calls ReleaseProvenNotExecuted.
 void End(Handle const&h){std::lock_guard<std::mutex>g(mutex);auto it=rows.find(h.responseObject);if(Exact(it,h))rows.erase(it);}
 void ForgetMap(std::uint64_t map){std::lock_guard<std::mutex>g(mutex);for(auto it=rows.begin();it!=rows.end();)if(it->second.h.binding.task[0]==map)it=rows.erase(it);else ++it;}
 void ForgetEvent(TaskKey const&key){std::lock_guard<std::mutex>g(mutex);for(auto it=rows.begin();it!=rows.end();)if(it->second.h.binding.task==key)it=rows.erase(it);else ++it;}
 void Clear(){std::lock_guard<std::mutex>g(mutex);rows.clear();} // serial never reset
};
}
