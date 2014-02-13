#include "Event.hh"

ClassImp(Ulm)
ClassImp(Germanium)
ClassImp(Plastic)
ClassImp(Silicon)
ClassImp(BaF2)
ClassImp(Event)

Germanium::Germanium(uint32_t eventTime, uint32_t eventNumber, uint16_t detector, uint16_t energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm) 
: fEventTime(eventTime), fEventNumber(eventNumber), fEnergy(energy), fSubAddress(subAddress), fTime(time), fUlm(ulm) {
}

Plastic::Plastic(uint32_t eventTime, uint32_t eventNumber, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm) 
: fEventTime(eventTime), fEventNumber(eventNumber), fEnergy(energy), fSubAddress(subAddress), fTime(time), fUlm(ulm) {
}

Silicon::Silicon(uint32_t eventTime, uint32_t eventNumber, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm) 
: fEventTime(eventTime), fEventNumber(eventNumber), fEnergy(energy), fSubAddress(subAddress), fTime(time), fUlm(ulm) {
}

BaF2::BaF2(uint32_t eventTime, uint32_t eventNumber, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm) 
: fEventTime(eventTime), fEventNumber(eventNumber), fEnergy(energy), fSubAddress(subAddress), fTime(time), fUlm(ulm) {
}
