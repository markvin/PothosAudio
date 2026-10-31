// Copyright (c) 2014-2016 Josh Blum
//                    2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#pragma once
#include <Pothos/Framework.hpp>
#include <miniaudio.h>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>

/*!
 * The common part of the audio source and sink: a miniaudio context, one
 * device and a ring buffer between the device callback and work().
 * The callback does no allocation, takes no lock and does not log.
 */
class AudioBlock : public Pothos::Block
{
public:
    AudioBlock(const std::string &blockName, const bool isSink, const Pothos::DType &dtype, const size_t numChans, const std::string &chanMode);
    ~AudioBlock(void);

    std::string overlay(void) const;

    void setupDevice(const std::string &deviceName);
    void setupStream(const double sampRate);

    void setReportMode(const std::string &mode);
    void setBackoffTime(const long backoff);

    void activate(void);
    void deactivate(void);

    //! Stop the device as an unplug would (for the self-tests only, not a block call)
    void stopDeviceForTest(void);

protected:
    //! Wait at most a short time for frames to read (source) or space to write (sink)
    size_t waitAvailable(void);

    //! Move frames from the ring buffer to the output ports (source)
    void readFrames(const size_t numFrames);

    //! Move frames from the input ports to the ring buffer (sink)
    void writeFrames(const size_t numFrames);

    //! Report an overflow or underflow: "aO"/"aU" to stderr or the logger, and start the backoff
    void reportXrun(const char *shortText, const std::string &message);

    //! True while the backoff time after an overflow or underflow is not over
    bool inBackoff(void) const;

    //! True when the device stopped while the block is active; logs it once
    bool deviceStopped(void);

    const std::string _blockName;
    const bool _isSink;
    const size_t _numChans;
    const bool _interleaved;
    const bool _isInt8;
    const ma_format _format;
    const size_t _sampleSize;
    double _sampleRate;
    bool _sendLabel;
    bool _reportLogger;
    bool _reportStderror;
    std::chrono::steady_clock::duration _backoffTime;
    std::chrono::steady_clock::time_point _readyTime;

    //written by the device callback, read by work()
    std::atomic<bool> _xrun;
    std::atomic<unsigned long long> _xrunFrames;
    std::atomic<bool> _dataWritten; //sink: data came after activate()
    std::atomic<bool> _running; //between activate() and deactivate()
    std::atomic<bool> _deviceStopped; //the device stopped without deactivate()
    bool _stopReported;
    std::string _deviceName;

private:
    static void dataCallback(ma_device *device, void *output, const void *input, ma_uint32 frameCount);
    static void notificationCallback(const ma_device_notification *notification);
    void captureCallback(const void *input, ma_uint32 frameCount);
    void playbackCallback(void *output, ma_uint32 frameCount);
    void closeStream(void);

    ma_context _context;
    std::optional<ma_device_id> _deviceId;
    bool _xrunActive; //used only by the device callback: one report for each event
    bool _primed; //sink, used only by the device callback: the pre-fill is done
    unsigned _primeWaits; //sink, used only by the device callback: callbacks that waited for the pre-fill
    size_t _prefillFrames; //sink: frames in the ring buffer before the playback starts
    ma_device _device;
    bool _deviceInit;
    ma_pcm_rb _ringBuffer;
    bool _ringBufferInit;
};
