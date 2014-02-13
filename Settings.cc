#include "Settings.hh"

#include <iomanip>
#include <sstream>

#include "TEnv.h"

using namespace std;

Settings::Settings(string settingsFileName, int verbosityLevel) {
  fVerbosityLevel = verbosityLevel;

  TEnv env;
  env.ReadFile(settingsFileName.c_str(),kEnvLocal);

  fTemperatureFileName = env.GetValue("TemperatureFileName","temperature.dat");
  
  fNofGermaniumDetectors = env.GetValue("Germanium.NofDetectors",20);
  fNofPlasticDetectors = env.GetValue("Plastic.NofDetectors",20);
  fNofSiliconDetectors = env.GetValue("Silicon.NofDetectors",5);
  fNofBaF2Detectors = env.GetValue("BaF2.NofDetectors",10);
}

//get detector type (as string) based on the bank name
string Settings::DetectorType(uint32_t bankName) {
  ostringstream result;

  switch(bankName) {
  case FME_ZERO:
    result<<"Germanium";
    break;
    
  case FME_ONE:
    result<<"Plastic";
    break;
    
  case FME_TWO:
    result<<"BariumFluoride";
    break;
    
  case FME_THREE:
    result<<"Silicon";
    break;
    
  default:
    result<<"Unknown event type 0x"<<hex<<bankName;
    break;
  }
  
  return result.str();
}
