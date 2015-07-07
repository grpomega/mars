/*
 * MARS Long Distance Replication Software
 *
 * This file is part of MARS project: http://schoebel.github.io/mars/
 *
 * Copyright (C) 2010-2014 Thomas Schoebel-Theuer
 * Copyright (C) 2011-2014 1&1 Internet AG
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _PROVISIONARY_WRAPPER
#define _PROVISIONARY_WRAPPER

/* TRANSITIONAL compatibility to BOTH the old prepatch
 * and the new wrapper around vfs_*(). Both will be replaced
 * for kernel upstream.
 */
#ifndef MARS_MAJOR
#define __USE_COMPAT
#endif

#ifdef __USE_COMPAT

int _provisionary_wrapper_to_vfs_symlink(const char __user *oldname,
					 const char __user *newname);

int _provisionary_wrapper_to_vfs_mkdir(const char __user *pathname,
				       int mode);

int _provisionary_wrapper_to_vfs_rename(const char __user *oldname,
					const char __user *newname);

#endif
#endif
