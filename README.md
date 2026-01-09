# MetaGuardFS

![Language](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Status](https://img.shields.io/badge/status-Completed-brightgreen)

## Overview
**MetaGuardFS** is a crash-consistent journaling extension for a VSFS-like file system.  
It implements **metadata journaling** to ensure reliability and consistency after crashes or unexpected shutdowns.

This project demonstrates how journaling can preserve file system integrity by first logging metadata updates to a dedicated journal before applying them to their actual disk locations.

---
## Features
- Metadata journaling for crash consistency  
- Safe recovery and atomic metadata updates  
- Two journaling operations: `create` and `install`  
- Journal header with magic number & used bytes tracking  
- COMMIT record for transaction sealing  
- Journal replay and checkpointing  
- Multiple validation and consistency checks  

---
## Project Specs

| Property | Details |
|-----------|----------|
| **Type** | File System Project |
| **Focus** | Metadata Journaling (Crash Consistency) |
| **Language** | C |
| **Block Size** | 4 KB |
| **Total Blocks** | 85 |
| **Status** | Completed |

---
## Filesystem Layout

| Component | Blocks | Description |
|------------|---------|-------------|
| Superblock | 1 | File system metadata descriptor |
| Journal | 16 | Logs all metadata updates |
| Inode Bitmap | 1 | Tracks used/free inodes |
| Data Bitmap | 1 | Tracks used/free data blocks |
| Inode Table | 2 | Contains inode structures |
| Data Blocks | 64 | User and directory data |
| **Total** | **85 blocks (4 KB each)** | — |

---
## Compilation & Run Commands

```bash
# Compile all programs
gcc -Wall -Wextra -o mkfs mkfs.c
gcc -Wall -Wextra -o validator validator.c
gcc -Wall -Wextra -o journal journal.c

# Create a clean image and check consistency
./mkfs
./validator

# Create operations (journal logging)
./journal create file1
./validator
./journal create file2
./validator

# Apply committed transactions
./journal install
./validator

# Multiple create + install tests
./mkfs
./journal create a
./journal create b
./journal create c
./validator
./journal install
./validator
./journal install

# Final checks
./mkfs
./validator
ls -l vsfs.img journal mkfs validator
```
---
## Validation & Debug Commands

```bash
# Dump home area (Excluding Journal)
./mkfs
dd if=vsfs.img of=home_before.bin bs=4096 skip=17 count=68 status=none

# Create and check difference:
./journal create x
dd if=vsfs.img of=home_after.bin bs=4096 skip=17 count=68 status=none
sha256sum home_before.bin home_after.bin

# Stress test - fill journal until full
./mkfs
for i in $(seq 1 200); do ./journal create f$i || break; done

# Corrupt journal header manually
./mkfs
printf '\0\0\0\0\0\0\0\0' | dd of=vsfs.img bs=1 seek=$((1*4096)) count=8 conv=notrunc status=none
./journal install
```
---
## Example Workflow

```bash
./mkfs
./journal create file1
./validator
./journal install
./validator
```

## Future Improvements

- Add journaling for data blocks (not only metadata)  
- Implement partial transaction rollback  
- Add visualization for journal replay  
- Extend to multi-level directory structure  
- Include recovery simulation after crash  

---
## Author

**Name:** Md. Mahir Faisal  
**Project:** MetaGuardFS – Metadata Journaling for VSFS  
**Year:** ![Year](https://img.shields.io/badge/2026-blue)  
**Language:** ![Language](https://img.shields.io/badge/C-orange)

---
## License

This project is licensed under the **MIT License** – you are free to use, modify, and distribute this software with proper credit.  
For details, see the LICENSE file in this repository or visit the official MIT License page.
---
⭐ **Star this repo** if you found it helpful!  
📫 For any queries or collaborations, contact **[Md. Mahir Faisal](https://github.com/MahirFaisal08)**
---
