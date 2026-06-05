#include "soundboard.hpp"
#include "virtualdevices.hpp"

#include <exception>
#include <iostream>
#include <pulse/context.h>
#include <pulse/def.h>
#include <pulse/error.h>
#include <pulse/thread-mainloop.h>
#include <chrono>
#include <memory>
#include <array>
#include <regex>
#include <thread>

using namespace std;


Soundboard::Soundboard() : Soundboard(get_default_input_device()) {}

Soundboard::Soundboard(string input_device) {
    if (!connect_pulse()) {
        throw runtime_error("Failed to context to PulseAudio/PipeWire server!");
    }

    _virt_sink = make_unique<VirtualSink>(_threaded_mainloop, _context);
    _virt_source = make_unique<VirtualSource>(_threaded_mainloop, _context);

    this_thread::sleep_for(std::chrono::seconds(1));

    _virt_source->link_source(input_device);
    _virt_source->link_sink(std::string(_virt_sink->sink_name));


    initialize_socket_path();
    _socket_active.store(true);
    _socket_thread = thread([this](){this->run_socket_server();});
}

Soundboard::~Soundboard() {
    cout << "Cleaning up..." << endl;
    _socket_active.store(false);
    if(_socket_thread.joinable()){
        _socket_thread.join();
    }

    _virt_sink.reset();
    _virt_source.reset();
    disconnect_pulse();
}

//static size_t i = 0;
void Soundboard::update(){
    // cout << "updating..." << endl;
    //++i;
    this_thread::sleep_for(chrono::seconds(1));
    // if (i == 5){
    //     std::cout << "playing dubstep intro" << std::endl;
    //     play_wav("/home/noco/dev/arch/soundboard/assets/dubstepintro.wav");
    // }

}

void Soundboard::play_wav(const fs::path& path, float volume){
    try {_virt_sink->play_wav(path, volume);}
    catch (exception& e) {cout << "Error: " << e.what() << endl;}
}

string Soundboard::get_default_input_device(){
    array<char, 128> buffer;
    string result;

    unique_ptr<FILE, int(*)(FILE*)> pipe(
        popen("wpctl status | grep -A 5 -i \"Audio/Source\"", "r"), 
        pclose
    );

    if (!pipe) {
        throw runtime_error("popen() failed to execute wpctl status!");
    }

    // 1. Define the robust regex pattern
    regex mic_regex(R"((alsa_input\.[a-zA-Z0-9_\.\-]+))");
    smatch match;

    // 2. Read the pipe stream line by line
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        string line(buffer.data());
        
        // 3. Evaluate the regex against the current line
        if (regex_search(line, match, mic_regex)) {
            result = match[1].str(); // Capture the first matched group
            break; // Found it! Exit loop early
        }
    }

    return result;
}

bool Soundboard::connect_pulse()
{   
    //cout << "Connecting PulseAudio/PipeWire Mainloop..." << endl;
    _threaded_mainloop = pa_threaded_mainloop_new();
    if (!_threaded_mainloop) return false;

    //cout << "Makin PA/PW Context..." << endl;
    _context = pa_context_new(pa_threaded_mainloop_get_api(_threaded_mainloop), "soundboard");
    if (!_context) {
        pa_threaded_mainloop_free(_threaded_mainloop);
        return false;
    }

    if (pa_threaded_mainloop_start(_threaded_mainloop) < 0) {
        pa_context_unref(_context);
        pa_threaded_mainloop_free(_threaded_mainloop);
        return false;
    }


    pa_threaded_mainloop_lock(_threaded_mainloop);
    if (pa_context_connect(_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(_context);
        disconnect_pulse();
        return false;
    }

    int retval = 0;
    while (true) {
        pa_context_state_t state = pa_context_get_state(_context);
        if (state == PA_CONTEXT_READY) {
            pa_threaded_mainloop_unlock(_threaded_mainloop);
            return true;
        }
        if( state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED ) {
            cerr << "Context Error: " << pa_strerror(pa_context_errno(_context)) << endl;
            pa_threaded_mainloop_unlock(_threaded_mainloop);
            disconnect_pulse();
            return false;
        }
        pa_threaded_mainloop_unlock(_threaded_mainloop);
        this_thread::sleep_for(chrono::milliseconds(1));
        pa_threaded_mainloop_lock(_threaded_mainloop);
    }
}

void Soundboard::disconnect_pulse() {
    if (_threaded_mainloop) {
        pa_threaded_mainloop_stop(_threaded_mainloop);
    }
    if (_context) {
        pa_context_disconnect(_context);
        pa_context_unref(_context);
        _context = nullptr;
    }
    if (_threaded_mainloop){
        pa_threaded_mainloop_free(_threaded_mainloop);
        _threaded_mainloop=nullptr;
    }
}