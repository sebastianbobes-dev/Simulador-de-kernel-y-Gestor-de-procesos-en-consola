#include "pcb.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char* state_name(ProcessState state) {
    switch (state) {
        case ProcessState::New: return "NEW";
        case ProcessState::Ready: return "READY";
        case ProcessState::Running: return "RUNNING";
        case ProcessState::Terminated: return "TERMINATED";
    }
    return "DESCONOCIDO";
}

bool parse_integer(const std::string& text, int& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string format_seconds(std::int64_t milliseconds) {
    std::string result = std::to_string(milliseconds / 1000);
    const int fractional_milliseconds =
        static_cast<int>(milliseconds % 1000);
    if (fractional_milliseconds != 0) {
        std::string fraction = std::to_string(1000 + fractional_milliseconds).substr(1);
        while (fraction.back() == '0') fraction.pop_back();
        result += "." + fraction;
    }
    return result;
}

bool tokenize(const std::string& line, std::vector<std::string>& tokens) {
    std::string token;
    bool quoted = false;
    bool started = false;
    bool just_closed_quote = false;

    for (char character : line) {
        if (character == '"') {
            if (!quoted && (started || just_closed_quote)) return false;
            quoted = !quoted;
            started = true;
            just_closed_quote = !quoted;
        } else if (!quoted && (character == ' ' || character == '\t' ||
                               character == '\r')) {
            if (started) {
                tokens.push_back(token);
                token.clear();
                started = false;
            }
            just_closed_quote = false;
        } else {
            if (just_closed_quote) return false;
            token.push_back(character);
            started = true;
        }
    }

    if (quoted) return false;
    if (started) tokens.push_back(token);
    return true;
}

struct TraceProcess {
    int arrival_time;
    std::string name;
    int burst;
    int priority;
};

class Simulator {
public:
    Simulator() : next_pid_(1), clock_ticks_(0), cpu_ticks_(0),
                  running_pid_(-1), quantum_(2), quantum_left_(2),
                  speed_ms_(1000), use_rr_(true), started_(false),
                  running_(false), shutdown_(false), worker_failed_(false) {
        int error = pthread_mutex_init(&mutex_, nullptr);
        if (error != 0) {
            throw std::runtime_error("pthread_mutex_init: " +
                                     std::string(std::strerror(error)));
        }
        error = pthread_cond_init(&condition_, nullptr);
        if (error != 0) {
            pthread_mutex_destroy(&mutex_);
            throw std::runtime_error("pthread_cond_init: " +
                                     std::string(std::strerror(error)));
        }
    }

    ~Simulator() {
        if (started_) {
            const int lock_error = pthread_mutex_lock(&mutex_);
            if (lock_error == 0) {
                shutdown_ = true;
                pthread_cond_broadcast(&condition_);
                pthread_mutex_unlock(&mutex_);
            }
            const int join_error = pthread_join(cpu_thread_, nullptr);
            if (lock_error != 0 || join_error != 0) {
                std::cerr << "[ERROR]: No se pudieron cerrar limpiamente los hilos.\n";
            }
        }
        pthread_cond_destroy(&condition_);
        pthread_mutex_destroy(&mutex_);
    }

    void start_worker() {
        const int error = pthread_create(&cpu_thread_, nullptr, &Simulator::thread_entry, this);
        if (error != 0) {
            throw std::runtime_error("pthread_create: " +
                                     std::string(std::strerror(error)));
        }
        started_ = true;
    }

    void shutdown_and_join() {
        if (!started_) return;
        {
            MutexGuard guard(mutex_);
            shutdown_ = true;
            const int error = pthread_cond_broadcast(&condition_);
            if (error != 0) {
                throw std::runtime_error("pthread_cond_broadcast: " +
                                         std::string(std::strerror(error)));
            }
        }
        const int error = pthread_join(cpu_thread_, nullptr);
        if (error != 0) {
            throw std::runtime_error("pthread_join: " +
                                     std::string(std::strerror(error)));
        }
        started_ = false;
    }

    void create_process(const std::string& name, int burst, int priority) {
        MutexGuard guard(mutex_);
        add_process_locked(name, burst, priority, clock_ticks_);
        pthread_cond_signal(&condition_);
        std::cout << "Proceso creado con PID " << next_pid_ - 1 << ".\n";
    }

    bool load_trace(const std::string& path, std::string& error_message) {
        std::ifstream file(path);
        if (!file) {
            error_message = "No se pudo abrir el archivo: " + path;
            return false;
        }

        std::vector<TraceProcess> trace;
        std::string line;
        int line_number = 0;
        while (std::getline(file, line)) {
            ++line_number;
            const std::size_t first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#') continue;

            std::string arrival_text;
            std::string name;
            std::string burst_text;
            std::string priority_text = "3";
            int arrival = 0;
            int burst = 0;
            int priority = 3;
            bool valid = true;
            if (line.find(',') != std::string::npos) {
                std::vector<std::string> fields;
                std::istringstream input(line);
                std::string field;
                while (std::getline(input, field, ',')) {
                    fields.push_back(trim(field));
                }
                if (fields.size() != 4 ||
                    std::count(line.begin(), line.end(), ',') != 3) {
                    valid = false;
                } else {
                    name = fields[0];
                    burst_text = fields[1];
                    priority_text = fields[2];
                    arrival_text = fields[3];
                }
            } else {
                std::istringstream input(line);
                std::string extra;
                valid = static_cast<bool>(input >> arrival_text >> name >> burst_text);
                if (valid && input >> priority_text && input >> extra) {
                    valid = false;
                }
            }
            if (valid) {
                valid = !name.empty() && parse_integer(arrival_text, arrival) &&
                        parse_integer(burst_text, burst) &&
                        parse_integer(priority_text, priority);
            }
            if (!valid || arrival < 0 || burst <= 0 ||
                priority < 1 || priority > 5) {
                error_message = "Formato inválido en la línea " +
                                std::to_string(line_number) +
                                ". Use CSV: <nombre>, <burst>, <prioridad>, <llegada> "
                                "o texto: <llegada> <nombre> <burst> [prioridad].";
                return false;
            }
            trace.push_back({arrival, name, burst, priority});
        }
        if (file.bad()) {
            error_message = "Error al leer el archivo: " + path;
            return false;
        }
        if (trace.empty()) {
            error_message = "El archivo no contiene procesos válidos.";
            return false;
        }

        MutexGuard guard(mutex_);
        for (const TraceProcess& process : trace) {
            add_process_locked(process.name, process.burst,
                               process.priority, process.arrival_time);
        }
        pthread_cond_signal(&condition_);
        std::cout << trace.size() << " proceso(s) cargado(s).\n";
        return true;
    }

    void print_processes() {
        MutexGuard guard(mutex_);
        std::cout << "PID NOMBRE ESTADO BURST RESTANTE PRIORIDAD\n"
                  << "--------------------------------------------------------------\n";
        for (const PCB& process : processes_) {
            std::cout << process.pid << ' ' << process.name << ' '
                      << state_name(process.state) << ' '
                      << process.remaining_burst << "s "
                      << process.priority << '\n';
        }
    }

    bool print_pcb(int pid) {
        MutexGuard guard(mutex_);
        const PCB* process = find_process_locked(pid);
        if (process == nullptr) return false;

        const int elapsed = process->state == ProcessState::Terminated
                                ? process->completion_time - process->arrival_time
                                : std::max(0, clock_ticks_ - process->arrival_time);
        const int executed = process->initial_burst - process->remaining_burst;
        const int waiting = std::max(0, elapsed - executed);

        std::cout << "==================================================\n"
                  << "CONTROL BLOCK DE PROCESO (PCB) - PID: " << process->pid << '\n'
                  << "==================================================\n"
                  << "Nombre del Proceso : " << process->name << '\n'
                  << "Estado Actual : " << state_name(process->state) << '\n'
                  << "Prioridad : " << process->priority << '\n'
                  << "Tiempo de Llegada : " << process->arrival_time << " ticks\n"
                  << "Ráfaga Inicial : " << process->initial_burst << " ticks\n"
                  << "Ráfaga Restante : " << process->remaining_burst << " ticks\n"
                  << "Tiempo de Espera : " << waiting << " ticks\n"
                  << "Tiempo de Retorno : ";
        if (process->state == ProcessState::Terminated) {
            std::cout << process->completion_time - process->arrival_time << " ticks";
        } else if (process->state == ProcessState::Running) {
            std::cout << "N/A (En ejecución)";
        } else {
            std::cout << "N/A (No finalizado)";
        }
        std::cout << "\nTiempo de Respuesta: ";
        if (process->first_start_time >= 0) {
            std::cout << process->first_start_time - process->arrival_time << " ticks";
        } else {
            std::cout << "N/A";
        }
        std::cout << "\n==================================================\n";
        return true;
    }

    void set_scheduler(bool use_rr, int quantum) {
        MutexGuard guard(mutex_);
        use_rr_ = use_rr;
        quantum_ = quantum;
        if (running_pid_ >= 0 && use_rr_ &&
            (quantum_left_ <= 0 || quantum_left_ > quantum_)) {
            quantum_left_ = quantum_;
        }
        std::cout << "Planificador: " << (use_rr_ ? "RR, quantum " : "SRTF")
                  << (use_rr_ ? std::to_string(quantum_) : "") << ".\n";
        pthread_cond_signal(&condition_);
    }

    void set_speed(int milliseconds) {
        MutexGuard guard(mutex_);
        speed_ms_ = milliseconds;
        std::cout << "Velocidad: " << speed_ms_ << " ms por tick.\n";
        pthread_cond_signal(&condition_);
    }

    bool start_cpu() {
        MutexGuard guard(mutex_);
        if (worker_failed_) {
            std::cout << "[ERROR]: El hilo de CPU terminó por un error.\n";
            return false;
        }
        running_ = true;
        pthread_cond_signal(&condition_);
        std::cout << "CPU iniciada/reanudada.\n";
        return true;
    }

    void pause_cpu() {
        MutexGuard guard(mutex_);
        running_ = false;
        pthread_cond_signal(&condition_);
        std::cout << "CPU pausada.\n";
    }

    void print_gantt() {
        MutexGuard guard(mutex_);
        if (gantt_.empty()) {
            std::cout << "El diagrama de Gantt está vacío.\n";
            return;
        }
        std::cout << "DIAGRAMA DE GANTT (tiempo simulado):\n";
        std::size_t begin = 0;
        std::int64_t begin_ms = 0;
        while (begin < gantt_.size()) {
            std::size_t end = begin + 1;
            std::int64_t end_ms = begin_ms + gantt_tick_duration_ms_[begin];
            while (end < gantt_.size() && gantt_[end] == gantt_[begin]) {
                end_ms += gantt_tick_duration_ms_[end];
                ++end;
            }
            if (begin != 0) std::cout << " -> ";
            const PCB* process = gantt_[begin] < 0
                                     ? nullptr
                                     : find_process_locked(gantt_[begin]);
            std::cout << '[' << format_seconds(begin_ms) << '-'
                      << format_seconds(end_ms) << "s: "
                      << (process == nullptr ? "IDLE" : process->name) << ']';
            begin_ms = end_ms;
            begin = end;
        }
        std::cout << '\n';
    }

    void print_stats() {
        MutexGuard guard(mutex_);
        double total_waiting = 0.0;
        double total_turnaround = 0.0;
        double total_response = 0.0;
        int completed = 0;
        for (const PCB& process : processes_) {
            if (process.state != ProcessState::Terminated) continue;
            ++completed;
            total_waiting += process.completion_time - process.arrival_time -
                             process.initial_burst;
            total_turnaround += process.completion_time - process.arrival_time;
            total_response += process.first_start_time - process.arrival_time;
        }

        const std::ios::fmtflags original_flags = std::cout.flags();
        const std::streamsize original_precision = std::cout.precision();
        std::cout << "ESTADÍSTICAS DEL SISTEMA:\n"
                  << "- Procesos Finalizados : " << completed << " / "
                  << processes_.size() << '\n';
        if (completed == 0) {
            std::cout << "- Tiempo Promedio de Espera : N/A\n"
                      << "- Tiempo Promedio de Retorno: N/A\n"
                      << "- Tiempo Promedio de Respuesta: N/A\n";
        } else {
            std::cout << std::fixed << std::setprecision(2)
                      << "- Tiempo Promedio de Espera : "
                      << total_waiting / completed << " ticks\n"
                      << "- Tiempo Promedio de Retorno: "
                      << total_turnaround / completed << " ticks\n"
                      << "- Tiempo Promedio de Respuesta: "
                      << total_response / completed << " ticks\n";
        }
        const double utilization = clock_ticks_ == 0
                                       ? 0.0
                                       : 100.0 * cpu_ticks_ / clock_ticks_;
        std::cout << std::fixed << std::setprecision(1)
                  << "- Uso de CPU : " << utilization << "%\n";
        std::cout.flags(original_flags);
        std::cout.precision(original_precision);
    }

private:
    class MutexGuard {
    public:
        explicit MutexGuard(pthread_mutex_t& mutex) : mutex_(mutex) {
            const int error = pthread_mutex_lock(&mutex_);
            if (error != 0) {
                throw std::runtime_error("pthread_mutex_lock: " +
                                         std::string(std::strerror(error)));
            }
        }

        ~MutexGuard() {
            const int error = pthread_mutex_unlock(&mutex_);
            if (error != 0) {
                std::cerr << "[ERROR]: pthread_mutex_unlock: "
                          << std::strerror(error) << '\n';
            }
        }

    private:
        pthread_mutex_t& mutex_;
    };

    static void* thread_entry(void* argument) {
        Simulator* simulator = static_cast<Simulator*>(argument);
        try {
            simulator->cpu_loop();
        } catch (const std::exception& error) {
            std::cerr << "[ERROR]: Hilo de CPU: " << error.what() << '\n';
        }
        return nullptr;
    }

    void add_process_locked(const std::string& name, int burst, int priority,
                            int arrival_time) {
        PCB process{next_pid_++, name, ProcessState::New, arrival_time, burst,
                    burst, priority, -1, -1};
        if (arrival_time <= clock_ticks_) {
            process.state = ProcessState::Ready;
            ready_queue_.push_back(process.pid);
        }
        processes_.push_back(process);
    }

    PCB* find_process_locked(int pid) {
        for (PCB& process : processes_) {
            if (process.pid == pid) return &process;
        }
        return nullptr;
    }

    const PCB* find_process_locked(int pid) const {
        for (const PCB& process : processes_) {
            if (process.pid == pid) return &process;
        }
        return nullptr;
    }

    bool has_pending_locked() const {
        return std::any_of(processes_.begin(), processes_.end(),
                           [](const PCB& process) {
                               return process.state != ProcessState::Terminated;
                           });
    }

    void admit_arrivals_locked() {
        for (PCB& process : processes_) {
            if (process.state == ProcessState::New &&
                process.arrival_time <= clock_ticks_) {
                process.state = ProcessState::Ready;
                ready_queue_.push_back(process.pid);
            }
        }
    }

    int take_ready_locked() {
        if (ready_queue_.empty()) return -1;
        auto selected = ready_queue_.begin();
        if (!use_rr_) {
            selected = std::min_element(
                ready_queue_.begin(), ready_queue_.end(),
                [this](int left, int right) {
                    return find_process_locked(left)->remaining_burst <
                           find_process_locked(right)->remaining_burst;
                });
        }
        const int pid = *selected;
        ready_queue_.erase(selected);
        return pid;
    }

    void tick_locked() {
        admit_arrivals_locked();

        if (running_pid_ >= 0 && !use_rr_ && !ready_queue_.empty()) {
            const PCB* current = find_process_locked(running_pid_);
            const auto shortest = std::min_element(
                ready_queue_.begin(), ready_queue_.end(),
                [this](int left, int right) {
                    return find_process_locked(left)->remaining_burst <
                           find_process_locked(right)->remaining_burst;
                });
            if (find_process_locked(*shortest)->remaining_burst <
                current->remaining_burst) {
                find_process_locked(running_pid_)->state = ProcessState::Ready;
                ready_queue_.push_back(running_pid_);
                running_pid_ = -1;
            }
        }

        if (running_pid_ < 0) {
            running_pid_ = take_ready_locked();
            if (running_pid_ >= 0) {
                PCB* process = find_process_locked(running_pid_);
                process->state = ProcessState::Running;
                if (process->first_start_time < 0) {
                    process->first_start_time = clock_ticks_;
                }
                if (use_rr_) quantum_left_ = quantum_;
            }
        }

        if (running_pid_ < 0) {
            gantt_.push_back(-1);
        } else {
            PCB* process = find_process_locked(running_pid_);
            gantt_.push_back(running_pid_);
            --process->remaining_burst;
            ++cpu_ticks_;
            if (use_rr_) --quantum_left_;
            if (process->remaining_burst == 0) {
                process->state = ProcessState::Terminated;
                process->completion_time = clock_ticks_ + 1;
                running_pid_ = -1;
            } else if (use_rr_ && quantum_left_ == 0) {
                process->state = ProcessState::Ready;
                ready_queue_.push_back(running_pid_);
                running_pid_ = -1;
            }
        }
        gantt_tick_duration_ms_.push_back(speed_ms_);
        ++clock_ticks_;
    }

    static timespec deadline_after(int milliseconds) {
        timespec deadline{};
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += milliseconds / 1000;
        deadline.tv_nsec += static_cast<long>(milliseconds % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        return deadline;
    }

    void cpu_loop() {
        MutexGuard guard(mutex_);
        while (!shutdown_) {
            while (!shutdown_ && (!running_ || !has_pending_locked())) {
                const int error = pthread_cond_wait(&condition_, &mutex_);
                if (error != 0) {
                    worker_failed_ = true;
                    running_ = false;
                    std::cerr << "[ERROR]: pthread_cond_wait: "
                              << std::strerror(error) << '\n';
                    return;
                }
            }
            if (shutdown_) break;

            const timespec deadline = deadline_after(speed_ms_);
            int result = 0;
            result = pthread_cond_timedwait(&condition_, &mutex_, &deadline);

            if (shutdown_) break;
            if (!running_) continue;
            if (result == ETIMEDOUT) {
                tick_locked();
            } else if (result == 0) {
                continue;
            } else if (result != 0) {
                worker_failed_ = true;
                running_ = false;
                std::cerr << "[ERROR]: pthread_cond_timedwait: "
                          << std::strerror(result) << '\n';
                return;
            }
        }
    }

    pthread_mutex_t mutex_;
    pthread_cond_t condition_;
    pthread_t cpu_thread_{};
    std::vector<PCB> processes_;
    std::deque<int> ready_queue_;
    std::vector<int> gantt_;
    std::vector<int> gantt_tick_duration_ms_;
    int next_pid_;
    int clock_ticks_;
    int cpu_ticks_;
    int running_pid_;
    int quantum_;
    int quantum_left_;
    int speed_ms_;
    bool use_rr_;
    bool started_;
    bool running_;
    bool shutdown_;
    bool worker_failed_;
};

void print_help() {
    std::cout
        << "Comandos:\n"
        << "  create <nombre> <burst> [prioridad]  Crear proceso (prioridad 1-5, defecto 3)\n"
        << "  load <archivo>                       Cargar CSV: nombre, burst, prioridad, llegada\n"
        << "  ps                                    Listar procesos\n"
        << "  pcb <PID>                             Mostrar detalle del PCB\n"
        << "  set_sched <RR|SRTF> [quantum]         Cambiar planificador (RR defecto: 2)\n"
        << "  set_speed <ms_por_tick>               Ajustar velocidad (50-5000 ms; defecto: 1000)\n"
        << "  start | pause                         Iniciar/reanudar o pausar CPU\n"
        << "  gantt | stats                         Mostrar ejecucion o metricas\n"
        << "  help                                  Mostrar esta ayuda\n"
        << "  exit                                  Cerrar la simulacion\n"
        << "Tambien se acepta el formato antiguo: llegada nombre burst [prioridad].\n"
        << "En archivos de traza se permiten lineas vacias y comentarios que comiencen con #.\n"
        << "Las rutas con espacios pueden escribirse entre comillas dobles.\n";
}

bool require_count(const std::vector<std::string>& tokens,
                   std::size_t minimum, std::size_t maximum,
                   const std::string& usage) {
    if (tokens.size() >= minimum && tokens.size() <= maximum) return true;
    if (tokens.size() < minimum) {
        std::cout << "[ERROR DE SINTAXIS]: Parámetros insuficientes.\n";
    } else {
        std::cout << "[ERROR DE SINTAXIS]: Parámetros adicionales no permitidos.\n";
    }
    std::cout << "Uso correcto: " << usage << '\n';
    return false;
}

void run_cli(Simulator& simulator) {
    std::string line;
    bool exit_requested = false;
    while (!exit_requested) {
        std::cout << "sim> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        std::vector<std::string> tokens;
        if (!tokenize(line, tokens)) {
            std::cout << "[ERROR DE SINTAXIS]: Comillas sin cerrar o argumento entrecomillado inválido.\n";
            continue;
        }
        if (tokens.empty()) continue;

        const std::string& command = tokens[0];
        int first = 0;
        int second = 3;
        if (command == "create") {
            if (!require_count(tokens, 3, 4,
                              "create <nombre> <burst> [prioridad]")) continue;
            if (tokens[1].empty() ||
                tokens[1].find_first_of(" \t\r\n") != std::string::npos ||
                !parse_integer(tokens[2], first) || first <= 0) {
                std::cout << "[ERROR DE TIPO]: El nombre debe ser una cadena sin espacios "
                             "y burst_time un entero positivo.\n"
                          << "Uso correcto: create <nombre> <burst_time> [prioridad]\n";
                continue;
            }
            if (tokens.size() == 4 &&
                (!parse_integer(tokens[3], second) || second < 1 || second > 5)) {
                std::cout << "[ERROR DE TIPO]: La prioridad debe ser un entero entre 1 y 5.\n"
                          << "Uso correcto: create <nombre> <burst_time> [prioridad]\n";
                continue;
            }
            simulator.create_process(tokens[1], first, second);
        } else if (command == "load") {
            if (!require_count(tokens, 2, 2, "load <archivo>")) continue;
            std::string error;
            if (!simulator.load_trace(tokens[1], error)) {
                std::cout << "[ERROR]: " << error << '\n';
            }
        } else if (command == "ps") {
            if (!require_count(tokens, 1, 1, "ps")) continue;
            simulator.print_processes();
        } else if (command == "pcb") {
            if (!require_count(tokens, 2, 2, "pcb <PID>")) continue;
            if (!parse_integer(tokens[1], first) || first <= 0) {
                std::cout << "[ERROR DE TIPO]: El PID debe ser un número entero positivo.\n";
                continue;
            }
            if (!simulator.print_pcb(first)) {
                std::cout << "[ERROR DE PARAMETRO]: No existe el proceso con PID "
                          << first << ".\n";
            }
        } else if (command == "set_sched") {
            if (!require_count(tokens, 2, 3, "set_sched <RR|SRTF> [quantum]")) continue;
            if (tokens[1] == "RR") {
                int quantum = 2;
                if (tokens.size() == 3 &&
                    (!parse_integer(tokens[2], quantum) || quantum <= 0)) {
                    std::cout << "[ERROR DE TIPO]: El quantum debe ser un entero positivo.\n"
                              << "Uso correcto: set_sched RR [quantum]\n";
                    continue;
                }
                simulator.set_scheduler(true, quantum);
            } else if (tokens[1] == "SRTF" && tokens.size() == 2) {
                simulator.set_scheduler(false, 2);
            } else if (tokens[1] == "SRTF") {
                std::cout << "[ERROR DE PARAMETRO]: SRTF no acepta quantum. "
                             "Uso correcto: set_sched SRTF\n";
            } else {
                std::cout << "[ERROR DE PARAMETRO]: Algoritmo no reconocido. "
                             "Algoritmos válidos: RR, SRTF.\n";
            }
        } else if (command == "set_speed") {
            if (!require_count(tokens, 2, 2, "set_speed <ms_por_tick>")) continue;
            if (!parse_integer(tokens[1], first)) {
                std::cout << "[ERROR DE TIPO]: ms_por_tick debe ser un entero.\n";
                continue;
            }
            if (first < 50 || first > 5000) {
                std::cout << "[ERROR DE PARAMETRO]: La velocidad debe estar entre 50 y 5000 ms.\n";
                continue;
            }
            simulator.set_speed(first);
        } else if (command == "start") {
            if (!require_count(tokens, 1, 1, "start")) continue;
            simulator.start_cpu();
        } else if (command == "pause") {
            if (!require_count(tokens, 1, 1, "pause")) continue;
            simulator.pause_cpu();
        } else if (command == "gantt") {
            if (!require_count(tokens, 1, 1, "gantt")) continue;
            simulator.print_gantt();
        } else if (command == "stats") {
            if (!require_count(tokens, 1, 1, "stats")) continue;
            simulator.print_stats();
        } else if (command == "help") {
            if (!require_count(tokens, 1, 1, "help")) continue;
            print_help();
        } else if (command == "exit") {
            if (!require_count(tokens, 1, 1, "exit")) continue;
            exit_requested = true;
        } else {
            std::cout << "[ERROR DE PARAMETRO]: Comando desconocido. Escriba help para ver las opciones.\n";
        }
    }
}

}  // namespace

int main() {
    try {
        Simulator simulator;
        simulator.start_worker();
        run_cli(simulator);
        simulator.shutdown_and_join();
    } catch (const std::exception& error) {
        std::cerr << "[ERROR]: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
