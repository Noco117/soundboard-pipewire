#include <cstring>
#include <filesystem>
#include <pulse/def.h>
#include <soundboard.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <unordered_map>
#include <exception>
#include <charconv>

using namespace std;

enum class Command {
    Play,
    Stop,
    Set,
    Link,
    Unlink,
    Kill,
    Unkown
};

static const unordered_map<string, Command> command_map = {
    {"PLAY",  Command::Play},
    {"STOP",  Command::Stop},
    {"SET" ,  Command::Set},
    {"LINK",  Command::Link},
    {"UNLINK",Command::Unlink},
    {"KILL",  Command::Kill}
};



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

vector<string> Soundboard::parseSocketCommand(string cmd){
    vector<string> reversed_tokens;
    string_view sv(cmd);

    size_t idx = sv.find_last_of(' ');
    while (idx != string_view::npos){
        string_view token = sv.substr(idx + 1);
        if (!token.empty()) reversed_tokens.emplace_back(token);
        sv = sv.substr(0, idx);
        idx = sv.find_last_of(' ');
    }

    if (!sv.empty()) reversed_tokens.emplace_back(sv);

    return reversed_tokens;
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
            continue; 
        }

        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0) {
            string command(buffer);
            command.erase(command.find_last_not_of("\n\t\r") + 1); 
            cout << "[Socket IPC] Received command: " << command << endl;
            // Basic parsing framework: Expecting "PLAY /absolute/path/to/audio.wav"

            vector<string> reversed_tokens = parseSocketCommand(command);
            size_t n = reversed_tokens.size();
            if (n == 0) {close(client_fd); continue;}

            auto it = command_map.find(reversed_tokens.back());
            Command cmd = (it != command_map.end()) ? it->second : Command::Unkown;
            reversed_tokens.pop_back();
            switch(cmd){
                case Command::Play:
                {   
                    if (n == 1) break; else if (n > 3) break;
                    string_view token = reversed_tokens.back();
                    float volume = 1.0f;
                    fs::path fpath;
                    try{
                        fpath = fs::canonical(token);
                        if(!fs::is_regular_file(fpath)) break;
                    }
                    catch (exception& e){
                        cerr << "Error while parsing Socket Command: " << e.what() << endl;
                        break;
                    }
                    if (n==3) {
                        reversed_tokens.pop_back();
                        token = reversed_tokens.back();
                        try {
                            auto [ptr, ec] = from_chars(token.data(), token.data() + token.size(), volume);
                            if(ec != std::errc()){
                                volume = 1.0f;
                            }
                            std::cout << "Volume: " << volume << endl;
                        }
                        catch (exception& e){
                            break;
                        }
                    }

                    if (fpath.extension() == ".wav"){
                        _virt_sink->stop_playback_by_path(fpath);
                        play_wav(fpath, volume);
                    }

                    break;
                }
                case Command::Stop:
                    if (n == 1){
                        _virt_sink->stop_playback();
                    break;
                    } else if (n == 2){
                        string_view token = reversed_tokens.back();
                        try {
                            fs::path path = fs::canonical(token);
                            _virt_sink->stop_playback_by_path(path);
                        }
                        catch (exception& e){
                            break;
                        }
                    } else {
                        break;
                    }
                case Command::Set:
                {
                    if (n==1) break;
                    
                    string_view token = reversed_tokens.back();
                    if (token == "VOLUME"){
                        reversed_tokens.pop_back();
                        if (n!=3) break;
                        token = reversed_tokens.back();
                        try{
                            float volume;
                            auto [ptr, ec] = from_chars(token.data(), token.data() + token.size(), volume);
                            if (ec != std::errc()){
                                break;
                            }

                            _virt_sink->set_volume(volume);
                        }
                        catch(exception& e){
                            break;
                        }
                    }
                    else break;
                }
                case Command::Link:
                {
                    if (n!=3) break;
                    string_view token = reversed_tokens.back();

                    if (token == "SINK") {reversed_tokens.pop_back(); _virt_source->link_sink(reversed_tokens.back());}
                    else if (token == "SOURCE") {reversed_tokens.pop_back(); _virt_source->link_source(reversed_tokens.back());}
                    else if (token == "INPUT") break;
                    else break;
                }
                case Command::Unlink:
                {
                    if (n!=3) break;
                    string_view token = reversed_tokens.back();

                    if (token == "SINK") {reversed_tokens.pop_back(); _virt_source->unlink_sink(reversed_tokens.back());}
                    else if (token == "SOURCE") {reversed_tokens.pop_back(); _virt_source->unlink_source(reversed_tokens.back());}
                    else if (token == "INPUT") break;
                    else break;
                }
                case Command::Kill:
                    _close_cb();
                case Command::Unkown:
                    break;
                default:
                    break;
            }
        }
        close(client_fd);
    }

    close(server_fd);
    unlink(_socket_path.c_str());
    std::cout << "Networking worker thread safely exited." << std::endl;
}