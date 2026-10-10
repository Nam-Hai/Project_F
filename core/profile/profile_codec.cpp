// jsmn doit être inclus en premier, avec son implémentation, en mode strict.
#define JSMN_STATIC
#define JSMN_STRICT
#include "../../third_party/jsmn/jsmn.h"

#include "profile_codec.h"

#include <cmath>
#include <cstring>

namespace profile {

// ============================================================================================
// Lecture
// ============================================================================================
namespace {

constexpr int MAX_DEPTH = 16;  // imbrication maximale tolérée (les clés inconnues sont sautées)

// jsmn ne valide que les caractères : virgules et deux-points ne sont pas vérifiés. Tous les
// accès ci-dessous sont donc bornés et la structure est contrôlée au fur et à mesure.
struct Doc {
    const char*        s;
    const jsmntok_t*   t;
    int                n;
};

// Indice du jeton qui suit le sous-arbre commençant en `i`, ou -1 si la structure est incohérente.
int end_of(const Doc& d, int i, int depth) {
    if (i < 0 || i >= d.n || depth > MAX_DEPTH) return -1;

    const jsmntok_t& tk = d.t[i];
    int              j  = i + 1;

    if (tk.type == JSMN_OBJECT) {
        for (int k = 0; k < tk.size; ++k) {
            // une clé : une chaîne qui porte exactement une valeur
            if (j >= d.n || d.t[j].type != JSMN_STRING || d.t[j].size != 1) return -1;
            j = end_of(d, j + 1, depth + 1);
            if (j < 0) return -1;
        }
    } else if (tk.type == JSMN_ARRAY) {
        for (int k = 0; k < tk.size; ++k) {
            j = end_of(d, j, depth + 1);
            if (j < 0) return -1;
        }
    }
    return j;
}

bool key_equals(const Doc& d, int i, const char* key) {
    const int len = d.t[i].end - d.t[i].start;
    return len == static_cast<int>(std::strlen(key)) && std::memcmp(d.s + d.t[i].start, key, static_cast<std::size_t>(len)) == 0;
}

constexpr int ABSENT = -1;
constexpr int BROKEN = -2;

// Jeton de la valeur associée à `key` dans l'objet `obj` : ABSENT, BROKEN, ou son indice.
int find(const Doc& d, int obj, const char* key) {
    int j = obj + 1;
    for (int k = 0; k < d.t[obj].size; ++k) {
        if (j + 1 >= d.n || d.t[j].type != JSMN_STRING || d.t[j].size != 1) return BROKEN;
        if (key_equals(d, j, key)) return j + 1;
        j = end_of(d, j + 1, 2);
        if (j < 0) return BROKEN;
    }
    return ABSENT;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Nombre décimal JSON, indépendant de la langue du système (jamais de virgule décimale).
bool parse_number(const char* p, int len, double& out) {
    int  i   = 0;
    bool neg = false;
    if (i < len && p[i] == '-') {
        neg = true;
        ++i;
    }
    if (i >= len || !is_digit(p[i])) return false;

    double mantissa = 0.0;
    int    frac     = 0;
    while (i < len && is_digit(p[i])) mantissa = mantissa * 10.0 + (p[i++] - '0');

    if (i < len && p[i] == '.') {
        ++i;
        if (i >= len || !is_digit(p[i])) return false;
        while (i < len && is_digit(p[i])) {
            mantissa = mantissa * 10.0 + (p[i++] - '0');
            ++frac;
        }
    }

    int exponent = 0;
    if (i < len && (p[i] == 'e' || p[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < len && (p[i] == '+' || p[i] == '-')) eneg = (p[i++] == '-');
        if (i >= len || !is_digit(p[i])) return false;
        while (i < len && is_digit(p[i])) {
            exponent = exponent * 10 + (p[i++] - '0');
            if (exponent > 400) return false;
        }
        if (eneg) exponent = -exponent;
    }
    if (i != len) return false;

    double v = mantissa * std::pow(10.0, exponent - frac);
    if (!std::isfinite(v)) return false;
    out = neg ? -v : v;
    return true;
}

bool as_number(const Doc& d, int i, double& out) {
    const jsmntok_t& tk = d.t[i];
    return tk.type == JSMN_PRIMITIVE && parse_number(d.s + tk.start, tk.end - tk.start, out);
}

bool as_int(const Doc& d, int i, int lo, int hi, int& out) {
    double v = 0.0;
    if (!as_number(d, i, v) || v != std::floor(v) || v < lo || v > hi) return false;
    out = static_cast<int>(v);
    return true;
}

// Chaîne JSON copiée dans `dst` (capacité `cap`, zéro final compris). Seuls \" \\ \/ sont acceptés.
bool as_string(const Doc& d, int i, char* dst, int cap) {
    const jsmntok_t& tk = d.t[i];
    if (tk.type != JSMN_STRING) return false;

    const char* p   = d.s + tk.start;
    const int   len = tk.end - tk.start;
    int         w   = 0;
    for (int k = 0; k < len; ++k) {
        char c = p[k];
        if (c == '\\') {
            if (++k >= len) return false;
            c = p[k];
            if (c != '"' && c != '\\' && c != '/') return false;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
        if (w >= cap - 1) return false;
        dst[w++] = c;
    }
    dst[w] = '\0';
    return true;
}

// Liste d'entiers séparés par des virgules (« 127, 90,100 », signe + ou - accepté) écrite dans `out`.
// Retourne le nombre de valeurs, ou -1 si le texte est mal formé, trop long, ou si une valeur sort de
// [lo, hi]. Une chaîne vide est une liste vide.
int parse_int_list(const char* s, int* out, int max_count, int lo, int hi) {
    int i = 0;
    int count = 0;
    while (s[i] == ' ') ++i;
    if (s[i] == '\0') return 0;

    for (;;) {
        while (s[i] == ' ') ++i;
        int sign = 1;
        if (s[i] == '-' || s[i] == '+') sign = (s[i++] == '-') ? -1 : 1;
        if (s[i] < '0' || s[i] > '9') return -1;

        int value = 0;
        while (s[i] >= '0' && s[i] <= '9') {
            value = value * 10 + (s[i++] - '0');
            if (value > 100000) return -1;  // bien au-delà de toute borne : on s'arrête avant un débordement
        }
        value *= sign;
        if (count >= max_count || value < lo || value > hi) return -1;
        out[count++] = value;

        while (s[i] == ' ') ++i;
        if (s[i] == '\0') return count;
        if (s[i++] != ',') return -1;
    }
}

// Lit la liste facultative `key` d'une piste dans `dst`, les valeurs absentes gardant leur défaut.
ProfileStatus parse_step_list(const Doc& d, int track_obj, const char* key, int lo, int hi, int8_t* dst_signed,
                              uint8_t* dst_unsigned) {
    const int v = find(d, track_obj, key);
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v == ABSENT) return ProfileStatus::Ok;

    char text[128];
    if (!as_string(d, v, text, sizeof text)) return ProfileStatus::BadField;
    int values[cfg::MAX_STEPS];
    const int n = parse_int_list(text, values, cfg::MAX_STEPS, lo, hi);
    if (n < 0) return ProfileStatus::BadField;
    for (int i = 0; i < n; ++i) {
        if (dst_signed) dst_signed[i] = static_cast<int8_t>(values[i]);
        else dst_unsigned[i] = static_cast<uint8_t>(values[i]);
    }
    return ProfileStatus::Ok;
}

// Un entier signé dans `s` à partir de `i` (signe + ou - accepté), avancé après les chiffres. false si absent.
bool read_int(const char* s, int& i, int& out) {
    int sign = 1;
    if (s[i] == '-' || s[i] == '+') sign = (s[i++] == '-') ? -1 : 1;
    if (s[i] < '0' || s[i] > '9') return false;
    int value = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        value = value * 10 + (s[i++] - '0');
        if (value > 100000) return false;  // bien au-delà de toute borne
    }
    out = sign * value;
    return true;
}

// « extra_hits » : des entrées « pas:décalage:vélocité:pitch:pan » séparées par des espaces. Le pan est facultatif
// (anciens fichiers : 0). Chaque entrée est une note supplémentaire du pas, rangée dans l'ordre d'apparition
// (3 au plus par pas).
ProfileStatus parse_extra_hits(const Doc& d, int track_obj, TrackData& out) {
    const int v = find(d, track_obj, "extra_hits");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v == ABSENT) return ProfileStatus::Ok;

    char text[1024];  // 48 notes de 18 caractères au plus, avec leurs espaces
    if (!as_string(d, v, text, sizeof text)) return ProfileStatus::BadField;

    int i = 0;
    for (;;) {
        while (text[i] == ' ') ++i;
        if (text[i] == '\0') break;

        int step = 0, offset = 0, velocity = 0, pitch = 0, pan = 0;
        if (!read_int(text, i, step) || text[i++] != ':' || !read_int(text, i, offset) || text[i++] != ':' ||
            !read_int(text, i, velocity) || text[i++] != ':' || !read_int(text, i, pitch))
            return ProfileStatus::BadField;
        if (text[i] == ':') {  // le pan, absent des anciens fichiers
            ++i;
            if (!read_int(text, i, pan)) return ProfileStatus::BadField;
        }
        if (text[i] != ' ' && text[i] != '\0') return ProfileStatus::BadField;

        if (step < 0 || step >= cfg::MAX_STEPS || offset < cfg::OFFSET_MIN || offset > cfg::OFFSET_MAX ||
            velocity < cfg::VELOCITY_MIN || velocity > cfg::VELOCITY_MAX || pitch < cfg::STEP_PITCH_MIN ||
            pitch > cfg::STEP_PITCH_MAX || pan < cfg::PAN_MIN || pan > cfg::PAN_MAX)
            return ProfileStatus::BadField;
        if (out.extra_count[step] >= cfg::MAX_EXTRA_HITS) return ProfileStatus::BadField;  // trop de notes dans ce pas

        Hit& h = out.extra[step][out.extra_count[step]++];
        h.offset   = static_cast<int8_t>(offset);
        h.velocity = static_cast<uint8_t>(velocity);
        h.pitch    = static_cast<int8_t>(pitch);
        h.pan      = static_cast<int8_t>(pan);
    }
    return ProfileStatus::Ok;
}

ProfileStatus parse_track(const Doc& d, int idx, int sample_count, TrackData& out) {
    out = TrackData{};
    if (d.t[idx].type != JSMN_OBJECT) return ProfileStatus::BadField;

    int v = find(d, idx, "sample");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    int sample = 0;
    if (v == ABSENT || !as_int(d, v, 0, sample_count - 1, sample)) return ProfileStatus::BadField;
    out.sample = static_cast<uint8_t>(sample);

    v = find(d, idx, "gain");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        double g = 0.0;
        if (!as_number(d, v, g)) return ProfileStatus::BadField;
        out.gain = static_cast<float>(g);
    }

    v = find(d, idx, "pitch");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        double p = 0.0;
        if (!as_number(d, v, p)) return ProfileStatus::BadField;
        out.pitch = static_cast<float>(p);
    }

    v = find(d, idx, "pattern");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        char text[cfg::MAX_STEPS + 1];
        if (!as_string(d, v, text, sizeof text)) return ProfileStatus::BadField;
        uint16_t bits = 0;
        for (int i = 0; text[i]; ++i) {
            if (text[i] == 'x' || text[i] == 'X') bits = static_cast<uint16_t>(bits | (1u << i));
            else if (text[i] != '.') return ProfileStatus::BadField;
        }
        out.pattern = bits;
    }

    ProfileStatus s = parse_step_list(d, idx, "velocity", cfg::VELOCITY_MIN, cfg::VELOCITY_MAX, nullptr, out.step_velocity);
    if (s != ProfileStatus::Ok) return s;
    s = parse_step_list(d, idx, "step_pitch", cfg::STEP_PITCH_MIN, cfg::STEP_PITCH_MAX, out.step_pitch, nullptr);
    if (s != ProfileStatus::Ok) return s;
    s = parse_step_list(d, idx, "step_pan", cfg::PAN_MIN, cfg::PAN_MAX, out.step_pan, nullptr);
    if (s != ProfileStatus::Ok) return s;
    s = parse_step_list(d, idx, "step_offset", cfg::OFFSET_MIN, cfg::OFFSET_MAX, out.step_offset, nullptr);
    if (s != ProfileStatus::Ok) return s;
    s = parse_extra_hits(d, idx, out);
    if (s != ProfileStatus::Ok) return s;

    v = find(d, idx, "quantize");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        int q = 0;
        if (!as_int(d, v, 0, cfg::QUANTIZE_MAX, q)) return ProfileStatus::BadField;
        out.quantize = static_cast<uint8_t>(q);
    }

    v = find(d, idx, "width");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        int p = 0;
        if (!as_int(d, v, cfg::WIDTH_MIN, cfg::WIDTH_MAX, p)) return ProfileStatus::BadField;
        out.width = static_cast<int8_t>(p);
    }

    v = find(d, idx, "balance");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        int b = 0;
        if (!as_int(d, v, cfg::PAN_MIN, cfg::PAN_MAX, b)) return ProfileStatus::BadField;
        out.balance = static_cast<int8_t>(b);
    }

    struct EnvKey {
        const char* key;
        int         max;
        int         dflt;
    };
    const EnvKey keys[4] = {{"attack", cfg::ENV_MS_MAX, 0}, {"decay", cfg::ENV_MS_MAX, 0}, {"sustain", 100, 100}, {"release", cfg::ENV_MS_MAX, cfg::ENV_DEFAULT_RELEASE_MS}};
    int          env[4]  = {0, 0, 100, cfg::ENV_DEFAULT_RELEASE_MS};
    for (int k = 0; k < 4; ++k) {
        v = find(d, idx, keys[k].key);
        if (v == BROKEN) return ProfileStatus::InvalidJson;
        if (v != ABSENT && !as_int(d, v, 0, keys[k].max, env[k])) return ProfileStatus::BadField;
    }
    out.attack  = static_cast<uint16_t>(env[0]);
    out.decay   = static_cast<uint16_t>(env[1]);
    out.sustain = static_cast<uint8_t>(env[2]);
    out.release = static_cast<uint16_t>(env[3]);
    return ProfileStatus::Ok;
}

ProfileStatus parse_loop(const Doc& d, int idx, int sample_count, LoopData& out) {
    out = LoopData{};
    if (d.t[idx].type != JSMN_OBJECT) return ProfileStatus::BadField;

    int v = find(d, idx, "bpm");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        double bpm = 0.0;
        if (!as_number(d, v, bpm)) return ProfileStatus::BadField;
        out.bpm = static_cast<float>(bpm);
    }

    v = find(d, idx, "tracks");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v == ABSENT) return ProfileStatus::Ok;
    if (d.t[v].type != JSMN_ARRAY) return ProfileStatus::BadField;
    if (d.t[v].size > cfg::MAX_TRACKS) return ProfileStatus::TooBig;

    int j = v + 1;
    for (int k = 0; k < d.t[v].size; ++k) {
        const ProfileStatus s = parse_track(d, j, sample_count, out.tracks[k]);
        if (s != ProfileStatus::Ok) return s;
        out.track_count = static_cast<uint8_t>(k + 1);
        j = end_of(d, j, 3);
        if (j < 0) return ProfileStatus::InvalidJson;
    }
    return ProfileStatus::Ok;
}

}  // namespace

ProfileStatus ProfileCodec::parse(const char* text, std::size_t length, ProfileData& out) {
    // Réinitialisation sur place (un ProfileData temporaire pèserait environ 9 Ko de pile).
    std::strcpy(out.name, "Default");
    out.sample_count = 0;
    out.loop_count   = 0;

    jsmn_parser parser;
    jsmn_init(&parser);
    const int n = jsmn_parse(&parser, text, length, tokens_, MAX_TOKENS);
    if (n == JSMN_ERROR_PART) return ProfileStatus::Truncated;
    if (n == JSMN_ERROR_NOMEM) return ProfileStatus::TooBig;
    if (n <= 0) return ProfileStatus::InvalidJson;

    const Doc d{text, tokens_, n};
    if (end_of(d, 0, 0) != n) return ProfileStatus::InvalidJson;  // structure incohérente ou texte en trop
    if (d.t[0].type != JSMN_OBJECT) return ProfileStatus::NotAProfile;

    // Identification : « format » et « version ».
    int v = find(d, 0, "format");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v == ABSENT || d.t[v].type != JSMN_STRING || !key_equals(d, v, "dmp")) return ProfileStatus::NotAProfile;

    v = find(d, 0, "version");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v == ABSENT) return ProfileStatus::NotAProfile;
    int version = 0;
    if (!as_int(d, v, 1, 1000000, version)) return ProfileStatus::BadField;
    if (version > FORMAT_VERSION) return ProfileStatus::TooNew;

    v = find(d, 0, "profile");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT && !as_string(d, v, out.name, sizeof out.name)) return ProfileStatus::BadField;

    // Table de samples.
    v = find(d, 0, "samples");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        if (d.t[v].type != JSMN_ARRAY) return ProfileStatus::BadField;
        if (d.t[v].size > MAX_SAMPLES) return ProfileStatus::TooBig;
        for (int k = 0; k < d.t[v].size; ++k) {
            if (!as_string(d, v + 1 + k, out.samples[k], sizeof out.samples[k])) return ProfileStatus::BadField;
            out.sample_count = static_cast<uint8_t>(k + 1);
        }
    }

    // Loops.
    v = find(d, 0, "loops");
    if (v == BROKEN) return ProfileStatus::InvalidJson;
    if (v != ABSENT) {
        if (d.t[v].type != JSMN_ARRAY) return ProfileStatus::BadField;
        if (d.t[v].size > MAX_LOOPS) return ProfileStatus::TooBig;

        int j = v + 1;
        for (int k = 0; k < d.t[v].size; ++k) {
            const ProfileStatus s = parse_loop(d, j, out.sample_count, out.loops[k]);
            if (s != ProfileStatus::Ok) return s;
            out.loop_count = static_cast<uint8_t>(k + 1);
            j = end_of(d, j, 2);
            if (j < 0) return ProfileStatus::InvalidJson;
        }
    }
    return ProfileStatus::Ok;
}

// ============================================================================================
// Écriture
// ============================================================================================
namespace {

// Écrit dans un buffer sans jamais déborder, en comptant tout de même la longueur voulue.
struct Writer {
    char*       buf;
    std::size_t cap;
    std::size_t len = 0;
    bool        ok  = true;

    void ch(char c) {
        if (len < cap) buf[len] = c;
        ++len;
    }
    void raw(const char* s) {
        while (*s) ch(*s++);
    }
    void indent(int levels) {
        for (int i = 0; i < levels * 2; ++i) ch(' ');
    }
    void str(const char* s) {
        ch('"');
        for (; *s; ++s) {
            if (static_cast<unsigned char>(*s) < 0x20) {
                ok = false;
                return;
            }
            if (*s == '"' || *s == '\\') ch('\\');
            ch(*s);
        }
        ch('"');
    }
    void integer(long long v) {
        if (v < 0) {
            ch('-');
            v = -v;
        }
        char tmp[24];
        int  n = 0;
        do {
            tmp[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v > 0);
        while (n > 0) ch(tmp[--n]);
    }
    // Décimal à 3 chiffres au plus, sans zéros inutiles mais toujours avec une décimale (« 1.0 »).
    void number(double v) {
        if (!(v < 1e9 && v > -1e9)) {  // exclut aussi NaN
            ok = false;
            return;
        }
        if (v < 0) {
            ch('-');
            v = -v;
        }
        const long long scaled = std::llround(v * 1000.0);
        integer(scaled / 1000);
        ch('.');
        int frac = static_cast<int>(scaled % 1000);
        if (frac == 0) {
            ch('0');
            return;
        }
        char digits[3] = {static_cast<char>('0' + frac / 100), static_cast<char>('0' + frac / 10 % 10),
                          static_cast<char>('0' + frac % 10)};
        int  n = 3;
        while (n > 1 && digits[n - 1] == '0') --n;
        for (int i = 0; i < n; ++i) ch(digits[i]);
    }
};

}  // namespace

int ProfileCodec::write(const ProfileData& p, char* buffer, std::size_t capacity) {
    Writer w{buffer, capacity};

    w.raw("{\n");
    w.indent(1); w.raw("\"format\": \"dmp\",\n");
    w.indent(1); w.raw("\"version\": "); w.integer(FORMAT_VERSION); w.raw(",\n");
    w.indent(1); w.raw("\"profile\": "); w.str(p.name); w.raw(",\n");

    w.indent(1); w.raw("\"samples\": [");
    for (int i = 0; i < p.sample_count; ++i) {
        w.raw(i ? ",\n" : "\n");
        w.indent(2);
        w.str(p.samples[i]);
    }
    if (p.sample_count) { w.raw("\n"); w.indent(1); }
    w.raw("],\n");

    w.indent(1); w.raw("\"loops\": [");
    for (int l = 0; l < p.loop_count; ++l) {
        const LoopData& loop = p.loops[l];
        w.raw(l ? ",\n" : "\n");
        w.indent(2); w.raw("{\n");
        w.indent(3); w.raw("\"bpm\": "); w.number(loop.bpm); w.raw(",\n");
        w.indent(3); w.raw("\"tracks\": [");
        for (int t = 0; t < loop.track_count; ++t) {
            const TrackData& tr = loop.tracks[t];
            if (tr.sample >= p.sample_count) w.ok = false;

            char pattern[cfg::MAX_STEPS + 1];
            for (int s = 0; s < cfg::MAX_STEPS; ++s) pattern[s] = ((tr.pattern >> s) & 1u) ? 'x' : '.';
            pattern[cfg::MAX_STEPS] = '\0';

            w.raw(t ? ",\n" : "\n");
            w.indent(4);
            w.raw("{ \"sample\": "); w.integer(tr.sample);
            w.raw(", \"gain\": "); w.number(tr.gain);
            w.raw(", \"pitch\": "); w.number(tr.pitch);
            w.raw(", \"pattern\": "); w.str(pattern);

            // Listes de valeurs par pas, seulement si elles ne sont pas toutes par défaut.
            bool custom_velocity = false, custom_pitch = false, custom_pan = false, custom_offset = false, any_extra = false;
            if (tr.quantize > cfg::QUANTIZE_MAX) w.ok = false;
            if (tr.attack > cfg::ENV_MS_MAX || tr.decay > cfg::ENV_MS_MAX || tr.release > cfg::ENV_MS_MAX || tr.sustain > 100) w.ok = false;
            if (tr.width < cfg::WIDTH_MIN || tr.width > cfg::WIDTH_MAX || tr.balance < cfg::PAN_MIN || tr.balance > cfg::PAN_MAX) w.ok = false;
            for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                if (tr.step_offset[s] < cfg::OFFSET_MIN || tr.step_offset[s] > cfg::OFFSET_MAX) w.ok = false;
                custom_offset = custom_offset || tr.step_offset[s] != 0;
                if (tr.extra_count[s] > cfg::MAX_EXTRA_HITS) {
                    w.ok = false;
                } else {
                    any_extra = any_extra || tr.extra_count[s] > 0;
                    for (int h = 0; h < tr.extra_count[s]; ++h) {
                        const Hit& hit = tr.extra[s][h];
                        if (hit.offset < cfg::OFFSET_MIN || hit.offset > cfg::OFFSET_MAX || hit.velocity < cfg::VELOCITY_MIN ||
                            hit.velocity > cfg::VELOCITY_MAX || hit.pitch < cfg::STEP_PITCH_MIN || hit.pitch > cfg::STEP_PITCH_MAX ||
                            hit.pan < cfg::PAN_MIN || hit.pan > cfg::PAN_MAX)
                            w.ok = false;
                    }
                }
                if (tr.step_velocity[s] < cfg::VELOCITY_MIN || tr.step_velocity[s] > cfg::VELOCITY_MAX) w.ok = false;
                if (tr.step_pitch[s] < cfg::STEP_PITCH_MIN || tr.step_pitch[s] > cfg::STEP_PITCH_MAX) w.ok = false;
                custom_velocity = custom_velocity || tr.step_velocity[s] != cfg::VELOCITY_MAX;
                if (tr.step_pan[s] < cfg::PAN_MIN || tr.step_pan[s] > cfg::PAN_MAX) w.ok = false;
                custom_pitch    = custom_pitch || tr.step_pitch[s] != 0;
                custom_pan      = custom_pan || tr.step_pan[s] != 0;
            }
            if (custom_velocity) {
                w.raw(", \"velocity\": \"");
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    if (s) w.ch(',');
                    w.integer(tr.step_velocity[s]);
                }
                w.ch('"');
            }
            if (custom_pitch) {
                w.raw(", \"step_pitch\": \"");
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    if (s) w.ch(',');
                    w.integer(tr.step_pitch[s]);
                }
                w.ch('"');
            }
            if (custom_pan) {
                w.raw(", \"step_pan\": \"");
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    if (s) w.ch(',');
                    w.integer(tr.step_pan[s]);
                }
                w.ch('"');
            }
            if (custom_offset) {
                w.raw(", \"step_offset\": \"");
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    if (s) w.ch(',');
                    w.integer(tr.step_offset[s]);
                }
                w.ch('"');
            }
            if (any_extra) {
                w.raw(", \"extra_hits\": \"");
                bool first = true;
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    for (int h = 0; h < tr.extra_count[s]; ++h) {
                        const Hit& hit = tr.extra[s][h];
                        if (!first) w.ch(' ');
                        first = false;
                        w.integer(s);          w.ch(':');
                        w.integer(hit.offset); w.ch(':');
                        w.integer(hit.velocity); w.ch(':');
                        w.integer(hit.pitch); w.ch(':');
                        w.integer(hit.pan);
                    }
                }
                w.ch('"');
            }
            if (tr.quantize != 0) {
                w.raw(", \"quantize\": ");
                w.integer(tr.quantize);
            }
            if (tr.width != 0) {
                w.raw(", \"width\": ");
                w.integer(tr.width);
            }
            if (tr.balance != 0) {
                w.raw(", \"balance\": ");
                w.integer(tr.balance);
            }
            if (tr.attack != 0) {
                w.raw(", \"attack\": ");
                w.integer(tr.attack);
            }
            if (tr.decay != 0) {
                w.raw(", \"decay\": ");
                w.integer(tr.decay);
            }
            if (tr.sustain != 100) {
                w.raw(", \"sustain\": ");
                w.integer(tr.sustain);
            }
            if (tr.release != cfg::ENV_DEFAULT_RELEASE_MS) {
                w.raw(", \"release\": ");
                w.integer(tr.release);
            }
            w.raw(" }");
        }
        if (loop.track_count) { w.raw("\n"); w.indent(3); }
        w.raw("]\n");
        w.indent(2); w.raw("}");
    }
    if (p.loop_count) { w.raw("\n"); w.indent(1); }
    w.raw("]\n}\n");

    if (!w.ok || w.len + 1 > capacity) return -1;
    buffer[w.len] = '\0';
    return static_cast<int>(w.len);
}

}  // namespace profile
