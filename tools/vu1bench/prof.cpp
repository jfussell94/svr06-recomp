// SIGPROF RIP sampler for the WSL bench (no perf/valgrind needed): link into bench, run with VUPROF=out.txt
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/time.h>
#include <ucontext.h>
static unsigned long g_rip[400000]; static volatile int g_n = 0;
static void h(int, siginfo_t *, void *uc) { if (g_n < 400000) g_rip[g_n++] = ((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP]; }
static void dump() { const char *p = getenv("VUPROF"); if (!p) return; FILE *f = fopen(p, "w"); for (int i = 0; i < g_n; ++i) fprintf(f, "%lx\n", g_rip[i]); fclose(f); }
struct ProfInit { ProfInit() { if (!getenv("VUPROF")) return; struct sigaction sa{}; sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigaction(SIGPROF, &sa, nullptr);
    itimerval t{{0, 500}, {0, 500}}; setitimer(ITIMER_PROF, &t, nullptr); atexit(dump); } } g_profInit;
