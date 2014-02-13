#include "MidasFileManager.hh"

#include "TextAttributes.hh"

using namespace std;

MidasFileManager::~MidasFileManager() {
  if(!fFileName.empty()) {
    file_mapping::remove(fFileName.c_str());
  }
}

bool MidasFileManager::Open(string fileName) {
  fFileName = fileName;
  try {
    fMapping = new file_mapping(fileName.c_str(), read_only);
    fRegion = new mapped_region(*fMapping, read_only);
    fStartAddress = static_cast<uint8_t*>(fRegion->get_address());
    fReadAddress = fStartAddress;
    fSize = fRegion->get_size();
  } catch(exception& exc) {
    file_mapping::remove(fileName.c_str());
    cerr<<Attribs::Bright<<Foreground::Red<<"Unhandled exception "<<exc.what()<<Attribs::Reset<<endl;
    return false;
  }

  if(fSettings->VerbosityLevel() > 0) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Input file size is "<<fSize<<" bytes."<<Attribs::Reset<<endl;
  }
  
  return true;
}

MidasFileHeader MidasFileManager::ReadHeader() {
  //The event header has the format
  //<event id> | <trigger mask>          (each 16 bits)
  //<serial number>                      (32 bits)
  //<time stamp>                         (32 bits)
  //<event data size>                    (32 bits)

  //for the file header the event id has to be 0x494d ('MI') and the trigger mask is 0x8000
  //the event data contained is an ASCII dump of the ODB

  //we could use the first four bytes to determine the endianess, i.e. whether we need to flip bytes

  MidasFileHeader fileHeader;

  //first check that the size is at least 16 bytes (4 * 32bit)
  if(BytesLeft() < 16) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left to read the header: "<<BytesLeft()<<" < 16"<<Attribs::Reset<<endl;
    exit(-1);
  }

  //get the file header and advance the read address
  uint32_t* fileHeaderWord = reinterpret_cast<uint32_t*>(fReadAddress);
  if((fileHeaderWord[0] & 0xffff) != 0x8000) {
    cerr<<Attribs::Bright<<Foreground::Red<<"ERROR! Bad Midas start number (should be 0x8000): 0x"<<hex<<fileHeaderWord[0]<<dec<<" = "<<fileHeaderWord[0]<<endl
	<<"Value at read address  = '"<<fReadAddress<<"', at start address '"<<fStartAddress<<"'"<<Attribs::Reset<<endl;
    exit(-1);
  }
  fReadAddress += 16;//4 32bit words => 16 bytes

  fileHeader.RunNumber(fileHeaderWord[1]);
  fileHeader.StartTime(fileHeaderWord[2]);
  //header information length is in bytes, but header information is stored in 16bit words => divide length by two
  fileHeader.InformationLength(fileHeaderWord[3]/2);

  //check that at least fileHeaderWord[3] bytes are left
  if(BytesLeft() < fileHeaderWord[3]) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left to read the header information: "<<BytesLeft()<<" < "<<fileHeaderWord[3]<<Attribs::Reset<<endl;
    exit(-1);
  }

  //read the header information and advance the read address
  copy(reinterpret_cast<uint16_t*>(fReadAddress),reinterpret_cast<uint16_t*>(fReadAddress+fileHeaderWord[3]),fileHeader.Information().begin());
  fReadAddress += fileHeaderWord[3];
       
  return fileHeader;
}

bool MidasFileManager::ReadHeader(MidasEvent& event) {
  //the event header has 24 bytes:
  //type - 2 bytes, mask - 2 bytes
  //number - 4 bytes
  //time - 4 bytes
  //nof event bytes - 4 bytes
  //total bank bytes - 4 bytes
  //flags - 4 bytes
  if(BytesLeft() < 24) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Error reading event header (24 bytes), only "<<BytesLeft()<<" bytes left"<<endl
	<<"Assuming end of file, file is most likely corrupt"<<Attribs::Reset<<endl;
    event.EoF();
    fStatus = kEoF;
    return false;
  } else if(fSettings->VerbosityLevel() > 0) {
    cout<<"reading header (24 bytes), got "<<BytesLeft()<<" bytes left (size = "<<fSize<<", position = "<<Position()<<")"<<endl;
  }

  //read the header information
  event.fType = *(reinterpret_cast<uint16_t*>(fReadAddress));
  fReadAddress += 2;

  event.fMask = *(reinterpret_cast<uint16_t*>(fReadAddress));
  fReadAddress += 2;

  event.fNumber = *(reinterpret_cast<uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fTime = *(reinterpret_cast<uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fNofBytes = *(reinterpret_cast<uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fTotalBankBytes = *(reinterpret_cast<uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fFlags = *(reinterpret_cast<uint32_t*>(fReadAddress));
  fReadAddress += 4;

  if(fSettings->VerbosityLevel() > 2) {
    cout<<endl
	<<hex<<"Got event header with type 0x"<<event.fType<<", mask 0x"<<event.fMask<<", time 0x"<<event.fTime<<dec<<", "<<event.fNofBytes<<" bytes, "<<event.fTotalBankBytes<<" total bytes, and flags 0x"<<hex<<event.fFlags<<dec<<endl;
  }

  return true;
}

bool MidasFileManager::Read(MidasEvent& event) {
  size_t nofBankBytesRead = 0; //Does not include the event header

  int bankBytesRead = 0;

  if(!ReadHeader(event)) {
    return false;
  }

  //$$$$Possibly remove:
  if(event.IsEoF()) {
    event.EoF();
    return false;
  }

  //Don't include header size in numberofeventbytesinput.

  //Double check size of event.
  if((event.fTotalBankBytes + 8) != event.fNofBytes) {
    //error.
    cerr<<Attribs::Bright<<Foreground::Red<<"The number of event bytes and total bank bytes do not agree in event "<<event.fNumber<<endl
	<<"There are "<<event.fTotalBankBytes<<" total bank bytes, and "<<event.fNofBytes<<" event bytes."<<endl
	<<"Looking for next good event."<<Attribs::Reset<<endl;

    //Need to go looking for the next good event ...
    while((event.fTotalBankBytes + 8) != event.fNofBytes) {
      //Return all but 4 of the 24 event header bytes.
      fReadAddress -= 20;

      if(!ReadHeader(event)) {
	cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find good event."<<Attribs::Reset<<endl;
	return false;
      }
    }

    cerr<<Attribs::Bright<<Foreground::Green<<"Recovered - found next good event."<<Attribs::Reset<<endl;
  }

  if((event.fFlags != 0x11) && (event.fFlags != 0x1)) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Bad flags of 0x"<<hex<<event.fFlags<<dec<<" in event "<<event.fNumber<<Attribs::Reset<<endl;
  }

  if(fSettings->VerbosityLevel() > 2) {
    cout<<"Starting on event "<<event.fNumber<<" with "<<event.fNofBytes<<" bytes and "<<event.fTotalBankBytes<<" bank bytes. Flags are 0x"<<hex<<event.fFlags<<dec<<endl;
  }

  //Fill the banks.
  while(nofBankBytesRead < event.fTotalBankBytes) {
    try {
      event.fBanks.push_back(Bank(event.fBanks.size()));
    } catch(exception exc) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Failed to allocate memory for bank #"<<event.fBanks.size()<<" in event "<<event.fNumber<<": "<<exc.what()<<Attribs::Reset<<endl;
      exit(1);
    }

    bankBytesRead = Read(event.fBanks.back(), event.fTotalBankBytes - nofBankBytesRead, event.fFlags); //readBank() returns the number of bytes read.

    nofBankBytesRead += bankBytesRead;

    if(bankBytesRead < 1) {
      if(bankBytesRead == -10) {
	//Unexpected end of file - likely hardware issue.
	cerr<<Attribs::Bright<<Foreground::Red<<__PRETTY_FUNCTION__<<": unexpected end of file"<<Attribs::Reset<<endl;
	event.EoF();
	return false;
      }
      //We're likely in an infinite loop.
      //$$$$$Error spectrum.
      //Do something about it!
      cerr<<Attribs::Bright<<Foreground::Red<<"Error, no bytes read in event "<<event.fNumber<<" for bank "<<event.fBanks.size()<<Attribs::Reset<<endl;
      event.fBanks.pop_back(); //Don't process the rest of the banks.
      return false;
    }

    event.fBanks.back().fEventNumber = event.fNumber;
  }

  if(nofBankBytesRead != event.fTotalBankBytes) {
    //Trouble, too many bytes inputted.
    cerr<<Attribs::Bright<<Foreground::Red<<"Number of bytes in event "<<event.fNumber<<" does not agree with number of bytes in banks."<<Attribs::Reset<<endl;
    return false;
  }

  return true;
}

int MidasFileManager::Read(Bank& bank, unsigned int maxBytes, unsigned int flags) {
  //$$$$ Could add a check for byte flipping - maybe in readEvent
  unsigned int nofHeaderBytes = 0;

  bank.fReadPoint = 0; //Start reading the bank at the first byte.

  if(fSettings->VerbosityLevel() > 2) {
    cout<<"starting to read "<<maxBytes<<" bytes from bank with flags "<<flags<<" (BANK32 = "<<BANK32<<")"<<endl;
  }

  if(flags & BANK32) {
    nofHeaderBytes = 12;

    if(maxBytes < nofHeaderBytes) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bank header."<<Attribs::Reset<<endl;
      return -1;
    }
    if(BytesLeft() < nofHeaderBytes) {
      //end of file
      return -10;
    }

    copy(fReadAddress,fReadAddress+4,bank.fName);
    fReadAddress += 4;

    bank.fType = *(reinterpret_cast<uint32_t*>(fReadAddress));
    fReadAddress += 4;

    bank.fSize = *(reinterpret_cast<uint32_t*>(fReadAddress));
    fReadAddress += 4;

    if(bank.fSize > (maxBytes - nofHeaderBytes)) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bankbytes."<<Attribs::Reset<<endl;
      fReadAddress += maxBytes - nofHeaderBytes;
      return -1;
    }
  } else {
    nofHeaderBytes = 8;

    if(maxBytes < nofHeaderBytes) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bank header."<<Attribs::Reset<<endl;
      return -1;
    }
    if(BytesLeft() < nofHeaderBytes) {
      //end of file
      return -10;
    }

    copy(fReadAddress,fReadAddress+4,bank.fName);
    fReadAddress += 4;

    bank.fType = static_cast<uint32_t>(*(reinterpret_cast<uint16_t*>(fReadAddress)));
    fReadAddress += 2;

    bank.fSize = static_cast<uint32_t>(*(reinterpret_cast<uint16_t*>(fReadAddress)));
    fReadAddress += 2;

    if(bank.fSize > (maxBytes - nofHeaderBytes)) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bankbytes."<<Attribs::Reset<<endl;
      fReadAddress += maxBytes - nofHeaderBytes;
      return -1;
    }
  }

  if(fSettings->VerbosityLevel() > 2) {
    cout<<"will now try to read "<<bank.fSize<<" bytes into bank"<<endl;
  }

  try {
    //size is in bytes, bank data in uint32_t
    bank.fData.resize(bank.fSize/4);
  } catch(exception exc) {
    cerr<<Attribs::Bright<<Foreground::Red<<__PRETTY_FUNCTION__<<": failed to allocated memory"<<Attribs::Reset<<endl;
    exit(1);
  }

  if(BytesLeft() < bank.fSize) {
    //end of file
    return -10;
  }

  copy(reinterpret_cast<uint32_t*>(fReadAddress),reinterpret_cast<uint32_t*>(fReadAddress+bank.fSize),bank.fData.begin());
  fReadAddress += bank.fSize;

  if(bank.fSize%8 != 0) {
    bank.fExtraBytes.resize(8 - bank.fSize%8);

    if(bank.fExtraBytes.size() > (maxBytes - nofHeaderBytes - bank.fSize)) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read extra bank bytes"<<Attribs::Reset<<endl;
      return -1;
    }
  } else {
    bank.fExtraBytes.clear();
  }

  if(BytesLeft() < bank.fExtraBytes.size()) {
    //end of file
    return -10;
  }

  if(fSettings->VerbosityLevel() > 2) {
    cout<<"and copy "<<bank.fExtraBytes.size()<<" bytes to extra bytes of bank"<<endl;
  }

  copy(fReadAddress,fReadAddress+bank.fExtraBytes.size(),bank.fExtraBytes.begin());
  fReadAddress += bank.fExtraBytes.size();
    
  return nofHeaderBytes + bank.fSize + bank.fExtraBytes.size();
}

int MidasFileManager::SetRunStartTime(int& starttime) {
  MidasEvent event;

  if(!ReadHeader(event)) {
    event.EoF();
    return -3;
  }

  starttime = event.fTime;

  fReadAddress -= 24;

  return 0;
}

//---------------------------------------- Bank
void Bank::Print(bool hexFormat, bool printBankContents) {
  if(hexFormat) {
    cout<<hex<<"Bank Number: 0x"<<fNumber<<", Bankname: "<<fName[0]<<fName[1]<<fName[2]<<fName[3]<<", Type: 0x"<<fType<<", Banksize: 0x"<<fData.size()<<", Number of Extra Bytes: 0x"<<fExtraBytes.size()<<endl;
    
    if(printBankContents) {
      for(size_t i = 0; i < fData.size(); i++) {
	if(i%8 == 0 && i != 0) {
	  cout<<endl<<"0x";
	}
	cout<<fData[i]<<" ";
      }
      cout<<endl;
    }
    cout<<dec;
  } else {
    cout<<"Bank Number: "<<fNumber<<", Bankname: "<<fName[0]<<fName[1]<<fName[2]<<fName[3]<<", Type: "<<fType<<", Banksize: "<<fData.size()<<", Number of Extra Bytes: "<<fExtraBytes.size()<<dec<<endl;

    if(printBankContents) {
      for(size_t i = 0; i < fData.size(); i++) {
	if(i%8 == 0 && i != 0) {
	  cout<<endl;
	}
	cout<<fData[i]<<" ";
      }
      cout<<endl;
    }
  }
}

bool Bank::Get(uint16_t& value) {
  //peek at the value and then advance the read point
  Peek(value);
  ++fReadPoint;
  return true;
}

bool Bank::Get(uint32_t& value) {
  //peek at the value and then advance the read point
  Peek(value);
  fReadPoint += 2;
  return true;
}

bool Bank::Get(float& value) {
  //peek at the value and then advance the read point
  Peek(value);
  fReadPoint += 2;
  return true;
}

bool Bank::Peek(uint16_t& value) {
  //fReadPoint counts the 16bit values, but data contains 32bit values
  if(fReadPoint/2 >= fData.size()) {
    cerr<<__PRETTY_FUNCTION__<<": end of buffer reached in event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words"<<endl;
    Print(true, true);
    value = 0;
    return false;
  }
  if(fReadPoint%2 == 0) {
    value = fData[fReadPoint/2] >> 16;
  } else {
    value = fData[fReadPoint/2] & 0xffff;
  }
  return true;
}

bool Bank::Peek(uint32_t& value) {
  //fReadPoint counts the 16bit values, but data contains 32bit values
  if(fReadPoint/2 >= fData.size()) {
    cerr<<__PRETTY_FUNCTION__<<": end of buffer reached in event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words"<<endl;
    Print(true, true);
    value = 0;
    return false;
  }
  value = fData[fReadPoint/2];
  return true;
}

bool Bank::Peek(float& value) {
  assert(sizeof(float) == sizeof(uint32_t));
  //fReadPoint counts the 16bit values, but data contains 32bit values
  if(fReadPoint/2 >= fData.size()) {
    cerr<<__PRETTY_FUNCTION__<<": end of buffer reached in event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words"<<endl;
    Print(true, true);
    value = 0;
    return false;
  }
  memcpy(&value,&(fData[fReadPoint/2]),sizeof(float));
  return true;
}

//---------------------------------------- Event
void MidasEvent::Zero() {
  fBanks.clear();
  fType = 0;
  fMask = 0;
  fNumber = 0;
  fTime = 0;
  fNofBytes = 0;
  
  fTotalBankBytes = 0;
  fFlags = 0;
}

void MidasEvent::Print(bool hexFormat, bool printBanks, bool printBankContents) {
  if(hexFormat) {
    cout<<hex<<"Eventtype: 0x"<<fType<<", Eventmask: 0x"<<fMask<<", Eventnumber: 0x"<<fNumber<<", Eventtime: 0x"<<fTime<<", Number of Event Bytes: "<<dec<<fNofBytes<<endl
	<<hex<<"Total Bank Bytes: 0x"<<fTotalBankBytes<<", Flags: 0x"<<fFlags<<", Number of Banks: "<<dec<<fBanks.size()<<endl;
  }  else {
    cout<<"Eventtype: "<<fType<<", Eventmask: "<<fMask<<", Eventnumber: "<<fNumber<<", Eventtime: "<<fTime<<", Number of Event Bytes: "<<fNofBytes<<endl
	<<"Total Bank Bytes: "<<fTotalBankBytes<<", Flags: "<<fFlags<<", Number of Banks: "<<fBanks.size()<<endl;
  }
  
  if(printBanks) {
    for(size_t i = 0; i < fBanks.size(); ++i) {
      fBanks[i].Print(hex, printBankContents);
    }
  }
}
