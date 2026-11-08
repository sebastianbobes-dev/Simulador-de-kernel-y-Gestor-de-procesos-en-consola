#include <iostream>
#include <cstdlib> //libreria estandar para el manejo de los errores.
#include <pthread.h>
#include "hilo_CLI.h"
#include "scheduler.h"

using namespace KernelSim;
using namespace std;

int main(){
    cout<< "======================================================================" << endl;
    cout<< "Simulador de Kernel y Gestor de Procesos en Consola" << endl;
    cout<< " Sistemas Operativos - UCAB" << endl;
    cout<< "======================================================================" << endl;

    pthread_t cli_thread;
    pthread_t scheduler_thread;

    // verificacion de la creacion del hilo cli
    int result_cli = pthread_create(&cli_thread, nullptr, runCLI, nullptr);
    if (result_cli != 0) {
        cerr << "[Error]: Fallo al crear el hilo de la CLI: " << result_cli << endl;
        return EXIT_FAILURE;
    }

    //verifacion de la creacion del hilo scheduler del cpu
    int result_scheduler = pthread_create(&scheduler_thread, nullptr, runScheduler, nullptr);
    if (result_scheduler != 0) {
        cerr << "[Error]: Fallo al crear el hilo del Scheduler: " << result_scheduler << endl;
        return EXIT_FAILURE;
    }

    cout<< "[sistema]: Los hilos de la CLI y del Scheduler se encuentrabajando en pararalelo." << endl;   
    cout<< "[Sistema]: Escribe 'help' para desplegar la lista de comandos."<< endl;

    pthread_join(cli_thread, nullptr);
    pthread_join(scheduler_thread, nullptr);

    cout << endl << "=======================================================================" << endl;
    cout << "[sistema]: Simulacion finalizada. Recursos liberados con exito." << endl;
    cout << "=======================================================================" << endl;
    return EXIT_SUCCESS;
}