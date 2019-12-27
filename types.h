/* types.h: The type names of the redo library.
 *
 * Copyright (C) 2013 by Brian Raiter. This program is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef _libredo_types_h_
#define _libredo_types_h_

/*
 * These typedefs are broken out here for the convenience of code that
 * needs to refer to these types opaquely, but doesn't need all the
 * other definitions in the library's main header file.
 */

typedef struct redo_session redo_session;
typedef struct redo_position redo_position;
typedef struct redo_branch redo_branch;

#endif
