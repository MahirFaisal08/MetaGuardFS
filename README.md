# MetaGuardFS

![Language](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Status](https://img.shields.io/badge/status-Completed-brightgreen)

## Overview
**MetaGuardFS** is a crash-consistent journaling extension for a VSFS-like file system.  
It implements **metadata journaling** to ensure reliability and consistency after crashes or unexpected shutdowns.

This project demonstrates how journaling can preserve file system integrity by first logging metadata updates to a dedicated journal before applying them to their actual disk locations.

---
