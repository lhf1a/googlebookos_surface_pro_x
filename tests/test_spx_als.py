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
        sample = source[source.index("#define TCS_CYCLES"):source.index("#define TCS_CHANNEL")]
        # Drop tcs_write definition so the test harness supplies a mock tcs_write.
        w_start = sample.index("static int tcs_write")
        w_end = sample.index("static int tcs_set_gain")
        sample = sample[:w_start] + sample[w_end:]
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
#define TCS_CFG1 0x90
#define TCS_STATUS 0x93
#define TCS_DATA 0x94
#define IIO_VAL_INT 1
#define IIO_VAL_INT_PLUS_MICRO 2
#define IIO_CHAN_INFO_RAW 1
#define IIO_CHAN_INFO_HARDWAREGAIN 2
#define IIO_CHAN_INFO_INT_TIME 3
struct i2c_client { int dummy; };
struct mutex { int locked; };
static void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = 0; }
struct iio_dev { void *priv; };
struct iio_chan_spec { unsigned int address; };
static void *iio_priv(struct iio_dev *d) { return d->priv; }
struct spx_tcs3430;
static int status_seq[4], status_idx, status_len;
static int block_ret, fail_enable, fail_stop, fail_cfg1;
static int writes, conversions, powered, last_reg, slept, cfg1_val, cfg1_writes;
static int sim_y_by_gain[4];
static int use_sim_y;
static int tcs_write(struct spx_tcs3430 *s, u8 reg, u8 v) {
    (void)s; last_reg=reg; writes++;
    if (reg == TCS_CFG1) {
        if (fail_cfg1) return -EIO;
        cfg1_val = v;
        cfg1_writes++;
    }
    if (reg == TCS_ENABLE) {
        if (v == 3 && fail_enable) return -EIO;
        if (v == 0 && writes > 1 && fail_stop) return -EIO;
        if (v == 3) conversions++;
        powered = v;
    }
    return 0;
}
static void msleep(int ms) { slept = ms; }
static int i2c_smbus_read_byte_data(void *c, int r) {
    (void)c; (void)r;
    if (status_len > 0) {
        int idx = status_idx < status_len ? status_idx++ : status_len - 1;
        return status_seq[idx];
    }
    return 0;
}
static int i2c_smbus_read_i2c_block_data(void *c, int r, int n, u8 *b) {
    (void)c; (void)r; assert(n == 8);
    if (use_sim_y) {
        int y = sim_y_by_gain[cfg1_val & 3];
        int vals[4] = { y / 2, y, y / 4, y };
        for (int i = 0; i < 4; i++) {
            b[i * 2] = vals[i] & 0xff;
            b[i * 2 + 1] = (vals[i] >> 8) & 0xff;
        }
        return block_ret;
    }
    for (int i = 0; i < 8; i++) b[i] = i + 1;
    return block_ret;
}
""" + sample + r"""
static void reset(void) {
    status_seq[0] = 0; status_idx = 0; status_len = 1;
    block_ret = 8; fail_enable = fail_stop = fail_cfg1 = 0;
    writes = conversions = powered = last_reg = slept = cfg1_writes = 0;
    cfg1_val = TCS_GAIN_DEFAULT_CODE;
    use_sim_y = 0;
}
static void init_sensor(struct spx_tcs3430 *s) {
    *s = (struct spx_tcs3430){0};
    s->gain_code = TCS_GAIN_DEFAULT_CODE;
    s->frame_gain = tcs_gains[TCS_GAIN_DEFAULT_CODE];
}
int main(void) {
    struct spx_tcs3430 s; struct iio_dev indio = { .priv = &s };
    struct iio_chan_spec ch = { .address = 1 };
    int v = -1, v2 = 0, g = -1;

    /* Baseline 4-channel reads, power-off, and I/O errors. */
    for (int i = 0; i < 4; i++) {
        init_sensor(&s); reset();
        assert(tcs_sample(&s, i, &v) == IIO_VAL_INT);
        assert(v == ((2*i+1) | ((2*i+2) << 8)));
        assert(!powered && slept >= 101 && s.frame_gain == 16);
    }
    init_sensor(&s); reset(); status_seq[0] = -EREMOTEIO;
    assert(tcs_sample(&s, 1, &v) == -EREMOTEIO && !powered);
    init_sensor(&s); reset(); s.gain_code = 0; cfg1_val = 0; status_seq[0] = 0x80;
    assert(tcs_sample(&s, 1, &v) == -EOVERFLOW && !powered && conversions == 1);
    init_sensor(&s); reset(); block_ret = 7;
    assert(tcs_sample(&s, 1, &v) == -EIO && !powered);
    init_sensor(&s); reset(); block_ret = -EREMOTEIO;
    assert(tcs_sample(&s, 1, &v) == -EREMOTEIO && !powered);
    init_sensor(&s); reset(); fail_enable = 1;
    assert(tcs_sample(&s, 1, &v) == -EIO && last_reg == TCS_ENABLE);
    init_sensor(&s); reset(); fail_stop = 1;
    assert(tcs_sample(&s, 1, &v) == -EIO);

    /* 4-channel cache reuse and expiry. */
    init_sensor(&s); reset();
    assert(tcs_cached_sample(&s, 3, &v) == IIO_VAL_INT); int count = writes;
    assert(tcs_cached_sample(&s, 1, &v) == IIO_VAL_INT && writes == count && v == 0x0403);
    assert(tcs_cached_sample(&s, 0, &v) == IIO_VAL_INT && writes == count);
    assert(tcs_cached_sample(&s, 2, &v) == IIO_VAL_INT && writes == count);
    assert(tcs_cached_sample(&s, 3, &v) == IIO_VAL_INT && writes > count);
    count = writes; jiffies = 201;
    assert(tcs_cached_sample(&s, 1, &v) == IIO_VAL_INT && writes > count);
    jiffies = 402; status_seq[0] = -EREMOTEIO;
    assert(tcs_cached_sample(&s, 1, &v) == -EREMOTEIO && !s.frame_valid);

    /* Auto-gain downshift + single retry on overflow (status & 0x80): 16x -> 4x succeeds. */
    init_sensor(&s); reset(); use_sim_y = 1;
    sim_y_by_gain[1] = 18000;
    status_seq[0] = 0x80; status_seq[1] = 0x00; status_len = 2;
    ch.address = 1;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT);
    assert(v == 18000 && conversions == 2 && !powered);
    assert(s.gain_code == 1 && cfg1_val == 1 && cfg1_writes == 1);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 4);

    /* Auto-gain downshift on persistent overflow steps 16x -> 4x -> 1x for next call. */
    init_sensor(&s); reset(); use_sim_y = 1;
    sim_y_by_gain[0] = 12000;
    status_seq[0] = 0x80; status_seq[1] = 0x80; status_len = 2;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == -EOVERFLOW);
    assert(!powered && conversions == 2 && s.gain_code == 0 && cfg1_val == 0 && !s.frame_valid);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 1);
    status_seq[0] = 0x00; status_idx = 0; status_len = 1; conversions = 0;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT);
    assert(v == 12000 && conversions == 1 && !powered);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 1);

    /* Auto-gain downshift + single retry on Y > 52000: 16x (55000) -> 4x (13750). */
    init_sensor(&s); reset(); use_sim_y = 1;
    sim_y_by_gain[2] = 55000; sim_y_by_gain[1] = 13750;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT);
    assert(v == 13750 && conversions == 2 && !powered && s.gain_code == 1 && cfg1_val == 1);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 4);

    /* Auto-gain upshift on dark Y < 200: frame_gain stays 16 for cached frame, next frame uses 64x. */
    init_sensor(&s); reset(); use_sim_y = 1; jiffies = 0;
    sim_y_by_gain[2] = 50; sim_y_by_gain[3] = 200;
    ch.address = 3;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT);
    assert(v == 50 && conversions == 1 && !powered);
    assert(s.gain_code == 3 && cfg1_val == 3 && cfg1_writes == 1);
    /* Remaining cached channels + hardwaregain MUST report 16 (the actual gain used for this frame). */
    ch.address = 1;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT && v == 50 && conversions == 1);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 16);
    /* Next fresh sample integrates at 64x and reports hardwaregain == 64. */
    jiffies = 250;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT && v == 200 && conversions == 2);
    assert(!powered && s.gain_code == 3);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 64);

    /* Dark Y at max gain (64x) does not overflow gain_code past 3. */
    sim_y_by_gain[3] = 10; jiffies = 500; cfg1_writes = 0;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == IIO_VAL_INT && v == 10);
    assert(s.gain_code == 3 && cfg1_writes == 0 && !powered);
    assert(tcs_read_raw(&indio, &ch, &g, &v2, IIO_CHAN_INFO_HARDWAREGAIN) == IIO_VAL_INT && g == 64);

    /* TCS_CFG1 write error propagates and leaves sensor powered off. */
    init_sensor(&s); reset(); use_sim_y = 1; fail_cfg1 = 1;
    sim_y_by_gain[2] = 55000;
    assert(tcs_read_raw(&indio, &ch, &v, &v2, IIO_CHAN_INFO_RAW) == -EIO && !powered && s.gain_code == 2);
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
