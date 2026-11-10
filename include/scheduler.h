#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <string>

namespace KernelSim{
    enum class Algoritmo{
        RR,     // Round Robin    
        SRTF    // Shortest Remaining Time First
    };

    void setAlgoritmo(Algoritmo algoritmo, int quantum = 2);
    void setSpeed(int ms_tick);
    void startSimualacion();
    void pauseSimulacion();

    void* runScheduler(void* arg);
}
#endif // SCHEDULER_H