#include <signal.h>

#if WITH_COMMON
#include "sim-main.h"
#include "sim-options.h"
#else
#include "mn10300_sim.h"
#endif

#include "sysdep.h"
#include "bfd.h"
#include "sim-assert.h"


#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif

#include "bfd.h"

#ifndef INLINE
#ifdef __GNUC__
#define INLINE inline
#else
#define INLINE
#endif
#endif


host_callback *mn10300_callback;
int mn10300_debug;

#if WITH_COMMON
#else
static void dispatch PARAMS ((uint32, uint32, int));
static long hash PARAMS ((long));
static void init_system PARAMS ((void));

static SIM_OPEN_KIND sim_kind;
static char *myname;
#define MAX_HASH  127

struct hash_entry
{
  struct hash_entry *next;
  long opcode;
  long mask;
  struct simops *ops;
#ifdef HASH_STAT
  unsigned long count;
#endif
};

static int max_mem = 0;
struct hash_entry hash_table[MAX_HASH+1];


/* This probably doesn't do a very good job at bucket filling, but
   it's simple... */
static INLINE long 
hash(insn)
     long insn;
{
  /* These are one byte insns, we special case these since, in theory,
     they should be the most heavily used.  */
  if ((insn & 0xffffff00) == 0)
    {
      switch (insn & 0xf0)
	{
	  case 0x00:
	    return 0x70;

	  case 0x40:
	    return 0x71;

	  case 0x10:
	    return 0x72;

	  case 0x30:
	    return 0x73;

	  case 0x50:
	    return 0x74;

	  case 0x60:
	    return 0x75;

	  case 0x70:
	    return 0x76;

	  case 0x80:
	    return 0x77;

	  case 0x90:
	    return 0x78;

	  case 0xa0:
	    return 0x79;

	  case 0xb0:
	    return 0x7a;

	  case 0xe0:
	    return 0x7b;

	  default:
	    return 0x7c;
	}
    }

  /* These are two byte insns */
  if ((insn & 0xffff0000) == 0)
    {
      if ((insn & 0xf000) == 0x2000
	  || (insn & 0xf000) == 0x5000)
	return ((insn & 0xfc00) >> 8) & 0x7f;

      if ((insn & 0xf000) == 0x4000)
	return ((insn & 0xf300) >> 8) & 0x7f;

      if ((insn & 0xf000) == 0x8000
	  || (insn & 0xf000) == 0x9000
	  || (insn & 0xf000) == 0xa000
	  || (insn & 0xf000) == 0xb000)
	return ((insn & 0xf000) >> 8) & 0x7f;

      if ((insn & 0xff00) == 0xf000
	  || (insn & 0xff00) == 0xf100
	  || (insn & 0xff00) == 0xf200
	  || (insn & 0xff00) == 0xf500
	  || (insn & 0xff00) == 0xf600)
	return ((insn & 0xfff0) >> 4) & 0x7f;
 
      if ((insn & 0xf000) == 0xc000)
	return ((insn & 0xff00) >> 8) & 0x7f;

      return ((insn & 0xffc0) >> 6) & 0x7f;
    }

  /* These are three byte insns.  */
  if ((insn & 0xff000000) == 0)
    {
      if ((insn & 0xf00000) == 0x000000)
	return ((insn & 0xf30000) >> 16) & 0x7f;

      if ((insn & 0xf00000) == 0x200000
	  || (insn & 0xf00000) == 0x300000)
	return ((insn & 0xfc0000) >> 16) & 0x7f;

      if ((insn & 0xff0000) == 0xf80000)
	return ((insn & 0xfff000) >> 12) & 0x7f;

      if ((insn & 0xff0000) == 0xf90000)
	return ((insn & 0xfffc00) >> 10) & 0x7f;

      return ((insn & 0xff0000) >> 16) & 0x7f;
    }

  /* These are four byte or larger insns.  */
  if ((insn & 0xf0000000) == 0xf0000000)
    return ((insn & 0xfff00000) >> 20) & 0x7f;

  return ((insn & 0xff000000) >> 24) & 0x7f;
}

static INLINE void
dispatch (insn, extension, length)
     uint32 insn;
     uint32 extension;
     int length;
{
  struct hash_entry *h;

  h = &hash_table[hash(insn)];

  while ((insn & h->mask) != h->opcode
	  || (length != h->ops->length))
    {
      if (!h->next)
	{
	  (*mn10300_callback->printf_filtered) (mn10300_callback,
	    "ERROR looking up hash for 0x%x, PC=0x%x\n", insn, PC);
	  exit(1);
	}
      h = h->next;
    }


#ifdef HASH_STAT
  h->count++;
#endif

  /* Now call the right function.  */
  (h->ops->func)(insn, extension);
  PC += length;
}

void
sim_size (power)
     int power;

{
  if (State.mem)
    free (State.mem);

  max_mem = 1 << power;
  State.mem = (uint8 *) calloc (1,  1 << power);
  if (!State.mem)
    {
      (*mn10300_callback->printf_filtered) (mn10300_callback, "Allocation of main memory failed.\n");
      exit (1);
    }
}

static void
init_system ()
{
  if (!State.mem)
    sim_size(19);
}

int
sim_write (sd, addr, buffer, size)
     SIM_DESC sd;
     SIM_ADDR addr;
     unsigned char *buffer;
     int size;
{
  int i;

  init_system ();

  for (i = 0; i < size; i++)
    store_byte (addr + i, buffer[i]);

  return size;
}

/* Compare two opcode table entries for qsort.  */
static int
compare_simops (arg1, arg2)
     const PTR arg1;
     const PTR arg2;
{
  unsigned long code1 = ((struct simops *)arg1)->opcode;
  unsigned long code2 = ((struct simops *)arg2)->opcode;

  if (code1 < code2)
    return -1;
  if (code2 < code1)
    return 1;
  return 0;
}


SIM_DESC
sim_open (kind, cb, abfd, argv)
     SIM_OPEN_KIND kind;
     host_callback *cb;
     struct _bfd *abfd;
     char **argv;
{
  struct simops *s;
  struct hash_entry *h;
  char **p;
  int i;

  mn10300_callback = cb;

  /* Sort the opcode array from smallest opcode to largest.
     This will generally improve simulator performance as the smaller
     opcodes are generally preferred to the larger opcodes.  */
  for (i = 0, s = Simops; s->func; s++, i++)
    ;
  qsort (Simops, i, sizeof (Simops[0]), compare_simops);

  sim_kind = kind;
  myname = argv[0];

  for (p = argv + 1; *p; ++p)
    {
      if (strcmp (*p, "-E") == 0)
	++p; /* ignore endian spec */
      else
#ifdef DEBUG
      if (strcmp (*p, "-t") == 0)
	mn10300_debug = DEBUG;
      else
#endif
	(*mn10300_callback->printf_filtered) (mn10300_callback, "ERROR: unsupported option(s): %s\n",*p);
    }

 /* put all the opcodes in the hash table */
  for (s = Simops; s->func; s++)
    {
      h = &hash_table[hash(s->opcode)];

      /* go to the last entry in the chain */
      while (h->next)
	{
	  /* Don't insert the same opcode more than once.  */
	  if (h->opcode == s->opcode
	      && h->mask == s->mask
	      && h->ops == s)
	    break;
	  else
	    h = h->next;
	}

      /* Don't insert the same opcode more than once.  */
      if (h->opcode == s->opcode
	  && h->mask == s->mask
	  && h->ops == s)
	continue;

      if (h->ops)
	{
	  h->next = calloc(1,sizeof(struct hash_entry));
	  h = h->next;
	}
      h->ops = s;
      h->mask = s->mask;
      h->opcode = s->opcode;
#if HASH_STAT
      h->count = 0;
#endif
    }


  /* fudge our descriptor for now */
  return (SIM_DESC) 1;
}


void
sim_close (sd, quitting)
     SIM_DESC sd;
     int quitting;
{
  /* nothing to do */
}

void
sim_set_profile (n)
     int n;
{
  (*mn10300_callback->printf_filtered) (mn10300_callback, "sim_set_profile %d\n", n);
}

void
sim_set_profile_size (n)
     int n;
{
  (*mn10300_callback->printf_filtered) (mn10300_callback, "sim_set_profile_size %d\n", n);
}

int
sim_stop (sd)
     SIM_DESC sd;
{
  return 0;
}

void
sim_resume (sd, step, siggnal)
     SIM_DESC sd;
     int step, siggnal;
{
  uint32 inst;
  reg_t oldpc;
  struct hash_entry *h;

  if (step)
    State.exception = SIGTRAP;
  else
    State.exception = 0;

  State.exited = 0;

  do
    {
      unsigned long insn, extension;

      /* Fetch the current instruction.  */
      inst = load_mem_big (PC, 2);
      oldpc = PC;

      /* Using a giant case statement may seem like a waste because of the
 	code/rodata size the table itself will consume.  However, using
 	a giant case statement speeds up the simulator by 10-15% by avoiding
 	cascading if/else statements or cascading case statements.  */

      switch ((inst >> 8) & 0xff)
	{
	  /* All the single byte insns except 0x80, 0x90, 0xa0, 0xb0
	     which must be handled specially.  */
	  case 0x00:
	  case 0x04:
	  case 0x08:
	  case 0x0c:
	  case 0x10:
	  case 0x11:
	  case 0x12:
	  case 0x13:
	  case 0x14:
	  case 0x15:
	  case 0x16:
	  case 0x17:
	  case 0x18:
	  case 0x19:
	  case 0x1a:
	  case 0x1b:
	  case 0x1c:
	  case 0x1d:
	  case 0x1e:
	  case 0x1f:
	  case 0x3c:
	  case 0x3d:
	  case 0x3e:
	  case 0x3f:
	  case 0x40:
	  case 0x41:
	  case 0x44:
	  case 0x45:
	  case 0x48:
	  case 0x49:
	  case 0x4c:
	  case 0x4d:
	  case 0x50:
	  case 0x51:
	  case 0x52:
	  case 0x53:
	  case 0x54:
	  case 0x55:
	  case 0x56:
	  case 0x57:
	  case 0x60:
	  case 0x61:
	  case 0x62:
	  case 0x63:
	  case 0x64:
	  case 0x65:
	  case 0x66:
	  case 0x67:
	  case 0x68:
	  case 0x69:
	  case 0x6a:
	  case 0x6b:
	  case 0x6c:
	  case 0x6d:
	  case 0x6e:
	  case 0x6f:
	  case 0x70:
	  case 0x71:
	  case 0x72:
	  case 0x73:
	  case 0x74:
	  case 0x75:
	  case 0x76:
	  case 0x77:
	  case 0x78:
	  case 0x79:
	  case 0x7a:
	  case 0x7b:
	  case 0x7c:
	  case 0x7d:
	  case 0x7e:
	  case 0x7f:
	  case 0xcb:
	  case 0xd0:
	  case 0xd1:
	  case 0xd2:
	  case 0xd3:
	  case 0xd4:
	  case 0xd5:
	  case 0xd6:
	  case 0xd7:
	  case 0xd8:
	  case 0xd9:
	  case 0xda:
	  case 0xdb:
	  case 0xe0:
	  case 0xe1:
	  case 0xe2:
	  case 0xe3:
	  case 0xe4:
	  case 0xe5:
	  case 0xe6:
	  case 0xe7:
	  case 0xe8:
	  case 0xe9:
	  case 0xea:
	  case 0xeb:
	  case 0xec:
	  case 0xed:
	  case 0xee:
	  case 0xef:
	  case 0xff:
	    insn = (inst >> 8) & 0xff;
	    extension = 0;
	    dispatch (insn, extension, 1);
	    break;

	  /* Special cases where dm == dn is used to encode a different
	     instruction.  */
	  case 0x80:
	  case 0x85:
	  case 0x8a:
	  case 0x8f:
	  case 0x90:
	  case 0x95:
	  case 0x9a:
	  case 0x9f:
	  case 0xa0:
	  case 0xa5:
	  case 0xaa:
	  case 0xaf:
	  case 0xb0:
	  case 0xb5:
	  case 0xba:
	  case 0xbf:
	    insn = inst;
	    extension = 0;
	    dispatch (insn, extension, 2);
	    break;

	  case 0x81:
	  case 0x82:
	  case 0x83:
	  case 0x84:
	  case 0x86:
	  case 0x87:
	  case 0x88:
	  case 0x89:
	  case 0x8b:
	  case 0x8c:
	  case 0x8d:
	  case 0x8e:
	  case 0x91:
	  case 0x92:
	  case 0x93:
	  case 0x94:
	  case 0x96:
	  case 0x97:
	  case 0x98:
	  case 0x99:
	  case 0x9b:
	  case 0x9c:
	  case 0x9d:
	  case 0x9e:
	  case 0xa1:
	  case 0xa2:
	  case 0xa3:
	  case 0xa4:
	  case 0xa6:
	  case 0xa7:
	  case 0xa8:
	  case 0xa9:
	  case 0xab:
	  case 0xac:
	  case 0xad:
	  case 0xae:
	  case 0xb1:
	  case 0xb2:
	  case 0xb3:
	  case 0xb4:
	  case 0xb6:
	  case 0xb7:
	  case 0xb8:
	  case 0xb9:
	  case 0xbb:
	  case 0xbc:
	  case 0xbd:
	  case 0xbe:
	    insn = (inst >> 8) & 0xff;
	    extension = 0;
	  dispatch (insn, extension, 1);
	  break;

	  /* The two byte instructions.  */
	  case 0x20:
	  case 0x21:
	  case 0x22:
	  case 0x23:
	  case 0x28:
	  case 0x29:
	  case 0x2a:
	  case 0x2b:
	  case 0x42:
	  case 0x43:
	  case 0x46:
	  case 0x47:
	  case 0x4a:
	  case 0x4b:
	  case 0x4e:
	  case 0x4f:
	  case 0x58:
	  case 0x59:
	  case 0x5a:
	  case 0x5b:
	  case 0x5c:
	  case 0x5d:
	  case 0x5e:
	  case 0x5f:
	  case 0xc0:
	  case 0xc1:
	  case 0xc2:
	  case 0xc3:
	  case 0xc4:
	  case 0xc5:
	  case 0xc6:
	  case 0xc7:
	  case 0xc8:
	  case 0xc9:
	  case 0xca:
	  case 0xce:
	  case 0xcf:
	  case 0xf0:
	  case 0xf1:
	  case 0xf2:
	  case 0xf3:
	  case 0xf4:
	  case 0xf5:
	  case 0xf6:
	    insn = inst;
	    extension = 0;
	    dispatch (insn, extension, 2);
	    break;

	  /* The three byte insns with a 16bit operand in little endian
	     format.  */
	  case 0x01:
	  case 0x02:
	  case 0x03:
	  case 0x05:
	  case 0x06:
	  case 0x07:
	  case 0x09:
	  case 0x0a:
	  case 0x0b:
	  case 0x0d:
	  case 0x0e:
	  case 0x0f:
	  case 0x24:
	  case 0x25:
	  case 0x26:
	  case 0x27:
	  case 0x2c:
	  case 0x2d:
	  case 0x2e:
	  case 0x2f:
	  case 0x30:
	  case 0x31:
	  case 0x32:
	  case 0x33:
	  case 0x34:
	  case 0x35:
	  case 0x36:
	  case 0x37:
	  case 0x38:
	  case 0x39:
	  case 0x3a:
	  case 0x3b:
	  case 0xcc:
	    insn = load_byte (PC);
	    insn <<= 16;
	    insn |= load_half (PC + 1);
	    extension = 0;
	    dispatch (insn, extension, 3);
	    break;

	  /* The three byte insns without 16bit operand.  */
	  case 0xde:
	  case 0xdf:
	  case 0xf8:
	  case 0xf9:
	    insn = load_mem_big (PC, 3);
	    extension = 0;
	    dispatch (insn, extension, 3);
	    break;
	  
	  /* Four byte insns.  */
	  case 0xfa:
	  case 0xfb:
	    if ((inst & 0xfffc) == 0xfaf0
		|| (inst & 0xfffc) == 0xfaf4
		|| (inst & 0xfffc) == 0xfaf8)
	      insn = load_mem_big (PC, 4);
	    else
	      {
		insn = inst;
		insn <<= 16;
		insn |= load_half (PC + 2);
		extension = 0;
	      }
	    dispatch (insn, extension, 4);
	    break;

	  /* Five byte insns.  */
	  case 0xcd:
	    insn = load_byte (PC);
	    insn <<= 24;
	    insn |= (load_half (PC + 1) << 8);
	    insn |= load_byte (PC + 3);
	    extension = load_byte (PC + 4);
	    dispatch (insn, extension, 5);
	    break;

	  case 0xdc:
	    insn = load_byte (PC);
	    insn <<= 24;
	    extension = load_word (PC + 1);
	    insn |= (extension & 0xffffff00) >> 8;
	    extension &= 0xff;
	    dispatch (insn, extension, 5);
	    break;
	
	  /* Six byte insns.  */
	  case 0xfc:
	  case 0xfd:
	    insn = (inst << 16);
	    extension = load_word (PC + 2);
	    insn |= ((extension & 0xffff0000) >> 16);
	    extension &= 0xffff;
	    dispatch (insn, extension, 6);
	    break;
	    
	  case 0xdd:
	    insn = load_byte (PC) << 24;
	    extension = load_word (PC + 1);
	    insn |= ((extension >> 8) & 0xffffff);
	    extension = (extension & 0xff) << 16;
	    extension |= load_byte (PC + 5) << 8;
	    extension |= load_byte (PC + 6);
	    dispatch (insn, extension, 7);
	    break;

	  case 0xfe:
	    insn = inst << 16;
	    extension = load_word (PC + 2);
	    insn |= ((extension >> 16) & 0xffff);
	    extension <<= 8;
	    extension &= 0xffff00;
	    extension |= load_byte (PC + 6);
	    dispatch (insn, extension, 7);
	    break;

	  default:
	    abort ();
	}
    }
  while (!State.exception);

#ifdef HASH_STAT
  {
    int i;
    for (i = 0; i < MAX_HASH; i++)
      {
	 struct hash_entry *h;
	 h = &hash_table[i];

	 printf("hash 0x%x:\n", i);

	 while (h)
	   {
	     printf("h->opcode = 0x%x, count = 0x%x\n", h->opcode, h->count);
	     h = h->next;
	   }

	 printf("\n\n");
      }
    fflush (stdout);
  }
#endif

}

int
sim_trace (sd)
     SIM_DESC sd;
{
#ifdef DEBUG
  mn10300_debug = DEBUG;
#endif
  sim_resume (sd, 0, 0);
  return 1;
}

void
sim_info (sd, verbose)
     SIM_DESC sd;
     int verbose;
{
  (*mn10300_callback->printf_filtered) (mn10300_callback, "sim_info\n");
}

SIM_RC
sim_create_inferior (sd, abfd, argv, env)
     SIM_DESC sd;
     struct _bfd *abfd;
     char **argv;
     char **env;
{
  if (abfd != NULL)
    PC = bfd_get_start_address (abfd);
  else
    PC = 0;
  return SIM_RC_OK;
}

void
sim_set_callbacks (p)
     host_callback *p;
{
  mn10300_callback = p;
}

/* All the code for exiting, signals, etc needs to be revamped.

   This is enough to get c-torture limping though.  */

void
sim_stop_reason (sd, reason, sigrc)
     SIM_DESC sd;
     enum sim_stop *reason;
     int *sigrc;
{
  if (State.exited)
    *reason = sim_exited;
  else
    *reason = sim_stopped;
  if (State.exception == SIGQUIT)
    *sigrc = 0;
  else
    *sigrc = State.exception;
}

int
sim_read (sd, addr, buffer, size)
     SIM_DESC sd;
     SIM_ADDR addr;
     unsigned char *buffer;
     int size;
{
  int i;
  for (i = 0; i < size; i++)
    buffer[i] = load_byte (addr + i);

  return size;
} 

void
sim_do_command (sd, cmd)
     SIM_DESC sd;
     char *cmd;
{
  (*mn10300_callback->printf_filtered) (mn10300_callback, "\"%s\" is not a valid mn10300 simulator command.\n", cmd);
}

SIM_RC
sim_load (sd, prog, abfd, from_tty)
     SIM_DESC sd;
     char *prog;
     bfd *abfd;
     int from_tty;
{
  extern bfd *sim_load_file (); /* ??? Don't know where this should live.  */
  bfd *prog_bfd;

  prog_bfd = sim_load_file (sd, myname, mn10300_callback, prog, abfd,
			    sim_kind == SIM_OPEN_DEBUG,
			    0, sim_write);
  if (prog_bfd == NULL)
    return SIM_RC_FAIL;
  if (abfd == NULL)
    bfd_close (prog_bfd);
  return SIM_RC_OK;
} 
#endif  /* not WITH_COMMON */


#if WITH_COMMON

/* For compatibility */
SIM_DESC simulator;

/* mn10300 interrupt model */

enum interrupt_type
{
  int_reset,
  int_nmi,
  int_intov1,
  int_intp10,
  int_intp11,
  int_intp12,
  int_intp13,
  int_intcm4,
  num_int_types
};

char *interrupt_names[] = {
  "reset",
  "nmi",
  "intov1",
  "intp10",
  "intp11",
  "intp12",
  "intp13",
  "intcm4",
  NULL
};


static void
do_interrupt (sd, data)
     SIM_DESC sd;
     void *data;
{
#if 0
  char **interrupt_name = (char**)data;
  enum interrupt_type inttype;
  inttype = (interrupt_name - STATE_WATCHPOINTS (sd)->interrupt_names);

  /* For a hardware reset, drop everything and jump to the start
     address */
  if (inttype == int_reset)
    {
      PC = 0;
      PSW = 0x20;
      ECR = 0;
      sim_engine_restart (sd, NULL, NULL, NULL_CIA);
    }

  /* Deliver an NMI when allowed */
  if (inttype == int_nmi)
    {
      if (PSW & PSW_NP)
	{
	  /* We're already working on an NMI, so this one must wait
	     around until the previous one is done.  The processor
	     ignores subsequent NMIs, so we don't need to count them.
	     Just keep re-scheduling a single NMI until it manages to
	     be delivered */
	  if (STATE_CPU (sd, 0)->pending_nmi != NULL)
	    sim_events_deschedule (sd, STATE_CPU (sd, 0)->pending_nmi);
	  STATE_CPU (sd, 0)->pending_nmi =
	    sim_events_schedule (sd, 1, do_interrupt, data);
	  return;
	}
      else
	{
	  /* NMI can be delivered.  Do not deschedule pending_nmi as
             that, if still in the event queue, is a second NMI that
             needs to be delivered later. */
	  FEPC = PC;
	  FEPSW = PSW;
	  /* Set the FECC part of the ECR. */
	  ECR &= 0x0000ffff;
	  ECR |= 0x10;
	  PSW |= PSW_NP;
	  PSW &= ~PSW_EP;
	  PSW |= PSW_ID;
	  PC = 0x10;
	  sim_engine_restart (sd, NULL, NULL, NULL_CIA);
	}
    }

  /* deliver maskable interrupt when allowed */
  if (inttype > int_nmi && inttype < num_int_types)
    {
      if ((PSW & PSW_NP) || (PSW & PSW_ID))
	{
	  /* Can't deliver this interrupt, reschedule it for later */
	  sim_events_schedule (sd, 1, do_interrupt, data);
	  return;
	}
      else
	{
	  /* save context */
	  EIPC = PC;
	  EIPSW = PSW;
	  /* Disable further interrupts.  */
	  PSW |= PSW_ID;
	  /* Indicate that we're doing interrupt not exception processing.  */
	  PSW &= ~PSW_EP;
	  /* Clear the EICC part of the ECR, will set below. */
	  ECR &= 0xffff0000;
	  switch (inttype)
	    {
	    case int_intov1:
	      PC = 0x80;
	      ECR |= 0x80;
	      break;
	    case int_intp10:
	      PC = 0x90;
	      ECR |= 0x90;
	      break;
	    case int_intp11:
	      PC = 0xa0;
	      ECR |= 0xa0;
	      break;
	    case int_intp12:
	      PC = 0xb0;
	      ECR |= 0xb0;
	      break;
	    case int_intp13:
	      PC = 0xc0;
	      ECR |= 0xc0;
	      break;
	    case int_intcm4:
	      PC = 0xd0;
	      ECR |= 0xd0;
	      break;
	    default:
	      /* Should never be possible.  */
	      sim_engine_abort (sd, NULL, NULL_CIA,
				"do_interrupt - internal error - bad switch");
	      break;
	    }
	}
      sim_engine_restart (sd, NULL, NULL, NULL_CIA);
    }
  
  /* some other interrupt? */
  sim_engine_abort (sd, NULL, NULL_CIA,
		    "do_interrupt - internal error - interrupt %d unknown",
		    inttype);
#endif  /* 0 */
}

/* These default values correspond to expected usage for the chip.  */

SIM_DESC
sim_open (kind, cb, abfd, argv)
     SIM_OPEN_KIND kind;
     host_callback *cb;
     struct _bfd *abfd;
     char **argv;
{
  SIM_DESC sd = sim_state_alloc (kind, cb);
  mn10300_callback = cb;

  SIM_ASSERT (STATE_MAGIC (sd) == SIM_MAGIC_NUMBER);

  /* for compatibility */
  simulator = sd;

  /* FIXME: should be better way of setting up interrupts */
  STATE_WATCHPOINTS (sd)->pc = &(PC);
  STATE_WATCHPOINTS (sd)->sizeof_pc = sizeof (PC);
  STATE_WATCHPOINTS (sd)->interrupt_handler = do_interrupt;
  STATE_WATCHPOINTS (sd)->interrupt_names = interrupt_names;

  if (sim_pre_argv_init (sd, argv[0]) != SIM_RC_OK)
    return 0;

  /* Allocate core managed memory */

  /* "Mirror" the ROM addresses below 1MB. */
  sim_do_command (sd, "memory region 0,0x100000");

  /* getopt will print the error message so we just have to exit if this fails.
     FIXME: Hmmm...  in the case of gdb we need getopt to call
     print_filtered.  */
  if (sim_parse_args (sd, argv) != SIM_RC_OK)
    {
      /* Uninstall the modules to avoid memory leaks,
	 file descriptor leaks, etc.  */
      sim_module_uninstall (sd);
      return 0;
    }

  /* check for/establish the a reference program image */
  if (sim_analyze_program (sd,
			   (STATE_PROG_ARGV (sd) != NULL
			    ? *STATE_PROG_ARGV (sd)
			    : NULL),
			   abfd) != SIM_RC_OK)
    {
      sim_module_uninstall (sd);
      return 0;
    }

  /* establish any remaining configuration options */
  if (sim_config (sd) != SIM_RC_OK)
    {
      sim_module_uninstall (sd);
      return 0;
    }

  if (sim_post_argv_init (sd) != SIM_RC_OK)
    {
      /* Uninstall the modules to avoid memory leaks,
	 file descriptor leaks, etc.  */
      sim_module_uninstall (sd);
      return 0;
    }


  /* set machine specific configuration */
/*   STATE_CPU (sd, 0)->psw_mask = (PSW_NP | PSW_EP | PSW_ID | PSW_SAT */
/* 			     | PSW_CY | PSW_OV | PSW_S | PSW_Z); */

  return sd;
}


void
sim_close (sd, quitting)
     SIM_DESC sd;
     int quitting;
{
  sim_module_uninstall (sd);
}


SIM_RC
sim_create_inferior (sd, prog_bfd, argv, env)
     SIM_DESC sd;
     struct _bfd *prog_bfd;
     char **argv;
     char **env;
{
  memset (&State, 0, sizeof (State));
  if (prog_bfd != NULL) {
    PC = bfd_get_start_address (prog_bfd);
  } else {
    PC = 0;
  }
  CIA_SET (STATE_CPU (sd, 0), (unsigned64) PC);

  return SIM_RC_OK;
}

void
sim_do_command (sd, cmd)
     SIM_DESC sd;
     char *cmd;
{
  char *mm_cmd = "memory-map";
  char *int_cmd = "interrupt";

  if (sim_args_command (sd, cmd) != SIM_RC_OK)
    {
      if (strncmp (cmd, mm_cmd, strlen (mm_cmd) == 0))
	sim_io_eprintf (sd, "`memory-map' command replaced by `sim memory'\n");
      else if (strncmp (cmd, int_cmd, strlen (int_cmd)) == 0)
	sim_io_eprintf (sd, "`interrupt' command replaced by `sim watch'\n");
      else
	sim_io_eprintf (sd, "Unknown command `%s'\n", cmd);
    }
}
#endif  /* WITH_COMMON */

/* FIXME These would more efficient to use than load_mem/store_mem,
   but need to be changed to use the memory map.  */

uint8
get_byte (x)
     uint8 *x;
{
  return *x;
}

uint16
get_half (x)
     uint8 *x;
{
  uint8 *a = x;
  return (a[1] << 8) + (a[0]);
}

uint32
get_word (x)
      uint8 *x;
{
  uint8 *a = x;
  return (a[3]<<24) + (a[2]<<16) + (a[1]<<8) + (a[0]);
}

void
put_byte (addr, data)
     uint8 *addr;
     uint8 data;
{
  uint8 *a = addr;
  a[0] = data;
}

void
put_half (addr, data)
     uint8 *addr;
     uint16 data;
{
  uint8 *a = addr;
  a[0] = data & 0xff;
  a[1] = (data >> 8) & 0xff;
}

void
put_word (addr, data)
     uint8 *addr;
     uint32 data;
{
  uint8 *a = addr;
  a[0] = data & 0xff;
  a[1] = (data >> 8) & 0xff;
  a[2] = (data >> 16) & 0xff;
  a[3] = (data >> 24) & 0xff;
}

int
sim_fetch_register (sd, rn, memory, length)
     SIM_DESC sd;
     int rn;
     unsigned char *memory;
     int length;
{
  put_word (memory, State.regs[rn]);
  return -1;
}
 
int
sim_store_register (sd, rn, memory, length)
     SIM_DESC sd;
     int rn;
     unsigned char *memory;
     int length;
{
  State.regs[rn] = get_word (memory);
  return -1;
}
