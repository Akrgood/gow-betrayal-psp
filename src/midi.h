#pragma once

#include <atomic>
#include "jrt.h"

namespace gow {
// A Standard MIDI File's channel messages, flattened through its tempo map.
struct MidiSequence {
  struct Event {
    double ms;
    uint32_t msg;  // status | data1 << 8 | data2 << 16
  };
  std::vector<Event> events;
  double lengthMs = 0;
  std::atomic<bool> playing{false};
};

std::shared_ptr<MidiSequence> parseMidi(const Arr<int8_t>& data);
}  // namespace gow
