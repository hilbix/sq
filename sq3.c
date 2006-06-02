/* $Header$
 *
 * Just a wrapper.  Sadly, SQLITE3 does not support bind.
 * So this cannot be adopted easyly to SQLITE2.
 *
 * Copyright (C)2006 Valentin Hilbig, webmaster@scylla-charybdis.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Log$
 * Revision 1.1  2006-06-02 01:31:49  tino
 * First version, global commit
 *
 */

#include <stdio.h>
#include <sqlite3.h>
#define	SQ_PREFIX(X)	sqlite3##X
#include "sq3_version.h"
#define	SQ_VERSION	SQ3_VERSION
#include "sq.h"
