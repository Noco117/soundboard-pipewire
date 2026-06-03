# soundboard-pipewire v1.0
This is a lightweight command-line soundboard for PipeWire with PipewirePulse. 
It provides a socket-based daemon that manages a linked virtual sink and virtual source, and plays uncompressed PCM .wav files into the audio stream from an audio input. 

## Dependencies
- glibc
- pipewire (and libpipewire)
- wireplumber
- pipewirepulse
- python
## Manual Installation
Clone the Repository into a local folder and navigate to into it.
```
git clone git@github.com:Noco117/soundboard-pipewire.git soundboard-pipewire && cd soundboard-pipewire
```
Then build and install using cmake 
```
cmake -B build
cmake --build build
sudo cmake --install build
```
