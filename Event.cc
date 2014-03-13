#include "Event.hh"

ClassImp(Ulm)
ClassImp(Detector)
ClassImp(Event)

//Adc::Adc(uint16_t detector, uint16_t rawEnergy)
//  : fDetector(detector), fRawEnergy(rawEnergy) {
//}
//
//Tdc::Tdc(uint16_t subAddress, uint16_t time)
//  : fSubAddress(subAddress), fTime(time) {
//}

Detector::Detector(uint32_t eventTime, uint32_t eventNumber,  uint8_t detectorType, std::pair<uint16_t, uint16_t> rawEnergy, Ulm ulm) 
  : fEventTime(eventTime), fEventNumber(eventNumber), fDetectorType(detectorType), fDetectorNumber(rawEnergy.first), fRawEnergy(rawEnergy.second), fUlm(ulm) {
  fTime = 0;
  fTdcHits = 0;
  fTdcHitsInWindow = 0;
}

Event::Event(const std::vector<Detector>& detectors)
  : fDetector(detectors) {
  for(auto& detector : fDetector) {
    ++fMultiplicity[detector.DetectorType()];
  }
}
