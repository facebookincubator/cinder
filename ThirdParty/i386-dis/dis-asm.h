/* Interface between the opcode library and its callers.

   Copyright (C) 1999-2019 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.

   Written by Cygnus Support, 1993.

   The opcode library (libopcodes.a) provides instruction decoders for
   a large variety of instruction sets, callable with an identical
   interface, for making instruction-processing programs more independent
   of the instruction set being processed.  */

#ifndef DIS_ASM_H
#define DIS_ASM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>

#define MAX_MNEM_SIZE 20
#define MAX_OPERANDS 5

typedef unsigned long vma_t;
typedef long bfd_signed_vma;
typedef unsigned char bfd_byte;
typedef int bfd_boolean;

typedef int (*fprintf_ftype) (void *, const char*, ...);

#define OPCODES_SIGJMP_BUF      jmp_buf
#define OPCODES_SIGSETJMP(buf)      setjmp(buf)
#define OPCODES_SIGLONGJMP(buf,val) longjmp((buf), (val))

#define STRING_COMMA_LEN(STR) (STR), (sizeof (STR) - 1)
#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

#define _(STRING) (STRING)

#define BFD_VMA_FMT "l"

#ifndef fprintf_vma
#define sprintf_vma(s,x) sprintf (s, "%016" BFD_VMA_FMT "x", x)
#define fprintf_vma(f,x) fprintf (f, "%016" BFD_VMA_FMT "x", x)
#endif

enum dis_insn_type
{
  dis_noninsn,			/* Not a valid instruction.  */
  dis_nonbranch,		/* Not a branch instruction.  */
  dis_branch,			/* Unconditional branch.  */
  dis_condbranch,		/* Conditional branch.  */
  dis_jsr,			/* Jump to subroutine.  */
  dis_condjsr,			/* Conditional jump to subroutine.  */
  dis_dref,			/* Data reference instruction.  */
  dis_dref2			/* Two data references in instruction.  */
};

/* This struct is passed into the instruction decoding routine,
   and is passed back out into each callback.  The various fields are used
   for conveying information from your main routine into your callbacks,
   for passing information into the instruction decoders (such as the
   addresses of the callback functions), or for passing information
   back from the instruction decoders to their callers.

   It must be initialized before it is first passed; this can be done
   by hand, or using one of the initialization macros below.  */

typedef struct disassemble_info
{
  fprintf_ftype fprintf_func;
  void *stream;
  // void *application_data;

  // void *insn_sets;

  /* For use by the disassembler.
     The top 16 bits are reserved for public use (and are documented here).
     The bottom 16 bits are for the internal use of the disassembler.  */
  // unsigned long flags;
  /* Set if the disassembler has determined that there are one or more
     relocations associated with the instruction being disassembled.  */
#define INSN_HAS_RELOC	 (1 << 31)
  /* Set if the user has requested the disassembly of data as well as code.  */
#define DISASSEMBLE_DATA (1 << 30)
  /* Set if the user has specifically set the machine type encoded in the
     mach field of this structure.  */
#define USER_SPECIFIED_MACHINE_TYPE (1 << 29)

  /* Use internally by the target specific disassembly code.  */
  void *private_data;

  /* Function used to get bytes to disassemble.  MEMADDR is the
     address of the stuff to be disassembled, MYADDR is the address to
     put the bytes in, and LENGTH is the number of bytes to read.
     INFO is a pointer to this struct.
     Returns an errno value or 0 for success.  */
  int (*read_memory_func)
    (vma_t memaddr, bfd_byte *myaddr, unsigned int length,
     struct disassemble_info *dinfo);

  /* Function which should be called if we get an error that we can't
     recover from.  STATUS is the errno value from read_memory_func and
     MEMADDR is the address that we were trying to read.  INFO is a
     pointer to this struct.  */
  void (*memory_error_func)
    (int status, vma_t memaddr, struct disassemble_info *dinfo);

  /* Function called to print ADDR.  */
  void (*print_address_func)
    (vma_t addr, struct disassemble_info *dinfo);

  /* Function called to determine if there is a symbol at the given ADDR.
     If there is, the function returns 1, otherwise it returns 0.
     This is used by ports which support an overlay manager where
     the overlay number is held in the top part of an address.  In
     some circumstances we want to include the overlay number in the
     address, (normally because there is a symbol associated with
     that address), but sometimes we want to mask out the overlay bits.  */
  int (* symbol_at_address_func)
    (vma_t addr, struct disassemble_info *dinfo);

  /* These are for buffer_read_memory.  */
  bfd_byte *buffer;
  vma_t buffer_vma;
  size_t buffer_length;

  /* This variable may be set by the instruction decoder.  It suggests
      the number of bytes objdump should display on a single line.  If
      the instruction decoder sets this, it should always set it to
      the same value in order to get reasonable looking output.  */
  int bytes_per_line;

  /* The next two variables control the way objdump displays the raw data.  */
  /* For example, if bytes_per_line is 8 and bytes_per_chunk is 4, the */
  /* output will look like this:
     00:   00000000 00000000
     with the chunks displayed according to "display_endian". */
  int bytes_per_chunk;
  // enum bfd_endian display_endian;

  /* Number of octets per incremented target address
     Normally one, but some DSPs have byte sizes of 16 or 32 bits.  */
  unsigned int octets_per_byte;

  /* The number of zeroes we want to see at the end of a section before we
     start skipping them.  */
  unsigned int skip_zeroes;

  /* The number of zeroes to skip at the end of a section.  If the number
     of zeroes at the end is between SKIP_ZEROES_AT_END and SKIP_ZEROES,
     they will be disassembled.  If there are fewer than
     SKIP_ZEROES_AT_END, they will be skipped.  This is a heuristic
     attempt to avoid disassembling zeroes inserted by section
     alignment.  */
  unsigned int skip_zeroes_at_end;

  /* Whether the disassembler always needs the relocations.  */
  bfd_boolean disassembler_needs_relocs;

  /* Results from instruction decoders.  Not all decoders yet support
     this information.  This info is set each time an instruction is
     decoded, and is only valid for the last such instruction.

     To determine whether this decoder supports this information, set
     insn_info_valid to 0, decode an instruction, then check it.  */

  char insn_info_valid;		/* Branch info has been set. */
  char branch_delay_insns;	/* How many sequential insn's will run before
				   a branch takes effect.  (0 = normal) */
  char data_size;		/* Size of data reference in insn, in bytes */
  enum dis_insn_type insn_type;	/* Type of instruction */
  vma_t target;		/* Target address of branch or dref, if known;
				   zero if unknown.  */
  vma_t target2;		/* Second target address for dref2 */

  /* Command line options specific to the target disassembler.  */
  const char *disassembler_options;

  /* If non-zero then try not disassemble beyond this address, even if
     there are values left in the buffer.  This address is the address
     of the nearest symbol forwards from the start of the disassembly,
     and it is assumed that it lies on the boundary between instructions.
     If an instruction spans this address then this is an error in the
     file being disassembled.  */
  vma_t stop_vma;

} disassemble_info;

/* This block of definitions is for particular callers who read instructions
   into a buffer before calling the instruction decoder.  */

/* Here is a function which callers may wish to use for read_memory_func.
   It gets bytes from a buffer.  */
extern int buffer_read_memory
  (vma_t, bfd_byte *, unsigned int, struct disassemble_info *);

/* This function goes with buffer_read_memory.
   It prints a message using info->fprintf_func and info->stream.  */
extern void perror_memory (int, vma_t, struct disassemble_info *);


/* Just print the address in hex.  This is included for completeness even
   though both GDB and objdump provide their own (to print symbolic
   addresses).  */
extern void generic_print_address
  (vma_t, struct disassemble_info *);

int print_insn (vma_t pc, disassemble_info *info);

#ifdef __cplusplus
}
#endif

#endif /* ! defined (DIS_ASM_H) */
