// Optional OEM reference vectors; calibration remains private and is supplied at runtime.
#include "../probe/sensors/spx-lux.h"
#include <cassert>
#include <cmath>
int main(int argc,char**argv){
 assert(argc==2);spx::LuxCalibration cal;assert(cal.load(argv[1]));
 const float expected[]={43.3457718f,138.27771f,480.780701f,967.23999f,2375.94043f,6609.73535f,19390.2012f,54298.1133f,162809.375f};
 int v=1;float lux;
 for(float x:expected){assert(cal.convert(35,16,{v,v,v,v/4},lux));assert(std::abs(lux-x)<0.01f+std::abs(x)*0.000001f);v*=3;}
 assert(!cal.convert(35,16,{v,v,v,v/4},lux));
 assert(!cal.convert(35,16,{65535,1,1,1},lux));
 assert(!cal.convert(35,2,{1,1,1,1},lux));
 assert(!cal.convert(35,16,{-1,1,1,1},lux));
 spx::LuxCalibration missing;assert(!missing.load("/nonexistent/spx-light-calibration"));assert(!missing.convert(35,16,{1,1,1,1},lux));
}
