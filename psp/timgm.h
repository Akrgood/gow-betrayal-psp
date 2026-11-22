#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "jrt.h"

namespace gow {
struct BakedTrack {
  uint32_t hash, midiBytes, offset, frames;
};

class TimGM {
 public:
  static constexpr int rate = 22050, packet = 256, samples = 505, tail = rate / 8;
  static constexpr size_t maxBytes = 1024 * 1024;

  bool load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::rewind(f);
    constexpr int header = 8 + 16 * 11;
    const bool sizeOk = size >= header && (size_t)size <= maxBytes;
    std::vector<uint8_t> bytes(sizeOk ? (size_t)size : 0);
    const bool read = sizeOk && std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    if (!read || std::memcmp(bytes.data(), "GWT1", 4) || word(bytes.data() + 4) != list_.size()) return false;
    size_t offset = header;
    for (size_t i = 0; i < list_.size(); ++i) {
      auto p = bytes.data() + 8 + 16 * i;
      BakedTrack t{word(p), word(p + 4), word(p + 8), word(p + 12)};
      if (!t.midiBytes || !t.frames || t.frames > 120 * rate || t.offset != offset) return false;
      offset += (t.frames + tail + samples - 1) / samples * packet;
      if (offset > bytes.size()) return false;
      list_[i] = t;
    }
    if (offset != bytes.size()) return false;
    data_.swap(bytes);
    return true;
  }

  const BakedTrack* find(const Arr<int8_t>& midi) const {
    if (data_.empty()) return nullptr;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < midi.length(); ++i) hash = (hash ^ (uint8_t)midi.data()[i]) * 16777619u;
    for (const auto& t : list_) if (t.hash == hash && t.midiBytes == (uint32_t)midi.length()) return &t;
    return nullptr;
  }

  const uint8_t* data(const BakedTrack& t) const { return data_.data() + t.offset; }
  size_t size() const { return data_.size(); }

 private:
  static uint32_t word(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  }
  std::array<BakedTrack, 11> list_{};
  std::vector<uint8_t> data_;
};

// IMA/DVI ADPCM mono WAV blocks, low nibble first. Each block restarts
// with its own predictor, so loop/end processing never needs to seek a file.
class ImaVoice {
 public:
  void start(const uint8_t* bytes, int count) {
    data_ = bytes;
    left_ = count;
    sample_ = 0;
    odd_ = false;
    a_ = sample();
    b_ = left_ ? sample() : a_;
  }

  int16_t output() {
    if (!odd_) { odd_ = true; return (int16_t)a_; }
    odd_ = false;
    const int result = (a_ + b_) / 2;
    a_ = b_;
    if (left_) b_ = sample();
    return (int16_t)result;
  }

 private:
  int sample() {
    static constexpr int steps[] = {
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
      73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,
      449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,
      2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,
      7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,
      24623,27086,29794,32767
    };
    static constexpr int adjust[] = {-1,-1,-1,-1,2,4,6,8};
    if (!sample_) {
      predictor_ = data_[0] | data_[1] << 8;
      if (predictor_ & 32768) predictor_ -= 65536;
      index_ = std::min<int>(88, data_[2]);
    } else {
      const int n = sample_ - 1;
      const int code = (data_[4 + (n >> 1)] >> ((n & 1) * 4)) & 15;
      const int delta = (steps[index_] * (2 * (code & 7) + 1)) >> 3;
      predictor_ = std::clamp(predictor_ + (code & 8 ? -delta : delta), -32768, 32767);
      index_ = std::clamp(index_ + adjust[code & 7], 0, 88);
    }
    --left_;
    if (++sample_ == TimGM::samples) { sample_ = 0; data_ += TimGM::packet; }
    return predictor_;
  }
  const uint8_t* data_ = nullptr;
  int left_ = 0, sample_ = 0, predictor_ = 0, index_ = 0;
  int a_ = 0, b_ = 0;
  bool odd_ = false;
};
}  // namespace gow
