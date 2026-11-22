// Host-side check of the shared renderer's clipping, mirroring and alpha.
#include "gfx.h"

#include <cstdint>
#include <cstdio>
#include <random>

int main() {
  std::mt19937 rng(1420);
  auto random = [&](int lo, int hi) { return lo + (int)(rng() % (hi - lo + 1)); };
  gow::Surface got(19, 13);
  gow::Image src{17, 11, std::vector<uint32_t>(17 * 11)};
  for (auto& p : src.px) {
    const uint32_t a[] = {0, 255, 127};
    p = (rng() & 0xffffff) | (a[random(0, 2)] << 24);
  }
  for (int n = 0; n < 25000; ++n) {
    for (auto& p : got.px) p = rng();
    auto expected = got.px;
    got.resetTransform();
    int cx = random(-6, 23), cy = random(-6, 18);
    got.setClip(cx, cy, random(0, 23), random(0, 18));
    cx = got.getClipX();
    cy = got.getClipY();
    int ex = cx + got.getClipWidth(), ey = cy + got.getClipHeight();
    int tx = random(-5, 5), ty = random(-5, 5);
    got.translate(tx, ty);
    int sx = random(-6, 23), sy = random(-6, 18), rw = random(0, 25), rh = random(0, 20);
    int dx = random(-8, 23), dy = random(-8, 18), trans = n % 4;
    got.blit(src, sx, sy, rw, rh, trans, dx, dy);
    for (int j = 0; j < rh; ++j) {
      for (int i = 0; i < rw; ++i) {
        int x = dx + tx + i, y = dy + ty + j;
        int u = sx + ((trans & 2) ? rw - 1 - i : i), v = sy + ((trans & 1) ? rh - 1 - j : j);
        if (x < cx || x >= ex || y < cy || y >= ey || u < 0 || u >= src.w || v < 0 || v >= src.h) continue;
        uint32_t p = src.px[v * src.w + u], a = p >> 24;
        auto& d = expected[y * got.w + x];
        if (!a) continue;
        uint32_t rgb = 0xff000000;
        for (int shift = 0; shift < 24; shift += 8)
          rgb |= (((p >> shift & 255) * a + (d >> shift & 255) * (255 - a)) / 255) << shift;
        d = rgb;
      }
    }
    if (got.px != expected) {
      std::fprintf(stderr, "blit differs in case %d\n", n);
      return 1;
    }
  }
  gow::Surface lcd(480, 272);
#ifdef __PSP__
  if (lcd.pitch() != 512 || lcd.px.size() != 512u * 272u ||
      gow::nativeColor(0xffff0000u) != 0xff0000ffu ||
      gow::nativeColor(0xff0000ffu) != 0xffff0000u) return 2;
#endif
  // Test pixel placement and the padding to the next hardware row.
  lcd.setColor(0xff0000);
  lcd.fillRect(479, 0, 1, 1);
  auto rgb = gow::Arr<int>::make(0x0000ff, 0x00ff00, 0x0000ff, 0x00ff00);
  lcd.drawRGB(rgb, 0, 2, -1, 270, 2, 2, false);
  if (lcd.px[479] != gow::nativeColor(0xffff0000u) ||
      lcd.px[lcd.pitch()] != 0xff000000u ||
      lcd.px[lcd.pitch() * 270] != gow::nativeColor(0xff00ff00u) ||
      lcd.px[lcd.pitch() * 271] != gow::nativeColor(0xff00ff00u) ||
      lcd.px[lcd.pitch() * 271 + 1] != 0xff000000u) return 3;
  std::puts("25000 clipped blits matched the reference");
}
