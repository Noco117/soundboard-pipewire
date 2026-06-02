#pragma once
#include <pulse/mainloop.h>
#include <string>
#include <memory>
#include <virtualdevices.hpp>
#include <pulse/pulseaudio.h>


class Soundboard {
    public:
        Soundboard();
        Soundboard(std::string input_device);

        ~Soundboard();

        void update();
    private:
       static std::string get_default_input_device();

       bool connect_pulse();
       void disconnect_pulse();

       void play_wav(const fs::path& path);
    
    private:
        pa_mainloop* _mainloop = nullptr;
        pa_context* _context = nullptr;


        std::unique_ptr<VirtualSink> _virt_sink;
        std::unique_ptr<VirtualSource> _virt_source;
};