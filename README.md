CBase is a lightweight database storage engine implemented in C++ from scratch.
It demonstrates how real database systems manage disk storage, memory caching, and record organization internally.

This project implements:

  -Persistent disk storage
  -Fixed-size page management
  -Slotted page architecture
  -Buffer pool caching
  -Logical deletion
  -RID-based record addressing
  -CLI interface for interaction

The goal of this project is to deeply understand how database systems like MySQL and PostgreSQL manage data internally.

🏗 Architecture
CLI
  ↓
HeapFile (Record Manager)
  ↓
BufferPool (Memory Cache)
  ↓
DiskManager (Disk I/O)
  ↓
Binary File (Persistent Storage)

# 🔧 Components

1️⃣ DiskManager

Handles low-level disk I/O in binary mode.

Reads and writes fixed-size pages

Maintains persistent storage

Maps logical page IDs to file offsets

Offset formula:

offset = page_id * PAGE_SIZE

2️⃣ Page

Represents an in-memory page.

Raw byte storage (char data[PAGE_SIZE])

Dirty flag for write-back tracking

Page ID assignment

3️⃣ BufferPool

Caches pages in memory.

Fetches pages from disk

Avoids repeated disk I/O

Flushes dirty pages to disk on exit

Implements basic page caching without eviction (v1 design).

4️⃣ HeapFile

Manages records using a slotted-page layout.

Supports:

Insert

Delete (logical)

Sequential scan

Uses:

PageHeader

Slot Directory

Free space tracking

RID (Record Identifier)

📦 Slotted Page Layout

Each page is structured as:

---------------------------------
| PageHeader                   |
---------------------------------
| Slot Directory               |
---------------------------------
|           Free Space         |
---------------------------------
| Record Data (grows upward)   |
---------------------------------


Records grow from bottom upward.
Slot directory grows downward.
When they meet → page is full.

🆔 Record Identifier (RID)

Each record is uniquely identified by:

(page_id, slot_id)


This ensures stable addressing even after deletions.

🗑 Logical Deletion

Records are not physically removed.
Instead:

slot.is_used = 0


This avoids shifting memory and keeps RIDs stable.

💾 Persistence

Data is persisted to disk using:

Binary file storage

Dirty page tracking

Explicit flush on exit

Data survives program restarts.

▶ How To Build

From project root:

g++ -std=c++17 -Wall main.cpp storage/buffer_pool.cpp storage/disk_manager.cpp storage/heap_file.cpp storage/page.cpp -o final


Run:

./final        (Linux/macOS)
.\final.exe    (Windows PowerShell)

🖥 CLI Commands (v1)
1. Insert
2. Delete
3. Scan
4. Exit

🧠 Key Concepts Learned

Disk block alignment

Buffer pool caching

Slotted page design

Free space management

Logical deletion

Persistent file-backed storage

Layered system architecture
