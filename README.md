# University Course Registration System

A multi-threaded course registration simulator built in C using POSIX threads. Each student runs as an independent thread competing concurrently for limited seats in courses, with high-priority students given a scheduling head-start over normal-priority students.

---

## Authors

| Name | Roll Number |
|---|---|
| Roay Muhammad Abdullah | 24F-0570 |
| Muhammad Subhan Yousaf | 24F-0820 |
| Abdul Hayee Kamran | 24F-0596 |

---

## Features

- Concurrent registration using POSIX threads (one thread per student)
- Per-course mutex locking — no deadlock possible since only one lock is held at a time
- Priority-based scheduling via a startup delay for normal-priority threads
- Two simulation modes: randomised full simulation and a mandatory stress-test scenario
- Thread-safe logging with timestamps (millisecond resolution)
- ANSI colour-coded terminal output for instant readability
- Seat invariant check after simulation to verify correctness
- Robust input validation and error handling throughout

---

## Synchronisation Design

```
Student Thread (HIGH)  ──────────────────────────────► tries Course mutex
Student Thread (NORMAL) ── sleep(80ms) ──────────────► tries Course mutex
                                             │
                                    Course.mutex (per-course)
                                             │
                                    availableSeats-- on success
```

Each course has its own dedicated `pthread_mutex_t`. A thread locks only the mutex for the specific course it is currently trying to register in, then immediately releases it. Because a thread never holds more than one lock at a time, circular wait (and therefore deadlock) is structurally impossible.

---

## Project Structure

```
registration-system/
├── registration.c      # Full source — all logic in one file
└── README.md
```

---

## Building

Requires GCC and POSIX threads (standard on Linux/macOS).

```bash
gcc -Wall -Wextra -o registration registration.c -lpthread
```

---

## Usage

```bash
# Default: full simulation with 100 students
./registration

# Full simulation with a custom student count (1-200)
./registration <N>

# Mandatory stress-test: 10 students, 3 tight-capacity courses
./registration test
```

---

## Simulation Modes

### Full Simulation (`./registration [N]`)

Loads eight courses with varying capacities. Students are created with random priorities (roughly 25% high-priority) and random course selections (up to 3 requests each).

| Course | Seats |
|---|---|
| CS101 | 5 |
| CS102 | 8 |
| CS103 | 10 |
| CS104 | 4 |
| CS105 | 6 |
| CS106 | 2 |
| CS107 | 12 |
| CS108 | 7 |

### Mandatory Stress-Test (`./registration test`)

Loads three deliberately tight-capacity courses. All 10 students request every course, so heavy contention is guaranteed. Students 1-3 are high-priority; students 4-10 are normal.

| Course | Seats |
|---|---|
| CS101 | 2 |
| CS102 | 1 |
| CS103 | 3 |

This mode is designed so that many registration attempts are expected to fail, directly verifying mutex correctness under contention.

---

## Sample Output

```
╔══════════════════════════════════════════════════════╗
║       University Course Registration System          ║
║    CL-2006 Operating Systems Lab — Final Project     ║
╚══════════════════════════════════════════════════════╝

[MODE] Mandatory Test Scenario

Courses Loaded:
  CS101    → 2 seats
  CS102    → 1 seat
  CS103    → 3 seats

Students:
  Total                   : 10
  High-Priority (final-yr): 3
  Normal-Priority         : 7

┌── Registration Log ──────────────────────────────────────┐
[14:32:01.042] Student   1 | Priority: HIGH   | Course: CS101  | SUCCESS
[14:32:01.043] Student   2 | Priority: HIGH   | Course: CS102  | SUCCESS
[14:32:01.044] Student   3 | Priority: HIGH   | Course: CS101  | FAILED – No Seats
...
└──────────────────────────────────────────────────────────┘

╔══════════════════════════════════════════════════════╗
║         FINAL COURSE SEAT ALLOCATION                 ║
╚══════════════════════════════════════════════════════╝
 Course    Total  Filled  Remaining  Status
 ------    -----  ------  ---------  ------
 CS101         2       2          0  OK
 CS102         1       1          0  OK
 CS103         3       3          0  OK

 Successful Registrations : 6
 Failed Registrations     : 24
 Total Attempts           : 30
 Seat Invariant Check     : PASSED ✓
 Deadlock                 : None (one mutex per course at a time)
```

---

## Key Concepts Demonstrated

| Concept | Implementation |
|---|---|
| Thread creation | `pthread_create()` — one thread per student |
| Thread joining | `pthread_join()` — main waits for all threads |
| Mutual exclusion | `pthread_mutex_lock/unlock()` per course |
| Priority scheduling | `usleep(80ms)` delay for normal-priority threads |
| Race condition prevention | Seat decrement inside the critical section |
| Resource cleanup | `pthread_mutex_destroy()` for all mutexes on exit |

---

## Error Handling

Every public function validates its arguments. Notable guarantees:

- `NULL` thread arguments are caught and logged; the thread exits cleanly without a segfault
- Out-of-range course indices in a student's request list are skipped with a warning
- `pthread_create()` failures are reported with `strerror()`; all already-started threads are joined before the program exits
- `clock_gettime()` failures produce a `??:??:??.???` placeholder rather than crashing the logger
- Student count arguments that are non-numeric, zero, or out of range produce a usage message and clean exit

---

## Limitations

- Priority is simulated via a fixed sleep delay, not via OS-level thread priority (`SCHED_FIFO` / `SCHED_RR`). Under very high system load the delay may not reliably guarantee ordering.
- Each student may request at most 3 courses (`MAX_REQUESTS`). This is a design constraint of the assignment, not a technical limitation.
- The system stores state in global arrays. It is not designed for shared-library or multi-instance use.
