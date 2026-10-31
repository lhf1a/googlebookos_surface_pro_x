// SPDX-License-Identifier: GPL-2.0-or-later
// Strict, data-only boundary between the Android wizard and the root helper.
#include "../test2/sq2-touch-calibration.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>
#include <map>
#include <set>
#include <cerrno>

using namespace iptsd::apps::daemon;
int main(int argc, char **argv) try {
    std::string body, line;
    while (std::getline(std::cin,line)) {
        if (line.size()>190 || body.size()+line.size()>4096) throw std::runtime_error("Profile too large");
        body+=line+'\n';
    }
    if (argc==4 && std::string(argv[1])=="publish") {
        // Directory is the known package's device-protected files directory.
        std::string name=argv[3];
        if(name!="context" && name!="reply") throw std::runtime_error("Invalid output name");
        int dir=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        struct stat st{};if(dir<0 || fstat(dir,&st)) throw std::runtime_error("Invalid directory");
        std::string temp=".bridge-"+std::to_string(getpid());
        int fd=openat(dir,temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0) throw std::runtime_error("Cannot create reply");
        if(fchown(fd,st.st_uid,st.st_gid)) throw std::runtime_error("Cannot set owner");
        size_t offset=0;while(offset<body.size()) {ssize_t n=write(fd,body.data()+offset,body.size()-offset);if(n<=0)throw std::runtime_error("Write failed");offset+=n;}
        if(fsync(fd) || close(fd) || renameat(dir,temp.c_str(),dir,name.c_str())) throw std::runtime_error("Publish failed");
        close(dir);return 0;
    }
    if(argc!=3 || std::string(argv[1])!="validate") throw std::runtime_error("Usage: validate DEVICE");
    std::string device=argv[2];
    if(device.size()!=64 || device.find_first_not_of("0123456789abcdef")!=std::string::npos) throw std::runtime_error("Invalid identity");
    std::istringstream in(body);std::map<std::string,std::string> values;
    const std::set<std::string> keys={"Version","Device","CalibrationX","CalibrationY","CalibrationXTop","CalibrationTopStart","CalibrationTopEnd","ContactSizeMax"};
    while(std::getline(in,line)) {
        auto eq=line.find('=');if(eq==std::string::npos)throw std::runtime_error("Invalid field");
        auto key=line.substr(0,eq),value=line.substr(eq+1);
        if(!keys.count(key)||values.count(key))throw std::runtime_error("Unexpected field");
        values[key]=value;
    }
    // Existing coordinate-only profiles retain the original 2.0 cm limit.
    if(!values.count("ContactSizeMax"))values["ContactSizeMax"]="2.0";
    if(values.size()!=keys.size()||values["Version"]!="1"||values["Device"]!=device)throw std::runtime_error("Wrong device or version");
    if(values["ContactSizeMax"]!="2.0" && values["ContactSizeMax"]!="2.4")throw std::runtime_error("Invalid contact size option");
    if(values["CalibrationTopStart"]!="0.1" || values["CalibrationTopEnd"]!="0.3")throw std::runtime_error("Invalid blend");
    SQ2TouchCalibration c;c.load(values["CalibrationX"],values["CalibrationY"],values["CalibrationXTop"],.1,.3);
    for(int yi=0;yi<=100;++yi)for(int xi=0;xi<=100;++xi) {
        double x=xi/100.,y=yi/100.;auto p=c.map(x,y);
        if(std::abs(p.first-x)>.08 || std::abs(p.second-y)>.08)throw std::runtime_error("Excessive correction");
    }
    std::cout<<"[Touchscreen]\n";
    for(auto key:{"CalibrationX","CalibrationY","CalibrationXTop","CalibrationTopStart","CalibrationTopEnd"})std::cout<<key<<" = "<<values[key]<<'\n';
    std::cout<<"[Contacts]\nSizeMax = "<<values["ContactSizeMax"]<<'\n';
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
