// On-disk file system format.
// Both the kernel and user programs use this header file.

#define ROOTINO 1 // root i-number
#define BSIZE 1024 // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
    uint_t magic; // Must be FSMAGIC
    uint_t size; // Size of file system image (blocks)
    uint_t nblocks; // Number of data blocks
    uint_t ninodes; // Number of inodes.
    uint_t nlog; // Number of log blocks
    uint_t logstart; // Block number of first log block
    uint_t inodestart; // Block number of first inode block
    uint_t bmapstart; // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint_t))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
    short type; // File type
    short major; // Major device number (T_DEVICE only)
    short minor; // Minor device number (T_DEVICE only)
    short nlink; // Number of links to inode in file system
    uint_t size; // Size of file (bytes)
    uint_t addrs[NDIRECT + 1]; // Data block addresses
};

// Inodes per block.
#define IPB (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB (BSIZE * 8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b) / BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
    ushort_t inum;
    char name[DIRSIZ];
};
