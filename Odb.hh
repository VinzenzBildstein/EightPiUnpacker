#ifndef __ODB_HH
#define __ODB_HH

#include <string>
#include <vector>
//#include <utility>

#include "pugixml.hpp"

class OdbEntry {
public:
  OdbEntry(){};
  OdbEntry(pugi::xml_node);
  ~OdbEntry(){};

  void Print(std::string);

private:
  std::string fName;
  std::string fType;
  std::string fValue;
  //std::vector<std::pair<std::string,std::string> > fAttributes;
};

class OdbDirectory {
public:
  OdbDirectory(){};
  OdbDirectory(pugi::xml_node);
  ~OdbDirectory(){};

  void Print(std::string);

  void AddEntry(OdbEntry entry) {
    fEntries.push_back(entry);
  }
  void AddSubDirectory(pugi::xml_node subDirectory) {
    fSubDirectories.push_back(OdbDirectory(subDirectory));
  }

private:
  std::string fName;
  std::vector<OdbDirectory> fSubDirectories;
  std::vector<OdbEntry> fEntries;
};

class Odb {
public:
  Odb(){};
  ~Odb(){};

  void ParseInformation(std::vector<uint16_t>);

  void Print() {
    fBase.Print(std::string(""));
  }

private:
  OdbDirectory fBase;
};

#endif
