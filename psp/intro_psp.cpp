#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspdmac.h>
#include <pspge.h>
#include <pspkernel.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gfx.h"

namespace {
using namespace gow;
constexpr int rate = 44100, block = 512;
constexpr int packedBytes = kPspWidth * kScreenH * sizeof(uint16_t);
constexpr int bufferBytes = kPspPitch * kScreenH * sizeof(uint16_t);
constexpr unsigned skipButtons = PSP_CTRL_START | PSP_CTRL_CROSS;

uint32_t word(const uint8_t* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool inflateExact(const uint8_t* src, size_t bytes, void* dest, size_t expected) {
  uLongf actual = expected;
  return uncompress(static_cast<Bytef*>(dest), &actual, src, bytes) == Z_OK && actual == expected;
}

struct IntroAudio {
  int channel = -1, thread = -1;
  const uint8_t* samples = nullptr;
  size_t bytes = 0;
  std::atomic<bool> stop{false};
  unsigned submitted = 0;

  static int run(SceSize, void* arg) {
    auto& self = **static_cast<IntroAudio**>(arg);
    alignas(64) int16_t pcm[2][2 * block];
    int slot = 0;
    for (size_t pos = 0; pos < self.bytes && !self.stop; pos += sizeof pcm[0]) {
      size_t len = std::min(self.bytes - pos, sizeof pcm[0]);
      std::memcpy(pcm[slot], self.samples + pos, len);
      std::memset(reinterpret_cast<uint8_t*>(pcm[slot]) + len, 0, sizeof pcm[0] - len);
      sceKernelDcacheWritebackRange(pcm[slot], sizeof pcm[0]);
      if (sceAudioOutputBlocking(self.channel, PSP_AUDIO_VOLUME_MAX, pcm[slot]) < 0) break;
      ++self.submitted;
      slot ^= 1;
    }
    // Keep the thread stack alive until the audio device has finished reading.
    while (sceAudioGetChannelRestLen(self.channel) > 0) sceKernelDelayThread(1000);
    return 0;
  }

  void start(const std::vector<uint8_t>& pcm) {
    if (pcm.empty()) return;
    samples = pcm.data();
    bytes = pcm.size();
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, block, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) return;
    thread = sceKernelCreateThread("GOW intro audio", run, 0x19, 0x4000, 0, nullptr);
    if (thread < 0) return;
    IntroAudio* self = this;
    if (sceKernelStartThread(thread, sizeof self, &self) < 0) {
      sceKernelDeleteThread(thread);
      thread = -1;
    }
  }

  void finish() {
    stop = true;
    if (thread >= 0) {
      sceKernelWaitThreadEnd(thread, nullptr);
      sceKernelDeleteThread(thread);
    }
    if (channel >= 0) sceAudioChRelease(channel);
  }
};
}  // namespace

// PINT v1: 40-byte header, one absolute end offset per RGB565 zlib frame,
// compressed audio length, frame streams, then a zlib stream of stereo PCM.
unsigned playIntro(bool (*cancel)(), void (*log)(const char*)) {
  FILE* f = std::fopen("INTRO.BIN", "rb");
  if (!f) return 0;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::rewind(f);
  const bool sizeOk = size >= 44 && size <= 4 * 1024 * 1024;
  std::vector<uint8_t> file(sizeOk ? (size_t)size : 0);
  const bool read = sizeOk && std::fread(file.data(), 1, file.size(), f) == file.size();
  std::fclose(f);
  const auto* p = file.data();
  if (!read || std::memcmp(p, "PINT", 4) || word(p+4) != 1 ||
      word(p+8) != kPspWidth || word(p+12) != kScreenH) {
    log("intro ignored: invalid PINT header");
    return 0;
  }
  const uint32_t count = word(p+16), fps = word(p+20), audioBytes = word(p+32), start = word(p+36);
  if (!count || count > 600 || !fps || fps > 60 || count > 10 * fps ||
      word(p+24) != rate || word(p+28) != 2 || audioBytes % 4 || audioBytes > rate * 4 * 10 ||
      start != 44 + 4 * count || start > file.size()) {
    log("intro ignored: unsupported PINT layout");
    return 0;
  }
  std::vector<uint32_t> offsets(count + 1);
  offsets[0] = start;
  for (uint32_t i = 1; i <= count; ++i) {
    offsets[i] = word(p + 36 + 4 * i);
    if (offsets[i] <= offsets[i-1] || offsets[i] > file.size()) {
      log("intro ignored: invalid frame table");
      return 0;
    }
  }
  const uint32_t audioPacked = word(p + 40 + 4 * count);
  if (audioPacked != file.size() - offsets.back() || (!!audioPacked != !!audioBytes)) {
    log("intro ignored: invalid audio block");
    return 0;
  }
  std::vector<uint8_t> sound(audioBytes);
  if (audioBytes && !inflateExact(p + offsets.back(), audioPacked, sound.data(), sound.size())) {
    log("intro audio damaged; playing video only");
    sound.clear();
  }
  std::vector<uint16_t> packed(packedBytes / 2), padded(bufferBytes / 2);
  const auto vram = reinterpret_cast<uintptr_t>(sceGeEdramGetAddr());
  sceDisplaySetMode(0, kPspWidth, kScreenH);
  IntroAudio audio;
  unsigned shown = 0, skip = 0;
  bool badFrame = false;
  int submitted = -1;
  uint64_t t0 = 0;
  log("intro playing");
  for (uint32_t i = 0; i < count;) {
    const auto now = sceKernelGetSystemTimeWide();
    if (cancel()) break;
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(&pad, 1);
    if ((skip = pad.Buttons & skipButtons)) break;
    if (t0) {
      const uint32_t target = (uint32_t)((now - t0) * fps / 1000000u);
      if (target < i) { sceKernelDelayThread(2000); continue; }
      i = std::min(target, count - 1);
    }
    if (!inflateExact(p + offsets[i], offsets[i+1] - offsets[i], packed.data(), packedBytes)) {
      badFrame = true;
      break;
    }
    for (int y = 0; y < kScreenH; ++y)
      std::memcpy(padded.data() + y*kPspPitch, packed.data() + y*kPspWidth, kPspWidth * 2);
    if (submitted == sceDisplayGetVcount()) sceDisplayWaitVblankStart();
    const uintptr_t dst = vram + (shown & 1) * bufferBytes;
    sceKernelDcacheWritebackRange(padded.data(), bufferBytes);
    if (sceDmacMemcpy(reinterpret_cast<void*>(dst), padded.data(), bufferBytes) < 0)
      std::memcpy(reinterpret_cast<void*>(dst | 0x40000000u), padded.data(), bufferBytes);
    asm volatile("sync" ::: "memory");
    sceDisplaySetFrameBuf(reinterpret_cast<void*>(dst), kPspPitch, PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
    submitted = sceDisplayGetVcount();
    if (!t0) {
      sceDisplayWaitVblankStart();
      t0 = sceKernelGetSystemTimeWide();
      audio.start(sound);
    }
    ++shown;
    ++i;
  }
  if (shown && !skip && !badFrame && !cancel()) {
    auto end = t0 + uint64_t(count) * 1000000 / fps;
    while (!cancel() && sceKernelGetSystemTimeWide() < end) sceKernelDelayThread(2000);
  }
  audio.finish();
  if (submitted == sceDisplayGetVcount()) sceDisplayWaitVblankStart();
  char msg[112];
  std::snprintf(msg, sizeof msg, "intro %s: %u/%u frames, %u PCM blocks",
                skip ? "skipped" : badFrame ? "decode error" : cancel() ? "interrupted" : "complete",
                shown, (unsigned)count, audio.submitted);
  log(msg);
  return skip;
}
