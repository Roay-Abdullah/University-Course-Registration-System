/* ================================================================
 * University Course Registration System
 * Operating Systems Lab — Final Project
 * ================================================================
 *
 * Simulates concurrent student registration using POSIX threads.
 * Each student runs as a separate thread and competes for seats
 * in courses, with high-priority students given scheduling
 * preference via a startup delay for normal-priority threads.
 *
 * Synchronisation : per-course mutexes (no deadlock possible
 *                   because only one lock is held at a time)
 * Exception safety : every public entry-point validates its
 *                   arguments and returns/logs an error rather
 *                   than crashing on bad input.
 *
 * Authors : Roay MuhammadAbdullah  (24F-0570)
 *           Abdul Hayee Kamran     (24F-0596)
 *           Muhammad Subhan Yousaf (24F-0820)
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

/* ── Configuration ─────────────────────────────────────────── */
#define MAX_COURSES 10
#define MAX_STUDENTS 200
#define MAX_REQUESTS 3
#define PRIORITY_NORMAL 0
#define PRIORITY_HIGH  1
#define PRIORITY_DELAY_US 80000   /* 80 ms head-start for HIGH priority */

/* ── ANSI colour codes ─────────────────────────────────────── */
#define CLR_RESET "\033[0m"
#define CLR_BOLD "\033[1m"
#define CLR_GREEN "\033[32m"
#define CLR_RED "\033[31m"
#define CLR_YELLOW "\033[33m"
#define CLR_CYAN "\033[36m"
#define CLR_BLUE "\033[34m"

/* ── Data structures ───────────────────────────────────────── */
typedef struct 
{
    int  id;
    char name[16];
    int  totalSeats;
    int  availableSeats;
    pthread_mutex_t mutex;
} Course;

typedef struct 
{
    int id;
    int priority;
    int requestedCourses[MAX_REQUESTS];
    int numRequests;
    int enrolled[MAX_COURSES];
} Student;

/* ── Global state ──────────────────────────────────────────── */
static Course courses[MAX_COURSES];
static int numCourses  = 0;
static Student students[MAX_STUDENTS];
static int numStudents = 0;
static pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
static int totalSuccess = 0;
static int totalFailed  = 0;

/* ================================================================
 * getTimestamp
 * ----------------------------------------------------------------
 * Writes the current wall-clock time as "HH:MM:SS.mmm" into the
 * caller-supplied buffer.
 *
 * Parameters:
 *   buf  – destination character array
 *   len  – size of buf in bytes (must be >= 16)
 *
 * Exceptions / edge-cases:
 *   • If clock_gettime() fails, the buffer is filled with the
 *     placeholder string "??:??:??.???" so logging never crashes.
 *   • If buf is NULL or len is 0 the function returns immediately.
 * ================================================================ */
static void getTimestamp(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }
    
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) 
    {
        strncpy(buf, "??:??:??.???", len - 1);
        buf[len - 1] = '\0';
        return;
    }

    struct tm *t = localtime(&ts.tv_sec);
    if (t == NULL) 
    {
        strncpy(buf, "??:??:??.???", len - 1);
        buf[len - 1] = '\0';
        return;
    }

    snprintf(buf, len, "%02d:%02d:%02d.%03ld", t->tm_hour, t->tm_min, t->tm_sec, ts.tv_nsec / 1000000L);
}

/* ================================================================
 * logAttempt
 * ----------------------------------------------------------------
 * Thread-safe logging of a single registration attempt.
 * Acquires logMutex, prints one formatted line to stdout, updates
 * the global success/failure counters, then releases the lock.
 *
 * Parameters:
 *   sid – student ID (must be > 0)
 *   priority – PRIORITY_HIGH or PRIORITY_NORMAL
 *   cname – course name string (must not be NULL)
 *   success – non-zero on successful registration, zero on failure
 *
 * Exceptions / edge-cases:
 *   • NULL cname is replaced with the string "<unknown>" so the
 *     program never dereferences a null pointer.
 *   • An unrecognised priority value is displayed as "UNKNOWN"
 *     instead of causing undefined behaviour.
 * ================================================================ */
static void logAttempt(int sid, int priority, const char *cname, int success)
{
    if (cname == NULL)
    {
        cname = "<unknown>";
    }

    char ts[32];
    getTimestamp(ts, sizeof(ts));

    const char *prioStr;
    switch (priority) 
    {
        case PRIORITY_HIGH:
            prioStr = CLR_YELLOW "HIGH  " CLR_RESET; 
            break;
        case PRIORITY_NORMAL:
            prioStr = CLR_BLUE "NORMAL" CLR_RESET; 
            break;
        default:
            prioStr = CLR_RED "UNKNWN" CLR_RESET; 
            break;
    }

    const char *resultStr = success ? CLR_GREEN "SUCCESS" CLR_RESET : CLR_RED "FAILED – No Seats" CLR_RESET;

    pthread_mutex_lock(&logMutex);
    
    printf("[%s] Student %3d | Priority: %s | Course: %-6s | %s\n", ts, sid, prioStr, cname, resultStr);
           
    if (success) 
    {
    	totalSuccess++;
    }
    else
    {
    	totalFailed++;
    }
    pthread_mutex_unlock(&logMutex);
}

/* ================================================================
 * studentThread
 * ----------------------------------------------------------------
 * Entry point for each student thread.
 * Optionally sleeps (normal-priority students) to give high-
 * priority students a scheduling head-start, then iterates over
 * the student's requested courses and attempts to claim a seat in
 * each using the course-level mutex.
 *
 * Parameters:
 *   arg – pointer to the Student whose registration is processed
 *         (cast from void *); must not be NULL.
 *
 * Returns:
 *   Always returns NULL (required by pthread_create).
 *
 * Exceptions / edge-cases:
 *   • If arg is NULL the function logs an error and exits the
 *     thread immediately without dereferencing the pointer.
 *   • Course indices stored in requestedCourses[] are bounds-
 *     checked against numCourses; out-of-range entries are
 *     skipped with a warning rather than causing a buffer overrun.
 * ================================================================ */
static void *studentThread(void *arg)
{
    if (arg == NULL) 
    {
        pthread_mutex_lock(&logMutex);
        fprintf(stderr, CLR_RED "[ERROR] studentThread received NULL argument – thread exiting.\n" CLR_RESET);
        pthread_mutex_unlock(&logMutex);
        return NULL;
    }

    Student *s = (Student *)arg;

    if (s->numRequests < 0 || s->numRequests > MAX_REQUESTS) 
    {
        pthread_mutex_lock(&logMutex);
        fprintf(stderr, CLR_RED "[ERROR] Student %d has invalid numRequests=%d – thread exiting.\n" CLR_RESET, s->id, s->numRequests);
        pthread_mutex_unlock(&logMutex);
        return NULL;
    }

    if (s->priority == PRIORITY_NORMAL)
    {
        usleep(PRIORITY_DELAY_US);
    }

    for (int i = 0; i < s->numRequests; i++) 
    {
        int ci = s->requestedCourses[i];

        if (ci < 0 || ci >= numCourses) 
        {
            pthread_mutex_lock(&logMutex);
            fprintf(stderr, CLR_RED "[WARN] Student %d requested invalid course index %d – skipping.\n" CLR_RESET, s->id, ci);
            pthread_mutex_unlock(&logMutex);
            continue;
        }

        if (s->enrolled[ci])
        {
            continue;  
        }

        Course *c = &courses[ci];

        pthread_mutex_lock(&c->mutex);
        if (c->availableSeats > 0) 
        {
            c->availableSeats--;
            s->enrolled[ci] = 1;
            pthread_mutex_unlock(&c->mutex);
            logAttempt(s->id, s->priority, c->name, 1);
        } 
        else 
        {
            pthread_mutex_unlock(&c->mutex);
            logAttempt(s->id, s->priority, c->name, 0);
        }
    }

    return NULL;
}

/* ================================================================
 * addCourse
 * ----------------------------------------------------------------
 * Registers a new course in the global courses[] array and
 * initialises its per-course mutex.
 *
 * Parameters:
 *   name  – human-readable course code (e.g. "CS101"); must not
 *            be NULL and must fit in 15 printable characters.
 *   seats – positive integer capacity for the course.
 *
 * Returns:
 *   0 on success, -1 on any validation or initialisation failure.
 *
 * Exceptions / edge-cases:
 *   • Returns -1 (does not crash) if the course array is full,
 *     if name is NULL or empty, or if seats <= 0.
 *   • If pthread_mutex_init() fails the slot is not committed and
 *     the error is printed to stderr.
 * ================================================================ */
static int addCourse(const char *name, int seats)
{
    if (numCourses >= MAX_COURSES) 
    {
        fprintf(stderr, CLR_RED "[ERROR] Cannot add course '%s': maximum of %d courses reached.\n" CLR_RESET, name ? name : "<NULL>", MAX_COURSES);
        return -1;
    }

    if (name == NULL || name[0] == '\0') 
    {
        fprintf(stderr, CLR_RED "[ERROR] addCourse: course name must not be NULL or empty.\n" CLR_RESET);
        return -1;
    }

    if (seats <= 0) 
    {
        fprintf(stderr, CLR_RED "[ERROR] addCourse '%s': seat count must be positive (got %d).\n" CLR_RESET, name, seats);
        return -1;
    }

    Course *c = &courses[numCourses];
    c->id = numCourses;
    c->totalSeats = seats;
    c->availableSeats = seats;

    strncpy(c->name, name, sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';

    int rc = pthread_mutex_init(&c->mutex, NULL);
    if (rc != 0) 
    {
        fprintf(stderr, CLR_RED "[ERROR] pthread_mutex_init failed for course '%s': %s\n" CLR_RESET, name, strerror(rc));
        return -1;
    }

    numCourses++;
    return 0;
}

/* ================================================================
 * initDefaultCourses
 * ----------------------------------------------------------------
 * Populates the courses[] array with the eight standard courses
 * used by the full random-student simulation mode.
 *
 * Exceptions / edge-cases:
 *   • Calls addCourse() for each entry; if any call fails the
 *     error is already reported inside addCourse() and the
 *     remaining courses are still attempted.
 * ================================================================ */
static void initDefaultCourses(void)
{
    addCourse("CS101", 5);
    addCourse("CS102", 8);
    addCourse("CS103", 10);
    addCourse("CS104", 4);
    addCourse("CS105", 6);
    addCourse("CS106", 2);
    addCourse("CS107", 12);
    addCourse("CS108", 7);
}

/* ================================================================
 * initMandatoryCourses
 * ----------------------------------------------------------------
 * Populates the courses[] array with the three deliberately tight-
 * capacity courses used in the mandatory stress-test scenario.
 * Low seat counts (2 / 1 / 3) are intentional so that many
 * registration attempts are expected to fail, verifying correct
 * mutex behaviour under contention.
 *
 * Exceptions / edge-cases:
 *   • Delegates validation to addCourse(); see that function.
 * ================================================================ */
static void initMandatoryCourses(void)
{
    addCourse("CS101", 2);
    addCourse("CS102", 1);
    addCourse("CS103", 3);
}

/* ================================================================
 * initStudentsRandom
 * ----------------------------------------------------------------
 * Initialises n students with random priorities and random course
 * selections drawn from the currently loaded courses.
 * Approximately 25 % of students receive PRIORITY_HIGH.
 *
 * Parameters:
 *   n – number of students to create (1 .. MAX_STUDENTS).
 *
 * Exceptions / edge-cases:
 *   • If n is out of range it is clamped to [1, MAX_STUDENTS] and
 *     a warning is printed, rather than writing past array bounds.
 *   • If no courses have been loaded (numCourses == 0) the
 *     function prints an error and creates students with zero
 *     requests, avoiding a modulo-by-zero fault.
 * ================================================================ */
static void initStudentsRandom(int n)
{
    if (n < 1 || n > MAX_STUDENTS) 
    {
        fprintf(stderr, CLR_YELLOW "[WARN] initStudentsRandom: n=%d out of range [1,%d]; clamping.\n" CLR_RESET, n, MAX_STUDENTS);
        if (n < 1) 
        {
            n = 1;
        }
        if (n > MAX_STUDENTS)
        {
            n = MAX_STUDENTS;
        }
    }

    if (numCourses == 0) 
    {
        fprintf(stderr, CLR_RED "[ERROR] initStudentsRandom called before any courses were loaded.\n" CLR_RESET);
    }

    numStudents = n;
    for (int i = 0; i < n; i++) 
    {
        Student *s = &students[i];
        s->id = i + 1;
        s->priority = (rand() % 4 == 0) ? PRIORITY_HIGH : PRIORITY_NORMAL;
        s->numRequests = 0;
        memset(s->enrolled, 0, sizeof(s->enrolled));

        if (numCourses == 0)
        {
            continue; 
        }  

        int nr = (rand() % MAX_REQUESTS) + 1;
        int used[MAX_COURSES] = {0};

        for (int j = 0; j < nr && s->numRequests < MAX_REQUESTS; j++) 
        {
            int ci, attempts = 0;
            do 
            {
                ci = rand() % numCourses;
                attempts++;
                
            } while (used[ci] && attempts < 50);

            if (!used[ci]) 
            {
                s->requestedCourses[s->numRequests++] = ci;
                used[ci] = 1;
            }
        }
    }
}

/* ================================================================
 * initStudentsMandatory
 * ----------------------------------------------------------------
 * Initialises exactly 10 students for the mandatory test scenario.
 * Students 1-3 are high-priority; students 4-10 are normal.
 * Every student requests all loaded courses, deliberately creating
 * heavy contention to stress-test the mutex logic.
 *
 * Exceptions / edge-cases:
 *   • If numCourses > MAX_REQUESTS the number of requests per
 *     student is capped at MAX_REQUESTS with a printed warning,
 *     preventing a buffer overrun in requestedCourses[].
 * ================================================================ */
static void initStudentsMandatory(void)
{
    numStudents = 10;

    int requestable = numCourses;
    if (requestable > MAX_REQUESTS) 
    {
        fprintf(stderr, CLR_YELLOW "[WARN] initStudentsMandatory: numCourses (%d) > MAX_REQUESTS (%d); " "requests capped per student.\n" CLR_RESET,
                numCourses, MAX_REQUESTS);
        requestable = MAX_REQUESTS;
    }

    for (int i = 0; i < numStudents; i++) 
    {
        Student *s = &students[i];
        s->id = i + 1;
        s->priority = (i < 3) ? PRIORITY_HIGH : PRIORITY_NORMAL;
        s->numRequests = requestable;
        memset(s->enrolled, 0, sizeof(s->enrolled));

        for (int j = 0; j < requestable; j++)
        {
            s->requestedCourses[j] = j;
        }
    }
}

/* ================================================================
 * printCourseSummary
 * ----------------------------------------------------------------
 * Prints a formatted table showing final seat allocation for every
 * course, followed by overall registration statistics and a seat-
 * invariant check (no course may have negative available seats).
 *
 * Exceptions / edge-cases:
 *   • If numCourses is 0 the table is skipped with a notice
 *     instead of printing a header with no rows beneath it.
 * ================================================================ */
static void printCourseSummary(void)
{
    printf("\n" CLR_BOLD CLR_CYAN
           "╔══════════════════════════════════════════════════════╗\n"
           "║         FINAL COURSE SEAT ALLOCATION                 ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           CLR_RESET);

    if (numCourses == 0) 
    {
        printf(" (no courses were loaded)\n");
    } 
    else 
    {
        printf(" %-8s %6s %6s %9s  %s\n", "Course", "Total", "Filled", "Remaining", "Status");
        printf(" %-8s %6s %6s %9s  %s\n", "------", "-----", "------", "---------", "------");

        int allOk = 1;
        for (int i = 0; i < numCourses; i++) 
        {
            Course *c = &courses[i];
            int filled = c->totalSeats - c->availableSeats;
            int ok = (c->availableSeats >= 0);
            if (!ok) 
            {
               allOk = 0;
            }

            printf(" %-8s %6d %6d %9d  %s\n", c->name, c->totalSeats, filled, c->availableSeats, ok ? CLR_GREEN "OK" CLR_RESET
                   : CLR_RED "VIOLATED!" CLR_RESET);
        }

        printf("\n" CLR_BOLD CLR_CYAN
               "╔══════════════════════════════════════════════════════╗\n"
               "║               REGISTRATION STATISTICS                ║\n"
               "╚══════════════════════════════════════════════════════╝\n"
               CLR_RESET);
               
        printf(" Successful Registrations : " CLR_GREEN "%d\n" CLR_RESET, totalSuccess);
        printf(" Failed Registrations     : " CLR_RED   "%d\n" CLR_RESET, totalFailed);
        printf(" Total Attempts           : %d\n", totalSuccess + totalFailed);
        printf(" Seat Invariant Check     : %s\n", allOk ? CLR_GREEN "PASSED ✓" CLR_RESET : CLR_RED "FAILED ✗" CLR_RESET);
        printf(" Deadlock                 : " CLR_GREEN "None (one mutex per course at a time)\n" CLR_RESET);
    }
    
    printf("\n");
}

/* ================================================================
 * cleanup
 * ----------------------------------------------------------------
 * Destroys every per-course mutex and the global log mutex.
 * Should be called once at program exit, after all threads have
 * been joined, to release OS resources.
 *
 * Exceptions / edge-cases:
 *   • Iterates only over the range [0, numCourses) so it is safe
 *     to call even if the simulation ended early with fewer courses
 *     than MAX_COURSES initialised.
 * ================================================================ */
static void cleanup(void)
{
    for (int i = 0; i < numCourses; i++)
    {
       pthread_mutex_destroy(&courses[i].mutex);
    }

    pthread_mutex_destroy(&logMutex);
}

/* ================================================================
 * runSimulation
 * ----------------------------------------------------------------
 * Spawns one POSIX thread per student, waits for all threads to
 * finish, and prints the registration log boundary lines.
 *
 * Returns:
 *   0 on success.
 *   1 if any thread could not be created; in that case all threads
 *   that were successfully started are joined before returning so
 *   no threads are leaked.
 *
 * Exceptions / edge-cases:
 *   • If numStudents is 0 the function returns immediately with
 *     success (no threads to spawn or join).
 *   • pthread_create() errors are reported with strerror() for a
 *     human-readable OS error message.
 *   • pthread_join() errors are reported but do not abort the
 *     join loop, ensuring every joinable thread is waited upon.
 * ================================================================ */
static int runSimulation(void)
{
    if (numStudents == 0) 
    {
        fprintf(stderr, CLR_YELLOW "[WARN] runSimulation: no students loaded – nothing to simulate.\n" CLR_RESET);
        return 0;
    }

    pthread_t threads[MAX_STUDENTS];

    printf(CLR_BOLD "\n┌── Registration Log ──────────────────────────────────────┐\n" CLR_RESET);

    int created = 0;  
    for (int i = 0; i < numStudents; i++) 
    {
        int rc = pthread_create(&threads[i], NULL, studentThread, &students[i]);
        if (rc != 0) 
        {
            fprintf(stderr, CLR_RED "[ERROR] Failed to create thread for student %d: %s\n" CLR_RESET, i + 1, strerror(rc));
         
            for (int j = 0; j < created; j++) 
            {
                int jrc = pthread_join(threads[j], NULL);
                if (jrc != 0)
                {
                    fprintf(stderr, CLR_RED "[ERROR] pthread_join failed for thread %d: %s\n" CLR_RESET, j, strerror(jrc));
                }
            }
            return 1;
        }
        created++;
    }

    for (int i = 0; i < created; i++) 
    {
        int jrc = pthread_join(threads[i], NULL);
        if (jrc != 0)
        {
            fprintf(stderr, CLR_RED "[ERROR] pthread_join failed for thread %d: %s\n" CLR_RESET, i, strerror(jrc));
        }
    }

    printf(CLR_BOLD "└──────────────────────────────────────────────────────────┘\n" CLR_RESET);
    return 0;
}

/* ================================================================
 * main
 * ----------------------------------------------------------------
 * Program entry point.
 * Parses command-line arguments, selects the simulation mode,
 * initialises courses and students, runs the simulation, prints
 * the summary, and cleans up resources.
 *
 * Usage:
 *   ./registration – full sim with 100 students
 *   ./registration <N> – full sim with N students (1-200)
 *   ./registration test – mandatory stress-test scenario
 *
 * Returns:
 *   EXIT_SUCCESS (0) on normal completion.
 *   EXIT_FAILURE (1) on argument errors or simulation failure.
 *
 * Exceptions / edge-cases:
 *   • A non-numeric, zero, or out-of-range student count prints
 *     usage instructions and exits cleanly rather than invoking
 *     undefined behaviour via atoi() on bad input.
 *   • cleanup() is called on all exit paths so mutexes are always
 *     released.
 * ================================================================ */
int main(int argc, char *argv[])
{
    srand((unsigned)time(NULL));

    int mandatoryMode = 0;
    int nStudents = 100;   

    if (argc >= 2) 
    {
        if (strcmp(argv[1], "test") == 0) 
        {
            mandatoryMode = 1;
        } 
        else 
        {
            char *endptr = NULL;
            long val = strtol(argv[1], &endptr, 10);

            if (endptr == argv[1] || *endptr != '\0') 
            {
                fprintf(stderr, CLR_RED "[ERROR] Invalid argument '%s'. " "Expected a number (1-%d) or 'test'.\n" CLR_RESET, argv[1], MAX_STUDENTS);
                fprintf(stderr, "Usage: %s [<num_students(1-%d)> | test]\n", argv[0], MAX_STUDENTS);
                return EXIT_FAILURE;
            }

            if (val < 1 || val > MAX_STUDENTS) 
            {
                fprintf(stderr, CLR_RED "[ERROR] Student count %ld is out of range [1, %d].\n" CLR_RESET, val, MAX_STUDENTS);
                fprintf(stderr, "Usage: %s [<num_students(1-%d)> | test]\n", argv[0], MAX_STUDENTS);
                return EXIT_FAILURE;
            }

            nStudents = (int)val;
        }
    }

    printf(CLR_BOLD CLR_CYAN
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║       University Course Registration System          ║\n"
           "║    CL-2006 Operating Systems Lab — Final Project     ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           CLR_RESET "\n");

    /* ── Initialise data ────────────────────────────────────── */
    if (mandatoryMode) 
    {
        printf(CLR_BOLD "[MODE] Mandatory Test Scenario\n\n" CLR_RESET);
        initMandatoryCourses();
        initStudentsMandatory();
    } 
    else 
    {
        printf(CLR_BOLD "[MODE] Full Simulation | Students: %d\n\n" CLR_RESET, nStudents);
        initDefaultCourses();
        initStudentsRandom(nStudents);
    }

    /* Abort early if course initialisation produced nothing */
    if (numCourses == 0) 
    {
        fprintf(stderr, CLR_RED "[FATAL] No courses were initialised. Exiting.\n" CLR_RESET);
        cleanup();
        return EXIT_FAILURE;
    }

    /* ── Display loaded courses ─────────────────────────────── */
    printf(CLR_BOLD "Courses Loaded:\n" CLR_RESET);
    for (int i = 0; i < numCourses; i++)
    {
        printf("  %-8s → %d seat%s\n", courses[i].name, courses[i].totalSeats, courses[i].totalSeats == 1 ? "" : "s");
    }

    /* ── Display student breakdown ──────────────────────────── */
    int highPriorityCount = 0;
    for (int i = 0; i < numStudents; i++)
    {
        if (students[i].priority == PRIORITY_HIGH)
        {
            highPriorityCount++;
        }
    }

    printf(CLR_BOLD "\nStudents:\n" CLR_RESET);
    printf("  Total                   : %d\n", numStudents);
    printf("  " CLR_YELLOW "High-Priority (final-yr) : %d\n" CLR_RESET, highPriorityCount);
    printf("  " CLR_BLUE   "Normal-Priority          : %d\n" CLR_RESET, numStudents - highPriorityCount);

    /* ── Run ────────────────────────────────────────────────── */
    if (runSimulation() != 0) 
    {
        fprintf(stderr, CLR_RED "[FATAL] Simulation aborted due to thread creation failure.\n" CLR_RESET);
        cleanup();
        return EXIT_FAILURE;
    }

    /* ── Report ─────────────────────────────────────────────── */
    printCourseSummary();

    cleanup();
    return EXIT_SUCCESS;
}
