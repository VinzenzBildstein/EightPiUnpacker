#ifndef __CALIBRATION_HH
#define __CALIBRATION_HH

#include "TGraph.h"
#include "TH1I.h"

#include "Settings.hh"

class Calibration {
public:
  Calibration(Settings*);
  Calibration(){};
  ~Calibration(){};

  double operator()(double*, double*);
  TGraph Calibrate(const uint8_t&, const uint16_t&, TH1I*);

  void SetSettings(Settings* settings) {
    fSettings = settings;
  }

private:
  TGraph FindPeaks(const uint8_t&, const uint16_t&, TH1I*);

  Settings* fSettings;
};

#endif
