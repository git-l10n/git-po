/*
 * Copyright (c) 2011, Google Inc.
 */
#include "bulk-checkin.h"
#include "csum-file.h"
#include "pack.h"

#ifndef CHUNK_MAX
#define CHUNK_MAX 2000
#endif

static int pack_compression_level = Z_DEFAULT_COMPRESSION;

static struct bulk_checkin_state {
	unsigned plugged:1;

	char *pack_tmp_name;
	struct sha1file *f;
	off_t offset;
	struct pack_idx_option pack_idx_opts;

	struct pack_idx_entry **written;
	uint32_t alloc_written;
	uint32_t nr_written;
} state;

static void finish_bulk_checkin(struct bulk_checkin_state *state)
{
	unsigned char sha1[20];
	char packname[PATH_MAX];
	int i;

	if (!state->f)
		return;

	if (state->nr_written == 0) {
		close(state->f->fd);
		unlink(state->pack_tmp_name);
		goto clear_exit;
	} else if (state->nr_written == 1) {
		sha1close(state->f, sha1, CSUM_FSYNC);
	} else {
		int fd = sha1close(state->f, sha1, 0);
		fixup_pack_header_footer(fd, sha1, state->pack_tmp_name,
					 state->nr_written, sha1,
					 state->offset);
		close(fd);
	}

	sprintf(packname, "%s/pack/pack-", get_object_directory());
	finish_tmp_packfile(packname, state->pack_tmp_name,
			    state->written, state->nr_written,
			    &state->pack_idx_opts, sha1);
	for (i = 0; i < state->nr_written; i++)
		free(state->written[i]);

clear_exit:
	free(state->written);
	memset(state, 0, sizeof(*state));

	/* Make objects we just wrote available to ourselves */
	reprepare_packed_git();
}

static int already_written(struct bulk_checkin_state *state, unsigned char sha1[])
{
	int i;

	/* The object may already exist in the repository */
	if (has_sha1_file(sha1))
		return 1;

	/* Might want to keep the list sorted */
	for (i = 0; i < state->nr_written; i++)
		if (!hashcmp(state->written[i]->sha1, sha1))
			return 1;

	/* This is a new object we need to keep */
	return 0;
}

struct chunk_ctx {
	struct chunk_ctx *up;
	git_SHA_CTX ctx;
};

static void chunk_SHA1_Update(struct chunk_ctx *ctx,
			      const unsigned char *buf, size_t size)
{
	while (ctx) {
		git_SHA1_Update(&ctx->ctx, buf, size);
		ctx = ctx->up;
	}
}

/*
 * Read the contents from fd for size bytes, streaming it to the
 * packfile in state while updating the hash in ctx. Signal a failure
 * by returning a negative value when the resulting pack would exceed
 * the pack size limit and this is not the first object in the pack,
 * so that the caller can discard what we wrote from the current pack
 * by truncating it and opening a new one. The caller will then call
 * us again after rewinding the input fd.
 *
 * The already_hashed_to pointer is kept untouched by the caller to
 * make sure we do not hash the same byte when we are called
 * again. This way, the caller does not have to checkpoint its hash
 * status before calling us just in case we ask it to call us again
 * with a new pack.
 */
static int stream_to_pack(struct bulk_checkin_state *state,
			  struct chunk_ctx *ctx, off_t *already_hashed_to,
			  int fd, size_t size, enum object_type type,
			  const char *path, unsigned flags)
{
	git_zstream s;
	unsigned char obuf[16384];
	unsigned hdrlen;
	int status = Z_OK;
	int write_object = (flags & HASH_WRITE_OBJECT);
	off_t offset = 0;

	memset(&s, 0, sizeof(s));
	git_deflate_init(&s, pack_compression_level);

	hdrlen = encode_in_pack_object_header(type, size, obuf);
	s.next_out = obuf + hdrlen;
	s.avail_out = sizeof(obuf) - hdrlen;

	while (status != Z_STREAM_END) {
		unsigned char ibuf[16384];

		if (size && !s.avail_in) {
			ssize_t rsize = size < sizeof(ibuf) ? size : sizeof(ibuf);
			if (xread(fd, ibuf, rsize) != rsize)
				die("failed to read %d bytes from '%s'",
				    (int)rsize, path);
			offset += rsize;
			if (*already_hashed_to < offset) {
				size_t hsize = offset - *already_hashed_to;
				if (rsize < hsize)
					hsize = rsize;
				if (hsize)
					chunk_SHA1_Update(ctx, ibuf, hsize);
				*already_hashed_to = offset;
			}
			s.next_in = ibuf;
			s.avail_in = rsize;
			size -= rsize;
		}

		status = git_deflate(&s, size ? 0 : Z_FINISH);

		if (!s.avail_out || status == Z_STREAM_END) {
			if (write_object) {
				size_t written = s.next_out - obuf;

				/* would we bust the size limit? */
				if (state->nr_written &&
				    pack_size_limit_cfg &&
				    pack_size_limit_cfg < state->offset + written) {
					git_deflate_abort(&s);
					return -1;
				}

				sha1write(state->f, obuf, written);
				state->offset += written;
			}
			s.next_out = obuf;
			s.avail_out = sizeof(obuf);
		}

		switch (status) {
		case Z_OK:
		case Z_BUF_ERROR:
		case Z_STREAM_END:
			continue;
		default:
			die("unexpected deflate failure: %d", status);
		}
	}
	git_deflate_end(&s);
	return 0;
}

/* Lazily create backing packfile for the state */
static void prepare_to_stream(struct bulk_checkin_state *state,
			      unsigned flags)
{
	if (!(flags & HASH_WRITE_OBJECT) || state->f)
		return;

	state->f = create_tmp_packfile(&state->pack_tmp_name);
	reset_pack_idx_option(&state->pack_idx_opts);

	/* Pretend we are going to write only one object */
	state->offset = write_pack_header(state->f, 1);
	if (!state->offset)
		die_errno("unable to write pack header");
}

static int deflate_to_pack(struct bulk_checkin_state *state,
			   unsigned char result_sha1[],
			   int fd, size_t size,
			   enum object_type type, const char *path,
			   unsigned flags,
			   struct chunk_ctx *up)
{
	off_t seekback, already_hashed_to;
	struct chunk_ctx ctx;
	unsigned char obuf[16384];
	unsigned header_len;
	struct sha1file_checkpoint checkpoint;
	struct pack_idx_entry *idx = NULL;

	seekback = lseek(fd, 0, SEEK_CUR);
	if (seekback == (off_t) -1)
		return error("cannot find the current offset");

	header_len = sprintf((char *)obuf, "%s %" PRIuMAX,
			     typename(type), (uintmax_t)size) + 1;
	memset(&ctx, 0, sizeof(ctx));
	ctx.up = up;
	git_SHA1_Init(&ctx.ctx);
	git_SHA1_Update(&ctx.ctx, obuf, header_len);

	/* Note: idx is non-NULL when we are writing */
	if ((flags & HASH_WRITE_OBJECT) != 0)
		idx = xcalloc(1, sizeof(*idx));

	already_hashed_to = 0;

	while (1) {
		prepare_to_stream(state, flags);
		if (idx) {
			sha1file_checkpoint(state->f, &checkpoint);
			idx->offset = state->offset;
			crc32_begin(state->f);
		}
		if (!stream_to_pack(state, &ctx, &already_hashed_to,
				    fd, size, type, path, flags))
			break;
		/*
		 * Writing this object to the current pack will make
		 * it too big; we need to truncate it, start a new
		 * pack, and write into it.
		 */
		if (!idx)
			die("BUG: should not happen");
		sha1file_truncate(state->f, &checkpoint);
		state->offset = checkpoint.offset;
		finish_bulk_checkin(state);
		if (lseek(fd, seekback, SEEK_SET) == (off_t) -1)
			return error("cannot seek back");
	}
	git_SHA1_Final(result_sha1, &ctx.ctx);
	if (!idx)
		return 0;

	idx->crc32 = crc32_end(state->f);
	if (already_written(state, result_sha1)) {
		sha1file_truncate(state->f, &checkpoint);
		state->offset = checkpoint.offset;
		free(idx);
	} else {
		hashcpy(idx->sha1, result_sha1);
		ALLOC_GROW(state->written,
			   state->nr_written + 1,
			   state->alloc_written);
		state->written[state->nr_written++] = idx;
	}
	return 0;
}

/*
 * This is only called when actually writing the object out.
 * When only hashing to compute the object name, we will pass
 * the data through deflate_to_pack() codepath.
 */
static int store_in_chunks(struct bulk_checkin_state *state,
			   unsigned char result_sha1[],
			   int fd, size_t size,
			   enum object_type type, const char *path,
			   unsigned flags,
			   struct chunk_ctx *up)
{
	struct pack_idx_entry *idx;
	struct chunk_ctx ctx;
	unsigned char chunk[CHUNK_MAX][20];
	unsigned char header[100];
	unsigned header_len;
	unsigned chunk_cnt, i;
	size_t remainder = size;
	size_t write_size;
	int status;

	header_len = sprintf((char *)header, "%s %" PRIuMAX,
			     typename(type), (uintmax_t)size) + 1;

	memset(&ctx, 0, sizeof(ctx));
	ctx.up = up;
	git_SHA1_Init(&ctx.ctx);
	git_SHA1_Update(&ctx.ctx, header, header_len);

	for (chunk_cnt = 0, remainder = size;
	     remainder && chunk_cnt < CHUNK_MAX - 1;
	     chunk_cnt++) {
		size_t chunk_size = carve_chunk(fd, remainder);
		status = deflate_to_pack(state, chunk[chunk_cnt], fd,
					 chunk_size, OBJ_BLOB, path, flags,
					 &ctx);
		if (status)
			return status;
		remainder -= chunk_size;
	}

	if (remainder) {
		if (split_size_limit_cfg && split_size_limit_cfg < remainder)
			status = store_in_chunks(state, chunk[chunk_cnt], fd,
						 remainder, OBJ_BLOB, path, flags,
						 &ctx);
		else
			status = deflate_to_pack(state, chunk[chunk_cnt], fd,
						 remainder, OBJ_BLOB, path, flags,
						 &ctx);
		if (status)
			return status;
		chunk_cnt++;
	}

	/*
	 * Now we have chunk_cnt chunks (the last one may be a large
	 * blob that itself is represented as concatenation of
	 * multiple blobs).
	 */
	git_SHA1_Final(result_sha1, &ctx.ctx);
	if (already_written(state, result_sha1))
		return 0;

	/*
	 * The standard type & size header is followed by
	 * - the number of chunks (varint)
	 * - the object names of the chunks (20 * chunk_cnt bytes)
	 * - the resulting object name (20 bytes)
	 */
	type = OBJ_CHUNKED(type);
	header_len = encode_in_pack_object_header(type, size, header);
	header_len += encode_in_pack_varint(chunk_cnt, header + header_len);

	write_size = header_len + 20 * (chunk_cnt + 1);

	prepare_to_stream(state, flags);
	if (state->nr_written &&
	    pack_size_limit_cfg &&
	    pack_size_limit_cfg < (state->offset + write_size)) {
		finish_bulk_checkin(state);
		prepare_to_stream(state, flags);
	}

	idx = xcalloc(1, sizeof(*idx));
	idx->offset = state->offset;

	crc32_begin(state->f);
	sha1write(state->f, header, header_len);
	for (i = 0; i < chunk_cnt; i++)
		sha1write(state->f, chunk[i], 20);
	sha1write(state->f, result_sha1, 20);
	idx->crc32 = crc32_end(state->f);
	hashcpy(idx->sha1, result_sha1);
	ALLOC_GROW(state->written,
		   state->nr_written + 1,
		   state->alloc_written);
	state->written[state->nr_written++] = idx;
	state->offset += write_size;

	return 0;
}

int index_bulk_checkin(unsigned char *sha1,
		       int fd, size_t size, enum object_type type,
		       const char *path, unsigned flags)
{
	int status;

	/*
	 * For now, we only deal with blob objects, as validation
	 * of a huge tree object that is split into chunks will be
	 * too cumbersome to be worth it.
	 *
	 * Note that we only have to use store_in_chunks() codepath
	 * when we are actually writing things out. deflate_to_pack()
	 * codepath can hash arbitrarily huge object without keeping
	 * everything in core just fine.
	 */
	if ((flags & HASH_WRITE_OBJECT) &&
	    type == OBJ_BLOB &&
	    split_size_limit_cfg &&
	    split_size_limit_cfg < size)
		status = store_in_chunks(&state, sha1, fd, size, type,
					 path, flags, NULL);
	else
		status = deflate_to_pack(&state, sha1, fd, size, type,
					 path, flags, NULL);
	if (!state.plugged)
		finish_bulk_checkin(&state);
	return status;
}

void plug_bulk_checkin(void)
{
	state.plugged = 1;
}

void unplug_bulk_checkin(void)
{
	state.plugged = 0;
	if (state.f)
		finish_bulk_checkin(&state);
}
