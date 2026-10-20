// FBTECH DAW - parte di CALCOLO portabile (nessuna dipendenza da JUCE ne' da Windows): decibel, misuratori con caduta e picco tenuto, formati di testo.
// Si prova da sola (tests/levels_test.cpp), cosi' le regole dei misuratori non dipendono dalla scheda audio.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace fbd {

inline float dbToGain(float db) { return db <= -100.f ? 0.f : std::pow(10.f, db / 20.f); }
inline float gainToDb(float g) { return g <= 0.00001f ? -100.f : 20.f * std::log10(g); }
// posizione (0..1) di un livello su una barra che va da floorDb a topDb
inline float dbToFraction(float db, float floorDb = -60.f, float topDb = 3.f) {
    if (db <= floorDb) return 0.f;
    if (db >= topDb) return 1.f;
    return (db - floorDb) / (topDb - floorDb);
}
inline std::string formatDb(float db) {
    if (db <= -99.f) return "-inf";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", (double)db); return b;
}
// secondi -> "mm:ss.d"
inline std::string formatTime(double seconds) {
    if (!(seconds >= 0)) seconds = 0;
    int tenths = (int)std::floor(seconds * 10.0 + 0.0001);
    int m = tenths / 600, s = (tenths / 10) % 60, d = tenths % 10;
    char b[32]; std::snprintf(b, sizeof b, "%02d:%02d.%d", m, s, d); return b;
}
// "rec-20261019-153042.wav"
inline std::string recordingName(int y, int mo, int d, int h, int mi, int s) {
    char b[64]; std::snprintf(b, sizeof b, "rec-%04d%02d%02d-%02d%02d%02d.wav", y, mo, d, h, mi, s); return b;
}
// millisecondi di un numero di campioni
inline double samplesToMs(long long samples, double sampleRate) { return sampleRate > 0 ? 1000.0 * (double)samples / sampleRate : 0.0; }

// Misuratore: sale subito, scende piano (24 dB al secondo); il picco resta fermo 1,5 s e poi scende; l'avviso di sovraccarico resta 3 s.
struct Meter {
    float db = -100.f, holdDb = -100.f;
    double holdLeft = 0, clipLeft = 0;
    static constexpr float kFallDbPerSec = 24.f, kHoldFallDbPerSec = 40.f;
    static constexpr double kHoldSec = 1.5, kClipSec = 3.0;
    void push(float peakGain, double dt) {
        const float d = gainToDb(peakGain);
        if (d >= db) db = d; else db = std::max(d, db - kFallDbPerSec * (float)dt);
        if (d >= holdDb) { holdDb = d; holdLeft = kHoldSec; }
        else { holdLeft -= dt; if (holdLeft <= 0) holdDb = std::max(db, holdDb - kHoldFallDbPerSec * (float)dt); }
        if (peakGain >= 0.999f) clipLeft = kClipSec; else clipLeft = std::max(0.0, clipLeft - dt);
    }
    bool clipping() const { return clipLeft > 0; }
};

} // namespace fbd
