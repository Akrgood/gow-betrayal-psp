#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspdmac.h>
#include <pspge.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspthreadman.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <malloc.h>
#include <string>
#include <unistd.h>

#include "gen/classes.h"
#include "locale_psp.h"

PSP_MODULE_INFO("GoW Betrayal", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(16 * 1024);

using namespace gow;
bool initAudio();
bool audioProducedSignal();
bool audioProducedTimGM();
void shutdownAudio();
unsigned playIntro(bool (*cancel)(), void (*log)(const char*));

namespace {
constexpr const char* kBuild = "psp-008";
constexpr int kFrameBytes = kPspPitch * kScreenH * sizeof(uint32_t);
static_assert(2 * kFrameBytes <= 2 * 1024 * 1024, "Must fit in PSP-1000 VRAM");
int fps = 25;
std::atomic<bool> exiting{false}, sleeping{false};

void log(const char* message) {
  if (FILE* f = std::fopen("gow-psp.log", "a")) {
    std::fprintf(f, "[%s] %s\n", kBuild, message);
    std::fclose(f);
  }
}

int exitCallback(int, int, void*) { exiting = true; return 0; }
int powerCallback(int, int flags, void*) {
  if (flags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) sleeping = true;
  if (flags & PSP_POWER_CB_RESUME_COMPLETE) sleeping = false;
  return 0;
}

int callbacks(SceSize, void*) {
  int cb = sceKernelCreateCallback("GOW exit", exitCallback, nullptr);
  if (cb >= 0) sceKernelRegisterExitCallback(cb);
  cb = sceKernelCreateCallback("GOW power", powerCallback, nullptr);
  if (cb >= 0) scePowerRegisterCallback(-1, cb);
  sceKernelSleepThreadCB();
  return 0;
}

// The CPU rasterizes directly in LCD pixel coordinates and native color format.
// Copy the completed image to the back buffer without any scaling or conversion.
class Video {
 public:
  void init() {
    base = reinterpret_cast<uintptr_t>(sceGeEdramGetAddr());
    sceDisplaySetMode(0, kPspWidth, kScreenH);
    std::memset(uncached(0), 0, kFrameBytes * 2);
    asm volatile("sync" ::: "memory");
    sceDisplaySetFrameBuf(address(0), kPspPitch, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    submitted = sceDisplayGetVcount();
  }
  void present(const Surface& s) {
    // The old front buffer becomes writable only once the queued flip has latched.
    if (sceDisplayGetVcount() == submitted) sceDisplayWaitVblankStart();
    if (!cpuCopy) {
      sceKernelDcacheWritebackRange(s.px.data(), kFrameBytes);
      if (sceDmacMemcpy(address(back), s.px.data(), kFrameBytes) < 0) {
        cpuCopy = true;
        log("DMAC unavailable: using CPU framebuffer copy");
      }
    }
    if (cpuCopy) std::memcpy(uncached(back), s.px.data(), kFrameBytes);
    asm volatile("sync" ::: "memory");
    sceDisplaySetFrameBuf(address(back), kPspPitch, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    submitted = sceDisplayGetVcount();  // conservative if submission crossed a vblank
    back ^= 1;
  }
 private:
  void* address(int i) const { return reinterpret_cast<void*>(base + i * kFrameBytes); }
  void* uncached(int i) const { return reinterpret_cast<void*>((base | 0x40000000u) + i * kFrameBytes); }
  uintptr_t base = 0;
  int submitted = 0;
  int back = 1;
  bool cpuCopy = false;
};

struct Controls {
  bool sent[8]{};
  unsigned suppress = 0;
  static constexpr int codes[] = {-1, -2, -3, -4, -5, -6, -7, 35};
  void poll(Game& game, bool reset = false) {
    SceCtrlData pad{};
    if (!reset) sceCtrlPeekBufferPositive(&pad, 1);
    else pad.Lx = pad.Ly = 128;
    suppress &= pad.Buttons;
    unsigned b = pad.Buttons & ~suppress;
    bool menu = Game::state != 100;
    bool now[] = {
      (b & PSP_CTRL_UP) || pad.Ly < 64 || (!menu && (b & PSP_CTRL_CROSS)),
      (b & PSP_CTRL_DOWN) || pad.Ly > 192 || (!menu && (b & PSP_CTRL_CIRCLE)),
      (b & PSP_CTRL_LEFT) || pad.Lx < 64,
      (b & PSP_CTRL_RIGHT) || pad.Lx > 192,
      (b & PSP_CTRL_SQUARE) != 0,
      (b & (PSP_CTRL_TRIANGLE | PSP_CTRL_RTRIGGER)) || (menu && (b & PSP_CTRL_CROSS)),
      (b & PSP_CTRL_START) || (menu && (b & PSP_CTRL_CIRCLE)),
      (b & (PSP_CTRL_LTRIGGER | PSP_CTRL_SELECT)) != 0,
    };
    for (int i = 0; i < 8; ++i) {
      if (sent[i] != now[i]) game.platformKey(codes[i], sent[i] = now[i]);
    }
  }
};

void showError(const char* message) {
  log(message);
  pspDebugScreenInit();
  pspDebugScreenPrintf("God of War: Betrayal [%s]\n\n%s\n\nPress START to quit.", kBuild, message);
  while (!exiting) {
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(&pad, 1);
    if (pad.Buttons & PSP_CTRL_START) break;
    sceDisplayWaitVblankStart();
  }
}

double nowMs() { return sceKernelGetSystemTimeWide() / 1000.0; }
}  // namespace

String Port::label(int row) {
  if (Game::languageIndex == 6) {
    if (row == 0) return String(u"Экран: 480x272");
    if (row == 1) return String(u"Масштаб: 1:1");
    return String(fps == 30 ? u"Кадры: 30" : u"Кадры: 25");
  }
  if (Game::languageIndex == 7) {
    if (row == 0) return String(u"Екран: 480x272");
    if (row == 1) return String(u"Масштаб: 1:1");
    return String(fps == 30 ? u"Кадри: 30" : u"Кадри: 25");
  }
  if (row == 0) return String(u"Screen: 480x272");
  if (row == 1) return String(u"Pixel scale: 1:1");
  return String(fps == 30 ? u"FPS: 30" : u"FPS: 25");
}
void Port::change(int row, int) {
  if (row == 2) fps = fps == 30 ? 25 : 30;
}
int Port::viewWidth() { return kPspWidth; }
int Port::displayFps() { return fps; }
Arr<int> Port::extendMenu(const Arr<int>& t) {
  // Generated Game.cpp expects the original quit entry (23) shifted by three.
  constexpr int at = 22, n = 3;
  if (t.length() <= at) return t;
  Arr<int> o(t.length() + n);
  for (int i = 0; i < t.length(); ++i) {
    int e = t[i], target = (e >> 8) & 255;
    if (target >= at) e = (e & ~0xff00) | (((target + n) & 255) << 8);
    o[i < at ? i : i + n] = e;
  }
  for (int i = 0; i < n; ++i) o[at + i] = ((13 + i) << 26) | 0x3000000;
  return o;
}

int main(int argc, char** argv) {
  if (argc && argv[0]) {
    std::string exe = argv[0];
    auto slash = exe.find_last_of('/');
    if (slash != std::string::npos) chdir(exe.substr(0, slash).c_str());
  }
  log("boot");
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
  auto th = sceKernelCreateThread("GOW callbacks", callbacks, 0x11, 0x1000, 0, nullptr);
  if (th >= 0) sceKernelStartThread(th, 0, nullptr);
  Platform::dataDir = "data";
  try {
    unsigned skip = playIntro([] { return exiting || sleeping; }, log);
    if (exiting) {
      sceKernelExitGame();
      return 0;
    }
    GOWMIDlet midlet;
    midlet.startApp();
    auto& game = *static_cast<Game*>(midlet.game);
    if (!game.ok() || !game.supportedBuild()) {
      showError("Missing or incompatible data/ banks.\nUse the K800i 1.4.20 JAR with prepare_data.py.");
    } else {
      Game::languageStringBanks = Arr<int>::make(10240, 12288, 33792, 29696, 30720, 32768,
                                                kRussianBank, kUkrainianBank);
      Game::languageNameStrings = Arr<int>::make(23, 24, 21, 20, 22, 19, kRussianName, kUkrainianName);
      auto language = Engine::readRecord(kLanguageRecord);
      if (language.length() == 1 && language[0] >= 0 && language[0] < Game::languageStringBanks.length())
        Game::languageIndex = language[0];
      Surface screen(kPspWidth, kScreenH);
      Video video;
      Controls input;
      input.suppress = skip;
      video.init();
      log(initAudio() ? "audio ready" : "audio unavailable");
      log("game initialized");
      bool paused = false;
      bool loggedAudio = false;
      bool loggedTimGM = false;
      int lastState = Game::state;
      double reportAt = nowMs() + 5000;
      long startTicks = 0, startPaints = 0;
      uint32_t engineUs = 0, copyUs = 0, maxEngineUs = 0, maxCopyUs = 0;
      unsigned calls = 0;
      while (!exiting && !game.quit) {
        if (sleeping) {
          if (!paused) { input.poll(game, true); game.shown = false; game.hideNotify(); paused = true; }
          sceKernelDelayThread(10000);
          continue;
        }
        if (paused) { game.shown = true; game.showNotify(); paused = false; }
        input.poll(game);
        auto begin = sceKernelGetSystemTimeWide();
        const double start = begin / 1000.0;
        auto old = game.paintCount;
        int wait = game.runFrame(start, screen);
        auto painted = sceKernelGetSystemTimeWide();
        if (game.paintCount != old) video.present(screen);
        auto end = sceKernelGetSystemTimeWide();
        uint32_t engine = painted - begin, copy = end - painted;
        engineUs += engine;
        copyUs += copy;
        maxEngineUs = std::max(maxEngineUs, engine);
        maxCopyUs = std::max(maxCopyUs, copy);
        ++calls;
        if (!loggedAudio && audioProducedSignal()) { loggedAudio = true; log("MIDI PCM submitted"); }
        if (!loggedTimGM && audioProducedTimGM()) { loggedTimGM = true; log("TimGM6mb PCM submitted"); }
        if (lastState != Game::state || start >= reportAt) {
          char info[180];
          if (lastState != Game::state) {
            lastState = Game::state;
            std::snprintf(info, sizeof info, "state=%d", lastState);
            log(info);
          }
          if (start >= reportAt) {
            std::snprintf(info, sizeof info, "5s: state=%d ticks=%ld paints=%ld heap=%lu player=%d,%d action=%d",
                         lastState, game.tickCount - startTicks, game.paintCount - startPaints,
                         (unsigned long)mallinfo().uordblks, game.playerX, game.playerY, game.playerAction);
            log(info);
            std::snprintf(info, sizeof info, "us/call core=%u peak=%u copy=%u peak=%u",
                          (unsigned)(engineUs / calls), (unsigned)maxEngineUs,
                          (unsigned)(copyUs / calls), (unsigned)maxCopyUs);
            log(info);
            engineUs = copyUs = maxEngineUs = maxCopyUs = calls = 0;
            reportAt = start + 5000;
            startTicks = game.tickCount;
            startPaints = game.paintCount;
          }
        }
        // The engine returns time until the next deadline, including drawing.
        const double deadline = start + wait;
        while (!exiting && !sleeping) {
          int remaining = (int)((deadline - nowMs()) * 1000);
          if (remaining <= 0) break;
          sceKernelDelayThread(std::min(remaining, 8000));
          input.poll(game);
        }
      }
      if (!game.quit) game.postLifecycle(3);
    }
  } catch (const JavaException& e) {
    showError(e.what);
  } catch (const std::exception& e) {
    showError(e.what());
  }
  shutdownAudio();
  log("exit");
  sceKernelExitGame();
  return 0;
}
