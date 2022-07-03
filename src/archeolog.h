/** archeolog: interfaces
2022, Simon Zolin */

#include <FFOS/process.h>
#include <FFOS/error.h>
#include <FFOS/time.h>

#define ARLG_VER  "0.2"

typedef unsigned short ushort;
typedef unsigned int uint;
typedef long long int64;
typedef unsigned long long uint64;

struct arlg_conf {
	char *filename;
	ffstr filter;
	fftime start_date, end_date;
	uint read_chunk_size_small, read_chunk_size_large;
	uint read_chunk_align;
	uint date_fmt;
	uint date_len;
	uint64 max_lines;
	ffbyte debug;
};
extern struct arlg_conf *gconf;

void conf_destroy(struct arlg_conf *conf);
int date_parse(struct arlg_conf *conf, const ffstr *s, fftime *t);
int conf_cmdline(struct arlg_conf *conf, int argc, const char **argv);


struct archeolog;
struct filter_if {
	const char *name;
	/** Return enum CHAIN_R */
	int (*open)(struct archeolog *a);
	void (*close)(struct archeolog *a);
	/** Return enum CHAIN_R */
	int (*process)(struct archeolog *a, ffstr *in, ffstr *out);
};
enum CHAIN_R {
	CHAIN_DONE, // remove filter (after its output is processed by next filters)
	CHAIN_SPLIT, // remove all previous filters and this filter
	CHAIN_PREV, // call previous filter
	CHAIN_NEXT, // call next filter
	CHAIN_ERR, // stop the chain with error
	CHAIN_FIN, // stop the chain
	CHAIN_READY, // filter open() OK
};

enum CHAIN_FLAGS {
	CHAIN_FBACK = 1, // moving backward through the chain
	CHAIN_FFIRST = 2, // filter is first in chain
};


enum FBEH_E {
	FBEH_SEQ = 1,
	FBEH_RANDOM = 2,
};

/**
flags: enum FBEH_E */
int arlg_file_behaviour(struct archeolog *a, uint flags);

void arlg_file_seek(struct archeolog *a, uint64 off);


void log_print(int level, const char *fmt, ...);

#define dbglog(fmt, ...) \
do { \
	if (gconf->debug) \
		log_print(1, "DBG:\t" fmt "\n", ##__VA_ARGS__); \
} while (0);

#define errlog(fmt, ...) \
	log_print(0, "ERR:\t" fmt "\n", ##__VA_ARGS__)
