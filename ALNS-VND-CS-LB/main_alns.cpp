#include "ALNS_um.h"
#include "Instance_um.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <arquivo_instancia>\n";
        return 1;
    }

    const std::string instance_path = argv[1];

    try {
        // Le a instancia usando a mesma classe parser ja usada pelo modelo exato.
        InstanceUM instance;
        std::string error_message;
        if (!instance.loadFromFile(instance_path, &error_message)) {
            std::cerr << "Erro ao carregar instancia: " << error_message << '\n';
            return 1;
        }

        const auto& metadata = instance.getMetadata();
        std::cout << "Instancia carregada com sucesso para o ALNS.\n";
        std::cout << "Clientes: " << metadata.num_clientes << '\n';
        std::cout << "Veiculos: " << metadata.num_veiculos << '\n';
        std::cout << "Itens: " << metadata.num_itens << "\n\n";

        // Parametros iniciais da metaheuristica.
        // Esses valores sao um ponto de partida e podem ser ajustados depois
        // conforme o comportamento observado nas instancias.
        ALNSUM::Parameters parameters;
        parameters.iterations = 10000;
        parameters.max_iterations_without_improvement = 1000;
        parameters.segment_size = 50;
        parameters.min_remove = 10;
        parameters.max_remove = 40;
        parameters.initial_temperature = 5000.0;
        parameters.cooling_rate = 0.995;
        parameters.reaction_factor = 0.25;
        parameters.random_seed = 42U;
        parameters.enable_vnd = true;
        parameters.enable_clustering_search = true;
        parameters.num_clusters = 8;
        parameters.cluster_activation_volume = 20;
        parameters.cluster_max_inefficacy = 3;
        parameters.local_branching_radius = 8;
        parameters.demand_local_branching_radius = 20;
        parameters.local_branching_time_limit = 10.0;
        parameters.cluster_intensification_tolerance = 0.05;
        parameters.large_instance_demand_threshold = 500;
        parameters.enable_final_intensification = true;
        parameters.final_local_branching_radius = 12;
        parameters.final_demand_local_branching_radius = 40;
        parameters.final_local_branching_time_limit = 30.0;

        ALNSUM alns(instance, parameters);
        const ALNSUM::Solution best_solution = alns.solve();
        alns.printSolutionSummary(best_solution, std::cout);

        const std::filesystem::path results_dir = std::filesystem::current_path() / "results_alns";
        std::filesystem::create_directories(results_dir);
        const std::filesystem::path instance_file(instance_path);
        const std::filesystem::path csv_path =
            results_dir / ("alns_" + instance_file.stem().string() + ".csv");
        alns.exportSolutionCsv(best_solution, csv_path.string());
        std::cout << "CSV da solucao salvo em: " << csv_path.string() << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "Erro: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
