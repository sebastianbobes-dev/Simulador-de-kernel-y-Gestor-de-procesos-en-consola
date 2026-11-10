#include "pcb.h"
using namespace std;
namespace KernelSim {
    //constructor
    PCB::PCB(int id, const string& name, int burst, int prio, int arrival){
        pid = id;
        nombre = name;
        estado = ProcessState::NUEVO;
        prioridad = prio;

        burst_time = burst;
        remaining_time = burst;
        arrival_time = arrival;

        waiting_time = 0;
        turnaround_time = 0;
        response_time = 0;
        has_started = false;
    }

    string stateToString(ProcessState state) {
        switch (state) {
            case ProcessState::NUEVO:
                return "NUEVO";
            case ProcessState::LISTO:
                return "LISTO";
            case ProcessState::EJECUCION:
                return "EJECUCION";
            case ProcessState::PAUSADO_BLOQUEADO:
                return "PAUSADO_BLOQUEADO";
            case ProcessState::FINALIZADO:
                return "FINALIZADO";
            default:
                return "DESCONOCIDO";
        }
    }
}