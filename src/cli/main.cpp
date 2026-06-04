#include <iostream>
#include <string>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

namespace fs = std::filesystem;

// THIS IS THE SAME LOGIC USED BY THE DAEMON TO FIND THE SOCKET PATH
fs::path get_active_socket_path() {
    uid_t uid = getuid();
    fs::path runtime_dir = fs::path("/run/user") / std::to_string(uid);
    
    if (!fs::exists(runtime_dir)) {
        const char* home = std::getenv("HOME");
        if (home) {
            runtime_dir = fs::path(home) / ".local/share/soundboard";
        } else {
            runtime_dir = "/tmp";
        }
    }
    return runtime_dir / "soundboard.sock";
}

int main(int argc, char* argv[]){
    if (argc < 2) {
        std::cerr << "Usage: soundboard <COMMAND> [ARGS...]\n"
                  << "Example: soundboard PLAY /path/to/file.wav 0.5\n";
        return 1;
    }

    std::string payload = argv[1];
    for (int i = 2; i < argc; ++i) {
        payload += " ";
        payload += argv[i];
    }

    fs::path socket_path = get_active_socket_path();
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "CLI Error: Failed to initialize socket interface.\n";
        return 1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Error: Action failed. Daemon is not running at " << socket_path << "\n";
        close(sock);
        return 1;
    }

    ssize_t bytes_sent = write(sock, payload.c_str(), payload.length());
    if (bytes_sent < 0) {
        std::cerr << "CLI Error: Failed writing payload data to daemon link.\n";
        close(sock);
        return 1;
    }

    char buffer[512];
    ssize_t bytes_written = recv(sock, buffer, sizeof(buffer)-1, 0);
    std::string_view bytes_to_flush(buffer, bytes_written);
    std::cout << bytes_to_flush << std::endl;

    close(sock);
    return 0;
}