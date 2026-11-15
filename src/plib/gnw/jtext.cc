// nettoge.com Japanese text layer for Fallout CE.
// MODIFIED FILE NOTICE: this file is an addition to fallout1-ce (Sustainable
// Use License 1.0, see LICENSE.md). It is not part of the original sources.

#include "plib/gnw/jtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/db/db.h"
#include "plib/gnw/memory.h"

namespace fallout {

// Glyph file "JFN1" (little endian), made by tools/mkjfont.py:
//   char  magic[4]
//   u16   cell_h     rows of every glyph bitmap
//   i16   line_h     value of text_height() (0 = keep the game's value)
//   i16   base_dy    rows the game's own glyphs are moved down
//   i16   glyph_dy   rows the Japanese glyphs are moved down
//   i16   spacing    pixels after each Japanese glyph
//   i16   reserved
//   u32   count
//   count x { u32 cp; u8 width; u8 advance; u16 pad; u32 offset }  (cp sorted)
//   glyph data: width * cell_h bytes, intensity 0..7 (7 = full color)

typedef struct JGlyph {
    unsigned int cp;
    unsigned char width;
    unsigned char advance;
    unsigned short pad;
    unsigned int offset;
} JGlyph;

typedef struct JFont {
    int font_num;
    int cell_h;
    int line_h;
    int base_dy;
    int glyph_dy;
    int spacing;
    int max_advance;
    int count;
    JGlyph* glyphs;
    unsigned char* data;
} JFont;

#define JFONT_MAX 32

static bool g_active = false;
static JFont g_fonts[JFONT_MAX];
static int g_font_count = 0;

// The current font.
static JFont* g_cur = NULL;

// The small font 101 for jtext_small() and its switch.
#define JFONT_SMALL_NUM 101
static JFont g_small;
static bool g_small_loaded = false;
static bool g_small_on = false;
static FontMgr g_base;

static void J_text_to_buf(unsigned char* buf, const char* str, int swidth, int fullw, int color);
static int J_text_height();
static int J_text_width(const char* str);
static int J_text_max();
static int J_text_char_width(char c);
static int J_text_spacing();
static int J_text_size(const char* str);

static int read_i16(unsigned char* p)
{
    return (short)(p[0] | (p[1] << 8));
}

static unsigned int read_u32(unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

static bool load_jfont(int font_num, JFont* jf, const char* suffix = "")
{
    char path[64];
    snprintf(path, sizeof(path), "text\\japanese\\fonts\\jfont%d%s.jfn", font_num, suffix);

    DB_FILE* stream = db_fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }

    int size = db_filelength(stream);
    unsigned char* raw = (unsigned char*)mem_malloc(size);
    if (raw == NULL || db_fread(raw, 1, size, stream) != (size_t)size || size < 20 || memcmp(raw, "JFN1", 4) != 0) {
        if (raw != NULL) {
            mem_free(raw);
        }
        db_fclose(stream);
        return false;
    }
    db_fclose(stream);

    memset(jf, 0, sizeof(*jf));
    jf->font_num = font_num;
    jf->cell_h = read_i16(raw + 4);
    jf->line_h = read_i16(raw + 6);
    jf->base_dy = read_i16(raw + 8);
    jf->glyph_dy = read_i16(raw + 10);
    jf->spacing = read_i16(raw + 12);
    jf->count = (int)read_u32(raw + 16);

    int table = 20;
    int data = table + jf->count * 12;
    if (data > size) {
        mem_free(raw);
        return false;
    }

    jf->glyphs = (JGlyph*)mem_malloc(sizeof(JGlyph) * jf->count);
    jf->data = (unsigned char*)mem_malloc(size - data + 1);
    if (jf->glyphs == NULL || jf->data == NULL) {
        mem_free(raw);
        return false;
    }

    for (int i = 0; i < jf->count; i++) {
        unsigned char* p = raw + table + i * 12;
        JGlyph* g = &(jf->glyphs[i]);
        g->cp = read_u32(p);
        g->width = p[4];
        g->advance = p[5];
        g->pad = 0;
        g->offset = read_u32(p + 8);
        if (g->advance > jf->max_advance) {
            jf->max_advance = g->advance;
        }
    }
    memcpy(jf->data, raw + data, size - data);
    mem_free(raw);
    return true;
}

void jtext_init(const char* language)
{
    if (language == NULL || compat_stricmp(language, "japanese") != 0) {
        g_active = false;
        return;
    }

    g_active = true;
    g_font_count = 0;

    // The .fon fonts are 0..9 and the .aaf fonts are 100..115.
    for (int n = 0; n < 116 && g_font_count < JFONT_MAX; n++) {
        if (n == 10) {
            n = 100;
        }
        if (load_jfont(n, &(g_fonts[g_font_count]))) {
            g_font_count++;
        }
    }

    g_small_loaded = load_jfont(JFONT_SMALL_NUM, &g_small, "s");

#ifdef NTG_SHOOT
    {
        FILE* log = fopen("jtext.log", "w");
        if (log != NULL) {
            fprintf(log, "language %s fonts %d:", language, g_font_count);
            for (int i = 0; i < g_font_count; i++) {
                fprintf(log, " %d(%d)", g_fonts[i].font_num, g_fonts[i].count);
            }
            fprintf(log, " small %d current %d", g_small_loaded ? g_small.count : 0, text_curr());
            fclose(log);
        }
    }
#endif

    // Re-apply the current font so the wrappers are installed.
    int cur = text_curr();
    if (cur != -1) {
        text_font(cur);
    }
}

void jtext_exit()
{
    for (int i = 0; i < g_font_count; i++) {
        mem_free(g_fonts[i].glyphs);
        mem_free(g_fonts[i].data);
    }
    g_font_count = 0;
    if (g_small_loaded) {
        mem_free(g_small.glyphs);
        mem_free(g_small.data);
        g_small_loaded = false;
    }
    g_cur = NULL;
    g_active = false;
}

bool jtext_active()
{
    return g_active;
}

void jtext_on_font(int font_num, FontMgrPtr mgr)
{
    g_cur = NULL;
    if (!g_active) {
        return;
    }

    for (int i = 0; i < g_font_count; i++) {
        if (g_fonts[i].font_num == font_num) {
            g_cur = &(g_fonts[i]);
            break;
        }
    }

    if (g_cur == NULL) {
        return;
    }

    if (g_small_on && g_small_loaded && font_num == JFONT_SMALL_NUM) {
        g_cur = &g_small;
    }

    g_base = *mgr;
    text_to_buf = J_text_to_buf;
    text_height = J_text_height;
    text_width = J_text_width;
    text_size = J_text_size;
    text_max = J_text_max;
    // Letters and digits come from the Japanese font too, so the per-character
    // widths that text input and dialogue code use must follow it.
    text_char_width = J_text_char_width;
    text_spacing = J_text_spacing;
}

void jtext_small(bool on)
{
    g_small_on = on;
    if (g_cur == NULL || g_cur->font_num != JFONT_SMALL_NUM) {
        return;
    }

    if (on && g_small_loaded) {
        g_cur = &g_small;
        return;
    }

    for (int i = 0; i < g_font_count; i++) {
        if (g_fonts[i].font_num == JFONT_SMALL_NUM) {
            g_cur = &(g_fonts[i]);
            break;
        }
    }
}

int jtext_decode(const char* str, unsigned int* cp)
{
    const unsigned char* s = (const unsigned char*)str;
    unsigned char c = s[0];
    if (c < 0x80 || !g_active) {
        *cp = c;
        return 1;
    }

    int len;
    unsigned int v;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        v = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        v = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        v = c & 0x07;
    } else {
        *cp = c;
        return 1;
    }

    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = c;
            return 1;
        }
        v = (v << 6) | (s[i] & 0x3F);
    }

    *cp = v;
    return len;
}

static JGlyph* find_glyph(JFont* jf, unsigned int cp)
{
    int lo = 0;
    int hi = jf->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (jf->glyphs[mid].cp == cp) {
            return &(jf->glyphs[mid]);
        } else if (jf->glyphs[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

// Width of a multi-byte character in the current font.
static int wide_advance(unsigned int cp)
{
    JGlyph* g = find_glyph(g_cur, cp);
    if (g == NULL) {
        g = find_glyph(g_cur, 0x25A1); // white square for missing glyphs
    }
    if (g == NULL) {
        return 0;
    }
    return g->advance + g_cur->spacing;
}

// Length of the run of single-byte characters at `str` that the Japanese font
// lacks (they are drawn with the game's own font). Letters, digits and signs
// the Japanese font has are drawn with it.
static int narrow_run(const char* str)
{
    int i = 0;
    while (str[i] != '\0') {
        unsigned int cp;
        int len = jtext_decode(str + i, &cp);
        if (len != 1 || find_glyph(g_cur, cp) != NULL) {
            break;
        }
        i++;
    }
    return i;
}

static int J_text_char_width(char c)
{
    JGlyph* g = find_glyph(g_cur, (unsigned char)c);
    if (g == NULL) {
        return g_base.text_char_width(c);
    }
    return g->advance;
}

static int J_text_spacing()
{
    return g_cur->spacing;
}

static int base_width_n(const char* str, int n)
{
    char tmp[256];
    int total = 0;
    while (n > 0) {
        int k = n < 255 ? n : 255;
        memcpy(tmp, str, k);
        tmp[k] = '\0';
        total += g_base.text_width(tmp);
        str += k;
        n -= k;
    }
    return total;
}

static void J_draw(unsigned char* buf, const char* str, int swidth, int fullw, int color)
{
    unsigned char* palette = getColorBlendTable(color & 0xFF);
    int baseColor = color & ~(FONT_SHADOW | FONT_UNDERLINE);
    int x = 0;

    while (*str != '\0') {
        int n = narrow_run(str);
        if (n > 0) {
            char tmp[256];
            int k = n < 255 ? n : 255;
            memcpy(tmp, str, k);
            tmp[k] = '\0';
            int w = g_base.text_width(tmp);
            g_base.text_to_buf(buf + fullw * g_cur->base_dy + x, tmp, swidth - x, fullw, baseColor);
            x += w;
            str += k;
            if (x > swidth) {
                break;
            }
            continue;
        }

        unsigned int cp;
        int len = jtext_decode(str, &cp);
        str += len;

        JGlyph* g = find_glyph(g_cur, cp);
        if (g == NULL) {
            g = find_glyph(g_cur, 0x25A1);
        }
        if (g == NULL) {
            continue;
        }

        int adv = g->advance + g_cur->spacing;
        if (x + adv > swidth) {
            break;
        }

        unsigned char* src = g_cur->data + g->offset;
        for (int y = 0; y < g_cur->cell_h; y++) {
            int row = y + g_cur->glyph_dy;
            if (row < 0) {
                src += g->width;
                continue;
            }
            unsigned char* dst = buf + fullw * row + x;
            for (int gx = 0; gx < g->width; gx++) {
                unsigned char level = *src++;
                if (level != 0) {
                    dst[gx] = palette[(level << 8) + dst[gx]];
                }
            }
        }
        x += adv;
    }

    if ((color & FONT_UNDERLINE) != 0) {
        int h = J_text_height();
        unsigned char* line = buf + fullw * (h - 1);
        for (int i = 0; i < x && i < swidth; i++) {
            line[i] = color & 0xFF;
        }
    }

    freeColorBlendTable(color & 0xFF);
}

static void J_text_to_buf(unsigned char* buf, const char* str, int swidth, int fullw, int color)
{
    if ((color & FONT_SHADOW) != 0) {
        int shadow = (color & ~0xFF & ~FONT_SHADOW) | colorTable[0];
        J_draw(buf + fullw + 1, str, swidth, fullw, shadow);
        color &= ~FONT_SHADOW;
    }
    J_draw(buf, str, swidth, fullw, color);
}

static int J_text_height()
{
    int h = g_base.text_height();
    if (g_cur->line_h > 0) {
        h = g_cur->line_h;
    }
    return h;
}

static int J_text_width(const char* str)
{
    int total = 0;
    while (*str != '\0') {
        int n = narrow_run(str);
        if (n > 0) {
            total += base_width_n(str, n);
            str += n;
            continue;
        }
        unsigned int cp;
        int len = jtext_decode(str, &cp);
        total += wide_advance(cp);
        str += len;
    }
    return total;
}

static int J_text_size(const char* str)
{
    return J_text_width(str) * J_text_height();
}

static int J_text_max()
{
    int m = g_base.text_max();
    int w = g_cur->max_advance + g_cur->spacing;
    return m > w ? m : w;
}

// ---- line breaking --------------------------------------------------------

static bool is_wide(unsigned int cp)
{
    return cp >= 0x2000;
}

// Characters that must not start a line.
static bool no_line_start(unsigned int cp)
{
    static const unsigned int list[] = {
        0x3001, 0x3002, 0xFF0C, 0xFF0E, 0x30FB, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF01, 0x30FC,
        0x300D, 0x300F, 0xFF09, 0x3011, 0x3015, 0x3009, 0x300B, 0x3019, 0x3017, 0xFF3D, 0xFF5D,
        0x2019, 0x201D, 0x309D, 0x309E, 0x30FD, 0x30FE, 0x3005, 0x301C, 0xFF5E,
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x3095, 0x3096,
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6,
        0x2026, 0x2025, 0x203C, 0x2047, 0x2048, 0x2049,
        ',', '.', ')', ']', '}', '!', '?', ':', ';', '%',
    };
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        if (list[i] == cp) {
            return true;
        }
    }
    return false;
}

// Characters that must not end a line.
static bool no_line_end(unsigned int cp)
{
    static const unsigned int list[] = {
        0x300C, 0x300E, 0xFF08, 0x3010, 0x3014, 0x3008, 0x300A, 0x3018, 0x3016, 0xFF3B, 0xFF5B,
        0x2018, 0x201C, '(', '[', '{', '$',
    };
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        if (list[i] == cp) {
            return true;
        }
    }
    return false;
}

static bool can_break_between(unsigned int prev, unsigned int cur)
{
    if (prev == ' ' || prev == '-' || prev == 0x3000) {
        return !no_line_start(cur);
    }
    if (cur == ' ' || cur == 0x3000) {
        return false;
    }
    if (!is_wide(prev) && !is_wide(cur)) {
        return false;
    }
    if (no_line_start(cur) || no_line_end(prev)) {
        return false;
    }
    // Keep "……" and "――" together.
    if (prev == cur && (cur == 0x2026 || cur == 0x2025 || cur == 0x2015 || cur == 0x2014)) {
        return false;
    }
    return true;
}

static bool is_ideographic_space(const char* s)
{
    return (unsigned char)s[0] == 0xE3 && (unsigned char)s[1] == 0x80 && (unsigned char)s[2] == 0x80;
}

static int char_width(const char* str, int len)
{
    char tmp[8];
    memcpy(tmp, str, len);
    tmp[len] = '\0';
    return text_width(tmp);
}

int jtext_fit(const char* str, int max_width, int* next)
{
    int x = 0;
    int i = 0;
    int last_break = -1;
    unsigned int prev = 0;
    int end;

    while (true) {
        if (str[i] == '\0') {
            *next = i;
            return i;
        }
        if (str[i] == '\n') {
            *next = i + 1;
            return i;
        }

        unsigned int cp;
        int len = jtext_decode(str + i, &cp);
        if (i > 0 && can_break_between(prev, cp)) {
            last_break = i;
        }

        int w = char_width(str + i, len);
        if (x + w > max_width && i > 0) {
            if (cp == ' ' || cp == 0x3000) {
                end = i;
            } else if (last_break > 0) {
                end = last_break;
            } else {
                end = i;
            }
            break;
        }

        x += w;
        prev = cp;
        i += len;
    }

    // Spaces (also the ideographic space U+3000, E3 80 80) never start a line.
    int skip = end;
    while (true) {
        if (str[skip] == ' ') {
            skip++;
        } else if (is_ideographic_space(str + skip)) {
            skip += 3;
        } else {
            break;
        }
    }
    if (str[skip] == '\n') {
        skip++;
    }
    *next = skip;

    // Do not draw the trailing spaces.
    while (true) {
        if (end > 0 && str[end - 1] == ' ') {
            end--;
        } else if (end >= 3 && is_ideographic_space(str + end - 3)) {
            end -= 3;
        } else {
            break;
        }
    }
    return end;
}

} // namespace fallout
