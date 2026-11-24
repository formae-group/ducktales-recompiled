/*
 * player/player.c - Scrooge, decompiled from the fixed bank (docs/PLAYER.md,
 * docs/DECOMPILATION.md): the frame's update and its mode dispatch, the
 * site adapters and the routine table. The other files of player/ hold the
 * contact tests (contact.c), the vertical and horizontal moves (move.c), the
 * ground, air, climbing and cane modes (modes.c), damage and death
 * (damage.c) and the message box and treasure modes 15 and 16 (message.c);
 * player_internal.h is what they share.
 *
 * Player_Update runs once per frame from the frame loop: it loads gravity,
 * counts the invulnerability and power-up timers down and runs the handler of
 * the current mode. Every routine below replaces the original one (decomp.c);
 * the address of each is in its comment and in ducktales_symbols.h. The
 * others call one another in C; only those original code calls have a site
 * (the table at the end).
 *
 * Where the original passes a value in a register to code whose effect
 * depends on it, the C passes it explicitly: "x" is the X register of the
 * original at that point. It matters for three things only: the slot the
 * walking code moves when it is entered without its own set-up (the push of
 * $73 with no direction held), the scratch byte $00 a sound request leaves,
 * and the routines not decompiled yet, which get the registers the original
 * gives them.
 *
 * The background probe, its snaps and the probe point $D350 (where the
 * vertical contact test can fall through to) are the C of collision.c, the
 * walls move $F0F8 and the climbing steps $F192/$F1A8 the C of objcommon.c
 * and the HP refill $F812 the C of objects/items.c.
 * Player_Update is task code (decomp.h): modes 15 and 16 (message box and
 * treasure) wait for frames, and the row change at $E8C0 drops
 * Player_Update's return address and hands off to the row scroll, which goes
 * back into the frame loop. The handlers on the way to the row change
 * (Player_Ground, Player_StartJump, Player_Air, Player_Pogo, Player_Climb,
 * Player_Stuck, Player_Carried) are task code too, since their original does
 * not return there: Player_Update's check covers them. The original jumps to
 * all of them (JMP), so no return address of theirs is on the stack. The
 * sites of $ED05, $EF62 and $E8C0 stay for the original Player_Update, which
 * replace mode runs when the call would cross VBlank.
 */
#include "decomp/player/player_internal.h"

/* $E354: once per frame from the frame loop. */
static void Player_Update(Regs *r)
{
    Decomp_Enter(DT_CODE_PLAYER_UPDATE, r->a, r->x, r->y);
    gGravity = PLAYER_GRAVITY;
    if (gPlayerInvulnerable != 0 && !(gPlayerInvulnerable & 0x80)) {
        gPlayerInvulnerable--;
    }
    if (gPlayerPower != 0 && !(gPlayerPower & 0x80)) {
        if (gMusic != MUSIC_POWER) {
            Music_Request(MUSIC_POWER, r->x);
        }
        uint8_t p = P_C;
        RAM_U8(DT_ADDR_POWER_FRAMES) = Alu_Sbc(RAM_U8(DT_ADDR_POWER_FRAMES), 1, &p);
        RAM_U8(DT_ADDR_POWER_FRAMES + 1) = Alu_Sbc(RAM_U8(DT_ADDR_POWER_FRAMES + 1), 0, &p);
        if (Player_GetPowerFrames() == 0) {
            gPlayerPower = 0;
            Music_Request(gLand, r->x);
        }
    }
    uint8_t mode = gPlayerMode;
    gScratch(0) = Game_ReadRom((uint16_t)(DT_CODE_MODE_LO + mode));
    gScratch(1) = Game_ReadRom((uint16_t)(DT_CODE_MODE_HI + mode));
    r->a = gScratch(1);
    r->x = mode;
    switch (mode) {
        case PLAYER_MODE_GROUND:
            Player_Ground();
            break;
        case PLAYER_MODE_AIR:
            Player_Air();
            break;
        case PLAYER_MODE_POGO:
            Player_Pogo();
            break;
        case PLAYER_MODE_CLIMB:
            Player_Climb();
            break;
        case PLAYER_MODE_CANE:
            Player_Cane();
            break;
        case PLAYER_MODE_HURT:
            Player_Hurt();
            break;
        case PLAYER_MODE_KNOCKBACK:
            Player_Knockback(mode);
            break;
        case PLAYER_MODE_STUCK:
            Player_Stuck();
            break;
        case PLAYER_MODE_PIT:
            Player_Pit();
            break;
        case PLAYER_MODE_9:
            Player_Mode9();
            break;
        case PLAYER_MODE_CARRIED_A:
        case PLAYER_MODE_CARRIED_B:
            Player_Carried();
            break;
        case PLAYER_MODE_12:
            Player_Mode12();
            break;
        case PLAYER_MODE_DEATH:
            Player_Death();
            break;
        case PLAYER_MODE_14:
            Player_Mode14();
            break;
        case PLAYER_MODE_MESSAGE:
            Player_Message(r);
            break;
        case PLAYER_MODE_TREASURE:
            Player_Treasure(r);
            break;
    }
}

/* ---- site adapters ---- */

static void Run_Update(Regs *r)
{
    Player_Update(r);
}

static void Run_Ground(Regs *r)
{
    (void)r;
    Player_Ground();
}

static void Run_StartJump(Regs *r)
{
    (void)r;
    Player_StartJump();
}

static void Run_Walk(Regs *r)
{
    Player_Walk(r->x);
}

static void Run_MoveRight(Regs *r)
{
    r->x = Player_MoveRight(r->x);
}

static void Run_MoveLeft(Regs *r)
{
    r->x = Player_MoveLeft(r->x);
}

static void Run_Air(Regs *r)
{
    (void)r;
    Player_Air();
}

static void Run_GrabClimb(Regs *r)
{
    (void)r;
    Player_GrabClimb();
}

static void Run_Pogo(Regs *r)
{
    (void)r;
    Player_Pogo();
}

static void Run_Climb(Regs *r)
{
    (void)r;
    Player_Climb();
}

static void Run_Cane(Regs *r)
{
    (void)r;
    Player_Cane();
}

static void Run_Fall(Regs *r)
{
    r->x = Player_Fall();
}

static void Run_Rise(Regs *r)
{
    r->x = Player_Rise();
}

static void Run_ProbeVertical(Regs *r)
{
    r->x = Player_ProbeVertical();
}

static void Run_Hurt(Regs *r)
{
    (void)r;
    Player_Hurt();
}

static void Run_Knockback(Regs *r)
{
    Player_Knockback(r->x);
}

static void Run_TakeDamage(Regs *r)
{
    (void)r;
    Player_MarkDamage();
}

static void Run_Stuck(Regs *r)
{
    (void)r;
    Player_Stuck();
}

static void Run_Pit(Regs *r)
{
    (void)r;
    Player_Pit();
}

static void Run_PitDeath(Regs *r)
{
    (void)r;
    Player_PitDeath();
}

static void Run_Mode9(Regs *r)
{
    (void)r;
    Player_Mode9();
}

static void Run_Carried(Regs *r)
{
    (void)r;
    Player_Carried();
}

static void Run_Mode12(Regs *r)
{
    (void)r;
    Player_Mode12();
}

static void Run_Death(Regs *r)
{
    (void)r;
    Player_Death();
}

static void Run_Mode14(Regs *r)
{
    (void)r;
    Player_Mode14();
}

static void Run_ContactH(Regs *r)
{
    r->x = Player_ContactH();
    r->p = (uint8_t)((r->p & ~P_C) | (r->x ? 0 : P_C));
}

static void Run_ContactV(Regs *r)
{
    r->p = (uint8_t)((r->p & ~P_C) | (Player_ContactV() ? 0 : P_C));
}

static void Run_SelectRefill(Regs *r)
{
    r->x = Player_SelectRefill();
}

/* Sites only where original code calls a routine: the frame loop calls
 * Player_Update, the original modes 15 and 16 jump to Player_PitDeath, and
 * the original $E878 falls into Player_RowChange. The
 * damage routine keeps its site for replace mode's measuring call (decomp.c);
 * the damage event runs at its own [[mod_function_hook]] there (declared
 * first, so it fires first), and the adapter is the routine without it. */
const DecompRoutine gPlayerRoutines[] = {
    { DT_CODE_PLAYER_UPDATE, "ducktales.decomp.E354", "Player_Update", Run_Update, 0, true, 0, false },
    { DT_CODE_PLAYER_GROUND, NULL, "Player_Ground", Run_Ground, 0, true, 0, false },
    { DT_CODE_JUMP_START, NULL, "Player_StartJump", Run_StartJump, 0, true, 0, false },
    { DT_CODE_PLAYER_WALK, NULL, "Player_Walk", Run_Walk, 0 },
    { DT_CODE_MOVE_RIGHT, NULL, "Player_MoveRight", Run_MoveRight, OUT_X },
    { DT_CODE_MOVE_LEFT, NULL, "Player_MoveLeft", Run_MoveLeft, OUT_X },
    { DT_CODE_PLAYER_AIR, NULL, "Player_Air", Run_Air, 0, true, 0, false },
    { DT_CODE_GRAB_CLIMB, NULL, "Player_GrabClimb", Run_GrabClimb, 0 },
    { DT_CODE_PLAYER_POGO, NULL, "Player_Pogo", Run_Pogo, 0, true, 0, false },
    { DT_CODE_PLAYER_CLIMB, NULL, "Player_Climb", Run_Climb, 0, true, 0, false },
    { DT_CODE_PLAYER_CANE, NULL, "Player_Cane", Run_Cane, 0 },
    { DT_CODE_PLAYER_FALL, NULL, "Player_Fall", Run_Fall, OUT_X },
    { DT_CODE_PLAYER_RISE, NULL, "Player_Rise", Run_Rise, OUT_X },
    { DT_CODE_PROBE_VERTICAL, NULL, "Player_ProbeVertical", Run_ProbeVertical, OUT_X },
    { DT_CODE_PLAYER_HURT, NULL, "Player_Hurt", Run_Hurt, 0 },
    { DT_CODE_PLAYER_KNOCKBACK, NULL, "Player_Knockback", Run_Knockback, 0 },
    { DT_CODE_TAKE_DAMAGE, "ducktales.decomp.EB5E", "Player_TakeDamage", Run_TakeDamage, 0 },
    { DT_CODE_PLAYER_STUCK, NULL, "Player_Stuck", Run_Stuck, 0, true, 0, false },
    { DT_CODE_PLAYER_PIT, NULL, "Player_Pit", Run_Pit, 0 },
    { DT_CODE_PIT_DEATH, "ducktales.decomp.EBB4", "Player_PitDeath", Run_PitDeath, 0 },
    { DT_CODE_PLAYER_MODE9, NULL, "Player_Mode9", Run_Mode9, 0 },
    { DT_CODE_PLAYER_CARRIED, NULL, "Player_Carried", Run_Carried, 0, true, 0, false },
    { DT_CODE_PLAYER_MODE12, NULL, "Player_Mode12", Run_Mode12, 0 },
    { DT_CODE_PLAYER_DEATH, NULL, "Player_Death", Run_Death, 0 },
    { DT_CODE_PLAYER_MODE14, NULL, "Player_Mode14", Run_Mode14, 0 },
    { DT_CODE_CONTACT_H, NULL, "Player_ContactH", Run_ContactH, OUT_X | OUT_C },
    { DT_CODE_CONTACT_V, NULL, "Player_ContactV", Run_ContactV, OUT_C },
    { DT_CODE_SELECT_REFILL, NULL, "Player_SelectRefill", Run_SelectRefill, OUT_X },
    { DT_CODE_ROW_CHANGE, "ducktales.decomp.E8C0", "Player_RowChange", Player_RowChange, 0, true, 0, false },
    { DT_CODE_PLAYER_MESSAGE, "ducktales.decomp.ED05", "Player_Message", Player_Message, 0, true, 0, false },
    { DT_MESSAGE_OPENED, NULL, "Message_Opened", Message_Opened, 0, true, 0, true },
    { DT_MESSAGE_SHOWN, NULL, "Message_Shown", Message_Shown, 0, true, 0, true },
    { DT_MESSAGE_DRAWN, NULL, "Message_Drawn", Message_Drawn, 0, true, 0, true },
    { DT_MESSAGE_WAIT, NULL, "Message_Wait", Message_Wait, 0, true, 0, true },
    { DT_MESSAGE_WAIT_NEXT, NULL, "Message_WaitNext", Message_WaitNext, 0, true, 0, true },
    { DT_MESSAGE_PAGED, NULL, "Message_Paged", Message_Paged, 0, true, 0, true },
    { DT_MESSAGE_CLEARED, NULL, "Message_Cleared", Message_Cleared, 0, true, 1, true },
    { DT_MESSAGE_CLOSED, NULL, "Message_Closed", Message_Closed, 0, true, 1, true },
    { DT_CODE_MESSAGE_SCROLL, NULL, "Message_Scroll", Message_Scroll, 0, true, 0, false },
    { DT_MESSAGE_SCROLLED, NULL, "Message_Scrolled", Message_Scrolled, OUT_X | OUT_Y, true, 0, true },
    { DT_CODE_MESSAGE_TEXT, NULL, "Message_Text", Message_Text, OUT_X | OUT_Y, true, 0, false },
    { DT_MESSAGE_LETTER_NEXT, NULL, "Message_LetterNext", Message_LetterNext, OUT_X | OUT_Y, true, 0, true },
    { DT_CODE_PLAYER_TREASURE, "ducktales.decomp.EF62", "Player_Treasure", Player_Treasure, 0, true, 0, false },
    { DT_TREASURE_WAITED, NULL, "Treasure_Waited", Treasure_Waited, 0, true, 0, true },
    { DT_TREASURE_DONE, NULL, "Treasure_Done", Treasure_Done, 0, true, 0, true },
    { DT_B5_PROGRESS_BITS, NULL, "Records_MarkTaken", Records_MarkTaken, OUT_A | OUT_X | OUT_Y,
      .bank = DT_BANK_SPRITES },
};
const int gPlayerRoutineCount = sizeof gPlayerRoutines / sizeof gPlayerRoutines[0];
