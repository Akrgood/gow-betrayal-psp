// Host check: all Russian/Ukrainian letters and added punctuation are drawable.
#include "locale_psp.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

int main() {
  using namespace gow;
  Image exemplarImage{9, 11, std::vector<uint32_t>(9 * 11, nativeColor(0xfff25000u))};
  Sprite exemplar(&exemplarImage);
  exemplar.width = 9;
  exemplar.height = 11;
  for (auto c : std::u16string_view(
         u"АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюяҐґЄєІіЇї«»—")) {
    if (pspLatinTwin(c)) continue;
    auto* glyph = pspDrawnGlyph(c, 17, &exemplar);
    if (!glyph || glyph->advance < glyph->width ||
        std::none_of(glyph->sheet->px.begin(), glyph->sheet->px.end(), [](uint32_t p) { return p != 0; })) {
      std::fprintf(stderr, "Missing glyph U+%04x\n", (unsigned)c);
      return 1;
    }
  }
  for (int bank : {kRussianBank, kUkrainianBank}) {
    for (int index = 0; index < 293; ++index) {
      if (auto s = pspLocalized(bank, index)) {
        for (char16_t c : std::u16string_view(s)) {
          if (c < 128 || pspLatinTwin(c) || pspDrawnGlyph(c, 17, &exemplar)) continue;
          std::fprintf(stderr, "Missing glyph U+%04x in string %d, bank %d\n", (unsigned)c, index, bank);
          return 2;
        }
      }
    }
  }
  constexpr int controlIds[] = {114, 124, 137, 178, 180, 181, 183, 188, 213, 214, 217, 218, 219, 220, 255};
  for (int bank : {10240, 12288, 33792, 29696, 30720, 32768}) {
    for (int index = 0; index < 293; ++index) {
      auto s = pspLocalized(bank, index);
      if (!!s != std::binary_search(std::begin(controlIds), std::end(controlIds), index)) return 3;
      if (s && index != 114 && s[0] != u'^') return 4;
    }
    auto s = std::u16string_view(pspLocalized(bank, 114));
    for (char16_t button : std::u16string_view(u"□×○△")) {
      if (s.find(button) == s.npos || !pspDrawnGlyph(button, 17, &exemplar)) return 5;
    }
  }
  std::puts("All eight control guides are present; Cyrillic and PSP button glyphs verified");
}
