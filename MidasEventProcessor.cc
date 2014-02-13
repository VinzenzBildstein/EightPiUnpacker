#include "MidasEventProcessor.hh"

#include <iomanip>

#include "TextAttributes.hh"

#include "MidasFileManager.hh"

using namespace std;

MidasEventProcessor::MidasEventProcessor(Settings* settings, TTree* tree) {
  fSettings = settings;
  fTree = tree;

  //attach leaf to tree
  int BufferSize = 1024000;
  fTree->Branch("Event",&fLeaf, BufferSize);
  fTree->BranchRef();

  //increase maximum tree size to 10GB
  Long64_t GByte = 1073741824L;
  fTree->SetMaxTreeSize(10*GByte);

  //start calibration thread (takes events from the input buffer, calibrates them and puts them into the calibrated buffer)
  fThreads.push_back(thread(&MidasEventProcessor::Calibrate, this));
  //start event building thread (takes events from calibrated buffer and combines them into build events in the output buffer)
  fThreads.push_back(thread(&MidasEventProcessor::BuildEvents, this));
  //start output thread (writes event in the output buffer to file/tree)
  fThreads.push_back(thread(&MidasEventProcessor::FillTree, this));

  fLastCycle = 0;
  fEventsInCycle = 0;

  fTemperatureFile.open(fSettings->TemperatureFile());
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
    cout<<"Processing midas event "<<event.Number()<<" of type 0x"<<hex<<event.Type()<<dec<<endl;
  }
  switch(event.Type()) {
  case FIFOEVENT:
    if(!FifoEvent(event)) {
      if(fSettings->VerbosityLevel() > 0) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Bad FIFO event."<<Attribs::Reset<<endl;
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
      cout<<"Reached file end, got "<<fClockState.NofStoredCycles()<<" cycles."<<endl;
    }
    Flush();
    break;

  default:
    //Unrecognized event type.
    cerr<<Attribs::Bright<<Foreground::Red<<"Unknown event type 0x"<<hex<<event.Type()<<dec<<" for midas event #"<<event.Number()<<Attribs::Reset<<endl;
    return false;
  }

  if(fSettings->VerbosityLevel() > 2) {
    cout<<"done processing midas event"<<endl;
  }

  return true;
}

//---------------------------------------- different midas event types ----------------------------------------
bool MidasEventProcessor::FifoEvent(MidasEvent& event) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Found FIFO event in midas event "<<event.Number()<<endl;
  }
  uint32_t fifoStatus = 0;
  uint32_t feraWords = 0;
  uint32_t fifoSerial = 0;

  size_t feraEnd = 0;

  size_t currentFeraStart;

  if(fSettings->VerbosityLevel() > 1) {
    //check for missed events
    if(event.Number() != (fLastEventNumber + 1)) {
      cerr<<Foreground::Red<<"Missed "<<event.Number() - fLastEventNumber - 1<<" FIFO data events, between events "<<fLastEventNumber<<" and "<<event.Number()<<Attribs::Reset<<endl;
    }

    //Check if events are ordered by time
    if(event.Time() < fLastEventTime) {
      cerr<<Attribs::Bright<<Foreground::Red<<"FIFO event "<<event.Number()<<" occured before the last event "<<fLastEventNumber<<" ("<<event.Time()<<" < "<<fLastEventTime<<")"<<Attribs::Reset<<endl;
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
	  cerr<<Attribs::Bright<<Foreground::Red<<"Invalid FIFO status "<<hex<<setw(8)<<setfill('0')<<fifoStatus<<dec<<" in event "<<event.Number()<<Attribs::Reset<<endl;
	}
	//just continue???
	continue;
      }

      bank.Get(feraWords);

      //Check timeout and overflow bit in ferawords.
      if(feraWords & 0x0000C000) {
	if(fSettings->VerbosityLevel() > 1) {
	  cerr<<Foreground::Red<<"Event "<<event.Number()<<", bank "<<bank.Number()<<": FIFO overflow bit or timeout bit set: "<<((feraWords>>14) & 0x3)<<Attribs::Reset<<endl;
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
	  cerr<<Foreground::Red<<"Missed a "<<fSettings->DetectorType(bank.IntName())<<" FIFO serial in Event "<<event.Number()<<", Bank "<<bank.Number()<<", FIFO serial "<<fifoSerial<<", last FIFO serial "<<fLastFifoSerial[bank.IntName()]<<Attribs::Reset<<endl;
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
	cerr<<Attribs::Bright<<Foreground::Red<<"Unknown bank name 0x"<<hex<<bank.IntName()<<dec<<" for bank "<<bank.Number()<<" in midas event "<<event.Number()<<Attribs::Reset<<endl;
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
    cout<<"FIFO event done"<<endl;
  }

  return true;
}

bool MidasEventProcessor::CamacScalerEvent(MidasEvent& event, vector<vector<uint16_t> > mcs) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Found Scaler event in midas event "<<event.Number()<<endl;
  }
  uint32_t tmp;
  //loop over banks
  for(auto bank : event.Banks()) {
    if(bank.IsBank("MCS0")) {
      //reset mcs
      mcs.resize(NOF_MCS_CHANNELS,vector<uint16_t>());
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
    cout<<"Scaler event done"<<endl;
  }

  return true;
}

bool MidasEventProcessor::EpicsEvent(MidasEvent& event) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Found epics event in midas event "<<event.Number()<<endl;
  }
  float tmp;

  //loop over banks or just get the right bank?
  //for(auto& bank : event.Banks()) {
  Bank bank = event.Banks()[1];
  for(int j = 0; bank.GotData(); ++j) {
    bank.Get(tmp);
    //tmp = ((tmp<<16)&0xffff0000) | ((tmp>>16)&0xffff);
    if(j==14) {
      fTemperatureFile<<tmp<<endl;
      break;
    }
  }
  //}

  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Epics event done"<<endl;
  }

  return true;
}

//---------------------------------------- different detector types ----------------------------------------

void MidasEventProcessor::GermaniumEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Starting on germanium event"<<endl;
  }

  uint16_t tmp;
  uint16_t vsn;
  uint16_t feraType;
  uint16_t detector = fSettings->NofGermaniumDetectors();
  uint16_t energy;
  vector<uint16_t> subAddress;
  vector<uint16_t> time;
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
      cout<<"FERA number = "<<vsn<<endl;
    }

    //get the module type (high bit has to be set)
    if((tmp & 0x8000) != 0) {
      feraType = tmp & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      cout<<"FERA type = 0x"<<hex<<feraType<<" (from  0x"<<tmp<<")"<<dec<<endl;
    }

    switch(feraType) {
    case VHAD1141:
      //Process the ADC, and check if it's followed immediately by a TDC
      detector = vsn;
      if(detector >= fSettings->NofGermaniumDetectors()) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Invalid detector number ("<<detector<<") in Event "<<bank.EventNumber()<<", Bank "<<bank.Number()<<Attribs::Reset<<endl;
      }
      
      if(GetAdc114(bank, feraEnd, energy)) {
	GetTdc3377(bank, feraEnd, subAddress, time);
	++fCounter[VH3377];
      }
      ++fCounter[VHAD1141];
      break;

    case VHAD1142:
      //Process the ADC, and check if it's followed immediately by a TDC
      detector = vsn + 16;
      if(detector >= fSettings->NofGermaniumDetectors()) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Invalid detector number ("<<detector<<") in Event "<<bank.EventNumber()<<", Bank "<<bank.Number()<<Attribs::Reset<<endl;
      }

      if(GetAdc114(bank, feraEnd, energy)) {
	GetTdc3377(bank, feraEnd, subAddress, time);
	++fCounter[VH3377];
      }
      ++fCounter[VHAD1142];
      break;

    case VH3377: /* 3377 TDC */
      GetTdc3377(bank, feraEnd, time, subAddress);
      ++fCounter[VH3377];
      break;

    case VHFULM:  /* Universal Logic Module end of event marking, clocks, etc.. */
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      if(ulm.CycleNumber() != fLastCycle && fLastCycle != 0) {
	if(fSettings->VerbosityLevel() > 0) {
	  cout<<ulm.CycleNumber()<<". cycle: "<<fEventsInCycle<<" events in last cycle"<<endl;
	}
	fEventsInCycle = 0;
      } else {
	++fEventsInCycle;
      }
      fLastCycle = ulm.CycleNumber();
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Bad germanium fera"<<Attribs::Reset<<endl;
      }
      ++fCounter[BADFERA];

      bank.SetReadPoint(feraEnd);
      return;

    default: /* Unrecognized header */
      cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<eventNumber<<", found 0x"<<hex<<feraType<<dec<<" instead"<<Attribs::Reset<<endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  fGermanium.push_back(Germanium(eventTime, eventNumber, detector, energy, subAddress, time, ulm));
}

void MidasEventProcessor::PlasticEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Starting on plastic event"<<endl;
  }

  uint16_t tmp;
  uint16_t vsn;
  uint16_t feraType;
  vector<uint16_t> detector;
  vector<uint16_t> energy;
  vector<uint16_t> subAddress;
  vector<uint16_t> time;
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
      cout<<"FERA number = "<<vsn<<endl;
    }

    //get the module type (high bit has to be set)
    if((tmp & 0x8000) != 0) {
      feraType = tmp & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      cout<<"FERA type = 0x"<<hex<<feraType<<" (from  0x"<<tmp<<")"<<dec<<endl;
    }

    switch(feraType) {
    case VH4300: //SCEPTAR ENERGY FERA
      GetAdc4300(bank, tmp, vsn, energy, detector);
      ++fCounter[VH4300];
      break;		    

    case VH3377: // 3377 TDC
      GetTdc3377(bank, feraEnd, time, subAddress);
      ++fCounter[VH3377];
      break;

    case VHFULM:  // Universal Logic Module end of event marking, clocks, etc..
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Found bad fera event in plastic data stream"<<Attribs::Reset<<endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      return;

    default: // Unrecognized header
      cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<bank.EventNumber()<<", found 0x"<<hex<<feraType<<dec<<" instead"<<Attribs::Reset<<endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  fPlastic.push_back(Plastic(eventTime, eventNumber, detector, energy, subAddress, time, ulm));
}

void MidasEventProcessor::SiliconEvent(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Starting on silicon event"<<endl;
  }

  uint16_t header;
  uint16_t vsn;
  uint16_t feraType;
  int nofAdcs = 0;
  vector<uint16_t> detector;
  vector<uint16_t> energy;
  vector<uint16_t> subAddress;
  vector<uint16_t> time;
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
      cout<<"FERA number = "<<vsn<<endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      cout<<"FERA type = 0x"<<hex<<feraType<<" (from  0x"<<header<<")"<<dec<<endl;
    }

    switch(feraType) {
    case VHAD413:
      //vsn is 0xD or 0xE (13 or 14), so to get the module number we subtract 13
      if(!GetAdc413(bank, vsn-13, (header&VHAD413_NUMBER_OF_DATA_WORDS_MASK)>>VHAD413_DATA_WORDS_OFFSET, detector, energy)) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Same problem with something immediately after ADC 413 data in silicon data stream"<<Attribs::Reset<<endl;
      }
      ++fCounter[VHAD413];
      ++nofAdcs;
      break;	
    case VHAD114Si:
      //Process the ADC, and check if it's followed immediately by a TDC
      if(vsn > fSettings->NofSiliconDetectors()) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Invalid detector number ("<<vsn<<") in Event "<<bank.EventNumber()<<", Bank "<<bank.Number()<<Attribs::Reset<<endl;
      }

      energy.push_back(0);
      if(GetAdc114(bank, feraEnd, energy.back())) {
	GetTdc3377(bank, feraEnd, subAddress, time);
	++fCounter[VH3377];
      }
      ++fCounter[VHAD114Si];
      break;		    

    case VH3377:
      GetTdc3377(bank, feraEnd, time, subAddress);
      ++fCounter[VH3377];
      break;

    case VHFULM:  /* Universal Logic Module end of event marking, clocks, etc.. */
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Found bad fera event in silicon data stream"<<Attribs::Reset<<endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      return;
		    
    default: // Unrecognized header
      cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<bank.EventNumber()<<", found 0x"<<hex<<feraType<<dec<<" instead"<<Attribs::Reset<<endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  fSilicon.push_back(Silicon(eventTime, eventNumber, detector, energy, subAddress, time, ulm));
}

void MidasEventProcessor::BaF2Event(Bank& bank, size_t feraEnd, uint32_t eventTime, uint32_t eventNumber) {
  if(fSettings->VerbosityLevel() > 3) {
    cout<<"Starting on barium fluoride event"<<endl;
  }

  uint16_t header;
  uint16_t vsn;
  uint16_t feraType;
  vector<uint16_t> detector;
  vector<uint16_t> energy;
  vector<uint16_t> subAddress;
  vector<uint16_t> time;
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
      cout<<"FERA number = "<<vsn<<endl;
    }

    //get the module type (high bit has to be set)
    if((header & 0x8000) != 0) {
      feraType = header & VHTMASK;
    } else {
      feraType = BADFERA;
    }

    if(fSettings->VerbosityLevel() > 4) {
      cout<<"FERA type = 0x"<<hex<<feraType<<" (from  0x"<<header<<")"<<dec<<endl;
    }

    switch(feraType) {
    case VHAD413:
      //vsn is 0-4
      if(!GetAdc413(bank, vsn, (header&VHAD413_NUMBER_OF_DATA_WORDS_MASK)>>VHAD413_DATA_WORDS_OFFSET, detector, energy)) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Same problem with something immediately after ADC 413 data in silicon data stream"<<Attribs::Reset<<endl;
      }
      ++fCounter[VHAD413];
      break;	
	
    case VH3377:
      GetTdc3377(bank, feraEnd, time, subAddress);
      ++fCounter[VH3377];
      break;

    case VHFULM:  /* Universal Logic Module end of event marking, clocks, etc.. */
      GetUlm(bank, ulm);
      ++fCounter[VHFULM];
      break;

    case BADFERA:
      if(fSettings->VerbosityLevel() > 1) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Found bad fera event in barium fluoride data stream"<<Attribs::Reset<<endl;
      }
      ++fCounter[BADFERA];
      bank.SetReadPoint(feraEnd);
      return;
		    
    default: // Unrecognized header
      cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find FERA header in midas event "<<bank.EventNumber()<<", found 0x"<<hex<<feraType<<dec<<" instead"<<Attribs::Reset<<endl;
      bank.SetReadPoint(feraEnd);
      return;
    }
  }

  //need to take care of: clockstate, ulm overflows, live clock overflows, dead times
  fBaF2.push_back(BaF2(eventTime, eventNumber, detector, energy, subAddress, time, ulm));
}


//---------------------------------------- different electronics modules ----------------------------------------

//get energy from an Adc 114
bool MidasEventProcessor::GetAdc114(Bank& bank, uint32_t feraEnd, uint16_t& energy) {
  uint16_t tdc;

  bank.Get(energy);

  if(energy > VHAD114_ENERGY_MASK) {
    cerr<<Foreground::Red<<"ADC 114 energy "<<energy<<" > "<<VHAD114_ENERGY_MASK<<Attribs::Reset<<endl;
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
bool MidasEventProcessor::GetAdc413(Bank& bank, uint16_t module, uint16_t nofDataWords, vector<uint16_t>& detector, vector<uint16_t>& energy) {
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
    energy.push_back(data&VHAD413_ENERGY_MASK);
    detector.push_back(module*4 + subAddress);
  }

  return true;
}

//read high and low word from tdc (extracting time and sub-address) until no more tdc data is left
bool MidasEventProcessor::GetTdc3377(Bank& bank, uint32_t feraEnd, vector<uint16_t>& subAddress, vector<uint16_t>& time) {
  subAddress.resize(0);
  time.resize(0);

  uint16_t highWord;
  uint16_t lowWord;

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
      cerr<<Attribs::Bright<<Foreground::Red<<"Tdc identifier mismatch, event "<<bank.EventNumber()<<", bank "<<bank.Number()<<": "<<(highWord&TDC3377_IDENTIFIER)<<" != "<<(lowWord&TDC3377_IDENTIFIER)<<Attribs::Reset<<endl;
      return false;
    }

    subAddress.push_back((highWord&TDC3377_IDENTIFIER) >> 10);
    time.push_back(((highWord&TDC3377_TIME) << 8) | (lowWord&TDC3377_TIME));
    ++fSubAddress[subAddress.back()];
  }

  return true;
}

bool MidasEventProcessor::GetAdc4300(Bank& bank, uint16_t header, uint16_t vsn, vector<uint16_t>& detector, vector<uint16_t>& energy) {
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
      cerr<<Attribs::Bright<<Foreground::Red<<"reached premature end of adc 4300 data: i = "<<i<<", # adc words = "<<nofAdcWords<<Attribs::Reset<<endl;
      bank.ChangeReadPoint(-2);
      break;
    }

    subAddress = (tmp&PLASTIC_IDENTIFIER) >> PLASTIC_IDENTIFIER_OFFSET;

    if(vsn*PLASTIC_CHANNELS + subAddress >= fSettings->NofPlasticDetectors()) {
      cout<<"Found plastic detector #"<<vsn*PLASTIC_CHANNELS + subAddress<<" in event "<<bank.EventNumber()<<", bank "<<bank.Number()<<", but there should only be "<<fSettings->NofPlasticDetectors()<<endl;
      continue;
    }

    detector.push_back(vsn*PLASTIC_CHANNELS + subAddress);
    energy.push_back(tmp&PLASTIC_ENERGY);
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

void MidasEventProcessor::Flush() {
  //set flushing to true (this triggers the flushing)
  fFlushing = true;
  //join all threads, i.e. wait for them to finish flushing
  for(auto& thread : fThreads) {
    //check whether the thread is joinable
    if(thread.joinable()) {
      thread.join();
    } else {
      if(thread.get_id() == thread::id()) {
	cout<<Attribs::Bright<<Foreground::Blue<<"Thread not joinable to this thread"<<endl;
      } else {
	cout<<Attribs::Bright<<Foreground::Blue<<"Thread with id "<<thread.get_id()<<" not joinable to this thread with id "<<this_thread::get_id()<<endl;
      }
    }
  }
}

void MidasEventProcessor::Print() {
  cout<<"Events found:"<<endl;
  for(auto it : fNofMidasEvents) {
    switch(it.first) {
    case FIFOEVENT:
      cout<<"Fifo:\t"<<setw(7)<<it.second<<endl;
      break;
    case CAMACSCALEREVENT:
      cout<<"Camac:\t"<<setw(7)<<it.second<<endl;
      break;

    case SCALERSCALEREVENT:
      cout<<"Scaler:\t"<<setw(7)<<it.second<<endl;
      break;

    case ISCALEREVENT:
      cout<<"i-scaler:\t"<<setw(7)<<it.second<<endl;
      break;

    case FRONTENDEVENT:
      cout<<"Frontend:\t"<<setw(7)<<it.second<<endl;
      break;

    case EPICSEVENTTYPE:
      cout<<"Epics:\t"<<setw(7)<<it.second<<endl;
      break;

    case FILEEND:
      cout<<"File-end:\t"<<setw(7)<<it.second<<endl;
      break;
    default:
      cout<<"Unknown event type 0x"<<hex<<it.first<<dec<<": "<<setw(7)<<it.second<<endl;
    }
  }
}

//start calibration thread (takes events from the input buffer, calibrates them and puts them into the calibrated buffer)
void MidasEventProcessor::Calibrate() {
//      while(fInputBuffer.size() > 0) {
//	//check if this detector is active
//	if(ACTIVE_DETECTORS & (1 << fInputBuffer.front().DetectorType())) {
//	  //we've got an active detector
//	  GainCorrect(fInputBuffer.front());
//	  while(fOutputBuffer.size() > 0) {
//
//	    addEventToConstructorQueue(correctedGeneralEventQueue.front(), &combinedeventqueue);
//	    processCombinedEvents();
//	  }
//	} else {
//	  numberofenergylessevents++;
//	}
//	fInputBuffer.pop_front();
//      }
}
//start event building thread (takes events from calibrated buffer and combines them into build events in the output buffer)
void MidasEventProcessor::BuildEvents() {
}
//start output thread (writes event in the output buffer to file/tree)
void MidasEventProcessor::FillTree() {
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
