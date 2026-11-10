#ifndef LOADER_H
#define LOADER_H

#include <string>

namespace KernelSim {

// Lee el archivo de texto y carga masivamente los procesos al sistema
bool loadWorkload(const std::string& filename);

} // namespace KernelSim

#endif // LOADER_H