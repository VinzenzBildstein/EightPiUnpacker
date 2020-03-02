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

void exitFunction() {
  //reset the text attributes of std-out and -err
  std::cout<<Attribs::Reset<<std::flush;
  std::cerr<<Attribs::Reset<<std::flush;
}

int main(int argc, char** argv) {
  atexit(exitFunction);
  
  CommandLineInterface interface;
  std::string midasFileName;
  interface.Add("-if","midas file name (required)",&midasFileName);
  std::string rootFileName;
  interface.Add("-of","root file name (optional, default = replacing extension with .root)",&rootFileName);
  std::string settingsFileName = "Settings.dat";
  interface.Add("-sf","settings file name (optional, default = 'Settings.dat'",&settingsFileName);
  std::string statisticsFile = "BufferStatistics.dat";
  interface.Add("-bf","buffer statistics file name (optional, default = 'BufferStatistics.dat'",&statisticsFile);
  bool statusUpdate = false;
  interface.Add("-su","activate status update",&statusUpdate);
  size_t nofEvents = 0;
  interface.Add("-ne","maximum number of events to be processed",&nofEvents);
  int verbosityLevel = 0;
  interface.Add("-vl","level of verbosity (optional, default = 0)",&verbosityLevel);
  
  //-------------------- check flags and arguments --------------------
  interface.CheckFlags(argc, argv);

  if(midasFileName.empty()) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"I need the name of the midas file!"<<Attribs::Reset<<std::endl;
    return 1;
  }

  if(!FileExists(midasFileName)) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find midas file '"<<midasFileName<<"'"<<Attribs::Reset<<std::endl;
    return 1;
  }

  if(rootFileName.empty()) {
    size_t extension = midasFileName.rfind('.');

    if(extension == std::string::npos) {
      std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed tp find extension of midas file name, please provide root file name."<<Attribs::Reset<<std::endl;
      return 1;
    }
    rootFileName = midasFileName.substr(0,extension);
    rootFileName.append(".root");
    if(verbosityLevel > 0) {
      std::cout<<"created root file name '"<<rootFileName<<"' from midas file name '"<<midasFileName<<"'"<<std::endl;
    }
  }

  //-------------------- create/open the settings --------------------
  if(!FileExists(settingsFileName)) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to find midas file '"<<settingsFileName<<"'"<<Attribs::Reset<<std::endl;
    return 1;
  }
  Settings settings(settingsFileName, verbosityLevel);

  //-------------------- open root file and tree --------------------
  TFile rootFile(rootFileName.c_str(),"recreate");

  if(!rootFile.IsOpen()) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to open root file '"<<rootFileName<<"' for writing"<<Attribs::Reset<<std::endl;
    return 1;
  }

  TTree tree("tree","gsort tree");

  //-------------------- variables needed --------------------
  TStopwatch watch;
  size_t totalEvents = 0;
  size_t oldPosition = 0;
  MidasFileManager fileManager(midasFileName, &settings);
  MidasEvent currentEvent;
  MidasEventProcessor eventProcessor(&settings, &rootFile, &tree, statisticsFile, statusUpdate);

  //-------------------- get the file header --------------------
  MidasFileHeader fileHeader = fileManager.ReadHeader();
  if(verbosityLevel > 0) {
    std::cout<<"Run number: "<<fileHeader.RunNumber()<<std::endl
	     <<"Start time: "<<hex<<fileHeader.StartTime()<<dec<<std::endl
	     <<"Number of bytes in header: "<<fileHeader.InformationLength()<<dec<<std::endl
	     <<"Starting main loop:"<<std::endl
	     <<std::endl
	     <<"===================="<<std::endl;
    fileHeader.PrintOdb();
    std::cout<<"===================="<<std::endl;
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
    if(totalEvents%10000 == 0) {
      std::cout<<setw(5)<<fixed<<setprecision(1)<<(100.*fileManager.Position())/fileManager.Size()<<"%: read "<<totalEvents<<" events ("<<1000./watch.RealTime()<<" events/s = "<<(fileManager.Position()-oldPosition)/watch.RealTime()/1024<<" kiB/s)\r"<<std::flush;
      oldPosition = fileManager.Position();
      watch.Continue();
    }
    if(nofEvents > 0 && totalEvents >= nofEvents) {
      break;
    }
  }
  std::cout<<std::endl;

  //check whether we've reached the end of file
  if(fileManager.Status() != MidasFileManager::kEoF) {
    std::cerr<<Attribs::Bright<<Foreground::Red<<"Failed to read all events, got only "<<totalEvents<<" events from "<<fileManager.Position()<<" bytes out of "<<fileManager.Size()<<" bytes."<<Attribs::Reset<<std::endl;
  } else if(verbosityLevel > 0) {
    std::cout<<"Reached end of file after "<<totalEvents<<" events from "<<fileManager.Position()<<" bytes out of "<<fileManager.Size()<<" bytes."<<std::endl;
  }

  //-------------------- flush all events to file and close all files --------------------
  eventProcessor.Flush();

  //if(verbosityLevel > 0) {
    eventProcessor.Print();
    //}

  fileManager.Close();
  tree.Write();
  rootFile.Close();
  
  return 0;
}
