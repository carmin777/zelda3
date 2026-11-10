#ifndef ZELDA3_MOD_GAME_OVER_TEXT_H_
#define ZELDA3_MOD_GAME_OVER_TEXT_H_

#include "../types.h"

// The game-over letter animation is data-driven so a translated ROM that
// changed "GAME OVER" to a different number of letters can supply its own
// layout. The defaults below are byte-for-byte the US layout (8 letters).
//
// kGameOverTextTiles stores four bytes per letter:
//   top tile, top attribute, bottom tile, bottom attribute.
enum {
  kGameOverTextMinLetters = 2,
  kGameOverTextMaxLetters = 10,
};

extern uint8 kGameOverTextLetterCount[1];
extern uint8 kGameOverTextTab1[kGameOverTextMaxLetters];
extern uint8 kGameOverTextTab2[kGameOverTextMaxLetters];
extern uint8 kGameOverTextTiles[kGameOverTextMaxLetters * 4];

#endif  // ZELDA3_MOD_GAME_OVER_TEXT_H_
