#include "virtualdevices.hpp"
#include <cstdint>
#include <pulse/operation.h>
#include <pulse/pulseaudio.h>
#include <pulse/stream.h>
#include <pulse/thread-mainloop.h>
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <format>
#include <fstream>
#include <vector>
#include <thread>

using namespace std;

struct VirtualSink::PlaybackInstance{
    thread thread_handle;
    shared_ptr<std::atomic<bool>> abort_flag;
    fs::path path;
};

void wait_for_operation(pa_threaded_mainloop *threaded_mainloop, pa_operation *op){
    if (!op) return;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING){
        pa_threaded_mainloop_wait(threaded_mainloop);
    }
    pa_operation_unref(op);
}

struct ModuleLoadContext{
    pa_threaded_mainloop* threaded_mainloop;
    uint32_t* index_out;
};

static void module_load_cb(pa_context*, uint32_t idx, void* userdata) {
    auto* ctx = static_cast<ModuleLoadContext*>(userdata);
    *(ctx->index_out) = idx;
    pa_threaded_mainloop_signal(ctx->threaded_mainloop, 0);
}

static void module_unload_cb(pa_context*, int success, void* userdata) {
    auto* mainloop = static_cast<pa_threaded_mainloop*>(userdata);
    if (!success) cerr << "Warning: Failed to gracefully unload module." << endl;
    pa_threaded_mainloop_signal(mainloop, 0);
}

static void stream_state_cb(pa_stream* s, void* userdata) {
    auto* mainloop = static_cast<pa_threaded_mainloop*>(userdata);
    pa_threaded_mainloop_signal(mainloop, 0);
}

static void stream_writable_cb(pa_stream* s, size_t length, void* userdata) {
    auto* mainloop = static_cast<pa_threaded_mainloop*>(userdata);
    pa_threaded_mainloop_signal(mainloop, 0);
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

VirtualSink::VirtualSink(pa_threaded_mainloop* threaded_mainloop, pa_context* context) : _threaded_mainloop(threaded_mainloop), _context(context){
    string module_args = format("media.class=Audio/Sink sink_name={} channel_map=stereo", sink_name);
    // auto cb = [](pa_context*, uint32_t idx, void* userdata){
    //     auto* index_ptr = static_cast<uint32_t*>(userdata);
    //     *index_ptr = idx;
    // };


    ModuleLoadContext load_ctx{_threaded_mainloop, &_module_index};

    pa_threaded_mainloop_lock(_threaded_mainloop);
    pa_operation* loadVirtualSink = pa_context_load_module(_context, "module-null-sink", module_args.c_str(), module_load_cb, &load_ctx);
    wait_for_operation(_threaded_mainloop, loadVirtualSink);

    pa_threaded_mainloop_unlock(_threaded_mainloop);

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
    lock_guard<mutex> lock(_thread_mutex);

    for(auto it = _playback_instances.begin(); it != _playback_instances.end(); ){
        if (it->thread_handle.joinable() && it->abort_flag->load() == true){
            it->thread_handle.join();
            it = _playback_instances.erase(it);
        }
        else {
            ++it;
        }
    }

    fs::path mutable_path = fs::absolute(path);
    auto per_thread_abort = std::make_shared<std::atomic<bool>>(false);
    _playback_instances.emplace_back(
            thread([this, path, per_thread_abort](){
                    this->play_wav_sync(path, per_thread_abort);
                    per_thread_abort->store(true);
            }),
            per_thread_abort,
            std::move(mutable_path)
    );
}

void VirtualSink::play_wav_sync(const fs::path& path, shared_ptr<atomic<bool>> abort_flag){
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


    // STREAM INSTANTIATION
    pa_threaded_mainloop_lock(_threaded_mainloop);
    pa_stream* stream = pa_stream_new(_context, "SoundboardWAVPlayback", &ss, nullptr);
    if (!stream) {
        std::cerr << "Error: Failed to instantiate playback stream channel." << std::endl;
        pa_threaded_mainloop_unlock(_threaded_mainloop);
        return;
    }
    
    pa_stream_set_state_callback(stream, stream_state_cb, _threaded_mainloop);
    pa_stream_set_write_callback(stream, stream_writable_cb, _threaded_mainloop);

    if (pa_stream_connect_playback(stream, sink_name.data(), nullptr, PA_STREAM_NOFLAGS, nullptr, nullptr) < 0) {
        std::cerr << "Error: Failed to connect playback stream channel." << std::endl;
        pa_stream_unref(stream);
        pa_threaded_mainloop_unlock(_threaded_mainloop);
        return;
    }

    while (true) {
        pa_stream_state_t state = pa_stream_get_state(stream);
        if (state == PA_STREAM_READY) break;
        if (state == PA_STREAM_FAILED) {
            cerr << "Stream Connection Failed!" << endl;
            pa_stream_unref(stream);
            pa_threaded_mainloop_unlock(_threaded_mainloop);
            return;
        }
        // Drops the lock and sleeps until stream_state_cb signals us
        pa_threaded_mainloop_wait(_threaded_mainloop);
    }

    pa_threaded_mainloop_unlock(_threaded_mainloop);

    std::vector<char> buffer(4096);
    
    while (wav_file.read(buffer.data(), buffer.size()) || wav_file.gcount() > 0) {
        if (abort_flag->load()) break;
        
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
        
        pa_threaded_mainloop_lock(_threaded_mainloop);

        while (pa_stream_writable_size(stream) < bytes_to_write) {
            pa_threaded_mainloop_wait(_threaded_mainloop);
        }

        if (pa_stream_write(stream, buffer.data(), bytes_to_write, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
            std::cerr << "Stream write execution error!" << std::endl;
            pa_threaded_mainloop_unlock(_threaded_mainloop);
            break;
        }
        pa_threaded_mainloop_unlock(_threaded_mainloop);
    }

    pa_threaded_mainloop_lock(_threaded_mainloop);
    std::cout << "WAV Playback finished." << std::endl;
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    pa_threaded_mainloop_unlock(_threaded_mainloop);
}

void VirtualSink::stop_playback(){

    for (auto& [_,abort_flag,__] : _playback_instances) {
        abort_flag->store(true);
    }

    pa_threaded_mainloop_lock(_threaded_mainloop);
    pa_threaded_mainloop_signal(_threaded_mainloop, 0);
    pa_threaded_mainloop_unlock(_threaded_mainloop); 

    for (auto& [t,_,__] : _playback_instances) {
        if (t.joinable()) {
            t.join(); 
        }
    }

    _playback_instances.clear();
}

void VirtualSink::stop_playback_by_path(const fs::path& path){
    fs::path absolute_target = fs::absolute(path);
    std::lock_guard<std::mutex> lock(_thread_mutex);

    for (auto it = _playback_instances.begin(); it!=_playback_instances.end(); ){
        if(it->path == absolute_target && it->thread_handle.joinable()){
            it->abort_flag->store(true);

            pa_threaded_mainloop_lock(_threaded_mainloop);
            pa_threaded_mainloop_signal(_threaded_mainloop, 0);
            pa_threaded_mainloop_unlock(_threaded_mainloop); 

            it->thread_handle.join();
            it = _playback_instances.erase(it);
        }
        else{
            ++it;
        }
    }
}

VirtualSink::~VirtualSink()
{   
    // 1. Lock the thread vector
    std::lock_guard<std::mutex> lock(_thread_mutex);
    stop_playback();

    if (_context && (_module_index != PA_INVALID_INDEX)){
        cout << "Unloading Module with ID: " << _module_index << endl;
        // auto cb = [](pa_context* c, int success, void* userdata) {
        //     if (!success) cerr << "Warning: Failed to gracefully unload module." << endl;
        // };

        pa_threaded_mainloop_lock(_threaded_mainloop);
        pa_operation* unloadVirtualSink = pa_context_unload_module(_context, _module_index, module_unload_cb, _threaded_mainloop);
        wait_for_operation(_threaded_mainloop, unloadVirtualSink);
        pa_threaded_mainloop_unlock(_threaded_mainloop);
    }
}

VirtualSource::VirtualSource(pa_threaded_mainloop* threaded_mainloop, pa_context* context) : _threaded_mainloop(threaded_mainloop), _context(context) {
    string module_args = format("media.class=Audio/Source/Virtual sink_name={} channel_map=stereo", source_name);
    // auto cb = [](pa_context*, uint32_t idx, void* userdata){
    //     auto* index_ptr = static_cast<uint32_t*>(userdata);
    //     *index_ptr = idx;
    // };

    ModuleLoadContext load_ctx{_threaded_mainloop, &_module_index};

    pa_threaded_mainloop_lock(_threaded_mainloop);
    pa_operation* loadVirtualSource = pa_context_load_module(_context, "module-null-sink", module_args.c_str(), module_load_cb, &load_ctx);
    wait_for_operation(_threaded_mainloop, loadVirtualSource);
    pa_threaded_mainloop_unlock(_threaded_mainloop);

    if (_module_index == PA_INVALID_INDEX){
        throw runtime_error("Failed to load virtual source module.");
    }

    cout << "Successfully created Virtual Source with Module ID: " << _module_index << endl;
}

bool VirtualSource::link_sink(string sink_name){
    const std::array<string, 2> directions = {"FL", "FR"};
    for (const auto& drc : directions){
        string command = format("pw-link {}:monitor_{} {}:input_{}", sink_name, drc, source_name, drc);
        
        cout << command << endl;
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
        // auto cb = [](pa_context* c, int success, void* userdata) {
        //     if (!success) cerr << "Warning: Failed to gracefully unload module." << endl;
        // };

        pa_threaded_mainloop_lock(_threaded_mainloop);
        pa_operation* unloadVirtualSink = pa_context_unload_module(_context, _module_index, module_unload_cb, _threaded_mainloop);
        wait_for_operation(_threaded_mainloop, unloadVirtualSink);
        pa_threaded_mainloop_unlock(_threaded_mainloop);
    }
}