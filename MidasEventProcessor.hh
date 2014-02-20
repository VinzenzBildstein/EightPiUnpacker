#ifndef __MIDAS_EVENT_PROCESSOR_HH
#define __MIDAS_EVENT_PROCESSOR_HH

#include <thread>
#include <fstream>
#include <boost/circular_buffer.hpp>

#include "TTree.h"
#include "Event.hh"

#include "MidasFileManager.hh"
#include "Settings.hh"

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
  map<uint32_t,uint32_t> fLastUlmClock;
  map<uint32_t,uint32_t> fLastDeadTime;
  map<uint32_t,uint32_t> fLastLiveTime;
  map<uint32_t,uint32_t> fNofLiveTimeOverflows;
};

class MidasEventProcessor {
public:
  MidasEventProcessor(Settings*,TTree*);
  ~MidasEventProcessor();
  //use default moving constructor and assignment
  MidasEventProcessor(MidasEventProcessor&&) = default;
  //MidasEventProcessor& operator=(MidasEventProcessor&) = default;//creates error in gcc 4.7, should be fixed in 4.8???
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

  //process the different midas event types
  bool FifoEvent(MidasEvent&);
  bool CamacScalerEvent(MidasEvent&, vector<vector<uint16_t> >);
  bool EpicsEvent(MidasEvent&);

  //process the different detector types
  void GermaniumEvent(Bank&, size_t, uint32_t, uint32_t);
  void PlasticEvent(Bank&, size_t, uint32_t, uint32_t);
  void SiliconEvent(Bank&, size_t, uint32_t, uint32_t);
  void BaF2Event(Bank&, size_t, uint32_t, uint32_t);

  //process the different electronic modules
  bool GetAdc114(Bank&, uint32_t, uint16_t&);
  bool GetAdc413(Bank&, uint16_t, uint16_t, vector<uint16_t>&, vector<uint16_t>&);
  bool GetTdc3377(Bank&, uint32_t, vector<uint16_t>&, vector<uint16_t>&);
  bool GetAdc4300(Bank&, uint16_t, uint16_t, vector<uint16_t>&, vector<uint16_t>&);
  bool GetUlm(Bank&, Ulm&);

  Settings* fSettings;
  TTree* fTree;

  bool fFlushing = false;
  Event* fLeaf = nullptr;

  //variable to keep track of number of events per detector type
  map<uint16_t,uint32_t> fNofMidasEvents;
  //keep track how often a bank has appeared in the data
  map<uint32_t, uint32_t> fBankCounter;
  //keep track how often a fera type has appeared in the data
  map<uint16_t, uint32_t> fCounter;
  //keep track how often a tdc sub-address has appeared in the data
  map<uint16_t, uint32_t> fSubAddress;
  //scaler data
  vector<vector<uint16_t> > fMcs;
  //buffers to store events
  boost::circular_buffer<Detector> fUncalibratedDetector;
  boost::circular_buffer<Detector> fWaitingDetector;
  boost::circular_buffer<Detector> fCalibratedDetector;

  boost::circular_buffer<Event> fInputBuffer;
  boost::circular_buffer<Event> fCalibratedBuffer;
  boost::circular_buffer<Event> fOutputBuffer;
  //variables to keep track of last event
  uint32_t fLastEventNumber;
  uint32_t fLastEventTime;
  map<uint32_t,uint32_t> fLastFifoSerial;
  map<uint32_t,uint32_t> fNofZeros;
  //the threads that are being started in the constructor
  vector<thread> fThreads;
  //clock state
  ClockState fClockState;

  //cycle statistics
  size_t fNofCycles;
  uint16_t fLastCycle;
  size_t fEventsInCycle;

  //temperature output file
  ofstream fTemperatureFile;
};

#endif
