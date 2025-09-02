// bench_runner.cpp
// Compilar: g++ -std=c++17 -O2 bench_runner.cpp -o bench_runner
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <numeric>
#include <map>

// Ejecuta un comando y devuelve su salida como string
std::string execCmd(const std::string& cmd,
                    const std::map<std::string,std::string>& env_extra = {}) {
    // Copiar entorno actual
    std::string result;
    char buffer[256];

    // Construir entorno combinado
    std::string env_str;
    for (auto& kv : env_extra) {
#ifdef _WIN32
        _putenv_s(kv.first.c_str(), kv.second.c_str());
#else
        setenv(kv.first.c_str(), kv.second.c_str(), 1);
#endif
    }

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

// Extrae "Tiempo total: X ms"
double parseTime(const std::string& text) {
    std::regex re(R"(Tiempo\s+total:\s*([0-9]+(?:[.,][0-9]+)?)\s*ms)");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        std::string num = m[1];
        for (char& c : num) if (c == ',') c = '.';
        return std::stod(num);
    }
    return -1.0;
}

std::vector<double> runBench(const std::string& bin, int frames, int runs,
                             const std::map<std::string,std::string>& env = {}) {
    std::vector<double> times;
    for (int i = 0; i < runs; i++) {
        std::string cmd = bin + " --benchmark --frames=" + std::to_string(frames);
        std::string out = execCmd(cmd, env);
        double t = parseTime(out);
        if (t > 0) {
            times.push_back(t);
            std::cout << "  Run " << (i+1) << ": " << t << " ms\n";
        } else {
            std::cout << "  Run " << (i+1) << ": FAIL\n";
            std::cerr << out << "\n"; // para debug
        }
    }
    return times;
}

double mean(const std::vector<double>& v) {
    if (v.empty()) return -1.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

int main() {
    const std::string seqBin = "secuencial\\screensaver.exe";
    const std::string parBin = "paralelo\\screensaver.exe";
    int frames = 500, runs = 10;

    std::cout << "== Benchmark Screensaver ==\n";

    std::cout << "\n>> Secuencial:\n";
    auto t_seq = runBench(seqBin, frames, runs);

    std::cout << "\n>> Paralelo (4 hilos):\n";
    auto t_par4 = runBench(parBin, frames, runs, {{"OMP_NUM_THREADS","4"}});

    std::cout << "\n>> Paralelo (8 hilos):\n";
    auto t_par8 = runBench(parBin, frames, runs, {{"OMP_NUM_THREADS","8"}});

    double avg_seq  = mean(t_seq);
    double avg_par4 = mean(t_par4);
    double avg_par8 = mean(t_par8);

    std::cout << "\n== Resumen Promedios ==\n";
    std::cout << "Secuencial: " << avg_seq  << " ms\n";
    if (avg_par4 > 0)
        std::cout << "Par (4 hilos): " << avg_par4 << " ms  | Speedup=" 
                  << (avg_seq/avg_par4) << "  Eff=" << (avg_seq/avg_par4/4) << "\n";
    if (avg_par8 > 0)
        std::cout << "Par (8 hilos): " << avg_par8 << " ms  | Speedup=" 
                  << (avg_seq/avg_par8) << "  Eff=" << (avg_seq/avg_par8/8) << "\n";

    return 0;
}
