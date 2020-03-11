/* redo.h: Growing and navigating a tree of states.
 *
 * Copyright (C) 2013 by Brian Raiter. This program is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef _libredo_redo_h_
#define _libredo_redo_h_

#ifdef __cplusplus
extern "C" {
#endif

/* The library version: 0.8
 */
#define REDO_LIBRARY_VERSION 0x0008

/*
 * Types.
 */

/* The list of objects used by the library. redo_session is opaque;
 * the other two are defined here.
 */
typedef struct redo_session redo_session;
typedef struct redo_position redo_position;
typedef struct redo_branch redo_branch;

/* The information associated with a visited state.
 */
struct redo_position {
    redo_position *prev;        /* position that points to this position */
    redo_branch *next;          /* linked list of moves from this position */
    redo_position *better;      /* position equal to this one in fewer moves */
    unsigned short movecount;   /* number of moves to reach this position */
    unsigned short solutionsize; /* size of best solution from this position */
    unsigned short nextcount:12; /* number of moves in next list */
    unsigned short endpoint:1;  /* true if this position is an endpoint */
    unsigned short setbetter:1; /* internal: set by redo_checkequivlater */
    unsigned short inuse:1;     /* internal: false if not in the tree */
    unsigned short inarray:1;   /* internal: false at the end of the array */
    unsigned short hashvalue;   /* internal: the state hash value */
};

/* A labeled branch in the tree of visited states.
 */
struct redo_branch {
    redo_branch *cdr;           /* a sibling branch */
    redo_position *p;           /* the position that the move leads to */
    int move;                   /* the move that this branch represents */
};

/*
 * Functions.
 */

/* Create and return a new redo session. initialstate points to a
 * buffer that contains the representation of the state of the
 * starting position, from which all other positions will descend.
 * size is the size of the state representation in bytes. It cannot be
 * larger than ~63k (and ideally should be as small as possible).
 * cmpsize is number of bytes in the state representation to actually
 * compare, or zero to use the entire state representation. NULL is
 * returned if the arguments are invalid, or if memory for the session
 * cannot be allocated.
 */
extern redo_session *redo_beginsession(void const *initialstate,
                                       int size, int cmpsize);

/* Possible values for the grafting argument to redo_setgraftbehavior().
 */
enum { redo_nograft, redo_graft, redo_copypath, redo_graftandcopy };

/* Change the grafting behavior option. This option controls what
 * redo_addposition() does when adding a position that provides a
 * shorter set of moves to a previously discovered state. redo_nograft
 * means do nothing. redo_graft (the default) means transplant the
 * subtree of following moves to the new position. redo_copypath means
 * don't transplant anything but do make a copy of the best solution
 * to the new position. Finally, redo_graftandcopy means do the graft
 * but then copy the best solution back to the old site. (Note that in
 * the case when no solution is currently available from the position
 * in question, then the behavior of redo_copypath is identical to
 * redo_nograft, and redo_graftandcopy is identical to redo_graft.)
 * The return value is the option's previous value.
 */
extern int redo_setgraftbehavior(redo_session *session, int grafting);

/* Return the position for the initial state.
 */
extern redo_position *redo_getfirstposition(redo_session const *session);

/* Return the number of positions stored in the session.
 */
extern int redo_getsessionsize(redo_session const *session);

/* Return a read-only pointer to the copied state associated with a
 * position.
 */
extern void const *redo_getsavedstate(redo_position const *position);

/* Return the position reached by making move from the given position.
 * Calling this function causes the given move to become the most
 * recently used move for the first position. NULL is returned if the
 * move in question has not yet been added to the session.
 */
extern redo_position *redo_getnextposition(redo_position *position, int move);

/* Possible values for the checkequiv argument to redo_addposition().
 */
enum { redo_nocheck, redo_check, redo_checklater };

/* Return a position in the session, obtained by starting at prev and
 * making the given move. If this position already exists, it is
 * returned. Otherwise, a new position is created and added to the
 * session. state points to a buffer containing the representation of
 * the state for the new position. endpoint is true if this state is a
 * valid solution state. checkequiv can take one of three values. If
 * its value is redo_check, then the function will identify other
 * positions in the session that have identical states, and if any are
 * found the position's better field will be initialized (or, if the
 * newly created position is actually the other node's better, the
 * latter's subtree will be grafted onto the new position). A value of
 * redo_checklater will delay this check until the next call to
 * redo_setbetterfields(). Finally, a value of redo_nocheck will
 * bypass this check entirely. NULL is returned if a new position
 * cannot be allocated.
 */
extern redo_position *redo_addposition(redo_session *session,
                                       redo_position *prev, int move,
                                       void const *state, int endpoint,
                                       int checkequiv);

/* Delete a position from the session. In order to be deleted, the
 * position must be a leaf node, i.e. it must not have any branches
 * emanating from it to other positions. Any better fields in the
 * session that point to this node are cleared (or updated, if another
 * position can be substituted). The return value is the deleted
 * position's parent, or the original position if it could not be
 * removed.
 */
extern redo_position *redo_dropposition(redo_session *session,
                                        redo_position *position);

/* Verify that the given state is not a revisiting of a state that
 * appears earlier in the path of moves leading to this position.
 * pposition contains a pointer to the position immediately preceding
 * the state in question. If the state is found to be identical to a
 * previous one, pposition's value is changed to the earlier position
 * and true is returned. Furthermore, if the number of intervening
 * steps between the two equivalent positions are under prunelimit,
 * then they are deleted (assuming no other moves branch out of the
 * cycle). If no match is found for the given position, the return
 * value is false and the session is not changed.
 */
extern int redo_suppresscycle(redo_session *session, redo_position **pposition,
                              void const *state, int prunelimit);

/* Copy the entire sequence of moves leading to the shortest solution
 * from src to the dest position. Nothing is done if no solution path
 * currently exists starting from src. The session's state is
 * undefined if src and dest do not represent identical states. false
 * is returned if sufficient memory was unavailable.
 */
extern int redo_duplicatepath(redo_session *session,
                              redo_position *dest, redo_position const *src);

/* Update the "extra" state data for an existing position, after the
 * compared state data. If redo_beginsession() was called without
 * creating extra state data (i.e. with a non-zero cmpsize argument),
 * then this function will silently do nothing.
 */
extern void redo_updatesavedstate(redo_session const *session,
                                  redo_position *position, void const *state);

/* Examine every position in the session, looking for ones that have
 * the setbetter field set to true. The ones that do will then have
 * their better fields re-initialized. (The purpose of this function
 * is to allow a serializer to omit the value of the better fields,
 * merely noting which ones have a non-NULL value. This function can
 * then recreate the values on deserialization.) The return value is
 * the number of better pointers that were set.
 */
extern int redo_setbetterfields(redo_session const *session);

/* Return true if positions have been added to or removed from the
 * session since it was initialized, or since the last call to
 * redo_clearsessionchanged().
 */
extern int redo_hassessionchanged(redo_session const *session);

/* Reset the session change flag. The flag's prior value is returned.
 */
extern int redo_clearsessionchanged(redo_session *session);

/* Delete the sesssion and free all associated memory.
 */
extern void redo_endsession(redo_session *session);

#ifdef __cplusplus
}
#endif

#endif
