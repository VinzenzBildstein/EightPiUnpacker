#ifndef __FILEMANAGER_H
#define __FILEMANAGER_H
#include <iostream>
#include <vector>
#include <string>

#include <boost/iostreams/device/mapped_file.hpp>

#include "Settings.hh"

#define BANK32 0x10
#define END_OF_FILE 0x8001

class MidasEvent;
class Bank;

class MidasFileHeader {
 public:
  MidasFileHeader(){};
  ~MidasFileHeader(){};

  void RunNumber(uint32_t number) {
    fRunNumber = number;
  }
  void StartTime(uint32_t time) {
    fStartTime = time;
  }
  void InformationLength(uint32_t length) {
    fInformation.resize(length);
  }

  int RunNumber() {
    return fRunNumber;
  }
  int StartTime() {
    return fStartTime;
  }
  size_t InformationLength() {
    return fInformation.size();
  }
  std::vector<unsigned short int>& Information() {
    return fInformation;
  }

 private:
  uint32_t fRunNumber;
  uint32_t fStartTime;
  std::vector<uint16_t> fInformation;
};

class MidasFileManager {
 public:
  enum EFileStatus {
    kOkay,
    kEoF
  };

  MidasFileManager() { 
    fStatus = EFileStatus::kOkay; 
  };
  MidasFileManager(std::string fileName, Settings* settings) {
    fStatus = EFileStatus::kOkay; 
    fSettings = settings;
    if(!Open(fileName)) {
      throw;
    }
    if(fSettings->VerbosityLevel() > 1) {
      std::cout<<"Done with creator of MidasFileManager"<<std::endl;
    }
  }
  ~MidasFileManager();

  EFileStatus Status() {
    return fStatus;
  }

  bool Open(std::string);
  MidasFileHeader ReadHeader();
  bool Read(MidasEvent&);

  size_t Position() {
    if(fReadAddress <= fStartAddress) {
      return 0;
    }
    //addresses are in 16bit words, but we want the position in bytes
    return 2*(fReadAddress-fStartAddress);
  }
  size_t Size() {
    return fSize;
  }
  void Close() {
    if(fFile.is_open()) {
      fFile.close();
    }
  }

private:
  int Read(Bank&,unsigned int, unsigned int);

  int SetRunStartTime(int&);  

  size_t BytesLeft() {
    if(fSize <= Position()) {
      return 0;
    }
    return fSize - Position();
  }
  bool ReadHeader(MidasEvent&);

 private:
  Settings* fSettings;
  boost::iostreams::mapped_file_source fFile;
  EFileStatus fStatus;
  std::string fFileName;
  size_t fSize;
  const char* fStartAddress;
  const char* fReadAddress;
};

class Bank {
public:
  friend class MidasFileManager;

  Bank() {};
  Bank(size_t number) {
    fNumber = number;
  }
  ~Bank(){};

  void Print(bool, bool);
  bool GotData() {
    //read point is in 16bit words, while data holds 32bit words
    return fReadPoint/2 < fData.size();
  }
  bool GotBytes(size_t bytes) {
    //read point is in 16bit words, while data holds 32bit words
    //dividing in this way ensures we account odd number of bytes and read points correctly
    return (fReadPoint+bytes/2)/2 < fData.size();
  }

  //set
  void EventNumber(uint32_t eventNumber) {
    fEventNumber = eventNumber;
  }
  void SetReadPoint(size_t readPoint) {
    fReadPoint = readPoint;
  }
  void ChangeReadPoint(int change) {
    if(((int) fReadPoint) > change) {
      fReadPoint += change;
    } else {
      throw change;
    }
  }
  //access
  char* Name() {
    return fName;
  }
  uint32_t IntName() {
    uint32_t result = ((uint32_t)fName[0])<<24 | ((uint32_t)fName[1])<<16 | ((uint32_t)fName[2])<<8 | ((uint32_t)fName[3]);
    return result;
  }
  uint32_t Type() {
    return fType;
  }
  //return size in bytes
  uint32_t Size() {
    return fSize;
  }
  std::vector<uint32_t> Data() {
    return fData;
  }
  size_t NofExtraBankBytes() {
    return fExtraBytes.size();
  }
  std::vector<uint8_t> ExtraBytes() {
    return fExtraBytes;
  }
  //return readpoint in bytes (readpoint itself is in 16bit words)
  size_t ReadPoint() {
    return 2*fReadPoint;
  }
  size_t Number() {
    return fNumber;
  }
  uint32_t EventNumber() {
    return fEventNumber;
  }
  
  bool IsBank(const char* name) {
    if(strncmp(this->fName,name,4) == 0) {
      return true;
    }
    return false;
  }

  bool Get(uint16_t&);
  bool Get(uint32_t&);
  bool Get(float&);

  bool Peek(uint16_t&);
  bool Peek(uint32_t&);
  bool Peek(float&);

private:
  char fName[4];
  uint32_t fType;
  uint32_t fSize;
  std::vector<uint32_t> fData;
  
  std::vector<uint8_t> fExtraBytes;
  
  size_t fNumber;
  uint32_t fEventNumber;
  
  size_t fReadPoint;
};

class MidasEvent {
public:
  friend class MidasFileManager;

  MidasEvent() {
    Zero();
  };
  ~MidasEvent(){};

  void Zero();
  void Print(bool, bool, bool);

  void EoF() {
    fType = END_OF_FILE;
    fNofBytes = 0;
    fTotalBankBytes = 0;
    fBanks.clear();
  }

  bool IsEoF() {
    return (fType == END_OF_FILE);
  }

  //assignment functions
  void Type(uint16_t type) {
    fType = type;
  }
  void Mask(uint16_t mask) {
    fMask = mask;
  }
  void Number(uint32_t number) {
    fNumber = number;
  }
  void Time(uint32_t time) {
    fTime = time;
  }
  void NofBytes(uint32_t nofBytes) {
    fNofBytes = nofBytes;
  }
  void TotalBankBytes(uint32_t totalBankBytes) {
    fTotalBankBytes = totalBankBytes;
  }
  void Flags(uint32_t flags) {
    fFlags = flags;
  }

  //access functions
  uint16_t Type() {
    return fType;
  }
  uint16_t Mask() {
    return fMask;
  }
  uint32_t Number() {
    return fNumber;
  }
  uint32_t Time() {
    return fTime;
  }
  uint32_t NofBytes() {
    return fNofBytes;
  }
  uint32_t TotalBankBytes() {
    return fTotalBankBytes;
  }
  uint32_t Flags() {
    return fFlags;
  }
  const std::vector<Bank>& Banks() {
    return fBanks;
  }

private:
  uint16_t fType;
  uint16_t fMask;
  uint32_t fNumber;
  uint32_t fTime;
  uint32_t fNofBytes;
  
  uint32_t fTotalBankBytes;
  uint32_t fFlags;

  std::vector<Bank> fBanks;
};

#endif
