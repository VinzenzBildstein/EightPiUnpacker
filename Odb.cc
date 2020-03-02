#include "Odb.hh"

#include <iostream>
#include <cstring>

void Odb::ParseInformation(std::vector<uint16_t> information) {
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_buffer_inplace(reinterpret_cast<char*>(information.data()),information.size()*2);//fInformation stores the header as 16-bit words

  if(!result) {
    std::cout<<"Parsing of "<<information.size()*2<<" bytes of information as Odb failed with "<<result.description()<<" at offset "<<result.offset<<std::endl;
  }

  pugi::xml_node odb = doc.child("odb");
  
  for(auto child : odb.children()) {
    //std::cout<<"adding base's sub-directory "<<child.name()<<std::endl;
    fBase.AddSubDirectory(child);
  }
}

OdbDirectory::OdbDirectory(pugi::xml_node node) {
  auto name = node.attribute("name");
  if(name) {
    fName = name.value();
  } else {
    std::cerr<<"Failed to find name of ODB-directory!"<<std::endl;
  }

  //loop over all "children"
  for(auto child : node.children()) {
    if(strcmp(child.name(),"dir") == 0) {
      //std::cout<<"adding '"<<fName<<"'s subdirectory '"<<child.name()<<"' with value '"<<child.value()<<"'"<<std::endl;
      fSubDirectories.push_back(OdbDirectory(child));
    } else if(strcmp(child.name(),"key") == 0) {
      //std::cout<<"adding '"<<fName<<"'s entry '"<<child.name()<<"' with value '"<<child.value()<<"'"<<std::endl;
      fEntries.push_back(OdbEntry(child));
    } else if(strcmp(child.name(),"keyarray") == 0) {
      std::cout<<"adding '"<<fName<<"'s entry '"<<child.name()<<"' with value '"<<child.value()<<"'"<<std::endl;
      fEntries.push_back(OdbEntry(child));
    } else {
      std::cout<<"found '"<<fName<<"'s entry '"<<child.name()<<"' with value '"<<child.value()<<"'"<<std::endl;
    }
  }
}

void OdbDirectory::Print(std::string parents) {
  //print base attributes
//  for(auto attribute : fAttributes) {
//    std::cout<<"'"<<parents<<"'"<<"/"<<attribute.first<<": "<<attribute.second<<std::endl;
//  }
  //loop over all "children"
  if(parents.compare("/") != 0) {
    parents.append("/");
  }
  parents.append(fName);
  for(auto entry : fEntries) {
    entry.Print(parents);
  }
  for(auto subDirectory : fSubDirectories) {
    subDirectory.Print(parents);
  }
}

OdbEntry::OdbEntry(pugi::xml_node node) {
  //get attributes
  fName = node.attribute("name").value();
  fType = node.attribute("type").value();
  //we ignore the size attribute of types "STRING" since we use a c++ string to store the value anyway
  //odb keys have only one child who's value is the value of the key
  fValue = node.first_child().value();
  //std::cout<<"found entry "<<fName<<": "<<fValue<<" ("<<fType<<")"<<std::endl;
}

void OdbEntry::Print(std::string parents) {
  std::cout<<"'"<<parents<<"'"<<"/"<<fName<<": "<<fValue<<" ("<<fType<<")"<<std::endl;
}

