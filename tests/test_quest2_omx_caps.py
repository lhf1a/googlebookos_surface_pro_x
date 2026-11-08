"""Capability translation uses firmware ranges and profile-specific levels."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class OmxCaps(unittest.TestCase):
 def test_runtime_caps_and_rejection(self):
  source=r'''#include "quest2-omx-caps.h"
#include <cassert>
int main(int argc,char** argv){
 assert(argc==3);std::ifstream firmware(argv[1]);auto real=Quest2CodecCaps::parse(firmware);
 assert(real.profiles.size()==5&&real.profiles[0].level==0x20000);
 assert(real.limits.at(1).max==8192&&real.limits.at(3).max==138240);
 assert(real.xml().find("range=\"1-3888000\"")!=std::string::npos);
 for(auto kind:{Quest2CodecCaps::Hevc,Quest2CodecCaps::Vp8,Quest2CodecCaps::Vp9,Quest2CodecCaps::Mpeg2}) {
  Quest2CodecCaps key;key.codec=kind;
  std::ifstream f(std::string(argv[2])+"/"+key.suffix()+"-hfi-caps.txt");
  auto caps=Quest2CodecCaps::parse(f,kind);
  assert(caps.nv12&&caps.xml().find(caps.name())!=std::string::npos);
  if(kind==Quest2CodecCaps::Hevc)assert(caps.profiles.size()==2&&caps.profiles[1].profile==2&&caps.profiles[1].level==0x200000&&caps.p010);
  if(kind==Quest2CodecCaps::Vp9)assert(caps.profiles.size()==2&&caps.profiles[1].profile==4&&caps.profiles[1].level==0x1000&&caps.p010);
  if(kind==Quest2CodecCaps::Vp8)assert(caps.profiles.size()==4&&caps.profiles[3].level==8&&!caps.p010&&caps.limits.at(4).max==1036800);
  if(kind==Quest2CodecCaps::Mpeg2)assert(caps.profiles[0].profile==0&&caps.profiles[1].profile==1&&caps.profiles[1].level==3&&!caps.p010&&caps.limits.at(1).max==1920);
 }
 const std::string raw="version=1\nsessions=8\ncap=1,96,4096,1\ncap=2,96,4096,1\ncap=3,1,36864,1\ncap=4,1,2073600,1\ncap=5,1,480,1\ncap=8,1,120000000,1\npl=1,32768\npl=4,65536\npl=64,65536\nfmt=3,2\nend=1\n";
 std::istringstream in(raw);auto c=Quest2CodecCaps::parse(in);
 assert(c.profiles.size()==3&&c.profiles[1].profile==8&&c.profiles[1].level==65536);
 auto xml=c.xml();assert(xml.find("max=\"4096x4096\"")!=std::string::npos);
 assert(xml.find("range=\"1-2073600\"")!=std::string::npos);
 assert(xml.find("max=\"8\"")!=std::string::npos);
 for(auto bad:{raw.substr(0,raw.find("end=")),raw+"pl=1,1\n",raw+std::string(4096,'x'),std::string("version=1\ncap=1,-1,4,1\nend=1\n")}){
  bool rejected=false;try{std::istringstream b(bad);Quest2CodecCaps::parse(b);}catch(const std::exception&){rejected=true;}assert(rejected);
 }
 for(uint32_t level:{0x20000u,0x40000u,0x80000u}) {
  auto sample=raw;sample.replace(sample.find("pl=4,65536"),10,"pl=4,"+std::to_string(level));
  std::istringstream b(sample);auto caps=Quest2CodecCaps::parse(b);
  assert(caps.profiles[1].level==level);
 }
 auto invalid=raw;invalid.replace(invalid.find("pl=4,65536"),10,"pl=4,1048576");
 bool badLevel=false;try{std::istringstream b(invalid);Quest2CodecCaps::parse(b);}catch(const std::exception&){badLevel=true;}assert(badLevel);
 auto unknown=raw;unknown.replace(unknown.find("pl=64,65536"),11,"pl=128,65536");
 bool rejected=false;try{std::istringstream b(unknown);Quest2CodecCaps::parse(b);}catch(const std::exception&){rejected=true;}assert(rejected);
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.cpp').write_text(source)
   subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(ROOT/'probe/video'),str(p/'test.cpp'),'-o',str(p/'test')],check=True,text=True)
   subprocess.run([str(p/'test'),str(ROOT/'tests/fixtures/quest2_video/avc-hfi-caps.txt'),str(ROOT/'tests/fixtures/quest2_video')],check=True,text=True)
if __name__=='__main__':unittest.main()
