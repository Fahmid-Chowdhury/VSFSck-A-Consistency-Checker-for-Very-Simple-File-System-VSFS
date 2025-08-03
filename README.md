# 🛠️ VSFSck: Consistency Checker for Very Simple File System (VSFS)

This project is a file system consistency checker (`vsfsck`) implemented in C, developed for CSE321 Project 2 at BRAC University. It inspects and validates the internal structures of a custom file system image (VSFS), identifies inconsistencies, and helps restore file system integrity.

---

## 📂 File Overview

- `checker.c` – Main consistency checker (vsfsck) for verifying superblock, inode bitmap, and data bitmap.
- `fix.c` – Small fix tool for manipulating the inode bitmap (used to simulate or patch issues).
- `vsfs.img` – A virtual file system image (expected input).
- `README.md` – Project documentation and usage instructions.

---

## 📋 Features Implemented

### ✅ Superblock Validator
- Checks:
  - Magic number (`0xD34D`)
  - Block size (4096)
  - Total blocks (64)
  - Inode/data bitmap locations, inode table, and data block start
  - Inode size (256)

### ✅ Inode Bitmap Consistency Checker
- Ensures:
  - Allocated inodes have valid links and no deletion timestamp
  - All active inodes are correctly reflected in the inode bitmap

### ✅ Data Bitmap Consistency Checker
- Ensures:
  - All blocks marked used are referenced by inodes
  - All blocks used by inodes are marked in the bitmap
  - Reports duplicate block usage and out-of-bounds references

### ✅ Duplicate & Bad Block Checker
- Detects:
  - Blocks referenced by multiple inodes
  - Blocks outside valid range

---

## ⚙️ File System Layout (VSFS)

- Block size: `4096 Bytes`
- Total blocks: `64`
- Layout:
  - `Block 0`: Superblock
  - `Block 1`: Inode Bitmap
  - `Block 2`: Data Bitmap
  - `Blocks 3–7`: Inode Table
  - `Blocks 8–63`: Data Blocks

---

## 🧪 How to Build

Compile both files using `gcc`:
gcc -o vsfsck checker.c
gcc -o fixer fix.c

## ▶️ How to Run
Run checker:
./vsfsck vsfs.img

Run fixer (to patch inode bitmap index 13, for example):
./fixer vsfs.img

## 🖥️ Sample Output
╔════════════════════════════════════════╗  
║            SUPERBLOCK INFO             ║  
╚════════════════════════════════════════╝  

Checking inode bitmap consistency...  
╔════════════╦════════════╦══════════════╗  
║ Inode      ║ Bitmap     ║ Status       ║  
╚════════════╩════════════╩══════════════╝  

Checking data bitmap consistency...  
DUPLICATE: Inode 5 references block 10 which is already referenced  
MISSING_BITMAP: Block 12 referenced 1 times but marked free  
