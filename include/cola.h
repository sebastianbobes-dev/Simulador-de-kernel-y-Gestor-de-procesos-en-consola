#ifndef COLA_H
#define COLA_H

#include "pcb.h"
#include <vector>
#include <pthread.h>

namespace KernelSim {
    class ProcesoCola {
        private:
        std::vector<PCB*> cola;
        pthread_mutex_t lock;

        public:
        ProcesoCola();
        ~ProcesoCola();

        void addProceso(PCB* proceso);
        PCB* getProcesoByPID(int pid);
        std::vector<PCB*> getAllProcesos();

        void lockCola();
        void unlockCola();
    }   

}