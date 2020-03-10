/* sokoban-example.c: A simple sokoban program that demonstrates the
 * use of libredo.
 *
 * Copyright (C) 2013 by Brian Raiter. This program is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Things to try when running this program:
 * - Use the arrow keys to move around. Use [-] to undo moves and [+]
 *   to redo moves.
 * - Watch the move count. Note that the program will automatically
 *   detect when making a move with the arrows is equivalent to an
 *   undo or redo.
 * - Use Home to return to the beginning. Note that the redo moves
 *   that are available are displayed on the left.
 * - Use X to undo and forget: now the undone moves are really gone.
 * - Move into an open space and turn in a tight circle. The program
 *   will discard the last four moves since they accomplished nothing.
 * - Try moving in a larger circle. The program will still undo the
 *   moves for you, but it won't delete them from the history.
 * - Exit the program and restart it. Your move history, branches and
 *   all, is retained.
 * - Solve the puzzle. The screen will flash and a new display will show
 *   you the number of moves it took to solve.
 * - Use Home to go back to the starting position and replay your
 *   solution by continually pressing redo.
 * - If you see a point where you made some moves that weren't useful,
 *   undo back to before them and try to solve it without the
 *   unnecessary moves. If you're successful, the program will
 *   complete the solution as soon as you reach a state that you've
 *   already solved the game from.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include "redo.h"

/* Traditional Sokoban levels have a size limit of 24x24.
 */
static int const maxwidth = 24;

/* The layout of the level in its initial state. (Note: This level is
 * borrowed from David Skinner's first Sasquatch collection, modified
 * to allow a wider range of alternate solutions. Feel free to swap
 * this out with another Sokoban level of your choosing, as long as
 * it doesn't exceed maxwidth.)
 */
static char const *initialmap = "       ####\n"
                                "       #  #\n"
                                "       #  #\n"
                                "       #  #\n"
                                "########$.#\n"
                                "#     $ $.#\n"
                                "#   $@$...#\n"
                                "#   $$$..##\n"
                                "#    $ ..#\n"
                                "##########\n";

/* Where the session gets stored when the program exits.
 */
static char const *sessionfilename = "./session";

/* Bitflags representing the game elements. 
 */
enum {
    FLOOR = 0x00,
    GOAL  = 0x01,
    BOX   = 0x02,
    PAWN  = 0x04,
    WALL  = 0x08
};

/* The list of user commands, plus some values that are only used in
 * the session file.
 */
enum {
    cmd_nil,
    cmd_left, cmd_down, cmd_up, cmd_right,
    cmd_undo, cmd_redo, cmd_undo10, cmd_redo10,
    cmd_undotobranch, cmd_redotobranch,
    cmd_restart, cmd_tosolution, cmd_forget,
    cmd_tobetter, cmd_copybetter,
    cmd_help, cmd_redraw, cmd_quit,
    cmd_startbranch = 0x7E,
    cmd_marksibling = 0x7F,
    cmd_closebranch = 0xFE,
    cmd_betterflag = 0x80
};

/* The elements that comprise the game's current state.
 */
typedef struct gamestate {
    int height;
    int boxcount;
    int storecount;
    int pawnpos;
    char *map;
    redo_session *session;
    redo_position *currpos;
    int bestsolutionsize;
} gamestate;

static gamestate game;
static short *statebuf;

/*
 * The sokoban game logic.
 */

/* Initialize the game state in accordance with the setup described by
 * the initialmap string.
 */
static int setupgame(void)
{
    char const *s;
    int pos;

    game.height = 0;
    game.boxcount = 0;
    game.storecount = 0;
    for (s = initialmap ; *s ; ++s) {
        switch (*s) {
          case '$':     ++game.boxcount;                        break;
          case '*':     ++game.boxcount; ++game.storecount;     break;
          case '\n':    ++game.height;                          break;
        }
    }
    game.map = malloc(game.height * maxwidth);
    for (s = initialmap, pos = 0 ; *s ; ++s, ++pos) {
        switch (*s) {
          case ' ':     game.map[pos] = FLOOR;                          break;
          case '#':     game.map[pos] = WALL;                           break;
          case '.':     game.map[pos] = GOAL;                           break;
          case '$':     game.map[pos] = BOX;                            break;
          case '*':     game.map[pos] = BOX|GOAL;                       break;
          case '@':     game.map[pos] = PAWN;       game.pawnpos = pos; break;
          case '+':     game.map[pos] = PAWN|GOAL;  game.pawnpos = pos; break;
          case '\n':
            for (;;) {
                game.map[pos] = FLOOR;
                if (pos % maxwidth == maxwidth - 1)
                    break;
                ++pos;
            }
            break;
          default:
            fprintf(stderr, "invalid character in initialmap: \"%c\"\n", *s);
            return 0;
        }
    }
    game.session = NULL;
    game.currpos = NULL;
    game.bestsolutionsize = 0;
    return 1;
}

/* Update the game state in accordance with the supplied move. Return
 * false if the move is not a legal move.
 */
static int applymove(int move)
{
    int pos, delta;

    switch (move) {
      case cmd_left:    delta = -1;             break;
      case cmd_down:    delta = +maxwidth;      break;
      case cmd_up:      delta = -maxwidth;      break;
      case cmd_right:   delta = +1;             break;
      default:                                  return 0;
    }

    pos = game.pawnpos + delta;
    if (game.map[pos] & WALL)
        return 0;
    if (game.map[pos] & BOX)
        if (game.map[pos + delta] & (WALL | BOX))
            return 0;

    game.map[game.pawnpos] &= ~PAWN;
    game.pawnpos = pos;
    game.map[game.pawnpos] |= PAWN;

    if (game.map[pos] & BOX) {
        game.map[pos] &= ~BOX;
        if (game.map[pos] & GOAL)
            --game.storecount;
        pos += delta;
        game.map[pos] |= BOX;
        if (game.map[pos] & GOAL)
            ++game.storecount;
    }

    return 1;
}

/* Return true if the game is currently in a solved state.
 */
static int isgameover(void)
{
    return game.storecount == game.boxcount;
}

/*
 * Storing and retrieving the game state. Most of the game map is
 * static. The only parts that move are the pawn (i.e. the player) and
 * the boxes, so these are what constitutes the state. The state is an
 * array of 2-byte values. The first element is the location of the
 * player, followed by the boxes. The boxes are always stored in the
 * order that they appear on the map (left-to-right, top-to-bottom) so
 * that e.g. if two boxes swap places with each other, the resulting
 * state is treated as being the same.
 */

/* Store the location of the pawn and the boxes in the array.
 */
static void storegamestate(short *state)
{
    int pos;

    *state++ = game.pawnpos;
    for (pos = 0 ; pos < game.height * maxwidth ; ++pos)
        if (game.map[pos] & BOX)
            *state++ = pos;
}

/* Clear the map, leaving just the walls and the goals, and then place
 * the pawn and the boxes according to the positions in the array.
 */
static void loadgamestate(short const *state)
{
    int pos, i;

    for (pos = 0 ; pos < game.height * maxwidth ; ++pos)
        game.map[pos] &= ~(PAWN | BOX);

    game.pawnpos = *state++;
    game.map[game.pawnpos] |= PAWN;
    game.storecount = 0;
    for (i = 0 ; i < game.boxcount ; ++i) {
        game.map[state[i]] |= BOX;
        if (game.map[state[i]] & GOAL)
            ++game.storecount;
    }
}

/*
 * Saving and loading the session to a file. Note that the
 * savesession() code is quite generic; it could be used with almost
 * any implementation that has move values in the range 0 to 125. (The
 * loadsession() code is slightly less generic, since it also has to
 * call applymove() to recreate the game state for each position.)
 */

/* Encode a branch as a byte value. The high bit is set if the
 * subsequent position has a non-NULL better value.
 */
static int movebyte(redo_branch const *branch)
{
    return branch->move | (branch->p->better ? cmd_betterflag : 0);
}

static void savesession_branchrecurse(FILE *fp, redo_branch const *branch);

/* Output a subtree to a file. A sequence of positions with no
 * branching is written to the file as consecutive move values. A
 * position with more than one subsequent move is output by the next
 * function, with a special byte value indicating the end of the
 * subtree.
 */
static void savesession_recurse(FILE *fp, redo_position const *position)
{
    while (position->nextcount == 1) {
        fputc(movebyte(position->next), fp);
        position = position->next->p;
    }
    if (position->nextcount > 1) {
        savesession_branchrecurse(fp, position->next);
        fputc(cmd_closebranch, fp);
    }
}

/* A special byte value marks the beginning of a subtree, and a second
 * byte value separates sibling branches from each other. This
 * function outputs the branches in their reverse order, so that when
 * they are read back in their original order is recreated.
 */
static void savesession_branchrecurse(FILE *fp, redo_branch const *branch)
{
    if (branch->cdr) {
        savesession_branchrecurse(fp, branch->cdr);
        fputc(cmd_marksibling, fp);
    } else {
        fputc(cmd_startbranch, fp);
    }
    fputc(movebyte(branch), fp);
    savesession_recurse(fp, branch->p);
}

/* Store the session's complete subtree in a file.
 */
static int savesession(void)
{
    FILE *fp;

    if (!redo_hassessionchanged(game.session))
        return 1;
    fp = fopen(sessionfilename, "wb");
    if (!fp)
        return 0;
    savesession_recurse(fp, redo_getfirstposition(game.session));
    fclose(fp);
    redo_clearsessionchanged(game.session);
    return 1;
}

/* Import a subtree's worth of moves from a file. For each move, the
 * game state is recreated so that the position can be added to the
 * session.
 */
static int loadsession_recurse(FILE *fp, redo_position *position)
{
    int byte, move;

    for (;;) {
        byte = fgetc(fp);
        if (byte == EOF || byte == cmd_closebranch)
            return 0;
        if (byte == cmd_marksibling)
            return 1;
        if (byte == cmd_startbranch) {
            while (loadsession_recurse(fp, position))
                loadgamestate(redo_getsavedstate(position));
            continue;
        }
        move = byte & ~cmd_betterflag;
        applymove(move);
        storegamestate(statebuf);
        position = redo_addposition(game.session, position, move,
                                    statebuf, isgameover(),
                                    (byte & cmd_betterflag ? redo_checklater
                                                           : redo_nocheck));
    }
}

/* Import the session tree from a file. After recreating the saved
 * session, it is rewound to the root position.
 */
static int loadsession(void)
{
    FILE *fp;
    redo_position *startpos;

    fp = fopen(sessionfilename, "rb");
    if (!fp)
        return 1;
    startpos = redo_getfirstposition(game.session);
    loadsession_recurse(fp, startpos);
    fclose(fp);
    redo_setbetterfields(game.session);
    game.bestsolutionsize = startpos->solutionsize;
    loadgamestate(redo_getsavedstate(startpos));
    redo_clearsessionchanged(game.session);
    return 1;
}

/*
 * Navigating the redo session tree.
 */

/* Change the current position, restoring the game to the associated
 * state.
 */
static int gotoposition(redo_position *pos)
{
    if (!pos)
        return 0;
    loadgamestate(redo_getsavedstate(pos));
    game.currpos = pos;
    return 1;
}

/* Execute an actual move. If the move has already been made, its
 * position is returned directly. Otherwise, assuming the move is
 * legal, the game is updated appropriately and the move is added to
 * the redo session. In addition to redo_addposition(), this function
 * uses redo_suppresscycle() to automatically discard useless circular
 * movements.
 */
static int domovecmd(int move)
{
    redo_position *p;
    int solutionsize;

    p = redo_getnextposition(game.currpos, move);
    if (p) {
        gotoposition(p);
        return 1;
    }

    if (isgameover())
        return 0;
    if (!applymove(move))
        return 0;

    storegamestate(statebuf);
    if (!redo_suppresscycle(game.session, &game.currpos, statebuf, 4))
        game.currpos = redo_addposition(game.session, game.currpos, move,
                                        statebuf, isgameover(), redo_check);
    solutionsize = game.currpos->solutionsize;
    if (solutionsize)
        if (game.bestsolutionsize > solutionsize || game.bestsolutionsize == 0)
            game.bestsolutionsize = solutionsize;
    return 1;
}

/* Follow the redo chain until a leaf node is reached, preferring a
 * path that leads to the shortest solution (if any) in the subtree.
 */
static redo_position *jumpforward(redo_position *position)
{
    redo_branch *branch;

    while (position->next) {
        if (!position->solutionsize) {
            position = position->next->p;
        } else {
            for (branch = position->next ; branch ; branch = branch->cdr)
                if (branch->p->solutionsize == position->solutionsize)
                    break;
            position = redo_getnextposition(position, branch->move);
        }
    }
    return position;
}

/* Execute a user command.
 */
static void docmd(int cmd)
{
    redo_position *p;
    int i;

    switch (cmd) {
      case cmd_left:
      case cmd_down:
      case cmd_up:
      case cmd_right:
        domovecmd(cmd);
        break;
      case cmd_undo:
        if (game.currpos->prev)
            gotoposition(game.currpos->prev);
        break;
      case cmd_redo:
        if (game.currpos->next)
            gotoposition(game.currpos->next->p);
        break;
      case cmd_undo10:
        p = game.currpos;
        for (i = 0 ; i < 10 && p->prev ; ++i)
            p = p->prev;
        gotoposition(p);
        break;
      case cmd_redo10:
        p = game.currpos;
        for (i = 0 ; i < 10 && p->next ; ++i)
            p = p->next->p;
        gotoposition(p);
        break;
      case cmd_undotobranch:
        p = game.currpos;
        while (p->prev) {
            p = p->prev;
            if (p->nextcount > 1)
                break;
        }
        gotoposition(p);
        break;
      case cmd_redotobranch:
        p = game.currpos;
        while (p->next) {
            p = p->next->p;
            if (p->nextcount > 1)
                break;
        }
        gotoposition(p);
        break;
      case cmd_restart:
        gotoposition(redo_getfirstposition(game.session));
        break;
      case cmd_tosolution:
        gotoposition(jumpforward(game.currpos));
        break;
      case cmd_forget:
        p = redo_dropposition(game.session, game.currpos);
        if (p == game.currpos)
            beep();
        else
            gotoposition(p);
        break;
      case cmd_tobetter:
        p = game.currpos;
        while (p->better)
            p = p->better;
        gotoposition(p);
        break;
      case cmd_copybetter:
        if (game.currpos->better)
            redo_duplicatepath(game.session, game.currpos,
                               game.currpos->better);
        break;
    }
}

/* Free the redo session's allocated memory.
 */
static void endredo(void)
{
    redo_endsession(game.session);
    game.session = NULL;
}

/* Initialize the redo session and give it the starting state.
 */
static int initredo(void)
{
    int size;

    size = (game.boxcount + 1) * sizeof *statebuf;
    statebuf = malloc(size);
    if (!statebuf)
        return 0;
    storegamestate(statebuf);
    game.session = redo_beginsession(statebuf, size, 0);
    if (!game.session)
        return 0;
    atexit(endredo);
    game.currpos = redo_getfirstposition(game.session);
    game.bestsolutionsize = 0;
    return 1;
}

/*
 * The curses UI.
 */

/* Return the terminal to its normal mode.
 */
static void endui(void)
{
    if (!isendwin())
        endwin();
}

/* Initialize the curses interface.
 */
static int initui(void)
{
    if (!initscr())
        return 0;
    atexit(endui);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    return 1;
}

/* Render the game display. The current game map is drawn in the
 * center of the screen. On the left is shown the current move count
 * and what redo-able moves are available, if any. Redo-able moves
 * that lead to a solution have their solution's move count displayed
 * alongside. If the current state has been visited before from a
 * shorter set of moves, the better count is displayed under the
 * current move count.
 */
static void render(void)
{
    static char const *moves[] = { "", " Left", " Down", "   Up", "Right" };
    redo_branch *branch;
    int pos, x, y;

    erase();

    mvaddstr(0, 8, "Sokoban -- libredo demonstration");

    mvprintw(2, 0, "Moves:%4d", game.currpos->movecount);
    if (game.currpos->better)
        mvprintw(3, 5, "=%4d", game.currpos->better->movecount);
    if (isgameover())
        mvaddstr(4, 0, "* SOLVED *");
    for (branch = game.currpos->next ; branch ; branch = branch->cdr) {
        mvaddstr(5 + branch->move, 0, moves[branch->move]);
        if (branch->p->solutionsize)
            printw(":%4d", branch->p->solutionsize);
    }

    for (pos = 0 ; pos < game.height * maxwidth ; ++pos) {
        x = 16 + 2 * (pos % maxwidth);
        y = 2 + pos / maxwidth;
        switch (game.map[pos]) {
          case FLOOR:                                   break;
          case GOAL:            mvaddstr(y, x, "::");   break;
          case BOX:             mvaddstr(y, x, "[]");   break;
          case BOX|GOAL:        mvaddstr(y, x, "[]");   break;
          case PAWN:            mvaddstr(y, x, "><");   break;
          case PAWN|GOAL:       mvaddstr(y, x, "><");   break;
          case WALL:            mvaddstr(y, x, "##");   break;
        }
    }

    y = game.height;
    mvprintw(y + 1, 0, "  Stored: %d", game.storecount);
    mvprintw(y + 2, 0, "Unstored: %d", game.boxcount - game.storecount);

    if (game.bestsolutionsize)
        mvprintw(y + 2, 16, "Best: %d", game.bestsolutionsize);

    move(y + 3, 0);
    refresh();
}

/* The help screen lists the available key commands.
 */
static void showhelp(void)
{
    static char const *helptext[][2] = {
        { "Move", "Arrows or H J K L" },
        { "Undo previous move", "-", },
        { "Redo next move", "+ or =" },
        { "Undo 10 moves", "PageUp" },
        { "Redo 10 moves", "PageDown" },
        { "Redo to next branch point", "Tab" },
        { "Undo to previous branch point", "Backspace" },
        { "Undo to the starting position", "Home" },
        { "Redo to the shorter solution", "End" },
        { "Undo and delete previous move", "X" },
        { "Switch to \"better\" position", "B" },
        { "Copy moves from \"better\" position", "C" },
        { "Redraw the screen", "Ctrl-L" },
        { "Display this help", "? or F1" },
        { "Quit the program", "Q" }
    };

    int i;

    erase();
    mvaddstr(0, 0, "KEY COMMANDS");
    for (i = 0 ; i < (int)(sizeof helptext / sizeof *helptext) ; ++i) {
        mvaddstr(i + 2, 4, helptext[i][0]);
        mvaddstr(i + 2, 40, helptext[i][1]);
    }
    mvaddstr(i + 3, 0, "Press any key to resume.");
    refresh();
    getch();
}

/* Translate a keystroke into a user command.
 */
static int translatekey(int key)
{
    switch (key) {
      case 'h':             return cmd_left;
      case 'j':             return cmd_down;
      case 'k':             return cmd_up;
      case 'l':             return cmd_right;
      case KEY_LEFT:        return cmd_left;
      case KEY_DOWN:        return cmd_down;
      case KEY_UP:          return cmd_up;
      case KEY_RIGHT:       return cmd_right;
      case '-':             return cmd_undo;
      case '+':             return cmd_redo;
      case '=':             return cmd_redo;
      case 'x':             return cmd_forget;
      case KEY_PPAGE:       return cmd_undo10;
      case '<':             return cmd_undo10;
      case KEY_NPAGE:       return cmd_redo10;
      case '>':             return cmd_redo10;
      case '\b':            return cmd_undotobranch;
      case '\177':          return cmd_undotobranch;
      case KEY_BACKSPACE:   return cmd_undotobranch;
      case '\t':            return cmd_redotobranch;
      case KEY_HOME:        return cmd_restart;
      case '^':             return cmd_restart;
      case KEY_END:         return cmd_tosolution;
      case '$':             return cmd_tosolution;
      case 'b':             return cmd_tobetter;
      case 'c':             return cmd_copybetter;
      case '\f':            return cmd_redraw;
      case KEY_RESIZE:      return cmd_redraw;
      case '?':             return cmd_help;
      case KEY_F(1):        return cmd_help;
      case 'q':             return cmd_quit;
      case '\003':          return cmd_quit;
      case ERR:             return cmd_quit;
    }
    return cmd_nil;
}

/* Display the game state, wait for a key event, and execute the
 * user's command. Repeat until the player asks to leave the program.
 */
static int runui(void)
{
    int solutionsize;
    int move;

    solutionsize = game.bestsolutionsize;
    for (;;) {
        render();
        move = translatekey(getch());
        if (move == cmd_quit)
            break;
        else if (move == cmd_redraw)
            clearok(stdscr, TRUE);
        else if (move == cmd_help)
            showhelp();
        else
            docmd(move);
        if (solutionsize != game.bestsolutionsize) {
            solutionsize = game.bestsolutionsize;
            flash();
        }
    }
    return 1;
}

/*
 * The top level.
 */

int main(void)
{
    return setupgame()
        && initredo()
        && loadsession()
        && initui()
        && runui()
        && savesession() ? EXIT_SUCCESS : EXIT_FAILURE;
}
