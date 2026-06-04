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
#include <algorithm>
#include <cctype>
#include <format>

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
    {"play",  Command::Play},
    {"stop",  Command::Stop},
    {"set" ,  Command::Set},
    {"link",  Command::Link},
    {"unlink",Command::Unlink},
    {"kill",  Command::Kill}
};

static std::string usage_instructions = "Usage: \t\t\t\t <required>; [optional]\n"
"\tsoundboard play <path-to-wav> [volume]\n"
"\tsoundboard stop [path-to-wav]";


std::string to_lower(std::string_view data) {
    // 1. Explicitly construct a mutable string from the view
    std::string result(data); 
    
    // 2. Transform the local copy
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    
    return result;
}

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

            auto it = command_map.find(to_lower(reversed_tokens.back()));
            Command cmd = (it != command_map.end()) ? it->second : Command::Unkown;
            reversed_tokens.pop_back();
            auto swrite = [client_fd](string msg){
                write(client_fd, msg.c_str(), msg.size() + 1);};
            switch(cmd){
                case Command::Play:
                {   
                    //if (n == 1) break; else if (n > 3) break;
                    if ((n==1) || (n > 3)){
                        swrite(usage_instructions);
                        break;
                    }
                    string_view token = reversed_tokens.back();
                    float volume = 1.0f;
                    fs::path fpath;
                    try{
                        fpath = fs::canonical(token);
                        if(!fs::is_regular_file(fpath)) {
                            swrite(string("Not a File!"));
                            break;
                        }
                    }
                    catch(fs::filesystem_error& e){
                        swrite(format("Not a valid path: {}", token));
                        break;
                    }
                    catch (exception& e){
                        string msg = string("Error while parsing Socket Command: ") + e.what();
                        swrite(msg);
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
                        }
                        catch (exception& e){
                            string msg = string("Syntax Error: ") + e.what();
                            swrite(msg);
                            break;
                        }
                    }

                    if (fpath.extension() == ".wav"){
                        _virt_sink->stop_playback_by_path(fpath);
                        play_wav(fpath, volume);
                    }else{
                        swrite("Not a .wav file!");
                    }
                    break;
                }
                case Command::Stop:
                {
                    if (n == 1){
                        _virt_sink->stop_playback();
                    break;
                    } else if (n == 2){
                        string_view token = reversed_tokens.back();
                        try {
                            fs::path path = fs::canonical(token);
                            _virt_sink->stop_playback_by_path(path);
                        }
                        catch(fs::filesystem_error&) {
                            swrite(format("Not a valid path: {}", token));
                        }
                        catch (exception& e){
                            string msg = string("Error: ") + e.what();
                            swrite(msg);
                            break;
                        }
                    } else {
                        swrite("Unexpected number of options\n" + usage_instructions);
                        break;
                    }
                    break;
                }
                case Command::Set:
                {
                    if (n!=3) {
                        swrite("Unexpected number of options\n" + usage_instructions);
                        break;
                    }
                    
                    string_view token = reversed_tokens.back();
                    if (token == "volume"){
                        reversed_tokens.pop_back();
                        token = reversed_tokens.back();
                        try{
                            float volume;
                            auto [ptr, ec] = from_chars(token.data(), token.data() + token.size(), volume);
                            if (ec != std::errc() || (0 > volume) || (volume > 2)){
                                swrite("Not a valid floating point volume between 0.0 and 2.0.");
                                break;
                            }

                            _virt_sink->set_volume(volume);
                        }
                        catch(exception& e){
                            swrite(string("Syntax Error: ") + e.what());
                            break;
                        }
                    }
                    else swrite("Unexpected option");
                    break;
                }
                case Command::Link:
                {
                    if (n!=3){
                        swrite("Unexpected number of options\n" + usage_instructions);
                        break;
                    }
                    string_view token = reversed_tokens.back();

                    if (token == "SINK") {reversed_tokens.pop_back(); _virt_source->link_sink(reversed_tokens.back());}
                    else if (token == "SOURCE") {reversed_tokens.pop_back(); _virt_source->link_source(reversed_tokens.back());}
                    else if (token == "INPUT") break;
                    else swrite("Unexpected option");
                    break;
                }
                case Command::Unlink:
                {
                    if (n!=3) {
                        swrite("Unexpected number of options\n" + usage_instructions);
                        break;
                    }
                    string_view token = reversed_tokens.back();

                    if (token == "SINK") {reversed_tokens.pop_back(); _virt_source->unlink_sink(reversed_tokens.back());}
                    else if (token == "SOURCE") {reversed_tokens.pop_back(); _virt_source->unlink_source(reversed_tokens.back());}
                    else if (token == "INPUT") break;
                    else swrite("Unexpected option");
                    break;
                }
                case Command::Kill:
                    _close_cb();
                    break;
                case Command::Unkown:
                    swrite("Unexpected command");
                    break;
                default:
                    swrite( "unexpected");
                    break;
            }
        }
        shutdown(client_fd, SHUT_WR);
        close(client_fd);
    }

    close(server_fd);
    unlink(_socket_path.c_str());
    std::cout << "Networking worker thread safely exited." << std::endl;
}