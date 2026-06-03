#include <cstring>
#include <soundboard.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>

using namespace std;


void Soundboard::initialize_socket_path(){
    uid_t uid = getuid();
    fs::path runtime_dir = fs::path("/run/user") / to_string(uid);
    if (!std::filesystem::exists(runtime_dir)) {
        const char* home = getenv("HOME");
        if (home) {
            runtime_dir = std::filesystem::path(home) / ".local/share/soundboard";
            std::filesystem::create_directories(runtime_dir); // Ensure it exists
        } else {
            runtime_dir = "/tmp"; // Hard fallback
        }
    }

    _socket_path = runtime_dir / "soundboard.sock";
    cout << "[Initialization] Socket assigned to: " << _socket_path << std::endl;
}

void Soundboard::run_socket_server(){
    unlink(_socket_path.c_str());
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0){
        cerr << "Failed to create socket." << endl;
        return;
    }
    
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(server_fd,SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, _socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        cerr << "Failed to bind socket file!" << endl;
        return;
    }

    if (listen(server_fd, 5) < 0) {
        cerr << "Networking Error: Failed to listen on socket." << endl;
        close(server_fd);
        return;
    }

    std::cout << "[Socket IPC] Server is actively listening on " << _socket_path << std::endl;

    char buffer[512];

    while (_socket_active.load()) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        
        if (client_fd < 0) {
            // This is usually a timeout hit (EAGAIN/EWOULDBLOCK). Loop back and check _networking_active
            continue; 
        }

        // Set client read timeout so slow clients can't stall our worker
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0) {
            string command(buffer);
            command.erase(command.find_last_not_of("\n\t\r") + 1); 
            cout << "[Socket IPC] Received command: " << command << endl;
            // Basic parsing framework: Expecting "PLAY /absolute/path/to/audio.wav"
            if (command.rfind("PLAY ", 0) == 0) {
                string audio_path = command.substr(5);
                
                // Strip trailing newlines or whitespaces sent by network tools (like netcat)
                audio_path.erase(audio_path.find_last_not_of(" \n\r\t") + 1);

                cout << "[Socket IPC] Triggering audio play: " << audio_path << endl;
                
                const fs::path absolute_path = fs::absolute(audio_path);
                _virt_sink->stop_playback_by_path(absolute_path);
                this->play_wav(absolute_path); 
            }
            else if (command=="STOP")
            {
                _virt_sink->stop_playback();
            }
            else if (command.rfind("STOP ", 0) == 0) { 
                string audio_path = command.substr(5);
                audio_path.erase(audio_path.find_last_not_of(" \n\r\t") + 1);
                
                cout << "[Socket IPC] Triggering audio stop: " << audio_path << endl;
                _virt_sink->stop_playback_by_path(audio_path);
            }
        }
        close(client_fd);
    }

    close(server_fd);
    unlink(_socket_path.c_str());
    std::cout << "Networking worker thread safely exited." << std::endl;
}