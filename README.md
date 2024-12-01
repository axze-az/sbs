# sbs

simple batch system similar to batch/at/atd

## Description

sbs (simple batch system) is a batch system similar to the batch part of at.
It manages queues of jobs with priorities between 0 and 100 at different nice
levels. These queues may be active only on a part of a day.

## Getting Started

### Dependencies
- libpam-dev
- the program was tested only under linux
- it has never been compiled with a compiler other than gcc

### Build and test

- Execute `fakeroot debian/rules binary` to produce a debian package 
- otherwise you may edit the Makefile in the root directory to change
  paths and compile it

## License

This project is licensed under the GPL v2.0 License.
