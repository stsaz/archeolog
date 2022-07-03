/** archeolog: find start date
2022, Simon Zolin */

int startdate_open(struct archeolog *a)
{
	struct arlg_startdate *sd = &a->startdate;
	if (a->conf->start_date.sec == 0) {
		return CHAIN_DONE;
	}
	arlg_file_behaviour(a, FBEH_RANDOM);
	sd->end_off = a->file.size;
	sd->off_prev = (uint64)-1;
	if (a->conf->debug)
		sd->time_start = fftime_monotonic();
	ffstream_realloc(&sd->stm, a->conf->date_len);
	return CHAIN_READY;
}

void startdate_close(struct archeolog *a)
{
	struct arlg_startdate *sd = &a->startdate;
	ffstream_free(&sd->stm);
}

/** Find the first line at or after start-date.
Return enum CHAIN_R */
int startdate_find(struct archeolog *a, ffstr *in, ffstr *out)
{
	struct arlg_startdate *sd = &a->startdate;
	int r;
	uint64 line_off;
	int64 off;
	ffstr buf, view;
	enum { I_FIRST, I_GATHER, I_FINDLINE, I_CHECK, I_DONE };

	for (;;) {
		switch (sd->state) {
		case I_FIRST:
			goto seek;

		case I_GATHER:
			r = ffstream_gather_ref(&sd->stm, *in, a->conf->date_len, &buf);
			ffstr_shift(in, r);
			a->off += r;
			if (buf.len < a->conf->date_len) {
				if (sd->stm.ref.len != 0)
					continue; // store input data in buffer
				if (a->file.read_last && in->len == 0) {
					goto fin;
				}
				return CHAIN_PREV;
			}
			view = buf;
			sd->state = a->nxstate;
			continue;

		case I_FINDLINE:
			line_off = a->off - view.len;
			if (line_off != 0) {
				if (0 > (r = newline_find(&view))) {
					ffstream_reset(&sd->stm);
					sd->state = I_GATHER,  a->nxstate = I_FINDLINE;
					continue;
				}
				ffstr_shift(&view, r);
				ffstream_consume(&sd->stm, r);
				line_off = a->off - view.len;
			}
			if (line_off >= sd->end_off)
				goto fin;
			sd->state = I_CHECK;
			// fallthrough

		case I_CHECK: {
			fftime curdate;
			r = date_parse(a->conf, &view, &curdate);
			if (r < 0) {
				// not enough data
				sd->state = I_GATHER,  a->nxstate = I_CHECK;
				continue;
			} else if (r == 0) {
				// invalid timestamp
				ffstream_consume(&sd->stm, a->conf->date_len);
				sd->state = I_GATHER,  a->nxstate = I_FINDLINE;
				continue;
			}

			line_off = a->off - view.len;
			dbglog("check: %*s @%U[%U..%U](%U)"
				, (ffsize)r, view.ptr, line_off
				, sd->start_off, sd->end_off, sd->end_off - sd->start_off);

			if (fftime_cmp(&curdate, &a->conf->start_date) < 0) {
				sd->start_off = line_off + 1;
				sd->start_date = curdate;
				if (sd->seq_scan) {
					ffstream_consume(&sd->stm, a->conf->date_len);
					sd->state = I_GATHER,  a->nxstate = I_FINDLINE;
					continue;
				}
			} else {
				sd->end_off = line_off;
				sd->end_date = curdate;
				if (sd->seq_scan)
					goto done;
			}

			goto seek;
		}

		case I_DONE:
			arlg_file_behaviour(a, FBEH_SEQ);
			*out = sd->input;
			return CHAIN_DONE;
		}
	}

seek:
	if (sd->end_off - sd->start_off <= a->conf->read_chunk_size_small * 2) {
		sd->seq_scan = 1; // small window: start sequential search
		a->off = sd->start_off;
	} else {
		off = (sd->end_off - sd->start_off) / 2 - a->conf->read_chunk_size_small;
		a->off = sd->start_off + ffmax(off, 0);
	}

	if (a->off == sd->off_prev) {
		goto err;
	}
	sd->off_prev = a->off;

	ffstream_reset(&sd->stm);
	sd->state = I_GATHER,  a->nxstate = I_FINDLINE;
	arlg_file_seek(a, a->off);
	sd->njumps++;
	return CHAIN_PREV;

fin:
	if (sd->end_date.sec == 0)
		goto err;

done:
	if (a->conf->debug) {
		fftime t = fftime_monotonic();
		fftime_sub(&t, &sd->time_start);
		dbglog("found start-time line @%U in %uus, %u jumps"
			, a->off, fftime_usec(&t), sd->njumps);
	}
	ffstream_reset(&sd->stm);
	sd->state = I_DONE;
	sd->input = *in;
	a->off = line_off;
	*out = view;
	return CHAIN_NEXT;

err:
	errlog("can't find start-time line");
	return CHAIN_ERR;
}

struct filter_if filter_startdate = { "startdate", startdate_open, startdate_close, startdate_find };
