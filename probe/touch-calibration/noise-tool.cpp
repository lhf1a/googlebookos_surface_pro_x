#include "adaptive-noise.hpp"
#include <iostream>
int main(int argc,char **argv)try {
    if(argc!=2)throw std::runtime_error("device required");
    std::string s,line;while(std::getline(std::cin,line)){s+=line+'\n';if(s.size()>4096)throw std::runtime_error("size");}
    auto p=surface_touch::Profile::parse(s,argv[1]);std::cout<<p.encode();return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
