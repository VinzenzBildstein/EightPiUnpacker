#include "Event.hh"

ClassImp(Ulm)
ClassImp(Detector)
ClassImp(Event)

Adc::Adc(uint16_t detector, uint16_t rawEnergy)
  : fDetector(detector), fRawEnergy(rawEnergy) {
}

Tdc::Tdc(uint16_t subAddress, uint16_t time)
  : fSubAddress(subAddress), fTime(time) {
}

Detector::Detector(uint32_t eventTime, uint32_t eventNumber,  uint8_t detectorType, vector<uint16_t> detector, vector<uint16_t> rawEnergy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm) 
  : fEventTime(eventTime), fEventNumber(eventNumber), fDetectorType(detectorType), fUlm(ulm) {
  if(detector.size() != rawEnergy.size()) {
    throw;
  }
  for(size_t i = 0; i < detector.size(); ++i) {
    fAdc.push_back(Adc(detector[i],rawEnergy[i]));
  }

  if(subAddress.size() != time.size()) {
    throw;
  }
  for(size_t i = 0; i < subAddress.size(); ++i) {
    fTdc.push_back(Adc(subAddress[i],time[i]));
  }
}
