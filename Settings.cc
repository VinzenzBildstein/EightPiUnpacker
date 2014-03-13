#include "Settings.hh"

#include <iomanip>
#include <sstream>

#include "TEnv.h"

Settings::Settings(std::string settingsFileName, int verbosityLevel) {
  fVerbosityLevel = verbosityLevel;

  TEnv env;
  env.ReadFile(settingsFileName.c_str(),kEnvLocal);

  uint8_t detType;

  fUncalibratedBufferSize = env.GetValue("UncalibratedBufferSize", 1024);
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

  //-------------------- calibration and detector settings
  fSigma = env.GetValue("Calibration.Sigma",2.);
  fPeakThreshold = env.GetValue("Calibration.PeakThreshold",0.1);
  fNofDevonvIterations = env.GetValue("Calibration.NofDeconvIterations",10000);
  fNofFitIterations = env.GetValue("Calibration.NofFitIterations",1000);
  fFitConvergenceCoeff = env.GetValue("Calibration.FitConvergenceCoeff",0.1);

  if(fVerbosityLevel > 0) {
    std::cout<<"Settings are:"<<std::endl
	     <<"uncalibrated buffer size: \t"<<fUncalibratedBufferSize<<std::endl
	     <<"built events buffer size: \t"<<fBuiltEventsSize<<std::endl;
  }

  //get the number of peaks, their rough location, and their energies for each detector
  //germanium
  detType = static_cast<uint8_t>(EDetectorType::kGermanium);
  fMinimumCounts[detType] = env.GetValue("Calibration.Germanium.MinCounts",10000);
  fActiveDetectors[detType].resize(fNofGermaniumDetectors);
  fCoarseTdcWindows[detType].resize(fNofGermaniumDetectors);
  fNofPeaks[detType].resize(fNofGermaniumDetectors);
  fRoughWindow[detType].resize(fNofGermaniumDetectors);
  for(int i = 0; i < fNofGermaniumDetectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("Germanium.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("Germanium.%d.",i),true),env.GetValue(Form("Germanium.%d.Active",i),true));
    fNofPeaks[detType][i] = env.GetValue(Form("Calibration.Germanium.%d.NofPeaks",i),0);
    fRoughWindow[detType][i].resize(fNofPeaks[detType][i]);
    for(int j = 0; j < fNofPeaks[detType][i]; ++j) {
      fRoughWindow[detType][i][j] = std::make_pair(env.GetValue(Form("Calibration.Germanium.%d.%d.LowerLimit",i,j),0), env.GetValue(Form("Calibration.Germanium.%d.%d.UpperLimit",i,j),0));
      fEnergy[detType][i][j] = env.GetValue(Form("Calibration.Germanium.%d.%d.Energy",i,j),0.);
    }
  }
  if(fVerbosityLevel > 0) {
    std::cout<<"Germanium:"<<std::endl
	     <<"minimum counts: \t"<<fMinimumCounts[detType]<<std::endl;
  }
  //plastic
  detType = static_cast<uint8_t>(EDetectorType::kPlastic);
  fMinimumCounts[detType] = env.GetValue("Calibration.Plastic.MinCounts",10000);
  fActiveDetectors[detType].resize(fNofPlasticDetectors);
  fCoarseTdcWindows[detType].resize(fNofPlasticDetectors);
  fNofPeaks[detType].resize(fNofPlasticDetectors);
  fRoughWindow[detType].resize(fNofPlasticDetectors);
  for(int i = 0; i < fNofPlasticDetectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("Plastic.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("Plastic.%d.",i),true),env.GetValue(Form("Plastic.%d.Active",i),true));
    fNofPeaks[detType][i] = env.GetValue(Form("Calibration.Plastic.%d.NofPeaks",i),0);
    fRoughWindow[detType][i].resize(fNofPeaks[detType][i]);
    for(int j = 0; j < fNofPeaks[detType][i]; ++j) {
      fRoughWindow[detType][i][j] = std::make_pair(env.GetValue(Form("Calibration.Plastic.%d.%d.LowerLimit",i,j),0), env.GetValue(Form("Calibration.Plastic.%d.%d.UpperLimit",i,j),0));
      fEnergy[detType][i][j] = env.GetValue(Form("Calibration.Plastic.%d.%d.Energy",i,j),0.);
    }
  }
  if(fVerbosityLevel > 0) {
    std::cout<<"Plastic:"<<std::endl
	     <<"minimum counts: \t"<<fMinimumCounts[detType]<<std::endl;
  }
  //silicon
  detType = static_cast<uint8_t>(EDetectorType::kSilicon);
  fMinimumCounts[detType] = env.GetValue("Calibration.Silicon.MinCounts",10000);
  fActiveDetectors[detType].resize(fNofSiliconDetectors);
  fCoarseTdcWindows[detType].resize(fNofSiliconDetectors);
  fNofPeaks[detType].resize(fNofSiliconDetectors);
  fRoughWindow[detType].resize(fNofSiliconDetectors);
  for(int i = 0; i < fNofSiliconDetectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("Silicon.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("Silicon.%d.",i),true),env.GetValue(Form("Silicon.%d.Active",i),true));
    fNofPeaks[detType][i] = env.GetValue(Form("Calibration.Silicon.%d.NofPeaks",i),0);
    fRoughWindow[detType][i].resize(fNofPeaks[detType][i]);
    for(int j = 0; j < fNofPeaks[detType][i]; ++j) {
      fRoughWindow[detType][i][j] = std::make_pair(env.GetValue(Form("Calibration.Silicon.%d.%d.LowerLimit",i,j),0), env.GetValue(Form("Calibration.Silicon.%d.%d.UpperLimit",i,j),0));
      fEnergy[detType][i][j] = env.GetValue(Form("Calibration.Silicon.%d.%d.Energy",i,j),0.);
    }
  }
  if(fVerbosityLevel > 0) {
    std::cout<<"Silicon:"<<std::endl
	     <<"minimum counts: \t"<<fMinimumCounts[detType]<<std::endl;
  }
  //BaF2
  detType = static_cast<uint8_t>(detType);
  fMinimumCounts[detType] = env.GetValue("Calibration.BaF2.MinCounts",10000);
  fActiveDetectors[detType].resize(fNofBaF2Detectors);
  fCoarseTdcWindows[detType].resize(fNofBaF2Detectors);
  fNofPeaks[detType].resize(fNofBaF2Detectors);
  fRoughWindow[detType].resize(fNofBaF2Detectors);
  for(int i = 0; i < fNofBaF2Detectors; ++i) {
    fActiveDetectors[detType][i] = env.GetValue(Form("BaF2.%d.Active",i),true);
    fCoarseTdcWindows[detType][i] = std::make_pair(env.GetValue(Form("BaF2.%d.",i),true),env.GetValue(Form("BaF2.%d.Active",i),true));
    fNofPeaks[detType][i] = env.GetValue(Form("Calibration.BaF2.%d.NofPeaks",i),0);
    fRoughWindow[detType][i].resize(fNofPeaks[detType][i]);
    for(int j = 0; j < fNofPeaks[detType][i]; ++j) {
      fRoughWindow[detType][i][j] = std::make_pair(env.GetValue(Form("Calibration.BaF2.%d.%d.LowerLimit",i,j),0), env.GetValue(Form("Calibration.BaF2.%d.%d.UpperLimit",i,j),0));
      fEnergy[detType][i][j] = env.GetValue(Form("Calibration.BaF2.%d.%d.Energy",i,j),0.);
    }
  }
  if(fVerbosityLevel > 0) {
    std::cout<<"BaF2:"<<std::endl
	     <<"minimum counts: \t"<<fMinimumCounts[detType]<<std::endl;
  }
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
