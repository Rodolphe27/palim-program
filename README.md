# Palim - Parallel File Content Search

**Palim** is a parallel file content search tool written in C. It allows you to search for a specific string in the content of files within a directory tree and provides statistics about the search process. This tool is designed to take advantage of multithreading to efficiently search through a large number of files and directories.

## Features

- Parallel file content search using multiple threads.
- Detailed statistics on the number of lines, files, and directories processed.
- Configurable maximum grep threads for optimization.

## Prerequisites

Before using Palim, you need to have the following prerequisites:

- A C compiler (e.g., GCC)
- pthreads library
- SEM library
- A Linux-based system (for Linux-specific system calls)

## Usage

To use Palim, follow these steps:

1. Compile the program using your C compiler. For example, you can use GCC:

   ```shell
   gcc -o palim palim.c -lpthread
