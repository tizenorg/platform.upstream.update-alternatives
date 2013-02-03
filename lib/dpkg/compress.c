/*
 * libdpkg - Debian packaging suite library routines
 * compress.c - compression support functions
 *
 * Copyright © 2000 Wichert Akkerman <wakkerma@debian.org>
 * Copyright © 2004 Scott James Remnant <scott@netsplit.com>
 * Copyright © 2006-2010 Guillem Jover <guillem@debian.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <compat.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef WITH_ZLIB
#include <zlib.h>
#endif
#ifdef WITH_BZ2
#include <bzlib.h>
#endif

#include <dpkg/i18n.h>
#include <dpkg/dpkg.h>
#include <dpkg/error.h>
#include <dpkg/varbuf.h>
#include <dpkg/fdio.h>
#include <dpkg/buffer.h>
#include <dpkg/command.h>
#include <dpkg/compress.h>
#include <dpkg/subproc.h>

static void DPKG_ATTR_SENTINEL
fd_fd_filter(int fd_in, int fd_out, const char *desc, const char *file, ...)
{
	va_list args;
	struct command cmd;
	pid_t pid;

	pid = subproc_fork();
	if (pid == 0) {
		if (fd_in != 0) {
			m_dup2(fd_in, 0);
			close(fd_in);
		}
		if (fd_out != 1) {
			m_dup2(fd_out, 1);
			close(fd_out);
		}

		command_init(&cmd, file, desc);
		command_add_arg(&cmd, file);
		va_start(args, file);
		command_add_argv(&cmd, args);
		va_end(args);

		command_exec(&cmd);
	}
	subproc_wait_check(pid, desc, 0);
}

struct compressor {
	const char *name;
	const char *extension;
	int default_level;
	bool (*check_params)(struct compress_params *params,
	                     struct dpkg_error *err);
	void (*fixup_params)(struct compress_params *params);
	void (*compress)(int fd_in, int fd_out, struct compress_params *params,
	                 const char *desc);
	void (*decompress)(int fd_in, int fd_out, const char *desc);
};

/*
 * No compressor (pass-through).
 */

static bool
check_none_params(struct compress_params *params, struct dpkg_error *err)
{
	if (params->strategy) {
		dpkg_put_warn(err, _("unknown compression strategy"));
		return false;
	}

	return true;
}

static void
fixup_none_params(struct compress_params *params)
{
}

static void
decompress_none(int fd_in, int fd_out, const char *desc)
{
	fd_fd_copy(fd_in, fd_out, -1, _("%s: decompression"), desc);
}

static void
compress_none(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	fd_fd_copy(fd_in, fd_out, -1, _("%s: compression"), desc);
}

static const struct compressor compressor_none = {
	.name = "none",
	.extension = "",
	.default_level = 0,
	.check_params = check_none_params,
	.fixup_params = fixup_none_params,
	.compress = compress_none,
	.decompress = decompress_none,
};

/*
 * Gzip compressor.
 */

#define GZIP		"gzip"

static void
fixup_gzip_params(struct compress_params *params)
{
	/* Normalize compression level. */
	if (params->level == 0)
		params->type = compressor_type_none;
}

#ifdef WITH_ZLIB
static void
decompress_gzip(int fd_in, int fd_out, const char *desc)
{
	char buffer[DPKG_BUFFER_SIZE];
	gzFile gzfile = gzdopen(fd_in, "r");

	if (gzfile == NULL)
		ohshit(_("%s: error binding input to gzip stream"), desc);

	for (;;) {
		int actualread, actualwrite;

		actualread = gzread(gzfile, buffer, sizeof(buffer));
		if (actualread < 0) {
			int z_errnum = 0;
			const char *errmsg = gzerror(gzfile, &z_errnum);

			if (z_errnum == Z_ERRNO)
				errmsg = strerror(errno);
			ohshit(_("%s: internal gzip read error: '%s'"), desc,
			       errmsg);
		}
		if (actualread == 0) /* EOF. */
			break;

		actualwrite = fd_write(fd_out, buffer, actualread);
		if (actualwrite != actualread)
			ohshite(_("%s: internal gzip write error"), desc);
	}

	if (close(fd_out))
		ohshite(_("%s: internal gzip write error"), desc);
}

static void
compress_gzip(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	char buffer[DPKG_BUFFER_SIZE];
	char combuf[6];
	int z_errnum;
	gzFile gzfile;

	snprintf(combuf, sizeof(combuf), "w%d", params->level);
	gzfile = gzdopen(fd_out, combuf);
	if (gzfile == NULL)
		ohshit(_("%s: error binding output to gzip stream"), desc);

	for (;;) {
		int actualread, actualwrite;

		actualread = fd_read(fd_in, buffer, sizeof(buffer));
		if (actualread < 0)
			ohshite(_("%s: internal gzip read error"), desc);
		if (actualread == 0) /* EOF. */
			break;

		actualwrite = gzwrite(gzfile, buffer, actualread);
		if (actualwrite != actualread) {
			const char *errmsg = gzerror(gzfile, &z_errnum);

			if (z_errnum == Z_ERRNO)
				errmsg = strerror(errno);
			ohshit(_("%s: internal gzip write error: '%s'"), desc,
			       errmsg);
		}
	}

	z_errnum = gzclose(gzfile);
	if (z_errnum) {
		const char *errmsg;

		if (z_errnum == Z_ERRNO)
			errmsg = strerror(errno);
		else
			errmsg = zError(z_errnum);
		ohshit(_("%s: internal gzip write error: %s"), desc, errmsg);
	}
}
#else
static void
decompress_gzip(int fd_in, int fd_out, const char *desc)
{
	fd_fd_filter(fd_in, fd_out, desc, GZIP, "-dc", NULL);
}

static void
compress_gzip(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	char combuf[6];

	snprintf(combuf, sizeof(combuf), "-c%d", params->level);
	fd_fd_filter(fd_in, fd_out, desc, GZIP, combuf, NULL);
}
#endif

static const struct compressor compressor_gzip = {
	.name = "gzip",
	.extension = ".gz",
	.default_level = 9,
	.check_params = check_none_params,
	.fixup_params = fixup_gzip_params,
	.compress = compress_gzip,
	.decompress = decompress_gzip,
};

/*
 * Bzip2 compressor.
 */

#define BZIP2		"bzip2"

static void
fixup_bzip2_params(struct compress_params *params)
{
	/* Normalize compression level. */
	if (params->level == 0)
		params->level = 1;
}

#ifdef WITH_BZ2
static void
decompress_bzip2(int fd_in, int fd_out, const char *desc)
{
	char buffer[DPKG_BUFFER_SIZE];
	BZFILE *bzfile = BZ2_bzdopen(fd_in, "r");

	if (bzfile == NULL)
		ohshit(_("%s: error binding input to bzip2 stream"), desc);

	for (;;) {
		int actualread, actualwrite;

		actualread = BZ2_bzread(bzfile, buffer, sizeof(buffer));
		if (actualread < 0) {
			int bz_errnum = 0;
			const char *errmsg = BZ2_bzerror(bzfile, &bz_errnum);

			if (bz_errnum == BZ_IO_ERROR)
				errmsg = strerror(errno);
			ohshit(_("%s: internal bzip2 read error: '%s'"), desc,
			       errmsg);
		}
		if (actualread == 0) /* EOF. */
			break;

		actualwrite = fd_write(fd_out, buffer, actualread);
		if (actualwrite != actualread)
			ohshite(_("%s: internal bzip2 write error"), desc);
	}

	if (close(fd_out))
		ohshite(_("%s: internal bzip2 write error"), desc);
}

static void
compress_bzip2(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	char buffer[DPKG_BUFFER_SIZE];
	char combuf[6];
	int bz_errnum;
	BZFILE *bzfile;

	snprintf(combuf, sizeof(combuf), "w%d", params->level);
	bzfile = BZ2_bzdopen(fd_out, combuf);
	if (bzfile == NULL)
		ohshit(_("%s: error binding output to bzip2 stream"), desc);

	for (;;) {
		int actualread, actualwrite;

		actualread = fd_read(fd_in, buffer, sizeof(buffer));
		if (actualread < 0)
			ohshite(_("%s: internal bzip2 read error"), desc);
		if (actualread == 0) /* EOF. */
			break;

		actualwrite = BZ2_bzwrite(bzfile, buffer, actualread);
		if (actualwrite != actualread) {
			const char *errmsg = BZ2_bzerror(bzfile, &bz_errnum);

			if (bz_errnum == BZ_IO_ERROR)
				errmsg = strerror(errno);
			ohshit(_("%s: internal bzip2 write error: '%s'"), desc,
			       errmsg);
		}
	}

	BZ2_bzWriteClose(&bz_errnum, bzfile, 0, NULL, NULL);
	if (bz_errnum != BZ_OK) {
		const char *errmsg = _("unexpected bzip2 error");

		if (bz_errnum == BZ_IO_ERROR)
			errmsg = strerror(errno);
		ohshit(_("%s: internal bzip2 write error: '%s'"), desc,
		       errmsg);
	}

	/* Because BZ2_bzWriteClose has done a fflush on the file handle,
	 * doing a close on the file descriptor associated with it should
	 * be safe™. */
	if (close(fd_out))
		ohshite(_("%s: internal bzip2 write error"), desc);
}
#else
static void
decompress_bzip2(int fd_in, int fd_out, const char *desc)
{
	fd_fd_filter(fd_in, fd_out, desc, BZIP2, "-dc", NULL);
}

static void
compress_bzip2(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	char combuf[6];

	snprintf(combuf, sizeof(combuf), "-c%d", params->level);
	fd_fd_filter(fd_in, fd_out, desc, BZIP2, combuf, NULL);
}
#endif

static const struct compressor compressor_bzip2 = {
	.name = "bzip2",
	.extension = ".bz2",
	.default_level = 9,
	.check_params = check_none_params,
	.fixup_params = fixup_bzip2_params,
	.compress = compress_bzip2,
	.decompress = decompress_bzip2,
};

/*
 * Xz compressor.
 */

#define XZ		"xz"

static bool
check_xz_params(struct compress_params *params, struct dpkg_error *err)
{
	if (params->strategy && strcmp(params->strategy, "extreme") != 0) {
		dpkg_put_warn(err, _("unknown compression strategy"));
		return false;
	}

	return true;
}

static void
decompress_xz(int fd_in, int fd_out, const char *desc)
{
	fd_fd_filter(fd_in, fd_out, desc, XZ, "-dc", NULL);
}

static void
compress_xz(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	char combuf[6];
	const char *strategy;

	if (params->strategy && strcmp(params->strategy, "extreme") == 0)
		strategy = "-e";
	else
		strategy = NULL;

	snprintf(combuf, sizeof(combuf), "-c%d", params->level);
	fd_fd_filter(fd_in, fd_out, desc, XZ, combuf, strategy, NULL);
}

static const struct compressor compressor_xz = {
	.name = "xz",
	.extension = ".xz",
	.default_level = 6,
	.check_params = check_xz_params,
	.fixup_params = fixup_none_params,
	.compress = compress_xz,
	.decompress = decompress_xz,
};

/*
 * Lzma compressor.
 */

static void
decompress_lzma(int fd_in, int fd_out, const char *desc)
{
	fd_fd_filter(fd_in, fd_out, desc, XZ, "-dc", "--format=lzma", NULL);
}

static void
compress_lzma(int fd_in, int fd_out, struct compress_params *params, const char *desc)
{
	char combuf[6];

	snprintf(combuf, sizeof(combuf), "-c%d", params->level);
	fd_fd_filter(fd_in, fd_out, desc, XZ, combuf, "--format=lzma", NULL);
}

static const struct compressor compressor_lzma = {
	.name = "lzma",
	.extension = ".lzma",
	.default_level = 6,
	.check_params = check_none_params,
	.fixup_params = fixup_none_params,
	.compress = compress_lzma,
	.decompress = decompress_lzma,
};

/*
 * Generic compressor filter.
 */

static const struct compressor *compressor_array[] = {
	[compressor_type_none] = &compressor_none,
	[compressor_type_gzip] = &compressor_gzip,
	[compressor_type_xz] = &compressor_xz,
	[compressor_type_bzip2] = &compressor_bzip2,
	[compressor_type_lzma] = &compressor_lzma,
};

static const struct compressor *
compressor(enum compressor_type type)
{
	const enum compressor_type max_type = array_count(compressor_array);

	if (type < 0 || type >= max_type)
		internerr("compressor_type %d is out of range", type);

	return compressor_array[type];
}

const char *
compressor_get_extension(enum compressor_type type)
{
	return compressor(type)->extension;
}

enum compressor_type
compressor_find_by_name(const char *name)
{
	size_t i;

	for (i = 0; i < array_count(compressor_array); i++)
		if (strcmp(compressor_array[i]->name, name) == 0)
			return i;

	return compressor_type_unknown;
}

enum compressor_type
compressor_find_by_extension(const char *extension)
{
	size_t i;

	for (i = 0; i < array_count(compressor_array); i++)
		if (strcmp(compressor_array[i]->extension, extension) == 0)
			return i;

	return compressor_type_unknown;
}

bool
compressor_check_params(struct compress_params *params, struct dpkg_error *err)
{
	return compressor(params->type)->check_params(params, err);
}

static void
compressor_fixup_params(struct compress_params *params)
{
	compressor(params->type)->fixup_params(params);
}

void
decompress_filter(enum compressor_type type, int fd_in, int fd_out,
                  const char *desc_fmt, ...)
{
	va_list args;
	struct varbuf desc = VARBUF_INIT;

	va_start(args, desc_fmt);
	varbuf_vprintf(&desc, desc_fmt, args);
	va_end(args);

	compressor(type)->decompress(fd_in, fd_out, desc.buf);
}

void
compress_filter(struct compress_params *params, int fd_in, int fd_out,
                const char *desc_fmt, ...)
{
	va_list args;
	struct varbuf desc = VARBUF_INIT;

	va_start(args, desc_fmt);
	varbuf_vprintf(&desc, desc_fmt, args);
	va_end(args);

	compressor_fixup_params(params);

	if (params->level < 0)
		params->level = compressor(params->type)->default_level;

	compressor(params->type)->compress(fd_in, fd_out, params, desc.buf);
}
