/*
 *   Software Updater - client side
 *
 *      Copyright © 2012-2016 Intel Corporation.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2 or later of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Authors:
 *         Arjan van de Ven <arjan@linux.intel.com>
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "swupd.h"

/* this function attempts to download the latest server version string file from
 * the preferred server to a memory buffer, returning either a negative integer
 * error code or >= 0 representing the server version */
int get_latest_version(void)
{
	char *url = NULL;
	char *path = NULL;
	int ret = 0;
	struct version_container tmp_version = { 0 };
	static int cached_version = -1;

	if (cached_version > 0) {
		return cached_version;
	}

	tmp_version.version = calloc(LINE_MAX, 1);
	if (tmp_version.version == NULL) {
		abort();
	}

	string_or_die(&url, "%s/version/format%s/latest", version_url, format_string);

	string_or_die(&path, "%s/server_version", state_dir);

	unlink(path);

	ret = swupd_curl_get_file(url, path, NULL, &tmp_version, false);
	if (ret) {
		goto out;
	} else {
		ret = strtol(tmp_version.version, NULL, 10);
	}

out:
	free_string(&path);
	free_string(&url);
	free_string(&tmp_version.version);
	cached_version = ret;
	return ret;
}

int get_current_version(char *path_prefix)
{
	char line[LINE_MAX];
	FILE *file;
	int v = -1;
	char *buildstamp;
	char *src, *dest;

	string_or_die(&buildstamp, "%s/usr/lib/os-release", path_prefix);
	file = fopen(buildstamp, "rm");
	if (!file) {
		free_string(&buildstamp);
		string_or_die(&buildstamp, "%s/etc/os-release", path_prefix);
		file = fopen(buildstamp, "rm");
		if (!file) {
			free_string(&buildstamp);
			return v;
		}
	}

	while (!feof(file)) {
		line[0] = 0;
		if (fgets(line, LINE_MAX, file) == NULL) {
			break;
		}
		if (strncmp(line, "VERSION_ID=", 11) == 0) {
			src = &line[11];
			/* Drop quotes and newline in value */
			dest = src;
			while (*src) {
				if (*src == '\'' || *src == '"' || *src == '\n') {
					++src;
				} else {
					*dest = *src;
					++dest;
					++src;
				}
			}
			*dest = 0;

			v = strtoull(&line[11], NULL, 10);
			break;
		}
	}

	free_string(&buildstamp);
	fclose(file);
	return v;
}

void read_versions(int *current_version,
		   int *server_version,
		   char *path_prefix)
{
	*current_version = get_current_version(path_prefix);

	*server_version = get_latest_version();
}

int check_versions(int *current_version,
		   int *server_version,
		   int requested_version,
		   char *path_prefix)
{

	if (swupd_curl_check_network()) {
		return -1;
	}

	read_versions(current_version, server_version, path_prefix);

	if (*current_version < 0) {
		fprintf(stderr, "Error: Unable to determine current OS version\n");
		return -1;
	}
	if (*current_version == 0) {
		fprintf(stderr, "Update from version 0 not supported yet.\n");
		return -1;
	}
	if (SWUPD_VERSION_IS_DEVEL(*current_version) || SWUPD_VERSION_IS_RESVD(*current_version)) {
		fprintf(stderr, "Update of dev build not supported %d\n", *current_version);
		return -1;
	}
	swupd_curl_set_current_version(*current_version);

	/* set preferred version and content server urls */
	if (*current_version < 0) {
		return -1;
	}

	if (requested_version != -1) {
		if (requested_version <= *current_version) {
			fprintf(stderr, "Requested version for update (%d) must be greater than current version (%d)\n",
				requested_version, *current_version);
			return -1;
		}
		if (requested_version < *server_version) {
			*server_version = requested_version;
		}
	}

	swupd_curl_set_requested_version(*server_version);

	return 0;
}

int read_mix_version_file(char *filename, char *path_prefix)
{
	char line[LINE_MAX];
	FILE *file;
	int v = -1;
	char *buildstamp;

	string_or_die(&buildstamp, "%s%s", path_prefix, filename);
	file = fopen(buildstamp, "rm");
	if (!file) {
		free_string(&buildstamp);
		return v;
	}

	while (!feof(file)) {
		line[0] = 0;
		if (fgets(line, LINE_MAX, file) == NULL) {
			break;
		}

		/* Drop newline in value */
		char *c = strchr(line, '\n');
		if (c) {
			*c = '\0';
		}

		v = strtoull(line, NULL, 10);
	}
	free_string(&buildstamp);
	fclose(file);
	return v;
}

void check_mix_versions(int *current_version, int *server_version, char *path_prefix)
{
	*current_version = read_mix_version_file("/usr/share/clear/version", path_prefix);
	*server_version = read_mix_version_file(MIX_STATE_DIR "version/format1/latest", path_prefix);
}
int update_device_latest_version(int version)
{
	FILE *file = NULL;
	char *path = NULL;

	string_or_die(&path, "%s/version", state_dir);
	file = fopen(path, "w");
	if (!file) {
		free_string(&path);
		return -1;
	}

	fprintf(file, "%i\n", version);
	fflush(file);
	fdatasync(fileno(file));
	fclose(file);
	free_string(&path);
	return 0;
}
