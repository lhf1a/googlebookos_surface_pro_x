"""Execute the production poll loop with bounded fake QMI results, no device IO."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace net {using RegStateResult=int;using SignalStrength=int;}
namespace sim {using CardStatus=int;}
namespace mdm {enum class RadioState{UNAVAILABLE,OFF,ON};}
int ticks=0,scenario=0,modemCount=0,networkCount=0,signalCount=0;
struct Done{};
void step(){if(++ticks==3)throw Done{};}
struct Changes{template<class F>void post(F f){f();}};
struct Core {std::atomic<bool> networkDirty{true};Changes changes;
 mdm::RadioState state(){return scenario==3 && ticks>0?mdm::RadioState::ON:mdm::RadioState::UNAVAILABLE;}
 int registration(){if(scenario==1)throw std::runtime_error("registration error");return 3;}
 int strength(){if(scenario==0 || (scenario==2 && ticks==0))throw std::runtime_error("signal error74");return -90;}
 int card(){return 0;}void checkBearer(){};
} instance,*core=&instance;
struct Network {void changed(int,bool n,bool s){networkCount+=n;signalCount+=s;}} nn,*network=&nn;
struct Modem {void changed(mdm::RadioState){++modemCount;}} mm,*modem=&mm;
struct Sim{void changed(){}} ss;
void test(){auto sim=&ss;
'''
SUFFIX = r'''
}
int main(int argc,char**argv){if(argc!=2)return 2;scenario=atoi(argv[1]);
try{test();}catch(const Done&){}printf("%d %d %d\n",modemCount,networkCount,signalCount);return 0;}
'''


class RadioPoll(unittest.TestCase):
    def test_independent_notifications_and_recovery(self):
        source = (ROOT/'probe/lte/sq2-radio.cpp').read_text()
        start = source.index('            net::RegStateResult oldReg;')
        end = source.index('        }).detach();', start)
        loop = source[start:end].replace('std::this_thread::sleep_for(std::chrono::seconds(5))', 'step()')
        # Mock scalar values replace generated AIDL structs with default fields.
        for name in ('oldReg', 'oldSignal', 'oldCard'):
            loop = loop.replace(name+';', name+'=0;')
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp)/'poll.cpp', Path(tmp)/'poll'
            cpp.write_text(PREFIX+loop+SUFFIX)
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
            for scenario, expected in enumerate(('1 1 0', '1 0 1', '1 1 1', '2 1 1')):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(binary), str(scenario)], check=True, capture_output=True, text=True)
                    self.assertEqual(result.stdout.strip(), expected)
                    self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
