#pragma once
#include "engine.h"

namespace gow {
constexpr int kRussianBank = -2, kUkrainianBank = -3;
constexpr int kRussianName = 293, kUkrainianName = 294;
constexpr int kLanguageRecord = -10;

const char16_t* pspLocalized(int bank, int index);
char16_t pspLatinTwin(char16_t c);
bool pspSmallCap(char16_t c);
Sprite* pspDrawnGlyph(char16_t c, int fontHeight, const Sprite* exemplar);
}
