#include "storage/books.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "storage/store.h"

LOG_MODULE_REGISTER(books, LOG_LEVEL_INF);

#define MNT "/lfs"
#define DIR MNT "/b"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);
static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_data,
	.storage_dev = (void *)FIXED_PARTITION_ID(littlefs_storage),
	.mnt_point = MNT,
};

/* Metadata file: id, size, crc, title. */
struct book_inf {
	uint32_t id;
	uint32_t size;
	uint32_t crc;
	char title[OP_BOOK_TITLE_MAX + 1];
} __packed;

static struct book_inf index_[BOOKS_MAX];
static uint16_t count;
static bool mounted;
static K_MUTEX_DEFINE(books_lock);

/* Partial file being written by the transfer thread. */
static struct fs_file_t part_file;
static bool part_open;
static struct book_inf part_inf;

/* Reader file. */
static struct fs_file_t read_file;
static bool read_open;

static void path(char *buf, size_t cap, uint32_t id, const char *ext)
{
	snprintf(buf, cap, DIR "/%08x.%s", id, ext);
}

static int write_inf(const struct book_inf *inf, const char *ext)
{
	struct fs_file_t f;
	char p[32];
	int err;

	path(p, sizeof(p), inf->id, ext);
	fs_file_t_init(&f);
	err = fs_open(&f, p, FS_O_CREATE | FS_O_WRITE);
	if (err) {
		return err;
	}
	(void)fs_truncate(&f, 0);
	err = fs_write(&f, inf, sizeof(*inf)) == sizeof(*inf) ? 0 : -EIO;
	fs_close(&f);
	return err;
}

static int read_inf(uint32_t id, const char *ext, struct book_inf *inf)
{
	struct fs_file_t f;
	char p[32];
	int err;

	path(p, sizeof(p), id, ext);
	fs_file_t_init(&f);
	err = fs_open(&f, p, FS_O_READ);
	if (err) {
		return err;
	}
	err = fs_read(&f, inf, sizeof(*inf)) == sizeof(*inf) ? 0 : -EIO;
	fs_close(&f);
	return err;
}

static void scan(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;

	k_mutex_lock(&books_lock, K_FOREVER);
	count = 0;
	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, DIR) == 0) {
		while (fs_readdir(&dir, &ent) == 0 && ent.name[0] && count < BOOKS_MAX) {
			size_t n = strlen(ent.name);

			if (n == 12 && strcmp(ent.name + 8, ".opb") == 0) {
				uint32_t id = (uint32_t)strtoul(ent.name, NULL, 16);

				if (read_inf(id, "inf", &index_[count]) == 0 && index_[count].id == id) {
					index_[count].size = (uint32_t)ent.size;
					count++;
				}
			}
		}
		fs_closedir(&dir);
	}
	k_mutex_unlock(&books_lock);
	LOG_INF("%u books", count);
}

int books_init(void)
{
	int err = fs_mount(&lfs_mnt);

	if (err) {
		LOG_ERR("book storage not available (%d)", err);
		return err;
	}
	mounted = true;
	err = fs_mkdir(DIR);
	if (err && err != -EEXIST) {
		LOG_ERR("mkdir failed (%d)", err);
	}
	scan();
	return 0;
}

bool books_ready(void)
{
	return mounted;
}

uint16_t books_count(void)
{
	return count;
}

bool books_get(uint16_t i, struct op_book_info *out)
{
	bool ok = false;

	k_mutex_lock(&books_lock, K_FOREVER);
	if (i < count) {
		out->book_id = index_[i].id;
		out->size = index_[i].size;
		memcpy(out->title, index_[i].title, sizeof(out->title));
		ok = true;
	}
	k_mutex_unlock(&books_lock);
	if (ok) {
		out->position = store_get_position(out->book_id);
	}
	return ok;
}

static void unlink_all(uint32_t id)
{
	char p[32];

	path(p, sizeof(p), id, "opb");
	(void)fs_unlink(p);
	path(p, sizeof(p), id, "prt");
	(void)fs_unlink(p);
	path(p, sizeof(p), id, "inf");
	(void)fs_unlink(p);
	path(p, sizeof(p), id, "pin");
	(void)fs_unlink(p);
}

int books_delete(uint32_t id)
{
	bool found = false;

	if (!mounted) {
		return -ENODEV;
	}
	k_mutex_lock(&books_lock, K_FOREVER);
	for (int i = 0; i < count; i++) {
		if (index_[i].id == id) {
			found = true;
		}
	}
	k_mutex_unlock(&books_lock);
	unlink_all(id);
	store_delete_position(id);
	scan();
	return found ? 0 : -ENOENT;
}

int books_erase_all(void)
{
	if (!mounted) {
		return -ENODEV;
	}
	books_close();
	while (count) {
		books_delete(index_[0].id);
	}
	return 0;
}

void books_storage(uint32_t *free_kib, uint32_t *total_kib)
{
	struct fs_statvfs st;

	*free_kib = 0;
	*total_kib = 0;
	if (mounted && fs_statvfs(MNT, &st) == 0) {
		*free_kib = (uint32_t)((uint64_t)st.f_bfree * st.f_frsize / 1024);
		*total_kib = (uint32_t)((uint64_t)st.f_blocks * st.f_frsize / 1024);
	}
}

/* ---- transfer io ----------------------------------------------------------------- */

static void close_part(void)
{
	if (part_open) {
		fs_close(&part_file);
		part_open = false;
	}
}

static uint8_t io_open(void *ctx, uint32_t id, uint32_t total, uint32_t crc, const char *title,
		       uint32_t *resume_at)
{
	char p[32];
	struct book_inf old;
	uint32_t free_kib, total_kib;
	int err;

	ARG_UNUSED(ctx);
	if (!mounted) {
		return OP_XFER_STORAGE;
	}
	close_part();
	*resume_at = 0;
	path(p, sizeof(p), id, "prt");

	bool resume = read_inf(id, "pin", &old) == 0 && old.id == id && old.size == total &&
		      old.crc == crc;

	if (!resume) {
		/* A different book with this id: start again. */
		unlink_all(id);
		books_storage(&free_kib, &total_kib);
		if ((uint64_t)total / 1024 + 64 > free_kib) {
			return OP_XFER_NO_SPACE;
		}
		memset(&part_inf, 0, sizeof(part_inf));
		part_inf.id = id;
		part_inf.size = total;
		part_inf.crc = crc;
		strncpy(part_inf.title, title, OP_BOOK_TITLE_MAX);
		if (write_inf(&part_inf, "pin")) {
			return OP_XFER_STORAGE;
		}
	} else {
		part_inf = old;
	}
	fs_file_t_init(&part_file);
	err = fs_open(&part_file, p, FS_O_CREATE | FS_O_RDWR);
	if (err) {
		return OP_XFER_STORAGE;
	}
	part_open = true;
	if (resume) {
		off_t end = fs_seek(&part_file, 0, FS_SEEK_END) == 0 ? fs_tell(&part_file) : 0;

		*resume_at = end > 0 ? (uint32_t)end : 0;
	} else {
		(void)fs_truncate(&part_file, 0);
	}
	LOG_INF("book %08x: receiving from %u of %u", id, *resume_at, total);
	return OP_XFER_OK;
}

static int io_write(void *ctx, uint32_t id, uint32_t off, const uint8_t *data, size_t len)
{
	ARG_UNUSED(ctx);
	if (!part_open || id != part_inf.id) {
		return -EINVAL;
	}
	if (fs_seek(&part_file, off, FS_SEEK_SET)) {
		return -EIO;
	}
	return fs_write(&part_file, data, len) == (ssize_t)len ? 0 : -EIO;
}

static int io_file_crc(void *ctx, uint32_t id, uint32_t *crc)
{
	uint8_t buf[256];
	uint32_t c = 0;
	ssize_t n;

	ARG_UNUSED(ctx);
	if (!part_open || id != part_inf.id) {
		return -EINVAL;
	}
	(void)fs_sync(&part_file);
	if (fs_seek(&part_file, 0, FS_SEEK_SET)) {
		return -EIO;
	}
	while ((n = fs_read(&part_file, buf, sizeof(buf))) > 0) {
		c = op_crc32(c, buf, (size_t)n);
	}
	*crc = c;
	return n < 0 ? -EIO : 0;
}

static int io_commit(void *ctx, uint32_t id)
{
	char from[32], to[32];
	int err;

	ARG_UNUSED(ctx);
	close_part();
	path(from, sizeof(from), id, "prt");
	path(to, sizeof(to), id, "opb");
	err = write_inf(&part_inf, "inf");
	if (!err) {
		err = fs_rename(from, to);
	}
	path(from, sizeof(from), id, "pin");
	(void)fs_unlink(from);
	store_delete_position(id);
	scan();
	LOG_INF("book %08x stored (%d)", id, err);
	return err;
}

static void io_discard(void *ctx, uint32_t id)
{
	char p[32];

	ARG_UNUSED(ctx);
	close_part();
	path(p, sizeof(p), id, "prt");
	(void)fs_unlink(p);
	path(p, sizeof(p), id, "pin");
	(void)fs_unlink(p);
}

static const struct xfer_io io = {
	.ctx = NULL,
	.open = io_open,
	.write = io_write,
	.file_crc = io_file_crc,
	.commit = io_commit,
	.discard = io_discard,
};

const struct xfer_io *books_xfer_io(void)
{
	return &io;
}

/* ---- reader --------------------------------------------------------------------- */

int books_open(uint32_t id, uint32_t *size)
{
	char p[32];
	struct fs_dirent ent;
	int err;

	books_close();
	path(p, sizeof(p), id, "opb");
	err = fs_stat(p, &ent);
	if (err) {
		return err;
	}
	fs_file_t_init(&read_file);
	err = fs_open(&read_file, p, FS_O_READ);
	if (err) {
		return err;
	}
	read_open = true;
	*size = (uint32_t)ent.size;
	return 0;
}

int books_read(void *ctx, uint32_t off, uint8_t *buf, size_t len)
{
	ARG_UNUSED(ctx);
	if (!read_open || fs_seek(&read_file, off, FS_SEEK_SET)) {
		return -EIO;
	}
	return (int)fs_read(&read_file, buf, len);
}

void books_close(void)
{
	if (read_open) {
		fs_close(&read_file);
		read_open = false;
	}
}
