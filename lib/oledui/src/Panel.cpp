#include "Panel.h"

#include <stdio.h>
#include <string.h>


namespace ui {

// A scrolling name waits this many steps at each end.
static constexpr int kMarqueePause = 3;

bool Panel::toggle(uint8_t id) {
  host_.set(id, host_.get(id) ? 0 : 1);
  return true;
}

// A list opens on the loaded file, about in the middle of the screen.
void Panel::openList(Screen list, Screen from) {
  from_ = from;
  screen_ = list;
  const int count = fileCount(m_, fileList()), current = fileCurrent(m_, fileList());
  listCursor_ = current >= 0 && current < count ? current : 0;
  const int entries = count + fileExtraRows(fileList());  // the files and "Rescan SD card" (presets: "Save as new")
  int top = listCursor_ - kIrBrowserRows / 2;
  if (top > entries - kIrBrowserRows) top = entries - kIrBrowserRows;
  listTop_ = top < 0 ? 0 : top;
  marquee_ = 0;
  previewAt_ = 0;
  // What plays and what is kept, for the list's tries (IR or preset).
  irPlaying_ = list == kPresetList ? m_.presetCurrent : m_.irCurrent;
  irSaved_ = list == kPresetList ? m_.presetCurrent : m_.irSaved;
  presetTried_ = false;
}

void Panel::askConfirm(uint8_t action, Screen from) {
  confirmAction_ = action;
  confirmFrom_ = from;
  screen_ = kConfirm;
}

// The name editor: renaming starts on the recalled preset's name without
// its number, a new preset on an empty name.
void Panel::openRename(Screen from, bool newPreset) {
  const char* name = !newPreset && m_.presetCurrent >= 0 && m_.presetName ? m_.presetName(m_.presetCurrent) : "";
  const char* p = name;
  while (*p >= '0' && *p <= '9') p++;
  if (p > name && *p == ' ') name = p + 1;
  snprintf(name_, sizeof(name_), "%s", name);
  namePos_ = 0;
  renameAction_ = newPreset ? kPresetNew : kPresetRename;
  confirmFrom_ = from;
  screen_ = kRename;
}

// Keeps the name, without trailing spaces: renames, or saves the new preset
// (an empty name: "Preset NN"). From the list, its cursor goes to it.
bool Panel::keepName() {
  int len = (int)strlen(name_);
  while (len && name_[len - 1] == ' ') name_[--len] = 0;
  if (renameAction_ == kPresetNew || len) host_.setText(renameAction_, name_);
  screen_ = confirmFrom_;
  if (screen_ == kPresetList) {
    syncCursor_ = true;
    presetTried_ = false;  // saving keeps what plays
  }
  return true;
}

void Panel::leavePresetList() {
  previewAt_ = 0;
  if (presetTried_) host_.set(kPresetRestore, 1);
  presetTried_ = false;
}

void Panel::leaveIrList() {
  previewAt_ = 0;
  if (irSaved_ >= 0 && irPlaying_ != irSaved_) host_.set(kIrPreview, irSaved_);
  irPlaying_ = irSaved_;
}

bool Panel::menuResult(Menu::Result r) {
  switch (r) {
    case Menu::kExit:
      screen_ = kHome;
      return true;
    case Menu::kOpenScreen:
      if (menu_.screen() == kScreenIR || menu_.screen() == kScreenAmp) {
        openList(menu_.screen() == kScreenAmp ? kAmpList : kIrList, kMenu);
      } else if (menu_.screen() == kScreenPreset) {
        openList(kPresetList, kMenu);
      } else if (menu_.screen() == kScreenPresetSave || menu_.screen() == kScreenPresetDelete) {
        askConfirm(menu_.screen() == kScreenPresetSave ? kPresetSave : kPresetDelete, kMenu);
      } else if (menu_.screen() == kScreenPresetNew || menu_.screen() == kScreenPresetRename) {
        openRename(kMenu, menu_.screen() == kScreenPresetNew);
      } else {
        from_ = kMenu;
        screen_ = menu_.screen() == kScreenMeter ? kMeter : (menu_.screen() == kScreenTuner ? kTuner : kStatus);
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
      if (editing_ == kBlockTempo) {
        const int factor = (editTurnMs_ != 0 && now - editTurnMs_ < Menu::kFastMs) ? Menu::kFastFactor : 1;
        editTurnMs_ = now ? now : 1;
        const ParamDef& d = paramDef(kDrumTempo);
        int v = host_.get(kDrumTempo) + detents * factor;
        v = v < d.min ? d.min : (v > d.max ? d.max : v);
        host_.set(kDrumTempo, v);
        return true;
      }
      if (editing_ == kBlockStyle) {
        // The row shows a song's name while one is selected: the style takes over.
        if (host_.get(kDrumSong)) host_.set(kDrumSong, 0);
        int lo = paramDef(kDrumStyle).min, hi = paramDef(kDrumStyle).max;
        host_.limits(kDrumStyle, &lo, &hi);
        int v = host_.get(kDrumStyle) + detents;
        v = v < lo ? lo : (v > hi ? hi : v);
        host_.set(kDrumStyle, v);
        return true;
      }
      int b = block_ + detents;
      b = b < 0 ? 0 : (b >= kHomeBlocks ? kHomeBlocks - 1 : b);
      if (b == block_) return false;
      block_ = (uint8_t)b;
      marquee_ = 0;
      marqueeMs_ = now;
      return true;
    }
    case kMenu:
      return menuResult(menu_.turn(detents, now));
    case kRename: {
      // The character under the cursor, through kRenameChars (one not in
      // it starts from the space).
      const int n = (int)strlen(kRenameChars);
      const char* at = name_[namePos_] ? strchr(kRenameChars, name_[namePos_]) : kRenameChars;
      int i = at ? (int)(at - kRenameChars) : 0;
      i = ((i + detents) % n + n) % n;
      // The cursor is within the name or just past its end: there, turning
      // adds a character.
      const int len = (int)strlen(name_);
      name_[namePos_] = kRenameChars[i];
      if (namePos_ >= len) name_[namePos_ + 1] = 0;
      return true;
    }
    case kIrList:
    case kAmpList:
    case kPresetList: {
      const int count = fileCount(m_, fileList());
      int c = listCursor_ + detents;
      const int last = count + fileExtraRows(fileList()) - 1;  // the Rescan row
      c = c < 0 ? 0 : (c > last ? last : c);
      if (c == listCursor_) return false;
      listCursor_ = c;
      if (c < listTop_) listTop_ = c;
      if (c >= listTop_ + kIrBrowserRows) listTop_ = c - kIrBrowserRows + 1;
      marquee_ = 0;
      marqueeMs_ = now;
      // The IR and preset lists play the cursor's entry once it rests (not
      // the extra rows).
      if (screen_ != kAmpList) previewAt_ = c < count ? ((now + kPreviewMs) ? now + kPreviewMs : 1) : 0;
      return true;
    }
    case kMeter: {
      // The line-in sensitivity (Levels > Line-in sens).
      const ParamDef& d = paramDef(kLineIn);
      int v = host_.get(kLineIn) + detents;
      v = v < d.min ? d.min : (v > d.max ? d.max : v);
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
      if (block_ == kBlockTempo || block_ == kBlockStyle) {
        // Push starts editing in place, and push again keeps it.
        if (editing_ == block_) {
          editing_ = -1;
        } else {
          editing_ = block_;
          editOrig_ = host_.get(block_ == kBlockTempo ? kDrumTempo : kDrumStyle);
          editSong_ = host_.get(kDrumSong);
          editTurnMs_ = 0;
        }
        return true;
      }
      if (block_ == kBlockIR || block_ == kBlockAmp) {
        openList(block_ == kBlockAmp ? kAmpList : kIrList, kHome);
        return true;
      }
      if (block_ == kBlockPreset) {
        openList(kPresetList, kHome);
        return true;
      }
      if (block_ == kBlockTrainer) return toggle(kTrainerOn);
      menu_.open(block_ == kBlockGate ? kPageGate : (block_ == kBlockDelay ? kPageDelay : kPageReverb));
      screen_ = kMenu;
      return true;
    case kMenu:
      return menuResult(menu_.push());
    case kPresetList: {
      const int count = fileCount(m_, fileList());
      previewAt_ = 0;
      if (listCursor_ < count) {
        host_.set(kPresetLoad, listCursor_);  // keeps the one being tried, or recalls it
        irPlaying_ = irSaved_ = listCursor_;
        presetTried_ = false;
      } else if (listCursor_ == count) {
        openRename(kPresetList, true);  // Save as new: its name first
      } else {
        host_.set(kIrRescan, 1);  // keeps what plays
        presetTried_ = false;
      }
      return true;
    }
    case kConfirm:
      host_.set(confirmAction_, 1);
      screen_ = confirmFrom_;  // the menu hides rows that no longer apply (and clamps its cursor)
      return true;
    case kRename: {
      // The next character; at the end, a space first (so two pushes make
      // one between words).
      const int len = (int)strlen(name_);
      if (namePos_ >= len) {
        if (len >= kRenameMax) return false;
        name_[len] = ' ';
        name_[len + 1] = 0;
      }
      if (namePos_ < kRenameMax - 1) namePos_++;
      return true;
    }
    case kIrList:
    case kAmpList:
      if (listCursor_ >= fileCount(m_, fileList())) {
        host_.set(kIrRescan, 1);
      } else {
        host_.set(screen_ == kAmpList ? kAmpLoad : kIrLoad, listCursor_);
        if (screen_ == kIrList) {
          previewAt_ = 0;
          irPlaying_ = irSaved_ = listCursor_;
        }
      }
      return true;
    case kTuner:
      screen_ = from_;
      return true;
    default:
      return false;
  }
}

bool Panel::hold(uint32_t now) {
  touch(now);
  if (screen_ == kRename) return keepName();
  if (screen_ != kHome) return false;
  // PRE: save over the recalled preset (asked first), or name a new one.
  if (block_ == kBlockPreset) {
    if (m_.presetCurrent >= 0) askConfirm(kPresetSave, kHome);
    else openRename(kHome, true);
    return true;
  }
  // The IR block is on when not bypassed: flipping bypass flips it.
  static const uint8_t kOnOff[kHomeBlocks] = {kNoItem,   kAmpOn,     kGateOn,    kBypass,   kDelayOn,
                                              kReverbOn, kDrumsPlay, kDrumsPlay, kTrainerOn};
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
    case kAmpList:
    case kPresetList:
    case kConfirm:
      return push(now);
    case kRename: {
      // Deletes the character under the cursor, the rest moving left; past
      // the end, the last one (a backspace).
      const int len = (int)strlen(name_);
      if (!len) return false;
      if (namePos_ >= len) namePos_ = len - 1;
      memmove(name_ + namePos_, name_ + namePos_ + 1, (size_t)(len - namePos_));
      return true;
    }
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
      if (editing_ == kBlockTempo) {
        host_.set(kDrumTempo, editOrig_);
        editing_ = -1;
        return true;
      }
      if (editing_ == kBlockStyle) {
        host_.set(kDrumStyle, editOrig_);
        if (host_.get(kDrumSong) != editSong_) host_.set(kDrumSong, editSong_);
        editing_ = -1;
        return true;
      }
      menu_.open();
      screen_ = kMenu;
      return true;
    case kMenu:
      return menuResult(menu_.back());
    case kConfirm:
      screen_ = confirmFrom_;
      return true;
    case kRename:
      // Back a character; on the first, cancel.
      if (namePos_ > 0) namePos_--;
      else screen_ = confirmFrom_;
      return true;
    default:
      if (screen_ == kIrList) leaveIrList();
      if (screen_ == kPresetList) leavePresetList();
      screen_ = from_;
      marquee_ = 0;
      return true;
  }
}

// The press before the hold has already gone back one step: the tuner
// returns to where that left the panel (home or the menu).
bool Panel::backHold(uint32_t now) {
  touch(now);
  if (screen_ == kTuner) return false;
  editing_ = -1;
  from_ = screen_ == kMenu ? kMenu : kHome;
  screen_ = kTuner;
  return true;
}

// The characters the cursor row's name is scrolled by: it waits at the
// start, scrolls to the end, waits again and starts over.
int Panel::marqueeOffset() const {
  return marquee_ > kMarqueePause ? marquee_ - kMarqueePause : 0;
}

bool Panel::tick(uint32_t now) {
  if (editing_ >= 0 && now - lastInputMs_ >= kTimeoutMs) {
    editing_ = -1;  // keeps the value
    return true;
  }
  if (screen_ != kHome && screen_ != kMeter && screen_ != kTuner && now - lastInputMs_ >= kTimeoutMs) {
    if (screen_ == kIrList) leaveIrList();
    if (presetTried_) leavePresetList();  // from the list, or the new preset's name started from it
    screen_ = kHome;
    return true;
  }
  if ((screen_ == kIrList || screen_ == kPresetList) && previewAt_ && (int32_t)(now - previewAt_) >= 0) {
    previewAt_ = 0;
    if (listCursor_ != irPlaying_ && listCursor_ < fileCount(m_, fileList())) {
      host_.set(screen_ == kIrList ? kIrPreview : kPresetPreview, listCursor_);
      irPlaying_ = listCursor_;
      if (screen_ == kPresetList) presetTried_ = true;
      return true;
    }
  }
  // The scrolling name: the open list's cursor row, or home row 0 on AMP or PRE.
  char name[64];
  int room;
  if (isList()) {
    if (listCursor_ >= fileCount(m_, fileList())) return false;
    listName(m_, fileList(), listCursor_, name, sizeof(name));
    room = listNameRoom(fileList());
  } else if (screen_ == kHome && (block_ == kBlockAmp || block_ == kBlockPreset)) {
    homeName(m_, block_, name, sizeof(name));
    room = kHomeNameChars;
  } else {
    return false;
  }
  if (now - marqueeMs_ < kMarqueeMs) return false;
  marqueeMs_ = now;
  const int over = (int)strlen(name) - room;
  if (over <= 0) return false;
  if (++marquee_ > kMarqueePause + over + kMarqueePause) marquee_ = 0;
  return true;
}

void Panel::draw(Canvas& c, const UiModel& m) {
  switch (screen_) {
    case kHome:
      drawHome(c, m, block_, editing_, block_ == kBlockAmp || block_ == kBlockPreset ? marqueeOffset() : 0);
      break;
    case kMenu:
      menu_.draw(c);
      break;
    case kConfirm: {
      char name[64] = "";
      homeName(m, kBlockPreset, name, sizeof(name));
      drawConfirm(c, confirmAction_ == kPresetDelete ? "DELETE?" : "SAVE?", name);
      break;
    }
    case kRename:
      drawRename(c, renameAction_ == kPresetNew ? "NEW PRESET" : "RENAME", name_, namePos_);
      break;
    case kIrList:
    case kAmpList:
    case kPresetList: {
      const int count = fileCount(m, fileList());
      if (syncCursor_ && m.presetCurrent >= 0) {  // a new preset: on it, and it is what plays
        listCursor_ = irPlaying_ = irSaved_ = m.presetCurrent;
        previewAt_ = 0;
      }
      syncCursor_ = false;
      if (listCursor_ > count + fileExtraRows(fileList()) - 1) listCursor_ = count + fileExtraRows(fileList()) - 1;  // after a rescan found fewer
      if (listTop_ < listCursor_ - kIrBrowserRows + 1) listTop_ = listCursor_ - kIrBrowserRows + 1;
      if (listTop_ > listCursor_) listTop_ = listCursor_;
      drawFileList(c, m, fileList(), listCursor_, listTop_, marqueeOffset());
      break;
    }
    case kMeter:
      drawMeterScreen(c, m);
      break;
    case kStatus:
      drawStatus(c, m);
      break;
    case kTuner:
      drawTuner(c, m);
      break;
  }
}

}  // namespace ui
