/* $Header$
 *
 * SQLITE3 query tool for shell usage
 *
 * Copyright (C)2006-2009 Valentin Hilbig, webmaster@scylla-charybdis.com
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
 * Revision 1.14  2009-05-29 16:03:50  tino
 * Usage now ready
 *
 * Revision 1.13  2009-05-29 15:55:48  tino
 * Next version
 *
 * Revision 1.12  2009-05-28 09:29:26  tino
 * Debug output for results
 *
 * Revision 1.11  2009-05-26 23:33:00  tino
 * Timeout problem found, it's an SQLite feature.  So no fix, only workaround
 * with "begin exclusive;".  Debugging improved and extended errorcodes support.
 *
 * Revision 1.10  2009-05-25 11:38:15  tino
 * Timestamping for debugging
 *
 * Revision 1.9  2009-05-25 11:14:45  tino
 * Whitespace trimming and improved debug
 *
 * Revision 1.8  2009-05-25 00:17:31  tino
 * Timeout now works for all statements
 *
 * Revision 1.7  2009-04-22 16:49:43  tino
 * -s and -u
 *
 * Revision 1.6  2009-02-16 04:41:37  tino
 * Feature :fd#_[#][t]_[#]_X (sigh)
 *
 * Revision 1.5  2008-06-06 23:56:34  tino
 * Echo escape now shall work as thought
 *
 * Revision 1.4  2008-05-30 00:55:45  tino
 * New options -a and -z, echo escape fixed
 *
 * Revision 1.3  2008-03-04 09:34:01  tino
 * Bugfix: $fd# did no more work without -l
 *
 * Revision 1.2  2008-02-07 02:23:20  tino
 * Option -l and Cygwin fixes
 *
 * Revision 1.1  2006-06-02 01:31:49  tino
 * First version, global commit
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define	MAX_SEPS	100

static SQ_PREFIX()	*db;
static int		dodebug, doraw;
static int		ansiescape, donul;
static int		unbuffered;
static const char	*sep[MAX_SEPS], *rowbegin, *rowend;
static int		seps;

static void
debug(const char *s, ...)
{
  if (!dodebug)
    return;
  va_list	list;

  if (*s=='[')
    {
      struct timeval	tv;
      struct tm		*tm;
      static long	mypid;

      if (!mypid)
	mypid	= getpid();
      gettimeofday(&tv, NULL);
      tm	= gmtime(&tv.tv_sec);
      fprintf(stderr, "[%04d%02d%02d-%02d%02d%02d.%06ld][%ld]", 1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (long)tv.tv_usec, mypid);
    }
  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
}

static void
verr(int rc, const char *s, va_list list)
{
  int		e;

  e	= errno;
  debug("[err] %d %s\n", rc, s);
  fprintf(stderr, "sqlite error %d: ", rc);
  vfprintf(stderr, s, list);
  if (rc!=SQLITE_OK)
    fprintf(stderr, ": %s", SQ_PREFIX(_errmsg)(db));
  if (e)
    fprintf(stderr, ": %s", strerror(e));
  fprintf(stderr, "\n");
  if (rc==SQLITE_BUSY)
    debug("[HINT] try using \"BEGIN EXCLUSIVE\" for write transactions\n");
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
  int		rc2;

  if (rc==SQLITE_OK)
    return;
  va_start(list, s);
  rc2	= SQ_PREFIX(_errcode)(db);
  verr((rc2&255)==rc ? rc2 : rc, s, list);
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
ansiescape_string(const unsigned char *s, int len)
{
  for (; --len>=0; s++)
    if (((unsigned char)*s)<33 || ((unsigned char)*s)>=127)
      printf("\\%03o", (unsigned char)*s);
    else
      switch (*s)
	{
	case '\\':
	case '\'':
	  putchar('\\');
	default:
	  putchar(*s);
	  break;
	}
}

static void
escape_string(const unsigned char *s, int len)
{
  for (; --len>=0; s++)
    {
      if (*s<=0x20 || (*s>0x7f && *s<0xa0))
	printf("\\0%03o", *s);
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
putStringOrNUL(const char *s)
{
  if (!s)
    return;
  if (*s)
    fputs(s, stdout);
  else
    putchar(0);
}

static void
row(SQ_PREFIX(_stmt) *s, int r)
{
  int	i, n;

  putStringOrNUL(rowbegin);

  n	= SQ_PREFIX(_column_count)(s);
  for (i=0; i<n; i++)
    {
      const void	*text;
      int		len=0 /* shutup stupid compiler */;

      if (seps)
        {
          if (i)
	    putStringOrNUL(sep[(i-1)%seps]);
        }
      else if (!doraw)
	{
	  printf("%d ", r);
	  quote_string(SQ_PREFIX(_column_name)(s, i));
	}
      else if (!donul && (i || r>1))
	printf("\n");
      text	= SQ_PREFIX(_column_blob)(s, i);
      if (text)
	{
	  len	= SQ_PREFIX(_column_bytes)(s, i);
	  if (doraw)
	    fwrite(text, len, 1, stdout);
	  else if (ansiescape)
	    {
	      if (!seps)
	        putchar(' ');
	      ansiescape_string(text, len);
	    }
	  else if (simplestring(text, len))
	    printf("%s%.*s", (seps ? "" : " t "), len, (const char *)text);
	  else
	    {
              if (!seps)
	        printf(" e ");
	      escape_string(text, len);
	    }
	}
      else if (!doraw && !seps)
	printf(" 0");
      if (seps)
	{}
      else if (donul)
	putchar(0);
      else if (!doraw)
	printf("\n");
      if (ferror(stdout))
	err(SQLITE_OK, "cannot write to stdout");

      if (text)
	debug("[row%dcol%d=%d]%.*s\n", r, i, len, len, text);
      else
	debug("[row%dcol%d=null]\n", r, i);
    }
  putStringOrNUL(rowend);
  if (seps)
    putchar((donul ? 0 : '\n'));
  if (unbuffered)
    fflush(stdout);
}

static void *
my_realloc(void *ptr, size_t len)
{
  ptr	= ptr ? realloc(ptr, len) : malloc(len);
  if (!ptr)
    err(SQLITE_OK, "out of memory");
  return ptr;
}

static int
hunt_read(const char *ptr, int *len, int got, int readend, int readmax)
{
  int	pos;

  if (readmax && *len+got>readmax)
    got	= readmax- *len;
  if (readend<0)
    {
      *len	+= got;
      return got;
    }
  pos	= *len;
  if (readend==256)
    {
      for (; --got>=0; pos++)
	if (isspace(ptr[pos]))
	  {
	    *len	= pos+1;
	    return pos;
	  }
    }
  else
    {
      for (; --got>=0; pos++)
	if ((unsigned char)ptr[pos]==(unsigned char)readend)
	  {
	    *len	= pos+1;
	    return pos;
	  }
    }
  *len	= pos;
  return pos;
}

static int
fetchfile(SQ_PREFIX(_stmt) *pStmt, int i, int cnt, int fd, char *end, int *looper)
{
  static int		allocs;
  static struct fetch_buf
    {
      int	pos, fill, max;
      char	*buf;
    } *fds;
  int			inc, len, off;
  struct fetch_buf	*tmp;
  long			readmax;
  int			readend;
  int			readtrim;

  if (fd>=allocs)
    {
      fds	= my_realloc(fds, (fd+1)*sizeof *fds);
      memset(fds+allocs, 0, (fd+1-allocs)*sizeof *fds);
      allocs	= fd+1;
    }
  readmax	= 0;
  readend	= -1;
  readtrim	= 0;
  if (end && *end)
    {
      if (*end++!='_')
	err(SQLITE_OK, "missing underscore (_) in %s", end);
      readmax	= strtol(end, &end, 0);
      if (end)
	{
	  if (*end=='t')
	    {
	      readtrim	= 1;
	      end++;
	    }
	  if (*end)
	    {
	      readend	= 256;
	      if (*end++!='_')
		err(SQLITE_OK, "missing underscore (_) in %s", end);
	      if (*end && *end!='_')
		{
		  readend	= strtol(end, &end, 0);
		  if (readend<0 || readend>255)
		    err(SQLITE_OK, "invalid parameter %d", readend);
		  if (*end && *end!='_')
		    err(SQLITE_OK, "parameter mismatch at %s", end);
		}
	      /* rest ignored, there to differentiate the definitions	*/
	    }
	}
    }
  tmp	= fds+fd;
  inc	= 0;
  len	= 0;
  off	= 0;
  for (;;)
    {
      int	got;

      if (tmp->pos+len<tmp->fill)
	{
	  off	=  hunt_read(tmp->buf+tmp->pos, &len, tmp->fill-tmp->pos-len, readend, readmax);
	  if (off<tmp->fill-tmp->pos)
	    {
	      if (!*looper)
		*looper	= 1;
	      break;
	    }
	}
      if (tmp->fill>=tmp->max)
	{
	  if (tmp->pos)
	    {
	      if (tmp->fill>tmp->pos)
		memmove(tmp->buf, tmp->buf+tmp->pos, tmp->fill-=tmp->pos);
	      else
		tmp->fill	= 0;
	      tmp->pos		= 0;
	      continue;
	    }
	  if ((tmp->max+=(inc+=BUFSIZ))<0)
	    err(SQLITE_OK, "internal counter overrun reading fd %d", fd);
	  tmp->buf	= my_realloc(tmp->buf, tmp->max);
	  debug("(fd %d: len %d) ", fd, tmp->max);
	}
      got	= read(fd, tmp->buf+tmp->fill, tmp->max-tmp->fill);
      if (got<=0)
	{
 	  if (!got)
	    {
	      if (!len && end)
		*looper	= -1;
	      break;
	    }
	  if (errno==EINTR || errno==EAGAIN)
	    continue;
	  err(SQLITE_OK, "read error from fd %d", fd);
	}
      tmp->fill	+= got;
    }
  if (readtrim)
    {
      while (off>0 && isspace(tmp->buf[tmp->pos]))
	{
	  off--;
	  tmp->pos++;
	}
      while (off>0 && isspace(tmp->buf[tmp->pos+off-1]))
	off--;
    }
  check(SQ_PREFIX(_bind_blob)(pStmt, i, tmp->buf+tmp->pos, off, SQLITE_TRANSIENT), "cannot bind parm %d: fd %d", cnt, fd);
  tmp->pos	+= len;
  return len;
}

int
main(int argc, char **argv)
{
  const char		*s, *zTail;
  SQ_PREFIX(_stmt)	*pStmt;
  int			cnt, arg, res, looping;
  long			timeout;
  const char		*arg0;

  arg0	= strrchr(argv[0], '/');
  if (!arg0++ && (arg0=strrchr(argv[0], '\\'), !arg0++))
    arg0	= argv[0];

  looping	= 0;
  timeout	= 100;

  /* actually this is a hack	*/
  for (; argc>1 && argv[1][0]=='-'; argv++, argc--)
    {
      switch (argv[1][1])
	{
	case 'a':
	  ansiescape	= 1;
	  continue;

	case 'b':
	  rowbegin	= argv[1]+2;
	  continue;

	case 'd':
	  dodebug	= 1;
	  continue;

	case 'e':
	  rowend	= argv[1]+2;
	  continue;

	case 'l':
	  looping	= 1;
	  continue;

	case 'z':
	  donul		= 1;
	case 'r':
	  doraw		= 1;
	  continue;

        case 's':
	  if (seps>=sizeof sep/sizeof *sep)
	    err(SQLITE_OK, "too many options -s, increase MAX_SEPS and recompile");
	  sep[seps++]	= argv[1]+2;
          continue;

	case 't':
	  timeout	= strtol(argv[1]+2, NULL, 0);
	  continue;

        case 'u':
	  unbuffered	= 1;
	  continue;

	case 'v':
	  ansiescape	= 1;
	  if (seps+1>=sizeof sep/sizeof *sep)
	    err(SQLITE_OK, "too many options -s, increase MAX_SEPS and recompile");
	  sep[seps++]	= "=$'";
	  sep[seps++]	= "' ";
	  rowend	= "'";
	  continue;
	}
      break;
    }
  if (argc<3)
    {
      fprintf(stderr,
	      "Usage: %s {-a|-d|-l|-n|-r|-sSTR|-tN|-z} database statement args #<data\n"
	      "\t\tversion " SQ_VERSION " compiled " __DATE__ "\n"
	      "\t-a	Use ANSI (shell) escapes instead of echo compatible ones\n"
	      "\t-bSTR	additional string to output on row Begin, empty for NUL\n"
	      "\t-d	switch on some debugging to stderr\n"
	      "\t-eSTR	additional string to output on row End, empty for NUL\n"
	      "\t-fSTR	final string when program terminates\n"
	      "\t-l	loop option added, loop if :fd#_X sequence not at EOF\n"
	      "\t-r	raw output, no row, col, type, fields NL separated\n"
	      "\t-sSEP	Output fields on one line SEP separated, empty for NUL\n"
	      "\t-tN	set the timeout in ms, default %ld\n"
	      "\t-u	unbuffered output, flush after each row\n"
	      "\t-v	same as -a -s\"=$'\" -s\"' \" -e\"'\"\n"
	      "\t-z	implies -r, but output NULs\n"
	      "\tUse ?NNN or :XXX to fetch args, $ENV to access environment\n"
	      "\tSome :name have special meaning:\n"
	      "\t	:fd#	read BLOB from file descriptor # (0=stdin)\n"
	      "\t	:fd#_N	read N characters, N is max if used in :fd#_N_T\n"
	      "\t	:fd#__T	read up to character T, T defaults to whitespace\n"
	      "\t	:fd#___X as before, but X is ignored to allow multiple use.\n"
	      "\t	Special: If N ends on t, whitespace trimming is done\n"
	      "\tParse the output as follows:\n"
	      "\t	sq3 'select * from table' |\n"
	      "\t	while read -r row col type data\n"
	      "\t	do case \"$type\" in\n"
	      "\t	t)	echo \"row=$row col=$col data=$data\";;\n"
	      "\t	e)	echo -e \"row=$row col=$col data=$data\";;\n"
	      "\t	0)	echo \"row=$row col=$col NULL\";;\n"
	      "\t	esac\n"
	      "\tReal apps will use 'echo -ne', not 'echo -e'.\n"
	      "\tExample for option -a:\n"
	      "\t	sq3 -a 'select * from table' |\n"
	      "\t	while read -r row col data\n"
	      "\t	do eval data=\"\\$'$data'\"\n"
	      "\t		...\n"
	      "\tExample for option -v:\n"
	      "\t	eval `./sq3 -v a.db \"select 't1',' hel lo ','t2','wo''rld'\"`\n"
	      "\t	echo \"#${t1}#${t2}#\"\n"
	      , arg0, timeout);
      return 1;
    }
  check(SQ_PREFIX(_open)(argv[1], &db), "cannot open db %s", argv[1]);
  check(SQ_PREFIX(_busy_timeout)(db, timeout), "cannot set timeout to %ld", timeout);
#if 0
  check(SQ_PREFIX(_busy_handler)(db, busy_handler, &timeout), "cannot set busy_handler");
#endif
  /* for some unknown reason it's really hard to get hold on the new extended error codes!
   */
  check(SQ_PREFIX(_extended_result_codes(db, 1)), "cannot set extended result codes");
#ifdef	WITH_PCRE
  check(SQ_PREFIX(_create_function)(db, "pcre", 2, SQLITE_UTF8, NULL, pcre2, NULL, NULL));
  check(SQ_PREFIX(_create_function)(db, "pcre", 3, SQLITE_UTF8, NULL, pcre3, NULL, NULL));
#endif

  debug("[start]\n");
  s	= argv[2];
  arg	= 3;
  cnt	= 0;
  res	= 0;
  while (*s)
    {
      int	n, i, looper, larg;

      if (isspace(*s))
	{
	  s++;
	  continue;
	}
      larg	= arg;
      zTail	= 0;
      check(SQ_PREFIX(_prepare_v2)(db, s, -1, &pStmt, &zTail), "invalid sql: %s", s);
      debug("[sql] %.*s\n", (int)(zTail-s), s);
      for (;;)
	{
	  looper	= 0;
	  arg		= larg;
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
		  if (name[1]=='f' && name[2]=='d' && name[3] && (((fd=strtol(name+3, &end, 10)), end) && end && (!*end || *end=='_')))
		    {
		      fd	= fetchfile(pStmt, i, cnt, fd, end, &looper);
		      debug(" = %d bytes (loop %d)\n", fd, looper);
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
	  if (looping && looper<0)
	    break;
	  debug("[run] %d\n", looper);
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
	  if (!looping || !looper)
	    break;
	  debug("[reset]\n");
	  check(SQ_PREFIX(_reset)(pStmt), "cannot reset statement: %.*s", (int)(zTail-s), s);
	}
      debug("[finalize]\n");
      check(SQ_PREFIX(_finalize)(pStmt), "cannot finalize statement: %.*s", (int)(zTail-s), s);
      s	= zTail;
    }
  debug("[end]\n");
  check(SQ_PREFIX(_close)(db), "cannot close db %s", argv[1]);
  return 0;
}
