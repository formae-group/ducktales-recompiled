# Game state

Current understanding, with what is still unknown stated explicitly.

## Control flow

The game runs as cooperative tasks (see [functions.md](functions.md)). Reset creates one task,
`$D4E6`; the scheduler resumes tasks whose wait expired, and the NMI drives the wait counters, so
game logic advances once per frame per task.

## Observed flow (route runs)

1. Title screen with the difficulty selector (Easy / Normal / Difficult). Right moves the cursor
   and wraps from Difficult to Easy (two presses from the default Normal select Easy); Start
   confirms.
2. Land Select with five destinations (The Amazon, Transylvania, African Mines, The Himalayas,
   The Moon). Down moves the cursor; the selection is confirmed by Start.
3. Each land opens with a short message, then gameplay with HUD (money, HP, total, player count,
   time).
4. Losing every life shows game over and returns to the title screen (observed in the
   `moon_intro` and `african_mines_easy` routes).

## Not yet located

Difficulty, current land, money, HP, lives, timer, treasures, boss state and ending flags have not
been located in RAM. They are the next reverse-engineering targets.
