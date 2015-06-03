#include "Calibration.hh"

#include "TList.h"
#include "TF1.h"
#include "TSpectrum.h"
#include "TSpectrumFit.h"

#include "TextAttributes.hh"

//numeric_limits<uint16_t>::min(),numeric_limits<uint16_t>::max()

Calibration::Calibration(Settings* settings)
  : fSettings(settings) {
  std::cout<<"got settings sigma "<<fSettings->Sigma()<<std::endl;
}

double Calibration::operator()(double* x, double* p) {
  return p[0]*(x[0]-p[1]);
}

TGraph Calibration::Calibrate(const uint8_t& detectorType, const uint16_t& detectorNumber, TH1I* histogram) {
  int nofBins = histogram->GetNbinsX();
  double minX = histogram->GetBinLowEdge(1);
  double maxX = histogram->GetBinLowEdge(nofBins+1);

  TF1* calibration = histogram->GetFunction("Calibration");
  if(calibration == nullptr) {
    //if this is the first time calibrating, we need to create a new calibration function
    //and try to find the peaks based on the settings provided
    calibration = new TF1("Calibration",this,minX,maxX,2);
    TGraph peaks = FindPeaks(detectorType, detectorNumber, histogram);
    if(peaks.GetN() < 2) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Found only "<<peaks.GetN()<<" peaks in histogram '"<<histogram->GetName()<<"'"<<Attribs::Reset<<std::endl;
      return TGraph();
    }
    //now we can fit the peaks with the new calibration function
    calibration->SetParNames("gain","offset");
    calibration->SetParameters(1.,1.);
    peaks.Fit(calibration);
    histogram->GetListOfFunctions()->Add(calibration);
    return peaks;
  }
  //we calibrated before, so we should only have a small shift in the calibration

  return TGraph();  
}

TGraph Calibration::FindPeaks(const uint8_t& detectorType, const uint16_t& detectorNumber, TH1I* histogram) {
  int nofBins = histogram->GetNbinsX();
  double minX = histogram->GetBinLowEdge(1);
  double maxX = histogram->GetBinLowEdge(nofBins+1);

  //get the data from the histogram, since we need these to be doubles, we can't use GetArray()
  float* data = new float[nofBins];
  for(int i = 0; i < nofBins; ++i) {
    data[i] = histogram->GetBinContent(i+1);
  }
  float* deconv = new float[nofBins];

  //find the peaks
  TSpectrum spectrum;
  if(fSettings->VerbosityLevel() > 3) {
    std::cout<<"Starting SearchHighRes(data, deconv, "<<nofBins<<", "<<fSettings->Sigma()<<", "<<fSettings->PeakThreshold()<<", false, "<<fSettings->NofDeconvIterations()<<",false,0)"<<std::endl;
  }
  Int_t nofPeaks = spectrum.SearchHighRes(data,deconv,nofBins,fSettings->Sigma(),fSettings->PeakThreshold(),false,fSettings->NofDeconvIterations(),false,0);
  if(nofPeaks <= 0) {
    std::cout<<Attribs::Bright<<Foreground::Red<<"SearchHighRes(data, deconv, "<<nofBins<<", "<<fSettings->Sigma()<<", "<<fSettings->PeakThreshold()<<", false, "<<fSettings->NofDeconvIterations()<<",false,0) failed!"<<Attribs::Reset<<std::endl;
    return TGraph();
  }
  float* posX = spectrum.GetPositionX();
  float* posY = new float[nofPeaks];
  Bool_t* fixPosition = new Bool_t[nofPeaks];
  Bool_t* fixAmplitude = new Bool_t[nofPeaks];
  for(int i = 0; i < nofPeaks; ++i) {
    posY[i] = histogram->GetBinContent(1+Int_t(posX[i]+0.5));
    fixPosition[i] = true;
    fixAmplitude[i] = true;
  }

  //fit the found peaks in the spectrum
  TSpectrumFit spectrumFit(nofPeaks);
  //min. x, max. x, # iterations, convergence coeff., statistics type, convergence algorithm optim., powers, oder of Taylor expans.
  spectrumFit.SetFitParameters(minX, maxX, fSettings->NofFitIterations(), fSettings->FitConvergenceCoeff(), spectrumFit.kFitOptimChiFuncValues, spectrumFit.kFitAlphaHalving, spectrumFit.kFitPower2, spectrumFit.kFitTaylorOrderFirst);  
  //sigma, fix sigma?, init. pos., fix position (array)?, init. ampl., fix ampl. (array)?
  spectrumFit.SetPeakParameters(fSettings->Sigma(), false, posX, fixPosition, posY, fixAmplitude);  
  spectrumFit.FitAwmi(data);
  Double_t* fitPos = spectrumFit.GetPositions();
  Double_t* fitAmp = spectrumFit.GetAmplitudes();
  
  //we've now got amplitudes and positions of the peaks in the spectrum -> find out which position belongs to which energy
  std::vector<std::vector<int> > peakPosList(fSettings->NofPeaks(detectorType, detectorNumber));
  for(int i = 0; i < nofPeaks; ++i) {
    //InRoughWindow returns -1 if not in any window. In that case we ignore this peak, otherwise add it to the list of peaks
    int tmp = fSettings->InRoughWindow(detectorType, detectorNumber, posX[i]);
    if(tmp >= 0 && tmp < (int)(peakPosList.size())) {
      peakPosList[tmp].push_back(i);
    }
  }
  //loop over list of peaks, check that we got at least one, and if more than one, select the highest one
  std::vector<int> peakPos(peakPosList.size(),-1);
  for(size_t i = 0; i < peakPosList.size(); ++i) {
    if(peakPosList[i].size() == 0) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find peak for window "<<i<<": "<<std::flush<<fSettings->PrintWindow(detectorType, detectorNumber, i)<<Attribs::Reset<<std::endl;
      //despite this error, we'll try to continue on with one less calibration point (actually the point is zero and we need to skip if when filling the graph)
    } else {
      peakPos[i] = peakPosList[i][0];
      //more than one peak => loop over them and find the biggest one
      for(size_t j = 1; j < peakPosList[i].size(); ++j) {
	if(fitAmp[peakPosList[i][j]] > fitAmp[peakPos[i]]) {
	  peakPos[i] = peakPosList[i][j];
	}
      }
    }
  }
  TGraph peaks(peakPos.size());
  for(size_t i = 0; i < peakPos.size();) {
    if(peakPos[i] >= 0) {
      peaks.SetPoint(i,fitPos[peakPos[i]],fSettings->Energy(detectorType, detectorNumber, i));
      ++i;
    } else {
      peaks.RemovePoint(i);
    }
  }

  return peaks;
}
