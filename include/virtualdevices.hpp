#ifndef VIRTUALDEVICES_H
#define VIRTUALDEVICES_H
#pragma once

#include <pulse/pulseaudio.h>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

struct WAVHeader {
    char riff_header[4];     // Contains "RIFF"
    uint32_t wav_size;       // Size of entire file minus 8 bytes
    char wave_header[4];     // Contains "WAVE"
    char fmt_header[4];      // Contains "fmt "
    uint32_t fmt_chunk_size; // Size of format chunk (usually 16)
    uint16_t audio_format;   // 1 = Uncompressed PCM
    uint16_t num_channels;   // 1 = Mono, 2 = Stereo
    uint32_t sample_rate;    // e.g., 44100, 48000
    uint32_t byte_rate;      // sample_rate * num_channels * (bits_per_sample / 8)
    uint16_t sample_alignment;
    uint16_t bits_per_sample;// 8, 16, or 32 bits
    char data_header[4];     // Contains "data"
    uint32_t data_bytes;     // Total size of raw audio data payload
};

class VirtualSink
{
public:
    VirtualSink(pa_mainloop*, pa_context*);
    ~VirtualSink();

    static constexpr std::string_view sink_name{"soundboard-sink"};
    bool link_source(std::string source);
    void play_wav(const fs::path& path);
private:
    pa_mainloop* _mainloop = nullptr;
    pa_context* _context = nullptr;
    uint32_t _module_index = PA_INVALID_INDEX;

};

class VirtualSource
{
public:
    VirtualSource(pa_mainloop*, pa_context*);
    ~VirtualSource();

    static constexpr std::string_view source_name{"soundboard-microphone"};
    bool link_sink(std::string sink);

private:
    pa_mainloop* _mainloop = nullptr;
    pa_context* _context = nullptr;
    uint32_t _module_index = PA_INVALID_INDEX;

};

void wait_for_operation(pa_mainloop* mainloop, pa_operation* op);

#endif