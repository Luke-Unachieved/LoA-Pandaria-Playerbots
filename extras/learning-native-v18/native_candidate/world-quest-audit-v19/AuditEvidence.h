#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>
namespace AutonomousPlayerAuditV19 { namespace Evidence {
struct Objective {
    std::uint32_t quest=0,id=0,type=0,object=0,count=0;
    std::int32_t required=0;
    bool complete=false;
};
struct Frame {
    std::uint64_t session=0,sequence=0;
    double at=0;
    unsigned map=0,instance=0,phase=0,level=0,xp=0,nextXp=0,goal=0,task=0,needs=0;
    std::uint64_t money=0;
    // XP, quest objectives, live maintenance needs, money and position are observed.
    // Skills, durability, inventory/trade receipts and equipment quality are not yet observed.
    unsigned observedCapabilities=31;
    float x=0,y=0,z=0;
    bool alive=false,combat=false,vehicle=false,known=true;
    std::vector<Objective> objectives;
};
enum class Finding : unsigned { Baseline=0, Progress=1, ContextChanged=2, Waiting=3,
    SuspectedNavigationOrAICoverage=4, MissingTemplate=5, NativeAcceptNotObserved=6,
    NativeRewardNotObserved=7, PrerequisiteBlocked=8, XpRateMismatch=9, InvalidEvidence=10 };
struct Verdict { Finding finding=Finding::Waiting; double quietSeconds=0; unsigned failedPaths=0;
    std::int64_t moneyDelta=0; unsigned needsCleared=0; };
class ProgressBook {
    Frame previous_; bool have_=false; double lastProgress_=0; unsigned failedPaths_=0;
public:
    void Path(bool reachable) { if(reachable) failedPaths_=0; else if(failedPaths_<1000) ++failedPaths_; }
    Verdict Observe(Frame const& current) {
        Verdict out;
        if(!std::isfinite(current.at)||!std::isfinite(current.x)||!std::isfinite(current.y)||!std::isfinite(current.z)||
           (have_&&current.at<previous_.at)) {
            have_=false;failedPaths_=0;lastProgress_=0;out.finding=Finding::InvalidEvidence;return out;
        }
        bool context=!have_ || current.session!=previous_.session || current.task!=previous_.task ||
            current.goal!=previous_.goal || current.map!=previous_.map || current.instance!=previous_.instance || current.phase!=previous_.phase;
        if(context) { lastProgress_=current.at; failedPaths_=0; out.finding=have_?Finding::ContextChanged:Finding::Baseline; }
        else {
            out.moneyDelta=std::int64_t(current.money)-std::int64_t(previous_.money);
            out.needsCleared=previous_.needs&~current.needs;
            bool progress=current.level>previous_.level || (current.level==previous_.level && current.xp>previous_.xp)||
                out.moneyDelta>0||out.needsCleared;
            for(auto const& x:current.objectives) for(auto const& old:previous_.objectives)
                if(x.quest==old.quest && x.id==old.id && (x.count>old.count || (x.complete&&!old.complete))) progress=true;
            // Moving while travelling is legitimate activity; a quest counter is not expected to rise every tick.
            double dx=current.x-previous_.x,dy=current.y-previous_.y,dz=current.z-previous_.z;
            if(dx*dx+dy*dy+dz*dz>=25) progress=true;
            if(progress) { lastProgress_=current.at; failedPaths_=0; out.finding=Finding::Progress; }
            out.quietSeconds=std::max(0.0,current.at-lastProgress_); out.failedPaths=failedPaths_;
            // At least three native failed path facts AND five quiet minutes. Still a SUSPECT, never server-bug proof.
            if(!progress && current.known && current.alive && !current.combat && !current.vehicle &&
                current.task && !current.needs && failedPaths_>=3 && out.quietSeconds>=300)
                out.finding=Finding::SuspectedNavigationOrAICoverage;
        }
        previous_=current;have_=true;return out;
    }
};
struct RecoveryBudget {
    unsigned failedAlternatives=0,attempts=0;
    bool pending=false,castObserved=false;
    double firstFailure=0,lastAlternative=0,attemptedAt=0,nextCheck=0;
    void Alternative(bool reachable,double now) {
        if(reachable) return; // reachable path is not proof it was traversed; preserve evidence only.
        if(!failedAlternatives) firstFailure=now;
        if(failedAlternatives<1000)++failedAlternatives;
        lastAlternative=now;
    }
    bool MayAttempt(double now,bool legal,bool cooldownReady)const {
        return !pending && attempts==0 && failedAlternatives>=2 && now-firstFailure>=30 &&
            now-lastAlternative<=120 && now>=nextCheck && legal && cooldownReady;
    }
    void Attempt(double now) { ++attempts;pending=true;attemptedAt=now;nextCheck=now+600; }
    void Rejected(double now) { pending=false;nextCheck=now+600; }
    bool TimedOut(double now)const{return pending && now-attemptedAt>=30;}
};
// World-thread login scheduling; the native per-GUID lease, not this timer, owns pending state.
struct LoginBudget {
    double nextCheck=0; unsigned failures=0;
    void Start(double now){failures=0;nextCheck=now+30;}
    bool Due(double now)const{return std::isfinite(now)&&now>=nextCheck;}
    void Present(double now){failures=0;nextCheck=now+30;}
    void Pending(double now){nextCheck=now+30;}
    void Backoff(double now){if(failures<6)++failures;nextCheck=now+std::min(900u,30u<<(failures-1));}
};
}}
