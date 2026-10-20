// Prove della parte di calcolo di FBTECH DAW.
#include "../src/core/Levels.h"
#include <cstdio>
#include <cstring>
static int ok = 0, ko = 0;
static void check(bool c, const char* m) { std::printf("  %s  %s\n", c ? "OK" : "KO", m); c ? ++ok : ++ko; }
static bool near(double a, double b, double e) { return std::fabs(a - b) <= e; }
int main() {
    using namespace fbd;
    std::printf("1) DECIBEL\n");
    check(near(dbToGain(0), 1.0, 1e-6) && near(dbToGain(-6.0206f), 0.5, 1e-4) && near(dbToGain(20), 10.0, 1e-4) && near(dbToGain(-20), 0.1, 1e-5) && dbToGain(-100) == 0.f && dbToGain(-200) == 0.f, "dB -> guadagno: 0 dB=1, -6 dB=0,5, +20 dB=10, -100 dB e sotto = silenzio");
    check(near(gainToDb(1), 0, 1e-5) && near(gainToDb(0.5f), -6.0206, 1e-3) && gainToDb(0) == -100.f && gainToDb(-1) == -100.f && gainToDb(1e-9f) == -100.f, "guadagno -> dB, silenzio = -100");
    bool rt = true;
    for (float d = -90; d <= 12; d += 3.7f) { rt = rt && near(gainToDb(dbToGain(d)), d, 0.01); }
    check(rt, "andata e ritorno senza perdite da -90 a +12 dB");
    std::printf("\n2) BARRA DEL MISURATORE\n");
    check(dbToFraction(-60) == 0 && dbToFraction(-100) == 0 && dbToFraction(3) == 1 && dbToFraction(12) == 1 && near(dbToFraction(-28.5f), 0.5, 1e-6) && dbToFraction(0) > dbToFraction(-12) && near(dbToFraction(0), 60.0 / 63.0, 1e-6), "da -60 dB (vuota) a +3 dB (piena): zero sotto, uno sopra, lineare in mezzo");
    std::printf("\n3) MISURATORE: SALITA, CADUTA, PICCO, SOVRACCARICO\n");
    Meter m; m.push(0.5f, 0.033); check(near(m.db, -6.02, 0.01) && near(m.holdDb, -6.02, 0.01), "sale subito al livello del picco");
    m.push(0.0f, 0.5); check(near(m.db, -6.02 - 12.0, 0.01) && near(m.holdDb, -6.02, 0.01), "con silenzio scende di 24 dB al secondo (0,5 s = 12 dB); il picco resta fermo");
    for (int i = 0; i < 3; ++i) { m.push(0.0f, 0.5); }
    check(near(m.db, -18.0 - 36.0, 0.05), "dopo altri 1,5 s e' sceso di altri 36 dB (-54 dB): caduta costante");
    for (int i = 0; i < 6; ++i) { m.push(0.0f, 0.5); }
    check(m.db == -100.f, "dopo qualche secondo arriva in fondo (-100 dB) e li' resta");
    Meter h; h.push(0.8f, 0.01); const float peak = h.holdDb; h.push(0.0f, 1.0); check(near(h.holdDb, peak, 0.001), "il picco resta fermo per 1,5 s (a 1,0 s e' ancora li')"); h.push(0.0f, 0.6); check(h.holdDb < peak, "poi comincia a scendere"); for (int i = 0; i < 10; ++i) { h.push(0.0f, 1.0); }
    check(h.holdDb <= h.db + 0.001f, "e non scende sotto il livello mostrato");
    Meter c; c.push(1.0f, 0.01); check(c.clipping() && c.clipLeft == Meter::kClipSec, "a 0 dB pieno (picco >= 0,999) compare l'avviso di sovraccarico"); c.push(0.1f, 2.0); check(c.clipping(), "resta acceso 3 secondi (a 2 s e' ancora acceso)"); c.push(0.1f, 1.5); check(!c.clipping(), "poi si spegne");
    Meter n; n.push(0.99f, 0.01); check(!n.clipping(), "0,99 (-0,09 dB) non e' sovraccarico");
    std::printf("\n4) TESTI\n");
    check(formatDb(-100) == "-inf" && formatDb(-99.5f) == "-inf" && formatDb(-12.34f) == "-12.3" && formatDb(0) == "0.0" && formatDb(2.96f) == "3.0", "decibel: -inf, una cifra decimale");
    check(formatTime(0) == "00:00.0" && formatTime(5.96) == "00:05.9" && formatTime(61.25) == "01:01.2" && formatTime(3599.99) == "59:59.9" && formatTime(-3) == "00:00.0" && formatTime(600) == "10:00.0", "tempo mm:ss.d (il decimo non si arrotonda per eccesso: 5,96 s = 5,9)");
    check(recordingName(2026, 10, 19, 9, 5, 7) == "rec-20261019-090507.wav" && recordingName(2026, 12, 31, 23, 59, 59) == "rec-20261231-235959.wav", "nome dei file registrati: ordinabile per data");
    check(near(samplesToMs(48, 48000), 1.0, 1e-9) && near(samplesToMs(288, 48000), 6.0, 1e-9) && samplesToMs(100, 0) == 0.0, "campioni -> millisecondi (288 campioni a 48 kHz = 6 ms)");
    std::printf("\nESITO: %s\n", ko ? "PROBLEMI" : "TUTTO OK");
    return ko ? 1 : 0;
}
