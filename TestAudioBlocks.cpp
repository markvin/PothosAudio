// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

//Self-tests of the audio blocks on the null backend of miniaudio
//(the test runner sets POTHOS_AUDIO_BACKEND=null): no audio hardware

#include "AudioBlock.hpp"
#include <Pothos/Testing.hpp>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>
#include <Pothos/Plugin.hpp>
#include <Pothos/System/Logger.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using Pothos::System::Logger;
using Pothos::System::LogLevel;
using Pothos::System::LogRecord;

static const std::vector<std::string> allTypes{"float32", "int32", "int16", "int8", "uint8"};

struct ChannelSetup
{
    size_t numChans;
    const char *mode;
};
static const std::vector<ChannelSetup> allSetups{{1, "INTERLEAVED"}, {2, "INTERLEAVED"}, {2, "PORTPERCHAN"}};

static Pothos::Proxy makeAudioBlock(const std::string &path, const std::string &dtype,
    const size_t numChans, const std::string &mode, const double rate, const std::string &reportMode = "DISABLED")
{
    auto block = Pothos::BlockRegistry::make(path, Pothos::DType(dtype), numChans, mode);
    block.call("setupDevice", std::string());
    block.call("setupStream", rate);
    block.call("setReportMode", reportMode);
    return block;
}

//the port type of a block: a vector type in the interleaved mode
static Pothos::DType portType(const std::string &dtype, const ChannelSetup &setup)
{
    if (std::string(setup.mode) == "INTERLEAVED") return Pothos::DType::fromDType(Pothos::DType(dtype), setup.numChans);
    return Pothos::DType(dtype);
}

static size_t numPorts(const ChannelSetup &setup)
{
    return (std::string(setup.mode) == "INTERLEAVED")? 1 : setup.numChans;
}

//record the log messages at Error level or worse from one source
struct ErrorRecords
{
    ErrorRecords(const std::string &source):
        source(source),
        id(Logger::addListener([this](const LogRecord &r)
        {
            if (r.level > LogLevel::Error or r.source != this->source) return;
            std::lock_guard<std::mutex> lock(mutex);
            records.push_back(r);
        }))
    {
        return;
    }
    ~ErrorRecords(void)
    {
        Logger::removeListener(id);
    }
    std::vector<LogRecord> get(void)
    {
        Logger::flush();
        std::lock_guard<std::mutex> lock(mutex);
        return records;
    }
    const std::string source;
    std::mutex mutex;
    std::vector<LogRecord> records;
    const size_t id;
};

//a call through the proxy gives Pothos::ProxyExceptionMessage: check its text
static void expectCallError(Pothos::Proxy &block, const std::string &name, const std::string &arg, const std::string &textA, const std::string &textB)
{
    try
    {
        block.call(name, arg);
    }
    catch (const Pothos::ProxyExceptionMessage &ex)
    {
        std::cout << "expected error: " << ex.message() << std::endl;
        POTHOS_TEST_TRUE(ex.message().find(textA) != std::string::npos);
        POTHOS_TEST_TRUE(ex.message().find(textB) != std::string::npos);
        return;
    }
    POTHOS_TEST_TRUE(false); //no exception
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_info)
{
    auto plugin = Pothos::PluginRegistry::get("/devices/audio/info");
    const auto call = plugin.getObject().extract<Pothos::Callable>();
    const auto info = json::parse(call.call<std::string>());
    POTHOS_TEST_TRUE(info.contains("miniaudio Version"));
    POTHOS_TEST_TRUE(info.at("Audio Device").is_array());
    POTHOS_TEST_TRUE(info.at("Audio Device").size() >= 1);
    POTHOS_TEST_TRUE(info.at("Audio Device").at(0).contains("Device Name"));
    POTHOS_TEST_TRUE(info.at("Audio Device").at(0).contains("Backend Name"));
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_overlay)
{
    auto source = Pothos::BlockRegistry::make("/audio/source", Pothos::DType("float32"), 1, "INTERLEAVED");
    const auto overlay = json::parse(source.call<std::string>("overlay"));
    POTHOS_TEST_EQUAL(overlay.at("params").at(0).at("options").at(0).at("name").get<std::string>(), "Default Device");
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_device_select)
{
    auto source = Pothos::BlockRegistry::make("/audio/source", Pothos::DType("float32"), 1, "INTERLEAVED");
    source.call("setupDevice", std::string());
    source.call("setupDevice", std::string("0"));
    const auto overlay = json::parse(source.call<std::string>("overlay"));
    const auto firstName = overlay.at("params").at(0).at("options").at(1).at("name").get<std::string>();
    source.call("setupDevice", firstName);
    expectCallError(source, "setupDevice", "999", "Out of range", "999");
    expectCallError(source, "setupDevice", "hw:0,0", "Not found", "hw:0,0");
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_report_mode)
{
    auto sink = Pothos::BlockRegistry::make("/audio/sink", Pothos::DType("float32"), 1, "INTERLEAVED");
    sink.call("setReportMode", std::string("LOGGER"));
    expectCallError(sink, "setReportMode", "FOO", "Invalid argument", "FOO");
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_source_formats)
{
    for (const auto &dtype : allTypes)
    for (const auto &setup : allSetups)
    {
        std::cout << "source " << dtype << " " << setup.numChans << " " << setup.mode << std::endl;
        auto source = makeAudioBlock("/audio/source", dtype, setup.numChans, setup.mode, 44.1e3);
        std::vector<Pothos::Proxy> collectors;
        {
            Pothos::Topology topology;
            for (size_t i = 0; i < numPorts(setup); i++)
            {
                collectors.push_back(Pothos::BlockRegistry::make("/blocks/collector_sink", portType(dtype, setup)));
                topology.connect(source, i, collectors.back(), 0);
            }
            topology.commit();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        for (auto &collector : collectors)
        {
            const auto buffer = collector.call<Pothos::BufferChunk>("getBuffer");
            POTHOS_TEST_TRUE(buffer.length > 0);
            POTHOS_TEST_TRUE(buffer.dtype == portType(dtype, setup));
            const auto labels = collector.call<std::vector<Pothos::Label>>("getLabels");
            POTHOS_TEST_TRUE(not labels.empty());
            POTHOS_TEST_EQUAL(labels.at(0).id, "rxRate");
            POTHOS_TEST_EQUAL(labels.at(0).data.convert<double>(), 44.1e3);
        }
    }
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_sink_formats)
{
    static const size_t numFrames = 4410;
    for (const auto &dtype : allTypes)
    for (const auto &setup : allSetups)
    {
        std::cout << "sink " << dtype << " " << setup.numChans << " " << setup.mode << std::endl;
        auto sink = makeAudioBlock("/audio/sink", dtype, setup.numChans, setup.mode, 44.1e3);
        Pothos::Topology topology;
        for (size_t i = 0; i < numPorts(setup); i++)
        {
            auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", portType(dtype, setup));
            feeder.call("feedBuffer", Pothos::BufferChunk(portType(dtype, setup), numFrames));
            topology.connect(feeder, 0, sink, i);
        }
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive(0.2, 5.0));
        auto block = sink.call<Pothos::Block *>("getPointer");
        for (size_t i = 0; i < numPorts(setup); i++)
        {
            POTHOS_TEST_EQUAL(block->input(i)->totalElements(), numFrames);
        }
    }
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_sink_start_no_underflow)
{
    ErrorRecords errors("AudioSink");
    auto sink = makeAudioBlock("/audio/sink", "float32", 1, "INTERLEAVED", 44.1e3, "LOGGER");
    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", Pothos::DType("float32"));
    Pothos::Topology topology;
    topology.connect(feeder, 0, sink, 0);
    topology.commit();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto feedTime = std::chrono::system_clock::now();
    feeder.call("feedBuffer", Pothos::BufferChunk(Pothos::DType("float32"), 4410));
    POTHOS_TEST_TRUE(topology.waitInactive(0.2, 5.0));
    for (const auto &r : errors.get())
    {
        if (r.time < feedTime) std::cout << "early error: " << r.text << std::endl;
        POTHOS_TEST_TRUE(r.time >= feedTime);
    }
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_source_overflow)
{
    ErrorRecords errors("AudioSource");
    auto source = makeAudioBlock("/audio/source", "float32", 1, "INTERLEAVED", 48e3, "LOGGER");
    auto pacer = Pothos::BlockRegistry::make("/blocks/pacer");
    pacer.call("setRate", 1000.0); //much slower than the device
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", Pothos::DType("float32"));
    Pothos::Topology topology;
    topology.connect(source, 0, pacer, 0);
    topology.connect(pacer, 0, collector, 0);
    topology.commit();
    //the pacer holds one buffer for about 2 s: the source reports the
    //overflow when it gets output space again
    bool overflow = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (not overflow and std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (const auto &r : errors.get())
        {
            if (r.text.find("overflow") != std::string::npos) overflow = true;
        }
    }
    POTHOS_TEST_TRUE(overflow);

    //the graph continues: data after the overflow
    pacer.call("setRate", 1e9);
    const auto before = collector.call<Pothos::BufferChunk>("getBuffer").length;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    POTHOS_TEST_TRUE(collector.call<Pothos::BufferChunk>("getBuffer").length > before);
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_stop_without_data)
{
    auto source = makeAudioBlock("/audio/source", "int16", 2, "INTERLEAVED", 48e3);
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", Pothos::DType::fromDType(Pothos::DType("int16"), 2));
    const auto start = std::chrono::steady_clock::now();
    {
        Pothos::Topology topology;
        topology.connect(source, 0, collector, 0);
        topology.commit();
    }
    POTHOS_TEST_TRUE(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_loop_graph)
{
    ErrorRecords sourceErrors("AudioSource"), sinkErrors("AudioSink");
    auto source = makeAudioBlock("/audio/source", "float32", 2, "INTERLEAVED", 48e3, "LOGGER");
    auto sink = makeAudioBlock("/audio/sink", "float32", 2, "INTERLEAVED", 48e3, "LOGGER");
    {
        Pothos::Topology topology;
        topology.connect(source, 0, sink, 0);
        topology.commit();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    POTHOS_TEST_TRUE(source.call<Pothos::Block *>("getPointer")->output(0)->totalElements() > 0);
    POTHOS_TEST_EQUAL(sourceErrors.get().size(), 0u);
    POTHOS_TEST_EQUAL(sinkErrors.get().size(), 0u);
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_sink_backoff_recovers)
{
    //an underflow with a backoff time: the sink plays the later data
    static const size_t numFrames = 4410;
    auto sink = makeAudioBlock("/audio/sink", "float32", 1, "INTERLEAVED", 44.1e3);
    sink.call("setBackoffTime", 20L);
    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", Pothos::DType("float32"));
    Pothos::Topology topology;
    topology.connect(feeder, 0, sink, 0);
    topology.commit();
    feeder.call("feedBuffer", Pothos::BufferChunk(Pothos::DType("float32"), numFrames));
    POTHOS_TEST_TRUE(topology.waitInactive(0.2, 5.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); //underflows
    feeder.call("feedBuffer", Pothos::BufferChunk(Pothos::DType("float32"), numFrames));
    POTHOS_TEST_TRUE(topology.waitInactive(0.2, 5.0));
    POTHOS_TEST_EQUAL(sink.call<Pothos::Block *>("getPointer")->input(0)->totalElements(), 2*numFrames);
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_device_stopped)
{
    //a device that stops while the graph runs (an unplug, a backend error): an error
    ErrorRecords errors("AudioSource");
    auto source = makeAudioBlock("/audio/source", "float32", 1, "INTERLEAVED", 48e3);
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", Pothos::DType("float32"));
    Pothos::Topology topology;
    topology.connect(source, 0, collector, 0);
    topology.commit();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto block = dynamic_cast<AudioBlock *>(source.call<Pothos::Block *>("getPointer"));
    POTHOS_TEST_TRUE(block != nullptr);
    block->stopDeviceForTest();
    bool reported = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (not reported and std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (const auto &r : errors.get())
        {
            if (r.text.find("stopped") != std::string::npos) reported = true;
        }
    }
    POTHOS_TEST_TRUE(reported);
}
