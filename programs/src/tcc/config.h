/*
    * config.h
    * TCC configuration for MontaukOS
    * Copyright (c) 2026 Daniel Hammer
*/

#ifndef TCC_CONFIG_H
#define TCC_CONFIG_H

#define TCC_VERSION "0.9.28"
#define TCC_TARGET_X86_64 1
#define CONFIG_TCC_STATIC 1

/* No backtrace, bcheck, or run support on MontaukOS */
#define CONFIG_TCC_BACKTRACE 0
#undef CONFIG_TCC_BCHECK

/* Paths on MontaukOS ramdisk */
#define CONFIG_TCCDIR "0:/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "0:/lib/tcc/include"
#define CONFIG_TCC_LIBPATHS "0:/lib/tcc/lib"
#define CONFIG_TCC_CRTPREFIX "0:/lib/tcc/lib"
#define CONFIG_TCC_ELFINTERP ""

/* We are not native -- no -run support */
#undef TCC_IS_NATIVE

/* Disable unused features */
#define CONFIG_TCC_PREDEFS 0
#define CONFIG_TCC_SEMLOCK 0

/* No libtcc1 runtime library needed */
#define TCC_LIBTCC1 ""

/* No libgcc needed */
#undef TCC_LIBGCC

/* MontaukOS uses ELF, not PE or Mach-O */
#undef TCC_TARGET_PE
#undef TCC_TARGET_MACHO

#endif /* TCC_CONFIG_H */
