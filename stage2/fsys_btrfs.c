/* fsys_btrfs.c - an implementation for the Btrfs filesystem
 *
 * Copyright 2009 Red Hat, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef FSYS_BTRFS

#include "shared.h"
#include "filesys.h"
#include "btrfs.h"

#define BTRFS_VERBOSE 0

/* Cache layouts */

#define LOOKUP_CACHE_BUF_SIZE   (4096)
#define LOOKUP_CACHE_SIZE       (LOOKUP_CACHE_BUF_SIZE * LAST_LOOKUP_POOL)
#define BTRFS_FS_INFO							\
	((struct btrfs_fs_info *)((unsigned long)FSYS_BUF +		\
				  LOOKUP_CACHE_SIZE))
#define BTRFS_CACHE_SIZE         (sizeof(struct btrfs_fs_info) +	\
				  LOOKUP_CACHE_SIZE)
#define BTRFS_TREE_ROOT          (&BTRFS_FS_INFO->tree_root)
#define BTRFS_CHUNK_ROOT         (&BTRFS_FS_INFO->chunk_root)
#define BTRFS_FS_ROOT            (&BTRFS_FS_INFO->fs_root)
#define BTRFS_SUPER              (&BTRFS_FS_INFO->sb_copy)
#define BTRFS_DEVICES            (&BTRFS_FS_INFO->devices[0])
#define BTRFS_FILE_INFO          (&BTRFS_FS_INFO->file_info)
#define BTRFS_FILE_INFO_KEY      (&BTRFS_FILE_INFO->key)

#define BTRFS_VOLATILE_DEV_CACHE				        \
	(&BTRFS_FS_INFO->devices[BTRFS_NUM_CACHED_DEVICES])

#define LOOKUP_CACHE_BUF(id) ((char *)((unsigned long)FSYS_BUF +	\
				       id * LOOKUP_CACHE_BUF_SIZE))

#define noop   do {; } while (0)

#if BTRFS_VERBOSE
#define btrfs_msg(format, ...) printf(format , ## __VA_ARGS__)
#else
#define btrfs_msg(format, args...) noop
#endif

/* compile-time check to make sure we don't overlap
   filesystem buffer */
static inline void check_btrfs_cache_size(void)
{
	cassert(BTRFS_CACHE_SIZE <= FSYS_BUFLEN);
}

static inline u64 btrfs_sb_offset(int mirror)
{
	u64 start = 16 * 1024;
	if (mirror)
		return start << (BTRFS_SUPER_MIRROR_SHIFT * mirror);
	return BTRFS_SUPER_INFO_OFFSET;
}

static inline char *grab_lookup_cache(lookup_pool_id lpid)
{
	char *buf = LOOKUP_CACHE_BUF(lpid);
	memset(buf, 0, LOOKUP_CACHE_BUF_SIZE);
	return buf;
}

static inline struct btrfs_path *btrfs_grab_path(lookup_pool_id lpid)
{
	return &BTRFS_FS_INFO->paths[lpid];
}

static inline void btrfs_set_path_key(struct btrfs_path *path,
				      struct btrfs_key *key)
{
	btrfs_item_key_to_cpu(&path->nodes[0],
			      key,
			      path->slots[0]);
}

static inline void btrfs_update_file_info(struct btrfs_path *path)
{
	btrfs_set_path_key(path, BTRFS_FILE_INFO_KEY);
}

static inline void btrfs_set_root_dir_key(struct btrfs_key *key)
{
	key->objectid = BTRFS_FIRST_FREE_OBJECTID;
	btrfs_set_key_type(key, BTRFS_INODE_ITEM_KEY);
	key->offset = 0;
}

static inline void copy_extent_buffer(struct extent_buffer *dst,
				      struct extent_buffer *src)
{
	char *data = dst->data;
	memcpy(dst, src, sizeof(*dst));
	memcpy(data, src->data, 4096);
	dst->data = data;
}

static inline void move_extent_buffer(struct extent_buffer *dst,
				      struct extent_buffer *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline void init_btrfs_root (struct btrfs_root *root)
{
	root->node.data = root->data;
}

static inline void init_btrfs_path(lookup_pool_id lpid)
{
	struct btrfs_path *path;
	path = btrfs_grab_path(lpid);
	path->lpid = lpid;
}

static inline void init_btrfs_info(void)
{
	int i;

	memset(BTRFS_FS_INFO, 0, sizeof(struct btrfs_fs_info));
	for(i = 0; i < LAST_LOOKUP_POOL; i++)
		init_btrfs_path(i);
	init_btrfs_root(BTRFS_TREE_ROOT);
	init_btrfs_root(BTRFS_CHUNK_ROOT);
	init_btrfs_root(BTRFS_FS_ROOT);
}

static void setup_root(struct btrfs_root *root,
		       u32 nodesize,
		       u32 leafsize,
		       u32 sectorsize,
		       u32 stripesize,
		       u64 objectid)
{
	root->nodesize = nodesize;
	root->leafsize = leafsize;
	root->sectorsize = sectorsize;
	root->stripesize = stripesize;
	root->objectid = objectid;
}

/*
 * Pick up the latest root of a
 * tree with specified @objectid
 */
static int btrfs_find_last_root(struct btrfs_root *tree_root,
				u64 objectid,
				struct btrfs_root_item *item,
				lookup_pool_id lpid)
{
	int ret;
	int slot;
	struct btrfs_key search_key;
	struct btrfs_key found_key;
	struct btrfs_path *path;

	search_key.objectid = objectid;
	search_key.type = BTRFS_ROOT_ITEM_KEY;
	search_key.offset = (u64)-1;
	path = btrfs_grab_path(lpid);

	ret = aux_tree_lookup(tree_root, &search_key, path);
	if (ret < 0)
		return 1;
	slot = path->slots[0];
	WARN_ON(slot == 0);
	slot -= 1;
	btrfs_item_key_to_cpu(&path->nodes[0], &found_key, slot);
	if (found_key.objectid != objectid)
		return 1;

	read_extent_buffer(&path->nodes[0], item,
			   btrfs_item_ptr_offset(&path->nodes[0], slot),
			   sizeof(*item));
	return 0;
}

static int find_setup_root(struct btrfs_root *tree_root,
			   u32 nodesize,
			   u32 leafsize,
			   u32 sectorsize,
			   u32 stripesize,
			   u64 objectid,
			   struct btrfs_root *dest_root,
			   u64 bytenr,
			   u32 blocksize,
			   u64 generation,
			   lookup_pool_id lpid)
{
	int ret;
	struct extent_buffer eb;

	setup_root(dest_root,
		   nodesize,
		   leafsize,
		   sectorsize,
		   stripesize,
		   objectid);
	if (tree_root) {
		/*
		 * pick up the latest version
		 * of the root we want to set up
		 */
		ret = btrfs_find_last_root(tree_root, objectid,
					   &dest_root->root_item,
					   lpid);
		if (ret)
			return ret;
		bytenr = btrfs_root_bytenr(&dest_root->root_item);
		blocksize = btrfs_level_size(dest_root,
				       btrfs_root_level(&dest_root->root_item));
		generation = btrfs_root_generation(&dest_root->root_item);
	}
	ret = read_tree_block(dest_root,
			      &eb,
			      bytenr,
			      blocksize,
			      generation,
			      lpid);
	if (!ret)
		return 1;
	copy_extent_buffer(&dest_root->node, &eb);
	return 0;
}

static inline int btrfs_strncmp(const char *cs, const char *ct, int count)
{
	signed char __res = 0;

	while (count) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
		count--;
	}
	return __res;
}

/*
 * the same as devread, but accepts
 * device number, start and length.
 */
static int btrfs_devread(unsigned long drive, unsigned long part,
			 unsigned long dev_len, int sector,
			 int byte_offset, int byte_len, char *buf)
{
	if (sector < 0
	    || ((sector + ((byte_offset + byte_len - 1) >> SECTOR_BITS))
		>= dev_len)) {
		errnum = ERR_OUTSIDE_PART;
		return 0;
	}
	sector += byte_offset >> SECTOR_BITS;
	byte_offset &= SECTOR_SIZE - 1;
#if !defined(STAGE1_5)
	if (disk_read_hook && debug)
		printf ("<%d, %d, %d>", sector, byte_offset, byte_len);
#endif /* !STAGE1_5 */
	return rawread(drive, part + sector, byte_offset,
		       byte_len, buf);
}

static int btrfs_check_super(void)
{
	struct btrfs_super_block *sb = BTRFS_SUPER;

	if (sb->nodesize != BTRFS_DEFAULT_NODE_SIZE) {
		btrfs_msg("Btrfs node size (%d) != %d unsupported\n",
			  sb->nodesize, BTRFS_DEFAULT_NODE_SIZE);
		goto error;
	}
	if (sb->leafsize != BTRFS_DEFAULT_LEAF_SIZE) {
 	        btrfs_msg("Btrfs leaf size (%d) != %d unsupported\n",
			  sb->leafsize, BTRFS_DEFAULT_LEAF_SIZE);
		goto error;
	}
	return 0;
 error:
	return 1;
}

/* lift the super block */
static int btrfs_uptodate_super_copy(struct btrfs_fs_info *fs)
{
	errnum = ERR_NONE;
	btrfs_devread(BTRFS_FS_INFO->sb_dev.drive,
		      BTRFS_FS_INFO->sb_dev.part,
		      BTRFS_FS_INFO->sb_dev.length,
		      btrfs_sb_offset(BTRFS_FS_INFO->sb_mirror) >> SECTOR_BITS,
		      0,
		      sizeof(struct btrfs_super_block),
		      (char *)BTRFS_SUPER);
	return btrfs_check_super();
}

/*
 * Looking for a btrfs super block by magic, @fsid and @devid
 * (the last two ones are optional). Update latest transid (if
 * any). Return 0, if such super block was found. Otherwise,
 * return 1.
 *
 * NOTE:
 * After calling this function the sb_copy of global btrfs_fs_info
 * can contain garbage, so the caller is responsible for this to be
 * uptodate (see the function btrfs_uptodate_super_copy()).
 */
static int btrfs_find_super(struct btrfs_device *dev, char *fsid, u64 *devid)
{
	int i, ret;
	int found = 0;

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		ret = btrfs_devread(dev->drive,
				    dev->part,
				    dev->length,
				    btrfs_sb_offset(i) >> SECTOR_BITS,
				    0,
				    sizeof(struct btrfs_super_block),
				    (char *)BTRFS_SUPER);
		if (!ret) {
			if (errnum == ERR_OUTSIDE_PART) {
				errnum = ERR_NONE;
				break;
			} else {
				errnum = ERR_NONE;
				continue;
			}
		}
		if (btrfs_super_bytenr(BTRFS_SUPER) != btrfs_sb_offset(i) ||
		    btrfs_strncmp((char *)(&BTRFS_SUPER->magic),
				  BTRFS_MAGIC,
				  sizeof(BTRFS_SUPER->magic)))
			continue;
		if (fsid &&
		    btrfs_strncmp(fsid,
				  (char *)BTRFS_SUPER->fsid,
				  BTRFS_FSID_SIZE))
			return 1;
		if (devid &&
		    *devid != btrfs_super_devid(BTRFS_SUPER))
			return 1;
		found = 1;
		dev->devid = btrfs_super_devid(BTRFS_SUPER);

		if (btrfs_super_generation(BTRFS_SUPER) >
		    BTRFS_FS_INFO->sb_transid) {
			BTRFS_FS_INFO->sb_transid =
				btrfs_super_generation(BTRFS_SUPER);
			BTRFS_FS_INFO->sb_mirror = i;
			BTRFS_FS_INFO->sb_dev.devid =
				btrfs_super_devid(BTRFS_SUPER);
			BTRFS_FS_INFO->sb_dev.drive = dev->drive;
			BTRFS_FS_INFO->sb_dev.part = dev->part;
			BTRFS_FS_INFO->sb_dev.length = dev->length;
		}
	}
	return !found;
}

/*
 * "Discern" a btrfs device by fsid and
 * optionaly by devid (if lookup is set).
 * Populate persistent device cache (if
 * there are free slots).
 */
static int btrfs_discerner(struct btrfs_device **dev, int lookup)
{
	if (btrfs_find_super(*dev,
			     (char *)BTRFS_FS_INFO->fsid,
			     (lookup ? &(*dev)->devid : 0)))
		/* not found */
		return 0;
	if (*dev < BTRFS_VOLATILE_DEV_CACHE) {
		/* populate persistent device cache */
		memcpy(*dev + 1, *dev, sizeof(struct btrfs_device));
		(*dev)++;
	}
	return 1;
}

/*
 * Scan available grub devices and call discerner
 * for them. Return a number of discerned devices
 * The scanner was stolen from print_completions().
 *
 * Preconditions:
 * The global structure btrfs_fs_info contains
 * the latest valid version of btrfs superblock
 * (the field @sb_copy)
 */
static u64 scan_grub_devices(struct btrfs_device *dev,
			     int (*discerner)(struct btrfs_device **, int),
			     int lookup)
{
	int i, j;
	u64 count = 0;
	struct geometry geom;

	for (i = 0; i < 2; i++)
		for (j = 0; j < 8; j++) {
			unsigned long part = 0xFFFFFF;
			int type, entry, gpt_count, gpt_size;
			unsigned long offset, ext_offset, gpt_offset;

			dev->drive = (i * 0x80) + j;
			if (get_diskinfo(dev->drive, &geom))
				continue;
			while (1) {
				int ret;
				buf_drive = -1;
				errnum = ERR_NONE;
				ret = next_partition(dev->drive, 0xFFFFFF,
						     &part, &type, &dev->part,
						     &dev->length, &offset,
						     &entry, &ext_offset,
						     &gpt_offset, &gpt_count,
						     &gpt_size,
						     BTRFS_FS_INFO->mbr);
				if (!ret)
					break;
				if (discerner(&dev, lookup)) {
					count++;
					if (lookup)
						goto exit;
				}
			}
		}
	errnum = ERR_NONE;
	if (cdrom_drive != GRUB_INVALID_DRIVE &&
	    !get_diskinfo(cdrom_drive, &geom)) {
		dev->drive = cdrom_drive;
		dev->part = 0;
		dev->length = geom.total_sectors;
		if (discerner(&dev, lookup)) {
			count++;
			if (lookup)
				goto exit;
		}
	}
#if 0
	errnum = ERR_NONE;
	if (network_ready &&
	    !get_diskinfo(NETWORK_DRIVE, &geom)) {
		dev->drive = NETWORK_DRIVE;
		dev->part = 0;
		dev->length = geom.total_sectors;
		if (discerner(&dev, lookup)) {
			count++;
			if (lookup)
				goto exit;
		}
	}
#endif /* 0 */
 exit:
	return count;
}

#if 0
static int btrfs_next_item(struct btrfs_root *root,
			   struct btrfs_path *path);

/*
 * Scan the chunk tree for dev items
 * and call a seeker for all of them.
 * Preconditions: chunk root is installed
 * to the global btrfs_fs_info.
 */
static int scan_dev_tree(struct btrfs_device* (*seeker)(u64))
{
	int ret;
	u64 num_devices = 0;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct btrfs_root *root;

	root = BTRFS_CHUNK_ROOT;
	path = btrfs_grab_path(FIRST_EXTERNAL_LOOKUP_POOL);
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = 0;
	key.offset = 0;

	ret = aux_tree_lookup(root, &key, path);
	if (ret == -1)
		goto corrupted;
	while (1) {
		struct btrfs_device *result;
		struct btrfs_dev_item *dev_item;

		btrfs_item_key_to_cpu(&path->nodes[0],
				      &found_key,
				      path->slots[0]);
		if (found_key.objectid != BTRFS_DEV_ITEMS_OBJECTID)
			break;
		dev_item = btrfs_item_ptr(&path->nodes[0],
					  path->slots[0],
					  struct btrfs_dev_item);
		result = seeker(btrfs_device_id(&path->nodes[0], dev_item));
		if (result == NULL) {
			btrfs_msg("Btrfs device %llu is not available\n",
				  btrfs_device_id(&path->nodes[0], dev_item));
			goto missed_dev;
		}
		num_devices++;
		ret = btrfs_next_item(root, path);
		if (ret)
			break;
	}
	if (num_devices == btrfs_super_num_devices(BTRFS_SUPER))
		return 0;
 corrupted:
	errnum = ERR_FSYS_CORRUPT;
	return 1;
 missed_dev:
	errnum = ERR_FSYS_MOUNT;
	return 1;
}
#endif /* 0 */

/*
 * Find a grub btrfs device by devid.
 * Preconditions: global btrfs_fs_info
 * contains a copy of btrfs super block.
 *
 * Return pointer to the cached device on success.
 * Otherwise return NULL.
 */
static struct btrfs_device *btrfs_lookup_device(u64 devid)
{
	int i, result;
	struct btrfs_device *cdev;

	for (i = 0; i < BTRFS_NUM_CACHED_DEVICES; i++) {
		cdev = &BTRFS_DEVICES[i];
		if (cdev->devid == devid)
			goto found_in_cache;
		if (cdev->devid == 0)
			goto not_found_in_cache;
	}
 not_found_in_cache:
	cdev = BTRFS_VOLATILE_DEV_CACHE;
	cdev->devid = devid;
	result = scan_grub_devices(cdev,
				   btrfs_discerner,
				   1);
	if (result == 0)
		/*
		 * At mount time we have figured out that
		 * number of available devices is not less
		 * then number of devices recorded in the
		 * super block. Hence we treat this case as
		 * file system corruption.
		 */
		goto corrupt;
	result = btrfs_uptodate_super_copy(BTRFS_FS_INFO);
	if (result)
		goto corrupt;
 found_in_cache:
	return cdev;
 corrupt:
	errnum = ERR_FSYS_CORRUPT;
	return NULL;
}

static int btrfs_find_device(struct btrfs_device *dev)
{
	struct btrfs_device *cdev;

	if (btrfs_super_num_devices(BTRFS_SUPER) == 1) {
		dev->drive = current_drive;
		dev->part = part_start;
		dev->length = part_length;
		return 0;
	}
	cdev = btrfs_lookup_device(dev->devid);
	if (cdev == NULL)
		return 1;
	dev->drive  = cdev->drive;
	dev->part   = cdev->part;
	dev->length = cdev->length;
	return 0;
}

static inline void init_btrfs_volatile_dev_cache(void)
{
	BTRFS_VOLATILE_DEV_CACHE->devid = 0;
	BTRFS_VOLATILE_DEV_CACHE->drive = current_drive;
	BTRFS_VOLATILE_DEV_CACHE->part = part_start;
	BTRFS_VOLATILE_DEV_CACHE->length = part_length;
}

/*
 * check availability of btrfs devices
 * and populate the persistent device cache
 */
static int btrfs_check_devices(void)
{
	u64 num_dev;

	if (btrfs_super_num_devices(BTRFS_SUPER) == 1)
		return 0;
	num_dev = scan_grub_devices(BTRFS_DEVICES,
				    btrfs_discerner, 0);
	if (btrfs_uptodate_super_copy(BTRFS_FS_INFO))
		return 1;
	if (num_dev < btrfs_super_num_devices(BTRFS_SUPER)) {
		btrfs_msg("Some (%llu) Btrfs devices is not available\n",
			  btrfs_super_num_devices(BTRFS_SUPER) - num_dev);
	        return 1;
	}
	return 0;
}

int btrfs_mount(void)
{
	int ret;

	check_btrfs_cache_size();
	init_btrfs_info();
	init_btrfs_volatile_dev_cache();

	ret = btrfs_find_super(BTRFS_VOLATILE_DEV_CACHE, NULL, NULL);
	if (ret) {
		btrfs_msg("Drive %lu, partition %lu: no Btrfs metadata\n",
			  current_drive, part_start);
		return 0;
	}
	ret = btrfs_uptodate_super_copy(BTRFS_FS_INFO);
	if (ret)
		goto error;
	BTRFS_FS_INFO->sb_transid =
		btrfs_super_generation(BTRFS_SUPER);
	memcpy(BTRFS_FS_INFO->fsid,
	       BTRFS_SUPER->fsid,
	       BTRFS_FSID_SIZE);
	ret = btrfs_check_devices();
	if (ret)
		goto error;
	/* setup chunk root */
	ret = find_setup_root(NULL,
			      btrfs_super_nodesize(BTRFS_SUPER),
			      btrfs_super_leafsize(BTRFS_SUPER),
			      btrfs_super_sectorsize(BTRFS_SUPER),
			      btrfs_super_stripesize(BTRFS_SUPER),
			      BTRFS_CHUNK_TREE_OBJECTID,
			      BTRFS_CHUNK_ROOT,
			      btrfs_super_chunk_root(BTRFS_SUPER),
			      btrfs_chunk_root_level_size(BTRFS_SUPER),
			      btrfs_super_chunk_root_generation(BTRFS_SUPER),
			      FIRST_EXTERNAL_LOOKUP_POOL);
	if (ret)
		return 0;
	/* setup tree root */
	ret = find_setup_root(NULL,
			      btrfs_super_nodesize(BTRFS_SUPER),
			      btrfs_super_leafsize(BTRFS_SUPER),
			      btrfs_super_sectorsize(BTRFS_SUPER),
			      btrfs_super_stripesize(BTRFS_SUPER),
			      BTRFS_ROOT_TREE_OBJECTID,
			      BTRFS_TREE_ROOT,
			      btrfs_super_root(BTRFS_SUPER),
			      btrfs_root_level_size(BTRFS_SUPER),
			      btrfs_super_generation(BTRFS_SUPER),
			      FIRST_EXTERNAL_LOOKUP_POOL);
	if (ret)
		return 0;
	/* setup fs_root */
	ret = find_setup_root(BTRFS_TREE_ROOT,
			      btrfs_super_nodesize(BTRFS_SUPER),
			      btrfs_super_leafsize(BTRFS_SUPER),
			      btrfs_super_sectorsize(BTRFS_SUPER),
			      btrfs_super_stripesize(BTRFS_SUPER),
			      BTRFS_FS_TREE_OBJECTID,
			      BTRFS_FS_ROOT,
			      0,
			      0,
			      0,
			      FIRST_EXTERNAL_LOOKUP_POOL);
	return !ret;
 error:
	errnum = ERR_FSYS_MOUNT;
	return 0;
}

/*
 * Check, whether @chunk is the map for a
 * block with @logical block number.
 * If yes, then fill the @map.
 * Return 1 on affirmative result,
 * otherwise return 0.
 */
int check_read_chunk(struct btrfs_key *key,
			    struct extent_buffer *leaf,
			    struct btrfs_chunk *chunk,
			    struct map_lookup *map,
			    u64 logical)
{
	int i, ret;
	u64 chunk_start;
	u64 chunk_size;
	int num_stripes;

	chunk_start = key->offset;
	chunk_size = btrfs_chunk_length(leaf, chunk);

	if (logical + 1 > chunk_start + chunk_size ||
	    logical < chunk_start)
		/* not a fit */
		return 0;
	num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	map->ce.start = chunk_start;
	map->ce.size = chunk_size;
	map->num_stripes = num_stripes;
	map->io_width = btrfs_chunk_io_width(leaf, chunk);
	map->io_align = btrfs_chunk_io_align(leaf, chunk);
	map->sector_size = btrfs_chunk_sector_size(leaf, chunk);
	map->stripe_len = btrfs_chunk_stripe_len(leaf, chunk);
	map->type = btrfs_chunk_type(leaf, chunk);
	map->sub_stripes = btrfs_chunk_sub_stripes(leaf, chunk);

	for (i = 0; i < num_stripes; i++) {
		map->stripes[i].physical =
			btrfs_stripe_offset_nr(leaf, chunk, i);
		map->stripes[i].dev.devid =
			btrfs_stripe_devid_nr(leaf, chunk, i);
		ret = btrfs_find_device(&map->stripes[i].dev);
		if (ret)
			return 0;
	}
	return 1;
}

static void init_extent_buffer(struct extent_buffer *eb,
			       struct btrfs_device *dev,
			       u64 logical,
			       u32 blocksize,
			       u64 physical,
			       lookup_pool_id lpid)
{
	if (dev)
		memcpy(&eb->dev, dev, sizeof(*dev));
	eb->start = logical;
	eb->len = blocksize;
	eb->dev_bytenr = physical;
	eb->data = grab_lookup_cache(lpid);
}

/*
 * Search for a map by logical offset in sys array.
 * Return -1 on errors;
 * Return 1 if the map is found,
 * Return 0 if the map is not found.
 */
int sys_array_lookup(struct map_lookup *map, u64 logical)
{
	struct extent_buffer sb;
	struct btrfs_disk_key *disk_key;
	struct btrfs_chunk *chunk;
	struct btrfs_key key;
	u32 num_stripes;
	u32 array_size;
	u32 len = 0;
	u8 *ptr;
	unsigned long sb_ptr;
	u32 cur;
	int ret;
	int i = 0;

	sb.data = (char *)BTRFS_SUPER;
	array_size = btrfs_super_sys_array_size(BTRFS_SUPER);

	ptr = BTRFS_SUPER->sys_chunk_array;
	sb_ptr = offsetof(struct btrfs_super_block, sys_chunk_array);
	cur = 0;

	while (cur < array_size) {
		disk_key = (struct btrfs_disk_key *)ptr;
		btrfs_disk_key_to_cpu(&key, disk_key);

		len = sizeof(*disk_key);
		ptr += len;
		sb_ptr += len;
		cur += len;

		if (key.type == BTRFS_CHUNK_ITEM_KEY) {
			chunk = (struct btrfs_chunk *)sb_ptr;
			ret = check_read_chunk(&key, &sb,
					       chunk, map, logical);
			if (ret)
				/* map is found */
				return ret;
			num_stripes = btrfs_chunk_num_stripes(&sb, chunk);
			len = btrfs_chunk_item_size(num_stripes);
		} else {
			errnum = ERR_FSYS_CORRUPT;
			return -1;
		}
		ptr += len;
		sb_ptr += len;
		cur += len;
		i++;
	}
	return 0;
}

/*
 * Search for a map by logical offset in the chunk tree.
 * Return 1 if map is found, otherwise return 0.
 */
static int chunk_tree_lookup(struct map_lookup *map,
			     u64 logical)
{
	int ret;
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_chunk *chunk;
	struct btrfs_path *path;

	path = btrfs_grab_path(INTERNAL_LOOKUP_POOL);

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = logical;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	ret = aux_tree_lookup(BTRFS_CHUNK_ROOT, &key, path);
	if (ret < 0)
		return 0;
	leaf = &path->nodes[0];
	slot = path->slots[0];
	if (ret == 1) {
		WARN_ON(slot == 0);
		slot -= 1;
	}
	btrfs_item_key_to_cpu(leaf, &found_key, slot);
	if (found_key.type != BTRFS_CHUNK_ITEM_KEY)
		return 0;
	chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
	return check_read_chunk(&found_key, leaf,
				chunk, map, logical);
}

/*
 * Btrfs logical/physical block mapper.
 * Look for an appropriate map-extent and
 * perform a translation. Return 1 on errors.
 */
static int btrfs_map_block(u64 logical, u64 *length,
			   struct btrfs_multi_bio *multi,
			   int mirror_num)
{
	struct map_lookup map;
	u64 offset;
	u64 stripe_offset;
	u64 stripe_nr;
	struct cache_extent *ce;
	int stripe_index;
	int i;
	int ret;

	memset(&map, 0, sizeof(map));
	ret = sys_array_lookup(&map, logical);
	if (ret == -1) {
		errnum = ERR_FSYS_CORRUPT;
		return 1;
	}
	if (ret == 0) {
		ret = chunk_tree_lookup(&map, logical);
		if (!ret) {
			/* something should be found! */
			errnum = ERR_FSYS_CORRUPT;
			return 1;
		}
	}
	/* do translation */
	ce = &map.ce;

	offset = logical - ce->start;
	stripe_nr = offset / map.stripe_len;
	stripe_offset = stripe_nr * map.stripe_len;
	WARN_ON(offset < stripe_offset);

	stripe_offset = offset - stripe_offset;

	if (map.type & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID1 |
			 BTRFS_BLOCK_GROUP_RAID10 |
			 BTRFS_BLOCK_GROUP_DUP)) {
		*length = min_t(u64, ce->size - offset,
			      map.stripe_len - stripe_offset);
	} else {
		*length = ce->size - offset;
	}
	multi->num_stripes = 1;
	stripe_index = 0;
	if (map.type & BTRFS_BLOCK_GROUP_RAID1) {
		if (mirror_num)
			stripe_index = mirror_num - 1;
		else
			stripe_index = stripe_nr % map.num_stripes;
	} else if (map.type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map.num_stripes / map.sub_stripes;

		stripe_index = stripe_nr % factor;
		stripe_index *= map.sub_stripes;

		if (mirror_num)
			stripe_index += mirror_num - 1;
		else
			stripe_index = stripe_nr % map.sub_stripes;

		stripe_nr = stripe_nr / factor;
	} else if (map.type & BTRFS_BLOCK_GROUP_DUP) {
		if (mirror_num)
			stripe_index = mirror_num - 1;
	} else {
		stripe_index = stripe_nr % map.num_stripes;
		stripe_nr = stripe_nr / map.num_stripes;
	}
	WARN_ON(stripe_index >= map.num_stripes);

	for (i = 0; i < multi->num_stripes; i++) {
		multi->stripes[i].physical =
			map.stripes[stripe_index].physical + stripe_offset +
			stripe_nr * map.stripe_len;
		memcpy(&multi->stripes[i].dev,
		       &map.stripes[stripe_index].dev,
		       sizeof(struct btrfs_device));
		stripe_index++;
	}
	return 0;
}

static u64 read_data_extent(u64 logical_start, u64 to_read, char *pos)
{
	int ret;
	u64 length;
	struct btrfs_multi_bio multi;

	while (to_read) {
		ret = btrfs_map_block(logical_start, &length, &multi, 0);
		if (ret) {
			errnum = ERR_FSYS_CORRUPT;
			return ret;
		}
		if (length > to_read)
			length = to_read;
		disk_read_func = disk_read_hook;
		ret = btrfs_devread(multi.stripes[0].dev.drive,
				    multi.stripes[0].dev.part,
				    multi.stripes[0].dev.length,
				    multi.stripes[0].physical >> SECTOR_BITS,
				    logical_start & ((u64)SECTOR_SIZE - 1),
				    length,
				    pos);
		disk_read_func = NULL;
		if (!ret)
			return 1;
		btrfs_msg("BTRFS data extent: read %llu bytes\n", length);
		to_read -= length;
		pos += length;
		logical_start += length;
	}
	return 0;
}

static int read_extent_from_disk(struct extent_buffer *eb)
{
	WARN_ON(eb->dev_bytenr % SECTOR_BITS);
	return btrfs_devread(eb->dev.drive,
			     eb->dev.part,
			     eb->dev.length,
			     eb->dev_bytenr >> SECTOR_BITS,
			     0,
			     eb->len,
			     eb->data);
}

static int verify_parent_transid(struct extent_buffer *eb, u64 parent_transid)
{
	return parent_transid && (btrfs_header_generation(eb) != parent_transid);
}

static int btrfs_num_copies(u64 logical, u64 len)
{
	return 1;
}

static int check_tree_block(struct btrfs_root *root, struct extent_buffer *buf)
{
	return 0;
}

static int csum_tree_block(struct btrfs_root *root, struct extent_buffer *buf,
		    int verify)
{
	return 0;
}

/*
 * Read a block of logical number @bytenr
 * from disk to buffer @eb.
 * Return 1 on success.
 */
int read_tree_block(struct btrfs_root *root,
		    struct extent_buffer *eb,
		    u64 bytenr, /* logical */
		    u32 blocksize,
		    u64 parent_transid,
		    lookup_pool_id lpid)
{
	int ret;
	int dev_nr;
	u64 length;
	struct btrfs_multi_bio multi;
	int mirror_num = 0;
	int num_copies;

	dev_nr = 0;
	length = blocksize;
	while (1) {
		ret = btrfs_map_block(bytenr,
				      &length, &multi, mirror_num);
		if (ret) {
			errnum = ERR_FSYS_CORRUPT;
			return 0;
		}
		init_extent_buffer(eb,
				   &multi.stripes[0].dev,
				   bytenr,
				   blocksize,
				   multi.stripes[0].physical,
				   lpid);

		ret = read_extent_from_disk(eb);
		if (ret &&
		    check_tree_block(root, eb) == 0 &&
		    csum_tree_block(root, eb, 1) == 0 &&
		    verify_parent_transid(eb, parent_transid) == 0)
			return 1;

		num_copies = btrfs_num_copies(eb->start, eb->len);
		if (num_copies == 1)
			break;
		mirror_num++;
		if (mirror_num > num_copies)
			break;
	}
	return 0;
}

/*
 * Read a child pointed by @slot node pointer
 * of @parent. Put the result to @parent.
 * Return 1 on success.
 */
static int parent2child(struct btrfs_root *root,
			struct extent_buffer *parent,
			int slot,
			lookup_pool_id lpid)
{
	int level;

	WARN_ON(slot < 0);
	WARN_ON(slot >= btrfs_header_nritems(parent));

	level = btrfs_header_level(parent);
	WARN_ON(level <= 0);

	return read_tree_block(root,
			       parent,
			       btrfs_node_blockptr(parent, slot),
			       btrfs_level_size(root, level - 1),
			       btrfs_node_ptr_generation(parent, slot),
			       lpid);
}

static int btrfs_comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	if (k1.objectid > k2->objectid)
		return 1;
	if (k1.objectid < k2->objectid)
		return -1;
	if (k1.type > k2->type)
		return 1;
	if (k1.type < k2->type)
		return -1;
	if (k1.offset > k2->offset)
		return 1;
	if (k1.offset < k2->offset)
		return -1;
	return 0;
}

static int bin_search(struct extent_buffer *eb, unsigned long p,
		      int item_size, struct btrfs_key *key,
		      int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	unsigned long offset;
	struct btrfs_disk_key *tmp;

	while(low < high) {
		mid = (low + high) / 2;
		offset = p + mid * item_size;

		tmp = (struct btrfs_disk_key *)(eb->data + offset);
		ret = btrfs_comp_keys(tmp, key);

		if (ret < 0)
			low = mid + 1;
		else if (ret > 0)
			high = mid;
		else {
			*slot = mid;
			return 0;
		}
	}
	*slot = low;
	return 1;
}

/* look for a key in a node */
static int node_lookup(struct extent_buffer *eb,
		       struct btrfs_key *key,
		       int *slot)
{
	if (btrfs_header_level(eb) == 0) {
		return bin_search(eb,
				  offsetof(struct btrfs_leaf, items),
				  sizeof(struct btrfs_item),
				  key, btrfs_header_nritems(eb),
				  slot);
	} else {
		return bin_search(eb,
				  offsetof(struct btrfs_node, ptrs),
				  sizeof(struct btrfs_key_ptr),
				  key, btrfs_header_nritems(eb),
				  slot);
	}
	return -1;
}

static inline int check_node(struct extent_buffer *buf, int slot)
{
	return 0;
}

/*
 * Look for an item by key in read-only tree.
 * Return 0, if key was found. Return -1 on io errors.
 *
 * Preconditions: btrfs_mount already executed.
 * Postconditions: if returned value is non-negative,
 * then path[0] represents the found position in the
 * tree. All components of the @path from leaf to root
 * are valid except their data buffers (only path[0]
 * has valid attached data buffer).
 */

int aux_tree_lookup(struct btrfs_root *root,
		    struct btrfs_key *key,
		    struct btrfs_path *path)
{
	int ret;
	int slot = 0;
	int level;
	struct extent_buffer node;
	init_extent_buffer(&node,
			   NULL,
			   0,
			   0,
			   0,
			   path->lpid);
	copy_extent_buffer(&node, &root->node);
	do {
		level = btrfs_header_level(&node);
		ret = check_node(&node, slot);
		if (ret)
			return -1;
		move_extent_buffer(&path->nodes[level],
				   &node);
		ret = node_lookup(&node, key, &slot);
		if (ret < 0)
			return ret;
		if (level) {
		        /*
			 * non-leaf,
			 * jump to the next level
			 */
			if (ret && slot > 0)
			        slot -= 1;
			ret = parent2child(root, &node, slot, path->lpid);
			if (ret == 0)
				return -1;
		}
		path->slots[level] = slot;
	} while (level);
	return ret;
}

static int readup_buffer(struct extent_buffer *buf, lookup_pool_id lpid)
{
	buf->data = grab_lookup_cache(lpid);
	return read_extent_from_disk(buf);
}

/*
 * Find the next leaf in accordance with tree order;
 * walk up the tree as far as required to find it.
 * Returns 0 if something was found, or 1 if there
 * are no greater leaves. Returns < 0 on io errors.
 *
 * Preconditions: all @path components from leaf to
 * root have valid meta-data fields. path[0] has a
 * valid attached data buffer with initial leaf.
 * Postcondition: the same as above, but path[0] has
 * an attached data buffer with the next leaf.
 */
static int btrfs_next_leaf(struct btrfs_root *root,
			   struct btrfs_path *path)
{
	int res;
	int slot;
	int level = 1;
	struct extent_buffer *buf;

	while(level < BTRFS_MAX_LEVEL) {
		buf = &path->nodes[level];
		slot = path->slots[level] + 1;
		/*
		 * lift data on this level
		 */
		res = readup_buffer(buf, path->lpid);
		if (!res)
			break;
		if (slot >= btrfs_header_nritems(buf)) {
			/* alas, go to parent (if any) */
			level++;
			res = 1;
			continue;
		}
		break;
	}
	if (!res)
		return 1;
	/*
	 * At this level slot points to
	 * the subtree we are interested in.
	 */
	path->slots[level] = slot;
	while(level) {
		struct extent_buffer tmp;
		move_extent_buffer(&tmp, &path->nodes[level]);
		res = parent2child(root, &tmp, slot, path->lpid);
		if (res == 0)
			return -1;
		level --;
		slot = 0;
		move_extent_buffer(&path->nodes[level], &tmp);
		path->slots[level] = slot;
	}
	return 0;
}

/* Preconditions: path is valid, data buffer
 * is attached to leaf node.
 * Postcondition: path is updated to point to
 * the next position with respect to the tree
 * order.
 *
 * Return -1 on io errors.
 * Return 0, if next item was found.
 * Return 1, if next item wasn't found (no more items).
 */
static int btrfs_next_item(struct btrfs_root *root,
			   struct btrfs_path *path)
{
	WARN_ON(path->slots[0] >= btrfs_header_nritems(&path->nodes[0]));

	path->slots[0] += 1;

	if (path->slots[0] < btrfs_header_nritems(&path->nodes[0]))
		return 0;
	if (coord_is_root(root, path))
		/* no more items */
		return 1;
	return btrfs_next_leaf(root, path);
}

/*
 * check if we can reuse results of previous
 * search for read operation
 */
static int path_is_valid(struct btrfs_path *path,
			 struct btrfs_key *key, u64 offset)
{
	btrfs_item_key_to_cpu(&path->nodes[0],
			      key,
			      path->slots[0]);
	if (BTRFS_FILE_INFO_KEY->objectid != key->objectid)
		return 0;
	if (btrfs_key_type(key) == BTRFS_INODE_ITEM_KEY)
		return 1;
	if (btrfs_key_type(key) != BTRFS_EXTENT_DATA_KEY)
		return 0;
	return BTRFS_FILE_INFO_KEY->offset <= offset;
}

/* ->read_func() */
int btrfs_read(char *buf, int len)
{
	int ret;
	struct btrfs_root *fs_root;
	struct btrfs_path *path;
	struct btrfs_key  path_key;
	u64 ioff;
	u64 bytes;
	int to_read;
	char *pos = buf;

	fs_root = BTRFS_FS_ROOT;
	path = btrfs_grab_path(FIRST_EXTERNAL_LOOKUP_POOL);

	if (!path_is_valid(path, &path_key, filepos)) {
		ret = aux_tree_lookup(fs_root, BTRFS_FILE_INFO_KEY, path);
		if (ret < 0)
			errnum = ERR_FSYS_CORRUPT;
	}
	while (!errnum) {
		struct btrfs_item *item;
		struct btrfs_file_extent_item *fi;
		u64 from;

		btrfs_item_key_to_cpu(&path->nodes[0],
				      &path_key,
				      path->slots[0]);
		if (BTRFS_FILE_INFO_KEY->objectid != path_key.objectid)
			break;
		if (btrfs_key_type(&path_key) != BTRFS_EXTENT_DATA_KEY)
			goto next;
		/*
		 * current position is extent item
		 */
		item = btrfs_item_nr(&path->nodes[0], path->slots[0]);
		fi = btrfs_item_ptr(&path->nodes[0],
				    path->slots[0],
				    struct btrfs_file_extent_item);
		if (btrfs_file_extent_compression(&path->nodes[0], fi)) {
		       btrfs_msg("Btrfs transparent compression unsupported\n");
		       errnum = ERR_BAD_FILETYPE;
		       goto exit;
		}
		ioff = filepos - path_key.offset;

		switch (btrfs_file_extent_type(&path->nodes[0], fi)) {
		case BTRFS_FILE_EXTENT_INLINE:
			bytes = btrfs_file_extent_inline_item_len(&path->
								  nodes[0],
								  item);
			if (path_key.offset + bytes < filepos)
				goto next;
			to_read = bytes - ioff;
			if (to_read > len)
				to_read = len;
			from = ioff + btrfs_file_extent_inline_start(fi);
			if (disk_read_hook != NULL) {
				disk_read_func = disk_read_hook;
				ret = btrfs_devread(path->nodes[0].dev.drive,
						    path->nodes[0].dev.part,
						    path->nodes[0].dev.length,
						    path->nodes[0].dev_bytenr >>
						    SECTOR_BITS,
						    from,
						    to_read,
						    pos);
				disk_read_func = NULL;
				if (ret)
					goto exit;
			} else
				memcpy(pos,
				       path->nodes[0].data + from,
				       to_read);
			btrfs_msg("BTRFS inline extent: read %d bytes pos %d\n",
				  to_read, filepos);
			break;
		case BTRFS_FILE_EXTENT_REG:
			bytes = btrfs_file_extent_num_bytes(&path->nodes[0],
							    fi);
			if (path_key.offset + bytes < filepos)
				goto next;
			to_read = bytes - ioff;
			if (to_read > len)
				to_read = len;
			from = ioff +
				btrfs_file_extent_disk_bytenr(&path->nodes[0],
							      fi) +
				btrfs_file_extent_offset(&path->nodes[0],
							 fi);
			ret = read_data_extent(from, to_read, pos);
			if (ret)
				goto exit;
			break;
		case BTRFS_FILE_EXTENT_PREALLOC:
			btrfs_msg("Btrfs preallocated extents unsupported\n");
			errnum = ERR_BAD_FILETYPE;
			goto exit;
		default:
			errnum = ERR_FSYS_CORRUPT;
			goto exit;
		}
		len -= to_read;
		pos += to_read;
		filepos += to_read;
		if (len == 0)
			break;
		/* not everything was read */
	next:
		ret = btrfs_next_item(fs_root, path);
		if (ret < 0) {
			errnum = ERR_FSYS_CORRUPT;
			break;
		}
		btrfs_update_file_info(path);
		continue;
	}
 exit:
	return errnum ? 0 : pos - buf;
}

static int btrfs_follow_link(struct btrfs_root *root,
			     struct btrfs_path *path,
			     char **dirname, char *linkbuf,
			     int *link_count,
			     struct btrfs_inode_item *sd)
{
	int ret;
	int len;
	char *name = *dirname;

	if (++(*link_count) > MAX_LINK_COUNT) {
		errnum = ERR_SYMLINK_LOOP;
		return 0;
	}
	/* calculate remaining name size */
	filemax = btrfs_inode_size(&path->nodes[0], sd);
	for (len = 0;
	     name[len] && isspace(name[len]);
	     len ++);

	if (filemax + len > PATH_MAX - 1) {
		errnum = ERR_FILELENGTH;
		return 0;
	}
	grub_memmove(linkbuf + filemax, name, len + 1);
	btrfs_update_file_info(path);
	filepos = 0;
	/* extract symlink content */
	while (1) {
		u64 oid = BTRFS_FILE_INFO_KEY->objectid;
		ret = btrfs_next_item(root, path);
		if (ret)
			break;
		btrfs_update_file_info(path);
		if (oid != BTRFS_FILE_INFO_KEY->objectid)
			break;
		if (btrfs_key_type(BTRFS_FILE_INFO_KEY) ==
		    BTRFS_EXTENT_DATA_KEY)
			goto found;
	}
	/* no target was found */
	errnum = ERR_FSYS_CORRUPT;
	return 0;
 found:
	/* fill the rest of linkbuf with the content */
	ret = btrfs_read(linkbuf, filemax);
	if (ret != filemax) {
		errnum = ERR_FSYS_CORRUPT;
		return 0;
	}
	return 1;
}

static int update_fs_root(struct btrfs_root *fs_root,
			  struct btrfs_key *location)
{
	int ret;
	struct btrfs_root *tree_root;

	if (location->offset != (u64)-1)
		return 0;
	tree_root = &BTRFS_FS_INFO->tree_root;
	ret = find_setup_root(tree_root,
			      tree_root->nodesize,
			      tree_root->leafsize,
			      tree_root->sectorsize,
			      tree_root->stripesize,
			      location->objectid,
			      fs_root,
			      0,
			      0,
			      0,
			      SECOND_EXTERNAL_LOOKUP_POOL);
	if (ret)
		return ret;
	location->objectid = btrfs_root_dirid(&fs_root->root_item);
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);
	location->offset = 0;
	return 0;
}

#ifndef STAGE1_5
static inline void update_possibilities(void)
{
	if (print_possibilities > 0)
		print_possibilities =
			-print_possibilities;
}
#endif

/*
 * Look for a directory item by name.
 * Print possibilities, if needed.
 * Postconditions: on success @sd_key points
 * to the key contained in the directory entry.
 */
static int btrfs_de_index_by_name(struct btrfs_root *root,
				  struct btrfs_path *path,
				  char **dirname,
				  struct btrfs_key *sd_key)
{
	char ch;
	int ret;
	char *rest;
	struct btrfs_dir_item *di;
#ifndef STAGE1_5
	int do_possibilities = 0;
#endif
	for (; **dirname == '/'; (*dirname)++);
	for (rest = *dirname;
	     (ch = *rest) && !isspace(ch) && ch != '/';
	     rest++);
	*rest = 0; /* for substrung() */
#ifndef STAGE1_5
	if (print_possibilities && ch != '/')
		do_possibilities = 1;
#endif
	/* scan a directory */
	while (1) {
		u32 total;
		u32 cur = 0;
		u32 len;
		struct btrfs_key di_key;
		struct btrfs_disk_key location;
		struct btrfs_item *item;

		/* extract next dir entry */
		ret = btrfs_next_item(root, path);
		if (ret)
			break;
		item = btrfs_item_nr(&path->nodes[0],
				     path->slots[0]);
		btrfs_item_key_to_cpu(&path->nodes[0],
				      &di_key,
				      path->slots[0]);
		if (di_key.objectid != sd_key->objectid)
			/* no more entries */
			break;
		di = btrfs_item_ptr(&path->nodes[0],
				    path->slots[0],
				    struct btrfs_dir_item);
		/*
		 * working around special cases:
		 * btrfs doesn't maintain directory entries
		 * which contain names "." and ".."
		 */
		if (!substring(".", *dirname)) {
#ifndef STAGE1_5
			if (do_possibilities) {
				update_possibilities();
				return 1;
			}
#endif
			goto found;
		}
		if (!substring("..", *dirname)) {
			if (di_key.type != BTRFS_INODE_REF_KEY)
				continue;
			sd_key->objectid = di_key.offset;
			btrfs_set_key_type(sd_key, BTRFS_INODE_ITEM_KEY);
			sd_key->offset = 0;
#ifndef STAGE1_5
			if (do_possibilities) {
				update_possibilities();
				return 1;
			}
#endif
			goto found;
		}
		if (di_key.type != BTRFS_DIR_ITEM_KEY)
			continue;
		total = btrfs_item_size(&path->nodes[0], item);
		/* scan a directory item */
		while (cur < total) {
			char tmp;
			int result;
			char *filename;
			char *end_of_name;
			int name_len;
			int data_len;

			btrfs_dir_item_key(&path->nodes[0], di, &location);

			name_len = btrfs_dir_name_len(&path->nodes[0], di);
			data_len = btrfs_dir_data_len(&path->nodes[0], di);

			WARN_ON(name_len > BTRFS_NAME_LEN);

			filename = (char *)(path->nodes[0].data +
					    (unsigned long)(di + 1));
			end_of_name = filename + name_len;
			/*
			 * working around not null-terminated
			 * directory names in btrfs: just
			 * a short-term overwrite of the
			 * cache with the following rollback
			 * of the change.
			 */
			tmp = *end_of_name;
			*end_of_name = 0;
			result = substring(*dirname, filename);
			*end_of_name = tmp;
#ifndef STAGE1_5
			if (do_possibilities) {
				if (result <= 0) {
					update_possibilities();
					*end_of_name = 0;
					print_a_completion(filename);
					*end_of_name = tmp;
				}
			}
			else
#endif
				if (result == 0) {
				      btrfs_dir_item_key_to_cpu(&path->nodes[0],
								di, sd_key);
				      goto found;
				}
			len = sizeof(*di) + name_len + data_len;
			di = (struct btrfs_dir_item *)((char *)di + len);
			cur += len;
		}
	}
#ifndef STAGE1_5
	if (print_possibilities < 0)
		return 1;
#endif
	errnum = ERR_FILE_NOT_FOUND;
	*rest = ch;
	return 0;
 found:
	*rest = ch;
	*dirname = rest;
	return 1;
}

/*
 * ->dir_func().
 * Postcondition: on a non-zero return BTRFS_FS_INFO
 * contains the latest fs_root of file's subvolume.
 * BTRFS_FS_INFO points to a subvolume of a file we
 * were trying to look up.
 * BTRFS_FILE_INFO contains info of the file we were
 * trying to look up.
 */

int btrfs_dir(char *dirname)
{
	int ret;
	int mode;
	u64 size;
	int linkcount = 0;
	char linkbuf[PATH_MAX];

	struct btrfs_path *path;
	struct btrfs_root *root;

	struct btrfs_key sd_key;
	struct btrfs_inode_item *sd;
	struct btrfs_key parent_sd_key;

	root = BTRFS_FS_ROOT;
	path = btrfs_grab_path(FIRST_EXTERNAL_LOOKUP_POOL);

	btrfs_set_root_dir_key(&sd_key);
	while (1) {
		struct extent_buffer *leaf;
		ret = aux_tree_lookup(root, &sd_key, path);
		if (ret)
			return 0;
		leaf = &path->nodes[0];
		sd = btrfs_item_ptr(leaf,
				    path->slots[0],
				    struct btrfs_inode_item);
		mode = btrfs_inode_mode(leaf, sd);
		size = btrfs_inode_size(leaf, sd);
		switch (btrfs_get_file_type(mode)) {
		case BTRFS_SYMLINK_FILE:
			ret = btrfs_follow_link(root,
						path,
						&dirname,
						linkbuf,
						&linkcount,
						sd);
			if (!ret)
				return 0;
			dirname = linkbuf;
			if (*dirname == '/')
				/* absolute name */
				btrfs_set_root_dir_key(&sd_key);
			else
				memcpy(&sd_key, &parent_sd_key,
				       sizeof(sd_key));
			continue;
		case BTRFS_REGULAR_FILE:
			/*
			 * normally we want to exit here
			 */
			if (*dirname && !isspace (*dirname)) {
				errnum = ERR_BAD_FILETYPE;
				return 0;
			}
			filepos = 0;
			filemax = btrfs_inode_size(leaf, sd);
			btrfs_update_file_info(path);
			return 1;
		case BTRFS_DIRECTORY_FILE:
			memcpy(&parent_sd_key, &sd_key, sizeof(sd_key));
			ret = btrfs_de_index_by_name(root,
						     path,
						     &dirname,
						     &sd_key);
			if (!ret)
				return 0;
#ifndef STAGE1_5
			if (print_possibilities < 0)
				return 1;
#endif
			/*
			 * update fs_tree:
			 * subvolume stuff goes here
			 */
			ret = update_fs_root(root, &sd_key);
			if (ret)
				return 0;
			continue;
		case BTRFS_UNKNOWN_FILE:
		default:
			btrfs_msg("Btrfs: bad file type\n");
			errnum = ERR_BAD_FILETYPE;
			return 0;
		}
	}
}

int btrfs_embed(int *start_sector, int needed_sectors)
{
	int ret;
	init_btrfs_info();
	init_btrfs_volatile_dev_cache();

	ret = btrfs_find_super(BTRFS_VOLATILE_DEV_CACHE, NULL, NULL);
	if (ret)
		return 0;
	ret = btrfs_uptodate_super_copy(BTRFS_FS_INFO);
	if (ret)
		return 0;
	*start_sector = 1; /* reserve first sector for stage1 */
	return needed_sectors <=
		((BTRFS_SUPER_INFO_OFFSET >> SECTOR_BITS) - 1);
}
#endif /* FSYS_BTRFS */

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
