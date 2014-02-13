#ifndef __EVENT_HH
#define __EVENT_HH

#include <vector>
#include <stdint.h>
//#include <inttypes.h>

#include "TObject.h"

#include "Settings.hh"

using namespace std;

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

class Germanium : public TObject {
public:
  Germanium(uint32_t eventTime, uint32_t eventNumber, uint16_t detector, uint16_t energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm);
  Germanium(){};
  ~Germanium(){};
private:
  uint32_t fEventTime;
  uint32_t fEventNumber;
  uint16_t fDetector;
  uint16_t fEnergy;
  vector<uint16_t> fSubAddress;
  vector<uint16_t> fTime;
  Ulm fUlm;

  ClassDef(Germanium,1);
};

class Plastic : public TObject {
public:
  Plastic(uint32_t eventTime, uint32_t eventNumber, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm);
  Plastic(){};
  ~Plastic(){};
private:
  uint32_t fEventTime;
  uint32_t fEventNumber;
  vector<uint16_t> fDetector;
  vector<uint16_t> fEnergy;
  vector<uint16_t> fSubAddress;
  vector<uint16_t> fTime;
  Ulm fUlm;

  ClassDef(Plastic,1);
};

class Silicon : public TObject {
public:
  Silicon(uint32_t eventTime, uint32_t eventNumber, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm);
  Silicon(){};
  ~Silicon(){};
private:
  uint32_t fEventTime;
  uint32_t fEventNumber;
  vector<uint16_t> fDetector;
  vector<uint16_t> fEnergy;
  vector<uint16_t> fSubAddress;
  vector<uint16_t> fTime;
  Ulm fUlm;

  ClassDef(Silicon,1);
};

class BaF2 : public TObject {
public:
  BaF2(uint32_t eventTime, uint32_t eventNumber, vector<uint16_t> detector, vector<uint16_t> energy, vector<uint16_t> subAddress, vector<uint16_t> time, Ulm ulm);
  BaF2(){};
  ~BaF2(){};
private:
  uint32_t fEventTime;
  uint32_t fEventNumber;
  vector<uint16_t> fDetector;
  vector<uint16_t> fEnergy;
  vector<uint16_t> fSubAddress;
  vector<uint16_t> fTime;
  Ulm fUlm;

  ClassDef(BaF2,1);
};

class Event : public TObject {
public:
  Event(){};
  ~Event(){};
private:
  vector<Germanium> fGermanium;
  vector<Plastic> fPlastic;
  vector<Silicon> fSilicon;
  vector<BaF2> fBaF2;
  ClassDef(Event,1);
};

#endif
