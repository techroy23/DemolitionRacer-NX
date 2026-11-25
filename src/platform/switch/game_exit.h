#ifndef DRACER_SWITCH_GAME_EXIT_H
#define DRACER_SWITCH_GAME_EXIT_H

// Synchronous game-thread only, after release of operation-local resources.
// Never returns to a caller that could consume an invalid result.
_Noreturn void switch_abort_game(const char *reason);

#endif
