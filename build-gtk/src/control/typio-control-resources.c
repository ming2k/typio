#include <gio/gio.h>

#if defined (__ELF__) && ( __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 6))
# define SECTION __attribute__ ((section (".gresource.typio_control"), aligned (sizeof(void *) > 8 ? sizeof(void *) : 8)))
#else
# define SECTION
#endif

static const SECTION union { const guint8 data[1729]; const double alignment; void * const ptr;}  typio_control_resource_data = {
  "\107\126\141\162\151\141\156\164\000\000\000\000\000\000\000\000"
  "\030\000\000\000\310\000\000\000\000\000\000\050\006\000\000\000"
  "\000\000\000\000\001\000\000\000\001\000\000\000\003\000\000\000"
  "\003\000\000\000\006\000\000\000\066\123\226\144\003\000\000\000"
  "\310\000\000\000\010\000\114\000\320\000\000\000\324\000\000\000"
  "\072\231\364\162\000\000\000\000\324\000\000\000\006\000\114\000"
  "\334\000\000\000\340\000\000\000\324\265\002\000\377\377\377\377"
  "\340\000\000\000\001\000\114\000\344\000\000\000\350\000\000\000"
  "\302\257\211\013\002\000\000\000\350\000\000\000\004\000\114\000"
  "\354\000\000\000\360\000\000\000\012\327\257\261\001\000\000\000"
  "\360\000\000\000\010\000\114\000\370\000\000\000\374\000\000\000"
  "\144\206\130\333\004\000\000\000\374\000\000\000\021\000\166\000"
  "\020\001\000\000\300\006\000\000\150\151\150\165\163\153\171\057"
  "\001\000\000\000\164\171\160\151\157\057\000\000\004\000\000\000"
  "\057\000\000\000\003\000\000\000\143\157\155\057\000\000\000\000"
  "\143\157\156\164\162\157\154\057\005\000\000\000\164\171\160\151"
  "\157\055\143\157\156\164\162\157\154\056\143\163\163\000\000\000"
  "\240\005\000\000\000\000\000\000\100\144\145\146\151\156\145\055"
  "\143\157\154\157\162\040\164\171\160\151\157\137\143\141\156\166"
  "\141\163\137\154\151\147\150\164\040\043\146\062\146\063\146\065"
  "\073\012\100\144\145\146\151\156\145\055\143\157\154\157\162\040"
  "\164\171\160\151\157\137\143\141\156\166\141\163\137\144\141\162"
  "\153\040\043\061\067\061\071\061\143\073\012\012\167\151\156\144"
  "\157\167\056\142\141\143\153\147\162\157\165\156\144\056\143\157"
  "\156\164\162\157\154\055\162\157\157\164\040\173\012\040\040\142"
  "\141\143\153\147\162\157\165\156\144\055\143\157\154\157\162\072"
  "\040\100\164\171\160\151\157\137\143\141\156\166\141\163\137\154"
  "\151\147\150\164\073\012\040\040\143\157\154\157\162\072\040\100"
  "\167\151\156\144\157\167\137\146\147\137\143\157\154\157\162\073"
  "\012\175\012\012\056\143\157\156\164\162\157\154\055\163\150\145"
  "\154\154\040\173\012\040\040\142\141\143\153\147\162\157\165\156"
  "\144\055\143\157\154\157\162\072\040\100\164\171\160\151\157\137"
  "\143\141\156\166\141\163\137\154\151\147\150\164\073\012\175\012"
  "\012\100\155\145\144\151\141\040\050\160\162\145\146\145\162\163"
  "\055\143\157\154\157\162\055\163\143\150\145\155\145\072\040\144"
  "\141\162\153\051\040\173\012\040\040\167\151\156\144\157\167\056"
  "\142\141\143\153\147\162\157\165\156\144\056\143\157\156\164\162"
  "\157\154\055\162\157\157\164\040\173\012\040\040\040\040\142\141"
  "\143\153\147\162\157\165\156\144\055\143\157\154\157\162\072\040"
  "\100\164\171\160\151\157\137\143\141\156\166\141\163\137\144\141"
  "\162\153\073\012\040\040\175\012\012\040\040\056\143\157\156\164"
  "\162\157\154\055\163\150\145\154\154\040\173\012\040\040\040\040"
  "\142\141\143\153\147\162\157\165\156\144\055\143\157\154\157\162"
  "\072\040\100\164\171\160\151\157\137\143\141\156\166\141\163\137"
  "\144\141\162\153\073\012\040\040\175\012\175\012\012\056\143\157"
  "\156\164\162\157\154\055\150\145\141\144\145\162\142\141\162\040"
  "\173\012\040\040\142\157\170\055\163\150\141\144\157\167\072\040"
  "\156\157\156\145\073\012\175\012\012\056\166\151\145\167\055\163"
  "\167\151\164\143\150\145\162\040\142\165\164\164\157\156\040\173"
  "\012\040\040\155\151\156\055\150\145\151\147\150\164\072\040\063"
  "\060\160\170\073\012\040\040\160\141\144\144\151\156\147\072\040"
  "\060\040\061\062\160\170\073\012\175\012\012\056\160\141\147\145"
  "\055\163\150\145\154\154\040\173\012\040\040\160\141\144\144\151"
  "\156\147\072\040\061\070\160\170\073\012\175\012\012\056\163\145"
  "\143\164\151\157\156\040\173\012\040\040\155\141\162\147\151\156"
  "\055\164\157\160\072\040\064\160\170\073\012\175\012\012\056\163"
  "\145\143\164\151\157\156\055\164\151\164\154\145\040\173\012\040"
  "\040\146\157\156\164\055\163\151\172\145\072\040\061\056\060\062"
  "\162\145\155\073\012\040\040\146\157\156\164\055\167\145\151\147"
  "\150\164\072\040\067\060\060\073\012\175\012\012\056\163\145\143"
  "\164\151\157\156\055\144\145\163\143\162\151\160\164\151\157\156"
  "\054\012\056\160\162\145\146\145\162\145\156\143\145\055\144\145"
  "\163\143\162\151\160\164\151\157\156\054\012\056\145\155\160\164"
  "\171\055\156\157\164\145\054\012\056\151\156\154\151\156\145\055"
  "\163\164\141\164\165\163\054\012\056\155\165\164\145\144\055\154"
  "\141\142\145\154\040\173\012\040\040\143\157\154\157\162\072\040"
  "\141\154\160\150\141\050\143\165\162\162\145\156\164\103\157\154"
  "\157\162\054\040\060\056\067\062\051\073\012\175\012\012\056\160"
  "\141\156\145\154\040\173\012\040\040\160\141\144\144\151\156\147"
  "\072\040\061\062\160\170\073\012\040\040\142\157\162\144\145\162"
  "\055\162\141\144\151\165\163\072\040\061\064\160\170\073\012\040"
  "\040\142\141\143\153\147\162\157\165\156\144\055\143\157\154\157"
  "\162\072\040\141\154\160\150\141\050\100\143\141\162\144\137\142"
  "\147\137\143\157\154\157\162\054\040\060\056\070\070\051\073\012"
  "\175\012\012\056\160\162\145\146\145\162\145\156\143\145\163\040"
  "\173\012\040\040\142\141\143\153\147\162\157\165\156\144\072\040"
  "\164\162\141\156\163\160\141\162\145\156\164\073\012\175\012\012"
  "\056\160\162\145\146\145\162\145\156\143\145\163\040\162\157\167"
  "\040\173\012\040\040\142\157\162\144\145\162\055\162\141\144\151"
  "\165\163\072\040\060\073\012\040\040\155\141\162\147\151\156\072"
  "\040\060\073\012\175\012\012\056\160\162\145\146\145\162\145\156"
  "\143\145\055\162\157\167\040\173\012\040\040\160\141\144\144\151"
  "\156\147\072\040\061\062\160\170\040\061\064\160\170\073\012\040"
  "\040\142\157\162\144\145\162\055\162\141\144\151\165\163\072\040"
  "\060\073\012\175\012\012\056\160\162\145\146\145\162\145\156\143"
  "\145\163\040\162\157\167\072\156\157\164\050\072\154\141\163\164"
  "\055\143\150\151\154\144\051\040\056\160\162\145\146\145\162\145"
  "\156\143\145\055\162\157\167\040\173\012\040\040\142\157\162\144"
  "\145\162\055\142\157\164\164\157\155\072\040\061\160\170\040\163"
  "\157\154\151\144\040\141\154\160\150\141\050\143\165\162\162\145"
  "\156\164\103\157\154\157\162\054\040\060\056\060\070\051\073\012"
  "\175\012\012\056\160\162\145\146\145\162\145\156\143\145\055\164"
  "\151\164\154\145\040\173\012\040\040\146\157\156\164\055\167\145"
  "\151\147\150\164\072\040\066\060\060\073\012\175\012\012\056\145"
  "\156\147\151\156\145\055\143\157\156\146\151\147\040\173\012\040"
  "\040\155\141\162\147\151\156\055\164\157\160\072\040\061\060\160"
  "\170\073\012\175\012\012\056\146\157\157\164\145\162\055\142\141"
  "\162\040\173\012\040\040\160\141\144\144\151\156\147\072\040\061"
  "\062\160\170\040\061\070\160\170\040\061\070\160\170\040\061\070"
  "\160\170\073\012\175\012\012\056\143\157\156\164\162\157\154\055"
  "\142\165\164\164\157\156\040\173\012\040\040\160\141\144\144\151"
  "\156\147\072\040\060\040\061\064\160\170\073\012\175\012\012\056"
  "\143\157\156\164\162\157\154\055\146\151\145\154\144\054\012\056"
  "\143\157\156\164\162\157\154\055\163\160\151\156\054\012\056\143"
  "\157\156\164\162\157\154\055\145\156\164\162\171\040\173\012\040"
  "\040\160\141\144\144\151\156\147\055\164\157\160\072\040\060\073"
  "\012\040\040\160\141\144\144\151\156\147\055\142\157\164\164\157"
  "\155\072\040\060\073\012\175\012\000\000\050\165\165\141\171\051"
  "" };

static GStaticResource static_resource = { typio_control_resource_data.data, sizeof (typio_control_resource_data.data) - 1 /* nul terminator */, NULL, NULL, NULL };

G_MODULE_EXPORT
GResource *typio_control_get_resource (void);
GResource *typio_control_get_resource (void)
{
  return g_static_resource_get_resource (&static_resource);
}
/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

#ifndef __G_CONSTRUCTOR_H__
#define __G_CONSTRUCTOR_H__

/*
  If G_HAS_CONSTRUCTORS is true then the compiler support *both* constructors and
  destructors, in a usable way, including e.g. on library unload. If not you're on
  your own.

  Some compilers need #pragma to handle this, which does not work with macros,
  so the way you need to use this is (for constructors):

  #ifdef G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA
  #pragma G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(my_constructor)
  #endif
  G_DEFINE_CONSTRUCTOR(my_constructor)
  static void my_constructor(void) {
   ...
  }

*/

#ifndef __GTK_DOC_IGNORE__

#if  __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)

#define G_HAS_CONSTRUCTORS 1

#define G_DEFINE_CONSTRUCTOR(_func) static void __attribute__((constructor)) _func (void);
#define G_DEFINE_DESTRUCTOR(_func) static void __attribute__((destructor)) _func (void);

#elif defined (_MSC_VER)

/*
 * Only try to include gslist.h if not already included via glib.h,
 * so that items using gconstructor.h outside of GLib (such as
 * GResources) continue to build properly.
 */
#ifndef __G_LIB_H__
#include "gslist.h"
#endif

#include <stdlib.h>

#define G_HAS_CONSTRUCTORS 1

/* We do some weird things to avoid the constructors being optimized
 * away on VS2015 if WholeProgramOptimization is enabled. First we
 * make a reference to the array from the wrapper to make sure its
 * references. Then we use a pragma to make sure the wrapper function
 * symbol is always included at the link stage. Also, the symbols
 * need to be extern (but not dllexport), even though they are not
 * really used from another object file.
 */

/* We need to account for differences between the mangling of symbols
 * for x86 and x64/ARM/ARM64 programs, as symbols on x86 are prefixed
 * with an underscore but symbols on x64/ARM/ARM64 are not.
 */
#ifdef _M_IX86
#define G_MSVC_SYMBOL_PREFIX "_"
#else
#define G_MSVC_SYMBOL_PREFIX ""
#endif

#define G_DEFINE_CONSTRUCTOR(_func) G_MSVC_CTOR (_func, G_MSVC_SYMBOL_PREFIX)
#define G_DEFINE_DESTRUCTOR(_func) G_MSVC_DTOR (_func, G_MSVC_SYMBOL_PREFIX)

#define G_MSVC_CTOR(_func,_sym_prefix) \
  static void _func(void); \
  extern int (* _array ## _func)(void);              \
  int _func ## _wrapper(void);              \
  int _func ## _wrapper(void) { _func(); g_slist_find (NULL,  _array ## _func); return 0; } \
  __pragma(comment(linker,"/include:" _sym_prefix # _func "_wrapper")) \
  __pragma(section(".CRT$XCU",read)) \
  __declspec(allocate(".CRT$XCU")) int (* _array ## _func)(void) = _func ## _wrapper;

#define G_MSVC_DTOR(_func,_sym_prefix) \
  static void _func(void); \
  extern int (* _array ## _func)(void);              \
  int _func ## _constructor(void);              \
  int _func ## _constructor(void) { atexit (_func); g_slist_find (NULL,  _array ## _func); return 0; } \
   __pragma(comment(linker,"/include:" _sym_prefix # _func "_constructor")) \
  __pragma(section(".CRT$XCU",read)) \
  __declspec(allocate(".CRT$XCU")) int (* _array ## _func)(void) = _func ## _constructor;

#elif defined(__SUNPRO_C)

/* This is not tested, but i believe it should work, based on:
 * http://opensource.apple.com/source/OpenSSL098/OpenSSL098-35/src/fips/fips_premain.c
 */

#define G_HAS_CONSTRUCTORS 1

#define G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA 1
#define G_DEFINE_DESTRUCTOR_NEEDS_PRAGMA 1

#define G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(_func) \
  init(_func)
#define G_DEFINE_CONSTRUCTOR(_func) \
  static void _func(void);

#define G_DEFINE_DESTRUCTOR_PRAGMA_ARGS(_func) \
  fini(_func)
#define G_DEFINE_DESTRUCTOR(_func) \
  static void _func(void);

#else

/* constructors not supported for this compiler */

#endif

#endif /* __GTK_DOC_IGNORE__ */
#endif /* __G_CONSTRUCTOR_H__ */

#ifdef G_HAS_CONSTRUCTORS

#ifdef G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA
#pragma G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(typio_controlresource_constructor)
#endif
G_DEFINE_CONSTRUCTOR(typio_controlresource_constructor)
#ifdef G_DEFINE_DESTRUCTOR_NEEDS_PRAGMA
#pragma G_DEFINE_DESTRUCTOR_PRAGMA_ARGS(typio_controlresource_destructor)
#endif
G_DEFINE_DESTRUCTOR(typio_controlresource_destructor)

#else
#warning "Constructor not supported on this compiler, linking in resources will not work"
#endif

static void typio_controlresource_constructor (void)
{
  g_static_resource_init (&static_resource);
}

static void typio_controlresource_destructor (void)
{
  g_static_resource_fini (&static_resource);
}
