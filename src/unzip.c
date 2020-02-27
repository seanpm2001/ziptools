/*
  unzip.c -- main routine for unzip
  Copyright (C) 2020 Dieter Baron and Thomas Klausner

  This file is part of ziptools.
  The authors can be contacted at <ziptools@nih.at>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
  3. The name of the author may not be used to endorse or promote
     products derived from this software without specific prior
     written permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"

#include <fnmatch.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zip.h>

#include "bitset.h"
#include "unzip.h"

char *usage = "Usage: %s [-hV] [-l|-t] zip-archive [file...]\n";

char help_head[] = PACKAGE " by Dieter Baron and Thomas Klausner\n\n";

char help[] = "\n"
    "  -h, --help              display this help message\n"
    "  -V, --version           display version number\n"
    "\nReport bugs to " PACKAGE_BUGREPORT ".\n";

char version_string[] = PACKAGE " " VERSION "\n"
    "Copyright (C) 2020 Dieter Baron and Thomas Klausner\n" PACKAGE " comes with ABSOLUTELY NO WARRANTY, to the extent permitted by law.\n";

#define GLOB_CHARS "*?["

#define OPTIONS "hltV"

enum { OPT_OPTIONS = 256 };

struct option options[] = {
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'V'},

    {"list", 0, 0, 'l'},
    {"test", 0, 0, 't'},

    {NULL, 0, 0, 0},
};

/*
Archive:  /archive/roms/redump/Fujitsu - FM-Towns/roms/Dragon Shock (Japan).zip
 Length   Method    Size  Cmpr    Date    Time   CRC-32   Name
--------  ------  ------- ---- ---------- ----- --------  ----
     894  Defl:N      156  83% 09-08-2019 05:45 d370a7a1  Dragon Shock (Japan).cue
 3015264  Defl:N  1245407  59% 09-02-2019 01:55 31baaa36  Dragon Shock (Japan) (Track 1).bin
 3128160  Defl:N  2026481  35% 09-02-2019 01:55 e0a7976f  Dragon Shock (Japan) (Track 2).bin
39085536  Defl:N 36358995   7% 09-02-2019 01:55 d6d669ef  Dragon Shock (Japan) (Track 3).bin
34927200  Defl:N 31940570   9% 09-02-2019 01:55 87a8024d  Dragon Shock (Japan) (Track 4).bin
51896880  Defl:N 47974762   8% 09-02-2019 01:55 05a4dc45  Dragon Shock (Japan) (Track 5).bin
39659424  Defl:N 36343104   8% 09-02-2019 01:55 dd7acae1  Dragon Shock (Japan) (Track 6).bin
59418576  Defl:N 51163543  14% 09-02-2019 01:55 e8e85b9f  Dragon Shock (Japan) (Track 7).bin
 3880800  Defl:N     3777 100% 12-15-2016 18:44 56903d96  Dragon Shock (Japan) (Track 8).bin
--------          -------  ---                            -------
235012734         207056795  12%                            9 files
*/

typedef struct pattern {
    const char *pattern;
    bool matched;
} pattern_t;

typedef enum { MODE_EXTRACT, MODE_LIST, MODE_TEST } runmode_t;

const char *mode_options = "none, -l, -t";

int
main(int argc, char **argv) {
    char c;
    int ret = 0;
    runmode_t runmode = MODE_EXTRACT;
    bitset_t *selected_files;
    zip_t *archive;

    setprogname(argv[0]);
    opterr = 0;
    while ((c = getopt_long(argc, argv, OPTIONS, options, 0)) != EOF) {
	switch (c) {
	case 'h':
	    fputs(help_head, stdout);
	    printf(usage, getprogname());
	    fputs(help, stdout);
	    exit(0);
	case 'l':
	    if (runmode != MODE_EXTRACT) {
		fprintf(stderr, "%s: only one mode selection allowed (%s)\n", getprogname(), mode_options);
		exit(1);
	    }
	    runmode = MODE_LIST;
	    break;
	case 't':
	    if (runmode != MODE_EXTRACT) {
		fprintf(stderr, "%s: only one mode selection allowed (%s)\n", getprogname(), mode_options);
		exit(1);
	    }
	    runmode = MODE_TEST;
	    break;
	case 'V':
	    fputs(version_string, stdout);
	    exit(0);
	default:
	    fprintf(stderr, usage, getprogname());
	    exit(1);
	}
    }
    if (optind >= argc) {
	/* die */
	exit(1);
    }
    if ((archive = zip_open(argv[optind], ZIP_RDONLY, NULL)) == NULL) {
	/* die */
	exit(1);
    }
    optind++;

    selected_files = bitset_new(zip_get_num_entries(archive, 0));

    if (argc == optind) {
	bitset_set_all(selected_files);
    }
    else {
	pattern_t *patterns;
	size_t npatterns = 0;

	if ((patterns = malloc(sizeof(*patterns)*(argc-optind))) == NULL) {
	    /* die */
	}
	for (size_t i = optind; i < argc; i++) {
	    if (strcspn(argv[i], GLOB_CHARS) == strlen(argv[i])) {
		/* unescape argv[i] */
		int64_t index = zip_name_locate(archive, argv[i], 0);
		if (index < 0) {
		    /* argv[i] not found */
		}
		else {
		    bitset_set(selected_files, (uint64_t)index);
		}
	    }
	    patterns[npatterns].pattern = argv[i];
	    patterns[npatterns].matched = false;
	    npatterns++;
	}

	for (size_t i = 0; i < zip_get_num_entries(archive, 0); i++) {
	    for (size_t j = 0; j < npatterns; j++) {
		const char *file_name = zip_get_name(archive, i, 0);
		if (fnmatch(patterns[j].pattern, file_name, 0) == 0) {
		    patterns[j].matched = true;
		    bitset_set(selected_files, i);
		    break;
		}
	    }
	}

	for (size_t i = 0; i < npatterns; i++) {
	    if (!patterns[i].matched) {
		/* pattern not matched */
	    }
	}

	free(patterns);
    }

    switch (runmode) {
    case MODE_EXTRACT:
/*	ret = extract_archive(archive, selected_files); */
	exit(1);
	break;
    case MODE_LIST:
	ret = list_archive(archive, selected_files);
	break;
    case MODE_TEST:
	ret = test_archive(archive, selected_files);
	exit(1);
	break;
    }

    if (zip_close(archive) < 0) {
	/* die */
	exit(1);
    }

    bitset_free(selected_files);
    return ret;
}
