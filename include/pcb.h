#ifndef PCB_H
#define PCB_H

#include <string>

namespace KernelSim {
    enum class ProcessState {
        NUEVO,
        LISTO,
        EJECUCION,
        PAUSADO_BLOQUEADO,
        FINALIZADO
    };
    
    std::string estadoString(ProcessState estado);

    struct PCB{
        // estructura base de la PCB
        int pid;
        std::string nombre;
        ProcessState estado;
        int prioridad;

        // Tiempos simulados
        int burst_time;
        int remaining_time;
        int arrival_time;

        // Metricas para ver el rendimiento
        int waiting_time;
        int turnaround_time;
        int response_time;
        bool has_started;

        PCB(int id, const std::string& name, int burst, int prio = 3, int arrival = 0);

    };
}

#endif // PCB_H
