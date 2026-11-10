// NamCache.h - a cached A2-Lite network (PRESETS.md, "The weights cache"):
// what readNamA2Lite() got out of a .nam file, saved next to it so the next
// load reads 7.5 KB instead of parsing ~670 KB of JSON (~10 ms against
// ~0.9 s on the Teensy).
//
// The file: a NamCacheHeader, the NamInfo, then the nama2::kWeightCount
// weights as raw floats (this firmware's own layout, little-endian). The
// header names the .nam file it was made from by its size and modified
// time, and carries a checksum of the rest, so a cache for another version
// of the file, or a damaged one, is never used: it costs one slow load, never
// a wrong sound.
//
// Portable C++, no Arduino; the firmware writes and reads SD files with it,
// the host tests memory buffers.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "IRLoader.h"  // ByteSource
#include "NamA2.h"
#include "NamFile.h"

namespace irdsp {

struct NamCacheHeader {
  uint32_t magic;        // kNamCacheMagic
  uint16_t version;      // kNamCacheVersion
  uint16_t infoSize;     // sizeof(NamInfo)
  uint32_t weightCount;  // nama2::kWeightCount
  uint32_t srcSize;      // the .nam file's size in bytes
  uint32_t srcStamp;     // its modified time, packed (0 if the card has none)
  uint32_t checksum;     // FNV-1a over the NamInfo and the weights
};

constexpr uint32_t kNamCacheMagic = 0x434D414Eu;  // "NAMC"
constexpr uint16_t kNamCacheVersion = 1;
constexpr int kNamCacheBytes = (int)(sizeof(NamCacheHeader) + sizeof(NamInfo) + nama2::kWeightCount * sizeof(float));

inline uint32_t namCacheChecksum(const void* data, size_t n, uint32_t h = 2166136261u) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

inline NamCacheHeader namCacheHeader(uint32_t srcSize, uint32_t srcStamp, const NamInfo& info, const float* weights) {
  NamCacheHeader h;
  memset(&h, 0, sizeof(h));
  h.magic = kNamCacheMagic;
  h.version = kNamCacheVersion;
  h.infoSize = (uint16_t)sizeof(NamInfo);
  h.weightCount = nama2::kWeightCount;
  h.srcSize = srcSize;
  h.srcStamp = srcStamp;
  h.checksum = namCacheChecksum(weights, nama2::kWeightCount * sizeof(float), namCacheChecksum(&info, sizeof(NamInfo)));
  return h;
}

// Reads a cache made from a .nam file of srcSize bytes modified at srcStamp.
// False if it is for another file or version, cut short or damaged; weights
// may then be partly written, info is untouched.
inline bool readNamCache(ByteSource& src, uint32_t srcSize, uint32_t srcStamp, float* weights, NamInfo* info) {
  NamCacheHeader h;
  if (src.read(&h, sizeof(h)) != (int)sizeof(h)) return false;
  if (h.magic != kNamCacheMagic || h.version != kNamCacheVersion || h.infoSize != sizeof(NamInfo) ||
      h.weightCount != (uint32_t)nama2::kWeightCount || h.srcSize != srcSize || h.srcStamp != srcStamp)
    return false;
  NamInfo in;
  if (src.read(&in, sizeof(in)) != (int)sizeof(in)) return false;
  const int wBytes = nama2::kWeightCount * (int)sizeof(float);
  if (src.read(weights, wBytes) != wBytes) return false;
  if (namCacheChecksum(weights, wBytes, namCacheChecksum(&in, sizeof(in))) != h.checksum) return false;
  in.name[sizeof(in.name) - 1] = 0;  // the strings stay terminated whatever was on the card
  in.gearMake[sizeof(in.gearMake) - 1] = 0;
  in.gearModel[sizeof(in.gearModel) - 1] = 0;
  in.gearType[sizeof(in.gearType) - 1] = 0;
  in.toneType[sizeof(in.toneType) - 1] = 0;
  *info = in;
  return true;
}

}  // namespace irdsp
