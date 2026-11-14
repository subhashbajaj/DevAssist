# DevAssist - Linux Developer Assistant

An Operating Systems project written in C. DevAssist is a command-line tool that helps
Linux developers diagnose common system and project problems from one menu. It collects
information from the operating system, applies simple rules, explains the problem in plain
language and suggests a solution. It is read-only: it never deletes or changes files.

## Features

1. **Process Manager** - lists running processes, lists listening ports, and checks whether a specific port is already in use (with a warning and a suggestion).
2. **Disk Analyzer** - shows disk and inode usage with an 80% warning, finds large files, and shows which folders use the most space.
3. **Log Analyzer** - counts errors and warnings in a log file, recognises common problems with their cause and fix, and prints an overall verdict.
4. **Permission Checker** - shows a file's permissions, owner and group, and warns about risky settings such as files writable by everyone.
5. **Project Health Checker** - runs 9 checks on a project folder (Git, README, build file, .gitignore, source files, tests, src folder, LICENSE, compiled files) and gives a score out of 100.
6. **CPU & RAM Monitor** - reads /proc to report CPU and memory usage, explains problems and suggests fixes.
7. **Exit**

## Requirements

- Ubuntu Linux (tested in VirtualBox)
- GCC and GNU Make
- The standard tools ps and ss (installed by default on Ubuntu)

## Build and run

    make clean
    make
    ./devassist

## Project structure

    src/       main.c and one .c file per module
    include/   one .h header per module
    Makefile   builds everything into the devassist program

## Team

- Subhash Bajaj (023-24-0252) - Process & Port Manager, integration
- Krishna (023-24-0271) - Disk Analyzer, Project Health Checker
- Sneha Kanjwani (023-24-0273) - Permission Checker, Log Analyzer, CPU & RAM Monitor
