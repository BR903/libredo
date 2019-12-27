/* redo.c: Growing and navigating a tree of states.
 *
 * Copyright (C) 2013 by Brian Raiter. This program is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "redo.h"

/* A redo session.
 */
struct redo_session {
    redo_position *root;        /* the session tree's root position */
    redo_position *parray;      /* the allocated redo_position array */
    redo_position *pfree;       /* pointer to a redo_position not in use */
    redo_branch *barray;        /* the allocated redo_branch array */
    redo_branch *bfree;         /* pointer to a redo_branch not in use */
    unsigned short statesize;   /* the size of the stored game state */
    unsigned short cmpsize;     /* how much of the state to compare */
    unsigned short elementsize; /* total byte size for each position */
    unsigned char changeflag;   /* used to track changes to the session */
    unsigned char grafting;     /* should grafts leave the solution path? */
};

/* Increment a redo_position pointer.
 */
#define incpos(s, p) ((redo_position*)((char*)(p) + (s)->elementsize))

/* Compute the hash value for a given state. This is the Meiyan hash
 * function, created by Sanmayce, slightly simplified.
 */
unsigned short gethashvalue(unsigned int const *data, size_t len)
{
    unsigned int const m = 0x000AD3E7;
    unsigned int const seed = 0x811C9DC5;
    unsigned int h, k, i;

    for (h = seed ; len >= 2 * sizeof *data ; len -= 2 * sizeof *data) {
        k = *data++;
        k = ((k << 5) | (k >> 27)) ^ *data++;
        h = (h ^ k) * m;
    }
    for (i = 0 ; i < len ; ++i)
        h ^= ((unsigned char*)data)[i] << (i * 8);
    h *= m;
    return h ^ (h >> 16);
}

/* Copy a state to a position.
 */
static void savestate(redo_session const *session, redo_position *position,
                      void const *state, int endpoint)
{
    position->endpoint = endpoint;
    position->hashvalue = gethashvalue(state, session->cmpsize);
    memcpy((void*)redo_getsavedstate(position), state, session->statesize);
}

/* Test if the given state is identical to the one saved at a
 * position.
 */
static int comparesavedstate(redo_session const *session,
                             redo_position const *position, void const *state)
{
    return !memcmp(redo_getsavedstate(position), state, session->cmpsize);
}

/*
 * Memory management.
 *
 * redo_position structs are allocated in arenas, or chunks, rather
 * than have each struct be a separate allocation. Unused structs are
 * kept in a linked list by reusing the prev field. (The inuse field
 * indicates whether or not a struct is currently being used.) The
 * pfree field of redo_session holds the head of this linked list. In
 * addition, each chunk is itself a member in a linked list: the last
 * element in each chunk is never used as a redo_position; instead,
 * its prev field points to the next chunk in the list of chunks. The
 * parray field of redo_session holds the head of this linked list.
 * Every redo_position struct is immediately followed by its own state
 * buffer. Because the state buffer's size is determined by the
 * caller, iterating over the elements of a chunk requires special
 * code to increment the element pointer.
 *
 * redo_branch structs are also allocated in chunks. Unused structs
 * are kept in a linked list by reusing the cdr field; a NULL value in
 * the p field indicates that a struct is unused. The redo_position
 * chunks reserve the first element, instead of the last, to hold the
 * pointer to the next chunk. redo_session usees the bfree and barray
 * fields to point to the heads of these lists.
 */

/* Allocate a new array of positions and add it to the linked list.
 */
static int newposarray(redo_session *session)
{
    static int const size = 1024;
    redo_position *array, *pos, *last;
    int i;

    array = malloc(size * (sizeof *array + session->elementsize));
    if (!array)
        return 0;
    pos = array;
    for (i = 0 ; i < size - 1 ; ++i) {
        pos->inuse = 0;
        pos->inarray = 1;
        last = pos;
        pos->prev = incpos(session, pos);
        pos = pos->prev;
    }
    last->prev = NULL;
    pos->inuse = 0;
    pos->inarray = 0;
    pos->prev = session->parray;
    session->parray = array;
    session->pfree = array;
    return 1;
}

/* Allocate a new array of redo_branch structs and add it to the
 * linked list.
 */
static int newbrancharray(redo_session *session)
{
    static int const size = 1024;
    redo_branch *array;
    int i;

    array = malloc(size * sizeof *array);
    if (!array)
        return 0;
    for (i = 1 ; i < size ; ++i) {
        array[i].p = NULL;
        array[i].cdr = &array[i + 1];
    }
    array[size - 1].cdr = NULL;
    array[0].p = NULL;
    array[0].cdr = session->barray;
    session->barray = array;
    session->bfree = array + 1;
    return 1;
}

/* Grab an unused redo_position and initialize it with the given state.
 */
static redo_position *getpositionstruct(redo_session *session,
                                        void const *state, int endpoint)
{
    redo_position *position;

    position = session->pfree;
    session->pfree = session->pfree->prev;
    if (!session->pfree)
        if (!newposarray(session))
            return NULL;
    savestate(session, position, state, endpoint);
    position->inuse = 1;
    return position;
}

/* Mark a redo_position as unused.
 */
static void droppositionstruct(redo_session *session, redo_position *position)
{
    position->inuse = 0;
    position->prev = session->pfree;
    session->pfree = position;
}

/* Grab an unused redo_branch.
 */
static redo_branch *getbranchstruct(redo_session *session, redo_position *p,
                                    int move, redo_branch *cdr)
{
    redo_branch *branch;

    branch = session->bfree;
    session->bfree = branch->cdr;
    if (!session->bfree)
        if (!newbrancharray(session))
            return NULL;
    branch->p = p;
    branch->move = move;
    branch->cdr = cdr;
    return branch;
}

/* Mark a redo_branch as unused.
 */
static void dropbranchstruct(redo_session *session, redo_branch *branch)
{
    branch->p = NULL;
    branch->cdr = session->bfree;
    session->bfree = branch;
}

/* Create a branch from the given position via the given move, if it
 * does not already exists.
 */
static redo_branch *insertmoveto(redo_session *session,
                                 redo_position *from, redo_position *to,
                                 int move)
{
    redo_branch *branch;

    for (branch = from->next ; branch ; branch = branch->cdr)
        if (branch->move == move)
            return branch;

    branch = getbranchstruct(session, to, move, from->next);
    if (!branch)
        return NULL;
    from->next = branch;
    ++from->nextcount;
    return branch;
}

/* Delete a branch representing a move between two positions. The
 * function does nothing if no such move exists.
 */
static redo_branch *dropmoveto(redo_session *session,
                               redo_position *from, redo_position *to)
{
    redo_branch *branch, *next;

    next = from->next;
    if (!next)
        return NULL;

    if (next->p == to) {
        from->next = next->cdr;
    } else {
        for (;;) {
            branch = next;
            next = next->cdr;
            if (!next)
                break;
            if (next->p == to) {
                branch->cdr = next->cdr;
                break;
            }
        }
    }
    if (next) {
        dropbranchstruct(session, next);
        --from->nextcount;
    }

    return next;
}

/* Compare the given state with all the states in the session. If any
 * positions with identical states are found, return the one with the
 * smallest move count. NULL is returned if no positions have a
 * matching state.
 */
static redo_position *checkforequiv(redo_session const *session,
                                    void const *state)
{
    redo_position *equiv, *pos;
    unsigned short hashvalue;

    hashvalue = gethashvalue(state, session->cmpsize);
    for (pos = session->parray  ; pos ; pos = pos->prev) {
        for ( ; pos->inarray ; pos = incpos(session, pos)) {
	    if (!pos->inuse)
		continue;
            if (!pos->setbetter && pos->hashvalue == hashvalue &&
                                   comparesavedstate(session, pos, state)) {
                equiv = pos;
                while (equiv->better)
                    equiv = equiv->better;
                return equiv;
            }
        }
    }
    return NULL;
}

/* Delete the nodes in the path leading from branchpoint to leaf in
 * the session. Nodes are deleted from leaf upwards. The return value
 * is true if all positions between leaf and branchpoint are deleted.
 * If a position is found that has more than one move leading from it,
 * no further deletions are done and the function returns false.
 */
static int prunebranch(redo_session *session, redo_position *leaf,
                       redo_position *branchpoint)
{
    redo_position *pos;

    pos = leaf;
    while (pos && pos != branchpoint) {
        if (pos->next)
            return 0;
        leaf = pos;
        pos = pos->prev;
        dropmoveto(session, pos, leaf);
        droppositionstruct(session, leaf);
        session->changeflag = 1;
    }
    return 1;
}

/* Change the movecount of the nodes of the subtree rooted at position
 * by delta. The solutionsize fields, if non-zero, are likewise
 * adjusted.
 */
static void adjustmovecount(redo_position *position, int delta)
{
    redo_branch *branch;

    position->movecount += delta;
    if (position->solutionsize)
        position->solutionsize += delta;
    if (position->better &&
                position->better->movecount > position->movecount) {
        position->better->better = position;
        position->better = NULL;
    }
    for (branch = position->next ; branch ; branch = branch->cdr)
        if (branch->p)
            adjustmovecount(branch->p, delta);
}

/* Move the entire subtree rooted at src to dest, leaving src a leaf
 * node upon return. No nodes are allocated or freed by this function.
 */
static void graftbranch(redo_position *dest, redo_position *src)
{
    redo_branch *branch;
    int n;

    dest->next = src->next;
    dest->nextcount = src->nextcount;
    src->next = NULL;
    src->nextcount = 0;
    for (branch = dest->next ; branch ; branch = branch->cdr)
        if (branch->p)
            branch->p->prev = dest;
    n = dest->movecount - src->movecount;
    dest->movecount = src->movecount;
    dest->solutionsize = src->solutionsize;
    adjustmovecount(dest, n);
    if (dest->solutionsize) {
        n = dest->solutionsize;
        for (dest = dest->prev ; dest ; dest = dest->prev)
            if (dest->solutionsize == 0 || dest->solutionsize > n)
                dest->solutionsize = n;
    }
}

/* Refresh the solutionsize field for each node along the path leading
 * from the given node to the session's root node.
 */
static void recalcsolutionsize(redo_position *position)
{
    redo_branch *branch;
    int size;

    while (position) {
        size = 0;
        for (branch = position->next ; branch ; branch = branch->cdr)
            if (branch->p && branch->p->solutionsize)
                if (!size || size > branch->p->solutionsize)
                    size = branch->p->solutionsize;
        position->solutionsize = size;
        position = position->prev;
    }
}

/*
 * Exported functions.
 */

/* Create a new session with a single position at the root.
 */
redo_session *redo_beginsession(void const *initialstate,
				int size, int cmpsize)
{
    redo_session *session;
    int n;

    if (size <= 0 || size > USHRT_MAX || cmpsize < 0 || cmpsize > size)
	return NULL;
    n = sizeof(redo_position) + size + (sizeof(void*) - 1);
    n = n - n % sizeof(void*);
    if (n > USHRT_MAX)
        return NULL;
    session = malloc(sizeof *session);
    if (!session)
        return NULL;
    session->statesize = size;
    session->cmpsize = cmpsize ? cmpsize : size;
    session->elementsize = n;
    session->grafting = redo_graft;
    session->parray = NULL;
    session->pfree = NULL;
    session->barray = NULL;
    session->bfree = NULL;
    if (!newposarray(session) || !newbrancharray(session)) {
        redo_endsession(session);
        return NULL;
    }
    session->root = redo_addposition(session, NULL, 0, initialstate, 0, 0);
    if (!session->root) {
        redo_endsession(session);
        return NULL;
    }
    session->changeflag = 0;
    return session;
}

/* Change the grafting behavior option.
 */
int redo_setgraftbehavior(redo_session *session, int grafting)
{
    int oldvalue;

    oldvalue = session->grafting;
    session->grafting = grafting;
    return oldvalue;
}

/* Return the session's root position.
 */
redo_position *redo_getfirstposition(redo_session const *session)
{
    return session->root;
}

/* Return a pointer to the state data associated with a position.
 */
void const *redo_getsavedstate(redo_position const *position)
{
    return position + 1;
}

/* Return the redo_branch for the branch originating at this position
 * and labelled with this move. NULL is returned if there is no such
 * branch in the session. If the branch is found, it is automatically
 * moved to the head of the next list.
 */
redo_position *redo_getnextposition(redo_position *position, int move)
{
    redo_branch *branch, *cdr;

    if (!position->next)
        return NULL;
    if (position->next->move == move)
        return position->next->p;
    for (branch = position->next ; branch->cdr ; branch = branch->cdr) {
        if (branch->cdr->move == move) {
            cdr = branch->cdr;
            branch->cdr = branch->cdr->cdr;
            cdr->cdr = position->next;
            position->next = cdr;
            return cdr->p;
        }
    }
    return NULL;
}

/* Add a new node to the session, leading from prev via move. If such
 * a node already exists, it is returned; otherwise, the node is
 * created, fully initialized, and returned. In the latter case,
 * gameplay supplies the game state for the new node. If checkequiv's
 * value is redo_check, then the function will check for equivalent
 * nodes in the session. If one is found, the better field will be
 * intialized to point to it, or, if the new node is actually the
 * other node's better, the latter's subtree is grafted onto the new
 * node directly.
 */
redo_position *redo_addposition(redo_session *session,
                                redo_position *prev, int move,
                                void const *state, int endpoint,
                                int checkequiv)
{
    redo_position *position, *equiv, *p;
    redo_branch *branch;
    unsigned short size;

    if (prev) {
        position = redo_getnextposition(prev, move);
        if (position)
            return position;
    }

    if (checkequiv == redo_check && !endpoint)
        equiv = checkforequiv(session, state);
    else
        equiv = NULL;

    position = getpositionstruct(session, state, endpoint);
    if (!position)
        return NULL;
    if (prev) {
        branch = insertmoveto(session, prev, position, move);
        if (!branch) {
            droppositionstruct(session, position);
            return NULL;
        }
    }

    position->better = NULL;
    position->setbetter = checkequiv == redo_checklater;
    position->prev = prev;
    position->next = NULL;
    position->nextcount = 0;

    position->movecount = prev ? prev->movecount + 1 : 0;
    position->solutionsize = 0;
    if (endpoint) {
        size = position->movecount;
        position->solutionsize = size;
        for (p = position->prev ; p ; p = p->prev) {
            if (p->solutionsize && p->solutionsize <= size)
                break;
            p->solutionsize = size;
        }
    }

    if (equiv) {
        if (position->movecount >= equiv->movecount) {
            position->better = equiv;
        } else {
            equiv->better = position;
            if (session->grafting == redo_copypath) {
                redo_duplicatepath(session, position, equiv);
            } else if (session->grafting != redo_nograft) {
                graftbranch(position, equiv);
                recalcsolutionsize(equiv);
                if (session->grafting == redo_graftandcopy)
                    redo_duplicatepath(session, equiv, position);
            }
        }
    }

    session->changeflag = 1;
    return position;
}

/* Delete a leaf node position from the session. The return value is
 * the position's parent node, or the original position if it cannot
 * be deleted.
 */
redo_position *redo_dropposition(redo_session *session,
                                 redo_position *position)
{
    redo_position *prev;
    redo_position *better, *pos;

    if (!position->prev || position->next)
        return position;
    prev = position->prev;
    if (!dropmoveto(session, prev, position))
        return position;

    better = position->better;
    for (pos = session->parray ; pos ; pos = pos->prev)
        for ( ; pos->inarray ; pos = incpos(session, pos))
            if (pos->inuse && pos->better == position)
                pos->better = better;

    droppositionstruct(session, position);
    recalcsolutionsize(prev);
    session->changeflag = 1;
    return prev;
}

/* Check that the given state isn't a revisiting of a state already
 * seen in the given move path. If it is, change *pposition to the
 * earlier position. If the intermediate steps are a single line and
 * within the length of prunelimit, then they are dropped.
 */
int redo_suppresscycle(redo_session *session, redo_position **pposition,
                       void const *state, int prunelimit)
{
    redo_position *p;
    int n;

    for (p = *pposition, n = 0 ; p ; p = p->prev, ++n) {
        if (comparesavedstate(session, p, state)) {
            if (n < prunelimit)
                prunebranch(session, *pposition, p);
            *pposition = p;
            return 1;
        }
    }
    return 0;
}

/* Find the path of the best solution emanating from src and make a
 * copy of it rooted at dest.
 */
int redo_duplicatepath(redo_session *session,
                       redo_position *dest, redo_position const *src)
{
    redo_branch *branch;
    redo_position *next;

    if (src->solutionsize == 0)
        return 0;

    while (src && src->solutionsize) {
        for (branch = src->next ; branch ; branch = branch->cdr)
            if (branch->p && branch->p->solutionsize == src->solutionsize)
                break;
        if (!branch)
            break;
        next = redo_addposition(session, dest, branch->move,
                                redo_getsavedstate(branch->p),
                                branch->p->endpoint, 0);
        if (!next)
            return 0;
        if (!dest->better && dest->movecount >= src->movecount)
            dest->better = src->better ? src->better : (redo_position*)src;
        src = branch->p;
        dest = next;
    }
    return 1;
}

/* Find all positions with setbetter flagged and initialize their
 * better field.
 */
void redo_setbetterfields(redo_session const *session)
{
    redo_position *position, *other;

    for (position = session->parray ; position ; position = position->prev) {
        for ( ; position->inarray ; position = incpos(session, position)) {
	    if (!position->inuse)
		continue;
            if (position->setbetter) {
                other = checkforequiv(session, redo_getsavedstate(position));
                position->better = other;
                if (other && other->movecount > position->movecount) {
                    position->better = NULL;
                    if (!other->better) {
                        other->better = position;
                        other->setbetter = 0;
                    }
                }
                position->setbetter = 0;
            }
        }
    }
}

/* Reset the change flag and return the prior value.
 */
int redo_hassessionchanged(redo_session *session)
{
    int flag = session->changeflag;
    session->changeflag = 0;
    return flag;
}

/* Free all memory associated with the session.
 */
void redo_endsession(redo_session *session)
{
    redo_position *position, *p;
    redo_branch *branch, *b;

    if (!session)
        return;
    for (position = session->parray ; position ; position = p) {
        for (p = position ; p->inarray ; p = incpos(session, p)) ;
        p = p->prev;
        free(position);
    }
    for (branch = session->barray ; branch ; branch = b) {
        b = branch->cdr;
        free(branch);
    }
    free(session);
}
