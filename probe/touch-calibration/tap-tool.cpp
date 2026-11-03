// SPDX-License-Identifier: GPL-2.0-or-later
#include "tap-core.hpp"
#include <iostream>
int main(int argc,char**argv)try{if(argc!=2)throw std::runtime_error("device required");std::string s,l;while(std::getline(std::cin,l)){if(s.size()+l.size()>512)throw std::runtime_error("size");s+=l+'\n';}std::cout<<surface_touch::TapPolicy::parse(s,argv[1]).encode();return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
