#include "MidasEventProcessor.hh"

#include <iomanip>

#include "TF1.h"
#include "TList.h"

#include "TextAttributes.hh"

#include "MidasFileManager.hh"

//make MidasEventProcessor a singleton???
MidasEventProcessor::MidasEventProcessor(Settings* settings, TTree* tree, bool statusUpdate) {
  fSettings = settings;
  fTree = tree;
  fStatus = kRun;

  //attach leaf to tree
  int BufferSize = 1024000;
  fTree->Branch("Event",&fLeaf, BufferSize);
  fTree->BranchRef();

  //increase maximum tree size to 10GB
  Long64_t GByte = 1073741824L;
  fTree->SetMaxTreeSize(10*GByte);

  //start calibration thread (takes events from the input buffer, calibrates them and puts them into the calibrated buffer)
  fThreads.push_back(std::thread(&MidasEventProcessor::Calibrate, this));
  //start event building thread (takes events from calibrated buffer and combines them into build events in the output buffer)
  fThreads.push_back(std::thread(&MidasEventProcessor::BuildEvents, this));
  //start output thread (writes event in the output buffer to file/tree)
  fThreads.push_back(std::thread(&MidasEventProcessor::FillTree, this));
  if(statusUpdate) {
    fThreads.push_back(std::thread(&MidasEventProcessor::StatusUpdate, this));
  }

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
  fRawEnergyHistograms[detType].resize(fSettings->NofGermaniumDetectors());
  for(int det = 0; det < fSettings->NofGermaniumDetectors(); ++det) {
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawGermanium_%d",(int)det),Form("rawGermanium_%d",(int)det),
						  fSettings->MaxGermaniumChannel(),0.,(double)fSettings->MaxGermaniumChannel());
  }

  detType = static_cast<uint8_t>(EDetectorType::kPlastic);
  fWaiting[detType].resize(fSettings->NofPlasticDetectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fRawEnergyHistograms[detType].resize(fSettings->NofPlasticDetectors());
  for(int det = 0; det < fSettings->NofPlasticDetectors(); ++det) {
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawPlastic_%d",(int)det),Form("rawPlastic_%d",(int)det),
						  fSettings->MaxPlasticChannel(),0.,(double)fSettings->MaxPlasticChannel());
  }

  detType = static_cast<uint8_t>(EDetectorType::kSilicon);
  fWaiting[detType].resize(fSettings->NofSiliconDetectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fRawEnergyHistograms[detType].resize(fSettings->NofSiliconDetectors());
  for(int det = 0; det < fSettings->NofSiliconDetectors(); ++det) {
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawSilicon_%d",(int)det),Form("rawSilicon_%d",(int)det),
						  fSettings->MaxSiliconChannel(),0.,(double)fSettings->MaxSiliconChannel());
  }

  detType = static_cast<uint8_t>(EDetectorType::kBaF2);
  fWaiting[detType].resize(fSettings->NofBaF2Detectors(),boost::circular_buffer<Detector>(1.1*fSettings->MinimumCounts(detType)));
  fRawEnergyHistograms[detType].resize(fSettings->NofBaF2Detectors());
  for(int det = 0; det < fSettings->NofBaF2Detectors(); ++det) {
    fRawEnergyHistograms[detType][det] = new TH1I(Form("rawBaF2_%d",(int)det),Form("rawBaF2_%d",(int)det),
						  fSettings->MaxBaF2Channel(),0.,(double)fSettings->MaxBaF2Channel());
  }
}

MidasEventProcessor::~MidasEventProcessor() {
  Flush();
  fTemperatureFile.close();
}

bool MidasEventProcessor::Process(MidasEvent& event) {
  //increment the count for this type of event, no matter what type it is
  //if this is the first time we encounter this type, it will automatically be inserted
  fNofMidasEvents[event.Type()]++;
  //choose the different methods based on the event type
  //events added to the input buffer are automatically being calibrated, combined, and written to file via the threads started in the constructor
  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<"Processing midas event "<<event.Number()<<" of type 0x"<<std::hex<<event.Type()<<std::dec<<std::endl;
  }
  switch(event.Type()) {
  case FIFOEVENT:
    if(!FifoEvent(event)) {
      if(fSettings->VerbosityLevel() > 0) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Bad FIFO event."<<Attribs::Reset<<std::endl;
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
      std::cout<<"Reached file end, got "<<fClockState.NofStoredCycles()<<" cycles."<<std::endl;
    }
    Flush();
    break;

  default:
    //Unrecognized event type.
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Unknown event type 0x"<<std::hex<<event.Type()<<std::dec<<" for midas event #"<<event.Number()<<Attribs::Reset<<std::endl;
    return false;
  }

  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<"done processing midas event"<<std::endl;
  }

  return true;
}

//---------------------------------------- different midas event types ----------------------------------------
bool MidasEventProcessor::FifoEvent(MidasEvent& event) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Found FIFO event in midas event "<<event.Number()<<std::endl;
  }
  uint32_t fifoStatus = 0;
  uint32_t feraWords = 0;
  uint32_t fifoSerial = 0;

  size_t feraEnd = 0;

  size_t currentFeraStart;

  if(fSettings->VerbosityLevel() > 1) {
    //check for missed events
    if(event.Number() != (fLastEventNumber + 1)) {
      std::cerr<<Foreground::Red<<"Missed "<<event.Number() - fLastEventNumber - 1<<" FIFO data events, between events "<<fLastEventNumber<<" and "<<event.Number()<<Attribs::Reset<<std::endl;
    }

    //Check if events are ordered by time
    if(event.Time() < fLastEventTime) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"FIFO event "<<event.Number()<<" occured before the last event "<<fLastEventNumber<<" ("<<event.Time()<<" < "<<fLastEventTime<<")"<<Attribs::Reset<<std::endl;
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
	  std::cerr<<Attribs::Bright<<Foreground::Red<<"Invalid FIFO status "<<std::hex<<std::setw(8)<<std::setfill('0')<<fifoStatus<<std::dec<<" in event "<<event.Number()<<Attribs::Reset<<std::endl;
	}
	//just continue???
	continue;
      }

      bank.Get(feraWords);

      //Check timeout and overflow bit in ferawords.
      if(feraWords & 0x0000C000) {
	if(fSettings->VerbosityLevel() > 1) {
	  std::cerr<<Foreground::Red<<"Event "<<event.Number()<<", bank "<<bank.Number()<<": FIFO overflow bit or timeout bit set: "<<((feraWords>>14) & 0x3)<<Attribs::Reset<<std::endl;
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
	  std::cerr<<Foreground::Red<<"Missed a "<<fSettings->DetectorType(bank.IntName())<<" FIFO serial in Event "<<event.Number()<<", Bank "<<bank.Number()<<", FIFO serial "<<fifoSerial<<", last FIFO serial "<<fLastFifoSerial[bank.IntName()]<<Attribs::Reset<<std::endl;
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
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Unknown bank name 0x"<<std::hex<<bank.IntName()<<std::dec<<" for bank "<<bank.Number()<<" in midas event "<<event.Number()<<Attribs::Reset<<std::endl;
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
    std::cout<<"Found Scaler event in midas event "<<event.Number()<<std::endl;
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
    std::cout<<"Found epics event in midas event "<<event.Number()<<std::endl;
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
    std::cout<<"Starting on germanium event"<<std::endl;
  }

  uint16_t tmp;
  uint16_t vsn;
  uint16_t feraType;
  uint16_t tmpEnergy;
  std::vector<std::pair<uint16_t, uint16_t> > energy;
  std::map<uint16_t, std::vector<uint16_t> > time;
  Ulm ulm;

  while(bank.ReadPoint() < feraEnd) {
    bank.Get(tmp);

    //skip all zeros
    while(tmp == 0 && bank.ReadPoint() < feraEnd) {
      //increment counter and get next word
      ++fNofZeros[bank.IntName()];
      bank.Get(tmp);
    }

    //get the module number
    vsn = tmp & VHNMASK;

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<"FERA number = "<<vsn<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((tmp & 0x8000) != 0) {
      feraType = tmp & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<tmp<<")"<<std::dec<<std::endl;
    }

    switch(feraType) {
    case VHAD1141:
      //Process the ADC, and check if it's followed immediately by a TDC
      if(vsn >= fSettings->NofGermaniumDetectors()) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Invalid detector number ("<<vsn<<") in Event "<<bank.EventNumber()<<", Bank "<<bank.Number()<<Attribs::Reset<<std::endl;
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
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Invalid detector number ("<<(vsn + 16)<<") in Event "<<bank.EventNumber()<<", Bank "<<bank.Number()<<Attribs::Reset<<std::endl;
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
	  std::cout<<ulm.CycleNumber()<<". cycle: "<<fEventsInCycle<<" events in last cycle"<<std::endl;
	}
	fEventsInCycle = 0;
      } else {
	++fEventsInCycle;
      }
      fLastCycle = ulm.CycleNumber();
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Bad germanium fera"<<Attribs::Reset<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;

    default: /* Unrecognized header */
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<eventNumber<<", found 0x"<<std::hex<<feraType<<std::dec<<" instead"<<Attribs::Reset<<std::endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  ConstructEvents(eventTime, eventNumber, EDetectorType::kGermanium, energy, time, ulm);
}

void MidasEventProcessor::PlasticEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Starting on plastic event"<<std::endl;
  }

  uint16_t tmp;
  uint16_t vsn;
  uint16_t feraType;
  std::vector<std::pair<uint16_t, uint16_t> > energy;
  std::map<uint16_t, std::vector<uint16_t> > time;
  Ulm ulm;

  while(bank.ReadPoint() < feraEnd) {
    bank.Get(tmp);

    //skip all zeros
    while(tmp == 0 && bank.ReadPoint() < feraEnd) {
      //increment counter and get next word
      ++fNofZeros[bank.IntName()];
      bank.Get(tmp);
    }

    //get the module number
    vsn = tmp & VHNMASK;

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<"FERA number = "<<vsn<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((tmp & 0x8000) != 0) {
      feraType = tmp & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<tmp<<")"<<std::dec<<std::endl;
    }

    switch(feraType) {
    case VH4300: //SCEPTAR ENERGY FERA
      GetAdc4300(bank, tmp, vsn, energy);
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
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Found bad fera event in plastic data stream"<<Attribs::Reset<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;

    default: // Unrecognized header
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<bank.EventNumber()<<", found 0x"<<std::hex<<feraType<<std::dec<<" instead"<<Attribs::Reset<<std::endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  ConstructEvents(eventTime, eventNumber, EDetectorType::kPlastic, energy, time, ulm);
}

void MidasEventProcessor::SiliconEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Starting on silicon event"<<std::endl;
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
      std::cout<<"FERA number = "<<vsn<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<header<<")"<<std::dec<<std::endl;
    }

    switch(feraType) {
    case VHAD413:
      //vsn is 0xD or 0xE (13 or 14), so to get the module number we subtract 13
      if(!GetAdc413(bank, vsn-13, (header&VHAD413_NUMBER_OF_DATA_WORDS_MASK)>>VHAD413_DATA_WORDS_OFFSET, energy)) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Same problem with something immediately after ADC 413 data in silicon data stream"<<Attribs::Reset<<std::endl;
      }
      ++fCounter[VHAD413];
      ++nofAdcs;
      break;	
    case VHAD114Si:
      //Process the ADC, and check if it's followed immediately by a TDC
      if(vsn > fSettings->NofSiliconDetectors()) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Invalid detector number ("<<vsn<<") in Event "<<bank.EventNumber()<<", Bank "<<bank.Number()<<Attribs::Reset<<std::endl;
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
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Found bad fera event in silicon data stream"<<Attribs::Reset<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;
		    
    default: // Unrecognized header
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<bank.EventNumber()<<", found 0x"<<std::hex<<feraType<<std::dec<<" instead"<<Attribs::Reset<<std::endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  ConstructEvents(eventTime, eventNumber, EDetectorType::kSilicon, energy, time, ulm);
}

void MidasEventProcessor::BaF2Event(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Starting on barium fluoride event"<<std::endl;
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
      std::cout<<"FERA number = "<<vsn<<std::endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      std::cout<<"FERA type = 0x"<<std::hex<<feraType<<" (from  0x"<<header<<")"<<std::dec<<std::endl;
    }

    switch(feraType) {
    case VHAD413:
      //vsn is 0-4
      if(!GetAdc413(bank, vsn, (header&VHAD413_NUMBER_OF_DATA_WORDS_MASK)>>VHAD413_DATA_WORDS_OFFSET, energy)) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Same problem with something immediately after ADC 413 data in silicon data stream"<<Attribs::Reset<<std::endl;
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
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Found bad fera event in barium fluoride data stream"<<Attribs::Reset<<std::endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      break;
		    
    default: // Unrecognized header
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<bank.EventNumber()<<", found 0x"<<std::hex<<feraType<<std::dec<<" instead"<<Attribs::Reset<<std::endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  ConstructEvents(eventTime, eventNumber, EDetectorType::kBaF2, energy, time, ulm);
}


//---------------------------------------- different electronics modules ----------------------------------------

//get energy from an Adc 114
bool MidasEventProcessor::GetAdc114(Bank& bank, uint32_t feraEnd, uint16_t& energy) {
  uint16_t tdc;

  bank.Get(energy);

  if(energy > VHAD114_ENERGY_MASK) {
    std::cerr<<Foreground::Red<<"ADC 114 energy "<<energy<<" > "<<VHAD114_ENERGY_MASK<<Attribs::Reset<<std::endl;
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

  while(bank.ReadPoint() < feraEnd) {
    if(!bank.Get(highWord)) {
      return false;
    }
    if(!bank.Get(lowWord)) {
      return false;
    }
    
    if((highWord & 0x8000) || (lowWord & 0x8000)) {
      bank.ChangeReadPoint(-4);
      return false;
    }
    
    if((highWord&TDC3377_IDENTIFIER) != (lowWord&TDC3377_IDENTIFIER)) {
      //two words from two different tdcs? output error message
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Tdc identifier mismatch, event "<<bank.EventNumber()<<", bank "<<bank.Number()<<": "<<(highWord&TDC3377_IDENTIFIER)<<" != "<<(lowWord&TDC3377_IDENTIFIER)<<Attribs::Reset<<std::endl;
      return false;
    }

    subAddress = (highWord&TDC3377_IDENTIFIER) >> 10;
    time[subAddress].push_back(((highWord&TDC3377_TIME) << 8) | (lowWord&TDC3377_TIME));
    ++fSubAddress[subAddress];
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
      std::cerr<<Attribs::Bright<<Foreground::Red<<"reached premature end of adc 4300 data: i = "<<i<<", # adc words = "<<nofAdcWords<<Attribs::Reset<<std::endl;
      bank.ChangeReadPoint(-2);
      break;
    }

    subAddress = (tmp&PLASTIC_IDENTIFIER) >> PLASTIC_IDENTIFIER_OFFSET;

    if(vsn*PLASTIC_CHANNELS + subAddress >= fSettings->NofPlasticDetectors()) {
      std::cout<<"Found plastic detector #"<<vsn*PLASTIC_CHANNELS + subAddress<<" in event "<<bank.EventNumber()<<", bank "<<bank.Number()<<", but there should only be "<<fSettings->NofPlasticDetectors()<<std::endl;
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
  
  return true;
}

//----------------------------------------

void MidasEventProcessor::ConstructEvents(const uint32_t& eventTime, const uint32_t& eventNumber, const EDetectorType& detectorType, std::vector<std::pair<uint16_t, uint16_t> >& energy, std::map<uint16_t, std::vector<uint16_t> >& time, const Ulm& ulm) {
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"starting to construct events from "<<energy.size()<<" detectors with "<<time.size()<<" times"<<std::endl;
  }
  size_t nofEvents = 0;

  //check that this is a known detector
  if(detectorType == EDetectorType::kUnknown) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<energy.size()<<" unknown detectors passed on to "<<__PRETTY_FUNCTION__<<Attribs::Reset<<std::endl;
    return;
  }

  //drop all deactivated adcs
  energy.erase(std::remove_if(energy.begin(), energy.end(), [&](const std::pair<uint16_t, uint16_t> en) -> bool {return fSettings->Active(detectorType,en.first);}),energy.end());

  //drop all deactivated tdcs
  auto it = time.begin();
  while(it != time.end()) {
    if(fSettings->Active(detectorType, it->first)) {
      time.erase(it++);//delete this map entry and then go to the next entry (post-increment)
    } else {
      ++it;//just go to the next entry
    }
  }

  //stop if all detectors were deactivated
  if(energy.size() == 0) {
    if(time.size() != 0) {
      if(fSettings->VerbosityLevel() > 0) {
	std::cout<<Foreground::Red<<"No active adcs, but "<<time.size()<<" active tdcs"<<Attribs::Reset<<std::endl;
      }
    } else if(fSettings->VerbosityLevel() > 2) {
      std::cout<<Foreground::Red<<"No active adcs and no active tdcs"<<Attribs::Reset<<std::endl;      
    }
    return;
  }

  //now loop over all detectors, create the event, fill the detector number and energy, find the corresponding times, and fill them too
  for(auto& en : energy) {
    //check whether the circular buffer is full
    //if it is and we can't increase it's capacity, we try once(!) to wait for it to empty
    bool slept = false;
    while(fUncalibratedDetector.full() && !slept) {
      try {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<"Trying to increase uncalibrated detector buffer capacity of "<<fUncalibratedDetector.capacity()<<" by "<<fSettings->UncalibratedBufferSize()<<std::endl;
	}
	fUncalibratedDetector.set_capacity(fUncalibratedDetector.capacity() + fSettings->UncalibratedBufferSize());
      } catch(std::exception& exc) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to increase capacity ("<<fUncalibratedDetector.capacity()<<") of uncalibrated detector buffer by "<<fSettings->UncalibratedBufferSize()<<Attribs::Reset<<std::endl;
	slept = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      }
    }
    fUncalibratedDetector.push_back(Detector(eventTime, eventNumber, static_cast<uint8_t>(detectorType), en, ulm));
    ++nofEvents;
    //check that we have any times for this detector
    if(time.find(en.first) != time.end() && time.at(en.first).size() > 0) {
      fUncalibratedDetector.back().TdcHits(time[en.first].size());
      //find the right tdc hit
      //since any good tdc hit creates a deadtime w/o anymore tdc hits, we want the last one
      //the tdcs are LIFO, so the last hit is the first coming out
      //however in Greg's FIFO.c he uses the last time found within the coarse window or (if none is found) the very first time
      for(const auto& thisTime : time[en.first]) {
	if(fSettings->CoarseTdcWindow(detectorType,en.first,thisTime)) {
	  fUncalibratedDetector.back().Time(thisTime);
	}
      }
      if(fUncalibratedDetector.back().Time() == 0) {
	fUncalibratedDetector.back().Time(time[en.first].front());
      }
    } else {
      //no tdc hits found for this detector
      if(fSettings->VerbosityLevel() > 2) {
	std::cerr<<Foreground::Red<<"Found no tdc hits for detector type "<<std::hex<<static_cast<uint16_t>(detectorType)<<std::dec<<", number "<<en.first<<Attribs::Reset<<std::endl;
      }
    }
  }//for detector

  fNofUncalibratedDetectors += nofEvents;

  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"done with creation of "<<nofEvents<<" events ("<<fUncalibratedDetector.size()<<" uncalibrated detectors in queue, "<<fNofUncalibratedDetectors<<" in total)"<<std::endl;
  }
}

//----------------------------------------

void MidasEventProcessor::Flush() {
  //set status to flush (this triggers the flushing)
  fStatus = kFlush;
  //join all threads, i.e. wait for them to finish flushing
  for(auto& thread : fThreads) {
    //check whether the thread is joinable
    if(thread.joinable()) {
      thread.join();
    } else {
      if(thread.get_id() == std::thread::id()) {
	std::cout<<Attribs::Bright<<Foreground::Blue<<"Thread not joinable to this thread"<<std::endl;
      } else {
	std::cout<<Attribs::Bright<<Foreground::Blue<<"Thread with id "<<thread.get_id()<<" not joinable to this thread with id "<<std::this_thread::get_id()<<std::endl;
      }
    }
  }
}

void MidasEventProcessor::Print() {
  std::cout<<"Events found:"<<std::endl;
  for(auto it : fNofMidasEvents) {
    switch(it.first) {
    case FIFOEVENT:
      std::cout<<"Fifo:\t"<<std::setw(7)<<it.second<<std::endl;
      break;
    case CAMACSCALEREVENT:
      std::cout<<"Camac:\t"<<std::setw(7)<<it.second<<std::endl;
      break;

    case SCALERSCALEREVENT:
      std::cout<<"Scaler:\t"<<std::setw(7)<<it.second<<std::endl;
      break;

    case ISCALEREVENT:
      std::cout<<"i-scaler:\t"<<std::setw(7)<<it.second<<std::endl;
      break;

    case FRONTENDEVENT:
      std::cout<<"Frontend:\t"<<std::setw(7)<<it.second<<std::endl;
      break;

    case EPICSEVENTTYPE:
      std::cout<<"Epics:\t"<<std::setw(7)<<it.second<<std::endl;
      break;

    case FILEEND:
      std::cout<<"File-end:\t"<<std::setw(7)<<it.second<<std::endl;
      break;
    default:
      std::cout<<"Unknown event type 0x"<<std::hex<<it.first<<std::dec<<": "<<std::setw(7)<<it.second<<std::endl;
    }
  }
}

//start calibration thread (takes events from the input buffers, calibrates them and puts them into the calibrated buffer)
void MidasEventProcessor::Calibrate() {
  Detector detector;
  TF1* calibration;
  TH1I* histogram;
  
  //we keep running until all detectors are calibrated and we're not running anymore
  //this ensures that we don't stop when the read queue drops down to zero while reading more events
  while(fStatus == kRun || fUncalibratedDetector.size() > 0) {
    //if there are no uncalibrated events we wait a little bit (and continue to make sure we weren't told to flush in the mean time)
    if(fUncalibratedDetector.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      continue;
    }
    //we only use the first uncalibrated detector, as we're running in parallel to a thread that adds elements to the std::vector
    //to minimize the time we have to lock the queue, we get the first element from it and then delete it
    //do we actually need any lock? the only other access to fUncalibratedDetector is putting things into it, so there shouldn't be any conflict.
    //lock;
    if(fSettings->VerbosityLevel() > 3) {
      std::cout<<"Got "<<fUncalibratedDetector.size()<<" uncalibrated detectors to calibrate!"<<std::endl;
    }
    detector = fUncalibratedDetector.front();
    fUncalibratedDetector.pop_front();
    //unlock;
    //add the energy to the histogram and the detector to the queue
    histogram = fRawEnergyHistograms[detector.DetectorType()][detector.DetectorNumber()];
    histogram->Fill(detector.RawEnergy());

    //check whether the circular buffer is full
    //if it is and we can't increase it's capacity, we try once(!) to wait for it to empty
    bool slept = false;
    while(fWaiting[detector.DetectorType()][detector.DetectorNumber()].full() && !slept) {
      try {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<"Trying to increase waiting buffer ("<<uint16_t(detector.DetectorType())<<", "<<detector.DetectorNumber()<<") capacity of "<<fWaiting[detector.DetectorType()][detector.DetectorNumber()].capacity()<<" by "<<fSettings->MinimumCounts(detector.DetectorType())<<std::endl;
	}
	fWaiting[detector.DetectorType()][detector.DetectorNumber()].set_capacity(fWaiting[detector.DetectorType()][detector.DetectorNumber()].capacity() + fSettings->MinimumCounts(detector.DetectorType()));
      } catch(std::exception& exc) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to increase capacity ("<<fWaiting[detector.DetectorType()][detector.DetectorNumber()].capacity()<<") of waiting buffer "<<detector.DetectorType()<<", "<<detector.DetectorNumber()<<" by "<<fSettings->MinimumCounts(detector.DetectorType())<<Attribs::Reset<<std::endl;
	slept = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      }
    }
    fWaiting[detector.DetectorType()][detector.DetectorNumber()].push_back(detector);
    ++fNofWaiting;
    //check whether we have enough events in histogram to start calibrating
    if(histogram->Integral() > fSettings->MinimumCounts(detector.DetectorType())) {
      TGraph peaks = fCalibration.Calibrate(detector.DetectorType(),detector.DetectorNumber(),histogram);
      //if calibration fails???
      if(peaks.GetN() == 0) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Calibration of histogram '"<<histogram->GetName()<<"' failed!"<<std::endl;
	continue;
      }
      calibration = (TF1*) peaks.GetListOfFunctions()->FindObject("Calibration");
      if(calibration == nullptr) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Calibration of histogram '"<<histogram->GetName()<<"' failed!"<<std::endl;
	continue;
      }
      //we're done calibrating so we can remove the first detector of the same type and number from the spectrum and queue
      detector = fWaiting[detector.DetectorType()][detector.DetectorNumber()].front();
      histogram->Fill(detector.RawEnergy(),-1);
      fWaiting[detector.DetectorType()][detector.DetectorNumber()].pop_front();
      //add calibrated energy to detector
      detector.Energy(calibration->Eval(detector.RawEnergy()));
      //add detector to calibrated queue
      //lock;
      fCalibratedDetector.insert(detector);
      ++fNofCalibratedDetectors;
      //unlock;
    }//if enough events
  }//while loop

  std::cout<<__PRETTY_FUNCTION__<<" finished with status "<<fStatus<<", fUncalibratedDetector.size() = "<<fUncalibratedDetector.size()<<std::endl;
}

//start event building thread (takes events from calibrated buffer and combines them into build events in the output buffer)
void MidasEventProcessor::BuildEvents() {
  std::vector<Detector> detectors;

  while(fStatus == kRun || fCalibratedDetector.size() > 0) {
    //if there are no calibrated events we wait a little bit (and continue to make sure we weren't told to flush in the mean time)
    if(fCalibratedDetector.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      continue;
    }
    //fCalibratedDetector is a multiset, i.e. ordered values which can have the same value
    //so we first want to check whether the time difference between begin and end is within the waiting window
    if(fSettings->InWaitingWindow(fCalibratedDetector.begin()->GetUlm().Clock(), fCalibratedDetector.end()->GetUlm().Clock())) {
      continue;
    }

    //our oldest time is outside the waiting window, so we take that detector out of the list
    detectors.clear();
    detectors.push_back(*(fCalibratedDetector.begin()));
    fCalibratedDetector.erase(fCalibratedDetector.begin());
    //now we want to find all that are in coincidence with that detector
    for(auto iterator = fCalibratedDetector.begin(); iterator != fCalibratedDetector.end(); ++iterator) {
      if(fSettings->Coincidence(detectors[0].Time(), iterator->GetUlm().Clock())) {
	detectors.push_back(*iterator);
	//if this detector is also outside the waiting window we remove it as well
	if(!fSettings->InWaitingWindow(iterator->GetUlm().Clock(), fCalibratedDetector.end()->GetUlm().Clock())) {
	  fCalibratedDetector.erase(iterator);
	}
      } else {
	//the set is ordered, so if this detector is outside the coincidence window all followings will be outside as well
	break;
      }
    }

    //check whether the circular buffer is full
    //if it is and we can't increase it's capacity, we try once(!) to wait for it to empty
    bool slept = false;
    while(fBuiltEvents.full() && !slept) {
      try {
	if(fSettings->VerbosityLevel() > 0) {
	  std::cout<<"Trying to increase built events buffer capacity of "<<fBuiltEvents.capacity()<<" by "<<fSettings->BuiltEventsSize()<<std::endl;
	}
	fBuiltEvents.set_capacity(fBuiltEvents.capacity() + fSettings->BuiltEventsSize());
      } catch(std::exception& exc) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to increase capacity ("<<fBuiltEvents.capacity()<<") of built events buffer by "<<fSettings->BuiltEventsSize()<<Attribs::Reset<<std::endl;
	slept = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      }
    }
    fBuiltEvents.push_back(Event(detectors));
    ++fNofBuiltEvents;
    if(fSettings->VerbosityLevel() > 1) {
      std::cout<<"Built event with "<<detectors.size()<<" detectors"<<std::endl;
    }
  }//while loop
  std::cout<<__PRETTY_FUNCTION__<<" finished with status "<<fStatus<<", fCalibratedDetector.size() = "<<fCalibratedDetector.size()<<std::endl;
}

//start output thread (writes event in the output buffer to file/tree)
void MidasEventProcessor::FillTree() {
  while(fStatus == kRun || fBuiltEvents.size() > 0) {
    //if there are no uncalibrated events we wait a little bit (and continue to make sure we weren't told to flush in the mean time)
    if(fBuiltEvents.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(STANDARD_WAIT_TIME));
      continue;
    }
    //make the leaf point to the first event
    fLeaf = &(fBuiltEvents.front());
    //fill the tree (this writes the first event to file)
    fTree->Fill();
    //delete the first event
    fBuiltEvents.pop_front();
    if(fSettings->VerbosityLevel() > 1) {
      std::cout<<"Wrote one event to tree."<<std::endl;
    }
  }//while loop

  std::cout<<__PRETTY_FUNCTION__<<" finished with status "<<fStatus<<", fBuiltEvents.size() = "<<fBuiltEvents.size()<<std::endl;
}

void MidasEventProcessor::StatusUpdate() {
  size_t waitingGermanium;
  size_t waitingPlastic;
  size_t waitingSilicon;
  size_t waitingBaF2;
  size_t waitingSum;
  while(fStatus == kRun) {
    waitingGermanium = 0;
    for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kGermanium)]) {
      waitingGermanium += det.size();
    }
    waitingPlastic = 0;
    for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kPlastic)]) {
      waitingPlastic += det.size();
    }
    waitingSilicon = 0;
    for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kSilicon)]) {
      waitingSilicon += det.size();
    }
    waitingBaF2 = 0;
    for(const auto& det : fWaiting[static_cast<uint8_t>(EDetectorType::kBaF2)]) {
      waitingBaF2 += det.size();
    }
    waitingSum = waitingGermanium + waitingPlastic + waitingSilicon + waitingBaF2;
    std::cout<<fUncalibratedDetector.size()<<"/"<<fNofUncalibratedDetectors<<" uncalibrated detectors, "
	     <<waitingSum<<"/"<<fNofWaiting<<" waiting detectors ("<<waitingGermanium<<" Ge, "<<waitingPlastic<<" Pl, "<<waitingSilicon<<" Si, "<<waitingBaF2<<" BaF2) , "
	     <<fCalibratedDetector.size()<<"/"<<fNofCalibratedDetectors<<" calibrated detectors, "
	     <<fBuiltEvents.size()<<"/"<<fNofBuiltEvents<<" built events, "
	     <<fTree->GetEntries()<<" entries in tree"<<std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
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
