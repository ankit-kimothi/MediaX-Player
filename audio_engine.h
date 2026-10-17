#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

using namespace std;

// Adhiraj Singh


struct AudioMetadata {
    string title;
    string artist;
    string album;
    string genre;
    int durationSeconds;
    int sampleRate;
    int channels;
    int bitRate;
};

class MetadataExtractor {
public:
    static AudioMetadata extractFromFile(const string& filePath) {
        AudioMetadata meta;
        
        meta.title = "Unknown Title";
        meta.artist = "Unknown Artist";
        meta.album = "Unknown Album";
        meta.genre = "Unknown Genre";
        meta.durationSeconds = 0;
        meta.sampleRate = 44100; 
        meta.channels = 2;     
        meta.bitRate = 320;

        ifstream audioFile(filePath, ios::binary);
        if (!audioFile.is_open()) {
            cout << "Couldn't open file: " << filePath << endl;
            return meta;
        } 

        char headerTag[4];
        audioFile.read(headerTag, 4);

        if (string(headerTag, 4) == "RIFF") {
            audioFile.seekg(22, ios::beg);
            uint16_t chCount = 0;
            audioFile.read(reinterpret_cast<char*>(&chCount), sizeof(chCount));
            meta.channels = chCount;

            uint32_t sRate = 0;
            audioFile.read(reinterpret_cast<char*>(&sRate), sizeof(sRate));
            meta.sampleRate = sRate;

            audioFile.seekg(40, ios::beg);
            uint32_t dataBytes = 0;
            audioFile.read(reinterpret_cast<char*>(&dataBytes), sizeof(dataBytes));

            // Duration calculation formula
            if (sRate > 0 && chCount > 0) {
                meta.durationSeconds = dataBytes / (sRate * chCount * 2);
            }

            size_t slashPos = filePath.find_last_of("/\\");
            if (slashPos != string::npos) {
                meta.title = filePath.substr(slashPos + 1);
            } else {
                meta.title = filePath;
            }
        } else {
            cout << "Not a WAV file, using default metadata values." << endl;
        }

        audioFile.close();
        return meta;
    }
};

//Akash Kumar Nath

// Frequency bands for equalization (in Hz)
struct EqualizerBands {
    float bass;      // 60 Hz band (-12 to +12 dB)
    float lowMid;    // 230 Hz band
    float mid;       // 910 Hz band
    float highMid;   // 4 kHz band
    float treble;    // 14 kHz band
};

class Equalizer {
private:
    EqualizerBands eqBands;
    bool eqActive;

public:

    Equalizer() {
       
        eqBands = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        eqActive = true;
    }

    // Adjust specific gain band index (0 to 4)

    void setBandGain(int bandIdx, float gain) {
        // Clamping gain value between -12 dB and +12 dB
        if (gain < -12.0f) gain = -12.0f;
        if (gain > 12.0f) gain = 12.0f;

        switch (bandIdx) {
            case 0: eqBands.bass = gain; break;
            case 1: eqBands.lowMid = gain; break;
            case 2: eqBands.mid = gain; break;
            case 3: eqBands.highMid = gain; break;
            case 4: eqBands.treble = gain; break;
            default: cout << "Invalid band index selected!" << endl; break;
        }
    }

    // Quick presets setting

    void applyPreset(const string& preset) {
        if (preset == "Bass Boost") {
            eqBands = {6.0f, 4.0f, 0.0f, 0.0f, 0.0f};
        } else if (preset == "Vocal Booster") {
            eqBands = {-2.0f, 1.0f, 5.0f, 3.0f, 0.0f};
        } else if (preset == "Pop") {
            eqBands = {-1.0f, 2.0f, 4.0f, 2.0f, -1.0f};
        } else if (preset == "Flat") {
            eqBands = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        } else {
            cout << "Unknown preset, staying on current configuration." << endl;
        }
    }

    // Process raw PCM audio samples

    void processAudioBuffer(vector<float>& pcmBuffer) {
        if (!eqActive) return;

        // Simple average scale factor calculation
        float avgGain = (eqBands.bass + eqBands.lowMid + eqBands.mid + eqBands.highMid + eqBands.treble) / 5.0f;
        float gainScale = pow(10.0f, avgGain / 20.0f);

        for (size_t i = 0; i < pcmBuffer.size(); ++i) {
            pcmBuffer[i] *= gainScale;

            // Clamping audio output to avoid clipping noise
            if (pcmBuffer[i] > 1.0f) pcmBuffer[i] = 1.0f;
            if (pcmBuffer[i] < -1.0f) pcmBuffer[i] = -1.0f;
        }
    }

    // Print current EQ status

    void displayEQStatus() const {
        cout << "\n--- Equalizer Band Settings [Akash] ---" << endl;
        cout << "1. Bass (60 Hz):     " << eqBands.bass << " dB" << endl;
        cout << "2. Low Mid (230 Hz): " << eqBands.lowMid << " dB" << endl;
        cout << "3. Mid (910 Hz):     " << eqBands.mid << " dB" << endl;
        cout << "4. High Mid (4 kHz): " << eqBands.highMid << " dB" << endl;
        cout << "5. Treble (14 kHz):  " << eqBands.treble << " dB" << endl;
    }
};

#endif