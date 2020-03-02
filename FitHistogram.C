double FitFunction(double* x, double* par) {
  //0: nof peaks
  //1-3: background par a0-2
  //4: sigma, 5: t, 6: s, 7: b
  //8+2i: pos, 9+2i: amp, 
  double result = par[1]+par[2]*x[0]+par[3]*x[0]*x[0];
  for(int i = 0; i < par[0]; ++i) {
    result += par[9+2*i]*(TMath::Exp(-TMath::Power(x[0]-par[8+2*i],2)/(2.*TMath::Power(par[4],2)))
			  +par[5]/2.*TMath::Exp((x[0]-par[8+2*i])/(par[4]*par[7]))*TMath::Erfc((x[0]-par[8+2*i])/par[4]+0.5/par[7])
			  +par[6]/2.*TMath::Exp((x[0]-par[8+2*i])/par[4]));
  }
  return result;
}

void FitHistogram(TH1F* hist, Double_t& sigma = 2.) {
  Int_t nbins = hist->GetNbinsX();
  Double_t minX = hist->GetBinLowEdge(1);
  Double_t maxX = hist->GetBinLowEdge(nbins+1);

  float* source = new float[nbins];
  float* dest = new float[nbins];  

  for(Int_t i = 0; i < nbins; ++i) {
    source[i] = hist->GetBinContent(i + 1);
  }

  TCanvas* Fit1 = gROOT->GetListOfCanvases()->FindObject("Fit1");
  if(!Fit1) {
    Fit1 = new TCanvas("Fit1","Fit1",10,10,1000,700);
  }
  
  Fit1->cd(0);
  Fit1->Divide(1,2);
  Fit1->cd(1);

  hist->Draw();

  TSpectrum spectrum;

  //searching for candidate peaks positions
  //sigma = 2, threshold = 0.1, #iterations = 10000
  //float*, float*, Int_t, float, Double_t, bool, Int_t, bool, Int_t
  Int_t nfound = spectrum.SearchHighRes(source, dest, nbins, sigma, 0.1, kFALSE, 10000, kFALSE, 0);

  Bool_t* FixPos = new Bool_t[nfound];
  Bool_t* FixAmp = new Bool_t[nfound];

  for(i = 0; i< nfound ; i++){
    FixPos[i] = kFALSE;
    FixAmp[i] = kFALSE;   
  }

  //filling in the initial estimates of the input parameters
  Float_t* PosX = new Float_t[nfound];        
  Float_t* PosY = new Float_t[nfound];

  PosX = spectrum.GetPositionX();

  for (i = 0; i < nfound; i++) {
    PosY[i] = hist->GetBinContent(1+Int_t(PosX[i]+0.5));
  }  

  TSpectrumFit spectrumFit(nfound);

  spectrumFit.SetFitParameters(minX, maxX-1, 1000, 0.1, spectrumFit.kFitOptimChiFuncValues, spectrumFit.kFitAlphaOptimal, spectrumFit.kFitPower4, spectrumFit.kFitTaylorOrderFirst);

  spectrumFit.SetPeakParameters(sigma, kFALSE, PosX, FixPos, PosY, FixAmp);  

  //Double_t tInit, Bool_t fixT, Double_t bInit, Bool_t fixB, Double_t sInit, Bool_t fixS
  spectrumFit.SetTailParameters(0.1, false, 0.1, false, 1., false); 

  spectrumFit.FitAwmi(source);

  Double_t* CalcPositions  = spectrumFit.GetPositions();
  Double_t* CalcAmplitudes = spectrumFit.GetAmplitudes();  

  //TH1F* fit = gROOT->GetListOfCanvases()->FindObject("Fit1");
  //if(!fit) {
  TH1F* fit = new TH1F("fit","fit",nbins,minX,maxX);
  //}

  for(i = 0; i < nbins; i++) {
    fit->SetBinContent(i + 1, source[i]);
  }
  fit->SetLineColor(kRed);  
  fit->Draw("same"); 

  Fit1->cd(2);
  TH1F* res = hist->Clone("res");
  res->SetTitle("residual");
  res->Add(fit,-1.);
  res->SetLineColor(kBlue);
  res->Draw(""); 

  for(i = 0; i < nfound; i++) {
    PosX[i] = fit->GetBinCenter(1+Int_t(CalcPositions[i]+0.5));
    PosY[i] = fit->GetBinContent(1+Int_t(CalcPositions[i]+0.5));
  }

  TPolyMarker* pm = (TPolyMarker*)hist->GetListOfFunctions()->FindObject("TPolyMarker");
  if(pm) {
    hist->GetListOfFunctions()->Remove(pm);
    delete pm;
  }
  pm = new TPolyMarker(nfound, PosX, PosY);
  hist->GetListOfFunctions()->Add(pm);
  pm->SetMarkerStyle(23);
  pm->SetMarkerColor(kRed);
  pm->SetMarkerSize(1);  


  Double_t sigma, sigmaErr, t, tErr, b, bErr, s, sErr, a0, a0Err, a1, a1Err, a2, a2Err;
  spectrumFit.GetSigma(sigma, sigmaErr);
  spectrumFit.GetTailParameters(t, tErr, b, bErr, s, sErr);
  spectrumFit.GetBackgroundParameters(a0, a0Err, a1, a1Err, a2, a2Err);
  cout<<"sigma = "<<sigma<<" +- "<<sigmaErr<<endl
      <<"t = "<<t<<" +- "<<tErr<<endl
      <<"b = "<<b<<" +- "<<bErr<<endl
      <<"s = "<<s<<" +- "<<sErr<<endl
      <<"a0 = "<<a0<<" +- "<<a0Err<<endl
      <<"a1 = "<<a1<<" +- "<<a1Err<<endl
      <<"a2 = "<<a2<<" +- "<<a2Err<<endl;
  
  TF1 fitFunction("fitFunction", FitFunction, minX, maxX, 8+2*nfound);
  //0: nof peaks
  //1-3: background par a0-2
  //4: sigma, 5: t, 6: s, 7: b
  //8+2i: pos, 9+2i: amp, 
  fitFunction.SetParameter(0,nfound);
  fitFunction.SetParameter(1,a0);
  fitFunction.SetParameter(2,a1);
  fitFunction.SetParameter(3,a2);
  fitFunction.SetParameter(4,sigma);
  fitFunction.SetParameter(5,s);
  fitFunction.SetParameter(6,t);
  fitFunction.SetParameter(7,b);
  for(int i = 0; i < nfound; ++i) {
    fitFunction.SetParameter(8+2*i,CalcPositions[i]);
    fitFunction.SetParameter(9+2*i,CalcAmplitudes[i]);
  }
  fitFunction.SetLineColor(kBlue);
  cout<<"fit(1000): "<<fitFunction.Eval(1000.)<<endl;
}
