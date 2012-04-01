#include "git-compat-util.h"
#include "bulk-checkin.h"

/* Cut at around 512kB */
#define TARGET_CHUNK_SIZE_LOG2 19
#define TARGET_CHUNK_SIZE (1U << TARGET_CHUNK_SIZE_LOG2)

/*
 * Carve out around 500kB to be stored as a separate blob
 */
size_t carve_chunk(int fd, size_t size)
{
	size_t chunk_size;
	off_t seekback = lseek(fd, 0, SEEK_CUR);

	if (seekback == (off_t) -1)
		die("cannot find the current offset");

	/*
	 * Future patch will do something clever to find out where to
	 * cut, so that a large unchanged byte-range is cut at the same
	 * location to result in a series of same set of blob objects.
	 *
	 * For now, this cuts at the same interval, which is only good
	 * for append-only files or files whose tail part is updated;
	 * the other parts of the code are designed not to care how
	 * chunks are carved, so that this function can be updated
	 * without any compatibility issues.
	 */
	chunk_size = size;
	if (TARGET_CHUNK_SIZE < chunk_size)
		chunk_size = TARGET_CHUNK_SIZE;

	if (lseek(fd, seekback, SEEK_SET) == (off_t) -1)
		return error("cannot seek back");

	return chunk_size;
}
