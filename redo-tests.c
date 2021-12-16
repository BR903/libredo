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

/* The state size is intentionally given an odd value. One byte of
 * state data is non-comparing.
 */
#define SIZE_STATE 33
#define SIZE_CMPSTATE (SIZE_STATE - 1)

/* A state data buffer for general use.
 */
static char sbuf[SIZE_STATE];

/* Minimal smoke test, run before the real tests.
 */
static void test_init(void)
{
    redo_session *s;
    void *p;

    /* Verify that session creation and deletion works at all. */

    s = redo_beginsession("", 1, 0);
    assert(s);
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
    session = redo_beginsession(sbuf, SIZE_STATE, SIZE_CMPSTATE);
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

/* Test the validity of state comparisons when adding positions.
 */
static void test_statecompares(void)
{
    char state[SIZE_STATE];
    char const *s;
    redo_position *pos, *pos2;
    int i;

    setup();

    /* Verify that every comparing byte in the state is significant. */

    memcpy(state, redo_getsavedstate(rootpos), SIZE_STATE);
    for (i = 0 ; i < SIZE_CMPSTATE ; ++i) {
        state[i] ^= 1;
        pos = redo_addposition(session, rootpos, i, state, 0, redo_check);
        assert(pos);
        assert(pos->better == NULL);
        assert(rootpos->nextcount == i + 1);
    }
    assert(redo_getsessionsize(session) == i + 1);

    /* Verify that the non-comparing byte of state data is not examined. */

    state[i] ^= 1;
    pos2 = redo_addposition(session, rootpos, i, state, 0, redo_check);
    assert(pos2);
    assert(pos2->better == pos);

    /* Verify that state data can be changed, but only non-comparing bytes. */

    memcpy(state, redo_getsavedstate(pos2), SIZE_STATE);
    for (i = 0 ; i < SIZE_STATE ; ++i)
        state[i] ^= 0xFF;
    redo_updatesavedstate(session, pos2, state);
    s = redo_getsavedstate(pos2);
    for (i = 0 ; i < SIZE_CMPSTATE ; ++i)
        assert(state[i] != s[i]);
    assert(state[i] == s[i]);

    teardown();
}

/* Give the full API a test run for one of the grafting behaviors.
 */
static void test_overall(int grafttype)
{
    redo_position *pos1a, *pos1b, *pos1c, *pos1d, *pos2a, *pos2c,
                  *pos3a, *pos3c, *pos4a, *pos4c, *pos5a;
    redo_position *pos;
    int g;

    setup();
    assert(redo_getsessionsize(session) == 1);

    g = redo_setgraftbehavior(session, redo_nograft);
    assert(g == redo_graft);
    g = redo_setgraftbehavior(session, grafttype);
    assert(g == redo_nograft);

    memset(sbuf, '.', sizeof sbuf);

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
    assert(redo_getsessionsize(session) == 2);

    /* Verify that the change flag works as expected. */

    assert(redo_hassessionchanged(session));
    assert(redo_clearsessionchanged(session));
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
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 3);

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
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 4);

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
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 3);

    /* Re-add the deleted move. */

    pos2a = redo_addposition(session, pos1a, 'a', sbuf, 0, redo_check);
    assert(pos2a);
    assert(pos2a->inuse);
    assert(pos2a != pos1a);
    assert(pos2a->prev == pos1a);
    assert(pos2a->movecount == 2);
    assert(pos1a->nextcount == 1);
    assert(pos1a->next != NULL);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 4);

    /* Verify that deleting a non-leaf position will do nothing. */

    pos = redo_dropposition(session, pos1a);
    assert(pos == pos1a);
    assert(pos1a->inuse);
    assert(redo_getsessionsize(session) == 4);
    assert(!redo_clearsessionchanged(session));

    /* Verify that repeating a move won't create a new position. */

    pos = redo_addposition(session, rootpos, 'b', sbuf, 0, redo_check);
    assert(pos == pos1b);
    assert(pos1b->movecount == 1);
    assert(pos1a->nextcount == 1);
    assert(rootpos->nextcount == 2);
    assert(redo_getsessionsize(session) == 4);
    assert(!redo_clearsessionchanged(session));

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
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 5);

    /* Verify that identical states are not seen when equivcheck isn't done. */

    sbuf[3] = 'a';
    pos3a = redo_addposition(session, pos2a, 'a', sbuf, 0, redo_checklater);
    assert(pos3a);
    assert(pos3a != pos2a);
    assert(pos3a->prev == pos2a);
    assert(pos3a->movecount == 3);
    assert(pos2a->nextcount == 1);
    assert(pos3a->better == NULL);
    assert(pos3a->setbetter);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 6);

    pos2c = redo_addposition(session, pos1c, 'c', sbuf, 0, redo_nocheck);
    assert(pos2c);
    assert(pos2c != pos3a);
    assert(pos2c->prev == pos1c);
    assert(pos2c->movecount == 2);
    assert(pos1c->nextcount == 1);
    assert(pos2c->better == NULL);
    assert(!pos2c->setbetter);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 7);

    /* Verify that redo_setbetterfields() finds matches for marked states. */

    pos2a->better = NULL;
    pos1a->setbetter = 1;
    redo_setbetterfields(session);
    assert(!pos3a->setbetter);
    assert(!pos1a->setbetter);
    assert(pos3a->better == pos2c);
    assert(pos2a->better == NULL);
    assert(pos1a->better == NULL);
    assert(redo_getsessionsize(session) == 7);
    assert(!redo_clearsessionchanged(session));

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
     * [with pos2a's better -> pos1c, and pos3a's better -> pos2c]
     */

    /* Create a cycle and verify that redo_suppresscycle() identifies it. */

    memset(sbuf, '.', sizeof sbuf);
    sbuf[1] = 'a';
    pos = pos3a;
    assert(redo_suppresscycle(session, &pos, sbuf, 3));
    assert(pos == pos1a);
    assert(redo_getnextposition(pos1a, 'a') == NULL);
    assert(!pos2a->inuse);
    assert(!pos3a->inuse);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 5);

    /* Re-add the deleted moves. */

    sbuf[1] = 'a';
    sbuf[2] = 'a';
    pos2a = redo_addposition(session, pos1a, 'a', sbuf, 0, redo_check);
    assert(pos2a->inuse);
    assert(redo_clearsessionchanged(session));
    sbuf[3] = 'a';
    pos3a = redo_addposition(session, pos2a, 'a', sbuf, 0, redo_check);
    assert(pos3a->inuse);
    assert(redo_clearsessionchanged(session));
    assert(pos3a->better == pos2c);
    assert(redo_getsessionsize(session) == 7);

    /* Verify that redo_suppresscycle() doesn't see cycles where none exist. */

    sbuf[SIZE_CMPSTATE - 1] ^= 1;
    assert(!redo_suppresscycle(session, &pos3a, sbuf, 3));
    assert(!redo_clearsessionchanged(session));

    /* Verify that a low prunelimit prevents anything from being deleted. */

    memset(sbuf, '.', sizeof sbuf);
    sbuf[1] = 'a';
    pos = pos3a;
    assert(redo_suppresscycle(session, &pos, sbuf, 2));
    assert(pos == pos1a);
    assert(pos2a->inuse);
    assert(pos3a->inuse);
    assert(redo_getsessionsize(session) == 7);
    assert(!redo_clearsessionchanged(session));

    /* Verify that the session contains no solutions. */

    assert(rootpos->solutionsize == 0 && rootpos->endpoint == 0);
    assert(pos1a->solutionsize == 0 && pos1a->endpoint == 0);
    assert(pos1b->solutionsize == 0 && pos1b->endpoint == 0);
    assert(pos1c->solutionsize == 0 && pos1c->endpoint == 0);
    assert(pos2a->solutionsize == 0 && pos2a->endpoint == 0);
    assert(pos2c->solutionsize == 0 && pos2c->endpoint == 0);
    assert(pos3a->solutionsize == 0 && pos3a->endpoint == 0);

    /* Add to the C branch, including one endpoint position. */

    sbuf[1] = 'c';
    sbuf[2] = 'c';
    sbuf[3] = 'c';
    pos3c = redo_addposition(session, pos2c, 'c', sbuf, 0, redo_check);
    assert(pos3c);
    assert(pos3c->movecount == 3);
    assert(pos3c->endpoint == 0);
    sbuf[4] = 'a';
    pos4a = redo_addposition(session, pos3c, 'a', sbuf, 0, redo_check);
    assert(pos4a);
    assert(pos4a->movecount == 4);
    assert(pos4a->endpoint == 0);
    sbuf[4] = 'c';
    pos4c = redo_addposition(session, pos3c, 'c', sbuf, 1, redo_check);
    assert(pos4c);
    assert(pos4c->movecount == 4);
    assert(pos4c->endpoint == 1);
    assert(pos3c->nextcount == 2);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 10);

    /* Verify that the entire solution path is marked (and nothing else). */

    assert(pos3c->solutionend == 1);
    assert(pos2c->solutionend == 1);
    assert(pos1c->solutionend == 1);
    assert(rootpos->solutionend == 1);
    assert(pos1a->solutionend == 0);
    assert(pos4a->solutionend == 0);

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
    assert(pos5a->endpoint == 1);
    assert(pos5a->movecount == 5);
    assert(pos5a->solutionend == 1);
    assert(pos5a->solutionsize == 5);
    assert(pos4a->solutionend == 1);
    assert(pos4a->solutionsize == 5);
    assert(pos3c->solutionend == 1);
    assert(pos3c->solutionsize == 4);
    assert(rootpos->solutionend == 1);
    assert(rootpos->solutionsize == 4);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 11);

    /* Copy the (shorter) solution path proceeding from pos1c to pos2a. */

    assert(pos2a->solutionend == 0);
    assert(pos2a->solutionsize == 0);
    redo_duplicatepath(session, pos2a, pos1c);
    assert(pos2a->solutionend == 1);
    assert(pos2a->solutionsize == 5);
    assert(pos2a->nextcount == 2);
    assert(redo_getnextposition(pos2a, 'c') != NULL);
    assert(redo_clearsessionchanged(session));
    assert(redo_getsessionsize(session) == 14);

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

    memset(sbuf, '.', sizeof sbuf);
    sbuf[1] = 'c';
    sbuf[2] = 'c';
    sbuf[3] = 'c';
    pos1d = redo_addposition(session, rootpos, 'd', sbuf, 0, redo_check);
    assert(pos1d);
    assert(pos1d->prev == rootpos);
    assert(pos1d->movecount == 1);
    assert(pos1d->inuse);
    assert(rootpos->nextcount == 4);
    assert(pos3c->better == pos1d);
    assert(redo_clearsessionchanged(session));

    /* Verify that the requested grafting behavior was correctly applied. */

    switch (grafttype) {

        /* No graft: the moves underneath pos3c remain there. */

      case redo_nograft:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionend == 1);
        assert(rootpos->solutionsize == 4);
        assert(pos3c->solutionend == 1);
        assert(pos3c->solutionsize == 4);
        assert(pos1d->solutionend == 0);
        assert(pos1d->solutionsize == 0);
        assert(pos3c->next != NULL);
        assert(pos3c->nextcount == 2);
        assert(pos1d->next == NULL);
        assert(pos1d->nextcount == 0);
        assert(redo_getsessionsize(session) == 15);
        break;

        /* Graft: pos3c's children are grafted onto pos1d wholesale. */

      case redo_graft:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionend == 1);
        assert(rootpos->solutionsize == 2);
        assert(pos1d->solutionend == 1);
        assert(pos1d->solutionsize == 2);
        assert(pos1c->solutionend == 0);
        assert(pos1c->solutionsize == 0);
        assert(pos3c->solutionend == 0);
        assert(pos3c->solutionsize == 0);
        assert(pos3c->next == NULL);
        assert(pos3c->nextcount == 0);
        assert(pos1d->next != NULL);
        assert(pos1d->nextcount == 2);
        pos = redo_getnextposition(pos1d, 'a');
        assert(pos == pos4a);
        pos = redo_getnextposition(pos1d, 'c');
        assert(pos == pos4c);
        assert(redo_getsessionsize(session) == 15);
        break;

        /* Copy path: pos1d gets a copy of the shortest solution path. */

      case redo_copypath:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionend == 1);
        assert(rootpos->solutionsize == 2);
        assert(pos1d->solutionend == 1);
        assert(pos1d->solutionsize == 2);
        assert(pos1c->solutionend == 1);
        assert(pos1c->solutionsize == 4);
        assert(pos3c->solutionend == 1);
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
        assert(pos1d->next->p->endpoint == 1);
        assert(redo_getsessionsize(session) == 16);
        break;

        /* Graft and copy: after the graft, pos3c gets a path copied back. */

      case redo_graftandcopy:
        assert(pos1d->better == NULL);
        assert(rootpos->solutionend == 1);
        assert(rootpos->solutionsize == 2);
        assert(pos1d->solutionend == 1);
        assert(pos1d->solutionsize == 2);
        assert(pos1c->solutionend == 1);
        assert(pos1c->solutionsize == 4);
        assert(pos3c->solutionend == 1);
        assert(pos3c->solutionsize == 4);
        assert(pos3c->next != NULL);
        assert(pos3c->nextcount == 1);
        assert(pos3c->next->p != pos4a);
        assert(pos3c->next->p->endpoint == 1);
        assert(pos1d->next != NULL);
        assert(pos1d->nextcount == 2);
        pos = redo_getnextposition(pos1d, 'a');
        assert(pos == pos4a);
        pos = redo_getnextposition(pos1d, 'c');
        assert(pos == pos4c);
        assert(redo_getsessionsize(session) == 16);
        break;
    }

    teardown();
}

/* Verify that solution propogation correctly respects endpoint value
 * as well as solution size.
 */
static void test_endpoints(void)
{
    redo_position *pos1a, *pos2a, *pos2c,
                  *pos3a, *pos3b, *pos3c, *pos4b, *pos4c;
    redo_position *pos;

    setup();
    assert(redo_getsessionsize(session) == 1);
    redo_setgraftbehavior(session, redo_graft);
    memset(sbuf, '.', sizeof sbuf);

    /* Build up a small tree of moves to work from. */

    sbuf[0] = '1';
    sbuf[1] = 'a';
    pos1a = redo_addposition(session, rootpos, 'a', sbuf, 0, redo_check);
    sbuf[0] = '2';
    pos2a = redo_addposition(session, pos1a, 'a', sbuf, 0, redo_check);
    sbuf[0] = '3';
    pos3a = redo_addposition(session, pos2a, 'a', sbuf, 0, redo_check);
    sbuf[1] = 'b';
    pos3b = redo_addposition(session, pos2a, 'b', sbuf, 0, redo_check);
    sbuf[0] = '4';
    pos4b = redo_addposition(session, pos3b, 'b', sbuf, 0, redo_check);
    sbuf[0] = '2';
    sbuf[1] = 'c';
    pos2c = redo_addposition(session, pos1a, 'c', sbuf, 0, redo_check);
    sbuf[0] = '3';
    pos3c = redo_addposition(session, pos2c, 'c', sbuf, 0, redo_check);
    sbuf[0] = '4';
    pos4c = redo_addposition(session, pos3c, 'c', sbuf, 0, redo_check);

    /* Verify that the current session tree contains no solutions. */

    assert(redo_getsessionsize(session) == 9);
    assert(rootpos && rootpos->solutionend == 0);
    assert(pos3a && pos3a->solutionend == 0);
    assert(pos4b && pos4b->solutionend == 0);
    assert(pos4c && pos4c->solutionend == 0);

    /*
     * The session tree now has three branches, with no endpoints:
     *
     * root ___ a: pos1a ___ a: pos2a ___ a: pos3a
     *                 |            |____ b: pos3b ___ b: pos4b
     *                 |____ c: pos2c ___ c: pos3c ___ c: pos4c
     */

    sbuf[0] = 'E';

    /* Verify that negative endpoints are recognized. */

    redo_addposition(session, pos4c, 'X', sbuf, -1, redo_check);
    assert(rootpos->solutionend == -1);
    assert(rootpos->solutionsize == 5);

    /* Verify that higher endpoint values get preference. */

    redo_addposition(session, pos4b, 'X', sbuf, 2, redo_check);
    assert(rootpos->solutionend == 2);
    assert(rootpos->solutionsize == 5);
    redo_addposition(session, pos4b, 'Y', sbuf, 3, redo_check);
    assert(rootpos->solutionend == 3);
    assert(rootpos->solutionsize == 5);
    redo_addposition(session, pos4b, 'Z', sbuf, 1, redo_check);
    assert(rootpos->solutionend == 3);
    assert(rootpos->solutionsize == 5);

    /* Verify that endpoint value takes priority over move count. */

    redo_addposition(session, pos3a, 'X', sbuf, 2, redo_check);
    assert(rootpos->solutionend == 3);
    assert(rootpos->solutionsize == 5);

    /* Verify that each branch tracks its own local best solution. */

    assert(pos3a->solutionend == 2);
    assert(pos3a->solutionsize == 4);
    assert(pos3b->solutionend == 3);
    assert(pos3b->solutionsize == 5);
    assert(pos3c->solutionend == -1);
    assert(pos3c->solutionsize == 5);

    /* Verify that grafting updates all solutionend values correctly. */

    sbuf[0] = '4';
    sbuf[1] = 'b';
    assert(pos2c->solutionend == -1);
    assert(pos2c->solutionsize == 5);
    assert(pos4b->better == NULL);
    pos = redo_addposition(session, pos2c, 'd', sbuf, 0, redo_check);
    assert(pos4b->better == pos);
    assert(pos2c->solutionend == 3);
    assert(pos2c->solutionsize == 4);
    assert(pos3c->solutionend == -1);
    assert(pos3c->solutionsize == 5);

    /* Verify that a lower-valued endpoint graft doesn't propagate. */

    sbuf[0] = '4';
    sbuf[1] = 'c';
    assert(pos1a->solutionend == 3);
    assert(pos1a->solutionsize == 4);
    assert(pos4c->better == NULL);
    pos = redo_addposition(session, pos1a, 'e', sbuf, 0, redo_check);
    assert(pos4c->better == pos);
    assert(pos->solutionend == -1);
    assert(pos->solutionsize == 3);
    assert(pos1a->solutionend == 3);
    assert(pos1a->solutionsize == 4);

    teardown();
}

int main(void)
{
    test_init();
    test_statecompares();
    test_overall(redo_nograft);
    test_overall(redo_graft);
    test_overall(redo_copypath);
    test_overall(redo_graftandcopy);
    test_endpoints();
    return 0;
}
