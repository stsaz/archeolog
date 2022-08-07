/** archeolog: configuration
2022, Simon Zolin */

#include <archeolog.h>
#include <util/cmdarg-scheme.h>
#include <FFOS/std.h>

struct arlg_conf *gconf;

void conf_destroy(struct arlg_conf *conf)
{
	ffmem_free(conf->filename);
	ffstr_free(&conf->filter);
}

int conf_date(struct arlg_conf *conf, ffdatetime *dt, ffstr *s)
{
	static const uint date_fmts[] = {
		FFTIME_DATE_YMD,
	};
	static const uint date_fmt_lens[] = {
		FFS_LEN("yyyy-MM-dd"),
	};
	for (uint i = 0;  ;  i++) {
		if (i == FF_COUNT(date_fmts)) {
			dbglog("unsupported date format: %S", s);
			return 0;
		}

		if (0 == _fftime_date_fromstr(dt, s, date_fmts[i])) {
			conf->date_fmt = date_fmts[i];
			conf->date_len = date_fmt_lens[i];

			if (s->len != 0) {
				if (s->ptr[0] == ' ' || s->ptr[0] == 'T') {
					ffstr_shift(s, 1);
					conf->date_len++;
				} else {
					errlog("unsupported date-time split character: '%c'", s->ptr[0]);
					return 1;
				}
			}
			break;
		}
	}
	return 0;
}

int conf_time(struct arlg_conf *conf, ffdatetime *dt, ffstr *s)
{
	static const uint time_fmts[] = {
		FFTIME_HMS_MSEC, FFTIME_HMS
	};
	static const uint time_fmt_lens[] = {
		FFS_LEN("hh:mm:ss.msc"), FFS_LEN("hh:mm:ss")
	};
	for (uint i = 0;  ;  i++) {
		if (i == FF_COUNT(time_fmts)) {
			errlog("unsupported time format: %S", s);
			return 1;
		}

		if (0 == _fftime_time_fromstr(dt, s, time_fmts[i])) {
			conf->date_fmt |= time_fmts[i];
			conf->date_len += time_fmt_lens[i];
			break;
		}
	}
	return 0;
}

/**
Return >0: success
 <0: need more data
 0: error */
int date_parse(struct arlg_conf *conf, const ffstr *s, fftime *t)
{
	ffstr ss = *s;
	ffdatetime dt = {};

	if (conf->date_fmt & 0x0f) {
		if (0 != _fftime_date_fromstr(&dt, &ss, conf->date_fmt))
			goto end;
		if (conf->date_fmt & 0xf0) {
			if (ss.len == 0)
				goto end;
			if (!(ss.ptr[0] == ' ' || ss.ptr[0] == 'T'))
				return 0;
			ffstr_shift(&ss, 1);
		}
	}

	if (conf->date_fmt & 0xf0) {
		if (0 != _fftime_time_fromstr(&dt, &ss, conf->date_fmt))
			goto end;
	}

	fftime_join1(t, &dt);
	return s->len - ss.len;

end:
	if (s->len < conf->date_len)
		return -1;
	return 0;
}

#define R_DONE  100
#define R_BADVAL  101

static int conf_infile(ffcmdarg_scheme *cs, struct arlg_conf *conf, char *s)
{
	ffmem_free(conf->filename);
	conf->filename = ffsz_dup(s);
	return 0;
}

static int conf_startend(ffcmdarg_scheme *cs, struct arlg_conf *conf, ffstr *s)
{
	ffstr d = *s;

	ffdatetime dt = {};
	if (conf->date_fmt == 0) {
		// detect datetime format
		if (0 != conf_date(conf, &dt, &d))
			return R_BADVAL;
		if (d.len != 0) {
			if (0 != conf_time(conf, &dt, &d))
				return R_BADVAL;
		}
	}

	if (ffsz_eq(cs->arg->long_name, "start")) {
		if (s->len != date_parse(conf, s, &conf->start_date))
			return R_BADVAL;
		// fftime_join1(&conf->start_date, &dt);
		dbglog("start-date: %Usec", conf->start_date.sec);

	} else {
		if (s->len != date_parse(conf, s, &conf->end_date))
			return R_BADVAL;
		dbglog("end-date: %Usec", conf->end_date.sec);
	}

	return 0;
}

static int conf_help()
{
	static const char help[] =
"archeolog v" ARLG_VER "\n\
Usage:\n\
 archeolog [OPTIONS] FILE\n\
\n\
OPTIONS:\n\
 -s, --start=TIME  Start-datetime\n\
 -e, --end=TIME    End-datetime\n\
 -l, --lines       Max N of output lines\n\
     --buffer      File buffer in bytes (=8M)\n\
 -D, --debug       Debug logging\n\
 -h, --help        Show help\n\
";
	ffstdout_write(help, FFS_LEN(help));
	return R_DONE;
}

static const ffcmdarg_arg arlg_cmd_args[] = {
	{ 0, "",	FFCMDARG_TSTRZ | FFCMDARG_FNOTEMPTY, (ffsize)conf_infile },
	{ 's', "start",	FFCMDARG_TSTR | FFCMDARG_FNOTEMPTY, (ffsize)conf_startend },
	{ 'e', "end",	FFCMDARG_TSTR | FFCMDARG_FNOTEMPTY, (ffsize)conf_startend },
	{ 'l', "lines",	FFCMDARG_TINT64, FF_OFF(struct arlg_conf, max_lines) },
	{ 0, "buffer",	FFCMDARG_TINT32, FF_OFF(struct arlg_conf, read_chunk_size_large) },
	{ 'D', "debug",	FFCMDARG_TSWITCH, FF_OFF(struct arlg_conf, debug) },
	{ 'h', "help",	FFCMDARG_TSWITCH, (ffsize)conf_help },
	{}
};

void conf_init(struct arlg_conf *conf)
{
	conf->read_chunk_size_small = 4*1024;
	conf->read_chunk_size_large = 8*1024*1024;
	conf->read_chunk_align = 4*1024;
}

int conf_check(struct arlg_conf *conf)
{
	if (conf->filename == NULL) {
		errlog("input file isn't specified");
		return 1;
	}
	if (conf->start_date.sec != 0 && conf->end_date.sec != 0
		&& fftime_cmp(&conf->start_date, &conf->end_date) > 0) {
		errlog("end-date must be larger than start-date");
		return 1;
	}
	if (conf->read_chunk_size_large == 0) {
		errlog("bad buffer size");
		return 1;
	}
	conf->read_chunk_size_small = ffmin(conf->read_chunk_size_small, conf->read_chunk_size_large);
	return 0;
}

/** Set configuration from command line */
int conf_cmdline(struct arlg_conf *conf, int argc, const char **argv)
{
	conf_init(conf);

	int rc = 1;
	ffstr errmsg = {};
	int r = ffcmdarg_parse_object(arlg_cmd_args, conf, argv, argc, 0, &errmsg);
	if (r < 0) {
		if (r == -R_DONE)
			goto err;
		else if (r == -R_BADVAL)
			ffstderr_fmt("command line: bad value\n");
		else
			ffstderr_fmt("command line: %S\n", &errmsg);
		goto err;
	}

	if (0 != conf_check(conf))
		goto err;

	rc = 0;

err:
	ffstr_free(&errmsg);
	return rc;
}
