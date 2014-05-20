#include "MidasEventProcessor.hh"

#include <iomanip>
#include <sstream>

#include "TROOT.h"
#include "TFile.h"
#include "TDirectory.h"
#include "TF1.h"
#include "TList.h"
#include "TStopwatch.h"

#include "Utilities.hh"
#include "TextAttributes.hh"

#include "MidasFileManager.hh"

//make MidasEventProcessor a singleton???
MidasEventProcessor::MidasEventProcessor(Settings* settings, TFile* file, TTree* tree, bool statusUpdate) {  
  fSettings = settings;
  fRootFile = file;
  fTree = tree;
  fStatus = kRun;
  fCalibration.SetSettings(settings);

  //attach leaf to tree
  int BufferSize = 1024000;
  fTree->Branch("Event",&fLeaf, BufferSize);
  fTree->BranchRef();

  //increase maximum tree size to 10GB
  Long64_t GByte = 1073741824L;
  fTree->SetMaxTreeSize(10*GByte);

  fLastCycle = 0;
  fEventsInCycle = 0;

  fNofUncalibratedDetectors = 0;
  fNofWaiting = 0;
  fNofCalibratedDetectors = 0;
  fNofBuiltEvents = 0;

  fTemperatureFile.open(fSettings->TemperatureFile());

  //set size of circular buffer for uncalibrated detectors
  fUncalibratedDetector.set_capacity(fSettings->UncalibratedBufferSize());

  //set size of circular buffer for built events
  fBuiltEvents.set_capacity(fSettings->BuiltEventsSize());

  //for each detector type:
  uint8_t detType;

  //create energy calibration histograms (4 detector type: ge, pl, si, and baf2)
  fRawEnergyHistograms.resize(4);
  //create buffer for detectors awaiting calibration (map of vectors of circular buffers)
  //we set the size of the circular buffer to be 10% larger than the maximum number of events we require to calibrate (to give some room if the event building is slow)

  detType = static_cast<uint8_t>(EDetectorType::kGermanium);
  fWaiting[detType].resize(fSettings->NofGermaniumDetectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fWaitingMutex[detType];
  fRawEnergyHistograms[detType].resize(fSettings->NofGermaniumDetectors());
  for(int det = 0; det < fSettings->NofGermaniumDetectors(); ++det) {
    //fWaitingMutex[detType].emplace_back(std::mutex());
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawGermanium_%d",(int)det),Form("rawGermanium_%d",(int)det),
						  fSettings->MaxGermaniumChannel(),0.,(double)fSettings->MaxGermaniumChannel());
  }

  detType = static_cast<uint8_t>(EDetectorType::kPlastic);
  fWaiting[detType].resize(fSettings->NofPlasticDetectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fWaitingMutex[detType];
  fRawEnergyHistograms[detType].resize(fSettings->NofPlasticDetectors());
  for(int det = 0; det < fSettings->NofPlasticDetectors(); ++det) {
    //fWaitingMutex[detType].emplace_back(std::mutex());
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawPlastic_%d",(int)det),Form("rawPlastic_%d",(int)det),
						  fSettings->MaxPlasticChannel(),0.,(double)fSettings->MaxPlasticChannel());
  }

  detType = static_cast<uint8_t>(EDetectorType::kSilicon);
  fWaiting[detType].resize(fSettings->NofSiliconDetectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fWaitingMutex[detType];
  fRawEnergyHistograms[detType].resize(fSettings->NofSiliconDetectors());
  for(int det = 0; det < fSettings->NofSiliconDetectors(); ++det) {
    //fWaitingMutex[detType].emplace_back(std::mutex());
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawSilicon_%d",(int)det),Form("rawSilicon_%d",(int)det),
						  fSettings->MaxSiliconChannel(),0.,(double)fSettings->MaxSiliconChannel());
  }

  detType = static_cast<uint8_t>(EDetectorType::kBaF2);
  fWaiting[detType].resize(fSettings->NofBaF2Detectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fWaitingMutex[detType];
  fRawEnergyHistograms[detType].resize(fSettings->NofBaF2Detectors());
  for(int det = 0; det < fSettings->NofBaF2Detectors(); ++det) {
    //fWaitingMutex[detType].emplace_back(std::mutex());
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawBaF2_%d",(int)det),Form("rawBaF2_%d",(int)det),
						  fSettings->MaxBaF2Channel(),0.,(double)fSettings->MaxBaF2Channel());
  }

  if(fSettings->VerbosityLevel() > 2) {
    fDataFile.open("Data.dat");
  }


  //-------------------- the threads
  //is this done best with async and yield, or should I use threads and promises, or maybe condition variables
  //seems that ayncs lets some threads "disappear", i.e. they're not scheduled anymore

  //start calibration thread (takes events from the input buffer, calibrates them and puts them into the calibrated buffer)
  fThreads.push_back(std::make_pair(0,std::async(std::launch::async, &MidasEventProcessor::Calibrate, this)));
  //start event building thread (takes events from calibrated buffer and combines them into build events in the output buffer)
  fThreads.push_back(std::make_pair(1,std::async(std::launch::async, &MidasEventProcessor::BuildEvents, this)));
  //start output thread (writes event in the output buffer to file/tree)
  fThreads.push_back(std::make_pair(2,std::async(std::launch::async, &MidasEventProcessor::FillTree, this)));
  if(statusUpdate) {
    fThreads.push_back(std::make_pair(3,std::async(std::launch::async, &MidasEventProcessor::StatusUpdate, this)));
  }
}

MidasEventProcessor::~MidasEventProcessor() {
  fTemperatureFile.close();
  if(fDataFile.is_open()) {
    fDataFile.close();
  }
}

bool MidasEventProcessor::Process(MidasEvent& event) {
  //increment the count for this type of event, no matter what type it is
  //if this is the first time we encounter this type, it will automatically be inserted
  fNofMidasEvents[event.Type()]++;
  //choose the different methods based on the event type
  //events added to the input buffer are automatically being calibrated, combined, and written to file via the threads started in the constructor
  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<Show("Processing midas event ",event.Number()," of type 0x",std::hex,event.Type(),std::dec)<<std::endl;
  }
  switch(event.Type()) {
  case FIFOEVENT:
    if(!FifoEvent(event)) {
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Bad FIFO event.",Attribs::Reset())<<std::endl;
      }
      return false;
    }
    break;

  case CAMACSCALEREVENT:
    CamacScalerEvent(event, fMcs);
    //end of cycle
    fClockState.Update(event.Time());
    //#ifdef CYCLE_MODE
    //    //end
    //
    //    //Tell the event constructor that this event marks the end of a cycle.
    //    cycleendevent = createCycleEndEvent(event);
    //    gainCorrect(cycleendevent, &correctedgeneraleventqueue);
    //
    //    //Update the cycle start time.
    //    updateClockStateForNewCycle(&clockstate, cycleendevent->eventtime);
    //    printf("Cycle %d started at: %u\n", cyclenumber, clockstate.cyclestarttime);
    //
    //    cyclenumber++;  //This only keeps track of the number of cycleendevents added to queue - the actual output could be way behind.
    //
    //#endif
    break;

  case SCALERSCALEREVENT:
    CamacScalerEvent(event, fMcs);
    break;

  case ISCALEREVENT:
    break;

  case FRONTENDEVENT:
    //			frontEndEvent(currentevent);
    //Tell the event constructor that this event marks the end of a cycle.
    //dvmevent = createFrontEndEvent(event, clockstate.cyclestarttime);
    //gainCorrect(dvmevent, &correctedgeneraleventqueue);
    break;

  case EPICSEVENTTYPE:
    EpicsEvent(event);
    break;

  case FILEEND:
    if(fSettings->VerbosityLevel() > 0) {
      std::cout<<Show("Reached file end, got ",fClockState.NofStoredCycles()," cycles.")<<std::endl;
    }
    Flush();
    break;

  default:
    //Unrecognized event type.
    std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Unknown event type 0x",std::hex,event.Type(),std::dec," for midas event #",event.Number(),Attribs::Reset())<<std::endl;
    return false;
  }

  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<Show("done processing midas event: ",ThreadStatus())<<std::endl;
  }

  return true;
}

//---------------------------------------- different midas event types ----------------------------------------
bool MidasEventProcessor::FifoEvent(MidasEvent& event) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Found FIFO event in midas event ",event.Number())<<std::endl;
  }
  uint32_t fifoStatus = 0;
  uint32_t feraWords = 0;
  uint32_t fifoSerial = 0;

  size_t feraEnd = 0;

  size_t currentFeraStart;

  if(fSettings->VerbosityLevel() > 1) {
    //check for missed events
    if(event.Number() != (fLastEventNumber + 1)) {
      std::cerr<<Show(Foreground::Red(),"Missed ",event.Number() - fLastEventNumber - 1," FIFO data events, between events ",fLastEventNumber," and ",event.Number(),Attribs::Reset())<<std::endl;
    }

    //Check if events are ordered by time
    if(event.Time() < fLastEventTime) {
      std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"FIFO event ",event.Number()," occured before the last event ",fLastEventNumber," (",event.Time()," < ",fLastEventTime,")",Attribs::Reset())<<std::endl;
    }
  }

  fLastEventNumber = event.Number();
  fLastEventTime = event.Time();

  //loop over banks
  for(auto bank : event.Banks()) {
    if(bank.Size() == 0) {
      continue;
    }

    while(bank.GotData()) {
      //Note: If there's multiple ferastreams in the bank, then this loop will run both of them.
      //Further, in that case it will call the same event type multiple times, as the event type is in the bank header.
      currentFeraStart = bank.ReadPoint();

      //Check if it's a good FIFO event
      bank.Get(fifoStatus);

      //Check if this FIFO event is valid
      if((fifoStatus != GOODFIFO1) && (fifoStatus != GOODFIFO2)) {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Invalid FIFO status ",std::hex,std::setw(8),std::setfill('0'),fifoStatus,std::dec," in event ",event.Number(),Attribs::Reset())<<std::endl;
	}
	//just continue???
	continue;
      }

      bank.Get(feraWords);

      //Check timeout and overflow bit in ferawords.
      if(feraWords & 0x0000C000) {
	if(fSettings->VerbosityLevel() > 1) {
	  std::cerr<<Show(Foreground::Red(),"Event ",event.Number(),", bank ",bank.Number(),": FIFO overflow bit or timeout bit set: ",((feraWords>>14) & 0x3),Attribs::Reset())<<std::endl;
	}
      }

      //get the number of fera words
      feraWords = feraWords & FERAWORDS;

      //Set feraEnd, need to account for the header words and the (not yet read) fifo serial
      feraEnd = bank.ReadPoint()+2*feraWords + 4;

      //Check if feraWords will fit in the buffer.
      //feraEnd and readpoint count bytes, whereas size counts 32bit words!
      if(feraEnd  > 2*bank.Size()) {
	//Not enough room for ferawords in bankbuffer.
	bank.SetReadPoint(2*bank.Size());
	continue;
      }

      //Get fifoserial
      bank.Get(fifoSerial);

      //only the last byte contains information
      fifoSerial = fifoSerial & 0xFF;

      //increase counter and check serial for all banks
      ++(fBankCounter[bank.IntName()]);
      if(fifoSerial != ((fLastFifoSerial[bank.IntName()]+1) & 0xff)) {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cerr<<Show(Foreground::Red(),"Missed a ",fSettings->DetectorType(bank.IntName())," FIFO serial in Event ",event.Number(),", Bank ",bank.Number(),", FIFO serial ",fifoSerial,", last FIFO serial ",fLastFifoSerial[bank.IntName()],Attribs::Reset())<<std::endl;
	}
      }
      fLastFifoSerial[bank.IntName()] = fifoSerial;

      //Now, do different things depending on the type of detector triggered.
      switch(bank.IntName()) {
      case FME_ZERO:
	GermaniumEvent(bank, feraEnd, event.Time(), event.Number());
	break;

      case FME_ONE:
	PlasticEvent(bank, feraEnd, event.Time(), event.Number());
	break;

      case FME_TWO:
	BaF2Event(bank, feraEnd, event.Time(), event.Number());
	break;

      case FME_THREE:
	SiliconEvent(bank, feraEnd, event.Time(), event.Number());
	break;

      default:
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Unknown bank name 0x",std::hex,bank.IntName(),std::dec," for bank ",bank.Number()," in midas event ",event.Number(),Attribs::Reset())<<std::endl;
	break;
      }

      //Make sure that the readpoint is at the end of the fera data
      //Readpoint should be offset by 12 bytes for the fera header, and 2 bytes per each fera word.
      //If the number of fera words is odd, pad with an additional word.
      bank.SetReadPoint(currentFeraStart + 2*(feraWords + (feraWords%2)) + 12);

      //If we're at the end of the bank, increment ferawords by 2 to bypass the junk.
      if(bank.ReadPoint() == (bank.Size() - 2)) {
	bank.SetReadPoint(bank.Size());
      }
    }//while(bank.GotData())
  }//loop over banks

  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"FIFO event done"<<std::endl;
  }

  return true;
}

bool MidasEventProcessor::CamacScalerEvent(MidasEvent& event, std::vector<std::vector<uint16_t> > mcs) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Found Scaler event in midas event ",event.Number())<<std::endl;
  }
  uint32_t tmp;
  //loop over banks
  for(auto bank : event.Banks()) {
    if(bank.IsBank("MCS0")) {
      //reset mcs
      mcs.resize(NOF_MCS_CHANNELS,std::vector<uint16_t>());
      for(int i = 0; bank.GotBytes(2); ++i) {
	bank.Get(tmp);
	//tmp = ((tmp<<16)&0xffff0000) | ((tmp>>16)&0xffff);
	mcs[i%NOF_MCS_CHANNELS].push_back(tmp);
      }
      //done (there shouldn't be another scaler bank)???
      break;
    }
  }

  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Scaler event done"<<std::endl;
  }

  return true;
}

bool MidasEventProcessor::EpicsEvent(MidasEvent& event) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Found epics event in midas event ",event.Number())<<std::endl;
  }
  float tmp;

  //loop over banks or just get the right bank?
  //for(auto& bank : event.Banks()) {
  Bank bank = event.Banks()[1];
  for(int j = 0; bank.GotData(); ++j) {
    bank.Get(tmp);
    //tmp = ((tmp<<16)&0xffff0000) | ((tmp>>16)&0xffff);
    if(j==14) {
      fTemperatureFile<<tmp<<std::endl;
      break;
    }
  }
  //}

  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Epics event done"<<std::endl;
  }

  return true;
}

//---------------------------------------- different detector types ----------------------------------------

void MidasEventProcessor::GermaniumEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Starting on germanium event ",eventNumber)<<std::endl;
  }

  uint16_t header;
  uint16_t vsn;
  uint16_t feraType;
  uint16_t tmpEnergy;
  std::vector<std::pair<uint16_t, uint16_t> > energy;
  std::map<uint16_t, std::vector<uint16_t> > time;
  Ulm ulm;

  while(bank.ReadPoint() < feraEnd) {
    bank.Get(header);

    //skip all zeros
    while(header == 0 && bank.ReadPoint() < feraEnd) {
      //increment counter and get next word
      ++fNofZeros[bank.IntName()];
      bank.Get(header);
    }

    //get the module number
    vsn = header & VHNMASK;

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA number = ",vsn)<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    //std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<header<<")"<<std::dec<<std::endl;
    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA type = 0x",std::hex,feraType," (from  0x",header,")",std::dec)<<std::endl;
    }

    switch(feraType) {
    case VHAD1141:
      //Process the ADC, and check if it's followed immediately by a TDC
      if(vsn >= fSettings->NofGermaniumDetectors()) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Invalid detector number (",vsn,") in Event ",bank.EventNumber(),", Bank ",bank.Number(),Attribs::Reset())<<std::endl;
      }
      
      if(GetAdc114(bank, feraEnd, tmpEnergy)) {
	GetTdc3377(bank, feraEnd, time);
	++fCounter[VH3377];
      }
      energy.push_back(std::make_pair(vsn, tmpEnergy));
      ++fCounter[VHAD1141];
      break;

    case VHAD1142:
      //Process the ADC, and check if it's followed immediately by a TDC
      if((vsn + 16) >= fSettings->NofGermaniumDetectors()) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Invalid detector number (",(vsn + 16),") in Event ",bank.EventNumber(),", Bank ",bank.Number(),Attribs::Reset())<<std::endl;
      }

      if(GetAdc114(bank, feraEnd, tmpEnergy)) {
	GetTdc3377(bank, feraEnd, time);
	++fCounter[VH3377];
      }
      energy.push_back(std::make_pair(vsn + 16, tmpEnergy));
      ++fCounter[VHAD1142];
      break;

    case VH3377: /* 3377 TDC */
      GetTdc3377(bank, feraEnd, time);
      ++fCounter[VH3377];
      break;

    case VHFULM:  /* Universal Logic Module end of event marking, clocks, etc.. */
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      if(ulm.CycleNumber() != fLastCycle && fLastCycle != 0) {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<Show(ulm.CycleNumber(),". cycle: ",fEventsInCycle," events in last cycle")<<std::endl;
	}
	fEventsInCycle = 0;
      } else {
	++fEventsInCycle;
      }
      fLastCycle = ulm.CycleNumber();
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Bad germanium fera",Attribs::Reset())<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;

    default: /* Unrecognized header */
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to find FERA header in germanium midas event ",eventNumber,", found 0x",std::hex,feraType," from header 0x",header,std::dec," instead",Attribs::Reset())<<std::endl;
      }
      ++fNofUnkownFera[bank.IntName()];
      //try and find the next header
      bank.Get(header);
      //skip all words until we find one with the high bit set
      while((header & 0x8000) == 0 && bank.ReadPoint() < feraEnd) {
	//increment counter and get next word
	bank.Get(header);
      }
      //un-read the header (will be read again in the next iteration of the while-loop_
      bank.ChangeReadPoint(-1);
      break;
    }
  }

  if(ulm.Clock() != 0 || ulm.CycleNumber() != 0) { 
    //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
    ConstructEvents(eventTime, eventNumber, EDetectorType::kGermanium, energy, time, ulm);
  } else if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Discarding event with ulm clock 0, ",energy.size()," adcs, and ",time.size()," tdcs")<<std::endl;
  }
}

void MidasEventProcessor::PlasticEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Starting on plastic event ",eventNumber)<<std::endl;
  }

  uint16_t header;
  uint16_t vsn;
  uint16_t feraType;
  std::vector<std::pair<uint16_t, uint16_t> > energy;
  std::map<uint16_t, std::vector<uint16_t> > time;
  Ulm ulm;

  while(bank.ReadPoint() < feraEnd) {
    bank.Get(header);

    //skip all zeros
    while(header == 0 && bank.ReadPoint() < feraEnd) {
      //increment counter and get next word
      ++fNofZeros[bank.IntName()];
      bank.Get(header);
    }

    //get the module number
    vsn = header & VHNMASK;

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA number = ",vsn)<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    //std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<header<<")"<<std::dec<<std::endl;
    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA type = 0x",std::hex,feraType," (from  0x",header,")",std::dec)<<std::endl;
    }

    switch(feraType) {
    case VH4300: //SCEPTAR ENERGY FERA
      GetAdc4300(bank, header, vsn, energy);
      ++fCounter[VH4300];
      break;		    

    case VH3377: // 3377 TDC
      GetTdc3377(bank, feraEnd, time);
      ++fCounter[VH3377];
      break;

    case VHFULM:  // Universal Logic Module end of event marking, clocks, etc..
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Found bad fera event in plastic data stream",Attribs::Reset())<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;

    default: // Unrecognized header
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to find FERA header in plastic midas event ",bank.EventNumber(),", found 0x",std::hex,feraType," from header 0x",header,std::dec," instead",Attribs::Reset())<<std::endl;
      }
      ++fNofUnkownFera[bank.IntName()];
      //try and find the next header
      bank.Get(header);
      //skip all words until we find one with the high bit set
      while((header & 0x8000) == 0 && bank.ReadPoint() < feraEnd) {
	//increment counter and get next word
	bank.Get(header);
      }
      //un-read the header (will be read again in the next iteration of the while-loop_
      bank.ChangeReadPoint(-1);
      break;
    }
  }

  if(ulm.Clock() != 0 || ulm.CycleNumber() != 0) { 
    //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
    ConstructEvents(eventTime, eventNumber, EDetectorType::kPlastic, energy, time, ulm);
  } else if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Discarding event with ulm clock 0, ",energy.size()," adcs, and ",time.size()," tdcs")<<std::endl;
  }
}

void MidasEventProcessor::SiliconEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Starting on silicon event ",eventNumber)<<std::endl;
  }

  uint16_t header;
  uint16_t vsn;
  uint16_t feraType;
  int nofAdcs = 0;
  uint16_t tmpEnergy;
  std::vector<std::pair<uint16_t, uint16_t> > energy;
  std::map<uint16_t, std::vector<uint16_t> > time;
  Ulm ulm;

  while(bank.ReadPoint() < feraEnd) {
    bank.Get(header);

    //skip all zeros
    while(header == 0 && bank.ReadPoint() < feraEnd) {
      //increment counter and get next word
      ++fNofZeros[bank.IntName()];
      bank.Get(header);
    }

    //get the module number
    vsn = header & VHNMASK;

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA number = ",vsn)<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    //std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<header<<")"<<std::dec<<std::endl;
    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA type = 0x",std::hex,feraType," (from  0x",header,")",std::dec)<<std::endl;
    }

    switch(feraType) {
    case VHAD413:
      //vsn is 0xD or 0xE (13 or 14), so to get the module number we subtract 13
      if(!GetAdc413(bank, vsn-13, (header&VHAD413_NUMBER_OF_DATA_WORDS_MASK)>>VHAD413_DATA_WORDS_OFFSET, energy)) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Same problem with something immediately after ADC 413 data in silicon data stream",Attribs::Reset())<<std::endl;
      }
      ++fCounter[VHAD413];
      ++nofAdcs;
      break;	
    case VHAD114Si:
      //Process the ADC, and check if it's followed immediately by a TDC
      if(vsn > fSettings->NofSiliconDetectors()) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Invalid detector number (",vsn,") in Event ",bank.EventNumber(),", Bank ",bank.Number(),Attribs::Reset())<<std::endl;
      }

      if(GetAdc114(bank, feraEnd, tmpEnergy)) {
	GetTdc3377(bank, feraEnd, time);
	++fCounter[VH3377];
      }
      energy.push_back(std::make_pair(vsn, tmpEnergy));
      ++fCounter[VHAD114Si];
      break;		    

    case VH3377:
      GetTdc3377(bank, feraEnd, time);
      ++fCounter[VH3377];
      break;

    case VHFULM:  /* Universal Logic Module end of event marking, clocks, etc.. */
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Found bad fera event in silicon data stream",Attribs::Reset())<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;
		    
    default: // Unrecognized header
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to find FERA header in silicon midas event ",bank.EventNumber(),", found 0x",std::hex,feraType," from header 0x",header,std::dec," instead",Attribs::Reset())<<std::endl;
      }
      ++fNofUnkownFera[bank.IntName()];
      //try and find the next header
      bank.Get(header);
      //skip all words until we find one with the high bit set
      while((header & 0x8000) == 0 && bank.ReadPoint() < feraEnd) {
	//increment counter and get next word
	bank.Get(header);
      }
      //un-read the header (will be read again in the next iteration of the while-loop_
      bank.ChangeReadPoint(-1);
      break;
    }//switch
  }

  if(ulm.Clock() != 0 || ulm.CycleNumber() != 0) { 
    //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
    ConstructEvents(eventTime, eventNumber, EDetectorType::kSilicon, energy, time, ulm);
  } else if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Discarding event with ulm clock 0, ",energy.size()," adcs, and ",time.size()," tdcs")<<std::endl;
  }
}

void MidasEventProcessor::BaF2Event(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Starting on barium fluoride event ",eventNumber)<<std::endl;
  }

  uint16_t header;
  uint16_t vsn;
  uint16_t feraType;
  std::vector<std::pair<uint16_t, uint16_t> > energy;
  std::map<uint16_t, std::vector<uint16_t> > time;
  Ulm ulm;

  while(bank.ReadPoint() < feraEnd) {
    bank.Get(header);

    //skip all zeros
    while(header == 0 && bank.ReadPoint() < feraEnd) {
      //increment counter and get next word
      ++fNofZeros[bank.IntName()];
      bank.Get(header);
    }

    //get the module number
    vsn = header & VHNMASK;

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA number = ",vsn)<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    //std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<header<<")"<<std::dec<<std::endl;
    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<Show("FERA type = 0x",std::hex,feraType," (from  0x",header,")",std::dec)<<std::endl;
    }

    switch(feraType) {
    case VHAD413:
      //vsn is 0-4
      if(!GetAdc413(bank, vsn, (header&VHAD413_NUMBER_OF_DATA_WORDS_MASK)>>VHAD413_DATA_WORDS_OFFSET, energy)) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Same problem with something immediately after ADC 413 data in barium fluoride data stream",Attribs::Reset())<<std::endl;
      }
      ++fCounter[VHAD413];
      break;	
	
    case VH3377:
      GetTdc3377(bank, feraEnd, time);
      ++fCounter[VH3377];
      break;

    case VHFULM:  /* Universal Logic Module end of event marking, clocks, etc.. */
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Found bad fera event in barium fluoride data stream",Attribs::Reset())<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;
		    
    default: // Unrecognized header
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to find FERA header in barium fluoride midas event ",bank.EventNumber(),", found 0x",std::hex,feraType," from header 0x",header,std::dec," instead",Attribs::Reset())<<std::endl;
      }
      ++fNofUnkownFera[bank.IntName()];
      //try and find the next header
      bank.Get(header);
      //skip all words until we find one with the high bit set
      while((header & 0x8000) == 0 && bank.ReadPoint() < feraEnd) {
	//increment counter and get next word
	bank.Get(header);
      }
      //un-read the header (will be read again in the next iteration of the while-loop_
      bank.ChangeReadPoint(-1);
      break;
    }
  }

  if(ulm.Clock() != 0 || ulm.CycleNumber() != 0) { 
    //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
    ConstructEvents(eventTime, eventNumber, EDetectorType::kBaF2, energy, time, ulm);
  } else if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Discarding event with ulm clock 0, ",energy.size()," adcs, and ",time.size()," tdcs")<<std::endl;
  }
}


//---------------------------------------- different electronics modules ----------------------------------------

//get energy from an Adc 114
bool MidasEventProcessor::GetAdc114(Bank& bank, uint32_t feraEnd, uint16_t& energy) {
  uint16_t tdc;

  bank.Get(energy);

  if(energy > VHAD114_ENERGY_MASK) {
    std::cerr<<Show(Foreground::Red(),"ADC 114 energy ",energy," > ",VHAD114_ENERGY_MASK,Attribs::Reset())<<std::endl;
  }

  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("Got Adc114 energy: 0x",std::hex,energy," = ",std::dec,energy)<<std::endl;
  }

  if(bank.ReadPoint() < feraEnd) {
    //check whether we have a tdc following this adc
    bank.Peek(tdc);
    if((tdc & 0x8000) == 0) {
      return true;
    }
  }

  return false;
}

//get energy from an Adc 413
bool MidasEventProcessor::GetAdc413(Bank& bank, uint16_t module, uint16_t nofDataWords, std::vector<std::pair<uint16_t, uint16_t> >& energy) {
  uint16_t data;
  uint16_t subAddress;

  //Header is followed by 1 to 4 data records, each with the following format:
  //B16 	B15 . . . B14 	B13 . . . . . . . . . . . . . . . . . .  . . B1
  //0 	SUBADDR 	DATA

  for(uint16_t i = 0; i < nofDataWords; ++i) {
    bank.Get(data);
		
    subAddress = (data&VHAD413_SUBADDRESS_MASK)>>VHAD413_SUBADDRESS_OFFSET;
    if(subAddress > 3) {
      return false;
    }
    energy.push_back(std::make_pair(module*4 + subAddress, data&VHAD413_ENERGY_MASK));
  }

  return true;
}

//read high and low word from tdc (extracting time and sub-address) until no more tdc data is left
bool MidasEventProcessor::GetTdc3377(Bank& bank, uint32_t feraEnd, std::map<uint16_t, std::vector<uint16_t> >& time) {
  uint16_t highWord;
  uint16_t lowWord;
  uint16_t subAddress;

  while(bank.ReadPoint() < feraEnd) {//???
    if(!bank.Get(highWord)) {
      return false;
    }
    if(!bank.Get(lowWord)) {
      return false;
    }
    
    if((highWord & 0x8000) || (lowWord & 0x8000)) {
      bank.ChangeReadPoint(-2);
      return false;
    }
    
    if((highWord&TDC3377_IDENTIFIER) != (lowWord&TDC3377_IDENTIFIER)) {
      //two words from two different tdcs? output error message
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Tdc identifier mismatch, event ",bank.EventNumber(),", bank ",bank.Number(),": ",(highWord&TDC3377_IDENTIFIER)," != ",(lowWord&TDC3377_IDENTIFIER),Attribs::Reset())<<std::endl;
      }
      return false;
    }

    subAddress = (highWord&TDC3377_IDENTIFIER) >> 10;
    time[subAddress].push_back(((highWord&TDC3377_TIME) << 8) | (lowWord&TDC3377_TIME));
    ++fSubAddress[subAddress];
    if(fSettings->VerbosityLevel() > 3) {
      std::cout<<Show("Got two tdc words: 0x",std::hex,highWord,", 0x",lowWord,std::dec)<<std::endl;
    }
  }

  return true;
}

bool MidasEventProcessor::GetAdc4300(Bank& bank, uint16_t header, uint16_t vsn, std::vector<std::pair<uint16_t, uint16_t> >& energy) {
  uint16_t tmp;
  uint16_t subAddress;
  uint16_t nofAdcWords = (header&PLASTIC_ADC_WORDS) >> PLASTIC_ADC_WORDS_OFFSET;

  if(nofAdcWords == 0) {
    //all channels fired.
    nofAdcWords = PLASTIC_CHANNELS;
  }

  for(uint16_t i = 0; i < nofAdcWords; ++i) {
    bank.Get(tmp);
    
    if((tmp & 0x8000) != 0) {
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"reached premature end of adc 4300 data: i = ",i,", # adc words = ",nofAdcWords,Attribs::Reset())<<std::endl;
      }
      bank.ChangeReadPoint(-1);
      break;
    }

    subAddress = (tmp&PLASTIC_IDENTIFIER) >> PLASTIC_IDENTIFIER_OFFSET;

    if(vsn*PLASTIC_CHANNELS + subAddress >= fSettings->NofPlasticDetectors()) {
      if(fSettings->VerbosityLevel() > 1) {
	std::cout<<Show("Found plastic detector #",vsn*PLASTIC_CHANNELS + subAddress," in event ",bank.EventNumber(),", bank ",bank.Number(),", but there should only be ",fSettings->NofPlasticDetectors())<<std::endl;
      }
      continue;
    }

    energy.push_back(std::make_pair(vsn*PLASTIC_CHANNELS + subAddress, tmp&PLASTIC_ENERGY));
  }
  return true;
}

bool MidasEventProcessor::GetUlm(Bank& bank, Ulm& ulm) {
  uint16_t header;
  if(!bank.Get(header)) {
    return false;
  }

  ulm.Header(header);

  uint32_t tmp;
  if(!bank.Get(tmp)) {
    return false;
  }
  ulm.Clock(tmp);
  if(!bank.Get(tmp)) {
    return false;
  }
  ulm.LiveClock(tmp);
  if(!bank.Get(tmp)) {
    return false;
  }
  ulm.MasterCount(tmp);

  if(fSettings->VerbosityLevel() > 3) {  
    std::cout<<Show("Got ulm with header 0x",std::hex,header,", clock 0x",ulm.Clock(),", live clock 0x",ulm.LiveClock(),", and master count 0x",tmp,std::dec)<<std::endl;
  }

  return true;
}

//----------------------------------------

void MidasEventProcessor::ConstructEvents(const uint32_t& eventTime, const uint32_t& eventNumber, const EDetectorType& detectorType, std::vector<std::pair<uint16_t, uint16_t> >& energy, std::map<uint16_t, std::vector<uint16_t> >& time, Ulm& ulm) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("starting to construct events from ",energy.size()," detectors with ",time.size()," times")<<std::endl;
  }
  size_t nofEvents = 0;

  //check that this is a known detector
  if(detectorType == EDetectorType::kUnknown) {
    std::cerr<<Show(Attribs::Bright(),Foreground::Red(),energy.size()," unknown detectors passed on to ",__PRETTY_FUNCTION__,Attribs::Reset())<<std::endl;
    return;
  }

  //drop all deactivated adcs
  energy.erase(std::remove_if(energy.begin(), energy.end(), [&](const std::pair<uint16_t, uint16_t> en) -> bool {return !fSettings->Active(detectorType,en.first);}),energy.end());

  //drop all deactivated tdcs (this is a map, so we can't use remove_if)
  auto it = time.begin();
  while(it != time.end()) {
    if(!fSettings->Active(detectorType, it->first)) {
      time.erase(it++);//delete this map entry and then go to the next entry (post-increment)
    } else {
      ++it;//just go to the next entry
    }
  }

  //stop if all detectors were deactivated
  if(energy.size() == 0) {
    if(time.size() != 0) {
      if(fSettings->VerbosityLevel() > 0) {
	std::cout<<Show(Foreground::Red(),"No active adcs, but ",time.size()," active tdcs",Attribs::Reset())<<std::endl;
      }
    } else if(fSettings->VerbosityLevel() > 2) {
      std::cout<<Show(Foreground::Red(),"No active adcs and no active tdcs",Attribs::Reset())<<std::endl;      
    }
    return;
  }

  //correct ulm overflows
  fClockState.CorrectOverflow(detectorType, eventTime, ulm);

  //now loop over all detectors, create the event, fill the detector number and energy, find the corresponding times, and fill them too
  for(auto& en : energy) {
    //check whether the circular buffer is full
    //if it is and we can't increase it's capacity, we try once(!) to wait for it to empty
    bool slept = false;
    while(fUncalibratedDetector.full() && !slept) {
      try {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<Show("Trying to increase uncalibrated detector buffer capacity of ",fUncalibratedDetector.capacity()," by ",fSettings->UncalibratedBufferSize())<<std::endl;
	}
	if(fUncalibratedMutex.try_lock()) {
	  std::cout<<Show(Background::Red(),"ConstructEvents acquired uncalibrated mutex 1",Attribs::Reset())<<std::endl;
	  fUncalibratedDetector.set_capacity(fUncalibratedDetector.capacity() + fSettings->UncalibratedBufferSize());
	  fUncalibratedMutex.unlock();
	} else {
	  std::cout<<Show(Background::Red(),"ConstructEvents: Uncalibrated mutex is locked, waiting for it",Attribs::Reset())<<std::endl;
	  fUncalibratedMutex.lock();
	  fUncalibratedDetector.set_capacity(fUncalibratedDetector.capacity() + fSettings->UncalibratedBufferSize());
	  fUncalibratedMutex.unlock();
	}
	std::cout<<Show(Background::Green(),"ConstructEvents released uncalibrated mutex 1",Attribs::Reset())<<std::endl;
      } catch(std::exception& exc) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to increase capacity (",fUncalibratedDetector.capacity(),") of uncalibrated detector buffer by ",fSettings->UncalibratedBufferSize(),Attribs::Reset())<<std::endl;
	slept = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      }
    }
    if(ulm.Clock() == 0) {
      std::cout<<Show(Foreground::Red(),"Detector (type ",static_cast<uint16_t>(detectorType),", number ",en.first,") with ulm clock 0!",Attribs::Reset())<<std::endl;
    } else if(fSettings->VerbosityLevel() > 3) {
      std::cout<<Foreground::Green<<Show("Detector with ulm clock ",ulm.Clock(),Attribs::Reset())<<std::endl;
    }
    //create a temporary detector, so that we don't need to lock!
    //we need to keep this lock until we're done with the creation of the detector (including filling in the tdc times) to prevent it being removed before we're done with it
    Detector tmpDetector(eventTime, eventNumber, static_cast<uint8_t>(detectorType), en, ulm);
    if(fDataFile.is_open()) {
      fDataFile<<eventNumber<<" "<<eventTime<<" "<<static_cast<uint16_t>(detectorType)<<" "<<en.first<<" "<<en.second<<" "<<ulm.Clock()<<" "<<ulm.LiveClock()<<std::endl;
    }
    ++nofEvents;
    //check that we have any times for this detector
    if(time.find(en.first) != time.end() && time.at(en.first).size() > 0) {
      tmpDetector.TdcHits(time[en.first].size());
      //find the right tdc hit
      //since any good tdc hit creates a deadtime w/o anymore tdc hits, we want the last one
      //the tdcs are LIFO, so the last hit is the first coming out
      //however in Greg's FIFO.c he uses the last time found within the coarse window or (if none is found) the very first time
      for(const auto& thisTime : time[en.first]) {
	if(fSettings->CoarseTdcWindow(detectorType,en.first,thisTime)) {
	  tmpDetector.Time(thisTime);
	}
      }
      if(tmpDetector.Time() == 0) {
	tmpDetector.Time(time[en.first].front());
      }
    } else {
      //no tdc hits found for this detector
      if(fSettings->VerbosityLevel() > 2) {
	std::cerr<<Show(Foreground::Red(),"Found no tdc hits for detector type ",std::hex,static_cast<uint16_t>(detectorType),std::dec,", number ",en.first,Attribs::Reset())<<std::endl;
      }
    }
    std::cout<<Show(Background::Blue(),"ConstructEvents acquiring uncalibrated mutex 2",Attribs::Reset())<<std::endl;
    fUncalibratedMutex.lock();
    std::cout<<Show(Background::Red(),"ConstructEvents acquired uncalibrated mutex 2",Attribs::Reset())<<std::endl;
    fUncalibratedDetector.push_back(tmpDetector);
    fUncalibratedMutex.unlock();
    std::cout<<Show(Background::Green(),"ConstructEvents released uncalibrated mutex 2",Attribs::Reset())<<std::endl;
  }//for detector

  fNofUncalibratedDetectors += nofEvents;

  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<Show("done with creation of ",nofEvents," events (",fUncalibratedDetector.size()," uncalibrated detectors in queue, ",fNofUncalibratedDetectors," in total)")<<std::endl;
  }
}

//----------------------------------------

void MidasEventProcessor::Flush() {
  //set status to flush (this triggers the flushing)
  fStatus = kFlushUncalibrated;
  //join all threads, i.e. wait for them to finish flushing
  for(auto& thread : fThreads) {
    //wait for the thread to be ready
    if(thread.second.valid()) {
      size_t nofWaits = 0;
      while(thread.second.wait_for(std::chrono::milliseconds(STANDARD_WAIT_TIME))==std::future_status::timeout) {
	if(nofWaits%10 == 0) {
	  std::cout<<Attribs::Bright<<Foreground::Cyan<<Show("Waiting for thread ",thread.first," to finish: ",Status(),Attribs::Reset())<<std::endl;
	}
	++nofWaits;
      }
    } else {
      std::cout<<Show(Attribs::Bright(),Foreground::Blue(),"Thread ",thread.first," not valid",Attribs::Reset())<<std::endl;
      continue;
    }
    //check return status
    std::cout<<Show(Attribs::Bright(),Foreground::Blue(),thread.second.get(),Attribs::Reset())<<std::endl;
  }

  //write histograms to file
  for(auto& detType : fRawEnergyHistograms) {
    for(auto& histogram : detType) {
      if(histogram == nullptr) {
	std::cout<<Show(Attribs::Bright(),Foreground::Red(),"Got empty histogram, skipping it",Attribs::Reset())<<std::endl;
	continue;
      }
      std::cout<<Show("Writing histogram ",std::hex,histogram,std::dec," = '",histogram->GetName(),"' to file '",fRootFile->GetName(),"'")<<std::endl;
      fRootFile->cd();
      histogram->Write("", TObject::kOverwrite);
    }
  }
}

void MidasEventProcessor::Print() {
  std::cout<<"Zeros skipped:"<<std::endl;
  for(auto it : fNofZeros) {
    std::cout<<Show(it.first,": \t",std::setw(7),it.second)<<std::endl;
  }

  std::cout<<"Unknown FERA header:"<<std::endl;
  for(auto it : fNofUnkownFera) {
    std::cout<<Show(it.first,": \t",std::setw(7),it.second)<<std::endl;
  }

  std::cout<<"Events found:"<<std::endl;
  for(auto it : fNofMidasEvents) {
    switch(it.first) {
    case FIFOEVENT:
      std::cout<<Show("Fifo:\t",std::setw(7),it.second)<<std::endl;
      break;
    case CAMACSCALEREVENT:
      std::cout<<Show("Camac:\t",std::setw(7),it.second)<<std::endl;
      break;

    case SCALERSCALEREVENT:
      std::cout<<Show("Scaler:\t",std::setw(7),it.second)<<std::endl;
      break;

    case ISCALEREVENT:
      std::cout<<Show("i-scaler:\t",std::setw(7),it.second)<<std::endl;
      break;

    case FRONTENDEVENT:
      std::cout<<Show("Frontend:\t",std::setw(7),it.second)<<std::endl;
      break;

    case EPICSEVENTTYPE:
      std::cout<<Show("Epics:\t",std::setw(7),it.second)<<std::endl;
      break;

    case FILEEND:
      std::cout<<Show("File-end:\t",std::setw(7),it.second)<<std::endl;
      break;
    default:
      std::cout<<Show("Unknown event type 0x",std::hex,it.first,std::dec,": ",std::setw(7),it.second)<<std::endl;
    }
  }
}

//start calibration thread (takes events from the input buffers, calibrates them and puts them into the calibrated buffer)
std::string MidasEventProcessor::Calibrate() {
  TStopwatch watch;
  Detector detector;
  TF1* calibration;
  TH1I* histogram;
  
  //we keep running until all detectors are calibrated and we're not running anymore
  //this ensures that we don't stop when the read queue drops down to zero while reading more events
  while(fStatus != kFlushUncalibrated || fUncalibratedDetector.size() > 0) {
    //if there are no uncalibrated events we wait a little bit (and continue to make sure we weren't told to flush in the mean time)
    if(fUncalibratedDetector.empty()) {
      if(fStatus != kRun) {
	std::cout<<Show(Attribs::Bright(),Foreground::Blue(),ThreadStatus(),Attribs::Reset())<<std::endl;
      }
      //std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      std::this_thread::yield();
      continue;
    }
    //we only use the first uncalibrated detector, as we're running in parallel to a thread that adds elements to the std::vector
    //to minimize the time we have to lock the queue, we get the first element from it and then delete it
    if(fSettings->VerbosityLevel() > 3) {
      std::cout<<Show("Got ",fUncalibratedDetector.size()," uncalibrated detectors to calibrate! Done ",fNofCalibratedDetectors)<<std::endl;
    }
    //std::cout<<"Got "<<fUncalibratedDetector.size()<<" uncalibrated detectors to calibrate! Done "<<fNofCalibratedDetectors<<std::endl;
    std::cout<<Show(Background::Blue(),"Calibrate acquiring uncalibrated mutex 1",Attribs::Reset())<<std::endl;
    fUncalibratedMutex.lock();
    std::cout<<Show(Background::Red(),"Calibrate acquired uncalibrated mutex 1",Attribs::Reset())<<std::endl;
    detector = fUncalibratedDetector.front();
    fUncalibratedDetector.pop_front();
    fUncalibratedMutex.unlock();
    std::cout<<Show(Background::Green(),"Calibrate released uncalibrated mutex 1",Attribs::Reset())<<std::endl;
    //add the energy to the histogram
    histogram = fRawEnergyHistograms[detector.DetectorType()][detector.DetectorNumber()];
    if(histogram == nullptr) {
      std::cerr<<Show("Failed to get histogram for detector type ",uint16_t(detector.DetectorType()),", number ",detector.DetectorNumber())<<std::endl;
      throw;
    }
    histogram->Fill(detector.RawEnergy());

    //if we don't calibrate we just set the energy to the raw energy and continue
    if(fSettings->NoCalibration()) {
      detector.Energy(detector.RawEnergy());
      //add detector to calibrated queue
      std::cout<<Show(Background::Blue(),"Calibrate trying to acquire calibrated mutex 1",Attribs::Reset())<<std::endl;
      fCalibratedMutex.lock();
      std::cout<<Show(Background::Red(),"Calibrate acquired calibrated mutex 1",Attribs::Reset())<<std::endl;
      fCalibratedDetector.insert(detector);
      fCalibratedMutex.unlock();
      std::cout<<Show(Background::Green(),"Calibrate released calibrated mutex 1",Attribs::Reset())<<std::endl;
      ++fNofCalibratedDetectors;
      continue;
    }

    //add the detector to the queue
    //check whether the circular buffer is full
    //if it is and we can't increase it's capacity, we try once(!) to wait for it to empty
    bool slept = false;
    while(fWaiting[detector.DetectorType()][detector.DetectorNumber()].full() && !slept) {
      try {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<Show("Trying to increase waiting buffer (",uint16_t(detector.DetectorType()),", ",detector.DetectorNumber(),") capacity of ",fWaiting[detector.DetectorType()][detector.DetectorNumber()].capacity()," by ",fSettings->MinimumCounts(detector.DetectorType()))<<std::endl;
	}
	fWaitingMutex[detector.DetectorType()].lock();
	fWaiting[detector.DetectorType()][detector.DetectorNumber()].set_capacity(fWaiting[detector.DetectorType()][detector.DetectorNumber()].capacity() + fSettings->MinimumCounts(detector.DetectorType()));
	fWaitingMutex[detector.DetectorType()].unlock();
      } catch(std::exception& exc) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to increase capacity (",fWaiting[detector.DetectorType()][detector.DetectorNumber()].capacity(),") of waiting buffer ",detector.DetectorType(),", ",detector.DetectorNumber()," by ",fSettings->MinimumCounts(detector.DetectorType()),Attribs::Reset())<<std::endl;
	slept = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      }
    }
    fWaitingMutex[detector.DetectorType()].lock();
    fWaiting[detector.DetectorType()][detector.DetectorNumber()].push_back(detector);
    fWaitingMutex[detector.DetectorType()].unlock();
    ++fNofWaiting;
    //check whether we have enough events in histogram to start calibrating
    if(histogram->Integral() > fSettings->MinimumCounts(detector.DetectorType())) {
      TGraph peaks = fCalibration.Calibrate(detector.DetectorType(),detector.DetectorNumber(),histogram);
      //if calibration fails???
      if(peaks.GetN() == 0) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Calibration of histogram '",histogram->GetName(),"' failed!",Attribs::Reset())<<std::endl;
	continue;
      }
      calibration = (TF1*) peaks.GetListOfFunctions()->FindObject("Calibration");
      if(calibration == nullptr) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Found no calibration for histogram '",histogram->GetName(),"'!",Attribs::Reset())<<std::endl;
	continue;
      }
      //we're done calibrating so we can remove the first detector of the same type and number from the spectrum and queue
      fWaitingMutex[detector.DetectorType()].lock();
      detector = fWaiting[detector.DetectorType()][detector.DetectorNumber()].front();
      histogram->Fill(detector.RawEnergy(),-1);
      fWaiting[detector.DetectorType()][detector.DetectorNumber()].pop_front();
      fWaitingMutex[detector.DetectorType()].unlock();
      //add calibrated energy to detector
      detector.Energy(calibration->Eval(detector.RawEnergy()));
      //add detector to calibrated queue
      std::cout<<Show(Background::Blue(),"Calibrate acquiring calibrated mutex 2",Attribs::Reset())<<std::endl;
      fCalibratedMutex.lock();
      std::cout<<Show(Background::Red(),"Calibrate acquired calibrated mutex 2",Attribs::Reset())<<std::endl;
      fCalibratedDetector.insert(detector);
      fCalibratedMutex.unlock();
      std::cout<<Show(Background::Green(),"Calibrate released calibrated mutex 2",Attribs::Reset())<<std::endl;
      ++fNofCalibratedDetectors;
    }//if enough events
  }//while loop

  //end of the while loop, we're not running anymore and there are no more events in the uncalibrated queue
  //now we want to use the last calibration we have to calibrate all remaining waiting detectors
  //so we loop over all detector types in the waiting map and for each detector type over all detectors
  //we get the right histogram, and use it's calibration, if there is no calibration we force the creation of one
  for(auto& detType : fWaiting) {//detType is a pair of detector type and a vector of circular buffers
    //for each detector type we need to loop over all detectors
    size_t i = 0;
    for(auto& det : detType.second) {//det is a circular buffer
      //get the right histogram
      histogram = fRawEnergyHistograms[detType.first][i];
      while(det.size() > 0) {
	TGraph peaks = fCalibration.Calibrate(detType.first,i,histogram);
	//if calibration fails???
	if(peaks.GetN() == 0) {
	  std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Calibration of histogram '",histogram->GetName(),"' failed!",Attribs::Reset())<<std::endl;
	  calibration = nullptr;
	} else {
	  calibration = (TF1*) peaks.GetListOfFunctions()->FindObject("Calibration");
	  if(calibration == nullptr) {
	    std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Found no calibration for histogram '",histogram->GetName(),"'!",Attribs::Reset())<<std::endl;
	  }
	}
	//we're done calibrating so we can remove the first detector of the same type and number from the spectrum and queue
	fWaitingMutex[detType.first].lock();
	detector = fWaiting[detType.first][i].front();
	histogram->Fill(detector.RawEnergy(),-1);
	fWaiting[detType.first][i].pop_front();
	fWaitingMutex[detType.first].unlock();
	//add calibrated energy to detector
	if(calibration != nullptr) {
	  detector.Energy(calibration->Eval(detector.RawEnergy()));
	} else {
	  detector.Energy(detector.RawEnergy());
	}
	//add detector to calibrated queue
	std::cout<<Show(Background::Blue(),"Calibrate acquiring calibrated mutex 3",Attribs::Reset())<<std::endl;
	fCalibratedMutex.lock();
	std::cout<<Show(Background::Red(),"Calibrate acquired calibrated mutex 3",Attribs::Reset())<<std::endl;
	fCalibratedDetector.insert(detector);
	fCalibratedMutex.unlock();
	std::cout<<Show(Background::Green(),"Calibrate released calibrated mutex 3",Attribs::Reset())<<std::endl;
	++fNofCalibratedDetectors;
	++i;
      }
    }
  }

  fStatus = kFlushCalibrated;

  std::stringstream result;
  result<<"Calibrate finished with status "<<fStatus<<", fUncalibratedDetector.size() = "<<fUncalibratedDetector.size()<<" after "<<watch.RealTime()<<"/"<<watch.CpuTime()<<" seconds (real time/cpu time)"<<std::endl;
  return result.str();
}

//start event building thread (takes events from calibrated buffer and combines them into build events in the output buffer)
std::string MidasEventProcessor::BuildEvents() {
  TStopwatch watch;
  std::vector<Detector> detectors;

  while(fStatus != kFlushCalibrated || fCalibratedDetector.size() > 0) {
    //if there are no calibrated events we wait a little bit (and continue to make sure we weren't told to flush in the mean time)
    if(fCalibratedDetector.empty()) {
      if(fStatus != kRun) {
	std::cout<<Show(Attribs::Bright(),Foreground::Green(),ThreadStatus(),Attribs::Reset())<<std::endl;
      }
      //std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      std::this_thread::yield();
      continue;
    }
    if(fSettings->VerbosityLevel() > 3) {
      std::cout<<Show("Got ",fCalibratedDetector.size()," calibrated detectors to build event! Done ",fNofBuiltEvents)<<std::endl;
    }
    //std::cout<<"Got "<<fCalibratedDetector.size()<<" calibrated detectors to calibrate! Done "<<fNofBuiltEvents<<std::endl;
    //fCalibratedDetector is a multiset, i.e. ordered values which can have the same value
    //so we first want to check whether the time difference between begin and end is within the waiting window (unless we're flushing)
    std::cout<<Show(Background::Blue(),"BuiltEvents acquiring calibrated mutex 1",Attribs::Reset())<<std::endl;
    fCalibratedMutex.lock();
    std::cout<<Show(Background::Red(),"BuiltEvents acquired calibrated mutex 1",Attribs::Reset())<<std::endl;
    if(fStatus != kFlushCalibrated && fSettings->InWaitingWindow(fCalibratedDetector.begin()->GetUlm().Clock(), std::prev(fCalibratedDetector.end())->GetUlm().Clock())) {
      //std::cout<<"Not flushing (fStatus = "<<fStatus<<"), "<<fCalibratedDetector.size()<<" events between "<<fCalibratedDetector.begin()->GetUlm().Clock()<<" and "<<std::prev(fCalibratedDetector.end())->GetUlm().Clock()<<std::endl;
      fCalibratedMutex.unlock();
      std::cout<<Show(Background::Green(),"BuiltEvents released calibrated mutex 1",Attribs::Reset())<<std::endl;
      continue;
    }
    fCalibratedMutex.unlock();
    std::cout<<Show(Background::Green(),"BuiltEvents released calibrated mutex 1",Attribs::Reset())<<std::endl;

    size_t nofRemoved = 0;

    //our oldest time is outside the waiting window, so we take that detector out of the list
    detectors.clear();
    std::cout<<Show(Background::Blue(),"BuiltEvents trying to acquire calibrated mutex 2",Attribs::Reset())<<std::endl;
    fCalibratedMutex.lock();
    std::cout<<Show(Background::Red(),"BuiltEvents acquired calibrated mutex 2",Attribs::Reset())<<std::endl;
    detectors.push_back(*(fCalibratedDetector.begin()));
    fCalibratedDetector.erase(fCalibratedDetector.begin());
    ++nofRemoved;
    //now we want to find all that are in coincidence with that detector
    for(auto iterator = fCalibratedDetector.begin(); iterator != fCalibratedDetector.end(); ++iterator) {
      //std::cout<<Show(Background::Yellow<<"Working on detector "<<std::distance(fCalibratedDetector.begin(),iterator)<<" of "<<fCalibratedDetector.size()<<Attribs::Reset())<<std::endl;
      std::cout<<Show(Background::Yellow(),"Working on detector ",std::distance(fCalibratedDetector.begin(),iterator)," of ",fCalibratedDetector.size(),Attribs::Reset())<<std::endl;
      if(fSettings->Coincidence(detectors[0].GetUlm().Clock(), iterator->GetUlm().Clock())) {
	detectors.push_back(*iterator);
	//if this detector is also outside the waiting window or has the same time as the current detector (if we're flushing) we remove it as well
	if(!fSettings->InWaitingWindow(iterator->GetUlm().Clock(), std::prev(fCalibratedDetector.end())->GetUlm().Clock()) || iterator->GetUlm().Clock() == detectors[0].GetUlm().Clock()) {
	  fCalibratedDetector.erase(iterator);
	  ++nofRemoved;
	}
      } else {
	//the set is ordered, so if this detector is outside the coincidence window all followings will be outside as well
	break;
      }
      std::cout<<Show(Background::Yellow(),"Done with detector ",std::distance(fCalibratedDetector.begin(),iterator)," of ",fCalibratedDetector.size(),Attribs::Reset())<<std::endl;
    }
    std::cout<<Show(Background::Yellow(),"Done with all detectors",Attribs::Reset())<<std::endl;
    fCalibratedMutex.unlock();
    std::cout<<Show(Background::Green(),"BuiltEvents released calibrated mutex 2",Attribs::Reset())<<std::endl;

    //check whether the circular buffer is full
    //if it is and we can't increase it's capacity, we try once(!) to wait for it to empty
    bool slept = false;
    while(fBuiltEvents.full() && !slept) {
      try {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<Show("Trying to increase built events buffer capacity of ",fBuiltEvents.capacity()," by ",fSettings->BuiltEventsSize())<<std::endl;
	}
	std::cout<<Show(Background::Blue(),"BuiltEvents acquiring built mutex 1",Attribs::Reset())<<std::endl;
	fBuiltMutex.lock();
	std::cout<<Show(Background::Red(),"BuiltEvents acquired built mutex 1",Attribs::Reset())<<std::endl;
	fBuiltEvents.set_capacity(fBuiltEvents.capacity() + fSettings->BuiltEventsSize());
	fBuiltMutex.unlock();
	std::cout<<Show(Background::Green(),"BuiltEvents released built mutex 1",Attribs::Reset())<<std::endl;
      } catch(std::exception& exc) {
	std::cerr<<Show(Attribs::Bright(),Foreground::Red(),"Failed to increase capacity (",fBuiltEvents.capacity(),") of built events buffer by ",fSettings->BuiltEventsSize(),Attribs::Reset())<<std::endl;
	slept = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      }
    }
    std::cout<<Show(Background::Blue(),"BuiltEvents acquiring built mutex 2",Attribs::Reset())<<std::endl;
    fBuiltMutex.lock();
    std::cout<<Show(Background::Red(),"BuiltEvents acquired built mutex 2",Attribs::Reset())<<std::endl;
    fBuiltEvents.push_back(Event(detectors));
    fBuiltMutex.unlock();
    std::cout<<Show(Background::Green(),"BuiltEvents released built mutex 2",Attribs::Reset())<<std::endl;
    ++fNofBuiltEvents;
    if(fSettings->VerbosityLevel() > 1) {
      std::cout<<Show("Built event with ",detectors.size()," detectors (removed ",nofRemoved,")")<<std::endl;
    }
  }//while loop

  fStatus = kFlushBuilt;

  std::stringstream result;
  result<<"BuildEvents finished with status "<<fStatus<<", fCalibratedDetector.size() = "<<fCalibratedDetector.size()<<" after "<<watch.RealTime()<<"/"<<watch.CpuTime()<<" seconds (real time/cpu time)"<<std::endl;
  return result.str();
}

//start output thread (writes event in the output buffer to file/tree)
std::string MidasEventProcessor::FillTree() {
  TStopwatch watch;

  while(fStatus != kFlushBuilt || fBuiltEvents.size() > 0) {
    //if there are no uncalibrated events we wait a little bit (and continue to make sure we weren't told to flush in the mean time)
    if(fBuiltEvents.empty()) {
      if(fStatus != kRun) {
	std::cout<<Show(Attribs::Bright(),Foreground::Yellow(),ThreadStatus(),Attribs::Reset())<<std::endl;
      }
      //std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      std::this_thread::yield();
      continue;
    }
    //make the leaf point to the first event
  std::cout<<Show(Background::Blue(),"FillTree acquiring built mutex",Attribs::Reset())<<std::endl;
    fBuiltMutex.lock();
    std::cout<<Show(Background::Red(),"FillTree acquired built mutex",Attribs::Reset())<<std::endl;
    fLeaf = &(fBuiltEvents.front());
    //fill the tree (this writes the first event to file)
    fTree->Fill();
    //delete the first event
    fBuiltEvents.pop_front();
    fBuiltMutex.unlock();
    std::cout<<Show(Background::Green(),"FillTree released built mutex",Attribs::Reset())<<std::endl;
    if(fSettings->VerbosityLevel() > 1) {
      std::cout<<"Wrote one event to tree."<<std::endl;
    }
  }//while loop

  fStatus = kDone;

  std::stringstream result;
  result<<"FillTree finished with status "<<fStatus<<", fBuiltEvents.size() = "<<fBuiltEvents.size()<<" after "<<watch.RealTime()<<"/"<<watch.CpuTime()<<" seconds (real time/cpu time)"<<std::endl;
  return result.str();
}

std::string MidasEventProcessor::StatusUpdate() {
  TStopwatch watch;

  while(fStatus == kRun) {
    std::cout<<Status()<<std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::stringstream result;
  result<<"StatusUpdate finished after "<<watch.RealTime()<<"/"<<watch.CpuTime()<<" seconds (real time/cpu time)"<<std::endl;
  return result.str();
}

std::string MidasEventProcessor::Status() {
  std::stringstream result;

  size_t waitingGermanium = 0;
  size_t waitingPlastic = 0;
  size_t waitingSilicon = 0;
  size_t waitingBaF2 = 0;
  size_t waitingSum;

  for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kGermanium)]) {
    waitingGermanium += det.size();
  }
  for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kPlastic)]) {
    waitingPlastic += det.size();
  }
  for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kSilicon)]) {
    waitingSilicon += det.size();
  }
  for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kBaF2)]) {
    waitingBaF2 += det.size();
  }
  waitingSum = waitingGermanium + waitingPlastic + waitingSilicon + waitingBaF2;
  if(fStatus == kRun) {
    result<<"running: ";
  } else if(fStatus == kFlushUncalibrated) {
    result<<"flushing uncalibrated: ";
  } else if(fStatus == kFlushCalibrated) {
    result<<"flushing calibrated: ";
  } else if(fStatus == kFlushBuilt) {
    result<<"flushing built: ";
  } else if(fStatus == kDone) {
    result<<"done: ";
  } else {
    result<<"unknown status: ";
  }
  result<<fUncalibratedDetector.size()<<"/"<<fNofUncalibratedDetectors<<" uncalibrated detectors, "
	<<waitingSum<<"/"<<fNofWaiting<<" waiting detectors ("<<waitingGermanium<<" Ge, "<<waitingPlastic<<" Pl, "<<waitingSilicon<<" Si, "<<waitingBaF2<<" BaF2) , "
	<<fCalibratedDetector.size()<<"/"<<fNofCalibratedDetectors<<" calibrated detectors, "
	<<fBuiltEvents.size()<<"/"<<fNofBuiltEvents<<" built events, "
	<<fTree->GetEntries()<<" entries in tree";

  return result.str();
}

std::string MidasEventProcessor::ThreadStatus() {
  std::stringstream result;

  size_t nofValid = 0;
  size_t nofReady = 0;
  size_t nofTimeout = 0;

  for(const auto& thread : fThreads) {
    if(thread.second.valid()) {
      ++nofValid;
      auto status = thread.second.wait_for(std::chrono::milliseconds(0));
      if(status == std::future_status::ready) {
	++nofReady;
      } else if(status == std::future_status::timeout) {
	++nofTimeout;
      }
    }
  }
  result<<fThreads.size()<<" threads: "<<nofValid<<" valid, "<<nofReady<<" ready, "<<nofTimeout<<" timed out; active mutexes: ";

  if(fUncalibratedMutex.try_lock()) {
    fUncalibratedMutex.unlock();
  } else {
    result<<"uncalibrated mutex, ";
  }
  uint8_t detType = static_cast<uint8_t>(EDetectorType::kGermanium);
  if(fWaitingMutex[detType].try_lock()) {
    fWaitingMutex[detType].unlock();
  } else {
    result<<"germanium mutex, ";
  }
  detType = static_cast<uint8_t>(EDetectorType::kPlastic);
  if(fWaitingMutex[detType].try_lock()) {
    fWaitingMutex[detType].unlock();
  } else {
    result<<"plastic mutex, ";
  }
  detType = static_cast<uint8_t>(EDetectorType::kSilicon);
  if(fWaitingMutex[detType].try_lock()) {
    fWaitingMutex[detType].unlock();
  } else {
    result<<"silicon mutex, ";
  }
  detType = static_cast<uint8_t>(EDetectorType::kBaF2);
  if(fWaitingMutex[detType].try_lock()) {
    fWaitingMutex[detType].unlock();
  } else {
    result<<"baf2 mutex, ";
  }
  if(fCalibratedMutex.try_lock()) {
    fCalibratedMutex.unlock();
  } else {
    result<<"calibrated mutex, ";
  }
  if(fBuiltMutex.try_lock()) {
    fBuiltMutex.unlock();
  } else {
    result<<"built mutex";
  }

  return result.str();
}

ClockState::ClockState(uint32_t startTime) {
  fCycleStartTime = startTime;
}

void ClockState::Update(uint32_t time) {
  //increment stored cycles
  ++fNofStoredCycles;

  //  //loop over all
  //  for(i = 0; i < NUMBER_OF_DETECTOR_TYPES; i++) {
  //
  //    clockstate->firsteventtime[i] = 0;
  //    clockstate->firsteventtimeset[i] = 0;
  //
  //    clockstate->cyclecorruptdeadtimes[i] = realloc(clockstate->cyclecorruptdeadtimes[i], sizeof(unsigned long long)*clockstate->numberofstoredcycles);
  //    clockstate->cycleulmclocks[i] = realloc(clockstate->cycleulmclocks[i], sizeof(unsigned long long)*clockstate->numberofstoredcycles);
  //    clockstate->cyclelivetimes[i] = realloc(clockstate->cyclelivetimes[i], sizeof(unsigned long long)*clockstate->numberofstoredcycles);
  //    clockstate->cycledeadtimes[i] = realloc(clockstate->cycledeadtimes[i], sizeof(unsigned long long)*clockstate->numberofstoredcycles);
  //
  //    if(clockstate->cyclelivetimes[i] == 0 || clockstate->cycledeadtimes[i] == 0) {
  //      printf("Memory allocation error in updateClockStateForNewCycle()");
  //      exit(-1);
  //    }
  //
  //    clockstate->cyclecorruptdeadtimes[i][clockstate->numberofstoredcycles - 1] = clockstate->corruptdeadtime[i];
  //    clockstate->cycleulmclocks[i][clockstate->numberofstoredcycles - 1] = clockstate->lastulmclock[i];
  //    clockstate->cyclelivetimes[i][clockstate->numberofstoredcycles - 1] = clockstate->lastlivetime[i];
  //    clockstate->cycledeadtimes[i][clockstate->numberofstoredcycles - 1] = clockstate->lastdeadtime[i];
  //  }
  //
  //  clockstate->cyclestarttime = cyclestarttime;
  //
  //  //Reset the clock state.
  //  for(i = 0; i < NUMBER_OF_DETECTOR_TYPES; i++) {
  //    clockstate->corruptdeadtime[i] = 0;
  //    clockstate->lastlivetime[i] = 0;
  //    clockstate->numberoflivetimeoverflows[i] = 0;
  //    clockstate->lastdeadtime[i] = 0;
  //    clockstate->lastulmclock[i] = 0;
  //  }
}

void ClockState::CorrectOverflow(const EDetectorType& detectorType, const uint32_t& eventTime, Ulm& ulm) {
  uint8_t detType = static_cast<uint8_t>(detectorType);
  if(fFirstEventTime.find(detType) == fFirstEventTime.end()) {
    fFirstEventTime[detType] = eventTime - ulm.Clock()/ULM_CLOCK_IN_SECONDS;//all in seconds
    return;
  }
  
  uint32_t nofOverflows;
  if(ulm.Clock() > ULM_CLOCK_OVERFLOW/2) {
    nofOverflows = (uint32_t)(float(eventTime - fFirstEventTime[detType] - ULM_CLOCK_OVERFLOW/ULM_CLOCK_IN_SECONDS/4)/(float(ULM_CLOCK_OVERFLOW)/float(ULM_CLOCK_IN_SECONDS)));
    //std::cout<<"nofOverflows = (uint32_t)(float("<<eventTime<<" - "<<fFirstEventTime[detType]<<" - "<<ULM_CLOCK_OVERFLOW/ULM_CLOCK_IN_SECONDS/4<<" = "<<eventTime - fFirstEventTime[detType] - ULM_CLOCK_OVERFLOW/ULM_CLOCK_IN_SECONDS/4<<")/("<<float(ULM_CLOCK_OVERFLOW)/float(ULM_CLOCK_IN_SECONDS)<<")) = "<<nofOverflows<<", ulm = "<<ulm.Clock()<<std::endl;
  } else {
    nofOverflows = (uint32_t)(float(eventTime - fFirstEventTime[detType] + ULM_CLOCK_OVERFLOW/ULM_CLOCK_IN_SECONDS/4)/(float(ULM_CLOCK_OVERFLOW)/float(ULM_CLOCK_IN_SECONDS)));
    //std::cout<<"nofOverflows = (uint32_t)(float("<<eventTime<<" - "<<fFirstEventTime[detType]<<" + "<<ULM_CLOCK_OVERFLOW/ULM_CLOCK_IN_SECONDS/4<<" = "<<eventTime - fFirstEventTime[detType] + ULM_CLOCK_OVERFLOW/ULM_CLOCK_IN_SECONDS/4<<")/("<<float(ULM_CLOCK_OVERFLOW)/float(ULM_CLOCK_IN_SECONDS)<<")) = "<<nofOverflows<<", ulm = "<<ulm.Clock()<<std::endl;
  }
  
  ulm.ClockOverflow(nofOverflows);
}
