#include "soundboard.hpp"
#include "virtualdevices.hpp"

#include <iostream>
#include <pulse/context.h>
#include <pulse/def.h>
#include <thread>
#include <chrono>
#include <memory>
#include <array>
#include <regex>

using namespace std;


Soundboard::Soundboard() : Soundboard(get_default_input_device()) {}

Soundboard::Soundboard(string input_device) {
    // TODO: Implement the constructor for the Sounboard
    if (!connect_pulse()) {
        throw runtime_error("Failed to context to PulseAudio/PipeWire server!");
    }

    _virt_sink = make_unique<VirtualSink>(_mainloop, _context);
    _virt_source = make_unique<VirtualSource>(_mainloop, _context);

    _virt_sink->link_source(input_device);
    _virt_source->link_sink(std::string(_virt_sink->sink_name));
}

Soundboard::~Soundboard() {
    _virt_sink.reset();
    _virt_source.reset();

    disconnect_pulse();
    cout << "Cleaning up..." << endl;
}

static size_t i = 0;
void Soundboard::update(){
    // cout << "updating..." << endl;
    ++i;
    this_thread::sleep_for(chrono::seconds(1));
    if (i == 10){
        std::cout << "playing dubstep intro" << std::endl;
        play_wav("/home/noco/dev/arch/soundboard/assets/dubstepintro.wav");
    }
}

void Soundboard::play_wav(const fs::path& path){
    _virt_sink->play_wav(path);
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
    _mainloop = pa_mainloop_new();
    if (!_mainloop) return false;

    //cout << "Makin PA/PW Context..." << endl;
    _context = pa_context_new(pa_mainloop_get_api(_mainloop), "soundboard");
    if (!_context) return false;

    if (pa_context_connect(_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        return false;
    }

    int retval = 0;
    while (true) {
        pa_mainloop_iterate(_mainloop, 1, &retval);
        if (retval < 0) return false; // Error in mainloop
        pa_context_state_t state = pa_context_get_state(_context);
        if (state == PA_CONTEXT_READY) return true;
        if( state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED ) return false;
    }
}

void Soundboard::disconnect_pulse() {
    if (_context) {
        pa_context_disconnect(_context);
        pa_context_unref(_context);
        _context = nullptr;
    }
    if (_mainloop) {
        pa_mainloop_free(_mainloop);
        _mainloop = nullptr;
    }
}