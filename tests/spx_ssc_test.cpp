#include "../probe/sensors/spx-ssc.cpp"
#include <cassert>
#include <cmath>
#include <limits>

namespace {
using namespace spx;

uint32_t floatBits(float v) {
  uint32_t bits = 0;
  std::memcpy(&bits, &v, 4);
  return bits;
}

Bytes makeUid(uint64_t low, uint64_t high) {
  Bytes id{9};
  le(id, low, 8);
  id.push_back(17);
  le(id, high, 8);
  return id;
}

Bytes wrapQmiIndication(const Bytes& originUid, const std::vector<Bytes>& rawEvents, uint16_t msgId = 0x22) {
  Bytes body = blob(1, originUid);
  for (const auto& ev : rawEvents) append(body, blob(2, ev));
  Bytes packet{4, 1, 0};
  le(packet, msgId, 2);
  le(packet, body.size() + 5, 2);
  packet.push_back(2);
  le(packet, body.size() + 2, 2);
  le(packet, body.size(), 2);
  append(packet, body);
  return packet;
}

Bytes makeSuidEvent(const std::string& dataType, bool includeUid, uint64_t low = 0, uint64_t high = 0) {
  Bytes suidPayload = blob(1, Bytes(dataType.begin(), dataType.end()));
  if (includeUid) append(suidPayload, blob(2, makeUid(low, high)));
  Bytes ev{13};
  le(ev, 768, 4);
  append(ev, blob(3, suidPayload));
  return ev;
}

Bytes makeSampleEvent(std::array<float, 3> xyz, uint64_t ticks, uint64_t accuracy, bool packed, bool includeAccuracy = true) {
  Bytes sample;
  if (packed) {
    Bytes packedFloats;
    for (float f : xyz) le(packedFloats, floatBits(f), 4);
    sample = blob(1, packedFloats);
  } else {
    for (float f : xyz) {
      sample.push_back(13);
      le(sample, floatBits(f), 4);
    }
  }
  if (includeAccuracy) {
    sample.push_back(16);
    var(sample, accuracy);
  }
  Bytes ev{13};
  le(ev, 1025, 4);
  ev.push_back(17);
  le(ev, ticks, 8);
  append(ev, blob(3, sample));
  return ev;
}
}  // namespace

int main() {
  // 1. Protobuf parser baseline & malformed input rejection.
  std::vector<Field> fields;
  assert(parse({9, 1, 2, 3, 4, 5, 6, 7, 8, 0x10, 3}, fields));
  assert(fields.size() == 2 && fields[0].value == 0x0807060504030201ULL && fields[1].value == 3);
  for (Bytes b : std::vector<Bytes>{
           {9, 1}, {0}, {0x0a, 5, 1}, {0x0f}, {8, 128},
           {8, 255, 255, 255, 255, 255, 255, 255, 255, 255, 2}})
    assert(!parse(b, fields));

  // 2. Targeted SUID queries for accel/gyro/mag; empty data_type MUST be rejected (no full enum).
  Bytes query;
  assert(!buildSuidQuery("", query));
  for (const char* name : {"accel", "gyro", "mag"}) {
    assert(buildSuidQuery(name, query));
    assert(parse(query, fields));
    auto dt = find(fields, 1, 2);
    auto regUpd = find(fields, 2, 0);
    auto defOnly = find(fields, 3, 0);
    assert(dt && dt->bytes == Bytes(name, name + std::strlen(name)));
    assert(regUpd && regUpd->value == 0);
    assert(defOnly && defOnly->value == 1);
  }

  // 3. SUID response parsing & routing across accel, gyro, mag, and missing-SUID handling.
  const Bytes suidSvc = suidServiceUid();
  const Bytes accelUid = makeUid(0x1111222233334444ULL, 0x5555666677778888ULL);
  const Bytes gyroUid = makeUid(0xaaaa1111bbbb2222ULL, 0xcccc3333dddd4444ULL);
  const Bytes magUid = makeUid(0x123456789abcdef0ULL, 0x0fedcba987654321ULL);

  Bytes resolved;
  Bytes pktAccelSuid = wrapQmiIndication(suidSvc, {makeSuidEvent("accel", true, 0x1111222233334444ULL, 0x5555666677778888ULL)});
  assert(parseSuidResponse(pktAccelSuid, suidSvc, "accel", resolved) == SscProbeResult::Found && resolved == accelUid);
  assert(parseSuidResponse(pktAccelSuid, suidSvc, "gyro", resolved) == SscProbeResult::Unavailable);

  Bytes pktGyroSuid = wrapQmiIndication(suidSvc, {makeSuidEvent("gyro", true, 0xaaaa1111bbbb2222ULL, 0xcccc3333dddd4444ULL)}, 0x21);
  assert(parseSuidResponse(pktGyroSuid, suidSvc, "gyro", resolved) == SscProbeResult::Found && resolved == gyroUid);

  Bytes pktMagSuid = wrapQmiIndication(suidSvc, {makeSuidEvent("mag", true, 0x123456789abcdef0ULL, 0x0fedcba987654321ULL)});
  assert(parseSuidResponse(pktMagSuid, suidSvc, "mag", resolved) == SscProbeResult::Found && resolved == magUid);

  // Missing SUID (SLPI responds for "mag" with zero SUID entries).
  Bytes pktMagMissing = wrapQmiIndication(suidSvc, {makeSuidEvent("mag", false)});
  assert(parseSuidResponse(pktMagMissing, suidSvc, "mag", resolved) == SscProbeResult::Missing);
  assert(parseSuidResponse(pktMagMissing, suidSvc, "gyro", resolved) == SscProbeResult::Unavailable);

  // Wrong origin UID is ignored.
  Bytes pktWrongOrigin = wrapQmiIndication(accelUid, {makeSuidEvent("accel", true, 1, 2)});
  assert(parseSuidResponse(pktWrongOrigin, suidSvc, "accel", resolved) == SscProbeResult::Unavailable);

  // 4. Rate config payload encoding.
  Bytes ratePayload;
  assert(buildRatePayload(26.0f, ratePayload) && ratePayload.size() == 5 && ratePayload[0] == 13);
  assert(buildRatePayload(52.0f, ratePayload));
  assert(buildRatePayload(10.0f, ratePayload));
  assert(!buildRatePayload(0.0f, ratePayload));
  assert(!buildRatePayload(-5.0f, ratePayload));
  assert(!buildRatePayload(std::numeric_limits<float>::quiet_NaN(), ratePayload));
  assert(!buildRatePayload(std::numeric_limits<float>::infinity(), ratePayload));

  // 5. Accel / Gyro / Mag sample decoding (unpacked + packed), SUID isolation, and timestamp math.
  std::vector<SscSample> samples;
  int64_t offset = 0;
  bool timeSet = false;

  // Unpacked accel event (m/s^2).
  Bytes accelPkt = wrapQmiIndication(accelUid, {makeSampleEvent({0.1f, -0.2f, 9.81f}, 19200000ULL, 3, false)});
  assert(parseSamplePacket(accelPkt, accelUid, samples, offset, timeSet, 5000000000LL));
  assert(samples.size() == 1 && timeSet && samples[0].accuracy == 3 && samples[0].timestamp == 5000000000LL);
  assert(std::fabs(samples[0].xyz[0] - 0.1f) < 1e-6f && std::fabs(samples[0].xyz[2] - 9.81f) < 1e-6f);
  // Cross-SUID rejection: gyro and mag MUST reject accel's packet.
  assert(!parseSamplePacket(accelPkt, gyroUid, samples, offset, timeSet, 5000000000LL));
  assert(!parseSamplePacket(accelPkt, magUid, samples, offset, timeSet, 5000000000LL));

  // Packed gyro event (rad/s) with two batched events (+20ms = +384000 ticks at 19.2 MHz).
  samples.clear();
  offset = 0;
  timeSet = false;
  Bytes gyroPkt = wrapQmiIndication(gyroUid, {
      makeSampleEvent({0.015f, -0.025f, 0.5f}, 19200000ULL, 2, true),
      makeSampleEvent({0.020f, -0.010f, 0.4f}, 19200000ULL + 384000ULL, 3, true),
  });
  assert(parseSamplePacket(gyroPkt, gyroUid, samples, offset, timeSet, 9000000000LL));
  assert(samples.size() == 2);
  assert(std::fabs(samples[0].xyz[2] - 0.5f) < 1e-6f && samples[0].accuracy == 2 && samples[0].timestamp == 9000000000LL);
  assert(std::fabs(samples[1].xyz[0] - 0.020f) < 1e-6f && samples[1].accuracy == 3 && samples[1].timestamp == 9020000000LL);

  // Packed magnetometer event (uT).
  samples.clear();
  offset = 0;
  timeSet = false;
  Bytes magPkt = wrapQmiIndication(magUid, {makeSampleEvent({22.5f, -14.25f, 41.0f}, 38400000ULL, 1, true)});
  assert(parseSamplePacket(magPkt, magUid, samples, offset, timeSet, 12000000000LL));
  assert(samples.size() == 1 && samples[0].accuracy == 1);
  assert(std::fabs(samples[0].xyz[0] - 22.5f) < 1e-6f && std::fabs(samples[0].xyz[1] + 14.25f) < 1e-6f &&
         std::fabs(samples[0].xyz[2] - 41.0f) < 1e-6f);

  // 6. Malformed sample rejections (NaN, bad accuracy, missing accuracy, timestamp overflow, truncation).
  samples.clear();
  Bytes nanPkt = wrapQmiIndication(gyroUid, {makeSampleEvent({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}, 19200000ULL, 3, true)});
  assert(!parseSamplePacket(nanPkt, gyroUid, samples, offset, timeSet, 0));
  Bytes badAccPkt = wrapQmiIndication(magUid, {makeSampleEvent({1.f, 2.f, 3.f}, 19200000ULL, 4, true)});
  assert(!parseSamplePacket(badAccPkt, magUid, samples, offset, timeSet, 0));
  Bytes noAccPkt = wrapQmiIndication(magUid, {makeSampleEvent({1.f, 2.f, 3.f}, 19200000ULL, 0, true, false)});
  assert(!parseSamplePacket(noAccPkt, magUid, samples, offset, timeSet, 0));
  Bytes hugeTsPkt = wrapQmiIndication(accelUid, {makeSampleEvent({1.f, 2.f, 3.f}, UINT64_MAX, 3, false)});
  assert(!parseSamplePacket(hugeTsPkt, accelUid, samples, offset, timeSet, 0));

  Bytes truncPkt = accelPkt;
  truncPkt.pop_back();
  Bytes actual;
  std::vector<Field> ev;
  assert(!events(truncPkt, actual, ev));

  // 7. SscAccel compatibility and SscSensor dataType / empty-type guard.
  SscAccel compatAccel;
  Acceleration compatSample{};
  assert(compatAccel.dataType() == "accel");
  assert(compatAccel.read(compatSample, 0) == -1);
  SscSensor gyro("gyro");
  SscSensor mag("mag");
  SscSensor empty("");
  assert(gyro.dataType() == "gyro" && mag.dataType() == "mag");
  assert(!empty.open(10) && empty.probe(10) == SscProbeResult::Unavailable);
  return 0;
}
