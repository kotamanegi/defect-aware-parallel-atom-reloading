
#include "simulator.h"
#include "noise.h"
#include <iostream>
#include <iomanip>
#include <cassert>
int main(){
std::cout<<std::setprecision(17);
for(double idle:{0.0,0.002,0.004,0.008,0.012,0.016,0.020}){
for(double op:{0.0,0.004,0.020}){
auto p=makeScaledParameters(3,idle,op,100,100);
TrialNoise noise(p); long losses=0, failures=0;
for(int s=0;s<100;s++){auto &x=noise.nextStep();for(int r=0;r<p.h;r++)for(int c=0;c<p.w;c++){failures+=x.reload_failure[r][c];losses+=x.idle_loss[r][c];}}
assert(idle == 0 ? losses == 0 : losses > 0);
assert(op == 0 ? failures == 0 : failures > 0);
for(int t=0;t<100;t++){p.trial_index=t;std::cout<<idle<<","<<op<<","<<t<<","<<runSimulation(p,1)<<"\n";}
}}}
