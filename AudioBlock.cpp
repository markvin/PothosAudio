// Copyright (c) 2014-2018 Josh Blum
//                    2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include "AudioBlock.hpp"
#include "AudioHelpers.hpp"
#include <Pothos/System/Logger.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <thread>

using json = nlohmann::json;
using namespace PothosAudio;

//the wait of work() for data or space, then it yields
static const auto MAX_WORK_WAIT = std::chrono::milliseconds(10);

AudioBlock::AudioBlock(const std::string &blockName, const bool isSink, const Pothos::DType &dtype, const size_t numChans, const std::string &chanMode):
    _blockName(blockName),
    _isSink(isSink),
    _numChans(numChans),
    _interleaved(chanMode == "INTERLEAVED"),
    _isInt8(dtype == Pothos::DType("int8")),
    _format(formatFromDType(dtype)),
    _sampleSize(ma_get_bytes_per_sample(_format)),
    _sampleRate(0.0),
    _sendLabel(false),
    _reportLogger(false),
    _reportStderror(true),
    _backoffTime(0),
    _xrun(false),
    _xrunFrames(0),
    _dataWritten(false),
    _running(false),
    _deviceStopped(false),
    _stopReported(false),
    _xrunActive(false),
    _device{},
    _deviceInit(false),
    _ringBuffer{},
    _ringBufferInit(false)
{
    this->registerCall(this, POTHOS_FCN_TUPLE(AudioBlock, overlay));
    this->registerCall(this, POTHOS_FCN_TUPLE(AudioBlock, setupDevice));
    this->registerCall(this, POTHOS_FCN_TUPLE(AudioBlock, setupStream));
    this->registerCall(this, POTHOS_FCN_TUPLE(AudioBlock, setReportMode));
    this->registerCall(this, POTHOS_FCN_TUPLE(AudioBlock, setBackoffTime));

    if (numChans == 0) throw Pothos::InvalidArgumentException("AudioBlock()", "the number of channels is 0");

    const auto backends = contextBackends();
    const ma_result result = ma_context_init(backends.data(), ma_uint32(backends.size()), nullptr, &_context);
    if (result != MA_SUCCESS)
    {
        throw Pothos::Exception("AudioBlock()", std::string("ma_context_init: ") + ma_result_description(result));
    }
}

AudioBlock::~AudioBlock(void)
{
    this->closeStream();
    ma_context_uninit(&_context);
}

void AudioBlock::closeStream(void)
{
    if (_deviceInit) ma_device_uninit(&_device);
    _deviceInit = false;
    if (_ringBufferInit) ma_pcm_rb_uninit(&_ringBuffer);
    _ringBufferInit = false;
}

std::string AudioBlock::overlay(void) const
{
    json topObj;
    json params;

    json options;
    json deviceNameParam;
    deviceNameParam["key"] = "deviceName";

    //editable drop down for user-controlled input
    deviceNameParam["widgetKwargs"]["editable"] = true;
    deviceNameParam["widgetType"] = "ComboBox";

    //a default option for empty/unspecified device
    json defaultOption;
    defaultOption["name"] = "Default Device";
    defaultOption["value"] = "\"\"";
    options.push_back(defaultOption);

    //enumerate devices and add to the options list
    ma_device_info *playback = nullptr, *capture = nullptr;
    ma_uint32 numPlayback = 0, numCapture = 0;
    auto context = const_cast<ma_context *>(&_context);
    if (ma_context_get_devices(context, &playback, &numPlayback, &capture, &numCapture) == MA_SUCCESS)
    {
        const auto devices = _isSink? playback : capture;
        const auto numDevices = _isSink? numPlayback : numCapture;
        for (ma_uint32 i = 0; i < numDevices; i++)
        {
            json option;
            const std::string deviceName(devices[i].name);
            option["name"] = deviceName;
            option["value"] = "\""+deviceName+"\"";
            options.push_back(option);
        }
    }

    deviceNameParam["options"] = options;
    params.push_back(deviceNameParam);
    topObj["params"] = params;

    return topObj.dump();
}

void AudioBlock::setupDevice(const std::string &deviceName)
{
    _deviceId = findDevice(_context, _isSink? ma_device_type_playback : ma_device_type_capture, deviceName, _deviceName);
}

void AudioBlock::setupStream(const double sampRate)
{
    this->closeStream();

    //the device converts the format, the channels and the rate to the request
    ma_device_config config = ma_device_config_init(_isSink? ma_device_type_playback : ma_device_type_capture);
    const ma_device_id *deviceId = _deviceId? &(*_deviceId) : nullptr;
    if (_isSink)
    {
        config.playback.pDeviceID = deviceId;
        config.playback.format = _format;
        config.playback.channels = ma_uint32(_numChans);
    }
    else
    {
        config.capture.pDeviceID = deviceId;
        config.capture.format = _format;
        config.capture.channels = ma_uint32(_numChans);
    }
    config.sampleRate = ma_uint32(sampRate);
    config.dataCallback = &AudioBlock::dataCallback;
    config.notificationCallback = &AudioBlock::notificationCallback;
    config.pUserData = this;

    ma_result result = ma_device_init(&_context, &config, &_device);
    if (result != MA_SUCCESS)
    {
        throw Pothos::Exception("AudioBlock::setupStream()", std::string("ma_device_init: ") + ma_result_description(result));
    }
    _deviceInit = true;
    _sampleRate = double(_device.sampleRate);

    //the device side of the stream
    const std::string name(_isSink? _device.playback.name : _device.capture.name);
    _deviceName = name;
    const auto internalFormat = _isSink? _device.playback.internalFormat : _device.capture.internalFormat;
    const auto internalChannels = _isSink? _device.playback.internalChannels : _device.capture.internalChannels;
    const auto internalRate = _isSink? _device.playback.internalSampleRate : _device.capture.internalSampleRate;
    const auto internalPeriod = _isSink? _device.playback.internalPeriodSizeInFrames : _device.capture.internalPeriodSizeInFrames;

    POTHOS_LOG_INFORMATION(_blockName, "Using {} through {}", name, ma_get_backend_name(_context.backend));
    if (internalFormat != _format or internalChannels != _numChans or internalRate != _device.sampleRate)
    {
        POTHOS_LOG_INFORMATION(_blockName, "The device converts: {} {} channels {} Hz (device) to {} {} channels {} Hz",
            ma_get_format_name(internalFormat), internalChannels, internalRate,
            ma_get_format_name(_format), _numChans, _device.sampleRate);
    }

    //the ring buffer: about 100 ms, at least 4 periods and 2 blocking reads
    const size_t period = size_t(internalPeriod) * _device.sampleRate / std::max<ma_uint32>(internalRate, 1);
    const size_t numFrames = std::max({size_t(_device.sampleRate/10), 4*period, size_t(2*MIN_FRAMES_BLOCKING)});
    result = ma_pcm_rb_init(_format, ma_uint32(_numChans), ma_uint32(numFrames), nullptr, nullptr, &_ringBuffer);
    if (result != MA_SUCCESS)
    {
        this->closeStream();
        throw Pothos::Exception("AudioBlock::setupStream()", std::string("ma_pcm_rb_init: ") + ma_result_description(result));
    }
    _ringBufferInit = true;
}

void AudioBlock::setReportMode(const std::string &mode)
{
    if (mode == "LOGGER"){}
    else if (mode == "STDERROR"){}
    else if (mode == "DISABLED"){}
    else throw Pothos::InvalidArgumentException(
        "AudioBlock::setReportMode("+mode+")", "unknown report mode");
    _reportLogger = (mode == "LOGGER");
    _reportStderror = (mode == "STDERROR");
}

void AudioBlock::setBackoffTime(const long backoff)
{
    _backoffTime = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(backoff));
}

void AudioBlock::activate(void)
{
    if (not _deviceInit) throw Pothos::Exception("AudioBlock::activate()", "setupStream() was not called");
    ma_pcm_rb_reset(&_ringBuffer);
    _xrun = false;
    _xrunFrames = 0;
    _dataWritten = false;
    _xrunActive = false;
    _deviceStopped = false;
    _stopReported = false;
    _readyTime = std::chrono::steady_clock::now();
    _running = true; //a stop notification from now on is a failure
    const ma_result result = ma_device_start(&_device);
    if (result != MA_SUCCESS)
    {
        throw Pothos::Exception("AudioBlock::activate()", std::string("ma_device_start: ") + ma_result_description(result));
    }
    _sendLabel = true;
}

void AudioBlock::deactivate(void)
{
    _running = false; //this stop is not a failure
    const ma_result result = ma_device_stop(&_device);
    if (result != MA_SUCCESS)
    {
        throw Pothos::Exception("AudioBlock::deactivate()", std::string("ma_device_stop: ") + ma_result_description(result));
    }
}

void AudioBlock::stopDeviceForTest(void)
{
    ma_device_stop(&_device);
}

/***********************************************************************
 * Device callback: no allocation, no lock, no log
 **********************************************************************/
void AudioBlock::dataCallback(ma_device *device, void *output, const void *input, ma_uint32 frameCount)
{
    auto self = static_cast<AudioBlock *>(device->pUserData);
    if (self->_isSink) self->playbackCallback(output, frameCount);
    else self->captureCallback(input, frameCount);
}

void AudioBlock::notificationCallback(const ma_device_notification *notification)
{
    auto self = static_cast<AudioBlock *>(notification->pDevice->pUserData);
    if (notification->type == ma_device_notification_type_stopped and self->_running) self->_deviceStopped = true;
}

void AudioBlock::captureCallback(const void *input, ma_uint32 frameCount)
{
    auto src = static_cast<const char *>(input);
    const size_t frameSize = _sampleSize*_numChans;
    while (frameCount != 0)
    {
        ma_uint32 numFrames = frameCount;
        void *dst = nullptr;
        if (ma_pcm_rb_acquire_write(&_ringBuffer, &numFrames, &dst) != MA_SUCCESS or numFrames == 0) break;
        std::copy(src, src + numFrames*frameSize, static_cast<char *>(dst));
        ma_pcm_rb_commit_write(&_ringBuffer, numFrames);
        src += numFrames*frameSize;
        frameCount -= numFrames;
    }

    //a full buffer: the frames are dropped (an overflow), one report for each event
    if (frameCount != 0)
    {
        _xrunFrames += frameCount;
        if (not _xrunActive) _xrun = true;
        _xrunActive = true;
    }
    else _xrunActive = false;
}

void AudioBlock::playbackCallback(void *output, ma_uint32 frameCount)
{
    auto dst = static_cast<char *>(output);
    const size_t frameSize = _sampleSize*_numChans;
    while (frameCount != 0)
    {
        ma_uint32 numFrames = frameCount;
        void *src = nullptr;
        if (ma_pcm_rb_acquire_read(&_ringBuffer, &numFrames, &src) != MA_SUCCESS or numFrames == 0) break;
        const auto begin = static_cast<const char *>(src);
        std::copy(begin, begin + numFrames*frameSize, dst);
        ma_pcm_rb_commit_read(&_ringBuffer, numFrames);
        dst += numFrames*frameSize;
        frameCount -= numFrames;
    }

    //an empty buffer: silence for the missing frames (an underflow after the first data)
    if (frameCount != 0)
    {
        ma_silence_pcm_frames(dst, frameCount, _format, ma_uint32(_numChans));
        if (_dataWritten)
        {
            _xrunFrames += frameCount;
            if (not _xrunActive) _xrun = true;
            _xrunActive = true;
        }
    }
    else _xrunActive = false;
}

/***********************************************************************
 * Helpers of work()
 **********************************************************************/
size_t AudioBlock::waitAvailable(void)
{
    const auto deadline = std::chrono::steady_clock::now() + MAX_WORK_WAIT;
    while (true)
    {
        const size_t available = _isSink?
            ma_pcm_rb_available_write(&_ringBuffer) : ma_pcm_rb_available_read(&_ringBuffer);
        if (available != 0 or std::chrono::steady_clock::now() >= deadline) return available;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void AudioBlock::readFrames(const size_t numFrames)
{
    const size_t frameSize = _sampleSize*_numChans;
    const auto &outs = this->workInfo().outputPointers;
    size_t done = 0;
    while (done < numFrames)
    {
        ma_uint32 chunk = ma_uint32(numFrames - done);
        void *src = nullptr;
        if (ma_pcm_rb_acquire_read(&_ringBuffer, &chunk, &src) != MA_SUCCESS or chunk == 0) break;
        if (_interleaved)
        {
            const auto begin = static_cast<const char *>(src);
            std::copy(begin, begin + chunk*frameSize, static_cast<char *>(outs[0]) + done*frameSize);
        }
        else
        {
            std::vector<void *> dsts(_numChans);
            for (size_t ch = 0; ch < _numChans; ch++) dsts[ch] = static_cast<char *>(outs[ch]) + done*_sampleSize;
            deinterleave(src, dsts, chunk, _numChans, _sampleSize);
        }
        ma_pcm_rb_commit_read(&_ringBuffer, chunk);
        done += chunk;
    }

    //miniaudio has no signed 8-bit format: the device data is unsigned
    if (_isInt8)
    {
        const size_t numPorts = _interleaved? 1 : _numChans;
        const size_t perPort = _interleaved? numFrames*_numChans : numFrames;
        for (size_t p = 0; p < numPorts; p++)
        {
            auto samples = static_cast<std::uint8_t *>(outs[p]);
            for (size_t i = 0; i < perPort; i++) samples[i] = std::uint8_t(u8ToInt8(samples[i]));
        }
    }
}

void AudioBlock::writeFrames(const size_t numFrames)
{
    const size_t frameSize = _sampleSize*_numChans;
    const auto &ins = this->workInfo().inputPointers;
    size_t done = 0;
    while (done < numFrames)
    {
        ma_uint32 chunk = ma_uint32(numFrames - done);
        void *dst = nullptr;
        if (ma_pcm_rb_acquire_write(&_ringBuffer, &chunk, &dst) != MA_SUCCESS or chunk == 0) break;
        if (_interleaved)
        {
            const auto begin = static_cast<const char *>(ins[0]) + done*frameSize;
            std::copy(begin, begin + chunk*frameSize, static_cast<char *>(dst));
        }
        else
        {
            std::vector<const void *> srcs(_numChans);
            for (size_t ch = 0; ch < _numChans; ch++) srcs[ch] = static_cast<const char *>(ins[ch]) + done*_sampleSize;
            interleave(srcs, dst, chunk, _numChans, _sampleSize);
        }

        //miniaudio has no signed 8-bit format: convert in the ring buffer
        if (_isInt8)
        {
            auto samples = static_cast<std::uint8_t *>(dst);
            for (size_t i = 0; i < chunk*_numChans; i++) samples[i] = int8ToU8(std::int8_t(samples[i]));
        }
        ma_pcm_rb_commit_write(&_ringBuffer, chunk);
        done += chunk;
    }
    _dataWritten = true;
}

void AudioBlock::reportXrun(const char *shortText, const std::string &message)
{
    _readyTime = std::chrono::steady_clock::now() + _backoffTime;
    if (_reportStderror) std::cerr << shortText << std::flush;
    if (_reportLogger) POTHOS_LOG_ERROR(_blockName, "{}", message);
}

bool AudioBlock::deviceStopped(void)
{
    if (not _deviceStopped) return false;
    if (not _stopReported)
    {
        _stopReported = true;
        POTHOS_LOG_ERROR(_blockName, "The audio device {} stopped (for example it was removed)", _deviceName);
    }
    return true;
}

bool AudioBlock::inBackoff(void) const
{
    return _readyTime > std::chrono::steady_clock::now();
}
