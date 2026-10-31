// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

//Helpers of the audio blocks: the miniaudio backend, the device lookup,
//the sample formats and the channel layout

#pragma once
#include <Pothos/Framework/DType.hpp>
#include <miniaudio.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace PothosAudio {

/*!
 * The backend for a miniaudio backend name ("null", "alsa", "pulseaudio",
 * "jack", "coreaudio", "wasapi", ...): the display name of miniaudio in any
 * case, without spaces and punctuation.
 * \throws Pothos::InvalidArgumentException for an unknown name
 */
ma_backend backendFromName(const std::string &name);

/*!
 * The backends for ma_context_init(): the backend of the environment
 * variable POTHOS_AUDIO_BACKEND, or else the default order of miniaudio
 * without the null and custom backends.
 * \throws Pothos::InvalidArgumentException for an unknown name
 */
std::vector<ma_backend> contextBackends(void);

/*!
 * The miniaudio format for a block data type. int8 uses ma_format_u8
 * (miniaudio has no signed 8-bit format): convert with int8ToU8/u8ToInt8.
 * \throws Pothos::InvalidArgumentException for another type
 */
ma_format formatFromDType(const Pothos::DType &dtype);

//! A signed 8-bit sample as an unsigned 8-bit sample (offset binary)
inline std::uint8_t int8ToU8(const std::int8_t x)
{
    return std::uint8_t(x) ^ 0x80;
}

//! An unsigned 8-bit sample as a signed 8-bit sample
inline std::int8_t u8ToInt8(const std::uint8_t x)
{
    return std::int8_t(x ^ 0x80);
}

//! Copy interleaved frames into one buffer per channel
void deinterleave(const void *in, const std::vector<void *> &outs, size_t numFrames, size_t numChans, size_t sampleSize);

//! Copy one buffer per channel into interleaved frames
void interleave(const std::vector<const void *> &ins, void *out, size_t numFrames, size_t numChans, size_t sampleSize);

/*!
 * Find a device of the context.
 * \param name empty: the default device (no id); digits: the index in the
 * capture or playback list; else the device name
 * \param [out] foundName the name of the device
 * \throws Pothos::NotFoundException no device, or no device with the name
 * \throws Pothos::RangeException the index is out of range
 */
std::optional<ma_device_id> findDevice(ma_context &ctx, const ma_device_type type, const std::string &name, std::string &foundName);

//! The different native sample rates of a device, in order (0 means "any rate": not in the list)
std::vector<ma_uint32> nativeSampleRates(const ma_device_info &info);

} //namespace PothosAudio
