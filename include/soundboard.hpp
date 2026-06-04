#pragma once
#include <csignal>
#include <string>
#include <memory>
#include <virtualdevices.hpp>
#include <pulse/pulseaudio.h>
#include <thread>
#include <functional>

class Soundboard {
    public:
        Soundboard();
        Soundboard(std::string input_device);

        ~Soundboard();

        void update();
        void set_close_cb(std::function<void()> close_cb){_close_cb = close_cb;};
    private:
        static std::string get_default_input_device();

        bool connect_pulse();
        void disconnect_pulse();
        void initialize_socket_path();

        void play_wav(const fs::path& path, float vol); 
    private:
        pa_threaded_mainloop* _threaded_mainloop = nullptr;
        pa_context* _context = nullptr;


        std::unique_ptr<VirtualSink> _virt_sink;
        std::unique_ptr<VirtualSource> _virt_source;

        std::thread _socket_thread;
        std::atomic<bool> _socket_active{false};
        std::string _socket_path;

        void run_socket_server();

        std::function<void()> _close_cb = [&]{std::raise(SIGTERM);};
        static std::vector<std::string> parseSocketCommand(std::string cmd);
};