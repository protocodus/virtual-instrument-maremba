#include "EarlyReflectionScatter.h"
#include "CaptureGeometry.h"
#include "RoomCapture.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <vector>
namespace{int checks=0;void require(bool ok,const char*message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}}
int main(){
 double maxEnergyError=0,maxMagnitudeError=0,maxLateEnergy=0;size_t memoryAt192k=0;
 for(int room=0;room<3;++room)for(int ear=0;ear<2;++ear)for(int wall=0;wall<6;++wall){
  const double budget=.05*maremba::RoomCapture::Geometry(room,{0,0,0},ear)[wall+1].seconds;
  maremba::EarlyReflectionScatter scatter;const size_t size=scatter.Layout(budget,192000);memoryAt192k+=size;
  std::vector<float> memory(size+2,0.f);memory.front()=123.25f;memory.back()=-456.5f;float*ptr=memory.data()+1;
  for(double rate:{8000.,44100.,48000.,96000.,192000.}){
   scatter.Configure(budget,rate);scatter.Reset(ptr);const int frames=static_cast<int>(.15*rate);
   std::vector<float> h(frames);double energy=0,late=0,dc=0,energyMoment=0;
   for(int n=0;n<frames;++n){h[n]=scatter.Process(n==0?1.f:0.f,ptr);require(std::isfinite(h[n]),"finite response");energy+=double(h[n])*h[n];energyMoment+=n*double(h[n])*h[n];dc+=h[n];if(n>=int(.05*rate))late+=double(h[n])*h[n];}
   const double error=std::abs(energy-1.);maxEnergyError=std::max(maxEnergyError,error);maxLateEnergy=std::max(maxLateEnergy,late);
   require(error<2.e-6,"single reflection energy preserved");require(std::abs(dc-1)<2.e-6,"DC reflection gain and polarity preserved");
   require(h[0]>0,"physical onset retained");
   require(energy-double(h[0])*h[0]>.9*energy,"reflection must scatter energy beyond its initial sample");
   // For a unit-magnitude allpass, the impulse-energy centroid equals its
   // frequency-averaged group delay. This detects bypassed/incorrect delays.
   const double centroid=energyMoment/energy;
   require(std::abs(centroid-scatter.MeanGroupDelaySamples())<1.e-5+2.e-6*scatter.MeanGroupDelaySamples(),"energy centroid must match configured mean group delay");
   require(late<1.e-8,"local scattering may not create a long tail");
   for(int bin=0;bin<=32;++bin){std::complex<double> value=0;for(int n=0;n<frames;++n)if(h[n]!=0)value+=double(h[n])*std::polar(1.,-std::acos(-1.)*bin*n/32.);
    const double error=std::abs(std::abs(value)-1.);maxMagnitudeError=std::max(maxMagnitudeError,error);require(error<2.e-6,"single reflection frequency magnitude preserved");}
   for(size_t i=0;i<size;++i)require(std::fpclassify(ptr[i])!=FP_SUBNORMAL,"no subnormal delay state");
   scatter.Reset(ptr);for(size_t i=0;i<size;++i)require(ptr[i]==0.f,"reset clears full reserved span");
   for(int n=0;n<64;++n)require(scatter.Process(0,ptr)==0,"reset silence");
   require(memory.front()==123.25f&&memory.back()==-456.5f,"storage canaries intact");
  }
 }
 std::cout<<std::setprecision(12)<<"PASS checks="<<checks<<" maximum_energy_error="<<maxEnergyError<<" maximum_magnitude_error="<<maxMagnitudeError<<" maximum_energy_after50ms="<<maxLateEnergy<<" total_storage_bytes192k="<<4*memoryAt192k<<'\n';
}
