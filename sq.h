/* $Header$
 *
 * SQLITE3 query tool for shell usage
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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>

static SQ_PREFIX()	*db;
static int		dodebug, doraw;

static void
debug(const char *s, ...)
{
  if (!dodebug)
    return;
  va_list	list;

  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
}

static void
verr(int rc, const char *s, va_list list)
{
  int		e;

  e	= errno;
  fprintf(stderr, "sqlite error %d: ", rc);
  vfprintf(stderr, s, list);
  if (rc!=SQLITE_OK)
    fprintf(stderr, ": %s", SQ_PREFIX(_errmsg)(db));
  if (e)
    fprintf(stderr, ": %s", strerror(e));
  fprintf(stderr, "\n");
  exit(-1);
}

static void
err(int rc, const char *s, ...)
{
  va_list	list;

  va_start(list, s);
  verr(rc, s, list);
  va_end(list);
}

static void
check(int rc, const char *s, ...)
{
  va_list	list;

  if (rc==SQLITE_OK)
    return;
  va_start(list, s);
  verr(rc, s, list);
  va_end(list);
}

static void
quote_string(const char *s)
{
  for (; *s; s++)
    {
      if (isspace(*s) || *s=='\\')
	putchar('\\');
      putchar(*s);
    }
}

static void
escape_string(const unsigned char *s, int len)
{
  for (; --len>=0; s++)
    {
      if (*s<=0x20 || (*s>0x7f && *s<0xa0))
	printf("\\x%02x", *s);
      else
	putchar(*s);
    }
}

static int
simplestring(const unsigned char *s, int len)
{
  if (len<=0)
    return 1;
  /* special case:
   * Shell kills whitespace around strings.
   */
  if (*s<=0x20 || s[len-1]<=0x20)
    return 0;
  while (--len>=0)
    if (*s<0x20 || (*s>=0x7f && *s<0xa0))
      return 0;
  return 1;
}

static void
row(SQ_PREFIX(_stmt) *s, int r)
{
  int	i, n;

  n	= SQ_PREFIX(_column_count)(s);
  for (i=0; i<n; i++)
    {
      const void	*text;

      if (!doraw)
	{
	  printf("%d ", r);
	  quote_string(SQ_PREFIX(_column_name)(s, i));
	}
      else if (i || r>1)
	printf("\n");
      text	= SQ_PREFIX(_column_blob)(s, i);
      if (text)
	{
	  int	len;

	  len	= SQ_PREFIX(_column_bytes)(s, i);
	  if (doraw)
	    fwrite(text, len, 1, stdout);
	  else if (simplestring(text, len))
	    printf(" t %.*s", len, (const char *)text);
	  else
	    {
	      printf(" e ");
	      escape_string(text, len);
	    }
	}
      else if (!doraw)
	printf(" 0");
      if (!doraw)
	printf("\n");
      if (ferror(stdout))
	err(SQLITE_OK, "cannot write to stdout");
    }
}

static int
fetchfile(SQ_PREFIX(_stmt) *pStmt, int i, int cnt, int fd)
{
  static char	*buf;
  static int	max;
  int		len, inc;

  inc	= 0;
  len	= 0;
  for (;;)
    {
      int	got;

      if (len>=max)
	{
	  if ((max+=inc+=BUFSIZ)<0)
	    err(SQLITE_OK, "internal counter overrun reading fd %d", fd);
	  buf	= realloc(buf, max);
	}
      if (!buf)
	err(SQLITE_OK, "out of memory readin fd %d", fd);
      got	= read(fd, buf+len, max-len);
      if (got<=0)
	{
 	  if (!got)
	    break;
	  if (errno==EINTR || errno==EAGAIN)
	    continue;
	  err(SQLITE_OK, "read error from fd %d", fd);
	}
      len	+= got;
    }
  check(SQ_PREFIX(_bind_blob)(pStmt, i, buf, len, SQLITE_TRANSIENT), "cannot bind parm %d: fd %d", cnt, fd);
  return len;
}

int
main(int argc, char **argv)
{
  const char		*s, *zTail;
  SQ_PREFIX(_stmt)	*pStmt;
  int			cnt, arg, res;
  long			timeout	= 100;
  const char		*arg0;

  arg0	= strrchr(argv[0], '/');
  if (!arg0++ && (arg0=strrchr(argv[0], '/'), !arg0++))
    arg0	= argv[0];

  /* actually this are hacks	*/
  if (argc>1 && !strcmp(argv[1], "-d"))
    argv++, argc--, dodebug=1;
  if (argc>1 && argv[1][0]=='-' && argv[1][1]=='t')
    argv++, argc--, timeout = strtol(argv[0]+2, NULL, 0);
  if (argc>1 && !strcmp(argv[1], "-r"))
    argv++, argc--, doraw=1;

  if (argc<3)
    {
      fprintf(stderr,
	      "Usage: %s [-d] [-tN] [-r] database statement args #<data\n"
	      "\t\tversion " SQ_VERSION " compiled " __DATE__ "\n"
	      "\t-d	switch on some debugging to stderr\n"
	      "\t-tN	set the timeout in ms, default %ld\n"
	      "\t-rN	raw output, no row, col, type, fields NL separated\n"
	      "\tUse ? or :name to fetch args, $ENV to access environment\n"
	      "\tSome :name have special meaning:\n"
	      "\t	:fd#	read BLOB from file descriptor # (0=stdin)\n"
	      "\tParse the output as follows:\n"
	      "\t	sq3 'select * from table' |\n"
	      "\t	while read -r row col type data\n"
	      "\t	do case \"$type\" in\n"
	      "\t	t)	echo \"row=$row col=$col data=$data\";;\n"
	      "\t	e)	echo -e \"row=$row col=$col data=$data\";;\n"
	      "\t	0)	echo \"row=$row col=$col NULL\";;\n"
	      "\t	esac\n"
	      "\tNote that real apps will use 'echo -ne', not 'echo -e'\n"
	      , arg0, timeout);
      return 1;
    }
  check(SQ_PREFIX(_open)(argv[1], &db), "cannot open db %s", argv[1]);
  check(SQ_PREFIX(_busy_timeout)(db, timeout), "cannot set timeout to %ld", timeout);
  s	= argv[2];
  arg	= 3;
  cnt	= 0;
  res	= 0;
  do
    {
      int	n, i;

      zTail	= 0;
      check(SQ_PREFIX(_prepare)(db, s, -1, &pStmt, &zTail), "invalid sql: %s", s);
      n		= SQ_PREFIX(_bind_parameter_count)(pStmt);
      for (i=1; i<=n; i++)
	{
	  const char *name, *parm;

	  cnt++;
	  debug("[%d] parm", cnt);
	  name	= SQ_PREFIX(_bind_parameter_name)(pStmt, i);
	  if (name)
	    debug(" %s", name);
	  else
	    name	= "";
	  switch (*name)
	    {
	      int	fd;
	      char	*end;

	    default:
	      err(SQLITE_OK, "internal error: %s", name);
	    case ':':
	      if (name[1]=='f' && name[2]=='d' && name[3] && ((fd=strtol(name+3, &end, 10)), end && !*end))
		{
		  fd	= fetchfile(pStmt, i, cnt, fd);
	  	  debug(" = %d bytes\n", fd);
		  continue;
		}
	    case '?':
	    case 0:
	      if (arg>=argc)
		err(SQLITE_OK, "missing argument on commandline: %d", arg);
	      parm	= argv[arg++];
	      break;

	    case '$':
	      parm	= getenv(name+1);
	      break;
	    }
	  debug(" = '%s'\n", parm);
	  check(SQ_PREFIX(_bind_text)(pStmt, i, parm, -1, SQLITE_TRANSIENT), "cannot bind parm %d: '%s'", cnt, parm);
	}
      for (;;)
	{
	  int	r;

	  switch (r=SQ_PREFIX(_step)(pStmt))
	    {
	    case SQLITE_ROW:
	      row(pStmt, ++res);
	      continue;

	    default:
	      err(r, "cannot step statement: %.*s", (int)(zTail-s), s);

	    case SQLITE_DONE:
	      break;
	    }
	  break;
	}
      check(SQ_PREFIX(_finalize)(pStmt), "cannot finalize statement: %.*s", (int)(zTail-s), s);
    } while ((s=zTail)!=0 && *s);
  check(SQ_PREFIX(_close)(db), "cannot close db %s", argv[1]);
  return 0;
}
