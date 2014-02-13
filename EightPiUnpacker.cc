#include <iostream>
#include <iomanip>

#include "TFile.h"
#include "TTree.h"
#include "TStopwatch.h"

#include "CommandLineInterface.hh"
#include "Utilities.hh"
#include "TextAttributes.hh"

#include "MidasFileManager.hh"
#include "MidasEventProcessor.hh"
#include "Settings.hh"

using namespace std;

void exitFunction() {
  //reset the text attributes of std-out and -err
  cout<<Attribs::Reset<<flush;
  cerr<<Attribs::Reset<<flush;
}

int main(int argc, char** argv) {
  atexit(exitFunction);
  
  CommandLineInterface interface;
  string midasFileName;
  interface.Add("-if","midas file name (required)",&midasFileName);
  string rootFileName;
  interface.Add("-of","root file name (optional, default = replacing extension with .root)",&rootFileName);
  string settingsFileName = "Settings.dat";
  interface.Add("-sf","settings file name (optional, default = 'Settings.dat'",&settingsFileName);
  int verbosityLevel = 0;
  interface.Add("-vl","level of verbosity (optional, default = 0)",&verbosityLevel);
  
  //-------------------- check flags and arguments --------------------
  interface.CheckFlags(argc, argv);

  if(midasFileName.empty()) {
    cerr<<Attribs::Bright<<Foreground::Red<<"I need the name of the midas file!"<<Attribs::Reset<<endl;
    return 1;
  }

  if(!FileExists(midasFileName)) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find midas file '"<<midasFileName<<"'"<<Attribs::Reset<<endl;
    return 1;
  }

  if(rootFileName.empty()) {
    size_t extension = midasFileName.rfind('.');

    if(extension == string::npos) {
      cerr<<Attribs::Bright<<Foreground::Red<<"Failed tp find extension of midas file name, please provide root file name."<<Attribs::Reset<<endl;
      return 1;
    }
    rootFileName = midasFileName.substr(0,extension);
    rootFileName.append(".root");
    if(verbosityLevel > 0) {
      cout<<"created root file name '"<<rootFileName<<"' from midas file name '"<<midasFileName<<"'"<<endl;
    }
  }

  //-------------------- create/open the settings --------------------
  if(!FileExists(settingsFileName)) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find midas file '"<<settingsFileName<<"'"<<Attribs::Reset<<endl;
    return 1;
  }
  Settings settings(settingsFileName, verbosityLevel);

  //-------------------- open root file and tree --------------------
  TFile rootFile(rootFileName.c_str(),"recreate");

  if(!rootFile.IsOpen()) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Failed to open root file '"<<rootFileName<<"' for writing"<<Attribs::Reset<<endl;
    return 1;
  }

  TTree tree("tree","gsort tree");

  //-------------------- variables needed --------------------
  TStopwatch watch;
  size_t totalEvents = 0;
  size_t oldPosition = 0;
  MidasFileManager fileManager(midasFileName,&settings);
  MidasEvent currentEvent;
  MidasEventProcessor eventProcessor(&settings,&tree);

  //-------------------- get the file header --------------------
  MidasFileHeader fileHeader = fileManager.ReadHeader();
  if(verbosityLevel > 0) {
    cout<<"Run number: "<<fileHeader.RunNumber()<<endl
	<<"Start time: "<<hex<<fileHeader.StartTime()<<dec<<endl
	<<"Number of bytes in header: "<<fileHeader.InformationLength()<<dec<<endl
	<<"Starting main loop:"<<endl
	<<endl;
  }

  //-------------------- main loop --------------------
  while(fileManager.Status() != MidasFileManager::kEoF) {
    //zero the old event
    currentEvent.Zero();
    //if we suceed in reading the event from file, we process it
    if(fileManager.Read(currentEvent)) {
      if(!eventProcessor.Process(currentEvent)) {
	break;
      }
    }
    totalEvents++;
    if(totalEvents%1000 == 0) {
      cout<<setw(5)<<fixed<<setprecision(1)<<(100.*fileManager.Position())/fileManager.Size()<<"%: read "<<totalEvents<<" events ("<<1000./watch.RealTime()<<" events/s = "<<1000.*(fileManager.Position()-oldPosition)/watch.RealTime()<<" kB/s)\r"<<flush;
      watch.Continue();
    }
  }
  cout<<endl;

  //check whether we've reached the end of file
  if(fileManager.Status() != MidasFileManager::kEoF) {
    cerr<<Attribs::Bright<<Foreground::Red<<"Failed to read all events, got only "<<totalEvents<<" events from "<<fileManager.Position()<<" bytes out of "<<fileManager.Size()<<" bytes."<<Attribs::Reset<<endl;
  }

  //-------------------- flush all events to file and close all files --------------------
  eventProcessor.Flush();

  fileManager.Close();
  rootFile.Close();
  
  return 0;
}
