#include "locale_psp.h"

#include <algorithm>
#include <array>
#include <map>

namespace gow {
namespace {
#include "locale_strings.inc"

char16_t upper(char16_t c) {
  if (c >= u'а' && c <= u'я') return c - 32;
  switch (c) {
    case u'ё': return u'Ё';
    case u'ґ': return u'Ґ';
    case u'є': return u'Є';
    case u'і': return u'І';
    case u'ї': return u'Ї';
    default: return c;
  }
}

struct Shape {
  char16_t letter;
  int width;
  std::array<uint8_t, 7> rows;
  uint8_t accent = 0; // 1 dots, 2 breve
  uint8_t feet = 0;
};

// Pixel silhouettes. The stored width is the bit width, with MSB at left.
// Serif terminals, bowls and diagonals follow the original small-cap face.
constexpr Shape shapes[] = {
  {u'Б',7,{127,49,48,62,51,51,126}},
  {u'Г',7,{127,49,48,48,48,48,120}},
  {u'Ґ',7,{127,48,48,48,48,48,120},3},
  {u'Д',7,{62,26,26,26,50,50,127},0,65},
  {u'Ё',7,{127,49,52,60,52,49,127},1},
  {u'Є',7,{31,51,96,124,96,51,31}},
  {u'Ж',9,{0,0,0,0,0,0,0}}, // wide glyph has a separate 9-bit silhouette below
  {u'З',7,{62,97,3,30,3,99,62}},
  {u'И',7,{119,51,55,59,51,51,119}},
  {u'Й',7,{119,51,55,59,51,51,119},2},
  {u'Ї',5,{31,14,14,14,14,14,31},1},
  {u'Л',7,{63,27,27,27,51,51,103}},
  {u'П',7,{127,51,51,51,51,51,119}},
  {u'У',7,{119,51,54,30,12,56,112}},
  {u'Ф',9,{0,0,0,0,0,0,0}},
  {u'Ц',7,{119,51,51,51,51,51,127},0,3},
  {u'Ч',7,{119,51,51,31,3,3,7}},
  {u'Ш',9,{0,0,0,0,0,0,0}},
  {u'Щ',9,{0,0,0,0,0,0,0},0,3},
  {u'Ъ',7,{120,24,24,30,27,27,62}},
  {u'Ы',9,{0,0,0,0,0,0,0}},
  {u'Ь',7,{120,48,48,62,51,51,126}},
  {u'Э',7,{124,102,3,31,3,102,124}},
  {u'Ю',9,{0,0,0,0,0,0,0}},
  {u'Я',7,{63,99,99,63,27,51,119}},
  {u'«',6,{0,9,18,36,18,9,0}},
  {u'»',6,{0,36,18,9,18,36,0}},
  {u'—',7,{0,0,0,127,0,0,0}},
  {u'×',7,{0,65,34,28,34,65,0}},
  {u'□',7,{127,65,65,65,65,65,127}},
  {u'○',7,{28,34,65,65,65,34,28}},
  {u'△',7,{8,20,20,34,34,65,127}},
};

constexpr std::array<uint16_t, 7> wideRows(char16_t c) {
  switch (c) {
    case u'Ж': return {0x1bb,0x0ba,0x07c,0x038,0x07c,0x0ba,0x1bb};
    case u'Ф': return {0x038,0x0fe,0x1bb,0x1bb,0x1bb,0x0fe,0x038};
    case u'Ш':
    case u'Щ': return {0x1bb,0x092,0x092,0x092,0x092,0x092,0x1ff};
    case u'Ы': return {0x1db,0x0c6,0x0c6,0x0f6,0x0de,0x0de,0x1f7};
    case u'Ю': return {0x1de,0x0f3,0x0f3,0x0ff,0x0f3,0x0f3,0x1de};
    default: return {};
  }
}

struct Glyph {
  Image image;
  Sprite sprite{&image};
};
// Map nodes never move: every Sprite's Image pointer stays valid.
std::map<int, Glyph> glyphs;

uint32_t rowInk(const Sprite& source, int y) {
  int best = -1;
  uint32_t color = nativeColor(0xffed6800u);
  const auto& im = *source.sheet;
  if (source.srcY + y < 0 || source.srcY + y >= im.h) return color;
  for (int x = std::max<int>(0, source.srcX); x < std::min<int>(im.w, source.srcX + source.width); ++x) {
    uint32_t c = im.px[(source.srcY + y) * im.pitch() + x];
    const int strength = int(c >> 24) * 1024 + (c & 255) + ((c >> 8) & 255) + ((c >> 16) & 255);
    if ((c >> 24) > 96 && strength > best) { best = strength; color = c | 0xff000000u; }
  }
  return color;
}
}  // namespace

const char16_t* pspLocalized(int bank, int index) {
  if (index == kRussianName) return u"Русский";
  if (index == kUkrainianName) return u"Українська";
  if (index < 0 || index >= 293) return nullptr;
  if (bank == kRussianBank) return kRussian[index];
  if (bank == kUkrainianBank) return kUkrainian[index];
  const int* entry = std::lower_bound(std::begin(kControlIds), std::end(kControlIds), index);
  if (entry == std::end(kControlIds) || *entry != index) return nullptr;
  const auto n = entry - kControlIds;
  switch (bank) {
    case 10240: return kEnglishControls[n];     // RP10
    case 12288: return kFrenchControls[n];      // RP12
    case 33792: return kSpanishControls[n];     // RP33
    case 29696: return kGermanControls[n];      // RP29
    case 30720: return kItalianControls[n];     // RP30
    case 32768: return kPortugueseControls[n];  // RP32
    default: return nullptr;
  }
}

bool pspSmallCap(char16_t c) { return c != upper(c); }

char16_t pspLatinTwin(char16_t c) {
  if (c == u'’') return u'\'';
  constexpr char16_t cyr[] = u"АВЕКМНОРСТХІ";
  constexpr char16_t lat[] = u"ABEKMHOPCTXI";
  char16_t cap = upper(c);
  for (unsigned i = 0; cyr[i]; ++i)
    if (cyr[i] == cap) return lat[i] + (pspSmallCap(c) ? 32 : 0);
  return 0;
}

Sprite* pspDrawnGlyph(char16_t c, int fontHeight, const Sprite* exemplar) {
  char16_t cap = upper(c);
  auto shape = std::find_if(std::begin(shapes), std::end(shapes), [cap](const Shape& g) { return g.letter == cap; });
  if (shape == std::end(shapes) || !exemplar || !exemplar->sheet) return nullptr;
  auto [it, fresh] = glyphs.try_emplace((fontHeight << 16) | c);
  auto& g = it->second;
  if (!fresh) return &g.sprite;

  const int width = shape->width + (fontHeight != 15 ? 2 : 1);
  const int body = exemplar->height;
  const int top = shape->accent ? 2 : 0;
  const int height = top + body + (shape->feet ? 1 : 0);
  const auto wide = wideRows(cap);
  g.image.w = width;
  g.image.h = height;
  g.image.px.resize(width * height);
  for (int y = 0; y < body; ++y) {
    const int r = (2*y + 1) * 7 / (2*body);
    const uint16_t mask = shape->width == 9 ? wide[r] : shape->rows[r];
    const uint32_t ink = rowInk(*exemplar, y);
    for (int x = 0; x < width; ++x) {
      const int bit = shape->width - 1 - (2*x + 1) * shape->width / (2*width);
      if (mask & (1u << bit)) g.image.px[(top + y) * width + x] = ink;
    }
  }
  const uint32_t accent = rowInk(*exemplar, 0);
  if (shape->accent == 1) {
    g.image.px[width / 3] = g.image.px[width - 1 - width / 3] = accent;
  } else if (shape->accent == 2) {
    int m = width / 2;
    g.image.px[m - 1] = g.image.px[m + 1] = g.image.px[width + m] = accent;
  } else if (shape->accent == 3) {
    g.image.px[width - 2] = g.image.px[width * 2 - 2] = accent;
  }
  if (shape->feet) {
    const int base = (height - 1) * width;
    const uint32_t ink = rowInk(*exemplar, body - 1);
    if (shape->feet == 65) g.image.px[base] = ink;
    g.image.px[base + width - 1] = ink;
  }
  g.sprite.width = width;
  g.sprite.height = height;
  g.sprite.offsetY = exemplar->offsetY - top;
  g.sprite.advance = width + 1;
  return &g.sprite;
}
}  // namespace gow
