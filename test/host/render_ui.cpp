// Renders every OLED screen to PNG (6x scale), from the real UI code with a
// fake model, plus an overview sheet.
//   make -C test/host render        -> test/host/out/
//   make -C test/host screenshots   -> screenshots/ (the repo's pictures)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "Canvas.h"
#include "Menu.h"
#include "Screens.h"
#include "ui_fake.h"

// ---- Minimal PNG writer (8-bit grayscale, zlib-compressed) --------------------

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}

static void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  put32(out, (uint32_t)data.size());
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  out.insert(out.end(), td.begin(), td.end());
  put32(out, (uint32_t)crc32(0, td.data(), (uInt)td.size()));
}

static bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& gray) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), gray.begin() + (size_t)y * w, gray.begin() + (size_t)(y + 1) * w);
  }
  uLongf zLen = compressBound((uLong)raw.size());
  std::vector<uint8_t> z(zLen);
  if (compress2(z.data(), &zLen, raw.data(), (uLong)raw.size(), 9) != Z_OK) return false;
  z.resize(zLen);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
  put32(ihdr, w); put32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fwrite(png.data(), 1, png.size(), f);
  std::fclose(f);
  return true;
}

// ---- Screens ---------------------------------------------------------------------

static const int kScale = 6, kBorder = 4;  // OLED pixels of black border
static const int kImgW = (ui::Canvas::kW + 2 * kBorder) * kScale, kImgH = (ui::Canvas::kH + 2 * kBorder) * kScale;

// One screen as an image: each lit pixel a 5x5 dot on a 6x6 grid.
static std::vector<uint8_t> image(const ui::Canvas& c) {
  std::vector<uint8_t> g((size_t)kImgW * kImgH, 0);
  for (int y = 0; y < ui::Canvas::kH; y++)
    for (int x = 0; x < ui::Canvas::kW; x++)
      if (c.get(x, y))
        for (int j = 0; j < kScale - 1; j++)
          for (int i = 0; i < kScale - 1; i++)
            g[(size_t)((y + kBorder) * kScale + j) * kImgW + (x + kBorder) * kScale + i] = 235;
  return g;
}

struct Shot { std::string name; std::vector<uint8_t> img; };

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "out";
  static uint8_t buf[1024];
  ui::Canvas c(buf);
  std::vector<Shot> shots;
  auto snap = [&](const char* name) { shots.push_back({name, image(c)}); };

  ui::UiModel m = fakeModel();
  FakeHost host;
  ui::Menu menu(ui::menuPages(), host);

  ui::drawSplash(c, m);
  snap("01_splash");

  c.clear(); ui::drawHome(c, m, false); ui::drawHomeMeters(c, m);
  snap("02_home");

  ui::UiModel b = m;
  b.irShown = 4;
  c.clear(); ui::drawHome(c, b, true); ui::drawHomeMeters(c, b);
  snap("03_home_browsing");

  ui::UiModel x = m;
  x.bypass = true; x.delayOn = false;
  x.in.update(0.99f, false, 10000);
  x.out.update(1.0f, true, 10000);
  c.clear(); ui::drawHome(c, x, false); ui::drawHomeMeters(c, x);
  snap("04_home_bypass_clip");

  menu.open(); menu.turn(1, 0);
  menu.draw(c);
  snap("05_menu");

  menu.push(); menu.turn(3, 0);
  menu.draw(c);
  snap("06_menu_delay");

  menu.turn(6, 0);
  menu.draw(c);
  snap("06b_menu_delay_scrolled");

  menu.turn(-6, 0); menu.push();
  menu.draw(c);
  snap("07_edit_delay_time");
  menu.back(); menu.back(); menu.back();

  menu.open(); menu.turn(4, 0); menu.push(); menu.push();  // Levels > Out gain
  menu.draw(c);
  snap("08_edit_output_gain");
  menu.back();

  ui::drawIrBrowser(c, m, 2, 0, 0);
  snap("09_ir_browser");

  ui::drawMeterScreen(c, m);
  snap("10_meter");

  ui::drawStatus(c, m);
  snap("11_status");

  host.v[ui::kTrainerOn] = 1;
  menu.open(); menu.turn(3, 0); menu.push(); menu.turn(8, 0);  // Drums > Trainer
  menu.draw(c);
  snap("12_menu_drums");

  menu.turn(-7, 0); menu.push();  // Style
  menu.draw(c);
  snap("13_edit_drum_style");
  menu.back();

  menu.turn(2, 0); menu.push();  // Kit (after Song)
  menu.draw(c);
  snap("16_edit_drum_kit");
  menu.back();

  menu.turn(5, 0); menu.push(); menu.turn(2, 0);  // Trainer page, cursor on Every
  menu.draw(c);
  snap("14_menu_trainer");

  menu.turn(1, 0); menu.push();  // Target
  menu.draw(c);
  snap("15_edit_trainer_target");

  host.v[ui::kDrumSong] = 4;
  host.v[ui::kDrumLoopFrom] = 9;
  host.v[ui::kDrumLoopTo] = 16;
  menu.open(); menu.turn(3, 0); menu.push(); menu.turn(2, 0);  // Drums > Song
  menu.draw(c);
  snap("19_menu_drums_song");
  host.v[ui::kDrumSong] = 0;
  host.v[ui::kDrumLoopFrom] = 0;
  host.v[ui::kDrumLoopTo] = 32;

  menu.open(); menu.turn(2, 0); menu.push(); menu.turn(1, 0);  // Reverb > Type (80s hall)
  menu.draw(c);
  snap("17_menu_reverb_80s");

  host.v[ui::kReverbMode] = 1;
  menu.turn(100, 0);  // last row: Gate
  menu.draw(c);
  snap("18_menu_reverb_gated");
  host.v[ui::kReverbMode] = 0;

  int ok = 0;
  for (auto& s : shots) ok += writePng(dir + "/" + s.name + ".png", kImgW, kImgH, s.img);

  // Overview: 3 columns on a dark grey sheet.
  const int cols = 3, gap = 18, rows = ((int)shots.size() + cols - 1) / cols;
  const int ow = cols * kImgW + (cols + 1) * gap, oh = rows * kImgH + (rows + 1) * gap;
  std::vector<uint8_t> sheet((size_t)ow * oh, 48);
  for (size_t k = 0; k < shots.size(); k++) {
    const int ox = gap + (int)(k % cols) * (kImgW + gap), oy = gap + (int)(k / cols) * (kImgH + gap);
    for (int y = 0; y < kImgH; y++)
      std::memcpy(&sheet[(size_t)(oy + y) * ow + ox], &shots[k].img[(size_t)y * kImgW], kImgW);
  }
  ok += writePng(dir + "/00_overview.png", ow, oh, sheet);
  std::printf("wrote %d PNGs to %s/\n", ok, dir.c_str());
  return ok == (int)shots.size() + 1 ? 0 : 1;
}
