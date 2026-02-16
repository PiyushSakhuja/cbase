# MyDB – Design Document

## 1. Overview
MyDB is a simple disk-based relational database written in C++.
The goal of this project is to understand how real databases work internally,
including disk storage, paging, indexing, and recovery.

This is an educational database, not production-ready.

---

## 2. Language & Build
- Language: C++17
- Compiler: g++ 
- Platform: Windows
- Binary file storage (no text storage)

---

## 3. Storage Model

### 3.1 Database File
- Single database file: `mydb.db`
- File grows dynamically as pages are added
- Data persists across program restarts

---

### 3.2 Page Size
- Fixed page size: **4096 bytes (4KB)**
- All disk reads and writes happen in page-sized blocks
- Page IDs are zero-based integers

