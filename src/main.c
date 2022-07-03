/** archeolog: extract data from log files
2022, Simon Zolin */

#include <archeolog.h>
#include <proc.h>
#include <FFOS/std.h>
#include <FFOS/ffos-extern.h>

void log_print(int level, const char *fmt, ...)
{
	char buf[4096];
	ffstr s = {};
	s.ptr = buf;
	va_list args;
	va_start(args, fmt);
	ffsize r = ffstr_addfmtv(&s, sizeof(buf), fmt, args);
	va_end(args);
	if (r != 0)
		r = ffstderr_write(s.ptr, r);
}

int main(int argc, const char **argv)
{
	int rc = 1;
	struct archeolog *a = NULL;
	gconf = ffmem_new(struct arlg_conf);
	if (0 != conf_cmdline(gconf, argc, argv)) {
		goto end;
	}

	a = ffmem_new(struct archeolog);
	if (0 != arlg_open(a, gconf))
		goto end;
	if (0 != arlg_extract(a))
		goto end;

	rc = 0;

end:
	arlg_close(a);
	conf_destroy(gconf);
	return rc;
}
