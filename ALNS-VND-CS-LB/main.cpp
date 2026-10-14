#include "Instance_um.h"
#include "Model_um.h"

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
        InstanceUM instance;
        std::string error_message;
        if (!instance.loadFromFile(instance_path, &error_message)) {
            std::cerr << "Erro ao carregar instancia: " << error_message << '\n';
            return 1;
        }

        const auto& metadata = instance.getMetadata();
        std::cout << "Instancia carregada com sucesso.\n";
        std::cout << "Clientes: " << metadata.num_clientes << '\n';
        std::cout << "Veiculos: " << metadata.num_veiculos << '\n';
        std::cout << "Ofertas: " << metadata.num_ofertas << '\n';
        std::cout << "Itens: " << metadata.num_itens << '\n';

        const std::filesystem::path results_dir = std::filesystem::current_path() / "results_modelo";
        std::filesystem::create_directories(results_dir);
        const std::filesystem::path logs_dir = results_dir / "logs";
        std::filesystem::create_directories(logs_dir);
        const std::filesystem::path instance_file(instance_path);

        const std::filesystem::path log_path =
            logs_dir / ("gurobi_" + instance_file.stem().string() + ".txt");

        ModelUM model(instance);
        model.setLogFilePath(log_path.string());
        model.build();
        model.optimize();
        model.printSolutionSummary(std::cout);

        const std::filesystem::path csv_path =
            results_dir / ("modelo_" + instance_file.stem().string() + ".csv");
        model.exportSolutionCsv(csv_path.string());
        std::cout << "CSV da solucao salvo em: " << csv_path.string() << '\n';
        std::cout << "Log do Gurobi salvo em: " << log_path.string() << '\n';
    } catch (const GRBException& ex) {
        std::cerr << "Erro Gurobi: " << ex.getMessage()
                  << " (codigo " << ex.getErrorCode() << ")\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Erro: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
