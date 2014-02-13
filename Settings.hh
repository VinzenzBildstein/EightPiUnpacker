#ifndef __SETTINGS_HH
#define __SETTINGS_HH

#include <iostream>
#include <string>
#include <vector>

#define ULM_CYCLE        0x03ff     //Mask used to extract ULM cycle number from fera stream
#define ULM_BEAM_STATUS  0x0400   //Mask used to extract beam status. (0 = off, 1 = on)
#define ULM_TRIGGER_MASK 0xf800 //Mask used to extract the trigger mask

#define ULM_BEAM_STATUS_OFFSET  10 //Offset for ULM_BEAM_STATUS
#define ULM_TRIGGER_MASK_OFFSET 11 //Offset for ULM_TRIGGER_MASK

#define FME_ZERO  0x464d4530 //FME0 in hex.
#define FME_ONE   0x464d4531 //FME1
#define FME_TWO   0x464d4532 //FME2
#define FME_THREE 0x464d4533 //FME3

#define MCS_ZERO 0x4d435330 //MCS0 in hex
#define NOF_MCS_CHANNELS 32

#define VHNMASK   0x000f     //Mask to extract vsn numbers from the fera stream
#define VHTMASK   0x00f0     //Mask to extract feratype from the fera stream

#define VHAD114_ENERGY_MASK 0x3FFF

#define VHAD413_NUMBER_OF_DATA_WORDS_MASK 0x1800 //Mask for extracting the number of data words in a VHAD413
#define VHAD413_SUBADDRESS_MASK 0x6000 //Mask for extracting the subaddress from a VHAD413.
#define VHAD413_ENERGY_MASK 0x1FFF //Mask for extracting energy from a VHAD413.
#define VHAD413_DATA_WORDS_OFFSET 11
#define VHAD413_SUBADDRESS_OFFSET 13

#define TDC3377_IDENTIFIER    0x7c00	//Mask for the identity in the tdc word
#define TDC3377_TIME     0x00ff		//Mask for the time value in the tdc word

#define PLASTIC_CHANNELS     16         // Number of PLASTIC channels, if adcwords = 0 all fired
#define PLASTIC_ADC_WORDS     0x7800     // Mask to extract number of adc words PLASTIC          

#define PLASTIC_IDENTIFIER     0x7800     // Mask to extract the iden from PLASTIC qdc word       
#define PLASTIC_ENERGY     0x07ff     // Mask to extract the energy from PLASTIC qdc word     

#define PLASTIC_ADC_WORDS_OFFSET 11
#define PLASTIC_IDENTIFIER_OFFSET 11
#define GOODFIFO1 0xff06 //Good FIFO status word
#define GOODFIFO2 0xff16 //Good FIFO status word

#define FERAWORDS 0x1fff //Mask to extract number of fera words in the fera stream

#define VHAD1141  0x0040     // 114 ADC FERA xx40-xx4f (dets 1-15)                   
#define VHAD1142  0x0050     // 114 ADC FERA xx50-xx53 (dets 16-19)                  

#define VHAD114Si 0x0060	// 114 ADC FERA xx60-xx64 */

#define VHAD413   0x0000

#define VH3377    0x0010     // 3377 TDC FERA vsn=0 GEt, 1=PUt, 2=BGOt, 3=SCEt, 4=Dt, 5=Pt
#define VHFULM    0x0020     // 2366 ULM FERA vsn=0 8PI, 1=SCEPTAR, 2=DANTE, 3=PACES 
#define VH4300    0x0030     // SCEPTAR QDC FERA  xx30 = dets 0-15, xx31 = dets 16-19

#define BADFERA  0x0070     // Artificial "Bad FERA header" used to skip over bad data

#define INVALIDEVENTTYPE 0
#define FIFOEVENT 1
#define CAMACSCALEREVENT 2
#define SCALERSCALEREVENT 3
#define ISCALEREVENT 4
#define FRONTENDEVENT 8
#define EPICSEVENTTYPE 5
#define FILEEND 0x8001

using namespace std;

class Settings {
public:
  Settings(string, int);
  ~Settings(){};

  int VerbosityLevel() {
    return fVerbosityLevel;
  }

  string DetectorType(uint32_t);

  int NofGermaniumDetectors() {
    return fNofGermaniumDetectors;
  }
  int NofPlasticDetectors() {
    return fNofPlasticDetectors;
  }
  int NofSiliconDetectors() {
    return fNofSiliconDetectors;
  }
  int NofBaF2Detectors() {
    return fNofBaF2Detectors;
  }

  const char* TemperatureFile() {
    return fTemperatureFileName.c_str();
  }

private:
  int fVerbosityLevel;

  string fTemperatureFileName;

  int fNofGermaniumDetectors;
  int fNofPlasticDetectors;
  int fNofSiliconDetectors;
  int fNofBaF2Detectors;
};

#endif
