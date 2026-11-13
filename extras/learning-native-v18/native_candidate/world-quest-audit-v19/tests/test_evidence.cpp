#include "AuditEvidence.h"
#include <cassert>
#include <limits>
using namespace AutonomousPlayerAuditV19::Evidence;
int main() {
    ProgressBook b;Frame f;f.session=1;f.at=1;f.alive=true;f.task=42;
    assert(b.Observe(f).finding==Finding::Baseline);
    b.Path(false);b.Path(false);b.Path(false);f.at=302;
    assert(b.Observe(f).finding==Finding::SuspectedNavigationOrAICoverage);
    f.at=303;f.combat=true;assert(b.Observe(f).finding==Finding::Waiting);
    f.at=304;f.combat=false;f.instance=9;assert(b.Observe(f).finding==Finding::ContextChanged);
    f.at=305;f.objectives={{42,7,0,1,0,1,false}};b.Observe(f);
    f.at=306;f.objectives[0].complete=true;assert(b.Observe(f).finding==Finding::Progress);
    RecoveryBudget r;r.Alternative(false,1);r.Alternative(false,2);
    assert(!r.MayAttempt(30,true,true));assert(!r.MayAttempt(31,false,true));assert(!r.MayAttempt(31,true,false));
    assert(r.MayAttempt(31,true,true));r.Attempt(31);assert(!r.MayAttempt(32,true,true));
    assert(!r.TimedOut(60));assert(r.TimedOut(61));r.Rejected(61);
    assert(!r.MayAttempt(700,true,true)); // one attempt per goal, even with zero cooldown
    ProgressBook maintenance;Frame m;m.at=1;m.alive=true;m.task=42;m.needs=1;maintenance.Observe(m);
    maintenance.Path(false);maintenance.Path(false);maintenance.Path(false);m.at=400;
    assert(maintenance.Observe(m).finding==Finding::Waiting); // maintenance coverage incomplete, never navigation suspect
    m.at=401;m.needs=0;auto done=maintenance.Observe(m);assert(done.finding==Finding::Progress&&done.needsCleared==1);
    m.at=402;m.money=10;auto sale=maintenance.Observe(m);assert(sale.moneyDelta==10&&sale.finding==Finding::Progress);
    m.at=401;assert(maintenance.Observe(m).finding==Finding::InvalidEvidence);
    m.at=403;m.x=std::numeric_limits<float>::quiet_NaN();assert(maintenance.Observe(m).finding==Finding::InvalidEvidence);
    m.x=0;m.at=std::numeric_limits<double>::infinity();assert(maintenance.Observe(m).finding==Finding::InvalidEvidence);
    LoginBudget login;login.Start(100);assert(!login.Due(129));assert(login.Due(130));
    login.Backoff(130);assert(!login.Due(159));assert(login.Due(160));
    login.Pending(160);assert(!login.Due(189));assert(login.Due(190));
    for(unsigned i=0;i<10;++i)login.Backoff(200);
    assert(login.nextCheck==1100&&login.failures==6); // bounded 15-minute retry; no native lease cancellation
    login.Present(1100);assert(login.failures==0&&login.nextCheck==1130);
}
