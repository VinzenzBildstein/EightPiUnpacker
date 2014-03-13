#ifndef __MIDAS_EVENT_PROCESSOR_HH
#define __MIDAS_EVENT_PROCESSOR_HH

#include <thread>
#include <fstream>
#include <set>
#include <boost/circular_buffer.hpp>

#include "TTree.h"
#include "Event.hh"

#include "MidasFileManager.hh"
#include "Settings.hh"
#include "Calibration.hh"

#define STANDARD_WAIT_TIME 10

class ClockState {
public:
  ClockState(uint32_t startTime = 0);
  ~ClockState(){};

  void Update(uint32_t);

  uint32_t NofStoredCycles() {
    return fNofStoredCycles;
  }

private:
  uint32_t fCycleStartTime;
  uint32_t fNofStoredCycles;
  std::map<uint32_t,uint32_t> fLastUlmClock;
  std::map<uint32_t,uint32_t> fLastDeadTime;
  std::map<uint32_t,uint32_t> fLastLiveTime;
  std::map<uint32_t,uint32_t> fNofLiveTimeOverflows;
};

class MidasEventProcessor {
public:
  MidasEventProcessor(Settings*,TTree*,bool);
  ~MidasEventProcessor();
  //use default moving constructor and assignment
  MidasEventProcessor(MidasEventProcessor&&) = default;
  MidasEventProcessor& operator=(MidasEventProcessor&) = default;
  //disallow copying constructor and assignment
  MidasEventProcessor(const MidasEventProcessor&) = delete;
  MidasEventProcessor& operator=(const MidasEventProcessor&) = delete;

  bool Process(MidasEvent&);

  void Flush();

  void Print();

private:
  //these member functions will be started as individual threads
  void Calibrate();
  void BuildEvents();
  void FillTree();
  void StatusUpdate();

  //process the different midas event types
  bool FifoEvent(MidasEvent&);
  bool CamacScalerEvent(MidasEvent&, std::vector<std::vector<uint16_t> >);
  bool EpicsEvent(MidasEvent&);

  //process the different detector types
  void GermaniumEvent(Bank&, size_t, uint32_t, uint32_t);
  void PlasticEvent(Bank&, size_t, uint32_t, uint32_t);
  void SiliconEvent(Bank&, size_t, uint32_t, uint32_t);
  void BaF2Event(Bank&, size_t, uint32_t, uint32_t);

  //process the different electronic modules
  bool GetAdc114(Bank&, uint32_t, uint16_t&);
  bool GetAdc413(Bank&, uint16_t, uint16_t, std::vector<std::pair<uint16_t, uint16_t> >&);
  bool GetTdc3377(Bank&, uint32_t, std::map<uint16_t, std::vector<uint16_t> >&);
  bool GetAdc4300(Bank&, uint16_t, uint16_t, std::vector<std::pair<uint16_t, uint16_t> >&);
  bool GetUlm(Bank&, Ulm&);

  void ConstructEvents(const uint32_t&, const uint32_t&, const EDetectorType&, std::vector<std::pair<uint16_t, uint16_t> >&, std::map<uint16_t, std::vector<uint16_t> >&, const Ulm&);

  enum EProcessStatus {
    kRun,
    kFlush
  };

private:
  Settings* fSettings;
  Calibration fCalibration;
  TTree* fTree;

  EProcessStatus fStatus;
  Event* fLeaf = nullptr;

  //variable to keep track of number of events per detector type
  std::map<uint16_t,uint32_t> fNofMidasEvents;
  //keep track how often a bank has appeared in the data
  std::map<uint32_t, uint32_t> fBankCounter;
  //keep track how often a fera type has appeared in the data
  std::map<uint16_t, uint32_t> fCounter;
  //keep track how often a tdc sub-address has appeared in the data
  std::map<uint16_t, uint32_t> fSubAddress;
  //keep track of dropped detectors (marked as inactive)
  std::map<uint8_t, std::map<uint16_t, uint32_t> > fDroppedDetector;
  size_t fNofUncalibratedDetectors;
  size_t fNofWaiting;
  size_t fNofCalibratedDetectors;
  size_t fNofBuiltEvents;

  //scaler data
  std::vector<std::vector<uint16_t> > fMcs;
  //buffers to store detectors/events
  boost::circular_buffer<Detector> fUncalibratedDetector;
  std::map<uint8_t,std::vector<boost::circular_buffer<Detector> > > fWaiting;
  std::multiset<Detector> fCalibratedDetector;
  boost::circular_buffer<Event> fBuiltEvents;

  //calibration histograms
  std::vector<std::vector<TH1I*> > fRawEnergyHistograms;
  //variables to keep track of last event
  uint32_t fLastEventNumber;
  uint32_t fLastEventTime;
  std::map<uint32_t,uint32_t> fLastFifoSerial;
  std::map<uint32_t,uint32_t> fNofZeros;
  //the threads that are being started in the constructor
  std::vector<std::thread> fThreads;
  //clock state
  ClockState fClockState;

  //cycle statistics
  size_t fNofCycles;
  uint16_t fLastCycle;
  size_t fEventsInCycle;

  //temperature output file
  std::ofstream fTemperatureFile;
};

#endif
