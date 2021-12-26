
#include <iostream>
#include <thread>

#include <signal.h>
#include <unistd.h> // getpid

static volatile sig_atomic_t g_terminate = 0;

void sigHandler(int signum)
{
    if (signum == SIGUSR1) {
        std::cout << "SIGUSR1 received" << std::endl;
        g_terminate = 1;
    }
}

// Test von der Konsole:
// kill -s SIGUSR1 <PID>

int main_sigusr_test(int argc, char* argv[])
{
    (void)argc; (void) argv;
    std::cout << "process " << getpid() << " started"  << std::endl;

    signal(SIGUSR1, sigHandler);

    while (true) {
        if (g_terminate == 1) break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "process terminated"  << std::endl;

    return 22;
}
