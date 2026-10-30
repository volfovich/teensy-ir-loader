// SdSource.h - an SD card file as an irdsp::ByteSource, for the loaders
// (IRLoader.h, NamFile.h).
#pragma once

#include <SD.h>

#include "IRLoader.h"

class SdSource : public irdsp::ByteSource {
 public:
  explicit SdSource(File& f) : f_(f) {}
  int read(void* dst, int n) override { return f_.read(dst, n); }
  bool seek(uint32_t pos) override { return f_.seek(pos); }

 private:
  File& f_;
};
