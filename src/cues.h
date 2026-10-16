#pragma once
#include <Arduino.h>
#include <vector>

// Sequences of cues, played by one executor (console style).
//   Sequence: named cue list, always loops. Shared by all linked boxes.
//   Cue:      name, fade and duration (0.1 s units). Tracking: stores only the channels changed
//             against the previous cue. Each box stores its own channels in /cues/<seq>_<cue>.json.
// The output is always one cue of the active sequence: state(k) = all 0, then cues 1..k.
// Manual changes (faders, presets) mark channels and put the sequence on hold; Update / + Cue
// store the marked channels, moving to another cue discards them.

enum CueMode : uint8_t { CUE_HOLD = 0, CUE_PLAY = 1 };

// How a box gets to a cue.
enum CueMove : uint8_t {
  MOVE_FADE = 0,     // with the target cue's fade time
  MOVE_INSTANT = 1,  // without fade
  MOVE_KEEP = 2,     // stay on the cue, only the mode changes (Play discards marks first)
};

enum CueOp : uint8_t { OP_GO, OP_BACK, OP_SKIP, OP_SKIP_BACK, OP_PLAY, OP_HOLD, OP_SELECT, OP_GOTO, OP_DISCARD };

// Absolute playback state as sent over the link after a transport action.
struct CueTransport {
  uint16_t seq = 0;
  uint16_t cue = 0;  // cue id, 0 = none (empty sequence)
  uint8_t mode = CUE_HOLD;
  uint8_t move = MOVE_INSTANT;
  uint16_t epoch = 0;  // playback state counter: a box with an older one adopts the newer state
};

struct CueMeta {
  uint16_t id = 0;
  String name;
  uint16_t fade = 20;  // 0.1 s
  uint16_t dur = 50;   // 0.1 s, from the cue's start to the next cue's start
};

struct SeqMeta {
  uint16_t id = 0;
  String name;
  uint16_t nextCue = 1;  // cue ids are never reused
  std::vector<CueMeta> cues;
};

struct PlayInfo {
  uint16_t seq = 0;  // 0 = no sequence
  String seqName;
  int idx = -1;      // -1 = no cue (empty sequence)
  int count = 0;
  uint16_t cue = 0;
  String cueName;
  uint8_t mode = CUE_HOLD;
  uint16_t fade = 0, dur = 0;
  uint32_t ms = 0;   // time since the cue started
  uint16_t epoch = 0;
};

void cuesBegin();  // after fixturesLoad() / presetsLoad(); migrates the old snapshot
void cuesTick();   // from loop()

// Marks: channels changed by hand since the last cue change. Callable from any task.
void cuesManualChange(uint16_t dmxFrom, uint16_t dmxTo);
bool cuesMarked(uint16_t dmx);
uint16_t cuesMarkCount();
void cuesMarksBitmap(uint8_t out[64]);  // bit (dmx-1)
uint32_t cuesMarksVersion();

// Transport on this box. out = resulting state for the other boxes.
String cuesTransport(uint8_t op, uint16_t arg, CueTransport& out);
// State received from another box.
void cuesApplyTransport(const CueTransport& t);
// Auto-hold after a manual change during Play: the state to announce, once.
bool cuesTakeHoldEvent(CueTransport& out);
// Play state of another box (beacon): a newer epoch is adopted; with the same epoch the leading box
// (lowest id) sets the timing.
void cuesAlign(uint16_t seq, uint16_t cue, uint8_t mode, uint32_t msInCue, uint16_t epoch, bool leader);

// Storing. On the box where it happens: metadata + own part (insert after the current cue or
// update the current cue). newSeqName: creates a sequence first if none is active.
struct CueStoreResult {
  String err;
  uint16_t seq = 0, cue = 0, count = 0;
  bool insert = false;
  uint16_t epoch = 0;
};
CueStoreResult cuesStore(bool insert, const String& name, uint16_t fade, uint16_t dur, const String& newSeqName);
// On the other boxes: store the marked channels into that cue and go there (hold). Returns the count.
uint16_t cuesStorePart(uint16_t seq, uint16_t cue, bool insert, uint16_t epoch);

// Show editing (this box; the link distributes the metadata).
String cuesNewSequence(const String& name, CueTransport& out);
String cuesRenameSequence(uint16_t seq, const String& name);
String cuesDeleteSequence(uint16_t seq);
String cuesSetStartup(uint16_t seq);  // 0 = last active
String cuesEditCue(uint16_t seq, uint16_t cue, const String& name, uint16_t fade, uint16_t dur);
String cuesMoveCue(uint16_t seq, uint16_t cue, int dir);
String cuesDeleteCue(uint16_t seq, uint16_t cue);

PlayInfo cuesPlayInfo();
std::vector<SeqMeta> cuesSequences();
uint16_t cuesStartup();
bool cuesNeedsShow();  // the active sequence or cue is not in our metadata yet
uint16_t cuesPartSize(uint16_t seq, uint16_t cue);  // channels this box stores in that cue

// Link: show metadata blob (without the box-local "last active").
uint16_t cuesShowVersion();
void cuesNoteShowVersion(uint16_t v);  // version seen from another box
uint32_t cuesShowCrc();
String cuesShowBlob();
bool cuesApplyShowBlob(const uint8_t* data, size_t len);
