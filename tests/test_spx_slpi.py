import ctypes.util
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/"scripts"/(name+".py"))
    m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m


dt=load("prepare-spx-slpi-dtb")
driver=load("build-spx-slpi")
efi=load("prepare-spx-slpi-efi")


def firmware(flags=0x8000005):
    b=bytearray(88)
    b[:7]=b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H",b,18,164)
    struct.pack_into("<I",b,28,52)
    struct.pack_into("<HH",b,42,32,1)
    struct.pack_into("<8I",b,52,1,84,0xb0100000,0x97300000,4,0x1400000,flags,0x100000)
    return bytes(b)


def tree():
    source="""/dts-v1/;
    / {
     compatible="microsoft,surface-pro-x","qcom,sc8180x";
     #address-cells=<2>; #size-cells=<2>;
     reserved-memory {
      #address-cells=<2>; #size-cells=<2>; ranges;
      scss@99000000 { reg=<0 0x99000000 0 0x1400000>; no-map; };
      adsp@96e00000 { reg=<0 0x96e00000 0 0x1a00000>; no-map; };
     };
     gic { compatible="arm,gic-v3"; };
     rpmhcc { compatible="qcom,sc8180x-rpmh-clk"; phandle=<973>; };
     rpmhpd { compatible="qcom,sc8180x-rpmhpd"; };
     qmp { compatible="qcom,sc8180x-aoss-qmp"; };
     mailbox { compatible="qcom,sc8180x-apss-shared"; };
     smmu@15000000 { compatible="qcom,sc8180x-smmu-500"; reg=<0 0x15000000 0 0x100000>; };
     smmu@2ca0000 { compatible="qcom,sc8180x-smmu-500"; reg=<0 0x2ca0000 0 0x10000>; };
     smp2p-slpi { master-kernel {}; slave-kernel {}; };
     soc { remoteproc@17300000 { compatible="qcom,sc8180x-adsp-pas"; reg=<0 0x17300000 0 0x1000>; }; };
    };"""
    return subprocess.check_output(["dtc","-q","-I","dts","-O","dtb"],input=source.encode())


class SlpiTests(unittest.TestCase):
    def test_relocation_and_existing_resources_preserved(self):
        before=tree()
        after=dt.prepare(before,firmware(),"qcom/test/slpi.mbn")
        a=dt.dt.properties(libfdt.Fdt(before)); b=dt.dt.properties(libfdt.Fdt(after))
        self.assertTrue(all(b[k]==v for k,v in a.items()))
        self.assertEqual(b["/soc/remoteproc@2400000","microsoft,el2-preboot-complete"],dt.cells(0))
        self.assertEqual(b["/soc/remoteproc@2400000","firmware-name"],b"qcom/test/slpi.mbn\0")
        memory=int.from_bytes(b["/soc/remoteproc@2400000","memory-region"],"big")
        self.assertGreater(memory,973)
        self.assertEqual(b["/reserved-memory/scss@99000000","phandle"],dt.cells(memory))
        with self.assertRaises(ValueError):
            dt.prepare(after,firmware(),"qcom/test/slpi.mbn")

    def test_firmware_and_memory_rejection(self):
        for image in (b"short",firmware(5),firmware()[:-1]):
            with self.assertRaises(ValueError): dt.firmware_span(image)
        f=libfdt.Fdt(tree()); f.resize(len(f.as_bytearray())+1024)
        f.setprop(f.path_offset("/reserved-memory/adsp@96e00000"),"reg",dt.cells(0,0x98000000,0,0x2000000))
        with self.assertRaisesRegex(ValueError,"overlapping"):
            dt.prepare(bytes(f.as_bytearray()),firmware(),"qcom/test.mbn")
        with self.assertRaises(ValueError): dt.prepare(tree(),firmware(),"../other")

    def test_pas_driver_does_not_claim_audio_or_modem(self):
        text='''/* SQ2_EL2_PREBOOT_HANDOFF */
if (!(of_device_is_compatible(np, "qcom,sc8180x-adsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-cdsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-mpss-pas"))) return -EINVAL;
static const struct of_device_id qcom_pas_of_match[] = {
 { .compatible = "qcom,sc8180x-adsp-pas" },
};
.name = "qcom_q6v5_pas"
'''
        result=driver.source_for_slpi(text)
        self.assertNotIn('"qcom,sc8180x-adsp-pas"',result)
        self.assertIn('"qcom,sc8180x-slpi-pas"',result)
        self.assertIn("return -EINVAL",result)
        with self.assertRaises(ValueError): driver.source_for_slpi("unknown")

    def test_efi_handoff_copies_only_completed_result(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); src=root/"source"; (src/"src").mkdir(parents=True)
            (src/"src/pil-types.c").write_text("static const struct pil_type pil_types[] = {\n};")
            (src/"src/sq2-preboot-rmtfs.c").write_text(
                "EFI_STATUS sq2_preboot_publish_results(void) {\n"
                "\tWriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));\n}")
            efi.prepare(src,root/"output")
            s=(root/"output/src/sq2-preboot-rmtfs.c").read_text()
            helper=s[s.index("static EFI_STATUS spx_slpi_publish"):s.index("EFI_STATUS sq2_preboot_publish_results")]
            harness=r"""
#include <assert.h>
#include <stdint.h>
#include <libfdt.h>
typedef int EFI_STATUS;
#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER 1
#define EFI_NOT_READY 2
static void *result_dtb;
""" + helper + r"""
static void init(void *b) {
 assert(!fdt_create_empty_tree(b,4096));
 int soc=fdt_add_subnode(b,0,"soc@0");
 int n=fdt_add_subnode(b,soc,"remoteproc@2400000");
 assert(n>=0);
 assert(!fdt_setprop_string(b,n,"compatible","qcom,sc8180x-slpi-pas"));
 assert(!fdt_setprop(b,n,"microsoft,el2-preboot",NULL,0));
 assert(!fdt_setprop_u32(b,n,"microsoft,el2-preboot-complete",0));
 uint32_t z[6]={0};
 assert(!fdt_setprop(b,n,"microsoft,el2-preboot-status",z,sizeof(z)));
}
int main(void) {
 uint64_t a[512],b[512]; result_dtb=a;
 assert(!fdt_create_empty_tree(a,sizeof(a))); assert(!fdt_create_empty_tree(b,sizeof(b)));
 assert(spx_slpi_publish(b)==EFI_SUCCESS);
 init(a); assert(spx_slpi_publish(b)==EFI_INVALID_PARAMETER);
 init(b); assert(spx_slpi_publish(b)==EFI_NOT_READY);
 int n=fdt_path_offset(a,"/soc@0/remoteproc@2400000");
 fdt_setprop_u32(a,n,"microsoft,el2-preboot-complete",1);
 uint32_t trace[6]={1,2,3,4,5,6};
 fdt_setprop(a,n,"microsoft,el2-preboot-status",trace,sizeof(trace));
 assert(spx_slpi_publish(b)==EFI_SUCCESS);
 n=fdt_path_offset(b,"/soc@0/remoteproc@2400000");
 int len; const void *p=fdt_getprop(b,n,"microsoft,el2-preboot-status",&len);
 assert(len==24 && !memcmp(p,trace,24));
 fdt_setprop_string(b,n,"compatible","other,dsp");
 assert(spx_slpi_publish(b)==EFI_INVALID_PARAMETER);
}
"""
            (root/"test.c").write_text(harness)
            cc_cmd=["cc","-Wall","-Wextra","-Werror","-include","string.h","-fsanitize=address,undefined",
                    "-fno-pie","-no-pie",str(root/"test.c")]
            if Path("/usr/include/libfdt.h").is_file():
                cc_cmd+=["-lfdt"]
            else:
                fdt_so=ctypes.util.find_library("fdt")
                fdt_paths=[Path("/usr/lib/x86_64-linux-gnu/libfdt.so.1"),Path("/lib/x86_64-linux-gnu/libfdt.so.1")]
                lib_path=next((p for p in fdt_paths if p.is_file()),None)
                if lib_path is None and fdt_so:
                    lib_path=Path(fdt_so)
                if lib_path is None:
                    self.skipTest("libfdt headers and runtime library unavailable")
                (root/"libfdt.h").write_text(r"""
#pragma once
#include <stdint.h>
#include <string.h>
typedef uint32_t fdt32_t;
#define FDT_ERR_NOTFOUND 1
static inline uint32_t fdt32_to_cpu(fdt32_t x) { return __builtin_bswap32(x); }
static inline fdt32_t cpu_to_fdt32(uint32_t x) { return __builtin_bswap32(x); }
int fdt_create_empty_tree(void *buf, int bufsize);
int fdt_add_subnode(void *fdt, int parentoffset, const char *name);
int fdt_path_offset(const void *fdt, const char *path);
int fdt_node_check_compatible(const void *fdt, int nodeoffset, const char *compatible);
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
int fdt_setprop(void *fdt, int nodeoffset, const char *name, const void *val, int len);
int fdt_setprop_inplace(void *fdt, int nodeoffset, const char *name, const void *val, int len);
static inline int fdt_setprop_string(void *fdt, int nodeoffset, const char *name, const char *str) {
    return fdt_setprop(fdt, nodeoffset, name, str, (int)strlen(str) + 1);
}
static inline int fdt_setprop_u32(void *fdt, int nodeoffset, const char *name, uint32_t val) {
    fdt32_t tmp = cpu_to_fdt32(val);
    return fdt_setprop(fdt, nodeoffset, name, &tmp, sizeof(tmp));
}
static inline int fdt_setprop_inplace_u32(void *fdt, int nodeoffset, const char *name, uint32_t val) {
    fdt32_t tmp = cpu_to_fdt32(val);
    return fdt_setprop_inplace(fdt, nodeoffset, name, &tmp, sizeof(tmp));
}
""")
                cc_cmd+=["-I",str(root),str(lib_path)]
            subprocess.run(cc_cmd+["-o",str(root/"test")],check=True)
            subprocess.run([str(root/"test")],check=True)

    def test_subhal_multi_sensor_and_graceful_degradation(self):
        mock_subhal_h = r"""
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#define SUB_HAL_2_1_VERSION 1u
namespace android {
template <typename T>
class sp {
  T* ptr = nullptr;
 public:
  sp() = default;
  sp(T* p) : ptr(p) {}
  template <typename U>
  sp(const sp<U>& other) : ptr(other.get()) {}
  T* get() const { return ptr; }
  T* operator->() const { return ptr; }
  explicit operator bool() const { return ptr != nullptr; }
};
namespace hardware {
template <typename T>
class Return {
  T val{};
 public:
  Return() = default;
  Return(const T& v) : val(v) {}
  operator T() const { return val; }
  bool operator==(const T& o) const { return val == o; }
};
template <>
class Return<void> {};
inline Return<void> Void() { return {}; }
struct native_handle { int numFds; int data[1]; };
struct hidl_handle { const native_handle* getNativeHandle() const { return nullptr; } };
struct hidl_string { std::string s; const char* c_str() const { return s.c_str(); } };
template <typename T>
class hidl_vec : public std::vector<T> {
 public:
  using std::vector<T>::vector;
  hidl_vec(const std::vector<T>& v) : std::vector<T>(v) {}
};
namespace sensors {
namespace V1_0 {
enum class Result : int32_t { OK = 0, BAD_VALUE = -22, INVALID_OPERATION = -38 };
enum class OperationMode : int32_t { NORMAL = 0, DATA_INJECTION = 1 };
enum class SensorStatus : int8_t { UNRELIABLE = 0, ACCURACY_LOW = 1, ACCURACY_MEDIUM = 2, ACCURACY_HIGH = 3 };
enum class MetaDataEventType : uint32_t { META_DATA_FLUSH_COMPLETE = 1 };
struct SensorInfo { int32_t sensorHandle = 0; };
struct SharedMemInfo {};
enum class RateLevel : int32_t { STOP = 0 };
}
namespace V2_0::implementation {
class ScopedWakelock {
  bool locked = false;
 public:
  ScopedWakelock(void*, bool l) : locked(l) {}
  bool isLocked() const { return locked; }
};
}
namespace V2_1 {
enum class SensorType : int32_t { META_DATA = 0, ACCELEROMETER = 1, MAGNETIC_FIELD = 2, GYROSCOPE = 4, LIGHT = 5 };
struct SensorInfo {
  int32_t sensorHandle = 0;
  std::string name;
  std::string vendor;
  int32_t version = 0;
  SensorType type = SensorType::META_DATA;
  std::string typeAsString;
  float maxRange = 0;
  float resolution = 0;
  float power = 0;
  int32_t minDelay = 0;
  int32_t maxDelay = 0;
  uint32_t fifoReservedEventCount = 0;
  uint32_t fifoMaxEventCount = 0;
  uint32_t flags = 0;
};
struct Vec3 { float x, y, z; V1_0::SensorStatus status; };
struct MetaData { V1_0::MetaDataEventType what; };
struct Event {
  int64_t timestamp = 0;
  int32_t sensorHandle = 0;
  SensorType sensorType = SensorType::META_DATA;
  union { Vec3 vec3; float scalar; MetaData meta; } u{};
};
namespace implementation {
class IHalProxyCallback {
 public:
  using ScopedWakelock = V2_0::implementation::ScopedWakelock;
  virtual ~IHalProxyCallback() = default;
  virtual Return<void> onDynamicSensorsConnected(const hidl_vec<V1_0::SensorInfo>&) = 0;
  virtual Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<V2_1::SensorInfo>&) = 0;
  virtual Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>&) = 0;
  virtual void postEvents(const std::vector<V2_1::Event>&, ScopedWakelock) = 0;
  virtual ScopedWakelock createScopedWakelock(bool) = 0;
};
class ISensorsSubHal {
 public:
  using getSensorsList_2_1_cb = std::function<void(const hidl_vec<V2_1::SensorInfo>&)>;
  using registerDirectChannel_cb = std::function<void(V1_0::Result, int32_t)>;
  using configDirectReport_cb = std::function<void(V1_0::Result, int32_t)>;
  virtual ~ISensorsSubHal() = default;
  virtual Return<void> getSensorsList_2_1(getSensorsList_2_1_cb) = 0;
  virtual Return<V1_0::Result> initialize(const sp<IHalProxyCallback>&) = 0;
  virtual Return<V1_0::Result> setOperationMode(V1_0::OperationMode) = 0;
  virtual Return<V1_0::Result> activate(int32_t, bool) = 0;
  virtual Return<V1_0::Result> batch(int32_t, int64_t, int64_t) = 0;
  virtual Return<V1_0::Result> flush(int32_t) = 0;
  virtual Return<V1_0::Result> injectSensorData_2_1(const V2_1::Event&) = 0;
  virtual Return<void> registerDirectChannel(const V1_0::SharedMemInfo&, registerDirectChannel_cb) = 0;
  virtual Return<V1_0::Result> unregisterDirectChannel(int32_t) = 0;
  virtual Return<void> configDirectReport(int32_t, int32_t, V1_0::RateLevel, configDirectReport_cb) = 0;
  virtual Return<void> debug(const hidl_handle&, const hidl_vec<hidl_string>&) = 0;
  virtual const std::string getName() = 0;
};
}
}
}
}
}
"""
        subhal_source = (ROOT/"probe/sensors/spx-sensors-subhal.cpp").read_text()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root/"V2_1").mkdir()
            (root/"V2_1/SubHal.h").write_text(mock_subhal_h)
            flags = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-rtti",
                     "-fsanitize=address,undefined", "-I", str(root), "-I", str(ROOT/"probe/sensors")]
            # 1. Strict compile + link check of unmodified production subhal + ssc + lux + subhal_test.
            subprocess.run(flags + ["-shared", "-fPIC",
                                    str(ROOT/"probe/sensors/spx-sensors-subhal.cpp"),
                                    str(ROOT/"probe/sensors/spx-ssc.cpp"),
                                    str(ROOT/"probe/sensors/spx-lux.cpp"),
                                    "-o", str(root/"libsubhal.so")], check=True)
            subprocess.run(flags + [str(ROOT/"tests/spx_subhal_test.cpp"), "-ldl",
                                    "-o", str(root/"subhal-test")], check=True)

            # 2. Behavioral test of SurfaceSensors: 4-sensor discovery, graceful missing-SUID degradation,
            #    independent worker streaming, flush, and clean deactivation.
            sysfs_root = root/"sysfs"
            rproc = sysfs_root/"remoteproc/remoteproc0"
            rproc.mkdir(parents=True)
            (rproc/"name").write_text("slpi\n")
            (rproc/"state").write_text("attached\n")
            (sysfs_root/"iio").mkdir(parents=True)
            patched = (subhal_source
                       .replace('"/sys/class/remoteproc"', f'"{sysfs_root}/remoteproc"')
                       .replace('"/sys/bus/iio/devices"', f'"{sysfs_root}/iio"')
                       .replace('extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1',
                                'static ISensorsSubHal* unused_get_subhal'))
            (root/"subhal_under_test.hpp").write_text(patched)
            driver = r"""
#include "subhal_under_test.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <map>
#include <thread>

namespace mock_ssc {
std::mutex mu;
std::map<std::string, spx::SscProbeResult> probeResults;
std::map<std::string, bool> openOk;
std::map<std::string, std::array<float, 3>> sampleValues;
std::map<std::string, int> openCounts;
std::map<std::string, int> closeCounts;
std::map<std::string, int> quiesceCloseCounts;
std::map<std::string, int> disableCounts;
std::map<std::string, int> disableDelayMs;
std::map<std::string, uint32_t> lastBatchUs;
std::map<std::string, int> flushCounts;
std::map<std::string, int> samplesPerBatch;
std::atomic<bool> inDisable{false};
}

namespace spx {
SscSensor::SscSensor(std::string dataType) : type(std::move(dataType)) {}
bool SscSensor::isOpen() const { return fd >= 0; }
bool SscSensor::disable(int) {
  if (fd < 0) {
    streaming = false;
    return false;
  }
  int delay = 0;
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::disableCounts[type]++;
    delay = mock_ssc::disableDelayMs[type];
  }
  if (delay > 0) {
    mock_ssc::inDisable.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    mock_ssc::inDisable.store(false);
  }
  streaming = false;
  return true;
}
void SscSensor::close(bool quiesce) {
  if (fd >= 0) {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::closeCounts[type]++;
    if (quiesce && streaming) mock_ssc::quiesceCloseCounts[type]++;
    fd = -1;
    streaming = false;
  }
}
SscProbeResult SscSensor::probe(int) {
  std::lock_guard lk(mock_ssc::mu);
  auto it = mock_ssc::probeResults.find(type);
  return it == mock_ssc::probeResults.end() ? SscProbeResult::Unavailable : it->second;
}
bool SscSensor::open(int) {
  std::lock_guard lk(mock_ssc::mu);
  auto it = mock_ssc::openOk.find(type);
  bool ok = it != mock_ssc::openOk.end() && it->second;
  if (ok) {
    fd = 1;
    streaming = false;
    mock_ssc::openCounts[type]++;
  }
  return ok;
}
bool SscSensor::configure(float hz, uint32_t batchPeriodUs) {
  if (fd < 0 || hz <= 0.f) return false;
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::lastBatchUs[type] = batchPeriodUs;
  }
  streaming = true;
  return true;
}
bool SscSensor::rate(float hz) { return configure(hz, 0); }
bool SscSensor::flush() {
  if (fd < 0 || !streaming) return false;
  std::lock_guard lk(mock_ssc::mu);
  mock_ssc::flushCounts[type]++;
  return true;
}
int SscSensor::read(SscSample& sample, int) {
  if (fd < 0) return -1;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  std::lock_guard lk(mock_ssc::mu);
  sample.xyz = mock_ssc::sampleValues[type];
  sample.accuracy = 3;
  sample.timestamp = bootTime();
  return 1;
}
int SscSensor::readBatch(std::vector<SscSample>& out, int, int) {
  out.clear();
  if (fd < 0) return -1;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  std::lock_guard lk(mock_ssc::mu);
  int n = mock_ssc::samplesPerBatch[type];
  if (n <= 0) n = 1;
  int64_t baseTs = bootTime();
  for (int i = 0; i < n; i++) {
    SscSample sample{};
    sample.xyz = mock_ssc::sampleValues[type];
    sample.accuracy = 3;
    sample.timestamp = baseTs + int64_t(i) * 40000000LL;
    out.push_back(sample);
  }
  return int(out.size());
}
}

class TestCallback final : public IHalProxyCallback {
 public:
  std::mutex mu;
  std::map<int, int> counts;
  std::map<int, int> postCalls;
  std::map<int, int> flushes;
  std::map<int, v2::Event> last;
  Return<void> onDynamicSensorsConnected(const hidl_vec<v1::SensorInfo>&) override { return Void(); }
  Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<v2::SensorInfo>&) override { return Void(); }
  Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>&) override { return Void(); }
  void postEvents(const std::vector<v2::Event>& events, android::hardware::sensors::V2_0::implementation::ScopedWakelock w) override {
    assert(!w.isLocked());
    std::lock_guard lk(mu);
    if (!events.empty()) postCalls[events.front().sensorHandle]++;
    for (const auto& e : events) {
      if (e.sensorType == v2::SensorType::META_DATA) flushes[e.sensorHandle]++;
      else { counts[e.sensorHandle]++; last[e.sensorHandle] = e; }
    }
  }
  android::hardware::sensors::V2_0::implementation::ScopedWakelock createScopedWakelock(bool locked) override {
    assert(!locked);
    return android::hardware::sensors::V2_0::implementation::ScopedWakelock(nullptr, false);
  }
};

static std::vector<v2::SensorInfo> listSensors(SurfaceSensors& hal) {
  std::vector<v2::SensorInfo> out;
  hal.getSensorsList_2_1([&](const hidl_vec<v2::SensorInfo>& list) {
    out.assign(list.begin(), list.end());
  });
  return out;
}

int main() {
  (void)unused_get_subhal;
  // Case 1: SLPI attached, accel + gyro + mag all found and streaming independently.
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::probeResults = {{"gyro", spx::SscProbeResult::Found}, {"mag", spx::SscProbeResult::Found}};
    mock_ssc::openOk = {{"accel", true}, {"gyro", true}, {"mag", true}};
    mock_ssc::sampleValues = {{"accel", {0.f, 0.f, 9.81f}}, {"gyro", {0.1f, -0.2f, 0.3f}}, {"mag", {20.f, -10.f, 45.f}}};
    mock_ssc::openCounts.clear();
    mock_ssc::closeCounts.clear();
    mock_ssc::quiesceCloseCounts.clear();
    mock_ssc::disableCounts.clear();
    mock_ssc::disableDelayMs = {{"accel", 80}};
  }
  {
    SurfaceSensors hal;
    auto list = listSensors(hal);
    assert(list.size() == 3);
    assert(list[0].sensorHandle == 1 && list[0].type == v2::SensorType::ACCELEROMETER);
    assert(list[0].fifoReservedEventCount == 64 && list[0].fifoMaxEventCount == 256);
    assert(list[1].sensorHandle == 3 && list[1].type == v2::SensorType::GYROSCOPE);
    assert(list[1].fifoReservedEventCount == 64 && list[1].fifoMaxEventCount == 256);
    assert(list[2].sensorHandle == 4 && list[2].type == v2::SensorType::MAGNETIC_FIELD);
    assert(list[2].fifoReservedEventCount == 64 && list[2].fifoMaxEventCount == 256);
    // Pure helper checks for SLPI batching and ALS steady-light backoff.
    assert(computeSscBatchUs(20000000, 0, 26.f) == 0);
    assert(computeSscBatchUs(100000000, 0, 26.f) == 95000);
    assert(computeSscBatchUs(66667000, 100000000, 26.f) == 100000);
    assert(computeSscBatchUs(200000000, 2000000000, 26.f) == 2000000);
    assert(computeLightPollDelayNs(250000000, 0) == 500000000);
    assert(computeLightPollDelayNs(250000000, 3) == 1000000000);
    assert(computeLightPollDelayNs(250000000, 6) == 2000000000);
    assert(isLightSteady(100.f, 104.f) && !isLightSteady(100.f, 120.f));

    TestCallback cb;
    assert(hal.initialize(&cb) == v1::Result::OK);
    for (int h : {1, 3, 4}) {
      assert(hal.batch(h, 20000000, 0) == v1::Result::OK);
      assert(hal.activate(h, true) == v1::Result::OK);
    }
    assert(hal.activate(2, true) == v1::Result::BAD_VALUE);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    for (int h : {1, 3, 4}) assert(hal.flush(h) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    {
      std::lock_guard lk(cb.mu);
      assert(cb.counts[1] > 0 && cb.flushes[1] == 1 && cb.last[1].sensorType == v2::SensorType::ACCELEROMETER);
      assert(cb.counts[3] > 0 && cb.flushes[3] == 1 && cb.last[3].sensorType == v2::SensorType::GYROSCOPE);
      assert(cb.counts[4] > 0 && cb.flushes[4] == 1 && cb.last[4].sensorType == v2::SensorType::MAGNETIC_FIELD);
      assert(std::fabs(cb.last[3].u.vec3.z - 0.3f) < 1e-5f);
      assert(std::fabs(cb.last[4].u.vec3.x - 20.f) < 1e-5f);
    }

    // Deactivate accel while slow disable() (80ms) runs: concurrent batch/activate on gyro MUST NOT block!
    assert(hal.activate(1, false) == v1::Result::OK);
    for (int i = 0; i < 40 && !mock_ssc::inDisable.load(); i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(mock_ssc::inDisable.load());
    auto t0 = std::chrono::steady_clock::now();
    assert(hal.batch(3, 40000000, 0) == v1::Result::OK);
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    assert(elapsedMs < 30);

    for (int h : {3, 4}) assert(hal.activate(h, false) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    {
      std::lock_guard lk(mock_ssc::mu);
      assert(mock_ssc::disableCounts["accel"] == 1);
      assert(mock_ssc::disableCounts["gyro"] == 1);
      assert(mock_ssc::disableCounts["mag"] == 1);
      assert(mock_ssc::closeCounts["accel"] == 0);
      assert(mock_ssc::openCounts["accel"] == 1);
      mock_ssc::disableDelayMs.clear();
    }

    // Re-activating accel reuses the already-open QRTR connection without reopening/closing.
    assert(hal.activate(1, true) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(hal.activate(1, false) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    {
      std::lock_guard lk(mock_ssc::mu);
      assert(mock_ssc::openCounts["accel"] == 1);
      assert(mock_ssc::disableCounts["accel"] == 2);
      assert(mock_ssc::closeCounts["accel"] == 0);
    }
  }
  {
    std::lock_guard lk(mock_ssc::mu);
    assert(mock_ssc::closeCounts["accel"] == 1);
    assert(mock_ssc::closeCounts["gyro"] == 1);
    assert(mock_ssc::closeCounts["mag"] == 1);
  }

  // Case 2: Graceful degradation when mag SUID is Missing; accel + gyro still advertised and working.
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::probeResults = {{"gyro", spx::SscProbeResult::Found}, {"mag", spx::SscProbeResult::Missing}};
  }
  {
    SurfaceSensors hal;
    auto list = listSensors(hal);
    assert(list.size() == 2);
    assert(list[0].sensorHandle == 1 && list[1].sensorHandle == 3);
    assert(hal.activate(4, true) == v1::Result::BAD_VALUE);
  }

  // Case 3: SSC still initializing at boot (Unavailable) + mag fails open() at runtime; accel still streams.
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::probeResults = {{"gyro", spx::SscProbeResult::Unavailable}, {"mag", spx::SscProbeResult::Unavailable}};
    mock_ssc::openOk = {{"accel", true}, {"gyro", true}, {"mag", false}};
  }
  {
    SurfaceSensors hal;
    auto list = listSensors(hal);
    assert(list.size() == 3);
    TestCallback cb;
    assert(hal.initialize(&cb) == v1::Result::OK);
    assert(hal.activate(1, true) == v1::Result::OK);
    assert(hal.activate(4, true) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    {
      std::lock_guard lk(cb.mu);
      assert(cb.counts[1] > 0 && cb.counts[4] == 0);
    }
    assert(hal.activate(1, false) == v1::Result::OK);
    assert(hal.activate(4, false) == v1::Result::OK);
  }

  // Case 4: SLPI crashes mid-run -> worker closes without quiesce send and stops calling open()/rate().
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::probeResults = {{"gyro", spx::SscProbeResult::Missing}, {"mag", spx::SscProbeResult::Missing}};
    mock_ssc::openOk = {{"accel", true}};
    mock_ssc::openCounts.clear();
    mock_ssc::closeCounts.clear();
    mock_ssc::quiesceCloseCounts.clear();
    mock_ssc::disableCounts.clear();
  }
  {
    SurfaceSensors hal;
    TestCallback cb;
    assert(hal.initialize(&cb) == v1::Result::OK);
    assert(hal.activate(1, true) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
      std::ofstream st(""" + json.dumps(str(rproc/"state")) + r""");
      st << "crashed\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int opensAtCrash = 0;
    {
      std::lock_guard lk(mock_ssc::mu);
      opensAtCrash = mock_ssc::openCounts["accel"];
      assert(opensAtCrash == 1);
      assert(mock_ssc::closeCounts["accel"] == 1);
      assert(mock_ssc::quiesceCloseCounts["accel"] == 0);
    }
    assert(hal.activate(1, false) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(hal.activate(1, true) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    {
      std::lock_guard lk(mock_ssc::mu);
      assert(mock_ssc::openCounts["accel"] == opensAtCrash);
      assert(mock_ssc::disableCounts["accel"] == 0);
    }
  }
  // Case 5: Non-zero batch latency propagates to SLPI configure(), batches multiple samples in one postEvents(), and flush() invokes SLPI flush.
  {
    std::ofstream st(""" + json.dumps(str(rproc/"state")) + r""");
    st << "attached\n";
  }
  {
    std::lock_guard lk(mock_ssc::mu);
    mock_ssc::probeResults = {{"gyro", spx::SscProbeResult::Missing}, {"mag", spx::SscProbeResult::Found}};
    mock_ssc::openOk = {{"accel", true}, {"mag", true}};
    mock_ssc::lastBatchUs.clear();
    mock_ssc::flushCounts.clear();
    mock_ssc::samplesPerBatch = {{"accel", 4}};
  }
  {
    SurfaceSensors hal;
    TestCallback cb;
    assert(hal.initialize(&cb) == v1::Result::OK);
    assert(hal.batch(1, 40000000, 2000000000) == v1::Result::OK);
    assert(hal.activate(1, true) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    assert(hal.flush(1) == v1::Result::OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    {
      std::lock_guard lk(mock_ssc::mu);
      assert(mock_ssc::lastBatchUs["accel"] == 2000000);
      assert(mock_ssc::flushCounts["accel"] >= 1);
      mock_ssc::samplesPerBatch.clear();
    }
    {
      std::lock_guard lk(cb.mu);
      assert(cb.counts[1] >= 4);
      assert(cb.counts[1] > cb.postCalls[1]);
      assert(cb.flushes[1] == 1);
    }
    assert(hal.activate(1, false) == v1::Result::OK);
  }
  return 0;
}
"""
            (root/"driver.cpp").write_text(driver)
            subprocess.run(flags + [str(root/"driver.cpp"), str(ROOT/"probe/sensors/spx-lux.cpp"),
                                    "-pthread", "-o", str(root/"driver")], check=True)
            subprocess.run([str(root/"driver")], check=True)


if __name__=="__main__": unittest.main()
