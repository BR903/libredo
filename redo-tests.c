/* redo-tests.c: libredo testing code.
 *
 * Copyright (C) 2013 by Brian Raiter. This program is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "redo.h"

/* Variables initialized during setup().
 */
static redo_session *session;
static redo_position *rootpos;

/* The state buffer, intentionally given an odd size.
 */
static char sbuf[33];

/* Minimal smoke test, run before the real tests.
 */
static void test_init(void)
{
    redo_session *s;
    void *p;

    /* Verify that session creation and deletion works at all. */

    s = redo_beginsession("", 1, 0);
    redo_endsession(s);

    /* Verify that redo_beginsession() rejects a too-large statesize. */

    p = malloc(0xFFFF);
    s = redo_beginsession(p, 0xFFFF, 0);
    assert(s == NULL);
    free(p);

    /* Verify that calling redo_endsession() with NULL doesn't crash. */

    redo_endsession(NULL);
}

/* Initiate a session with one position at the root.
 */
static int setup(void)
{
    memset(sbuf, 0, sizeof sbuf);
    session = redo_beginsession(sbuf, sizeof sbuf, sizeof sbuf - 1);
    assert(session);
    rootpos = redo_getfirstposition(session);
    assert(rootpos);
    assert(rootpos->inuse);
    assert(rootpos->prev == NULL);
    assert(rootpos->next == NULL);
    assert(rootpos->nextcount == 0);
    assert(rootpos->movecount == 0);
    assert(!redo_hassessionchanged(session));
    return 1;
}

/* Clean up after setup.
 */
static void teardown(void)
{
    redo_endsession(session);
    session = NULL;
}

/* Give all of the functionality a test run.
 */
static void test_overall(int grafting)
{
    redo_position *pos1a, *pos1b, *pos1c, *pos1d, *pos2a, *pos2c,
                  *pos3a, *pos3c, *pos4a, *pos4c, *pos5a;
    redo_position *pos;
    int g;

    setup();

    g = redo_setgraftbehavior(session, redo_nograft);
    assert(g == redo_graft);
    g = redo_setgraftbehavior(session, grafting);
    assert(g == redo_nograft);

    memset(sbuf, '_', sizeof sbuf);

    /* Add a move to the root position. */

    sbuf[1] = 'a';
    pos1a = redo_addposition(session, rootpos, 'a', sbuf, 0, redo_check);
    assert(pos1a);
    assert(pos1a->inuse);
    assert(pos1a != rootpos);
    assert(pos1a->prev == rootpos);
    assert(pos1a->movecount == 1);
    assert(pos1a->nextcount == 0);
    assert(pos1a->next == NULL);
    assert(rootpos->next != NULL);
    assert(rootpos->nextcount == 1);

    /* Verify that the change flag works as expected. */

    assert(redo_hassessionchanged(session));
    assert(!redo_hassessionchanged(session));

    /* Add another move, and verify that the two positions are distinct. */

    sbuf[1] = 'b';
    pos1b = redo_addposition(session, rootpos, 'b', sbuf, 0, redo_check);
    assert(pos1b);
    assert(pos1b->inuse);
    assert(pos1b != rootpos);
    assert(pos1b != pos1a);
    assert(pos1b->prev == pos1a->prev);
    assert(pos1b->movecount == 1);
    assert(rootpos->nextcount == 2);
    assert(redo_hassessionchanged(session));

    /* Verify redo_getnextposition() behaves correctly. */

    pos = redo_getnextposition(rootpos, 'a');
    assert(pos);
    assert(pos == pos1a);
    assert(rootpos->next->p == pos);
    pos = redo_getnextposition(rootpos, 'b');
    assert(pos);
    assert(pos == pos1b);
    assert(rootpos->next->p == pos);
    assert(redo_getnextposition(rootpos, 'c') == NULL);

    /* Add another move to the A path. */

    sbuf[1] = 'a';
    sbuf[2] = 'a';
    pos2a = redo_addposition(session, pos1a, 'a', sbuf, 0, redo_check);
    assert(pos2a);
    assert(pos2a->inuse);
    assert(pos2a != rootpos);
    assert(pos2a != pos1a);
    assert(pos2a != pos1b);
    assert(pos2a->prev == pos1a);
    assert(pos2a->movecount == 2);
    assert(pos2a->nextcount == 0);
    assert(pos1a->nextcount == 1);
    assert(redo_hassessionchanged(session));

    pos = redo_getnextposition(pos1a, 'a');
    assert(pos);
    assert(pos == pos2a);
    assert(pos1a->next->p == pos);

    /* Delete the just-added move, and verify that it is fully removed. */

    pos = redo_dropposition(session, pos2a);
    assert(pos == pos1a);
    assert(redo_getnextposition(pos1a, 'a') == NULL);
    assert(pos1a->next == NULL);
    assert(pos1a->nextcount == 0);
    assert(!pos2a->inuse);
    assert(redo_hassessionchanged(session));

    /* Re-add the deleted move. */

    pos2a = redo_addposition(session, pos1a, 'a', sbuf, 0, redo_check);
    assert(pos2a);
    assert(pos2a->inuse);
    assert(pos2a != pos1a);
    assert(pos2a->prev == pos1a);
    assert(pos2a->movecount == 2);
    assert(pos1a->nextcount == 1);
    assert(pos1a->next != NULL);
    assert(redo_hassessionchanged(session));

    /* Verify that deleting a non-leaf position will do nothing. */

    pos = redo_dropposition(session, pos1a);
    assert(pos == pos1a);
    assert(pos1a->inuse);
    assert(!redo_hassessionchanged(session));

    /* Verify that repeating a move won't create a new position. */

    pos = redo_addposition(session, rootpos, 'b', sbuf, 0, redo_check);
    assert(pos == pos1b);
    assert(pos1b->movecount == 1);
    assert(pos1a->nextcount == 1);
    assert(rootpos->nextcount == 2);
    assert(!redo_hassessionchanged(session));

    /* Verify that identical states are recognized as such. */

    pos1c = redo_addposition(session, rootpos, 'c', sbuf, 0, redo_check);
    assert(pos1c);
    assert(pos1c != pos1b);
    assert(pos1c != pos2a);
    assert(pos1c->prev == rootpos);
    assert(pos1c->movecount == 1);
    assert(rootpos->nextcount == 3);
    assert(pos1c->better == NULL);
    assert(pos2a->better != NULL);
    assert(pos2a->better == pos1c);
    assert(redo_hassessionchanged(session));

    /* Verify that identical states are not seen when equivcheck is false. */

    sbuf[3] = 'a';
    pos3a = redo_addposition(session, pos2a, 'a', sbuf, 0, redo_checklater);
    assert(pos3a);
    assert(pos3a != pos2a);
    assert(pos3a->prev == pos2a);
    assert(pos3a->movecount == 3);
    assert(pos2a->nextcount == 1);
    assert(pos3a->better == NULL);
    assert(pos3a->setbetter);
    assert(redo_hassessionchanged(session));

    pos2c = redo_addposition(session, pos1c, 'c', sbuf, 0, redo_nocheck);
    assert(pos2c);
    assert(pos2c != pos3a);
    assert(pos2c->prev == pos1c);
    assert(pos2c->movecount == 2);
    assert(pos1c->nextcount == 1);
    assert(pos2c->better == NULL);
    assert(!pos2c->setbetter);
    assert(redo_hassessionchanged(session));

    /* Verify that redo_setbetterfields() finds matches for marked states. */

    pos2a->better = NULL;
    pos1a->setbetter = 1;
    redo_setbetterfields(session);
    assert(!pos3a->setbetter);
    assert(!pos1a->setbetter);
    assert(pos3a->better == pos2c);
    assert(pos2a->better == NULL);
    assert(pos1a->better == NULL);
    assert(!redo_hassessionchanged(session));

    /* Restore mucked-up better field for pos2a. */

    pos2a->setbetter = 1;
    redo_setbetterfields(session);
    assert(!pos2a->setbetter);
    assert(pos2a->better == pos1c);

    /*
     * At this point the session tree looks like this:
     *
     * root ___ a: pos1a ____ a: pos2a ____ a: pos3a
     *    |____ b: pos1b
     *    |____ c: pos1c ____ c: pos2c
     *
     * [with pos2a's better => pos1c, and pos3a's better => pos2c]
     */

    /* Create a cycle and verify that redo_suppresscycle() identifies it. */

    memset(sbuf, '_', sizeof sbuf);
    sbuf[1] = 'a';
    pos = pos3a;
    assert(redo_suppresscycle(session, &pos, sbuf, 3));
    assert(pos == pos1a);
    assert(redo_getnextposition(pos1a, 'a') == NULL);
    assert(!pos2a->inuse);
    assert(!pos3a->inuse);
    assert(redo_hassessionchanged(session));

    /* Re-add the deleted moves. */

    sbuf[1] = 'a';
    sbuf[2] = 'a';
    pos2a = redo_addposition(session, pos1a, 'a', sbuf, 0, redo_check);
    assert(pos2a->inuse);
    assert(redo_hassessionchanged(session));
    sbuf[3] = 'a';
    pos3a = redo_addposition(session, pos2a, 'a', sbuf, 0, redo_check);
    assert(pos3a->inuse);
    assert(redo_hassessionchanged(session));
    assert(pos3a->better == pos2c);

    /* Verify that redo_suppresscycle() doesn't see cycles where none exist. */

    sbuf[sizeof sbuf - 3] ^= 1;
    assert(!redo_suppresscycle(session, &pos3a, sbuf, 3));
    assert(!redo_hassessionchanged(session));

    /* Verify that a low prunelimit prevents anything from being deleted. */

    memset(sbuf, '_', sizeof sbuf);
    sbuf[1] = 'a';
    pos = pos3a;
    assert(redo_suppresscycle(session, &pos, sbuf, 2));
    assert(pos == pos1a);
    assert(pos2a->inuse);
    assert(pos3a->inuse);
    assert(!redo_hassessionchanged(session));

    /* Verify that the session contains no solutions. */

    assert(rootpos->solutionsize == 0 && !rootpos->endpoint);
    assert(pos1a->solutionsize == 0 && !pos1a->endpoint);
    assert(pos1b->solutionsize == 0 && !pos1b->endpoint);
    assert(pos1c->solutionsize == 0 && !pos1c->endpoint);
    assert(pos2a->solutionsize == 0 && !pos2a->endpoint);
    assert(pos2c->solutionsize == 0 && !pos2c->endpoint);
    assert(pos3a->solutionsize == 0 && !pos3a->endpoint);

    /* Add to the C branch, including one endpoint position. */

    sbuf[1] = 'c';
    sbuf[2] = 'c';
    sbuf[3] = 'c';
    pos3c = redo_addposition(session, pos2c, 'c', sbuf, 0, redo_check);
    assert(pos3c);
    assert(pos3c->movecount == 3);
    assert(!pos3c->endpoint);
    sbuf[4] = 'a';
    pos4a = redo_addposition(session, pos3c, 'a', sbuf, 0, redo_check);
    assert(pos4a);
    assert(pos4a->movecount == 4);
    assert(!pos4a->endpoint);
    sbuf[4] = 'c';
    pos4c = redo_addposition(session, pos3c, 'c', sbuf, 1, redo_check);
    assert(pos4c);
    assert(pos4c->movecount == 4);
    assert(pos4c->endpoint);
    assert(pos3c->nextcount == 2);
    assert(redo_hassessionchanged(session));

    /* Verify that the entire solution path is marked (and nothing else). */

    assert(pos3c->solutionsize == 4);
    assert(pos2c->solutionsize == 4);
    assert(pos1c->solutionsize == 4);
    assert(rootpos->solutionsize == 4);
    assert(pos1a->solutionsize == 0);
    assert(pos4a->solutionsize == 0);

    /* Verify a longer solution path doesn't replace the shorter one. */

    sbuf[4] = 'a';
    sbuf[5] = 'a';
    pos5a = redo_addposition(session, pos4a, 'a', sbuf, 1, redo_check);
    assert(pos5a);
    assert(pos5a->endpoint);
    assert(pos5a->movecount == 5);
    assert(pos5a->solutionsize == 5);
    assert(pos4a->solutionsize == 5);
    assert(pos3c->solutionsize == 4);
    assert(rootpos->solutionsize == 4);
    assert(redo_hassessionchanged(session));

    /* Copy the (shorter) solution path proceeding from pos1c to pos2a. */

    assert(pos2a->solutionsize == 0);
    redo_duplicatepath(session, pos2a, pos1c);
    assert(pos2a->solutionsize == 5);
    assert(pos2a->nextcount == 2);
    assert(redo_getnextposition(pos2a, 'c') != NULL);
    assert(redo_hassessionchanged(session));

    /*
     * The session tree now looks like this:
     *
     * root ___ a: pos1a ___ a: pos2a ___ a: pos3a
     *    |                         |____ c: (dup) ___ c: (dup) ___ c: (dup)*
     *    |____ b: pos1b
     *    |____ c: pos1c ___ c: pos2c ___ c: pos3c ___ a: pos4a ___ a: pos5a*
     *                                           |____ c: pos4c*
     *
     * [asterisks mark endpoint positions]
     */

    /* Add a new position off of rootpos that's equivalent to pos3c. */

    memset(sbuf, '_', sizeof sbuf);
    sbuf[1] = 'c';
    sbuf[2] = 'c';
    sbuf[3] = 'c';
    pos1d = redo_addposition(session, rootpos, 'd', sbuf, 0, redo_check);
    assert(pos1d);
    assert(pos1d->prev == rootpos);
    assert(pos1d->movecount == 1);
    assert(pos1d->inuse);
    assert(rootpos->nextcount == 4);
    assert(redo_hassessionchanged(session));
    assert(pos3c->better == pos1d);

    /* Verify that the requested grafting behavior was correctly applied. */

    switch (grafting) {

        /* No graft: the moves underneath pos3c remain there. */

      case redo_nograft:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionsize == 4);
        assert(pos3c->solutionsize == 4);
        assert(pos1d->solutionsize == 0);
        assert(pos3c->next != NULL);
        assert(pos3c->nextcount == 2);
        assert(pos1d->next == NULL);
        assert(pos1d->nextcount == 0);
        break;

        /* Graft: pos3c's children are grafted onto pos1d wholesale. */

      case redo_graft:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionsize == 2);
        assert(pos1d->solutionsize == 2);
        assert(pos1c->solutionsize == 0);
        assert(pos3c->solutionsize == 0);
        assert(pos3c->next == NULL);
        assert(pos3c->nextcount == 0);
        assert(pos1d->next != NULL);
        assert(pos1d->nextcount == 2);
        pos = redo_getnextposition(pos1d, 'a');
        assert(pos == pos4a);
        pos = redo_getnextposition(pos1d, 'c');
        assert(pos == pos4c);
        break;

        /* Copy path: pos1d gets a copy of the shortest solution path. */

      case redo_copypath:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionsize == 2);
        assert(pos1d->solutionsize == 2);
        assert(pos1c->solutionsize == 4);
        assert(pos3c->solutionsize == 4);
        assert(pos3c->next != NULL);
        assert(pos3c->nextcount == 2);
        pos = redo_getnextposition(pos3c, 'c');
        assert(pos == pos4c);
        assert(pos1d->next != NULL);
        assert(pos1d->nextcount == 1);
        assert(pos1d->next != NULL);
        assert(pos1d->next->move == 'c');
        assert(pos1d->next->p != pos4a);
        assert(pos1d->next->p->endpoint);
        break;

        /* Graft and copy: after the graft, pos3c gets a path copied back. */

      case redo_graftandcopy:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionsize == 2);
        assert(pos1d->solutionsize == 2);
        assert(pos1c->solutionsize == 4);
        assert(pos3c->solutionsize == 4);
        assert(pos3c->next != NULL);
        assert(pos3c->nextcount == 1);
        assert(pos3c->next->p != pos4a);
        assert(pos3c->next->p->endpoint);
        assert(pos1d->next != NULL);
        assert(pos1d->nextcount == 2);
        pos = redo_getnextposition(pos1d, 'a');
        assert(pos == pos4a);
        pos = redo_getnextposition(pos1d, 'c');
        assert(pos == pos4c);
        break;
    }

    teardown();
}

/* Test the validity of state comparisons when adding positions.
 */
static void test_statecompares(void)
{
    char state[sizeof sbuf + 1];
    redo_position *pos, *pos2;
    int i;

    setup();

    /* Verify that every byte in the state is examined when comparing. */

    memcpy(state, redo_getsavedstate(rootpos), sizeof sbuf);
    state[sizeof sbuf] = state[0];
    for (i = 0 ; i < (int)(sizeof sbuf) - 1 ; ++i) {
        state[i] ^= 1;
        pos = redo_addposition(session, rootpos, i, state, 0, redo_check);
        assert(pos);
        assert(pos->better == NULL);
        assert(rootpos->nextcount == i + 1);
    }

    /* Verify that the byte beyond cmpsize is not examined. */

    state[i] ^= 1;
    pos2 = redo_addposition(session, rootpos, i, state, 0, redo_check);
    assert(pos2);
    assert(pos2->better == pos);

    teardown();
}

int main(void)
{
    test_init();
    test_overall(redo_nograft);
    test_overall(redo_graft);
    test_overall(redo_copypath);
    test_overall(redo_graftandcopy);
    test_statecompares();
    return 0;
}
