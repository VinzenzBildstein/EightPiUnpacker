#ifndef __EVENT_HH
#define __EVENT_HH

#include <vector>
#include <stdint.h>
//#include <inttypes.h>

#include "TObject.h"

#include "Settings.hh"

using namespace std;

enum EDetectorType {
  kGermanium,
  kPlastic,
  kSilicon,
  kBaF2,
  kUnknown
};


class Ulm : public TObject {
public:
  Ulm(){};
  ~Ulm(){};

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
    fClock = clock;
  }
  void LiveClock(uint32_t liveClock) {
    fLiveClock = liveClock;
  }
  void MasterCount(uint32_t masterCount) {
    fMasterCount = masterCount;
  }

  uint16_t CycleNumber() {
    return fCycleNumber;
  }

private:
  uint16_t fCycleNumber;
  uint16_t fTriggerMask;
  bool fBeamStatus;
  uint32_t fClock;
  uint32_t fLiveClock;
  uint32_t fMasterCount;
  
  ClassDef(Ulm,1);
};

class Adc : public TObject {
public:
  Adc(uint16_t, uint16_t);
  Adc(){};
  ~Adc(){};

  void Energy(float energy) {
    fEnergy = energy;
  }

  uint16_t Detector() {
    return fDetector;
  }
  uint16_t RawEnergy() {
    return fRawEnergy;
  }
  float Energy() {
    return fEnergy;
  }

private:
  uint16_t fDetector;
  uint16_t fRawEnergy;
  float fEnergy;

  ClassDef(Adc,1);
};

class Tdc : public TObject {
public:
  Tdc(uint16_t, uint16_t);
  Tdc(){};
  ~Tdc(){};

  uint16_t SubAddress() {
    return fSubAddress;
  }
  uint16_t Time() {
    return fTime;
  }

private:
  uint16_t fSubAddress;
  uint16_t fTime;

  ClassDef(Tdc,1);
};

class Detector : public TObject {
public:
  Detector(uint32_t eventTime, uint32_t eventNumber, uint8_t detectorType, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm);
  Detector(){};
  ~Detector(){};

  uint32_t EventTime() {
    return fEventTime;
  }
  uint32_t EventNumber() {
    return fEventNumber;
  }
  uint8_t DetectorType() {
    return fDetectorType;
  }
  vector<Adc> GetAdc() {
    return fAdc;
  }
  vector<Tdc> GetTdc() {
    return fTdc;
  }
  Ulm GetUlm() {
    return fUlm;
  }

private:
  uint32_t fEventTime;
  uint32_t fEventNumber;
  uint8_t fDetectorType;//0 = germanium, 1 = plastic, 2 = silicon, 3 = BaF2/LaBr3
  vector<Adc> fAdc;
  vector<Tdc> fTdc;
  Ulm fUlm;

  ClassDef(Detector,1);
};

class Event : public TObject {
public:
  Event(){};
  ~Event(){};
private:
  vector<Detector> fDetector;
  ClassDef(Event,1);
};

#endif
