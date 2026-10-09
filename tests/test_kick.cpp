#include <cmath>
#include <cstdio>
#include <vector>

#include "../config.h"
#include "../core/engine.h"

static int failures = 0;

#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (!(cond)) {                                           \
            std::printf("FAIL: %s\n", msg);                      \
            ++failures;                                          \
        }                                                        \
    } while (0)

int main() {
    const int frames = 2 * cfg::SAMPLE_RATE;  // 2 s (le kick tombe sous le seuil de silence vers 1,4 s)
    std::vector<float> l(frames), r(frames);

    // Sans trig : silence.
    {
        Engine e;
        e.render(l.data(), r.data(), frames);
        float peak = 0.0f;
        for (float s : l) peak = std::fmax(peak, std::fabs(s));
        CHECK(peak == 0.0f, "silence sans trig");
    }

    // Avec trig : son borné, sans NaN, qui décroît jusqu'au silence.
    {
        Engine e;
        e.trigger(0);
        e.render(l.data(), r.data(), frames);

        bool  finite = true;
        float peak   = 0.0f;
        for (float s : l) {
            if (!std::isfinite(s)) finite = false;
            peak = std::fmax(peak, std::fabs(s));
        }
        CHECK(finite, "pas de NaN/inf");
        CHECK(peak > 0.3f && peak <= 1.0f, "pic dans ]0.3, 1]");
        CHECK(l[frames - 1] == 0.0f, "silence à la fin");

        for (int i = 0; i < frames; ++i) CHECK(l[i] == r[i], "canaux identiques");
    }

    // Le découpage en blocs ne change pas le résultat.
    {
        Engine a, b;
        a.trigger(0);
        b.trigger(0);
        a.render(l.data(), r.data(), frames);

        std::vector<float> l2(frames), r2(frames);
        for (int pos = 0; pos < frames;) {
            const int n = (pos % 2) ? 37 : 200;  // tailles volontairement irrégulières
            const int m = (frames - pos < n) ? frames - pos : n;
            b.render(l2.data() + pos, r2.data() + pos, m);
            pos += m;
        }
        bool same = true;
        for (int i = 0; i < frames; ++i) same = same && (l[i] == l2[i]);
        CHECK(same, "résultat indépendant de la taille des blocs");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
