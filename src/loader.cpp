#include "loader.h"
#include "pcb.h"
#include "cola.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;

namespace KernelSim {
//var globales
extern ProcesoCola globalCola;
extern int nextPID; 

bool loadworkload(const string& filename){
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "[Error]: No se pudo abrir el archivo " << filename << endl;
        return false;
    }

    string line;
    int loadedCount = 0;
    while (getline(file, line)) {
        if(line.empty() || line[0] == '#') {
            continue; // Ignorar líneas vacías o comentarios
        }
        stringstream ss(line);
        string name, burstStr, prioStr, arrivalStr;
        if(getline(ss, name, ',') && getline(ss, burstStr, ',') && getline(ss, prioStr, ',') && getline(ss, arrivalStr)) {
            try {
                //conversion estricta
                int burst = stoi(burstStr);
                int prio = stoi(prioStr);
                int arrival = stoi(arrivalStr);
                
                // validdacion de los datos permitidos segun lo establecido en el pdf
                if(burst <= 0 || prio < 1 || prio > 5 || arrival < 0) {
                    cerr << "[Error]: Valores inválidos en la línea: " << line << endl;
                    continue;
                }

                PCB* newProcess = new PCB(nextPID++, name, burst, prio, arrival);
                globalCola.addProceso(newProcess);
                loadedCount++;
            } catch (const invalid_argument& e) {
                cerr << "[Error]: Formato inválido en la línea: " << line << endl;
            }
        } else {
            cerr << "[Error]: Línea malformada: " << line << endl;
        }
    }
    file.close();
    cout << "[Info]: Se cargaron " << loadedCount << " procesos desde el archivo " << filename << endl;
    return true;
}

} // namespace KernelSim