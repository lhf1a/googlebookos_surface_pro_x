import pathlib,subprocess,tempfile,unittest,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('g++'),'requires native C++ compiler')
class CalibrationTest(unittest.TestCase):
    def test_real_curve_parser_mapping_and_invalid_profiles(self):
        source=r'''
#include "sq2-touch-calibration.hpp"
#include <cassert>
using iptsd::apps::daemon::SQ2CalibrationCurve;
using iptsd::apps::daemon::SQ2TouchCalibration;
int main(){
    SQ2CalibrationCurve c;
    for(int i=0;i<=10000;++i){double x=i/10000.;assert(c.map(x)==x);}
    c.load("0:0 .94:.94 .9957:.9805 1:1");
    assert(c.map(0)==0 && c.map(1)==1 && c.map(.5)==.5);
    assert(std::abs(c.map(.9957)-.9805)<1e-12);
    double last=-1;
    for(int i=0;i<=100000;++i){double x=i/100000.,y=c.map(x);assert(y>=0&&y<=1&&y>last);last=y;}
    for(auto text:{"0:0 1:.99","0:0 .8:.9 .8:.95 1:1","0:0 .5:.8 .9:.7 1:1",
        "0:0 .99:.5 1:1","0:0 .5:.01 1:1","0:0 nan:.5 1:1","0:0 .5:inf 1:1",
        "0:0 .5:.5junk 1:1","0:0 .5;.5 1:1","0:0 2:1","0:0"}){
        bool failed=false;try{c.load(text);}catch(const std::invalid_argument&){failed=true;}
        assert(failed);assert(std::abs(c.map(.9957)-.9805)<1e-12);
    }
    c.load("");assert(c.map(.9957)==.9957);
    SQ2TouchCalibration spatial;
    spatial.load("0:0 .02:.03 .12:.12 1:1", "0:0 .03:.04 .12:.12 1:1", "0:0 1:1", .1, .3);
    assert(std::abs(spatial.map(.02,.05).first-.02)<1e-12);
    assert(std::abs(spatial.map(.02,.2).first-.025)<1e-12);
    assert(std::abs(spatial.map(.02,.5).first-.03)<1e-12);
    assert(std::abs(spatial.map(.02,.03).second-.04)<1e-12);
    for(int yi=0;yi<=100;++yi){
        double previous=-1;
        for(int xi=0;xi<=10000;++xi){
            auto p=spatial.map(xi/10000.,yi/100.);
            assert(p.first>previous && p.first>=0 && p.first<=1);previous=p.first;
        }
    }
    for(auto interval: {std::pair<double,double>{.3,.1},{.1,.1},{-.1,.2},{.1,1.1},{NAN,.3},{.1,INFINITY}}){
        bool failed=false;
        try{spatial.load("", "", "0:0 1:1", interval.first, interval.second);}
        catch(const std::invalid_argument&){failed=true;}
        assert(failed && std::abs(spatial.map(.02,.5).first-.03)<1e-12);
    }
    spatial.load("", "");assert(spatial.map(.02,.2)==std::make_pair(.02,.2));
}
'''
        with tempfile.TemporaryDirectory() as work:
            w=pathlib.Path(work);(w/'test.cpp').write_text(source)
            subprocess.run(['g++','-std=c++17','-O2','-I'+str(ROOT/'probe/test2'),str(w/'test.cpp'),'-o',str(w/'test')],check=True)
            subprocess.run([str(w/'test')],check=True)

if __name__=='__main__':unittest.main()
