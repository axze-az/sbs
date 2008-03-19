/* 
 *  msg.h - header containing message functions
 *  Copyright (C) 2008  Axel Zeuner
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#if !defined (__MSG_H__)
#define __MSG_H__ 1

#include <stdarg.h>

extern int daemonized;

extern int msg_va(int prio, const char* fmt, va_list va);
extern int msg(int prio, const char* fmt, ...) 
	__attribute__ ((format (printf, 2, 3)));
extern int exit_msg(int code, const char* fmt, ...) 
	__attribute__((noreturn))
	__attribute__ ((format (printf, 2, 3)));
extern int err_msg(const char* fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern int warn_msg(const char* fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern int info_msg(const char* fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#endif
