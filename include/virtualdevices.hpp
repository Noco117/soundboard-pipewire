#ifndef VIRTUALDEVICES_H
#define VIRTUALDEVICES_H
#pragma once

#include <pulse/pulseaudio.h>
#include <string>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <vector>

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
};

struct WAVChunkHeader {
    char header[4]; // Name of the Chunk 
    uint32_t size; // Size of the Chunk in bytes
};

class VirtualSink
{
public:
    VirtualSink(pa_threaded_mainloop*, pa_context*);
    ~VirtualSink();

    static constexpr std::string_view sink_name{"soundboard-sink"};
    void set_volume(float volume);

    bool link_source(std::string source);
    void play_wav(const fs::path& path, float vol);

    void stop_playback();
    void stop_playback_by_path(const fs::path& path);
private:
    struct PlaybackInstance;

    void play_wav_sync(const fs::path& path, std::shared_ptr<std::atomic<bool>> abort_flag, float vol);
    
    std::vector<PlaybackInstance> _playback_instances;
    std::mutex _thread_mutex;
    
    pa_threaded_mainloop* _threaded_mainloop = nullptr;
    pa_context* _context = nullptr;
    uint32_t _module_index = PA_INVALID_INDEX;

    uint32_t _loopback_index = PA_INVALID_INDEX; 

};

class VirtualSource
{
public:
    VirtualSource(pa_threaded_mainloop*, pa_context*);
    ~VirtualSource();

    static constexpr std::string_view source_name{"soundboard-microphone"};
    bool link_sink(std::string sink);
    bool link_source(std::string source);
    bool unlink_sink(std::string sink);
    bool unlink_source(std::string source);

private:
    pa_threaded_mainloop* _threaded_mainloop = nullptr;
    pa_context* _context = nullptr;
    uint32_t _module_index = PA_INVALID_INDEX;
};

void wait_for_operation(pa_threaded_mainloop* threaded_mainloop, pa_operation* op);

#endif