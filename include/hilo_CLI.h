#ifndef CLI_H
#define CLI_H

namespace KernelSim {

    // funcion principal del hilo de la CLI, se encarga de recibir los comandos del usuario y ejecutar las acciones correspondientes
    void* runCLI(void* arg);

}
#endif // CLI_H