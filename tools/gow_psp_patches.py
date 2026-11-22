#!/usr/bin/env python3
"""Specialize the generated Game.cpp at PSP build time; leave the PC port intact."""
import re
import sys
from pathlib import Path


def patch_game(text):
    def sub(old, new, count):
        nonlocal text
        n = text.count(old)
        if n != count:
            raise ValueError(f"{old!r}: expected {count} matches, found {n}")
        text = text.replace(old, new)

    sub('#include "classes.h"', '#include "gen/classes.h"\n#include "locale_psp.h"\n#include <algorithm>', 1)
    sub('#include "../scene.h"', '#include "scene.h"', 1)
    sub('this->setViewSize(240, 320);', 'this->setViewSize(240, kScreenH);', 1)
    sub('this->screenHeight = 320;', 'this->screenHeight = kScreenH;', 1)
    sub('Scene::create(240, 320)', 'Scene::create(viewW, kScreenH)', 2)
    # Keep the same height of the player above the lower edge of the viewport.
    sub(' - 266)', ' - (kScreenH - 54))', 3)
    sub('this->maxCameraY = (this->bx - 320);', 'this->maxCameraY = std::max(0, this->bx - kScreenH);', 1)
    # Camera-lock activation still uses the original phone viewport. A reverse
    # lock pins the old (and new) bottom edge at the same world coordinate.
    sub('  int phoneX = (this->phoneN >> 8);',
        '  int phoneX = (this->phoneN >> 8);\n'
        '  int phoneY = std::max(0, cameraY - (320 - kScreenH));', 1)
    sub('phoneX, cameraY, 240, 320', 'phoneX, phoneY, 240, 320', 2)
    sub('(phoneX + this->aH), (cameraY + this->aI), 240, 320',
        '(phoneX + this->aH), (phoneY + this->aI), 240, 320', 3)
    sub('this->lockY[var12] - cameraY', 'this->lockY[var12] - phoneY', 1)
    # Only visual bounds and bottom-anchored UI. Do not replace gameplay IDs
    # or the original phone viewport tests above.
    sub('viewW, 320)', 'viewW, kScreenH)', 10)
    sub('var1->setClip(0, 0, 240, 320)', 'var1->setClip(0, 0, 240, kScreenH)', 1)
    sub('<= 320))) {', '<= kScreenH))) {', 16)
    sub(' * 320) >> 8)', ' * kScreenH) >> 8)', 3)
    sub('((320 - (var3 << 1)) - 1)', '((kScreenH - (var3 << 1)) - 1)', 1)
    sub('(320 - var3)', '(kScreenH - var3)', 5)
    sub('(320 - (var3 - 1))', '(kScreenH - (var3 - 1))', 2)
    sub('0, uiX(), 320)', '0, uiX(), kScreenH)', 1)
    sub('((viewW - 240) - uiX()), 320)', '((viewW - 240) - uiX()), kScreenH)', 1)
    sub('((halfWidth + 5) + 25), 320)', '((halfWidth + 5) + 25), kScreenH)', 1)
    sub('viewW, 306)', 'viewW, kScreenH - 14)', 1)
    sub('  int var4 = (var2 ? 168 : 173);', '  int var4 = kScreenH - (var2 ? 152 : 147);', 1)
    sub('(-uiX()), 166, viewW, 140', '(-uiX()), kScreenH - 154, viewW, 140', 1)
    sub('var6 = (285 - (this->ir >> 8))', 'var6 = (kScreenH - 35 - (this->ir >> 8))', 1)
    sub('  int var4 = 302;', '  int var4 = kScreenH - 18;', 1)
    a = text.index('void Game::drawBottomBar(')
    b = text.index('void Game::drawProgressBar(')
    text = text[:a] + re.sub(r'\b(306|307|320)\b',
                            lambda m: {"306": "(kScreenH - 14)", "307": "(kScreenH - 13)",
                                       "320": "kScreenH"}[m[0]], text[a:b]) + text[b:]
    # Java used garbage collection. Free both level scenes on reset and when replaced.
    sub('this->scene = nullptr;', 'delete this->scene;\n        this->scene = nullptr;', 2)
    sub('this->bgScene = Scene::create(viewW, kScreenH);',
        'delete this->bgScene;\n        this->bgScene = Scene::create(viewW, kScreenH);', 1)
    sub('        this->scene = nullptr;\n  this->tileSheet = nullptr;',
        '  this->scene = nullptr;\n  delete this->bgScene;\n  this->bgScene = nullptr;\n  this->tileSheet = nullptr;', 1)
    # Keep every original locale in its original slot; append Russian/Ukrainian.
    sub('languageIndex < 5', 'languageIndex + 1 < languageStringBanks.length()', 1)
    sub('var3 <= 5); (var3++)) {\n        this->drawString(var1, this->getString(languageNameStrings[var3])',
        'var3 < languageNameStrings.length()); (var3++)) {\n'
        '        this->drawString(var1, this->getString(languageNameStrings[var3])', 1)
    sub('languageIndex > 5', 'languageIndex >= languageStringBanks.length()', 1)
    sub('(((languageIndex + 1) > 5) ? 0 : (languageIndex + 1))',
        '((languageIndex + 1) % languageStringBanks.length())', 1)
    sub('this->loadStringTable(languageStringBanks[languageIndex]);',
        'this->loadStringTable(languageStringBanks[languageIndex]);\n'
        '        Engine::writeRecord(kLanguageRecord, Arr<int8_t>::make(languageIndex));', 2)
    sub('this->loadStringTable(10240);', 'this->loadStringTable(languageStringBanks[languageIndex]);', 1)
    # The phone slid the menu left when Kratos' art would overlap it on a
    # portrait screen. The PSP has room at the right; share the title center.
    sub('  int var2 = 0;\n  int var3 = 0;\n'
        '  if ((this->screenWidth > this->screenHeight)) {\n'
        '    var2 = (-(this->screenWidth >> 3));\n  }\n', '', 1)
    sub('  if (((((-this->menuSprites[2]->offsetY) + this->iK) >= this->screenHeight) && (var2 == 0))) {\n'
        '    var3 = (-(this->screenWidth >> 3));\n  }\n', '', 1)
    sub('(halfWidth + var2)', 'halfWidth', 2)
    sub('            this->iO = jlang::max<int>((int)(this->arrowBase), (int)(((this->stringWidth(var20) >> 1) + 14)));\n'
        '          } else {\n            this->iO = this->arrowBase;\n', '', 1)
    sub('                  if ((var4 > this->iM)) {\n'
        '                    bq[2]->draw(var1, ((((halfWidth - this->iO) - (bq[2]->width << 1)) + var2) + var3),',
        '                  const int offset = std::max(this->arrowBase, (this->stringWidth(var20) + 1) / 2 + 12);\n'
        '                  if ((var4 > this->iM)) {\n'
        '                    bq[2]->draw(var1, halfWidth - offset - bq[2]->width,', 1)
    sub('((this->iJ + var2) + var3), this->iK', 'halfWidth, this->iK', 1)
    sub('((((halfWidth + this->iO) + bq[3]->width) + var2) + var3),',
        'halfWidth + offset,', 1)
    return text


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    target.write_text(patch_game(source.read_text(encoding="utf-8")), encoding="utf-8")
