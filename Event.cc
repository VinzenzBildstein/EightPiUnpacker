#include "Event.hh"

#include <sstream>

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

std::string Detector::Print() const {
  std::stringstream str;

  switch(fDetectorType) {
  case 0:
    str<<"Germanium:  ";
    break;
  case 1:
    str<<"Plastic:    ";
    break;
  case 2:
    str<<"Silicon:    ";
    break;
  case 3:
    str<<"BaF2/LaBr3: ";
    break;
  case 4:
    str<<"Unknown:    ";
    break;
  }
  str<<fDetectorNumber<<"; event #"<<fEventNumber<<", time "<<fEventTime<<"; raw energy "<<fRawEnergy<<", time "<<fTime<<"; ulm clock "<<fUlm.Clock();

  return str.str();
}

Event::Event(const std::vector<Detector>& detectors)
  : fDetector(detectors) {
  for(auto& detector : fDetector) {
    ++fMultiplicity[detector.DetectorType()];
  }
}
