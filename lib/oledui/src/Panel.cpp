#include "Panel.h"

#include <string.h>


namespace ui {

// The IR list's marquee waits this many steps at each end of the name.
static constexpr int kMarqueePause = 3;

bool Panel::toggle(uint8_t id) {
  host_.set(id, host_.get(id) ? 0 : 1);
  return true;
}

// The list opens on the loaded IR, about in the middle of the screen.
void Panel::openIrList(Screen from) {
  from_ = from;
  screen_ = kIrList;
  irCursor_ = m_.irCurrent >= 0 && m_.irCurrent < m_.irCount ? m_.irCurrent : 0;
  const int entries = m_.irCount + 1;  // the IRs and "Rescan SD card"
  int top = irCursor_ - kIrBrowserRows / 2;
  if (top > entries - kIrBrowserRows) top = entries - kIrBrowserRows;
  irTop_ = top < 0 ? 0 : top;
  marquee_ = 0;
}

bool Panel::menuResult(Menu::Result r) {
  switch (r) {
    case Menu::kExit:
      screen_ = kHome;
      return true;
    case Menu::kOpenScreen:
      if (menu_.screen() == kScreenIR) {
        openIrList(kMenu);
      } else {
        from_ = kMenu;
        screen_ = menu_.screen() == kScreenMeter ? kMeter : kStatus;
      }
      return true;
    case Menu::kRedraw:
      return true;
    default:
      return false;
  }
}

bool Panel::turn(int detents, uint32_t now) {
  if (detents == 0) return false;
  touch(now);
  switch (screen_) {
    case kHome: {
      if (tempoEdit_) {
        const int factor = (tempoTurnMs_ != 0 && now - tempoTurnMs_ < Menu::kFastMs) ? Menu::kFastFactor : 1;
        tempoTurnMs_ = now ? now : 1;
        int v = host_.get(kDrumTempo) + detents * factor;
        v = v < kTempoMin ? kTempoMin : (v > kTempoMax ? kTempoMax : v);
        host_.set(kDrumTempo, v);
        return true;
      }
      int b = block_ + detents;
      b = b < 0 ? 0 : (b >= kHomeBlocks ? kHomeBlocks - 1 : b);
      if (b == block_) return false;
      block_ = (uint8_t)b;
      return true;
    }
    case kMenu:
      return menuResult(menu_.turn(detents, now));
    case kIrList: {
      int c = irCursor_ + detents;
      c = c < 0 ? 0 : (c > m_.irCount ? m_.irCount : c);  // irCount: the Rescan row
      if (c == irCursor_) return false;
      irCursor_ = c;
      if (c < irTop_) irTop_ = c;
      if (c >= irTop_ + kIrBrowserRows) irTop_ = c - kIrBrowserRows + 1;
      marquee_ = 0;
      marqueeMs_ = now;
      return true;
    }
    case kMeter: {
      // The line-in sensitivity, 0..15 (Levels > Line-in sens).
      int v = host_.get(kLineIn) + detents;
      v = v < 0 ? 0 : (v > 15 ? 15 : v);
      host_.set(kLineIn, v);
      return true;
    }
    default:
      return false;
  }
}

bool Panel::push(uint32_t now) {
  touch(now);
  switch (screen_) {
    case kHome:
      if (block_ == kBlockTempo) {
        // Push starts editing the tempo in place, and push again keeps it.
        tempoEdit_ = !tempoEdit_;
        tempoOrig_ = host_.get(kDrumTempo);
        tempoTurnMs_ = 0;
        return true;
      }
      if (block_ == kBlockIR) {
        openIrList(kHome);
        return true;
      }
      menu_.open(block_ == kBlockDelay ? kPageDelay : (block_ == kBlockReverb ? kPageReverb : kPageDrums));
      screen_ = kMenu;
      return true;
    case kMenu:
      return menuResult(menu_.push());
    case kIrList:
      if (irCursor_ >= m_.irCount) host_.set(kIrRescan, 1);
      else host_.set(kIrLoad, irCursor_);
      return true;
    default:
      return false;
  }
}

bool Panel::hold(uint32_t now) {
  touch(now);
  if (screen_ != kHome) return false;
  // The IR block is on when not bypassed: flipping bypass flips it.
  static const uint8_t kOnOff[kHomeBlocks] = {kBypass, kDelayOn, kReverbOn, kDrumsPlay, kDrumsPlay};
  return toggle(kOnOff[block_]);
}

bool Panel::confirm(uint32_t now) {
  touch(now);
  switch (screen_) {
    case kHome:
      return toggle(kDrumsPlay);
    case kMenu:
      return menuResult(menu_.push());
    case kIrList:
      return push(now);
    case kMeter:
      host_.set(kMeterReset, 1);
      return true;
    default:
      return false;
  }
}

bool Panel::back(uint32_t now) {
  touch(now);
  switch (screen_) {
    case kHome:
      if (tempoEdit_) {
        host_.set(kDrumTempo, tempoOrig_);
        tempoEdit_ = false;
        return true;
      }
      menu_.open();
      screen_ = kMenu;
      return true;
    case kMenu:
      return menuResult(menu_.back());
    default:
      screen_ = from_;
      return true;
  }
}

// The characters the cursor row's name is scrolled by: it waits at the
// start, scrolls to the end, waits again and starts over.
int Panel::marqueeOffset() const {
  return marquee_ > kMarqueePause ? marquee_ - kMarqueePause : 0;
}

bool Panel::tick(uint32_t now) {
  if (tempoEdit_ && now - lastInputMs_ >= kTimeoutMs) {
    tempoEdit_ = false;  // keeps the tempo
    return true;
  }
  if (screen_ != kHome && screen_ != kMeter && now - lastInputMs_ >= kTimeoutMs) {
    screen_ = kHome;
    return true;
  }
  if (screen_ != kIrList || irCursor_ >= m_.irCount || !m_.irPath || now - marqueeMs_ < kMarqueeMs) return false;
  marqueeMs_ = now;
  char name[64];
  irDisplayName(m_.irPath(irCursor_), name, sizeof(name));
  const int over = (int)strlen(name) - kIrBrowserNameChars;
  if (over <= 0) return false;
  if (++marquee_ > kMarqueePause + over + kMarqueePause) marquee_ = 0;
  return true;
}

void Panel::draw(Canvas& c, const UiModel& m) {
  switch (screen_) {
    case kHome:
      drawHome(c, m, block_, tempoEdit_);
      break;
    case kMenu:
      menu_.draw(c);
      break;
    case kIrList:
      if (irCursor_ > m.irCount) irCursor_ = m.irCount;  // after a rescan found fewer
      if (irTop_ > irCursor_) irTop_ = irCursor_;
      drawIrBrowser(c, m, irCursor_, irTop_, marqueeOffset());
      break;
    case kMeter:
      drawMeterScreen(c, m);
      break;
    case kStatus:
      drawStatus(c, m);
      break;
  }
}

}  // namespace ui
