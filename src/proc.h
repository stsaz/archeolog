/** archeolog: processor
2022, Simon Zolin */

#include "fcache.h"
#include <util/stream.h>
#include <FFOS/perf.h>
#include <FFOS/std.h>
#include <ffbase/vector.h>

struct arlg_file {
	fffd fd;
	uint64 size, cur, seek;
	struct fcache cache;
	uint read_last;
	uint read_chunk_size;
};

struct arlg_startdate {
	uint state;
	uint64 start_off, end_off, off_prev;
	fftime start_date, end_date;
	uint njumps;
	ffstr input;
	ffstream stm;
	fftime time_start;
	uint seq_scan :1;
};

struct filter {
	const struct filter_if *iface;
	uint opened :1
		, done :1;
};

struct archeolog {
	struct arlg_conf *conf;
	uint chain_flags; // enum CHAIN_FLAGS
	ffvec ffilters; // struct filter[]

	struct arlg_file file;
	struct arlg_startdate startdate;
	uint64 off;

	uint state, nxstate;
	uint64 lines;
	ffstream stm;
	ffstr input2;

	uint64 out_total;
};

int arlg_open(struct archeolog *a, struct arlg_conf *conf)
{
	a->conf = conf;
	ffstream_realloc(&a->stm, a->conf->date_len);
	return 0;
}

void arlg_close(struct archeolog *a)
{
	if (a == NULL)
		return;

	struct filter *f;
	FFSLICE_WALK(&a->ffilters, f) {
		if (f->opened && f->iface->close != NULL) {
			dbglog("filter '%s': closing", f->iface->name);
			f->iface->close(a);
		}
	}
	ffvec_free(&a->ffilters);
	ffstream_free(&a->stm);
}

int newline_find(const ffstr *s)
{
	int r;
	if (0 > (r = ffstr_findchar(s, '\n')))
		return -1;
	return r+1;
}

#include "file.h"
#include "startdate.h"

int dataproc_open(struct archeolog *a)
{
	if (a->conf->end_date.sec == 0
		&& a->conf->max_lines == 0
		&& a->conf->filter.len == 0) {
		return CHAIN_DONE;
	}
	return CHAIN_READY;
}

void dataproc_close(struct archeolog *a)
{
}

/** Return data until we pass end-time.
Return enum CHAIN_R */
int dataproc_process(struct archeolog *a, ffstr *in, ffstr *out)
{
	/*
	{DATA1
	[DATA2
	 DA]}

	{} - buf
	[] - view
	*/

	enum { I_FIRST, I_FINDLINE, I_CHECK, I_GATHER, };
	int r;
	uint64 line_off;
	ffstr buf, view;

	if (!(a->chain_flags & CHAIN_FBACK))
		a->input2 = *in;

	for (;;) {
		switch (a->state) {
		case I_FIRST:
			if (in->len == 0)
				return CHAIN_PREV;
			a->state = I_GATHER,  a->nxstate = I_CHECK;
			continue;

		case I_GATHER:
			r = ffstream_gather_ref(&a->stm, a->input2, a->conf->date_len, &buf);
			ffstr_shift(&a->input2, r);
			a->off += r;
			if (buf.len < a->conf->date_len) {
				if (a->stm.ref.len != 0)
					continue; // store input data in buffer
				if (!a->file.read_last)
					return CHAIN_PREV;
			}
			view = buf;
			a->state = a->nxstate;
			continue;

		case I_FINDLINE:
			if (0 > (r = newline_find(&view))) {
				ffstr_shift(&view, view.len);
				a->state = I_GATHER,  a->nxstate = I_FINDLINE;
				if (view.ptr == buf.ptr) {
					if (a->file.read_last)
						goto done;
					continue; // nothing to output
				}
				goto next;
			}
			ffstr_shift(&view, r);
			a->state = I_CHECK;
			continue;

		case I_CHECK:
			if (a->conf->end_date.sec != 0) {
				// check timestamp for the current line
				fftime curdate;
				r = date_parse(a->conf, &view, &curdate);
				if (r < 0) {
					if (a->file.read_last)
						goto done;
					a->state = I_GATHER,  a->nxstate = I_CHECK;
					if (view.ptr == buf.ptr)
						continue; // nothing to output
					goto next;
				} else if (r == 0) {
					a->state = I_FINDLINE;
					continue;
				}

				line_off = a->off - buf.len + view.ptr - buf.ptr;
				dbglog("current: %*s @%U", (ffsize)r, view.ptr, line_off);
				if (fftime_cmp(&curdate, &a->conf->end_date) > 0) {
					goto done;
				}
			}

			a->state = I_FINDLINE;
			continue;
		}
	}

next:
	ffstr_set(out, buf.ptr, view.ptr - buf.ptr);
	ffstream_consume(&a->stm, out->len);
	return CHAIN_NEXT;

done:
	ffstr_set(out, buf.ptr, view.ptr - buf.ptr);
	return CHAIN_SPLIT;
}

struct filter_if filter_data = { "data", dataproc_open, dataproc_close, dataproc_process };

int out_handle(struct archeolog *a, ffstr *in, ffstr *out)
{
	ffstdout_write(in->ptr, in->len);
	a->out_total += in->len;
	if (a->chain_flags & CHAIN_FFIRST) {
		dbglog("output:%U", a->out_total);
		return CHAIN_FIN;
	}
	return CHAIN_PREV;
}

struct filter_if filter_out = { "out", NULL, NULL, out_handle };

int arlg_extract(struct archeolog *a)
{
	int rc = 1, r;
	ffstr in = {}, out;
	int i = 0;

	// enum CHAIN_R
	static const char ret_str[][12] = {
		"CHAIN_DONE",
		"CHAIN_SPLIT",
		"CHAIN_PREV",
		"CHAIN_NEXT",
		"CHAIN_ERR",
		"CHAIN_FIN",
		"CHAIN_READY",
	};

	static const struct filter_if* filters[] = {
		&filter_file,
		&filter_startdate,
		&filter_data,
		&filter_out,
	};
	ffvec_zallocT(&a->ffilters, FF_COUNT(filters), struct filter);
	a->ffilters.len = FF_COUNT(filters);
	struct filter *f;
	FFSLICE_WALK(&a->ffilters, f) {
		f->iface = filters[i++];
	}

	i = 0;
	a->chain_flags |= CHAIN_FFIRST;
	for (;;) {

		f = a->ffilters.ptr;
		f = &f[i];

		if (!f->opened) {
			f->opened = 1;
			if (f->iface->open != NULL) {
				dbglog("filter '%s': opening", f->iface->name);
				r = f->iface->open(a);
				switch (r) {
				case CHAIN_DONE:
				case CHAIN_NEXT:
					out = in;
					goto chk_ret;
				case CHAIN_READY:
					break;
				case CHAIN_ERR:
				default:
					goto end;
				}
			}
		}

		if (!f->done) {
			ffstr_null(&out);
			dbglog("filter '%s': %s calling in:%L  first:%u"
				, f->iface->name
				, !(a->chain_flags & CHAIN_FBACK) ? ">>" : "<<"
				, in.len, !!(a->chain_flags & CHAIN_FFIRST));
			r = f->iface->process(a, &in, &out);
			dbglog("filter '%s' returned %s out:%L", f->iface->name, ret_str[r], out.len);
		} else {
			// Last time the filter had returned CHAIN_DONE,
			//  and now we're going backward
			//  - it's time to actually close it and remove from chain.
			FF_ASSERT(a->chain_flags & CHAIN_FBACK);
			if (f->iface->close != NULL) {
				dbglog("filter '%s': closing", f->iface->name);
				f->iface->close(a);
			}
			dbglog("filter '%s': removing", f->iface->name);
			ffslice_rmT((ffslice*)&a->ffilters, i, 1, struct filter);
			if (i > 0)
				i--;
			else
				a->chain_flags &= ~CHAIN_FBACK;
			continue;
		}

chk_ret:
		switch (r) {
		case CHAIN_PREV:
			a->chain_flags |= CHAIN_FBACK;
			ffstr_null(&in);
			i--;
			FF_ASSERT(i >= 0);

			if (i == 0)
				a->chain_flags |= CHAIN_FFIRST;
			break;

		case CHAIN_SPLIT: {
			struct filter *ff = a->ffilters.ptr;
			for (uint ii = 0;  ii < i;  ii++) {
				ff[ii].done = 1;
			}
			a->chain_flags |= CHAIN_FFIRST;
		}
			// fallthrough

		case CHAIN_DONE:
			f->done = 1;
			// fallthrough

		case CHAIN_NEXT:
			a->chain_flags &= ~CHAIN_FBACK;
			if (!(f->done && (a->chain_flags & CHAIN_FFIRST)))
				a->chain_flags &= ~CHAIN_FFIRST;
			in = out;
			i++;
			FF_ASSERT(i < a->ffilters.len);
			break;

		case CHAIN_FIN:
			rc = 0;
			goto end;
		case CHAIN_ERR:
			goto end;
		default:
			goto end;
		}
	}

end:
	return rc;
}
