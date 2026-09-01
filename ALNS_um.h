#ifndef ALNS_UM_H
#define ALNS_UM_H

#include "Instance_um.h"

#include "gurobi_c++.h"

#include <chrono>
#include <random>
#include <string>
#include <vector>

// Classe que implementa uma metaheuristica ALNS para o problema UM.
// A ideia central e manter uma solucao item-cliente -> veiculo e, a cada iteracao,
// destruir parte dessa solucao e repara-la usando operadores adaptativos.
class ALNSUM {
public:
    // Parametros principais de controle da metaheuristica.
    struct Parameters {
        int iterations = 10000;
        int max_iterations_without_improvement = 1000;
        int segment_size = 50;
        int min_remove = 10;
        int max_remove = 40;
        double initial_temperature = 5000.0;
        double cooling_rate = 0.995;
        double reaction_factor = 0.25;
        unsigned int random_seed = 42U;
        bool enable_vnd = true;

        // Parametros da camada de Clustering Search.
        // O ALNS continua gerando solucoes; o CS agrupa solucoes parecidas pela frota ativa
        // e chama Local Branching apenas quando uma regiao parece promissora.
        bool enable_clustering_search = true;
        int num_clusters = 8;
        int cluster_activation_volume = 20;
        int cluster_max_inefficacy = 3;
        int local_branching_radius = 8;
        int demand_local_branching_radius = 20;
        double local_branching_time_limit = 10.0;
        double cluster_intensification_tolerance = 0.05;
        int large_instance_demand_threshold = 500;
        bool enable_final_intensification = true;
        int final_local_branching_radius = 12;
        int final_demand_local_branching_radius = 40;
        double final_local_branching_time_limit = 30.0;

        // Mineracao de padroes frequentes (Data Mining) sobre um pool de solucoes de
        // elite, usada para fixar variaveis antes de cada chamada de Local Branching.
        // Reduz o tamanho do MIP resolvido pelo Gurobi, permitindo explorar raios
        // maiores ou usar melhor o tempo disponivel.
        bool enable_pattern_mining = true;
        int pattern_mining_pool_size = 30;
        int pattern_mining_min_pool_size = 8;
        double pattern_mining_support_threshold = 0.9;
    };

    // Representa uma demanda elementar do problema: item i exigido pelo cliente c.
    struct Demand {
        int item_id = -1;
        int client_id = -1;
    };

    // Estrutura completa de uma solucao do ALNS.
    struct Solution {
        // assigned_vehicle[i][c] = veiculo que atende o par (i,c).
        // Valor -1 indica nao atendimento.
        // Valor -2 indica que o par (i,c) nem faz parte da demanda.
        std::vector<std::vector<int>> assigned_vehicle;

        // Cargas agregadas por veiculo para avaliacao rapida das restricoes.
        std::vector<int> vehicle_weight_load;
        std::vector<double> vehicle_volume_load;
        std::vector<int> vehicle_num_assignments;

        // Valor da funcao objetivo da solucao.
        double objective = 0.0;

        // Estatisticas da execucao associadas a melhor solucao encontrada.
        int iterations_performed = 0;
        double runtime_seconds = 0.0;
        std::string stop_reason;

        // Tempos acumulados por etapa do algoritmo para diagnostico de desempenho.
        double initial_solution_seconds = 0.0;
        double destruction_seconds = 0.0;
        double repair_pool_seconds = 0.0;
        double repair_seconds = 0.0;
        double local_search_seconds = 0.0;
        double evaluation_seconds = 0.0;
        double acceptance_seconds = 0.0;
        double weight_update_seconds = 0.0;
        double clustering_search_seconds = 0.0;
        double local_branching_seconds = 0.0;
        int clustering_search_calls = 0;
        int local_branching_calls = 0;
        int local_branching_improvements = 0;
        double final_intensification_seconds = 0.0;
        int final_intensification_calls = 0;
        int final_intensification_improvements = 0;

        // Estatisticas da mineracao de padroes usada para fixar variaveis no Local Branching.
        int pattern_mining_calls = 0;
        long long pattern_mining_variables_fixed = 0;
    };

    explicit ALNSUM(const InstanceUM& instance);
    ALNSUM(const InstanceUM& instance, Parameters parameters);

    // Executa o ALNS completo e devolve a melhor solucao encontrada.
    Solution solve();

    // Imprime um resumo amigavel da solucao obtida.
    void printSolutionSummary(const Solution& solution, std::ostream& out) const;
    void exportSolutionCsv(const Solution& solution, const std::string& output_path) const;

private:
    // Metadados simples de um operador adaptativo.
    struct OperatorStats {
        std::string name;
        double weight = 1.0;
        double score = 0.0;
        int uses = 0;
        int total_uses = 0;
        int total_accepts = 0;
        int total_improvements = 0;
    };

    // Cada cluster representa uma regiao do espaco de solucoes.
    // O centro guarda a melhor solucao conhecida naquela regiao, o volume conta quantas
    // solucoes foram associadas ao cluster, e a ineficacia evita insistir em regioes
    // onde o Local Branching nao vem produzindo melhora.
    struct Cluster {
        Solution center;
        bool initialized = false;
        int volume = 0;
        int inefficacy = 0;
        int local_branching_calls = 0;
        int local_branching_improvements = 0;
    };

    const InstanceUM& instance_;
    Parameters parameters_;
    mutable std::mt19937 rng_;

    std::vector<Demand> demands_;
    std::vector<std::vector<bool>> psi_allowed_;
    std::vector<std::vector<bool>> gamma_allowed_;

    std::vector<OperatorStats> destroy_operators_;
    std::vector<OperatorStats> repair_operators_;
    std::vector<Cluster> clusters_;
    double cs_local_branching_seconds_ = 0.0;
    int cs_local_branching_calls_ = 0;
    int cs_local_branching_improvements_ = 0;

    // Pool de solucoes de elite usado pela mineracao de padroes frequentes.
    // Mantem as melhores solucoes distintas ja aceitas pelo ALNS para servir de base
    // estatistica as fixacoes de variaveis aplicadas antes de cada Local Branching.
    std::vector<Solution> elite_pool_;
    int cs_pattern_mining_calls_ = 0;
    long long cs_pattern_mining_variables_fixed_ = 0;

    // Preparacao de estruturas auxiliares derivadas da instancia.
    void buildDemandList();
    void buildCompatibilityMatrices();
    void initializeOperators();

    // Construcao e avaliacao de solucoes.
    Solution createEmptySolution() const;
    Solution buildInitialSolution();
    Solution buildPriorityGreedyInitialSolution();
    double evaluateSolution(Solution& solution) const;
    bool canAssignDemandToVehicle(const Solution& solution, int item_id, int client_id, int vehicle_id) const;
    void assignDemandToVehicle(Solution& solution, int item_id, int client_id, int vehicle_id) const;
    void unassignDemand(Solution& solution, int item_id, int client_id) const;
    int chooseBestVehicleForDemand(const Solution& base_solution, int item_id, int client_id) const;
    double computeAssignmentDelta(const Solution& solution, int item_id, int client_id, int vehicle_id) const;
    double getDemandPenalty(int item_id) const;
    std::vector<int> getPromisingVehiclesForDemand(
        const Solution& solution,
        int item_id,
        int client_id,
        int max_candidates
    ) const;

    // Operadores de destruicao.
    std::vector<Demand> destroyRandomAssignments(Solution& solution, int remove_count);
    std::vector<Demand> destroyWorstVehicle(Solution& solution, int remove_count);
    std::vector<Demand> destroyHighPenaltyAssignments(Solution& solution, int remove_count);
    std::vector<Demand> destroyUnderfilledVehicles(Solution& solution, int remove_count);

    // Operadores de reparacao.
    void repairGreedyBestInsertion(Solution& solution, std::vector<Demand>& removed_demands);
    void repairGreedyRandomOrder(Solution& solution, std::vector<Demand>& removed_demands);
    void repairRegretInsertion(Solution& solution, std::vector<Demand>& removed_demands);
    void repairBestNetGainInsertion(Solution& solution, std::vector<Demand>& removed_demands);

    // Prepara o conjunto de demandas que os reparos vao tentar reintroduzir.
    // Alem das demandas removidas, tambem adiciona demandas ainda nao atendidas para
    // que o ALNS possa atacar diretamente a parcela de penalidade da funcao objetivo.
    std::vector<Demand> buildRepairCandidatePool(
        const Solution& solution,
        const std::vector<Demand>& removed_demands
    ) const;

    // Busca local aplicada apos a fase de reparacao.
    // A implementacao atual usa um VND leve para manter o custo controlado.
    double computeInsertionDelta(
        const Solution& solution,
        int item_id,
        int client_id,
        int target_vehicle
    ) const;
    double computeRelocationDelta(
        const Solution& solution,
        int item_id,
        int client_id,
        int target_vehicle
    ) const;
    double computeSwapWithUnservedDelta(
        const Solution& solution,
        int served_item_id,
        int served_client_id,
        int unserved_item_id,
        int unserved_client_id
    ) const;
    bool localSearchInsertUnservedDemands(Solution& solution);
    bool localSearchRelocateDemands(Solution& solution);
    bool localSearchCloseUnderfilledVehicle(Solution& solution);
    bool localSearchSwapServedAndUnserved(Solution& solution);
    bool localSearchSwapServedDemands(Solution& solution);
    bool localSearchEjectForUnservedDemand(Solution& solution);
    void applyLocalSearch(Solution& solution);

    // Mecanismos adaptativos do ALNS.
    int selectOperatorIndex(const std::vector<OperatorStats>& operators);
    bool acceptCandidate(double candidate_objective, double current_objective, double temperature);
    void updateOperatorScore(OperatorStats& operator_stats, double reward);
    void updateAdaptiveWeights(std::vector<OperatorStats>& operators);

    // Clustering Search e Local Branching.
    void initializeClusters();
    int computeVehiclePatternDistance(const Solution& lhs, const Solution& rhs) const;
    int findNearestClusterIndex(const Solution& solution) const;
    bool processSolutionInClusteringSearch(Solution& solution, Solution& best);
    bool intensifyClusterWithLocalBranching(Cluster& cluster, Solution& best);
    bool solveLocalBranchingAroundCenter(const Solution& center, Solution& improved_solution) const;
    bool solveLocalBranchingAroundCenter(
        const Solution& center,
        Solution& improved_solution,
        int vehicle_radius,
        int demand_radius,
        double time_limit,
        int* mined_variables_fixed = nullptr
    ) const;
    bool applyFinalIntensification(Solution& best);

    // Mineracao de padroes frequentes (Data Mining) aplicada ao Local Branching.
    // Mantem o pool de elite atualizado e usa votacao por suporte para fixar variaveis
    // x/y no MIP antes de cada resolucao, reduzindo o espaco de busca do solver.
    void updateElitePool(const Solution& solution);
    int applyMinedVariableFixings(
        std::vector<GRBVar>& y,
        std::vector<std::vector<std::vector<GRBVar>>>& x,
        const std::vector<std::vector<std::vector<bool>>>& x_exists
    ) const;

    // Funcoes utilitarias usadas pelos operadores.
    std::vector<Demand> getAssignedDemands(const Solution& solution) const;
    std::vector<Demand> getUnassignedDemands(const Solution& solution) const;
    int drawRemovalCount(const Solution& solution);
};

#endif
