# DMA Matrix Multiply Linux Device Driver (Learning Project)

## Overview

This project implements a **Linux kernel device driver that simulates a hardware matrix multiplication accelerator**. The main goal of this project is **educational**: it was created to learn how Linux device drivers interact with user-space programs and how software communicates with hardware accelerators.

The driver exposes a character device:


/dev/dma_mtx_mul


User-space applications can submit matrix multiplication jobs to this device. The driver simulates a hardware accelerator using a **kernel worker thread** that processes jobs asynchronously.

This project demonstrates several important concepts used in real hardware accelerator drivers (such as GPUs, AI accelerators, or NPUs):

- Linux **character device driver development**
- **ioctl-based device control**
- **DMA buffer allocation** using `dma_alloc_coherent`
- **asynchronous job submission**
- **descriptor-based execution**
- **kernel worker threads**
- **wait queues and polling**
- **multi-process safe usage**
- **user-space ↔ kernel-space communication**

This driver is **not intended for production use**. It is purely a **learning project** for understanding Linux kernel programming and hardware accelerator interfaces.

---

# Architecture

The driver simulates a hardware accelerator with the following structure:


User Application
│
│ ioctl / read / write
▼
Linux Kernel Driver
│
│ job submission queue
▼
Worker Thread (simulated hardware engine)
│
▼
Matrix multiplication execution


The driver manages:

- DMA buffers used for matrix input/output
- a **pending job queue**
- a **completion queue per process**
- a worker thread that simulates hardware execution

Multiple user applications can open the device at the same time. Each process receives **its own job completions**, so results from one process will not be returned to another process.

---

# Driver Features

### Buffer Management

Buffers are allocated in DMA-capable memory using:


DMA_MTX_MUL_IOCTL_ALLOC_BUF
DMA_MTX_MUL_IOCTL_FREE_BUF


Each process owns the buffers it allocates.

---

### Data Transfer

Matrix data is transferred using standard file operations:


write()
read()


- `write()` sends matrix data into a buffer
- `read()` retrieves matrix data or results from a buffer

---

### Job Submission

Matrix multiplication jobs are submitted using:


DMA_MTX_MUL_IOCTL_SUBMIT_DESC


Each job descriptor specifies:

- matrix dimensions
- input buffer A
- input buffer B
- output buffer C

The job is placed into a **pending queue** and executed asynchronously.

---

### Completion Handling

Jobs complete asynchronously. Applications can wait for completion using:


DMA_MTX_MUL_IOCTL_WAIT
DMA_MTX_MUL_IOCTL_DEQUEUE_DONE


This simulates how real hardware accelerators signal job completion.

---

# Building the Driver

From the driver directory, run:


make


This builds the kernel module:


dma_mtx_mul.ko


---

# Loading the Driver

Unload any previously loaded version:


sudo rmmod dma_mtx_mul


Insert the module:


sudo insmod dma_mtx_mul.ko


Check kernel messages:


dmesg | tail


You should see output similar to:


dma_mtx_mul: driver loaded successfully


Verify the device exists:


ls /dev/dma_mtx_mul


---

# Building the Test Program

Compile the test program with:


gcc dma_mtx_mul_test.c -o dma_mtx_mul_test -I../include -pthread


---

# Running the Test

Run the test program:


./dma_mtx_mul_test


The test program performs the following steps:

1. Opens the device
2. Allocates DMA buffers
3. Writes matrix data to buffers
4. Submits matrix multiplication jobs
5. Waits for completion
6. Reads the result
7. Verifies correctness against a CPU implementation
8. Runs multiple threads concurrently

Example output:


thread 0: ops=1045 errors=0
thread 1: ops=1022 errors=0
thread 2: ops=1034 errors=0
thread 3: ops=1018 errors=0

TOTAL: ops=4119 errors=0


This confirms the driver works correctly under concurrent usage.

---

# Unloading the Driver

After testing, unload the driver:


sudo rmmod dma_mtx_mul


---

# Educational Purpose

This project helps illustrate how modern accelerator drivers operate. Many real devices (such as GPUs, AI accelerators, and FPGAs) use similar design patterns:

- command submission queues
- DMA memory buffers
- asynchronous execution
- interrupt or event-based completion

Although this driver simulates the hardware computation in software, the architecture closely resembles real accelerator driver designs.

---

# Author

Urban Klobcic

Linux device driver learning project for understanding low-level hardwa