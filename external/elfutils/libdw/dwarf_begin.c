
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#include <libdwP.h>


Dwarf *
dwarf_begin (fd, cmd)
     int fd;
     Dwarf_Cmd cmd;
{
  Elf *elf;
  Elf_Cmd elfcmd;
  Dwarf *result = NULL;

  switch (cmd)
    {
    case DWARF_C_READ:
      elfcmd = ELF_C_READ_MMAP;
      break;
    case DWARF_C_WRITE:
      elfcmd = ELF_C_WRITE;
      break;
    case DWARF_C_RDWR:
      elfcmd = ELF_C_RDWR;
      break;
    default:
      /* No valid mode.  */
      __libdw_seterrno (DWARF_E_INVALID_CMD);
      return NULL;
    }

  /* We have to call `elf_version' here since the user might have not
     done it or initialized libelf with a different version.  This
     would break libdwarf since we are using the ELF data structures
     in a certain way.  */
  elf_version (EV_CURRENT);

  /* Get an ELF descriptor.  */
  elf = elf_begin (fd, elfcmd, NULL);
  if (elf == NULL)
    {
      /* Test why the `elf_begin" call failed.  */
      struct stat64 st;

      if (fstat64 (fd, &st) == 0 && ! S_ISREG (st.st_mode))
	__libdw_seterrno (DWARF_E_NO_REGFILE);
      else if (errno == EBADF)
	__libdw_seterrno (DWARF_E_INVALID_FILE);
      else
	__libdw_seterrno (DWARF_E_IO_ERROR);
    }
  else
    {
      /* Do the real work now that we have an ELF descriptor.  */
      result = dwarf_begin_elf (elf, cmd, NULL);

      /* If this failed, free the resources.  */
      if (result == NULL)
	elf_end (elf);
      else
	result->free_elf = true;
    }

  return result;
}
