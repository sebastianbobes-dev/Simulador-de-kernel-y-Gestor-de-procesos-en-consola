#include "cola.h"

using namespace std;

namespace KernelSim{
    ProcesoCola globalCola; // Instancia global de la clase ProcesoCola

    int nextPID = 1; 
    ProcesoCola::ProcesoCola() {
        pthread_mutex_init(&lock, nullptr);
    }
    ProcesoCola::~ProcesoCola() {
        pthread_mutex_destroy(&lock);
        for (PCB* proceso : cola) {
            delete proceso; // Liberar memoria de cada PCB
        }
        proceses.clear();       
    }

    void ProcesoCola::addProceso(PCB* proceso) {
        lockCola();
        cola.push_back(proceso);
        unlockCola();
    }

    PCB* ProcesoCola::getProcesoByPID(int pid) {
        lockCola();
        PCB* encontrado = nullptr;
        for (PCB* proceso : cola) {
            if (proceso->pid == pid) {
                encontrado = proceso;
                break;
            }
        }
        unlockCola();
        return encontrado; // Retorna nullptr si no se encuentra el proceso
    }

    vector<PCB*> ProcesoCola::getAllProcesos() {
        lockCola();
        vector<PCB*> copiaCola = cola; // Copia de la cola para evitar problemas de concurrencia
        unlockCola();
        return copiaCola;
    }

    void ProcesoCola::lockCola() {
        pthread_mutex_lock(&lock);
    }

    void ProcesoCola::unlockCola() {
        pthread_mutex_unlock(&lock);
    }
    
}
