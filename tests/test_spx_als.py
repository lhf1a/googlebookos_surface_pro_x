import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("alsdt", ROOT/"scripts/prepare-spx-als-dtb.py")
alsdt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(alsdt)


class AlsTests(unittest.TestCase):
    def fixture(self, compatible="avago,apds9960", board="microsoft,surface-pro-x"):
        text = f"""/dts-v1/;
        /memreserve/ 0x99000000 0x1400000;
        / {{ compatible = "{board}", "qcom,sc8180x";
          #address-cells = <2>; #size-cells = <2>;
          unrelated {{ phandle = <333>; keep = <7>; }};
          i2c@888000 {{ compatible = "qcom,geni-i2c";
            reg = <0 0x888000 0 0x4000>; #address-cells = <1>; #size-cells = <0>;
            als@39 {{ compatible = "{compatible}"; reg = <0x39>; }};
          }};
        }};"""
        return subprocess.check_output(["dtc", "-I", "dts", "-O", "dtb"], input=text.encode())

    def test_only_compatible_changes_and_idempotent(self):
        before = self.fixture()
        after = alsdt.prepare(before)
        a = alsdt.dt.properties(alsdt.libfdt.Fdt(before))
        b = alsdt.dt.properties(alsdt.libfdt.Fdt(after))
        self.assertEqual({k for k in a if a[k] != b[k]}, {("/i2c@888000/als@39", "compatible")})
        self.assertEqual(alsdt.prepare(after), after)

    def test_wrong_board_or_unknown_sensor_rejected(self):
        for data in (self.fixture(board="other,board"), self.fixture(compatible="other,sensor")):
            with self.assertRaises(ValueError):
                alsdt.prepare(data)

    def test_actual_sample_io_errors_and_power_off(self):
        source = (ROOT/"probe/sensors/spx-tcs3430.c").read_text()
        sample = source[source.index("static int tcs_sample"):source.index("static int tcs_read_raw")]
        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#define BIT(x) (1u<<(x))
#define time_before(a,b) ((long)((a)-(b))<0)
#define msecs_to_jiffies(x) (x)
static unsigned long jiffies;
typedef uint8_t u8;
#define TCS_ENABLE 0x80
#define TCS_STATUS 0x93
#define TCS_DATA 0x94
#define IIO_VAL_INT 1
struct spx_tcs3430 { void *client; int frame[4]; unsigned long expires; unsigned int read_mask; bool frame_valid; };
static int status, block, fail_enable, fail_stop, writes, powered, last_reg, slept;
static int tcs_write(struct spx_tcs3430 *s, u8 reg, u8 v) {
    (void)s; last_reg=reg; writes++;
    if(reg==TCS_ENABLE) {
        if(v==3 && fail_enable) return -EIO;
        if(v==0 && writes>1 && fail_stop) return -EIO;
        powered=v;
    }
    return 0;
}
static void msleep(int ms) { slept=ms; }
static int i2c_smbus_read_byte_data(void *c, int r) { (void)c; (void)r; return status; }
static int i2c_smbus_read_i2c_block_data(void *c, int r, int n, u8 *b) {
    (void)c; (void)r; assert(n==8);
    for(int i=0;i<8;i++) b[i]=i+1;
    return block;
}
""" + sample + r"""
static void reset(void) { status=0; block=8; fail_enable=fail_stop=writes=powered=slept=0; }
int main(void) {
    struct spx_tcs3430 s={0}; int v=-1;
    for(int i=0;i<4;i++) {
        reset(); assert(tcs_sample(&s,i,&v)==IIO_VAL_INT);
        assert(v==((2*i+1)|((2*i+2)<<8))); assert(!powered && slept>=101);
    }
    reset(); status=-EREMOTEIO; assert(tcs_sample(&s,1,&v)==-EREMOTEIO); assert(!powered);
    reset(); status=0x80; assert(tcs_sample(&s,1,&v)==-EOVERFLOW); assert(!powered);
    reset(); block=7; assert(tcs_sample(&s,1,&v)==-EIO); assert(!powered);
    reset(); block=-EREMOTEIO; assert(tcs_sample(&s,1,&v)==-EREMOTEIO); assert(!powered);
    reset(); fail_enable=1; assert(tcs_sample(&s,1,&v)==-EIO); assert(last_reg==TCS_ENABLE);
    reset(); fail_stop=1; assert(tcs_sample(&s,1,&v)==-EIO);
    reset(); assert(tcs_cached_sample(&s,3,&v)==IIO_VAL_INT); int count=writes;
    assert(tcs_cached_sample(&s,1,&v)==IIO_VAL_INT && writes==count && v==0x0403);
    assert(tcs_cached_sample(&s,0,&v)==IIO_VAL_INT && writes==count);
    assert(tcs_cached_sample(&s,2,&v)==IIO_VAL_INT && writes==count);
    assert(tcs_cached_sample(&s,3,&v)==IIO_VAL_INT && writes>count);
    count=writes; jiffies=201;
    assert(tcs_cached_sample(&s,1,&v)==IIO_VAL_INT && writes>count);
    jiffies=402; status=-EREMOTEIO;
    assert(tcs_cached_sample(&s,1,&v)==-EREMOTEIO && !s.frame_valid);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)/"sample.c"
            exe = Path(tmp)/"sample"
            src.write_text(harness)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
