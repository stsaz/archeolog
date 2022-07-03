/** archeolog: file input
2022, Simon Zolin */

#include <FFOS/file.h>
#include <FFOS/error.h>

int file_open(struct archeolog *a)
{
	struct arlg_file *f = &a->file;
	f->fd = FFFILE_NULL;
	f->read_chunk_size = a->conf->read_chunk_size_large;
	f->seek = (uint64)-1;

	if (FFFILE_NULL == (f->fd = fffile_open(a->conf->filename, FFFILE_READONLY | FFFILE_NOATIME))) {
		errlog("file open: %s: %E", a->conf->filename, fferr_last());
		return CHAIN_ERR;
	}

	f->size = fffile_size(f->fd);
	if ((int64)f->size < 0) {
		errlog("file size: %E", fferr_last());
		return CHAIN_ERR;
	}

	dbglog("file open: %s (%U)", a->conf->filename, f->size);
	if (0 != fcache_init(&f->cache, 1, a->conf->read_chunk_size_large, a->conf->read_chunk_align))
		return CHAIN_ERR;
	return CHAIN_NEXT;
}

void file_close(struct archeolog *a)
{
	struct arlg_file *f = &a->file;
	if (f->fd != FFFILE_NULL) {
		fffile_close(f->fd);
		f->fd = FFFILE_NULL;
	}
	fcache_destroy(&f->cache);
	dbglog("file: cache-hits:%U  cache-miss:%U"
		, f->cache.hits, f->cache.misses);
}

/** Return enum LGV_R */
int file_read(struct archeolog *a, ffstr *in, ffstr *out)
{
	struct arlg_file *f = &a->file;
	if (f->seek != (uint64)-1) {
		f->cur = f->seek;
		dbglog("file seek: %U", f->seek);
		f->seek = (uint64)-1;
	} else if (f->read_last) {
		// next filters didn't ask for new data
		return CHAIN_DONE;
	}

	struct fcache_buf *b;
	if (NULL != (b = fcache_find(&f->cache, f->cur))) {
		dbglog("cache hit: %L @%U", b->len, b->off);
		ffstr_setstr(out, b);
		ffstr_shift(out, f->cur - b->off);
		f->cur += out->len;
		return CHAIN_NEXT;
	}

	b = fcache_nextbuf(&f->cache);
	b->off = ffint_align_floor2(f->cur, a->conf->read_chunk_align);
	fftime start, end;
	if (a->conf->debug)
		start = fftime_monotonic();
	int r = fffile_readat(f->fd, b->ptr, f->read_chunk_size, b->off);
	if (r <= 0) {
		if (r < 0)
			errlog("file read: %E", fferr_last());
		return CHAIN_ERR;
	}
	if (a->conf->debug) {
		end = fftime_monotonic();
		fftime_sub(&end, &start);
	}
	b->len = r;
	f->read_last = (r < f->read_chunk_size);
	dbglog("file read: %u @%U(%u%%)  last:%u  %uus"
		, b->len, b->off, (int)(b->off * 100 / f->size), f->read_last, fftime_usec(&end));
	ffstr_setstr(out, b);
	ffstr_shift(out, f->cur - b->off);
	f->cur = b->off + r;
	return CHAIN_NEXT;
}

int arlg_file_behaviour(struct archeolog *a, uint flags)
{
	struct arlg_file *f = &a->file;
	switch (flags) {
	case FBEH_SEQ:
		dbglog("file: sequential access");
		a->file.read_chunk_size = a->conf->read_chunk_size_large;
		if (0 != fffile_readahead(f->fd, f->size))
			dbglog("file read ahead: %E", fferr_last());
		break;

	case FBEH_RANDOM:
		dbglog("file: random access");
		f->read_chunk_size = a->conf->read_chunk_size_small;
		break;
	}
	return 0;
}

void arlg_file_seek(struct archeolog *a, uint64 off)
{
	struct arlg_file *f = &a->file;
	f->seek = off;
}

struct filter_if filter_file = { "file", file_open, file_close, file_read };
