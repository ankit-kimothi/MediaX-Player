#pragma once
/*
 * AudioHandle — Audio stream management and 6-band digital equalizer.
 *
 * DSA Concepts: std::map (EQ preset storage, O(log n) lookup),
 *               Abstract base class + Inheritance (MP3Source, MP4Source),
 *               RAII resource management, Biquad IIR digital filter
 *
 * EQ Bands: 60Hz | 230Hz | 910Hz | 3kHz | 14kHz | 22kHz
 *
 * Author: Akash Kumar Nath
 */
#include <string>
#include <vector>
#include <map>
#include <array>
#include <memory>
#include <cmath>

namespace mediax {

static const int NUM_EQ_BANDS = 6;
static const double EQ_BAND_FREQS[NUM_EQ_BANDS] = {60.0, 230.0, 910.0, 3000.0, 14000.0, 22000.0};

// Audio stream information
struct AudioInfo {
    int sampleRate   = 44100;
    int channels     = 2;
    int bitDepth     = 16;
    std::string codec = "Unknown";
    double durationSecs = 0.0;
    long long fileSizeBytes = 0;
    std::string filePath;
};

// IIR Biquad filter coefficients (peaking EQ)
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0; // feedforward
    double a1 = 0.0, a2 = 0.0;             // feedback (a0 normalized to 1)
};

// Per-channel per-band biquad state (Direct Form II Transposed)
struct BiquadState {
    double x1 = 0, x2 = 0; // input history
    double y1 = 0, y2 = 0; // output history
    void reset() { x1=x2=y1=y2=0.0; }
};

// Abstract audio source
class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual AudioInfo getInfo() const = 0;
    virtual bool isOpen() const = 0;
    virtual std::string getPath() const = 0;
};

// Concrete MP3 source (stub — real decode: libmpg123)
class MP3Source : public AudioSource {
public:
    bool open(const std::string& path) override;
    void close() override;
    AudioInfo getInfo() const override;
    bool isOpen() const override { return open_; }
    std::string getPath() const override { return info_.filePath; }
private:
    AudioInfo info_;
    bool open_ = false;
};

// Concrete MP4/AAC source (stub — real decode: FFmpeg/libavcodec)
class MP4Source : public AudioSource {
public:
    bool open(const std::string& path) override;
    void close() override;
    AudioInfo getInfo() const override;
    bool isOpen() const override { return open_; }
    std::string getPath() const override { return info_.filePath; }
private:
    AudioInfo info_;
    bool open_ = false;
};

// Factory: picks the right source based on file extension
std::unique_ptr<AudioSource> createAudioSource(const std::string& path);

// RAII audio handle + 6-band equalizer
class AudioHandle {
public:
    explicit AudioHandle(const std::string& path);
    ~AudioHandle(); // RAII: closes source on destruction

    bool isValid() const;
    AudioInfo getInfo() const;
    std::string getPath() const;

    // Equalizer control
    void setGain(int band, double gainDb);
    double getGain(int band) const;
    void setEnabled(bool on);
    bool isEnabled() const { return eqEnabled_; }
    void applyPreset(const std::string& presetName);
    std::vector<double> getGains() const;
    static std::vector<std::string> availablePresets();

    // Apply EQ to interleaved float PCM buffer
    // samples: interleaved L/R (or mono), frameCount: number of sample frames
    void processBuffer(float* samples, int frameCount, int channels);

private:
    std::unique_ptr<AudioSource> source_;
    bool eqEnabled_ = true;
    double gains_[NUM_EQ_BANDS] = {};
    BiquadCoeffs coeffs_[NUM_EQ_BANDS];
    BiquadState  state_[NUM_EQ_BANDS][2]; // [band][channel] — max 2 ch
    std::map<std::string, std::array<double, NUM_EQ_BANDS>> presets_;

    void initPresets();
    void recomputeCoeffs(int band);
    static BiquadCoeffs makePeakingEQ(double freq, double gainDb, double Q, double sampleRate);
};

} // namespace mediax
