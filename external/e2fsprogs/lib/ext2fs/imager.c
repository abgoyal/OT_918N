
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

#ifndef HAVE_TYPE_SSIZE_T
typedef int ssize_t;
#endif

static int check_zero_block(char *buf, int blocksize)
{
	char	*cp = buf;
	int	left = blocksize;

	while (left > 0) {
		if (*cp++)
			return 0;
		left--;
	}
	return 1;
}

#define BUF_BLOCKS	32

errcode_t ext2fs_image_inode_write(ext2_filsys fs, int fd, int flags)
{
	unsigned int	group, left, c, d;
	char		*buf, *cp;
	blk_t		blk;
	ssize_t		actual;
	errcode_t	retval;

	buf = malloc(fs->blocksize * BUF_BLOCKS);
	if (!buf)
		return ENOMEM;
	
	for (group = 0; group < fs->group_desc_count; group++) {
		blk = fs->group_desc[(unsigned)group].bg_inode_table;
		if (!blk) {
			retval = EXT2_ET_MISSING_INODE_TABLE;
			goto errout;
		}
		left = fs->inode_blocks_per_group;
		while (left) {
			c = BUF_BLOCKS;
			if (c > left)
				c = left;
			retval = io_channel_read_blk(fs->io, blk, c, buf);
			if (retval)
				goto errout;
			cp = buf;
			while (c) {
				if (!(flags & IMAGER_FLAG_SPARSEWRITE)) {
					d = c;
					goto skip_sparse;
				}
				/* Skip zero blocks */
				if (check_zero_block(cp, fs->blocksize)) {
					c--;
					blk++;
					left--;
					cp += fs->blocksize;
					lseek(fd, fs->blocksize, SEEK_CUR);
					continue;
				}
				/* Find non-zero blocks */
				for (d=1; d < c; d++) {
					if (check_zero_block(cp + d*fs->blocksize, fs->blocksize))
						break;
				}
			skip_sparse:
				actual = write(fd, cp, fs->blocksize * d);
				if (actual == -1) {
					retval = errno;
					goto errout;
				}
				if (actual != (ssize_t) (fs->blocksize * d)) {
					retval = EXT2_ET_SHORT_WRITE;
					goto errout;
				}
				blk += d;
				left -= d;
				cp += fs->blocksize * d;
				c -= d;
			}
		}
	}
	retval = 0;

errout:
	free(buf);
	return retval;
}

errcode_t ext2fs_image_inode_read(ext2_filsys fs, int fd, 
				  int flags EXT2FS_ATTR((unused)))
{
	unsigned int	group, c, left;
	char		*buf;
	blk_t		blk;
	ssize_t		actual;
	errcode_t	retval;

	buf = malloc(fs->blocksize * BUF_BLOCKS);
	if (!buf)
		return ENOMEM;
	
	for (group = 0; group < fs->group_desc_count; group++) {
		blk = fs->group_desc[(unsigned)group].bg_inode_table;
		if (!blk) {
			retval = EXT2_ET_MISSING_INODE_TABLE;
			goto errout;
		}
		left = fs->inode_blocks_per_group;
		while (left) {
			c = BUF_BLOCKS;
			if (c > left)
				c = left;
			actual = read(fd, buf, fs->blocksize * c);
			if (actual == -1) {
				retval = errno;
				goto errout;
			}
			if (actual != (ssize_t) (fs->blocksize * c)) {
				retval = EXT2_ET_SHORT_READ;
				goto errout;
			}
			retval = io_channel_write_blk(fs->io, blk, c, buf);
			if (retval)
				goto errout;
			
			blk += c;
			left -= c;
		}
	}
	retval = ext2fs_flush_icache(fs);

errout:
	free(buf);
	return retval;
}

errcode_t ext2fs_image_super_write(ext2_filsys fs, int fd, 
				   int flags EXT2FS_ATTR((unused)))
{
	char		*buf, *cp;
	ssize_t		actual;
	errcode_t	retval;

	buf = malloc(fs->blocksize);
	if (!buf)
		return ENOMEM;

	/*
	 * Write out the superblock
	 */
	memset(buf, 0, fs->blocksize);
	memcpy(buf, fs->super, SUPERBLOCK_SIZE);
	actual = write(fd, buf, fs->blocksize);
	if (actual == -1) {
		retval = errno;
		goto errout;
	}
	if (actual != (ssize_t) fs->blocksize) {
		retval = EXT2_ET_SHORT_WRITE;
		goto errout;
	}

	/*
	 * Now write out the block group descriptors
	 */
	cp = (char *) fs->group_desc;
	actual = write(fd, cp, fs->blocksize * fs->desc_blocks);
	if (actual == -1) {
		retval = errno;
		goto errout;
	}
	if (actual != (ssize_t) (fs->blocksize * fs->desc_blocks)) {
		retval = EXT2_ET_SHORT_WRITE;
		goto errout;
	}
	
	retval = 0;

errout:
	free(buf);
	return retval;
}

errcode_t ext2fs_image_super_read(ext2_filsys fs, int fd, 
				  int flags EXT2FS_ATTR((unused)))
{
	char		*buf;
	ssize_t		actual, size;
	errcode_t	retval;

	size = fs->blocksize * (fs->group_desc_count + 1);
	buf = malloc(size);
	if (!buf)
		return ENOMEM;

	/*
	 * Read it all in.
	 */
	actual = read(fd, buf, size);
	if (actual == -1) {
		retval = errno;
		goto errout;
	}
	if (actual != size) {
		retval = EXT2_ET_SHORT_READ;
		goto errout;
	}

	/*
	 * Now copy in the superblock and group descriptors
	 */
	memcpy(fs->super, buf, SUPERBLOCK_SIZE);

	memcpy(fs->group_desc, buf + fs->blocksize,
	       fs->blocksize * fs->group_desc_count);

	retval = 0;

errout:
	free(buf);
	return retval;
}

errcode_t ext2fs_image_bitmap_write(ext2_filsys fs, int fd, int flags)
{
	char		*ptr;
	int		c, size;
	char		zero_buf[1024];
	ssize_t		actual;
	errcode_t	retval;

	if (flags & IMAGER_FLAG_INODEMAP) {
		if (!fs->inode_map) {
			retval = ext2fs_read_inode_bitmap(fs);
			if (retval)
				return retval;
		}
		ptr = fs->inode_map->bitmap;
		size = (EXT2_INODES_PER_GROUP(fs->super) / 8);
	} else {
		if (!fs->block_map) {
			retval = ext2fs_read_block_bitmap(fs);
			if (retval)
				return retval;
		}
		ptr = fs->block_map->bitmap;
		size = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
	}
	size = size * fs->group_desc_count;

	actual = write(fd, ptr, size);
	if (actual == -1) {
		retval = errno;
		goto errout;
	}
	if (actual != size) {
		retval = EXT2_ET_SHORT_WRITE;
		goto errout;
	}
	size = size % fs->blocksize;
	memset(zero_buf, 0, sizeof(zero_buf));
	if (size) {
		size = fs->blocksize - size;
		while (size) {
			c = size;
			if (c > (int) sizeof(zero_buf))
				c = sizeof(zero_buf);
			actual = write(fd, zero_buf, c);
			if (actual == -1) {
				retval = errno;
				goto errout;
			}
			if (actual != c) {
				retval = EXT2_ET_SHORT_WRITE;
				goto errout;
			}
			size -= c;
		}
	}
	retval = 0;
errout:
	return (retval);
}


errcode_t ext2fs_image_bitmap_read(ext2_filsys fs, int fd, int flags)
{
	char		*ptr, *buf = 0;
	int		size;
	ssize_t		actual;
	errcode_t	retval;

	if (flags & IMAGER_FLAG_INODEMAP) {
		if (!fs->inode_map) {
			retval = ext2fs_read_inode_bitmap(fs);
			if (retval)
				return retval;
		}
		ptr = fs->inode_map->bitmap;
		size = (EXT2_INODES_PER_GROUP(fs->super) / 8);
	} else {
		if (!fs->block_map) {
			retval = ext2fs_read_block_bitmap(fs);
			if (retval)
				return retval;
		}
		ptr = fs->block_map->bitmap;
		size = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
	}
	size = size * fs->group_desc_count;

	buf = malloc(size);
	if (!buf)
		return ENOMEM;

	actual = read(fd, buf, size);
	if (actual == -1) {
		retval = errno;
		goto errout;
	}
	if (actual != size) {
		retval = EXT2_ET_SHORT_WRITE;
		goto errout;
	}
	memcpy(ptr, buf, size);
	
	retval = 0;
errout:
	if (buf)
		free(buf);
	return (retval);
}
