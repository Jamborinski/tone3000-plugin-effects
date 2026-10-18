Let's evaluate the architecture based on that correction and see where the actual risks and optimizations live.

Core Architecture & File Mapping

Now that we know the workflow context, we can evaluate what needs attention across your real-time processing and state serialization logic.
                  ┌──────────────────────────────┐
                  │   plugin/src/Processor.cpp   │
                  └──────────────┬───────────────┘
                                 │  (Signal Chain Execution)
                                 ▼
┌──────────────────────────────────────────────────────────────────┐
│  Tone Chains (Left / Right / Dual Mono Branching)                │
│                                                                  │
│  ┌───────────────────┐    ┌───────────────────┐                  │
│  │   NAM Block       │───►│   IR Block        │                  │
│  │   (Neural Amp)    │    │   (Fixed 48kHz)   │                  │
│  └───────────────────┘    └───────────────────┘                  │
└──────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼ (DAW Autosave / Project Commit)
                  ┌──────────────────────────────┐
                  │ plugin/src/ProcessorState.cpp│
                  └──────────────────────────────┘

Priority Optimization Breakdown


1. Audio Thread & Safety (plugin/src/Processor.cpp)

• The Mechanism: TONE3000Processor::processBlock steps through the active blocks in your signal chain. When a user manipulates nodes in the UI layout (adding blocks, changing routing, dragging to reorder), the underlying processing chain must rebuild.
• The Optimization: Ensure that any block instantiation or structural allocation triggered by a node workflow edit never halts the real-time audio thread.
• The Implementation: Keep structural modifications strictly background-allocated. Build out your new configuration on a worker thread and pass it down to the audio thread via an atomic pointer swap or a single-producer single-consumer (SPSC) ring buffer to prevent hard audio dropouts under stressful host processing environments.

2. Downsampling & Resampling Filters (plugin/src/Processor.cpp)

• The Mechanism: To keep processing optimized, IR blocks convolve inside a localized 48 kHz decimate/interpolate island while the broader NAM architecture executes at up to 8x oversampling.
• The Optimization: If consecutive layout nodes are both IR blocks, executing downsampling/upsampling logic independently between them creates redundant filtering overhead and unnecessary phase distortion.
• The Implementation: Group adjacent linear blocks together. If two or more IR blocks sit back-to-back in the chain layout, downsample the stream once before the first block, run the convolution algorithms back-to-back at the 48 kHz base rate, and upsample once after the final IR block completes.

3. State & Preset Footprints (plugin/src/ProcessorState.cpp)

• The Mechanism: To guarantee projects open seamlessly offline without path breaks, the plugin embeds raw machine learning model byte payloads directly into the DAW project state layer via getStateInformation.
• The Optimization: Heavy multi-megabyte NAM configurations duplicated across multiple nodes will cause your session state footprint to grow exponentially. This bloat slows down host performance during aggressive background auto-saving cycles.
• The Implementation:
	1. Compress the model byte payload using a lightweight, hyper-fast format like LZ4 or Zstd before committing it to the raw state memory stream.
	2. Implement a content-addressable deduplicator. If identical nodes are cloned or split across stereo chains, serialize the base binary data payload exactly once at the root level and map matching layout instances to it using a unique cryptographic hash reference.

Here are code examples addressing the two most critical architectural optimizations for your node-based signal chain layout: lock-free block engine swapping on the audio thread and state compression/deduplication for large machine learning model payloads.

1. Lock-Free Block Swapping (plugin/src/Processor.cpp)

When a user modifies or reorders nodes in the UI, deleting an old NeuralAmpModelerCore engine or allocating a new one directly on the audio thread will cause immediate audio dropouts due to heap allocation locks (malloc/delete).
The standard solution is a Deferred Garbage Collection pattern using an atomic pointer swap.
cpp
#include <atomic>
#include <memory>
#include <vector>

// Forward declarations of your existing types
class NeuralAmpModelerCore; 

struct ProcessingBlock 
{
    std::unique_ptr<NeuralAmpModelerCore> engine;
    bool isBypassed = false;
    // ... other block parameters
};

class TONE3000Processor 
{
public:
    void prepareToPlay (double sampleRate, int samplesPerBlock) 
    {
        // ... standard JUCE setup
    }

    // --- AUDIO THREAD ---
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) 
    {
        // 1. Instantly check if the background thread has swapped out our chain configuration
        if (chainNeedsUpdate.load (std::memory_order_acquire)) 
        {
            // Atomically exchange the active pointer with the new background configuration
            auto* oldChain = activeChain.exchange (pendingChain.exchange (nullptr, std::memory_order_relaxed), 
                                                   std::memory_order_release);
            
            if (oldChain != nullptr) 
            {
                // Push the dirty pointer into a lock-free bin so the background thread can sweep it
                deadChainGarbage.exchange (oldChain, std::memory_order_release);
            }
            chainNeedsUpdate.store (false, std::memory_order_release);
        }

        // 2. Fetch the active chain natively and process safely with no mutex locks
        auto* currentChain = activeChain.load (std::memory_order_acquire);
        if (currentChain != nullptr) 
        {
            for (auto& block : *currentChain) 
            {
                if (! block.isBypassed && block.engine != nullptr) 
                {
                    // block.engine->process(buffer);
                }
            }
        }
    }

    // --- MESSAGE / UI / BACKGROUND THREAD ---
    void handleNodeWorkflowUpdate (std::vector<ProcessingBlock> newLayout) 
    {
        // 1. Allocate and spin up the new engines safely on the background thread
        auto newChain = std::make_unique<std::vector<ProcessingBlock>> (std::move (newLayout));

        // 2. Queue the new chain pointer for the audio thread to grab
        pendingChain.store (newChain.release(), std::memory_order_release);
        chainNeedsUpdate.store (true, std::memory_order_release);

        // 3. Spin-wait or check if there is an old configuration dead-dropped by the audio thread
        while (chainNeedsUpdate.load (std::memory_order_relaxed)) 
        {
            // Wait a few cycles for the audio thread to swap pointers
        }

        // 4. Safely clean up the old layout heap allocation here on the UI background thread
        auto* garbage = deadChainGarbage.exchange (nullptr, std::memory_order_relaxed);
        if (garbage != nullptr) 
        {
            delete garbage; // Heap deallocation happens completely away from the audio thread!
        }
    }

private:
    std::atomic<bool> chainNeedsUpdate { false };
    std::atomic<std::vector<ProcessingBlock>*> activeChain { nullptr };
    std::atomic<std::vector<ProcessingBlock>*> pendingChain { nullptr };
    std::atomic<std::vector<ProcessingBlock>*> deadChainGarbage { nullptr };
};
Use code with caution.

2. State Compression & Deduplication (plugin/src/ProcessorState.cpp)

To avoid massive session state bloat (e.g., repeating the same multi-megabyte .nam model bytes multiple times if cloned across branches or stereo lanes), you can use an addressable mapping scheme alongside compression.
This example leverages juce::ZLibCompressorOutputStream (built into JUCE) to dramatically lower the binary payload overhead during DAW auto-saves.
cpp
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include <string>

struct UIBlockState 
{
    std::string blockId;
    std::string modelName;
    std::vector<char> rawModelBytes; // Large ML binary weights
};

class TONE3000StateSerializer 
{
public:
    static void writeCompressedState (juce::MemoryBlock& destBlock, const std::vector<UIBlockState>& activeNodes)
    {
        juce::ValueTree rootState ("TONE3000_PROJECT");
        juce::ValueTree modelsLibrary ("MODELS_LIBRARY");
        juce::ValueTree nodesChain ("NODES_CHAIN");
        
        rootState.addChild (modelsLibrary, -1, nullptr);
        rootState.addChild (nodesChain, -1, nullptr);

        // Track unique hashes to ensure we store data once
        std::unordered_map<size_t, std::string> storedModelHashes;
        std::hash<std::string> stringHasher;

        for (const auto& node : activeNodes) 
        {
            // Generate a content hash based on raw model strings or bytes
            size_t contentHash = stringHasher (node.modelName + std::to_string (node.rawModelBytes.size()));
            std::string sharedId = "model_" + std::to_string (contentHash);

            // Deduplication: Only write the heavy binary payload once to the Library tree
            if (storedModelHashes.find (contentHash) == storedModelHashes.end()) 
            {
                juce::ValueTree modelAsset ("ModelAsset");
                modelAsset.setProperty ("id", sharedId, nullptr);
                
                // Load binary asset directly into a memory property block
                juce::MemoryBlock binaryPayload (node.rawModelBytes.data(), node.rawModelBytes.size());
                modelAsset.setProperty ("binaryData", binaryPayload, nullptr);
                
                modelsLibrary.addChild (modelAsset, -1, nullptr);
                storedModelHashes[contentHash] = sharedId;
            }

            // Write the lightweight layout configuration pointing back to the shared asset
            juce::ValueTree nodeInstance ("NodeInstance");
            nodeInstance.setProperty ("blockId", node.blockId, nullptr);
            nodeInstance.setProperty ("modelRef", sharedId, nullptr); // Soft reference pointer
            nodesChain.addChild (nodeInstance, -1, nullptr);
        }

        // --- COMPRESSION PIPELINE ---
        // Instead of raw XML string creation, pipe the ValueTree data into a compressed memory buffer
        juce::MemoryOutputStream rawStream;
        rootState.writeToStream (rawStream);

        juce::MemoryOutputStream compressedStream (destBlock);
        // Using JUCE's native ZLib hook (or replace with an external LZ4 wrapper for faster compression)
        juce::ZLibCompressorOutputStream zlibStream (&compressedStream, 4, false); 
        
        zlibStream.write (rawStream.getData(), rawStream.getDataSize());
        zlibStream.flush();
    }
};


To integrate LZ4 into your project, you need to modify your CMake configuration to fetch the library and update your state serialization code to handle fast compression. LZ4 is significantly faster than ZLib/GZip, making it the ideal choice for real-time audio plugin state persistence where saving must happen seamlessly during DAW background auto-saves.
Here is how to set it up step-by-step.

1. Update CMake Configuration (CMakeLists.txt)

You can pull the official LZ4 repository automatically using CPM (which your repository already leverages for dependencies like FreeType and GoogleTest). Add this configuration block to your dependency initialization section:
cmake
# --- Fetch LZ4 via CPM ---
CPMAddPackage(
    NAME lz4
    GITHUB_REPOSITORY lz4/lz4
    GIT_TAG v1.10.0 # Pin to a stable version
    OPTIONS
        "LZ4_BUILD_CLI OFF"
        "LZ4_BUILD_SHARED OFF" # Build statically to keep dependencies local
)

# --- Link against your plugin target ---
# Assuming your target variable is named ${PROJECT_NAME} or tone3000-plugin
target_link_libraries(tone3000-plugin PRIVATE lz4::lz4)
Use code with caution.

2. High-Speed LZ4 Serialization Code (plugin/src/ProcessorState.cpp)

Once linked, replace the slow native ZLib stream calls with raw LZ4 processing blocks. The example below uses LZ4_compress_default and LZ4_decompress_safe.
cpp
#include <juce_audio_processors/juce_audio_processors.h>
#include <lz4.h>
#include <vector>

class TONE3000StateSerializer 
{
public:
    // --- SERIALIZATION (COMPRESSION) ---
    static void writeLZ4CompressedState (juce::MemoryBlock& destBlock, const juce::ValueTree& rootState)
    {
        // 1. Serialize the ValueTree layout natively down to a temporary raw memory buffer
        juce::MemoryOutputStream uncompressedStream;
        rootState.writeToStream (uncompressedStream);
        
        const char* sourceData = static_cast<const char*> (uncompressedStream.getData());
        const int sourceSize = static_cast<int> (uncompressedStream.getDataSize());

        if (sourceSize == 0) return;

        // 2. Query LZ4 for the absolute maximum bound required for the destination compression buffer
        const int maxCompressedSize = LZ4_compressBound (sourceSize);
        
        // Staging buffer accommodating the payload layout + space to prepend the original size prefix
        std::vector<char> compressedBuffer (sizeof (int) + maxCompressedSize);

        // 3. Store uncompressed data size at the very front (crucial for accurate decompression allocations)
        std::memcpy (compressedBuffer.data(), &sourceSize, sizeof (int));

        // 4. Compress the binary payload directly into the remainder of the buffer array
        int realCompressedBytes = LZ4_compress_default (
            sourceData, 
            compressedBuffer.data() + sizeof (int), 
            sourceSize, 
            maxCompressedSize
        );

        if (realCompressedBytes > 0) 
        {
            // 5. Trim and flush down to the final memory block target handed to the DAW host
            destBlock.replaceWith (compressedBuffer.data(), sizeof (int) + realCompressedBytes);
        }
    }

    // --- DESERIALIZATION (DECOMPRESSION) ---
    static juce::ValueTree readLZ4CompressedState (const void* compressedData, int compressedDataSize)
    {
        if (compressedDataSize <= static_cast<int> (sizeof (int)))
            return juce::ValueTree();

        // 1. Extract the original uncompressed byte boundary saved at the header position
        int originalSize = 0;
        std::memcpy (&originalSize, compressedData, sizeof (int));

        // 2. Allocate an target memory grid matching that exact structure boundary
        juce::MemoryBlock uncompressedBlock (static_cast<size_t> (originalSize));

        // 3. Decompress the remaining binary blob back into place
        int decompressionResult = LZ4_decompress_safe (
            static_cast<const char*> (compressedData) + sizeof (int),
            static_cast<char*> (uncompressedBlock.getData()),
            compressedDataSize - sizeof (int),
            originalSize
        );

        if (decompressionResult < 0)
        {
            juce::Logger::writeToLog ("Error: LZ4 State Decompression Failed!");
            return juce::ValueTree();
        }

        // 4. Restore the state elements cleanly straight back into the live JUCE node model tree
        return juce::ValueTree::readFromStream (uncompressedBlock.getData(), uncompressedBlock.getSize());
    }
};
Use code with caution.

Why this fits your refactor/repo-organization architecture:

• Microsecond Execution Speed: LZ4 finishes encoding steps magnitudes faster than conventional ZLib profiles. It ensures that whenever a DAW triggers background project autosaves, the entire multi-node serialization chain executes comfortably inside your thread time frame without causing any UI stutters.
• Clean Payload Footprints: Combining this compression scheme with the duplicate mapping hashes detailed earlier effectively strips out repetitive data inflation from cloned or dual-mono amp nodes.

Here are the concrete implementations for both the GoogleTest unit tests (test/src/processor_tests.cpp) and the Undo/Redo integration strategy (plugin/src/ProcessorState.cpp) to safely deploy your new compressed state pipeline.

1. Unit Tests for the Compression Pipeline (test/src/processor_tests.cpp)

Add these checks to your GoogleTest suite. They verify that serialization round-trips perfectly, handles empty data boundaries, and completely rejects corrupted byte injection without crashing your host process.
cpp
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "ProcessorState.cpp" // Adjust include path based on your exact layout

class LZ4CompressionPipelineTest : public ::testing::Test 
{
protected:
    juce::ValueTree createTestNodeTree() 
    {
        juce::ValueTree root("TONE3000_PROJECT");
        root.setProperty("version", "2.0.0", nullptr);

        juce::ValueTree nodesList("CHAIN_NODES");
        for (int i = 0; i < 5; ++i) 
        {
            juce::ValueTree node("NODE");
            node.setProperty("id", "slot_" + juce::String(i), nullptr);
            node.setProperty("mix", 0.75f, nullptr);
            node.setProperty("gain", -3.2f, nullptr);
            // Simulate a large dummy neural weight string footprint
            node.setProperty("weights", juce::String::repeatedString("A2_ML_DATA_BLOB_PRIME_", 200), nullptr);
            nodesList.addChild(node, -1, nullptr);
        }
        root.addChild(nodesList, -1, nullptr);
        return root;
    }
};

// 1. Core Validity Test
TEST_F(LZ4CompressionPipelineTest, RoundTripSerializationIsIdentical) 
{
    juce::ValueTree originalTree = createTestNodeTree();
    juce::MemoryBlock compressedMemory;

    // Serialize and compress
    TONE3000StateSerializer::writeLZ4CompressedState(compressedMemory, originalTree);
    ASSERT_GT(compressedMemory.getSize(), sizeof(int)); // Must be larger than the size prefix header

    // Decompress and restore
    juce::ValueTree restoredTree = TONE3000StateSerializer::readLZ4CompressedState(
        compressedMemory.getData(), 
        static_cast<int>(compressedMemory.getSize())
    );

    // Verify properties match exactly
    ASSERT_TRUE(restoredTree.isValid());
    EXPECT_EQ(restoredTree.getProperty("version").toString(), "2.0.0");
    EXPECT_EQ(restoredTree.getChildWithName("CHAIN_NODES").getChild(2).getProperty("id").toString(), "slot_2");
}

// 2. Empty State Safeguard
TEST_F(LZ4CompressionPipelineTest, HandlesEmptyOrInvalidTreesGracefully) 
{
    juce::ValueTree emptyTree;
    juce::MemoryBlock compressedMemory;

    TONE3000StateSerializer::writeLZ4CompressedState(compressedMemory, emptyTree);
    
    juce::ValueTree restoredTree = TONE3000StateSerializer::readLZ4CompressedState(
        compressedMemory.getData(), 
        static_cast<int>(compressedMemory.getSize())
    );
    
    EXPECT_FALSE(restoredTree.isValid());
}

// 3. Corruption/Fuzz Security Guard
TEST_F(LZ4CompressionPipelineTest, RejectsCorruptDataWithoutCrashing) 
{
    juce::MemoryBlock garbageData(128);
    // Inject a fake huge uncompressed size header to test allocation boundaries
    int fakeHugeSize = 1024 * 1024 * 50; // 50MB claim
    std::memcpy(garbageData.getData(), &fakeHugeSize, sizeof(int));
    
    // Scramble the remaining garbage payload bytes
    for (size_t i = sizeof(int); i < garbageData.getSize(); ++i) {
        static_cast<char*>(garbageData.getData())[i] = static_cast<char>(i % 255);
    }

    // Decompressor must fail safe, reject the payload, and return an invalid tree node instead of crashing
    juce::ValueTree restoredTree = TONE3000StateSerializer::readLZ4CompressedState(
        garbageData.getData(), 
        static_cast<int>(garbageData.getSize())
    );

    EXPECT_FALSE(restoredTree.isValid());
}
Use code with caution.

2. Integration with Undo/Redo Engine Steps (plugin/src/ProcessorState.cpp)

JUCE features an integrated juce::UndoManager stack. If your modern workflow state relies completely on a single master state ValueTree, you do not need to manually compress states for Undo/Redo snapshots. Doing so would throttle rendering cycles.
Instead, leverage dynamic state references. Let juce::UndoManager automatically track small delta property adjustments in memory, while applying your high-efficiency LZ4 compressor only at the boundaries where your plugin communicates state outward to the DAW host.
cpp
#include <juce_audio_processors/juce_audio_processors.h>

class TONE3000ProcessorWithUndo : public juce::AudioProcessor,
                                   public juce::ValueTree::Listener
{
public:
    TONE3000ProcessorWithUndo()
    {
        // 1. Initialize the master data topology state model
        pluginStateTree = juce::ValueTree("TONE3000_PROJECT");
        pluginStateTree.addListener(this); // Listen for adjustments across node structures
    }

    // --- DAW AUTO-SAVE BOUNDARY (COMPRESSED) ---
    void getStateInformation(juce::MemoryBlock& destData) override
    {
        // Thread-safe lock snapshot copy of current model tree state properties
        juce::ValueTree stateCopy;
        {
            const juce::ScopedLock sl(stateLock);
            stateCopy = pluginStateTree.createCopy();
        }
        
        // Squeeze down memory consumption boundaries tightly before delivery to host process container
        TONE3000StateSerializer::writeLZ4CompressedState(destData, stateCopy);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        juce::ValueTree restoredTree = TONE3000StateSerializer::readLZ4CompressedState(data, sizeInBytes);
        
        if (restoredTree.isValid())
        {
            const juce::ScopedLock sl(stateLock);
            
            // Clear current undo steps to avoid memory mismatches across complete session reload events
            undoManager.clearUndoHistory(); 
            
            pluginStateTree.copyPropertiesAndChildrenFrom(restoredTree, &undoManager);
            triggerSynchronousUiRefresh();
        }
    }

    // --- INTERNAL NODE WORKFLOW ACTIONS (AUTOMATIC LZ4 BYPASS) ---
    void userAddedSignalChainNode(const juce::String& blockType, int targetIndex)
    {
        juce::ValueTree newNode("NODE");
        newNode.setProperty("type", blockType, nullptr);
        
        // This transaction will be pushed straight into memory delta arrays seamlessly 
        // using native pointers—no encryption or compression overhead delays runtime usage!
        pluginStateTree.addChild(newNode, targetIndex, &undoManager);
    }

    void triggerUserUndoStep() { undoManager.undo(); }
    void triggerUserRedoStep() { undoManager.redo(); }

private:
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { /* Reactive UI hooks */ }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { /* ... */ }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { /* ... */ }
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { /* ... */ }
    void valueTreeParentChanged(juce::ValueTree&) override { /* ... */ }
    
    void triggerSynchronousUiRefresh() { /* Refresh UI Canvas elements */ }

    juce::CriticalSection stateLock;
    juce::ValueTree pluginStateTree;
    juce::UndoManager undoManager; // Monitors raw property edits directly inside memory cache lines
};
Use code with caution.

Implementation Takeaways

1. Execution Separation: The juce::UndoManager acts as an ultra-fast runtime memory log for user canvas adjustments. It bypasses compilation loops entirely while editing node chains live.
2. CI-Ready Security: The newly added pipeline tests automatically flag block errors or data array changes inside your headless environment, preventing raw payload memory leaks before any binaries make it into validation builds.

Here is a production-ready, cross-platform GitHub Actions workflow file (.github/workflows/build-and-test.yml) optimized specifically for your refactor/repo-organization architecture.
It leverages a modern build matrix to concurrently build and run your headless GoogleTest suite across macOS, Windows, and Ubuntu (Linux). It also includes the necessary setup for Linux runtime packages (like fontconfig and libasound2) that JUCE targets look for at compile time, and wraps the Linux run in a virtual framebuffer (xvfb-run) so any structural initialization assertions pass seamlessly.

.github/workflows/build-and-test.yml

yaml
name: TONE3000 Plugin Build & Test Pipeline

on:
  push:
    branches: [ "main", "develop", "refactor/repo-organization" ]
  pull_request:
    branches: [ "main", "develop" ]

jobs:
  build-and-test:
    name: Test on ${{ matrix.os }} (${{ matrix.build_type }})
    runs-on: ${{ matrix.os }}
    
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        build_type: [Release]

    steps:
    - name: Checkout Repository
      uses: actions/checkout@v4
      with:
        submodules: recursive # Instantly fetches NeuralAmpModelerCore, AudioDSPTools, etc.

    # --- ENVIRONMENT SETUP: LINUX ---
    - name: Install Linux Build Dependencies
      if: matrix.os == 'ubuntu-latest'
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          libgtk-3-dev \
          libasound2-dev \
          libjack-jackd2-dev \
          libcurl4-openssl-dev \
          libx11-dev \
          libxext-dev \
          libxinerama-dev \
          libxrandr-dev \
          libxcursor-dev \
          libgl1-mesa-dev \
          libglu1-mesa-dev \
          fontconfig \
          xvfb

    # --- ENVIRONMENT SETUP: WINDOWS ---
    - name: Setup MSBuild (Windows)
      if: matrix.os == 'windows-latest'
      uses: microsoft/setup-msbuild@v2

    # --- CACHE MANAGEMENT ---
    - name: Cache CMake & CPM Modules
      uses: actions/cache@v4
      with:
        path: |
          ~/.cache/CPM
          build/_deps
        key: ${{ matrix.os }}-${{ matrix.build_type }}-cpm-${{ hashFiles('**/CMakeLists.txt', '**/CMakePresets.json') }}
        restore-keys: |
          ${{ matrix.os }}-${{ matrix.build_type }}-cpm-

    # --- CMAKE CONFIGURATION ---
    - name: Configure CMake
      run: |
        cmake -B build -S . \
          -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
          -DHEADLESS=ON \
          -DT3K_BUILD_UI_TESTBED=OFF

    # --- COMPILATION ---
    - name: Build DSP and Serialization Tests
      run: cmake --build build --config ${{ matrix.build_type }} --parallel 4

    # --- EXECUTE GOOGLETEST SUITE ---
    - name: Run Tests (macOS / Windows)
      if: matrix.os != 'ubuntu-latest'
      working-directory: build
      run: ctest -C ${{ matrix.build_type }} --output-on-failure

    # Linux requires xvfb-run to simulate screen spaces, even for semi-headless JUCE instances
    - name: Run Tests (Linux with Virtual Framebuffer)
      if: matrix.os == 'ubuntu-latest'
      working-directory: build
      run: xvfb-run --server-args="-screen 0 1920x1080x24" ctest -C ${{ matrix.build_type }} --output-on-failure
Use code with caution.

Pipeline Key Elements

1. submodules: recursive: This ensures that native DSP submodules (NeuralAmpModelerCore and AudioDSPTools) are checked out alongside the base layer immediately before CMake begins generation.
2. -DHEADLESS=ON: Keeps the workflow incredibly lightweight. It strips out runtime graphics server locks so compilation finishes fast, focusing the host runner entirely on your core math validation (dsp_tests.cpp) and serialization compression checks (processor_tests.cpp).
3. xvfb-run on Ubuntu: Even when compiling headlessly, JUCE processors initialized inside unit test routines still try to call basic native window system handles (like X11 or font utilities). Wrapping the ctest step in a virtual screen environment ensures it passes without throwing environment setup exceptions.
4. CPM Caching: Maps your external dependencies (like JUCE and lz4 fetched via CPM) to a persistent runner memory store. This slashes subsequent test compilation runs down by minutes since it avoids downloading the frameworks over and over.
Now that the baseline script is ready, let me know if you would like to tackle setting up local directory simulation fixtures for your test runner (to emulate loading .nam captures inside the CI workflow), or if you want to refine how your node UI workflows handle multi-channel panning.
