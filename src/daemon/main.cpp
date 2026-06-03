#include <atomic>
#include <csignal>
#include <unistd.h>
#include <memory>
#include <iostream>

#include "soundboard.hpp"

std::atomic<bool> keep_running{true};

void signal_handler(int signum){
    keep_running = false;
}

int main(int argc, char** argv){
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::unique_ptr<Soundboard> soundboard = std::unique_ptr<Soundboard>(nullptr);

    try{
        soundboard = std::make_unique<Soundboard>();
    }
    catch (std::runtime_error e) {
        std::cout << "Failed to Create Soundboard: " << e.what() << std::endl;
        keep_running = false;
    }

    if (!soundboard){
        keep_running = false;
    }

    while(keep_running){
        soundboard->update(); 
    }

    return 0;
}