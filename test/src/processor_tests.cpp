// Processor-level tests
//
// The full TONE3000Processor (headless build) driven the way a host drives
// it: prepareToPlay + processBlock. These pin the plugin's host-facing
// contracts that no unit-level DSP test can see:
//
//   - a 48 kHz host with an empty chain is transparent with zero latency,
//   - at other host rates the boundary resampler's *reported* latency (PDC)
//     matches its *measured* group delay,
//   - toggling oversampling never changes reported latency (no PDC churn),
//   - without a stereo output the spread parameter is inert (plain mono out)
//     and getChainState reports the capability to the UI,
//   - without a stereo output stereo chains keep running and are summed to
//     mono, ½(L+R) with solo/polarity live inside the sum,
//   - parameter state survives a save/restore round trip,
//   - garbage, legacy-format, and newer-schema state blobs are ignored,
//   - the tail report covers the DC blocker floor.
//
// Model tests use embedded local fixtures, so nothing touches the network.
// Runs on the message thread (ScopedJuceInitialiser_GUI in main).
// Oversampling parameter changes are applied via a re-prepare, exactly like
// a host would (the live-toggle path is an AsyncUpdater that needs a running
// message pump; prepareToPlay resolves the same parameters synchronously).
#include "Processor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "test_helpers.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace {

// Drives the processor like a host: identical audio into both channels in
// fixed-size blocks; a trailing partial block (in.size() not a multiple of
// blockSize) is driven with exactly its remaining samples. Returns channel 0
// of the output.
std::vector<float> processThrough(TONE3000Processor& proc, const std::vector<float>& in,
                                  int blockSize) {
  const int total = static_cast<int>(in.size());
  std::vector<float> out(total, 0.0f);
  juce::MidiBuffer midi;
  for (int off = 0; off < total; off += blockSize) {
    // Clamp the final partial block: in.size() need not be a multiple of
    // blockSize, and the old flat blockSize copy read past `in` and wrote
    // past `out` on that trailing chunk (heap corruption detected on a
    // later free). Drive exactly the remaining samples.
    const int n = std::min(blockSize, total - off);
    juce::AudioBuffer<float> buffer(2, n);
    buffer.copyFrom(0, 0, in.data() + off, n);
    buffer.copyFrom(1, 0, in.data() + off, n);
    proc.processBlock(buffer, midi);
    std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + n,
              out.begin() + off);
  }
  return out;
}

// Round-trip gain at `freq` between the settled tails of `in` and `out`.
double settledGainDb(const std::vector<float>& out, const std::vector<float>& in, double freq,
                     double fs, int start = 16384, int window = 16384) {
  const double gOut = goertzelPower(out.data() + start, static_cast<size_t>(window), freq, fs);
  const double gIn = goertzelPower(in.data() + start, static_cast<size_t>(window), freq, fs);
  return db(gOut) - db(gIn);
}

TEST(ProcessorTest, EmptyChainAt48kIsTransparentWithZeroLatency) {
  // The core promise of the chain-domain design: at a 48 kHz host with
  // oversampling off, both resampling layers are dropped: no latency, and
  // the only thing between input and output is the (5 Hz) DC blocker.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  EXPECT_EQ(proc.getLatencySamples(), 0);

  const int total = 93 * 512;
  const auto in = makeSine(total, 997.0, 0.5f);
  const auto out = processThrough(proc, in, 512);

  EXPECT_NEAR(settledGainDb(out, in, 997.0, kFs), 0.0, 0.05);
  EXPECT_EQ(bestCorrelationLag(out, in, 16384, 4096, 32), 0) << "48k path must add no delay";
}

TEST(ProcessorTest, SpreadStaysIdleWithoutAStereoOutput) {
  // A rig that can't reproduce two distinct channels (mono host track, or a
  // standalone one-channel output device that still hands us a stereo
  // buffer but plays only channel 0) must never run the spread double: the
  // output stays the plain mono chain even when a preset arrives with
  // spreadEnabled on. The parameter keeps its value; the UI greys the group
  // out via the stereoOutput capability flag. Emulated with a mono main
  // output bus, which drives the same detection.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 1, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  EXPECT_FALSE(static_cast<bool>(proc.getChainState(-1)["stereoOutput"]));

  proc.parameters.getParameter("spreadEnabled")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("spreadOffset")->setValueNotifyingHost(1.0f);  // +24 ms lag

  const int total = 93 * 512;
  const auto in = makeNoise(total, 4242, 0.25f);
  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer midi;
  for (int off = 0; off < total; off += 512) {
    buffer.copyFrom(0, 0, in.data() + off, 512);
    buffer.copyFrom(1, 0, in.data() + off, 512);
    proc.processBlock(buffer, midi);
    for (int i = 0; i < 512; ++i)
      ASSERT_EQ(buffer.getReadPointer(0)[i], buffer.getReadPointer(1)[i])
          << "spread ran on a mono rig at sample " << off + i;
  }

  // A stereo bus reports the capability back.
  TONE3000Processor stereoProc;
  stereoProc.setPlayConfigDetails(2, 2, kFs, 512);
  stereoProc.prepareToPlay(kFs, 512);
  EXPECT_TRUE(static_cast<bool>(stereoProc.getChainState(-1)["stereoOutput"]));
}

TEST(ProcessorTest, StereoChainsFoldToMonoWithoutAStereoOutput) {
  // Stereo chains on a rig that can't reproduce stereo (a mono host track
  // here) keep running and are summed at the output: ½(balL·L + balR·R),
  // the same fold a host applies when it sums a stereo bus to mono, so a
  // rig keeps its level when moved between track types. With two identical
  // (empty) lanes the sum is transparent; polarity and solo keep acting
  // inside it, which also pins that the Right lane really processes
  // (before this, a mono track silently played the Left lane alone).
  TONE3000Processor proc;
  proc.setPlayConfigDetails(1, 1, kFs, 512);
  proc.setStereoMode(true);
  proc.prepareToPlay(kFs, 512);
  EXPECT_FALSE(static_cast<bool>(proc.getChainState(-1)["stereoOutput"]));

  const int total = 93 * 512;
  const auto in = makeSine(total, 997.0, 0.5f);
  juce::MidiBuffer midi;
  const auto run = [&] {
    std::vector<float> out(in.size(), 0.0f);
    juce::AudioBuffer<float> buffer(1, 512);
    for (int off = 0; off < total; off += 512) {
      buffer.copyFrom(0, 0, in.data() + off, 512);
      proc.processBlock(buffer, midi);
      std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + 512, out.begin() + off);
    }
    return out;
  };

  // Identical lanes at centered balance: ½(in + in) = in, transparent.
  EXPECT_NEAR(settledGainDb(run(), in, 997.0, kFs), 0.0, 0.05);

  // Flipping one lane's polarity cancels the sum: lane R is really in there.
  proc.parameters.getParameter("chainInvertRight")->setValueNotifyingHost(1.0f);
  {
    const auto out = run();
    float peak = 0.0f;
    for (int i = 16384; i < total; ++i)
      peak = std::max(peak, std::abs(out[i]));
    EXPECT_LT(peak, 1.0e-4f) << "inverted lane failed to cancel: the fold is broken";
  }

  // Solo auditions one lane, at the fold's ½ share.
  proc.parameters.getParameter("chainInvertRight")->setValueNotifyingHost(0.0f);
  proc.parameters.getParameter("chainSoloLeft")->setValueNotifyingHost(1.0f);
  EXPECT_NEAR(settledGainDb(run(), in, 997.0, kFs), -6.02, 0.1);

  // The same fold covers a stereo buffer on a mono rig (standalone
  // one-channel output device: the buffer is stereo but only channel 0 is
  // audible). Both channels carry the sum, so the listener hears the whole
  // rig; the polarity null proves the fold ran here too.
  TONE3000Processor monoOutProc;
  monoOutProc.setPlayConfigDetails(2, 1, kFs, 512);
  monoOutProc.setStereoMode(true);
  monoOutProc.prepareToPlay(kFs, 512);
  monoOutProc.parameters.getParameter("chainInvertRight")->setValueNotifyingHost(1.0f);
  juce::AudioBuffer<float> buffer(2, 512);
  float peak = 0.0f;
  for (int off = 0; off < total; off += 512) {
    buffer.copyFrom(0, 0, in.data() + off, 512);
    buffer.copyFrom(1, 0, in.data() + off, 512);
    monoOutProc.processBlock(buffer, midi);
    if (off >= 16384)
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
          peak = std::max(peak, std::abs(buffer.getReadPointer(ch)[i]));
  }
  EXPECT_LT(peak, 1.0e-4f) << "a 2-channel buffer on a mono rig must fold both channels";
}

TEST(ProcessorTest, BoundaryReportedLatencyMatchesMeasuredDelay) {
  // At non-48k host rates the Lanczos boundary engages (even with an empty
  // chain: that's the PDC-stability invariant) and reports its latency to
  // the host. The report is only worth anything if it matches the physical
  // group delay.
  for (double hostRate : {44100.0, 96000.0}) {
    TONE3000Processor proc;
    proc.setPlayConfigDetails(2, 2, hostRate, 512);
    proc.prepareToPlay(hostRate, 512);
    const int reported = proc.getLatencySamples();
    EXPECT_GT(reported, 0) << hostRate << " Hz host must engage the boundary";

    const int total = 120 * 512;
    const auto noise = makeNoise(total, 777, 0.25f);
    const auto out = processThrough(proc, noise, 512);
    const int measured = bestCorrelationLag(out, noise, 16384, 8192, reported + 64);
    EXPECT_NEAR(measured, reported, 2) << hostRate << " Hz: PDC report drifted from reality";

    // And the boundary itself must be sonically transparent.
    TONE3000Processor proc2;
    proc2.setPlayConfigDetails(2, 2, hostRate, 512);
    proc2.prepareToPlay(hostRate, 512);
    const auto sine = makeSine(total, 997.0, 0.5f, hostRate);
    const auto sineOut = processThrough(proc2, sine, 512);
    EXPECT_NEAR(settledGainDb(sineOut, sine, 997.0, hostRate), 0.0, 0.1)
        << hostRate << " Hz: boundary not transparent";
  }
}

TEST(ProcessorTest, OversamplingTogglesWithoutPdcChange) {
  // The oversampler is minimum-phase precisely so enabling it never changes
  // reported latency; hosts re-compensate on PDC changes (an audible
  // hiccup). Verified at the worst case: 44.1k host, ×8.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 44100.0, 512);
  proc.prepareToPlay(44100.0, 512);
  const int latencyBefore = proc.getLatencySamples();

  proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // index 2 = 8x
  proc.prepareToPlay(44100.0, 512);  // hosts re-prepare freely; picks up the factor

  EXPECT_EQ(proc.getLatencySamples(), latencyBefore) << "enabling 8x moved reported latency";

  // Still transparent through boundary + oversampler, and the physical delay
  // may only grow by the oversampler's few samples of min-phase group delay.
  const int total = 120 * 512;
  const auto sine = makeSine(total, 997.0, 0.5f, 44100.0);
  const auto out = processThrough(proc, sine, 512);
  EXPECT_NEAR(settledGainDb(out, sine, 997.0, 44100.0), 0.0, 0.15);

  const auto noise = makeNoise(total, 888, 0.25f);
  TONE3000Processor procN;
  procN.setPlayConfigDetails(2, 2, 44100.0, 512);
  procN.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  procN.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);
  procN.prepareToPlay(44100.0, 512);
  const auto noiseOut = processThrough(procN, noise, 512);
  const int measured = bestCorrelationLag(noiseOut, noise, 16384, 8192, latencyBefore + 64);
  EXPECT_LE(measured, latencyBefore + 10) << "8x added more than min-phase group delay";
  EXPECT_GE(measured, latencyBefore - 2);
}

TEST(ProcessorTest, ParameterStateSurvivesSaveRestore) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    a.parameters.getParameter("inputLevel")->setValueNotifyingHost(0.7f);
    a.parameters.getParameter("gateEnabled")->setValueNotifyingHost(0.0f);
    a.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    a.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // 8x
    a.getStateInformation(state);
  }

  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_NEAR(b.parameters.getRawParameterValue("inputLevel")->load(), 0.7f, 1e-5f);
  EXPECT_EQ(b.parameters.getRawParameterValue("gateEnabled")->load(), 0.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osEnabled")->load(), 1.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osFactor")->load(), 2.0f);  // choice index

  // The restored oversampling setting must actually take effect on the next
  // prepare; factor 8 leaves reported latency at zero for a 48k host.
  b.setPlayConfigDetails(2, 2, kFs, 512);
  b.prepareToPlay(kFs, 512);
  EXPECT_EQ(b.getLatencySamples(), 0);
}

// Calibration and oversampling double as machine-wide defaults (github issue
// #66): a Settings-page edit writes the parameter's denormalised value under
// its id, and a fresh instance seeds from those keys. Driven over a scratch
// PropertySet; the real file is never touched (main() disables the seeding).
TEST(ProcessorTest, MachineDefaultParametersRoundTrip) {
  juce::PropertySet settings;
  {
    TONE3000Processor a;
    a.parameters.getParameter("calibrateInput")->setValueNotifyingHost(1.0f);
    auto* dbu = a.parameters.getParameter("inputCalibrationLevel");
    dbu->setValueNotifyingHost(dbu->convertTo0to1(4.0f));
    a.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    a.parameters.getParameter("osFactor")->setValueNotifyingHost(0.5f);  // index 1 = 4x
    for (const auto* id : {"calibrateInput", "inputCalibrationLevel", "osEnabled", "osFactor"})
      a.writeMachineDefaultParameter(settings, id);
  }
  // Stored in real units, like preset files.
  EXPECT_EQ(settings.getIntValue("calibrateInput"), 1);
  EXPECT_NEAR(settings.getDoubleValue("inputCalibrationLevel"), 4.0, 1e-4);
  EXPECT_EQ(settings.getIntValue("osEnabled"), 1);
  EXPECT_EQ(settings.getIntValue("osFactor"), 1);

  TONE3000Processor b;
  b.applyMachineDefaultParameters(settings);
  EXPECT_EQ(b.parameters.getRawParameterValue("calibrateInput")->load(), 1.0f);
  EXPECT_NEAR(b.parameters.getRawParameterValue("inputCalibrationLevel")->load(), 4.0f, 1e-4f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osEnabled")->load(), 1.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osFactor")->load(), 1.0f);

  // A host restore that follows wins over the machine default, so a project
  // reopens exactly as it was saved.
  juce::MemoryBlock state;
  {
    TONE3000Processor saved;
    saved.parameters.getParameter("osEnabled")->setValueNotifyingHost(0.0f);
    saved.parameters.getParameter("calibrateInput")->setValueNotifyingHost(0.0f);
    saved.getStateInformation(state);
  }
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(b.parameters.getRawParameterValue("osEnabled")->load(), 0.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("calibrateInput")->load(), 0.0f);

  // Keys missing from the file leave the parameter at its default; a tone
  // parameter is never a machine default.
  juce::PropertySet partial;
  partial.setValue("osFactor", 2);
  partial.setValue("inputLevel", 0.9);
  TONE3000Processor c;
  c.applyMachineDefaultParameters(partial);
  EXPECT_EQ(c.parameters.getRawParameterValue("osFactor")->load(), 2.0f);
  EXPECT_EQ(c.parameters.getRawParameterValue("osEnabled")->load(), 0.0f);
  EXPECT_NEAR(c.parameters.getRawParameterValue("inputLevel")->load(), 0.5f, 1e-6f);
  EXPECT_FALSE(TONE3000Processor::isMachineDefaultParameter("inputLevel"));
  EXPECT_TRUE(TONE3000Processor::isMachineDefaultParameter("inputCalibrationLevel"));
}

TEST(ProcessorTest, RestoredNamsBeforePrepareMatchNamsLoadedAtFourTimes) {
  juce::MemoryBlock saved;
  {
    ChainTestProcessor source;
    juce::ValueTree snapshot("ChainSnapshot"), chain("ChainBlocks");
    chain.appendChild(makeNamBlockTree("first", 1, 101), nullptr);
    chain.appendChild(makeNamBlockTree("second", 2, 102, "a2-am-test-2.nam"), nullptr);
    snapshot.appendChild(chain, nullptr);
    source.restoreFromTree(snapshot);
    ASSERT_TRUE(waitForChainLoaded(source));
    source.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    source.parameters.getParameter("osFactor")->setValueNotifyingHost(0.5f);  // 4x
    source.getStateInformation(saved);
  }

  const auto input = makeNoise(96 * 512, 42, 0.05f);
  auto renderRestored = [&]() {
    TONE3000Processor proc;
    proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    EXPECT_TRUE(waitForChainLoaded(proc));  // engines installed at the initial 1x
    proc.setPlayConfigDetails(2, 2, kFs, 512);
    proc.prepareToPlay(kFs, 512);
    EXPECT_TRUE(waitForChainLoaded(proc));
    return processThrough(proc, input, 512);
  };
  const auto restored = renderRestored();

  TONE3000Processor reference;
  reference.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  reference.parameters.getParameter("osFactor")->setValueNotifyingHost(0.5f);
  reference.setPlayConfigDetails(2, 2, kFs, 512);
  reference.prepareToPlay(kFs, 512);
  reference.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(reference));
  const auto expected = processThrough(reference, input, 512);
  float maxDiff = 0.0f, peak = 0.0f;
  for (size_t i = 16384; i < expected.size(); ++i) {
    maxDiff = std::max(maxDiff, std::abs(restored[i] - expected[i]));
    peak = std::max(peak, std::abs(expected[i]));
  }
  ASSERT_GT(peak, 1e-5f);
  EXPECT_LT(maxDiff, 1e-5f);
}

TEST(ProcessorTest, IgnoresGarbageLegacyAndNewerSchemaState) {
  // setStateInformation must leave the current state untouched for anything
  // it can't own: random bytes, the retired XML format (no T3KB magic), and
  // a well-formed state from a newer schema than this build understands.
  TONE3000Processor proc;
  proc.parameters.getParameter("inputLevel")->setValueNotifyingHost(0.7f);
  auto inputLevel = [&] { return proc.parameters.getRawParameterValue("inputLevel")->load(); };

  const char garbage[] = "definitely not a plugin state";
  proc.setStateInformation(garbage, static_cast<int>(sizeof(garbage)));
  EXPECT_NEAR(inputLevel(), 0.7f, 1e-5f);

  const juce::String legacyXml =
      "<?xml version=\"1.0\"?><TONE3000State><PARAMETERS/></TONE3000State>";
  proc.setStateInformation(legacyXml.toRawUTF8(),
                           static_cast<int>(legacyXml.getNumBytesAsUTF8()));
  EXPECT_NEAR(inputLevel(), 0.7f, 1e-5f);

  // A valid save from this build, re-framed with a bumped schemaVersion. It
  // carries inputLevel = 0.2; restoring it would be visible immediately.
  juce::MemoryBlock saved;
  {
    TONE3000Processor future;
    future.parameters.getParameter("inputLevel")->setValueNotifyingHost(0.2f);
    future.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(
      static_cast<const char*>(saved.getData()) + 4, saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  tree.setProperty("schemaVersion", static_cast<int>(tree.getProperty("schemaVersion")) + 1,
                   nullptr);
  juce::MemoryBlock reframed;
  {
    juce::MemoryOutputStream out(reframed, false);
    out.write("T3KB", 4);
    tree.writeToStream(out);
  }
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_NEAR(inputLevel(), 0.7f, 1e-5f) << "state from a newer schema must be ignored";
}

TEST(ProcessorTest, TailReportCoversDcBlockerWithEmptyChain) {
  // No IRs loaded → the report must still cover the 5 Hz DC blocker decay
  // (2 s), so hosts don't truncate render tails.
  TONE3000Processor proc;
  EXPECT_DOUBLE_EQ(proc.getTailLengthSeconds(), 2.0);
}

TEST(ProcessorTest, AddedDelayBlockIsAudible) {
  // Regression: a runtime-added built-in effect (Delay/Chorus) must actually
  // change the sound. These blocks are model-less, so unlike NAM/IR they have
  // no model-load path to size their DSP (prepare) or reset their mix /
  // wet-fade / swap-mute smoothers. addEffectBlock must therefore prepare the
  // block itself - otherwise the delay/chorus rings stay empty (process() is
  // a no-op) and the block passes its input straight through dry, i.e. the
  // effect is completely inaudible. That is exactly the reported bug: "add a
  // delay/chorus block, but the sound doesn't change at all".
  //
  // Signature used: a short sine burst, then silence. A working delay lays an
  // audible echo across the silence (a tail), whereas a broken (dry) block
  // leaves the tail silent - identical to a clean 48 kHz pass-through.
  const int fs = 48000, bs = 512;
  const int burst = 12800;                       // 266 ms of tone
  const int total = burst + 16384;               // then 341 ms of silence
  // A true burst, NOT makeSine: makeSine runs the tone for the whole buffer, so
  // there would be no silent tail to lay an echo across. The tone stops after
  // `burst` samples and the rest is hard silence - the region a working delay
  // fills with echoes and a broken (dry) block leaves silent like a clean path.
  std::vector<float> sine(total, 0.0f);
  for (int i = 0; i < burst; ++i)
    sine[static_cast<size_t>(i)] = 0.6f * std::sin(2.0 * kPi * 200.0 * i / fs);
  auto rms = [](const std::vector<float>& x, int from, int n) {
    double a = 0.0;
    for (int i = from; i < from + n; ++i) {
      const double v = static_cast<double>(x[static_cast<size_t>(i)]);
      a += v * v;
    }
    return std::sqrt(a / static_cast<double>(n));
  };

  // Control: a clean 48 kHz chain is transparent (no resampler at 48k,
  // oversampling off), so its tail after the burst is silent.
  {
    TONE3000Processor proc;
    proc.setPlayConfigDetails(2, 2, fs, bs);
    proc.prepareToPlay(fs, bs);
    const auto out = processThrough(proc, sine, bs);
    EXPECT_LT(rms(out, total - 4096, 4096), 0.01);
  }

  // A runtime-added Delay block must leave an audible echo in that tail.
  {
    TONE3000Processor proc;
    proc.setPlayConfigDetails(2, 2, fs, bs);
    proc.prepareToPlay(fs, bs);
    const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
    ASSERT_FALSE(id.empty()) << "addEffectBlock(Delay) was rejected";
    proc.setBlockParam(id, "delayTimeMs", 200.0);
    proc.setBlockParam(id, "delayFeedback", 0.5);
    const auto out = processThrough(proc, sine, bs);
    const double tailRms = rms(out, total - 4096, 4096);
    EXPECT_GT(tailRms, 0.01)
        << "the delay added no audible echo (effect passed the input through dry)";
  }

  // A runtime-added Chorus block must change the sound the same way.
  {
    TONE3000Processor proc;
    proc.setPlayConfigDetails(2, 2, fs, bs);
    proc.prepareToPlay(fs, bs);
    const auto id = proc.addEffectBlock(EffectKind::Chorus, "left", 0);
    ASSERT_FALSE(id.empty()) << "addEffectBlock(Chorus) was rejected";
    proc.setBlockParam(id, "chorusDepthMs", 3.0);
    proc.setBlockParam(id, "chorusRateHz", 1.0);
    const auto out = processThrough(proc, sine, bs);
    double d = 0.0;
    for (int i = 0; i < total; ++i) {
      const double e =
          static_cast<double>(out[static_cast<size_t>(i)]) -
          static_cast<double>(sine[static_cast<size_t>(i)]);
      d += e * e;
    }
    EXPECT_GT(d / static_cast<double>(total), 1e-4)
        << "the chorus added no audible change (effect passed the input through dry)";
  }
}

TEST(ProcessorTest, ReverbParamsAreAcceptedBySetBlockParam) {
  // Regression: setBlockParam's "known param" allow-list omitted the five
  // reverb params (Decay/Pre/Tone/Size/Width), so every reverb knob was
  // rejected (returned false) and its value never persisted. The block was
  // still created audible-at-defaults (which is why "reverb works"), but any
  // resync after a neighbouring Mix/In/Out move re-read defaults and snapped
  // the knobs back -- the reported symptom. Each param must be accepted.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Reverb, "left", 0);
  ASSERT_FALSE(id.empty()) << "addEffectBlock(Reverb) was rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "reverbDecayMs", 2500.0)) << "reverbDecayMs rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "reverbPreMs", 20.0)) << "reverbPreMs rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "reverbTone", 0.8)) << "reverbTone rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "reverbSize", 0.9)) << "reverbSize rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "reverbWidth", 0.5)) << "reverbWidth rejected";
}

TEST(ProcessorTest, ConvolutionParamsAreAcceptedBySetBlockParam) {
  // Same regression guard as the reverb family: the five Convolution knobs
  // (Gain/Width/Start/End/Length) must each survive the known-param
  // allow-list, apply to the block, and round-trip through the state.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Convolution, "left", 0);
  ASSERT_FALSE(id.empty()) << "addEffectBlock(Convolution) was rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convGain", 0.7)) << "convGain rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convWidth", 0.3)) << "convWidth rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convStartS", 1.5)) << "convStartS rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convEndS", 4.0)) << "convEndS rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convPitch", 0.9)) << "convPitch rejected";
  // The B-surface additions (Pre / F In / F Out / InCrv / OutCrv / Tone)
  // must survive the allow-list too, with clamped-but-distinct values.
  EXPECT_TRUE(proc.setBlockParam(id, "convPreMs", 25.0)) << "convPreMs rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convFadeIn", 0.18)) << "convFadeIn rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convFadeOut", 0.33)) << "convFadeOut rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convInCurve", 0.22)) << "convInCurve rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convOutCurve", 0.66)) << "convOutCurve rejected";
  EXPECT_TRUE(proc.setBlockParam(id, "convToneDb", 3.5)) << "convToneDb rejected";
}

TEST(ProcessorTest, ConvolutionBlockRoundTripsAllParams) {
  // Every conv knob must survive BOTH round-trips: setBlockParam ->
  // getChainState (the UI's read path) and getStateInformation ->
  // setStateInformation (the persistence path). Regression guard for the
  // B-surface additions; the five original knobs ride along.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Convolution, "left", 0);
  ASSERT_FALSE(id.empty()) << "addEffectBlock(Convolution) was rejected";
  const auto set = [&](const char* name, double v) {
    EXPECT_TRUE(proc.setBlockParam(id, name, v)) << name << " rejected";
  };
  set("convGain", 0.63);
  set("convWidth", 0.44);
  set("convStartS", 0.75);
  set("convEndS", 2.5);
  set("convPitch", 0.78);
  set("convPreMs", 25.0);
  set("convFadeIn", 0.18);
  set("convFadeOut", 0.33);
  set("convInCurve", 0.22);
  set("convOutCurve", 0.66);
  set("convToneDb", 3.5);

  // (1) the UI's read path: getChainState must expose every knob.
  auto convRow = [](TONE3000Processor& prc) {
    juce::var row;
    const juce::var state = prc.getChainState(-1);  // keep alive (getArray points into it)
    const juce::var chainVar = state["chain"];     // stable copy
    auto* const chain = chainVar.getArray();
    for (int i = 0; chain != nullptr && i < chain->size(); ++i) {
      const juce::var item = (*chain)[i];
      const juce::var params = item["params"];
      if (params.isObject() && params.hasProperty("convGain"))
        row = params;
    }
    return row;
  };
  const auto row = convRow(proc);
  EXPECT_TRUE(row.isObject()) << "the conv block's state row is not published";
  EXPECT_NEAR(static_cast<double>(row["convGain"]), 0.63, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convWidth"]), 0.44, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convStartS"]), 0.75, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convEndS"]), 2.5, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convPitch"]), 0.78, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convPreMs"]), 25.0, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convFadeIn"]), 0.18, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convFadeOut"]), 0.33, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convInCurve"]), 0.22, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convOutCurve"]), 0.66, 1e-9);
  EXPECT_NEAR(static_cast<double>(row["convToneDb"]), 3.5, 1e-9);

  // (2) the persistence path: save, restore into a fresh processor.
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  TONE3000Processor restored;
  restored.setPlayConfigDetails(2, 2, 48000, 512);
  restored.prepareToPlay(48000, 512);
  restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  const auto row2 = convRow(restored);
  EXPECT_TRUE(row2.isObject()) << "the conv block did not survive the state round-trip";
  EXPECT_NEAR(static_cast<double>(row2["convGain"]), 0.63, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convWidth"]), 0.44, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convStartS"]), 0.75, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convEndS"]), 2.5, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convPitch"]), 0.78, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convPreMs"]), 25.0, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convFadeIn"]), 0.18, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convFadeOut"]), 0.33, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convInCurve"]), 0.22, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convOutCurve"]), 0.66, 1e-9);
  EXPECT_NEAR(static_cast<double>(row2["convToneDb"]), 3.5, 1e-9);
}


}  // namespace

namespace {
// Deterministic fixture IR (2 ch, 4096 samples, decaying tones) written to a
// stable temp path so the round-trip tests restore FROM DISK, as app start
// does.
juce::File makeFixedIrWav() {
  const juce::File wav(juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("t3k-ir-fixture.wav"));
  wav.deleteFile();
  juce::WavAudioFormat fmt;
  auto stream = wav.createOutputStream();
  if (stream == nullptr)
    return {};
  // This JUCE build's WavAudioFormat writer takes OWNERSHIP of the stream
  // (unique_ptr<OutputStream>&, consumed on success), so hand it a base-type
  // owning pointer rather than the derived FileOutputStream wrapper.
  auto base = std::unique_ptr<juce::OutputStream>(stream.release());
  juce::AudioFormatWriterOptions opts;
  opts = opts.withSampleRate(48000.0).withNumChannels(2).withBitsPerSample(24);
  if (auto writer = fmt.createWriterFor(base, opts); writer != nullptr) {
    juce::AudioBuffer<float> buf(2, 4096);
    for (int i = 0; i < 4096; ++i) {
      const float env = std::exp(-0.003f * i);
      buf.setSample(0, i, 0.4f * std::sin(0.02f * i) * env);
      buf.setSample(1, i, 0.3f * std::sin(0.023f * i) * env);
    }
    writer->writeFromAudioSampleBuffer(buf, 0, 4096);
    return wav;
  }
  return {};
}
}  // namespace

TEST(ProcessorTest, ConvIrReferenceSurvivesStateRoundTrip) {
  // E-2: a loaded kernel must survive the preset/app-restart round trip.
  // proc1 loads from disk; full state moves to a FRESH proc2, which must
  // come up with the engine re-hydrated from the persisted path.
  const juce::File irWav = makeFixedIrWav();
  ASSERT_TRUE(irWav.existsAsFile()) << "fixture WAV could not be written";

  TONE3000Processor proc1;
  proc1.setPlayConfigDetails(2, 2, 48000, 512);
  proc1.prepareToPlay(48000, 512);
  const auto id = proc1.addEffectBlock(EffectKind::Convolution, "left", 0);
  ASSERT_FALSE(id.empty());
  juce::var resp = proc1.loadConvIr(id, irWav);
  ASSERT_TRUE(resp.getDynamicObject() == nullptr ||
              !resp.getDynamicObject()->hasProperty("error"));

  juce::MemoryBlock stateBytes;
  proc1.getStateInformation(stateBytes);
  // The state is a BINARY ValueTree stream (magic prefix + raw bytes, embedded
  // NULs): never route it through juce::String (this JUCE's String
  // constructor is a Unicode decoder, not a byte container). Byte-wise search
  // of the raw buffer instead.
  const std::string_view state1(reinterpret_cast<const char*>(stateBytes.getData()),
                                stateBytes.getSize());
  EXPECT_TRUE(state1.find(irWav.getFullPathName().toRawUTF8()) != std::string_view::npos)
      << "state must carry the IR path (E-2 persistence)";
  EXPECT_TRUE(state1.find("t3k-ir-fixture.wav") != std::string_view::npos)
      << "state must carry the name";

  TONE3000Processor proc2;
  proc2.setPlayConfigDetails(2, 2, 48000, 512);
  proc2.prepareToPlay(48000, 512);
  proc2.setStateInformation(stateBytes.getData(), static_cast<int>(stateBytes.getSize()));

  // Functional proof the engine re-hydrated: 1.0 s of tone then silence —
  // ONLY a live convolver produces energy in the wet-tail window right
  // after the tone stops.
  {
    std::vector<float> tone; // scoped: torn down before the processors below
    const int rate = 48000;
    const int total = 3 * rate;
    tone.assign(static_cast<size_t>(total), 0.0f);
    const double w = 2.0 * 3.14159265358979 * 440.0 / rate;
    for (int i = 0; i < rate; ++i)
      tone[i] = 0.5f * static_cast<float>(std::sin(w * i));
    const auto out = processThrough(proc2, tone, 512);
    const int ws = (int) (1.01 * rate), we = (int) (1.08 * rate);
    double rms = 0.0;
    for (int i = ws; i < we; ++i)
      rms += (double) out[i] * out[i];
    rms = std::sqrt(rms / (we - ws));
    EXPECT_GT(rms, 1e-5) << "restored engine produced no wet tail (IR was not re-hydrated)";
  }
}

TEST(ProcessorTest, ConvIrMissingFileRestoresGracefully) {
  // E-2: a preset whose IR file is not on this machine must restore without
  // losing the identity — the tile's IR-missing recovery state depends on
  // name + path surviving while the engine stays empty. (The state is a
  // binary ValueTree, so we DROP the file rather than edit the bytes.)
  const juce::File irWav = makeFixedIrWav();
  ASSERT_TRUE(irWav.existsAsFile());

  TONE3000Processor proc1;
  proc1.setPlayConfigDetails(2, 2, 48000, 512);
  proc1.prepareToPlay(48000, 512);
  const auto id = proc1.addEffectBlock(EffectKind::Convolution, "left", 0);
  ASSERT_FALSE(id.empty());
  proc1.loadConvIr(id, irWav);

  juce::MemoryBlock stateBytes;
  proc1.getStateInformation(stateBytes);
  ASSERT_TRUE(irWav.deleteFile()) << "the file must exist to disappear";

  TONE3000Processor proc2;
  proc2.setPlayConfigDetails(2, 2, 48000, 512);
  proc2.prepareToPlay(48000, 512);
  proc2.setStateInformation(stateBytes.getData(), static_cast<int>(stateBytes.getSize()));

  // No crash; the chain still processes (dry path only).
  std::vector<float> in(512, 0.0f);
  for (int i = 0; i < 512; ++i)
    in[i] = 0.3f * std::sin(0.02f * i);
  (void) processThrough(proc2, in, 512);

  juce::MemoryBlock state2;
  proc2.getStateInformation(state2);
  const std::string_view out2(reinterpret_cast<const char*>(state2.getData()),
                              state2.getSize());
  EXPECT_TRUE(out2.find(irWav.getFullPathName().toRawUTF8()) != std::string_view::npos)
      << "the missing-file identity must persist for the IR-missing UI state";
}
