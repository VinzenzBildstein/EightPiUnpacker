#ifndef __EVENT_HH
#define __EVENT_HH

#include <vector>
#include <stdint.h>
//#include <inttypes.h>

#include "TObject.h"

#include "Settings.hh"

class Ulm : public TObject {
public:
  Ulm() {
    fCycleNumber = 0;
    fTriggerMask = 0;
    fBeamStatus = 0;
    fClock = 0;
    fLiveClock = 0;
    fMasterCount = 0;
  }
  ~Ulm(){};

  friend bool operator<(const Ulm& lh, const Ulm& rh) {
    return lh.fClock < rh.fClock;
  }
  friend bool operator>(const Ulm& lh, const Ulm& rh) {
    return lh.fClock > rh.fClock;
  }

  void Header(uint16_t header) {
    fCycleNumber = header&ULM_CYCLE;
    fTriggerMask = (header&ULM_TRIGGER_MASK)>>ULM_TRIGGER_MASK_OFFSET;
    if((header&ULM_BEAM_STATUS)>>ULM_BEAM_STATUS_OFFSET == 0) {
      fBeamStatus = false;
    } else {
      fBeamStatus = true;
    }
  }
  void Clock(uint32_t clock) {
    fClock = (uint64_t)clock;
  }
  void ClockOverflow(uint32_t overflow) {
    fClock = fClock | ((uint64_t)overflow)<<32;
  }
  void LiveClock(uint32_t liveClock) {
    fLiveClock = liveClock;
  }
  void MasterCount(uint32_t masterCount) {
    fMasterCount = masterCount;
  }

  uint16_t CycleNumber() const {
    return fCycleNumber;
  }
  uint16_t TriggerMask() const {
    return fTriggerMask;
  }
  bool BeamStatus() const {
    return fBeamStatus;
  }
  uint64_t Clock() const {
    return fClock;
  }
  uint32_t LiveClock() const {
    return fLiveClock;
  }
  uint32_t MasterCount() const {
    return fMasterCount;
  }


private:
  uint16_t fCycleNumber;
  uint16_t fTriggerMask;
  bool fBeamStatus;
  uint64_t fClock;//counts in 100ns steps
  uint32_t fLiveClock;
  uint32_t fMasterCount;
  
  ClassDef(Ulm,1);
};

class Detector : public TObject {
public:
  Detector(uint32_t eventTime, uint32_t eventNumber, uint8_t detectorType, std::pair<uint16_t, uint16_t> energy, Ulm ulm);
  Detector(){};
  ~Detector(){};

  friend bool operator<(const Detector& lh, const Detector& rh) {
    return lh.fUlm < rh.fUlm;
  }
  friend bool operator>(const Detector& lh, const Detector& rh) {
    return lh.fUlm > rh.fUlm;
  }

  void TdcHits(size_t tdcHits) {
    fTdcHits = tdcHits;
  }
  void Time(uint16_t time) {
    ++fTdcHitsInWindow;
    fTime = time;
  }
  void Energy(float energy) {
    fEnergy = energy;
  }
  
  uint32_t EventTime() {
    return fEventTime;
  }
  uint32_t EventNumber() {
    return fEventNumber;
  }
  uint8_t DetectorType() {
    return fDetectorType;
  }
  uint16_t DetectorNumber() {
    return fDetectorNumber;
  }
  uint16_t RawEnergy() {
    return fRawEnergy;
  }
  float Energy() {
    return fEnergy;
  }
  uint16_t Time() {
    return fTime;
  }
  size_t TdcHits() {
    return fTdcHits;
  }
  size_t TdcHitsInWindow() {
    return fTdcHitsInWindow;
  }
  const Ulm& GetUlm() const {
    return fUlm;
  }

private:
  uint32_t fEventTime;
  uint32_t fEventNumber;
  uint8_t fDetectorType;//0 = germanium, 1 = plastic, 2 = silicon, 3 = BaF2/LaBr3

  uint16_t fDetectorNumber;
  uint16_t fRawEnergy;
  float fEnergy;

  uint16_t fTime;
  size_t fTdcHits;
  size_t fTdcHitsInWindow;

  Ulm fUlm;

  ClassDef(Detector,1);
};

class Event : public TObject {
public:
  Event(const std::vector<Detector>&);
  Event(){};
  ~Event(){};

  size_t NofDetectors() {
    return fDetector.size();
  }
  Detector GetDetector(size_t index) {
    return fDetector.at(index);
  }

  int Multiplicity(const uint8_t& detector) {
    if(fMultiplicity.find(detector) == fMultiplicity.end()) {
      return -1;
    }
    return fMultiplicity[detector];
  }

private:
  std::vector<Detector> fDetector;
  std::map<uint8_t,int> fMultiplicity;
  ClassDef(Event,1);
};

#endif
