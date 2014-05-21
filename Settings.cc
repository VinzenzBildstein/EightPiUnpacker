#include "Settings.hh"

#include <iomanip>
#include <sstream>

#include "TEnv.h"

Settings::Settings(std::string settingsFileName, int verbosityLevel) {
  fVerbosityLevel = verbosityLevel;

  TEnv env;
  env.ReadFile(settingsFileName.c_str(),kEnvLocal);

  uint8_t detType;

  fBuiltEventsSize = env.GetValue("BuiltEventsSize", 1024);

  fTemperatureFileName = env.GetValue("TemperatureFileName","temperature.dat");
  
  fNofGermaniumDetectors = env.GetValue("Germanium.NofDetectors",20);
  fMaxGermaniumChannel = env.GetValue("Germanium.MaxChannel",16384);
  fNofPlasticDetectors = env.GetValue("Plastic.NofDetectors",20);
  fMaxPlasticChannel = env.GetValue("Plastic.MaxChannel",16384);
  fNofSiliconDetectors = env.GetValue("Silicon.NofDetectors",5);
  fMaxSiliconChannel = env.GetValue("Silicon.MaxChannel",16384);
  fNofBaF2Detectors = env.GetValue("BaF2.NofDetectors",10);
  fMaxBaF2Channel = env.GetValue("BaF2.MaxChannel",16384);

  //-------------------- detector settings
  if(fVerbosityLevel > 0) {
    std::cout<<"Settings are:"<<std::endl
	     <<"built events buffer size: \t"<<fBuiltEventsSize<<std::endl;
  }

  //get the number of peaks, their rough location, and their energies for each detector
  //germanium
  detType = static_cast<uint8_t>(EDetectorType::kGermanium);
  fActiveDetectors[detType].resize(fNofGermaniumDetectors,true);
  fCoarseTdcWindows[detType].resize(fNofGermaniumDetectors);
  for(int i = 0; i < fNofGermaniumDetectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("Germanium.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("Germanium.%d.",i),true),env.GetValue(Form("Germanium.%d.Active",i),true));
  }
  //plastic
  detType = static_cast<uint8_t>(EDetectorType::kPlastic);
  fActiveDetectors[detType].resize(fNofPlasticDetectors,true);
  fCoarseTdcWindows[detType].resize(fNofPlasticDetectors);
  for(int i = 0; i < fNofPlasticDetectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("Plastic.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("Plastic.%d.",i),true),env.GetValue(Form("Plastic.%d.Active",i),true));
  }
  //silicon
  detType = static_cast<uint8_t>(EDetectorType::kSilicon);
  fActiveDetectors[detType].resize(fNofSiliconDetectors,true);
  fCoarseTdcWindows[detType].resize(fNofSiliconDetectors);
  for(int i = 0; i < fNofSiliconDetectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("Silicon.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("Silicon.%d.",i),true),env.GetValue(Form("Silicon.%d.Active",i),true));
  }
  //BaF2
  detType = static_cast<uint8_t>(EDetectorType::kBaF2);
  fActiveDetectors[detType].resize(fNofBaF2Detectors,true);
  fCoarseTdcWindows[detType].resize(fNofBaF2Detectors);
  for(int i = 0; i < fNofBaF2Detectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("BaF2.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("BaF2.%d.",i),true),env.GetValue(Form("BaF2.%d.Active",i),true));
  }

  //-------------------- event building (times are in 100 ns)
  fWaitingWindow = env.GetValue("EventBuilding.WaitingWindow",10000000);//=1s
  fCoincidenceWindow = env.GetValue("EventBuilding.CoincidenceWindow",20);//=2us
}

//get detector type (as string) based on the bank name
std::string Settings::DetectorType(uint32_t bankName) {
  std::ostringstream result;

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
    result<<"Unknown event type 0x"<<std::hex<<bankName<<std::dec;
    break;
  }
  
  return result.str();
}

bool Settings::CoarseTdcWindow(const EDetectorType& detectorType, const uint16_t& detectorNumber, const uint16_t& channel) {
  if(fCoarseTdcWindows.find(static_cast<uint8_t>(detectorType)) == fCoarseTdcWindows.end()) {
    return false;
  }
  return (fCoarseTdcWindows[static_cast<uint8_t>(detectorType)][detectorNumber].first <= channel && channel <= fCoarseTdcWindows[static_cast<uint8_t>(detectorType)][detectorNumber].second);
}
