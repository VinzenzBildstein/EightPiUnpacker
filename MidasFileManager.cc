#include "MidasFileManager.hh"

#include <iomanip>
#include <fstream>

#include "TCollection.h"
#include "TXMLDocument.h"
#include "TXMLAttr.h"

#include "TextAttributes.hh"

void MidasFileHeader::ParseOdb() {
  fParser->SetValidate(false);
  fParser->ParseBuffer(reinterpret_cast<char*>(fInformation.data()),fInformation.size()*2);
  TXMLDocument* doc = fParser->GetXMLDocument();
  if(doc == nullptr) {
    std::cerr<<"Malformed ODB dump: cannot get XML document"<<std::endl;
    return;
  }

  fOdb = FindNode(doc->GetRootNode(),"odb");
  
  if(fOdb == nullptr) {
    std::cerr<<"Malformed ODB dump: cannot find <odb> tag"<<std::endl;
    return;
  }
}

void MidasFileHeader::PrintOdb(TXMLNode* startNode, size_t level) {
  if(startNode == nullptr) {
    startNode = fOdb;
  }
  for(TXMLNode* node = startNode; node != nullptr; node = node->GetNextNode()) {
    for(size_t i = 0; i < level; ++i) {
      std::cout<<"  ";
    }
    if(node->GetText() != nullptr) {
      for(size_t i = 0; i < level+1; ++i) {
	std::cout<<"  ";
      }
      std::cout<<"Text: "<<node->GetText()<<std::endl;
    }
    TIter next((TCollection*)(node->GetAttributes()));
    TXMLAttr* attribute;
    while((attribute = (TXMLAttr*) next()) != nullptr) {
      for(size_t i = 0; i < level+1; ++i) {
	std::cout<<"  ";
      }
      std::cout<<"Attribute: "<<attribute->GetName()<<" = "<<attribute->GetValue()<<std::endl;
    }
    if(node->HasChildren()) {
      PrintOdb(node->GetChildren(),level+1);
    }
  }
}

TXMLNode* MidasFileHeader::FindNode(TXMLNode* startNode, const char* name) {
  for(TXMLNode* node = startNode; node != nullptr; node = node->GetNextNode()) {
    if(strcmp(node->GetNodeName(),name) == 0) {
      return node;
    }
    if(node->HasChildren()) {
      TXMLNode* found = FindNode(node->GetChildren(),name);
      if(found != nullptr) {
	return found;
      }
    }
  }
  return nullptr;
}

const char* MidasFileHeader::GetAttribute(TXMLNode* node, const char* attributeName) {
  return GetAttribute(node, std::string(attributeName));
}

const char* MidasFileHeader::GetAttribute(TXMLNode* node, std::string attributeName) {
  TIter next((TCollection*)(node->GetAttributes()));
  TXMLAttr* attribute;
  while((attribute = (TXMLAttr*) next()) != nullptr) {
    if(attributeName.compare(attribute->GetName()) == 0) {
      return attribute->GetValue();
    }
  }
  return nullptr;
}

TXMLNode* MidasFileHeader::FindArrayPath(TXMLNode* node, std::string path, std::string type, int index) {
  if(fOdb == nullptr) {
    return nullptr;
  }

  if(node == nullptr) {
    node = fOdb->GetChildren();
  }

  std::transform(path.begin(), path.end(), path.begin(), tolower);
  std::transform(type.begin(), type.end(), type.begin(), tolower);  

  std::string element;

  TXMLNode* tmpNode = node;
  while(tmpNode != nullptr) {
    //strip leading slashes
    path.erase(0,path.find_first_not_of("/"));

    if(path.length() == 0) {
      node = tmpNode;
      break;
    }
   
    if(path.find('/') != std::string::npos) {
      element = path.substr(0,path.find('/')-1);
    } else {
      element = path;
    }

    for(; tmpNode != nullptr; tmpNode = tmpNode->GetNextNode()) {
      std::string nodeName = tmpNode->GetNodeName();
      std::string nameValue = GetAttribute(tmpNode,"name");
      
      bool isDir = nodeName.compare("dir") == 0;
      bool isKey = nodeName.compare("key") == 0;
      bool isKeyArray = nodeName.compare("keyarray") == 0;
      
      if(!isKey && !isDir && !isKeyArray) {
	continue;
      }
      
      std::transform(nameValue.begin(), nameValue.end(), nameValue.begin(), tolower);
      if(element.compare(nameValue) == 0) {
	if(isDir) {
	  // found the right subdirectory, descend into it
	  tmpNode = tmpNode->GetChildren();
	  break;
	} else if(isKey || isKeyArray) {
	  node = tmpNode;
	  tmpNode = nullptr;
	  break;
	}
      }
    }
  }

  if(node == nullptr) {
    return nullptr;
  }

  std::string nodeName = node->GetNodeName();
  std::string numberValues = GetAttribute(node,"num_values");

  std::string typeValue = GetAttribute(node,"type");
  std::transform(typeValue.begin(), typeValue.end(), typeValue.begin(), tolower);

  if(typeValue == nullptr || typeValue.compare(type) != 0) {
    std::cerr<<__PRETTY_FUNCTION__<<": Type mismatch: we expected '"<<type<<"', but got '"<<typeValue<<"'"<<std::endl;
    return nullptr;
  }

  if(numberValues != nullptr && nodeName.compare("keyarray") == 0) {
    if(index != 0) {
      std::cerr<<__PRETTY_FUNCTION__<<": Attempt to access array element "<<index<<", but this is not an array"<<std::endl;
      return nullptr;
    }

    return node;
  }

  int maxIndex;
  std::stringstream stream(numberValues);
  stream>>maxIndex;

  if(index < 0 || index >= maxIndex) {
    std::cerr<<__PRETTY_FUNCTION__<<": Attempt to access array element "<<index<<", but size of array  is "<<maxIndex<<std::endl;
    return nullptr;
  }

  TXMLNode* child = node->GetChildren();

  for(int i = 0; child != nullptr; ) {
    std::string name = child->GetNodeName();
    std::string text = child->GetText();

    if(name.compare("value") == 0) {
      if(i == index) {
	return child;
	++i;
      }
      
      child = child->GetNextNode();
    }
  }

  return node;
}

MidasFileManager::~MidasFileManager() {
  if(fFile.is_open()) {
    fFile.close();
  }
}

bool MidasFileManager::Open(std::string fileName) {
  fFileName = fileName;
  try {
    fFile.open(fFileName);
    fStartAddress = fFile.data();
    fReadAddress = fStartAddress;
    fSize = fFile.size();
  } catch(std::exception& exc) {
    //file_mapping::remove(fileName.c_str());
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Unhandled exception "<<exc.what()<<Attribs::Reset<<std::endl;
    return false;
  }

  if(fSettings->VerbosityLevel() > 0) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Input file size is "<<fSize<<" bytes."<<Attribs::Reset<<std::endl;
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
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left to read the header: "<<BytesLeft()<<" < 16"<<Attribs::Reset<<std::endl;
    exit(-1);
  }

  //get the file header and advance the read address
  const uint32_t* fileHeaderWord = reinterpret_cast<const uint32_t*>(fReadAddress);
  if((fileHeaderWord[0] & 0xffff) != 0x8000) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"ERROR! Bad Midas start number (should be 0x8000): 0x"<<std::hex<<fileHeaderWord[0]<<std::dec<<" = "<<fileHeaderWord[0]<<std::endl
	<<"Read address  = 0x"<<std::hex<<fReadAddress<<", start address 0x"<<fStartAddress<<std::dec<<Attribs::Reset<<std::endl;
    exit(-1);
  }
  fReadAddress += 16;//4 32bit words => 16 bytes

  fileHeader.RunNumber(fileHeaderWord[1]);
  fileHeader.StartTime(fileHeaderWord[2]);
  //header information length is in bytes, but header information is stored in 16bit words => divide length by two
  fileHeader.InformationLength(fileHeaderWord[3]/2);

  //check that at least fileHeaderWord[3] bytes are left
  if(BytesLeft() < fileHeaderWord[3]) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left to read the header information: "<<BytesLeft()<<" < "<<fileHeaderWord[3]<<Attribs::Reset<<std::endl;
    exit(-1);
  }

  //read the header information and advance the read address
  copy(reinterpret_cast<const uint16_t*>(fReadAddress),reinterpret_cast<const uint16_t*>(fReadAddress+fileHeaderWord[3]),fileHeader.Information().begin());
  fReadAddress += fileHeaderWord[3];

  //parse the odb
  fileHeader.ParseOdb();
  
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
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Error reading event header (24 bytes), only "<<BytesLeft()<<" bytes left!"<<std::endl
	     <<"Assuming end of file!"<<Attribs::Reset<<std::endl;
    event.EoF();
    fStatus = kEoF;
    return false;
  } else if(fSettings->VerbosityLevel() > 0) {
    std::cout<<"reading header (24 bytes), got "<<BytesLeft()<<" bytes left (size = "<<fSize<<", position = "<<Position()<<")"<<std::endl;
  }

  //read the header information
  event.fType = *(reinterpret_cast<const uint16_t*>(fReadAddress));
  fReadAddress += 2;

  event.fMask = *(reinterpret_cast<const uint16_t*>(fReadAddress));
  fReadAddress += 2;

  event.fNumber = *(reinterpret_cast<const uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fTime = *(reinterpret_cast<const uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fNofBytes = *(reinterpret_cast<const uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fTotalBankBytes = *(reinterpret_cast<const uint32_t*>(fReadAddress));
  fReadAddress += 4;

  event.fFlags = *(reinterpret_cast<const uint32_t*>(fReadAddress));
  fReadAddress += 4;

  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<std::endl
	     <<std::hex<<"Got event header with type 0x"<<event.fType<<", mask 0x"<<event.fMask<<", time 0x"<<event.fTime<<std::dec<<", "<<event.fNofBytes<<" bytes, "<<event.fTotalBankBytes<<" total bytes, and flags 0x"<<std::hex<<event.fFlags<<std::dec<<std::endl;
  }

  return true;
}

bool MidasFileManager::Read(MidasEvent& event) {
  size_t nofBankBytesRead = 0; //Does not include the event header

  int bankBytesRead = 0;

  if(!ReadHeader(event)) {
    return false;
  }

  if(event.IsEoF()) {
    event.EoF();
    return false;
  }

  //Don't include header size in numberofeventbytesinput.

  //Double check size of event.
  if((event.fTotalBankBytes + 8) != event.fNofBytes) {
    //error.
    std::cerr<<Attribs::Bright<<Foreground::Red<<"The number of event bytes and total bank bytes do not agree in event "<<event.fNumber<<std::endl
	     <<"There are "<<event.fTotalBankBytes<<" total bank bytes, and "<<event.fNofBytes<<" event bytes."<<std::endl
	     <<"Looking for next good event."<<Attribs::Reset<<std::endl;

    //Need to go looking for the next good event ...
    while((event.fTotalBankBytes + 8) != event.fNofBytes) {
      //Return all but 4 of the 24 event header bytes.
      fReadAddress -= 20;

      if(!ReadHeader(event)) {
	std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find good event."<<Attribs::Reset<<std::endl;
	return false;
      }
    }

    std::cerr<<Attribs::Bright<<Foreground::Green<<"Recovered - found next good event."<<Attribs::Reset<<std::endl;
  }

  if((event.fFlags != 0x11) && (event.fFlags != 0x1)) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Bad flags of 0x"<<std::hex<<event.fFlags<<std::dec<<" in event "<<event.fNumber<<Attribs::Reset<<std::endl;
  }

  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<"Starting on event "<<event.fNumber<<" with "<<event.fNofBytes<<" bytes and "<<event.fTotalBankBytes<<" bank bytes. Flags are 0x"<<std::hex<<event.fFlags<<std::dec<<std::endl;
  }

  //Fill the banks.
  while(nofBankBytesRead < event.fTotalBankBytes) {
    try {
      event.fBanks.push_back(Bank(event.fBanks.size()));
    } catch(std::exception exc) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to allocate memory for bank #"<<event.fBanks.size()<<" in event "<<event.fNumber<<": "<<exc.what()<<Attribs::Reset<<std::endl;
      exit(1);
    }

    bankBytesRead = Read(event.fBanks.back(), event.fTotalBankBytes - nofBankBytesRead, event.fFlags); //readBank() returns the number of bytes read.

    nofBankBytesRead += bankBytesRead;

    if(bankBytesRead < 1) {
      if(bankBytesRead == -10) {
	//Unexpected end of file - likely hardware issue.
	std::cerr<<Attribs::Bright<<Foreground::Red<<__PRETTY_FUNCTION__<<": unexpected end of file"<<Attribs::Reset<<std::endl;
	event.EoF();
	return false;
      }
      //We're likely in an infinite loop.
      //$$$$$Error spectrum.
      //Do something about it!
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Error, no bytes read in event "<<event.fNumber<<" for bank "<<event.fBanks.size()<<Attribs::Reset<<std::endl;
      event.fBanks.pop_back(); //Don't process the rest of the banks.
      return false;
    }

    event.fBanks.back().fEventNumber = event.fNumber;
  }

  if(nofBankBytesRead != event.fTotalBankBytes) {
    //Trouble, too many bytes inputted.
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Number of bytes in event "<<event.fNumber<<" does not agree with number of bytes in banks."<<Attribs::Reset<<std::endl;
    return false;
  }

  return true;
}

int MidasFileManager::Read(Bank& bank, unsigned int maxBytes, unsigned int flags) {
  //$$$$ Could add a check for byte flipping - maybe in readEvent
  unsigned int nofHeaderBytes = 0;

  bank.fReadPoint = 0; //Start reading the bank at the first byte.

  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<"starting to read "<<maxBytes<<" bytes from bank with flags "<<flags<<" (BANK32 = "<<BANK32<<")"<<std::endl;
  }

  if(flags & BANK32) {
    nofHeaderBytes = 12;

    if(maxBytes < nofHeaderBytes) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bank header."<<Attribs::Reset<<std::endl;
      return -1;
    }
    if(BytesLeft() < nofHeaderBytes) {
      //end of file
      return -10;
    }

    std::copy(fReadAddress,fReadAddress+4,bank.fName);
    fReadAddress += 4;

    bank.fType = *(reinterpret_cast<const uint32_t*>(fReadAddress));
    fReadAddress += 4;

    bank.fSize = *(reinterpret_cast<const uint32_t*>(fReadAddress));
    fReadAddress += 4;

    if(bank.fSize > (maxBytes - nofHeaderBytes)) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bankbytes."<<Attribs::Reset<<std::endl;
      fReadAddress += maxBytes - nofHeaderBytes;
      return -1;
    }
  } else {
    nofHeaderBytes = 8;

    if(maxBytes < nofHeaderBytes) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bank header."<<Attribs::Reset<<std::endl;
      return -1;
    }
    if(BytesLeft() < nofHeaderBytes) {
      //end of file
      return -10;
    }

    std::copy(fReadAddress,fReadAddress+4,bank.fName);
    fReadAddress += 4;

    bank.fType = static_cast<uint32_t>(*(reinterpret_cast<const uint16_t*>(fReadAddress)));
    fReadAddress += 2;

    bank.fSize = static_cast<uint32_t>(*(reinterpret_cast<const uint16_t*>(fReadAddress)));
    fReadAddress += 2;

    if(bank.fSize > (maxBytes - nofHeaderBytes)) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read bankbytes."<<Attribs::Reset<<std::endl;
      fReadAddress += maxBytes - nofHeaderBytes;
      return -1;
    }
  }

  if(fSettings->VerbosityLevel() > 2) {
    std::cout<<"will now try to read "<<bank.fSize<<" bytes into bank"<<std::endl;
  }

  try {
    //size is in bytes, bank data in uint32_t
    bank.fData.resize(bank.fSize/4);
  } catch(std::exception exc) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<__PRETTY_FUNCTION__<<": failed to allocated memory"<<Attribs::Reset<<std::endl;
    exit(1);
  }

  if(BytesLeft() < bank.fSize) {
    //end of file
    return -10;
  }

  std::copy(reinterpret_cast<const uint32_t*>(fReadAddress),reinterpret_cast<const uint32_t*>(fReadAddress+bank.fSize),bank.fData.begin());
  fReadAddress += bank.fSize;

  if(bank.fSize%8 != 0) {
    bank.fExtraBytes.resize(8 - bank.fSize%8);

    if(bank.fExtraBytes.size() > (maxBytes - nofHeaderBytes - bank.fSize)) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Not enough bytes left in event to read extra bank bytes"<<Attribs::Reset<<std::endl;
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
    std::cout<<"and copy "<<bank.fExtraBytes.size()<<" bytes to extra bytes of bank"<<std::endl;
  }

  std::copy(fReadAddress,fReadAddress+bank.fExtraBytes.size(),bank.fExtraBytes.begin());
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
    std::cout<<std::hex<<"Bank Number: 0x"<<fNumber<<", Bankname: "<<fName[0]<<fName[1]<<fName[2]<<fName[3]<<", Type: 0x"<<fType<<", Banksize: 0x"<<fData.size()<<", Number of Extra Bytes: 0x"<<fExtraBytes.size();
    
    if(printBankContents) {
      for(size_t i = 0; i < fData.size(); i++) {
	if(i%8 == 0) {
	  std::cout<<std::endl<<"0x";
	}
	std::cout<<std::setw(8)<<std::setfill('0')<<fData[i]<<" ";
      }
    }
    std::cout<<std::dec<<std::setfill(' ')<<std::endl;
  } else {
    std::cout<<"Bank Number: "<<fNumber<<", Bankname: "<<fName[0]<<fName[1]<<fName[2]<<fName[3]<<", Type: "<<fType<<", Banksize: "<<fData.size()<<", Number of Extra Bytes: "<<fExtraBytes.size()<<std::dec<<std::endl;

    if(printBankContents) {
      for(size_t i = 0; i < fData.size(); i++) {
	if(i%8 == 0 && i != 0) {
	  std::cout<<std::endl;
	}
	std::cout<<fData[i]<<" ";
      }
      std::cout<<std::endl;
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
  //std::cout<<"Peeking 16bits at "<<fReadPoint<<std::endl;
  //fReadPoint counts the 16bit values, but data contains 32bit values
  if(fReadPoint/2 >= fData.size()) {
    //std::cerr<<__PRETTY_FUNCTION__<<": end of buffer reached in event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words"<<std::endl;
    //Print(true, true);
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
  //std::cout<<"Peeking 32bits at "<<fReadPoint<<std::endl;
  //fReadPoint counts the 16bit values, but data contains 32bit values
  if((fReadPoint+1)/2 >= fData.size()) {
    //std::cerr<<__PRETTY_FUNCTION__<<": end of buffer reached in event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words"<<std::endl;
    //Print(true, true);
    value = 0;
    return false;
  }
  if(fReadPoint%2 != 0) {
    //std::cerr<<__PRETTY_FUNCTION__<<": trying to read 32bits after an odd number of 16bit words have been read! Event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words, fData[fReadPoint/2] 0x"<<std::hex<<fData[fReadPoint/2]<<", 0x"<<fData[(fReadPoint+1)/2];
    //get the low word of fReadPoint/2 and the high word of (fReadPoint+1)/2
    value = ((fData[fReadPoint/2] & 0xffff) << 16) | ((fData[(fReadPoint+1)/2] & 0xffff0000) >> 16);
    //std::cerr<<", value 0x"<<value<<std::dec<<std::endl;
  } else {
    value = fData[fReadPoint/2];
  }

  return true;
}

bool Bank::Peek(float& value) {
  assert(sizeof(float) == sizeof(uint32_t));
  //fReadPoint counts the 16bit values, but data contains 32bit values
  if(fReadPoint/2 >= fData.size()) {
    //std::cerr<<__PRETTY_FUNCTION__<<": end of buffer reached in event "<<fEventNumber<<", bank "<<fNumber<<", location "<<fReadPoint<<", size "<<fData.size()<<"+1 16bit words"<<std::endl;
    //Print(true, true);
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
    std::cout<<std::hex<<"Eventtype: 0x"<<fType<<", Eventmask: 0x"<<fMask<<", Eventnumber: 0x"<<fNumber<<", Eventtime: 0x"<<fTime<<", Number of Event Bytes: "<<std::dec<<fNofBytes<<std::endl
	<<std::hex<<"Total Bank Bytes: 0x"<<fTotalBankBytes<<", Flags: 0x"<<fFlags<<", Number of Banks: "<<std::dec<<fBanks.size()<<std::endl;
  }  else {
    std::cout<<"Eventtype: "<<fType<<", Eventmask: "<<fMask<<", Eventnumber: "<<fNumber<<", Eventtime: "<<fTime<<", Number of Event Bytes: "<<fNofBytes<<std::endl
	<<"Total Bank Bytes: "<<fTotalBankBytes<<", Flags: "<<fFlags<<", Number of Banks: "<<fBanks.size()<<std::endl;
  }
  
  if(printBanks) {
    for(auto& bank : fBanks) {
      bank.Print(hexFormat, printBankContents);
    }
  }
}
