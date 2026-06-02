#include "virtualdevices.hpp"
#include <cstdint>
#include <pulse/operation.h>
#include <pulse/pulseaudio.h>
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <format>
#include <fstream>
#include <thread>
#include <vector>

using namespace std;

void wait_for_operation(pa_mainloop *mainloop, pa_operation *op){
    if (!op) return;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING){
        pa_mainloop_iterate(mainloop, 1, nullptr);
    }
    pa_operation_unref(op);
}

pair<string, int> run_command(string command){
    array<char, 128> buffer;
    string result;

    unique_ptr<FILE, int(*)(FILE*)> pipe(
        popen(command.c_str(), "r"),
        &pclose
    );

    if (!pipe) throw runtime_error(format("Command: [{}] could not be executed by popen()", command));
    
    // read the stream from the command using the buffer
    while(fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr){
        result.append(buffer.data());
    }

    // Call the lambda deleter explicitly on release
    int raw_status = pipe.get_deleter()(pipe.release());
    int exit_code = WEXITSTATUS(raw_status);

    return {result, exit_code};
}

VirtualSink::VirtualSink(pa_mainloop* mainloop, pa_context* context) : _mainloop(mainloop), _context(context)
{
    string module_args = format("media.class=Audio/Sink sink_name={} channel_map=stereo", sink_name);
    auto cb = [](pa_context*, uint32_t idx, void* userdata){
        auto* index_ptr = static_cast<uint32_t*>(userdata);
        *index_ptr = idx;
    };

    pa_operation* loadVirtualSink = pa_context_load_module(_context, "module-null-sink", module_args.c_str(), cb, &_module_index);

    wait_for_operation(_mainloop, loadVirtualSink);

    if (_module_index == PA_INVALID_INDEX){
        throw runtime_error("Failed to load virtual sink module.");
    }

    cout << "Successfully created Virtual Sink with Module ID: " << _module_index << endl;
}

bool VirtualSink::link_source(string source_name){
    const std::array<string, 2> directions = {"FL", "FR"};
    for (const auto& drc : directions){
        string command = format("pw-link {}:capture_{} {}:playback_{}", source_name, drc, sink_name, drc);
        
        auto cmdreturn = run_command(command);
        if(cmdreturn.second != 0){
            cerr << cmdreturn.first << endl;
            return false;
        }
    }
    return true;
}

void VirtualSink::play_wav(const fs::path& path){
    ifstream wav_file(path, std::ios::binary);
    if (!wav_file.is_open()) {
        std::cerr << "Error: Could not open WAV file: " << path << std::endl;
        return;
    }

    WAVHeader header;
    wav_file.read(reinterpret_cast<char*>(&header), sizeof(WAVHeader));

    if (string(header.riff_header, 4) != "RIFF" || string(header.wave_header, 4) != "WAVE"){
        cerr << "Error: File is not a valid standard RIFF/WAVE file." << endl;
        return;
    }
    if (header.audio_format != 1){
        cerr << "Error: Only uncompressed PCM WAV files are supported. header.audio_format=" << header.audio_format << "." << endl;
        return;
    }
    // 2. Map WAV properties straight into PulseAudio configuration
    pa_sample_spec ss;
    ss.rate = header.sample_rate;
    ss.channels = header.num_channels;

    // Choose the matching bit depth format flag
    if (header.bits_per_sample == 16) {
        ss.format = PA_SAMPLE_S16LE;
    } else if (header.bits_per_sample == 32) {
        ss.format = PA_SAMPLE_S32LE;
    } else {
        std::cerr << "Unsupported WAV bit depth: " << header.bits_per_sample << std::endl;
        return;
    }

    pa_stream* stream = pa_stream_new(_context, "SoundboardWAVPlayback", &ss, nullptr);
    if (!stream || pa_stream_connect_playback(stream, sink_name.data(), nullptr, PA_STREAM_NOFLAGS, nullptr, nullptr) < 0) {
        std::cerr << "Error: Failed to instantiate playback stream channel." << std::endl;
        if (stream) pa_stream_unref(stream);
        return;
    }

    while (true) {
        pa_mainloop_iterate(_mainloop, 0, nullptr);
        if (pa_stream_get_state(stream) == PA_STREAM_READY) break;
        if (pa_stream_get_state(stream) == PA_STREAM_FAILED) {
            pa_stream_unref(stream);
            return;
        }
        this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::vector<char> buffer(4096);
    
    while (wav_file.read(buffer.data(), buffer.size()) || wav_file.gcount() > 0) {
        size_t bytes_to_write = wav_file.gcount();
        
        size_t num_samples = bytes_to_write / sizeof(int16_t);
        int16_t* samples = reinterpret_cast<int16_t*>(buffer.data());

        for (size_t i = 0; i < num_samples; ++i){
            float scaled_sample = samples[i] * 0.1f;

            // Clip the values to prevent integer overflow clipping artifacts
            if (scaled_sample > 32767.0f)  scaled_sample = 32767.0f;
            if (scaled_sample < -32768.0f) scaled_sample = -32768.0f;

            samples[i] = static_cast<int16_t>(scaled_sample);
        }

        while (pa_stream_writable_size(stream) < bytes_to_write) {
            pa_mainloop_iterate(_mainloop, 0, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (pa_stream_write(stream, buffer.data(), bytes_to_write, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
            std::cerr << "Stream write execution error!" << std::endl;
            break;
        }
        pa_mainloop_iterate(_mainloop, 0, nullptr);
    }

    std::cout << "WAV Playback finished." << std::endl;
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
}

VirtualSink::~VirtualSink()
{
    if (_context && (_module_index != PA_INVALID_INDEX)){
        cout << "Unloading Module with ID: " << _module_index << endl;
        auto cb = [](pa_context* c, int success, void* userdata) {
            if (!success) cerr << "Warning: Failed to gracefully unload module." << endl;
        };
        pa_operation* unloadVirtualSink = pa_context_unload_module(_context, _module_index, cb, nullptr);
        wait_for_operation(_mainloop, unloadVirtualSink);
    }
}

VirtualSource::VirtualSource(pa_mainloop* mainloop, pa_context* context) : _mainloop(mainloop), _context(context) {
    string module_args = format("media.class=Audio/Source/Virtual sink_name={} channel_map=stereo", source_name);
    auto cb = [](pa_context*, uint32_t idx, void* userdata){
        auto* index_ptr = static_cast<uint32_t*>(userdata);
        *index_ptr = idx;
    };

    pa_operation* loadVirtualSource = pa_context_load_module(_context, "module-null-sink", module_args.c_str(), cb, &_module_index);

    wait_for_operation(_mainloop, loadVirtualSource);

    if (_module_index == PA_INVALID_INDEX){
        throw runtime_error("Failed to load virtual source module.");
    }

    cout << "Successfully created Virtual Source with Module ID: " << _module_index << endl;
}

bool VirtualSource::link_sink(string sink_name){
    const std::array<string, 2> directions = {"FL", "FR"};
    for (const auto& drc : directions){
        string command = format("pw-link {}:monitor_{} {}:input_{}", sink_name, drc, source_name, drc);
        
        auto cmdreturn = run_command(command);
        if(cmdreturn.second != 0){
            cerr << cmdreturn.first << endl;
            return false;
        }
    }
    return true;
}

VirtualSource::~VirtualSource() {
    if (_context && (_module_index != PA_INVALID_INDEX)){
        cout << "Unloading Module with ID: " << _module_index << endl;
        auto cb = [](pa_context* c, int success, void* userdata) {
            if (!success) cerr << "Warning: Failed to gracefully unload module." << endl;
        };
        pa_operation* unloadVirtualSink = pa_context_unload_module(_context, _module_index, cb, nullptr);
        wait_for_operation(_mainloop, unloadVirtualSink);
    }
}