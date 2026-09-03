//Pra compilar o código
// call "D:\VS Code Tools\VC\Auxiliary\Build\vcvars64.bat"
// cl /MD /EHsc /std:c++17 /I"C:\gurobi912\win64\include" main_alns.cpp Instance_um.cpp ALNS_um.cpp /link /OUT:alns_um_vnd.exe /LIBPATH:"C:\gurobi912\win64\lib" gurobi_c++md2019.lib gurobi91.lib

#include "ALNS_um.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

ALNSUM::ALNSUM(const InstanceUM& instance)
    : ALNSUM(instance, Parameters{}) {}

ALNSUM::ALNSUM(const InstanceUM& instance, Parameters parameters)
    : instance_(instance), parameters_(parameters), rng_(parameters.random_seed) {
    buildDemandList();
    buildCompatibilityMatrices();
    initializeOperators();
    initializeClusters();
}

// Monta a lista explicita de demandas (item, cliente) a partir de Phi(i).
void ALNSUM::buildDemandList() {
    const auto& phi = instance_.getPhi();

    demands_.clear();
    for (std::size_t item_id = 0; item_id < phi.size(); ++item_id) {
        for (int client_id : phi[item_id]) {
            demands_.push_back({static_cast<int>(item_id), client_id});
        }
    }
}

// Preprocessa as relacoes Psi(i) e Gamma(v) em matrizes booleanas para acelerar testes.
void ALNSUM::buildCompatibilityMatrices() {
    const auto& metadata = instance_.getMetadata();
    const auto& psi = instance_.getPsi();
    const auto& gamma = instance_.getGamma();

    psi_allowed_.assign(metadata.num_itens, std::vector<bool>(metadata.num_veiculos, false));
    gamma_allowed_.assign(metadata.num_veiculos, std::vector<bool>(metadata.num_clientes, false));

    for (int item_id = 0; item_id < metadata.num_itens; ++item_id) {
        for (int vehicle_id : psi[item_id]) {
            psi_allowed_[item_id][vehicle_id] = true;
        }
    }

    for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
        for (int client_id : gamma[vehicle_id]) {
            gamma_allowed_[vehicle_id][client_id] = true;
        }
    }
}

// Registra operadores de destruicao e reparacao.
// Os reparos comecam equiprovaveis; as destruicoes ja recebem uma leve preferencia
// inicial para operadores que atacam custo local e frete morto.
void ALNSUM::initializeOperators() {
    destroy_operators_ = {
        {"destroy_random", 0.10, 0.0, 0, 0, 0, 0},
        {"destroy_worst_vehicle", 0.40, 0.0, 0, 0, 0, 0},
        {"destroy_high_penalty", 0.10, 0.0, 0, 0, 0, 0},
        {"destroy_high_dead_freight_vehicle", 0.40, 0.0, 0, 0, 0, 0}
    };

    repair_operators_ = {
        {"repair_greedy_best", 0.25, 0.0, 0, 0, 0, 0},
        {"repair_greedy_random", 0.25, 0.0, 0, 0, 0, 0},
        {"repair_regret", 0.25, 0.0, 0, 0, 0, 0},
        {"repair_best_net_gain", 0.25, 0.0, 0, 0, 0, 0}
    };
}

// Cria uma solucao vazia com todas as demandas marcadas como nao atendidas.
ALNSUM::Solution ALNSUM::createEmptySolution() const {
    const auto& metadata = instance_.getMetadata();
    const auto& phi = instance_.getPhi();

    Solution solution;
    solution.assigned_vehicle.assign(
        metadata.num_itens,
        std::vector<int>(metadata.num_clientes, -2)
    );
    solution.vehicle_weight_load.assign(metadata.num_veiculos, 0);
    solution.vehicle_volume_load.assign(metadata.num_veiculos, 0.0);
    solution.vehicle_num_assignments.assign(metadata.num_veiculos, 0);

    // Apenas pares pertencentes a Phi(i) fazem parte do espaco de decisao.
    for (std::size_t item_id = 0; item_id < phi.size(); ++item_id) {
        for (int client_id : phi[item_id]) {
            solution.assigned_vehicle[item_id][client_id] = -1;
        }
    }

    evaluateSolution(solution);
    return solution;
}

// Calcula a funcao objetivo completa da solucao.
// O valor segue a mesma logica do modelo: custo de ativacao + frete morto + penalidade.
double ALNSUM::evaluateSolution(Solution& solution) const {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();

    double objective = 0.0;

    // Soma custo de uso do veiculo e frete morto quando ele possui ao menos uma alocacao.
    for (std::size_t vehicle_id = 0; vehicle_id < veiculos.size(); ++vehicle_id) {
        if (solution.vehicle_num_assignments[vehicle_id] > 0) {
            objective += veiculos[vehicle_id].custo;
            objective += std::max(
                0.0,
                veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id])
            );
        }
    }

    // Para cada demanda nao atendida, aplica-se a penalidade correspondente ao item.
    for (const Demand& demand : demands_) {
        if (solution.assigned_vehicle[demand.item_id][demand.client_id] == -1) {
            objective += itens[demand.item_id].peso * itens[demand.item_id].penalidade;
        }
    }

    solution.objective = objective;
    return objective;
}

// Retorna a penalidade individual de deixar um item sem atendimento para um cliente.
double ALNSUM::getDemandPenalty(int item_id) const {
    const auto& itens = instance_.getItens();
    return itens[item_id].peso * itens[item_id].penalidade;
}

// Seleciona apenas os veiculos mais promissores para uma demanda.
// A ideia e evitar testar todos os veiculos no reparo, priorizando:
// 1. veiculos ja ativos
// 2. veiculos que ficariam mais bem preenchidos apos a insercao
// 3. veiculos de menor custo como criterio de desempate
std::vector<int> ALNSUM::getPromisingVehiclesForDemand(
    const Solution& solution,
    int item_id,
    int client_id,
    int max_candidates
) const {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();

    struct CandidateVehicle {
        int vehicle_id = -1;
        bool is_used = false;
        double fill_after = 0.0;
        double cost = 0.0;
    };

    std::vector<CandidateVehicle> candidates;
    candidates.reserve(instance_.getMetadata().num_veiculos);

    for (int vehicle_id = 0; vehicle_id < instance_.getMetadata().num_veiculos; ++vehicle_id) {
        if (!canAssignDemandToVehicle(solution, item_id, client_id, vehicle_id)) {
            continue;
        }

        const double weight_fill_after =
            static_cast<double>(solution.vehicle_weight_load[vehicle_id] + itens[item_id].peso) /
            std::max(1, veiculos[vehicle_id].capacidade_peso);
        const double volume_fill_after =
            (solution.vehicle_volume_load[vehicle_id] + itens[item_id].volume) /
            std::max(1.0, static_cast<double>(veiculos[vehicle_id].capacidade_volume));

        candidates.push_back(
            {
                vehicle_id,
                solution.vehicle_num_assignments[vehicle_id] > 0,
                std::max(weight_fill_after, volume_fill_after),
                static_cast<double>(veiculos[vehicle_id].custo)
            }
        );
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const CandidateVehicle& lhs, const CandidateVehicle& rhs) {
            if (lhs.is_used != rhs.is_used) {
                return lhs.is_used > rhs.is_used;
            }
            if (std::abs(lhs.fill_after - rhs.fill_after) > 1e-9) {
                return lhs.fill_after > rhs.fill_after;
            }
            return lhs.cost < rhs.cost;
        }
    );

    const int limit = std::min(max_candidates, static_cast<int>(candidates.size()));
    std::vector<int> selected;
    selected.reserve(limit);
    for (int index = 0; index < limit; ++index) {
        selected.push_back(candidates[index].vehicle_id);
    }

    return selected;
}

// Verifica compatibilidade estrutural e capacidade residual do veiculo.
bool ALNSUM::canAssignDemandToVehicle(
    const Solution& solution,
    int item_id,
    int client_id,
    int vehicle_id
) const {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();

    if (!psi_allowed_[item_id][vehicle_id]) {
        return false;
    }
    if (!gamma_allowed_[vehicle_id][client_id]) {
        return false;
    }

    // Como a estrutura ja e por par (item, cliente), o modelo heuristico tambem respeita
    // a unicidade: cada par pode ficar associado a no maximo um veiculo.
    if (solution.assigned_vehicle[item_id][client_id] >= 0) {
        return false;
    }

    const int new_weight = solution.vehicle_weight_load[vehicle_id] + itens[item_id].peso;
    const double new_volume = solution.vehicle_volume_load[vehicle_id] + itens[item_id].volume;

    if (new_weight > veiculos[vehicle_id].capacidade_peso) {
        return false;
    }
    if (new_volume > veiculos[vehicle_id].capacidade_volume + 1e-9) {
        return false;
    }

    return true;
}

// Aplica a alocacao de um par (item, cliente) em um veiculo, atualizando cargas agregadas.
void ALNSUM::assignDemandToVehicle(Solution& solution, int item_id, int client_id, int vehicle_id) const {
    const auto& itens = instance_.getItens();

    solution.assigned_vehicle[item_id][client_id] = vehicle_id;
    solution.vehicle_weight_load[vehicle_id] += itens[item_id].peso;
    solution.vehicle_volume_load[vehicle_id] += itens[item_id].volume;
    solution.vehicle_num_assignments[vehicle_id] += 1;
}

// Remove uma alocacao existente e devolve o par (item, cliente) ao estado nao atendido.
void ALNSUM::unassignDemand(Solution& solution, int item_id, int client_id) const {
    const auto& itens = instance_.getItens();

    const int current_vehicle = solution.assigned_vehicle[item_id][client_id];
    if (current_vehicle < 0) {
        return;
    }

    solution.assigned_vehicle[item_id][client_id] = -1;
    solution.vehicle_weight_load[current_vehicle] -= itens[item_id].peso;
    solution.vehicle_volume_load[current_vehicle] -= itens[item_id].volume;
    solution.vehicle_num_assignments[current_vehicle] -= 1;
}

// Escolhe o veiculo que produz a menor funcao objetivo depois de inserir a demanda.
// Se nenhum veiculo viavel existir, devolve -1 e a demanda permanece penalizada.
int ALNSUM::chooseBestVehicleForDemand(const Solution& base_solution, int item_id, int client_id) const {
    int best_vehicle = -1;
    double best_delta = std::numeric_limits<double>::infinity();

    const std::vector<int> promising_vehicles =
        getPromisingVehiclesForDemand(base_solution, item_id, client_id, 5);

    for (int vehicle_id : promising_vehicles) {
        const double delta = computeAssignmentDelta(base_solution, item_id, client_id, vehicle_id);
        if (delta < best_delta) {
            best_delta = delta;
            best_vehicle = vehicle_id;
        }
    }

    return best_vehicle;
}

// Calcula a variacao de custo de inserir uma demanda em um veiculo especifico.
// Valor negativo significa melhoria da funcao objetivo.
double ALNSUM::computeAssignmentDelta(
    const Solution& solution,
    int item_id,
    int client_id,
    int vehicle_id
) const {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();

    if (!canAssignDemandToVehicle(solution, item_id, client_id, vehicle_id)) {
        return std::numeric_limits<double>::infinity();
    }

    const double penalty_removed = getDemandPenalty(item_id);
    const bool vehicle_was_unused = (solution.vehicle_num_assignments[vehicle_id] == 0);
    const double current_dead_freight = vehicle_was_unused
        ? 0.0
        : std::max(0.0, veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id]));

    const int new_weight = solution.vehicle_weight_load[vehicle_id] + itens[item_id].peso;
    const double new_dead_freight = std::max(0.0, veiculos[vehicle_id].carga_minima - static_cast<double>(new_weight));
    const double activation_cost = vehicle_was_unused ? veiculos[vehicle_id].custo : 0.0;

    return activation_cost + (new_dead_freight - current_dead_freight) - penalty_removed;
}

// Alias semantico para a insercao de uma demanda nao atendida em um veiculo.
// Mantemos separado para deixar a intencao da vizinhanca de insercao mais clara no VND.
double ALNSUM::computeInsertionDelta(
    const Solution& solution,
    int item_id,
    int client_id,
    int target_vehicle
) const {
    return computeAssignmentDelta(solution, item_id, client_id, target_vehicle);
}

// Construtivo-base anterior: guloso por prioridade de penalidade.
// Mantemos este construtivo porque ele pode ser melhor em instancias nas quais
// vale a pena ativar veiculos cedo para capturar demandas muito penalizadas.
ALNSUM::Solution ALNSUM::buildPriorityGreedyInitialSolution() {
    const auto& itens = instance_.getItens();

    Solution solution = createEmptySolution();
    std::vector<Demand> ordered_demands = demands_;

    std::sort(
        ordered_demands.begin(),
        ordered_demands.end(),
        [&itens](const Demand& lhs, const Demand& rhs) {
            const double lhs_cost = itens[lhs.item_id].peso * itens[lhs.item_id].penalidade;
            const double rhs_cost = itens[rhs.item_id].peso * itens[rhs.item_id].penalidade;
            return lhs_cost > rhs_cost;
        }
    );

    for (const Demand& demand : ordered_demands) {
        const int best_vehicle = chooseBestVehicleForDemand(solution, demand.item_id, demand.client_id);
        if (best_vehicle >= 0) {
            assignDemandToVehicle(solution, demand.item_id, demand.client_id, best_vehicle);
            evaluateSolution(solution);
        }
    }

    evaluateSolution(solution);
    return solution;
}

// Construcao da solucao inicial do ALNS.
// Mantemos apenas o construtivo que apresentou melhor desempenho nos testes:
// o guloso por prioridade de penalidade.
ALNSUM::Solution ALNSUM::buildInitialSolution() {
    return buildPriorityGreedyInitialSolution();
}

// Retorna a lista dos pares (item, cliente) atualmente atendidos por algum veiculo.
std::vector<ALNSUM::Demand> ALNSUM::getAssignedDemands(const Solution& solution) const {
    std::vector<Demand> assigned;
    assigned.reserve(demands_.size());

    for (const Demand& demand : demands_) {
        if (solution.assigned_vehicle[demand.item_id][demand.client_id] >= 0) {
            assigned.push_back(demand);
        }
    }

    return assigned;
}

// Retorna a lista de demandas ainda nao atendidas na solucao corrente.
std::vector<ALNSUM::Demand> ALNSUM::getUnassignedDemands(const Solution& solution) const {
    std::vector<Demand> unassigned;
    unassigned.reserve(demands_.size());

    for (const Demand& demand : demands_) {
        if (solution.assigned_vehicle[demand.item_id][demand.client_id] == -1) {
            unassigned.push_back(demand);
        }
    }

    return unassigned;
}

// Sorteia o numero de remocoes respeitando a quantidade atual de demandas atendidas.
int ALNSUM::drawRemovalCount(const Solution& solution) {
    const int assigned_count = static_cast<int>(getAssignedDemands(solution).size());
    if (assigned_count <= 0) {
        return 0;
    }

    // Em instancias maiores, destruicoes muito pequenas deixam a busca excessivamente local.
    // Por isso, forçamos um tamanho minimo proporcional ao numero de demandas atendidas.
    const int proportional_min = std::max(parameters_.min_remove, static_cast<int>(std::ceil(0.05 * assigned_count)));
    const int proportional_max = std::max(proportional_min, static_cast<int>(std::ceil(0.15 * assigned_count)));

    const int upper = std::min(std::max(parameters_.max_remove, proportional_max), assigned_count);
    const int lower = std::min(proportional_min, upper);
    std::uniform_int_distribution<int> distribution(lower, upper);
    return distribution(rng_);
}

// Operador 1: remove alocacoes escolhidas aleatoriamente.
std::vector<ALNSUM::Demand> ALNSUM::destroyRandomAssignments(Solution& solution, int remove_count) {
    std::vector<Demand> assigned = getAssignedDemands(solution);
    std::shuffle(assigned.begin(), assigned.end(), rng_);

    std::vector<Demand> removed;
    for (int index = 0; index < remove_count && index < static_cast<int>(assigned.size()); ++index) {
        removed.push_back(assigned[index]);
        unassignDemand(solution, assigned[index].item_id, assigned[index].client_id);
    }

    evaluateSolution(solution);
    return removed;
}

// Operador 2: remove demandas de veiculos "ruins", priorizando os de maior custo local.
std::vector<ALNSUM::Demand> ALNSUM::destroyWorstVehicle(Solution& solution, int remove_count) {
    const auto& veiculos = instance_.getVeiculos();

    int worst_vehicle = -1;
    double worst_score = -1.0;

    for (std::size_t vehicle_id = 0; vehicle_id < veiculos.size(); ++vehicle_id) {
        if (solution.vehicle_num_assignments[vehicle_id] <= 0) {
            continue;
        }

        // Score alto indica veiculo caro com baixa utilizacao de carga.
        const double dead_freight = std::max(
            0.0,
            veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id])
        );
        const double score = veiculos[vehicle_id].custo + dead_freight;

        if (score > worst_score) {
            worst_score = score;
            worst_vehicle = static_cast<int>(vehicle_id);
        }
    }

    std::vector<Demand> candidates;
    if (worst_vehicle >= 0) {
        for (const Demand& demand : demands_) {
            if (solution.assigned_vehicle[demand.item_id][demand.client_id] == worst_vehicle) {
                candidates.push_back(demand);
            }
        }
    }

    std::shuffle(candidates.begin(), candidates.end(), rng_);

    std::vector<Demand> removed;
    for (int index = 0; index < remove_count && index < static_cast<int>(candidates.size()); ++index) {
        removed.push_back(candidates[index]);
        unassignDemand(solution, candidates[index].item_id, candidates[index].client_id);
    }

    evaluateSolution(solution);
    return removed;
}

// Operador 3: remove demandas atendidas de maior penalidade, permitindo reinsercoes melhores.
std::vector<ALNSUM::Demand> ALNSUM::destroyHighPenaltyAssignments(Solution& solution, int remove_count) {
    const auto& itens = instance_.getItens();

    std::vector<Demand> assigned = getAssignedDemands(solution);
    std::sort(
        assigned.begin(),
        assigned.end(),
        [&itens](const Demand& lhs, const Demand& rhs) {
            const double lhs_cost = itens[lhs.item_id].peso * itens[lhs.item_id].penalidade;
            const double rhs_cost = itens[rhs.item_id].peso * itens[rhs.item_id].penalidade;
            return lhs_cost > rhs_cost;
        }
    );

    std::vector<Demand> removed;
    for (int index = 0; index < remove_count && index < static_cast<int>(assigned.size()); ++index) {
        removed.push_back(assigned[index]);
        unassignDemand(solution, assigned[index].item_id, assigned[index].client_id);
    }

    evaluateSolution(solution);
    return removed;
}

// Operador 4: mira diretamente o veiculo com maior frete morto absoluto.
// A intuicao e atacar a fonte de custo mais clara na funcao objetivo,
// em vez de usar apenas a ocupacao relativa como sinal.
std::vector<ALNSUM::Demand> ALNSUM::destroyUnderfilledVehicles(Solution& solution, int remove_count) {
    const auto& veiculos = instance_.getVeiculos();

    int selected_vehicle = -1;
    double highest_dead_freight = -1.0;
    double tie_break_cost = -1.0;

    for (std::size_t vehicle_id = 0; vehicle_id < veiculos.size(); ++vehicle_id) {
        if (solution.vehicle_num_assignments[vehicle_id] <= 0) {
            continue;
        }

        const double dead_freight = std::max(
            0.0,
            veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id])
        );

        if (dead_freight > highest_dead_freight + 1e-9 ||
            (std::abs(dead_freight - highest_dead_freight) <= 1e-9 && veiculos[vehicle_id].custo > tie_break_cost)) {
            highest_dead_freight = dead_freight;
            tie_break_cost = static_cast<double>(veiculos[vehicle_id].custo);
            selected_vehicle = static_cast<int>(vehicle_id);
        }
    }

    std::vector<Demand> candidates;
    if (selected_vehicle >= 0) {
        for (const Demand& demand : demands_) {
            if (solution.assigned_vehicle[demand.item_id][demand.client_id] == selected_vehicle) {
                candidates.push_back(demand);
            }
        }
    }

    std::shuffle(candidates.begin(), candidates.end(), rng_);

    std::vector<Demand> removed;
    for (int index = 0; index < remove_count && index < static_cast<int>(candidates.size()); ++index) {
        removed.push_back(candidates[index]);
        unassignDemand(solution, candidates[index].item_id, candidates[index].client_id);
    }

    evaluateSolution(solution);
    return removed;
}

// Constroi o pool de demandas para a fase de reparacao.
// O conjunto e formado por:
// 1. demandas explicitamente removidas na etapa de destruicao
// 2. demandas ainda nao atendidas, priorizadas por penalidade
// Isso corrige a principal fraqueza da versao anterior, que quase nunca tentava
// reincorporar demandas que ja haviam ficado de fora da solucao inicial.
std::vector<ALNSUM::Demand> ALNSUM::buildRepairCandidatePool(
    const Solution& solution,
    const std::vector<Demand>& removed_demands
) const {
    const auto& metadata = instance_.getMetadata();
    std::vector<Demand> candidate_pool;
    candidate_pool.reserve(removed_demands.size() * 2);
    std::vector<std::vector<bool>> seen(
        metadata.num_itens,
        std::vector<bool>(metadata.num_clientes, false)
    );

    for (const Demand& demand : removed_demands) {
        if (!seen[demand.item_id][demand.client_id]) {
            candidate_pool.push_back(demand);
            seen[demand.item_id][demand.client_id] = true;
        }
    }

    std::vector<Demand> unassigned_demands = getUnassignedDemands(solution);

    std::sort(
        unassigned_demands.begin(),
        unassigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
        }
    );

    // Adicionamos ao pool de reparacao um subconjunto das demandas nao atendidas.
    // O tamanho do subconjunto e amarrado ao volume da destruicao para manter o reparo focado,
    // mas agora com capacidade real de atacar a penalidade.
    const int extra_candidates = std::min(
        static_cast<int>(unassigned_demands.size()),
        std::max(10, static_cast<int>(removed_demands.size()))
    );

    for (int index = 0; index < extra_candidates; ++index) {
        const Demand& demand = unassigned_demands[index];
        if (!seen[demand.item_id][demand.client_id]) {
            candidate_pool.push_back(demand);
            seen[demand.item_id][demand.client_id] = true;
        }
    }

    return candidate_pool;
}

// Reparacao 1: para cada demanda removida, tenta a melhor insercao possivel.
void ALNSUM::repairGreedyBestInsertion(Solution& solution, std::vector<Demand>& removed_demands) {
    for (const Demand& demand : removed_demands) {
        if (solution.assigned_vehicle[demand.item_id][demand.client_id] >= 0) {
            continue;
        }

        const std::vector<int> promising_vehicles =
            getPromisingVehiclesForDemand(solution, demand.item_id, demand.client_id, 4);

        int best_vehicle = -1;
        double best_delta = std::numeric_limits<double>::infinity();
        for (int vehicle_id : promising_vehicles) {
            const double delta = computeAssignmentDelta(solution, demand.item_id, demand.client_id, vehicle_id);
            if (delta < best_delta) {
                best_delta = delta;
                best_vehicle = vehicle_id;
            }
        }

        if (best_vehicle >= 0) {
            assignDemandToVehicle(solution, demand.item_id, demand.client_id, best_vehicle);
            solution.objective += best_delta;
        }
    }
}

// Reparacao 2: mesma ideia gulosa, mas com ordem aleatorizada para gerar diversidade.
void ALNSUM::repairGreedyRandomOrder(Solution& solution, std::vector<Demand>& removed_demands) {
    std::shuffle(removed_demands.begin(), removed_demands.end(), rng_);
    repairGreedyBestInsertion(solution, removed_demands);
}

// Reparacao 3: usa uma logica de "regret", priorizando demandas cuja segunda melhor opcao
// e muito pior do que a melhor opcao.
void ALNSUM::repairRegretInsertion(Solution& solution, std::vector<Demand>& removed_demands) {
    while (!removed_demands.empty()) {
        int best_index = -1;
        int best_vehicle = -1;
        double best_regret = -1.0;
        double best_delta_for_selected_demand = std::numeric_limits<double>::infinity();

        for (int index = 0; index < static_cast<int>(removed_demands.size()); ++index) {
            const Demand& demand = removed_demands[index];
            std::vector<double> candidate_values;
            std::vector<int> candidate_vehicles;

            if (solution.assigned_vehicle[demand.item_id][demand.client_id] >= 0) {
                continue;
            }

            const std::vector<int> promising_vehicles =
                getPromisingVehiclesForDemand(solution, demand.item_id, demand.client_id, 4);

            for (int vehicle_id : promising_vehicles) {
                const double delta = computeAssignmentDelta(solution, demand.item_id, demand.client_id, vehicle_id);
                if (!std::isfinite(delta)) {
                    continue;
                }

                candidate_values.push_back(solution.objective + delta);
                candidate_vehicles.push_back(vehicle_id);
            }

            if (candidate_values.empty()) {
                continue;
            }

            std::vector<std::size_t> order(candidate_values.size());
            std::iota(order.begin(), order.end(), 0U);
            std::sort(
                order.begin(),
                order.end(),
                [&candidate_values](std::size_t lhs, std::size_t rhs) {
                    return candidate_values[lhs] < candidate_values[rhs];
                }
            );

            const double best_value = candidate_values[order[0]];
            const double second_value = (order.size() >= 2) ? candidate_values[order[1]] : (best_value + 1.0);
            const double regret = second_value - best_value;

            if (regret > best_regret) {
                best_regret = regret;
                best_index = index;
                best_vehicle = candidate_vehicles[order[0]];
                best_delta_for_selected_demand = best_value - solution.objective;
            }
        }

        if (best_index < 0) {
            break;
        }

        assignDemandToVehicle(
            solution,
            removed_demands[best_index].item_id,
            removed_demands[best_index].client_id,
            best_vehicle
        );
        solution.objective += best_delta_for_selected_demand;
        removed_demands.erase(removed_demands.begin() + best_index);
    }
}

// Reparacao 4: insere primeiro as demandas com melhor ganho liquido,
// isto e, maior reducao imediata da funcao objetivo.
void ALNSUM::repairBestNetGainInsertion(Solution& solution, std::vector<Demand>& removed_demands) {
    while (!removed_demands.empty()) {
        int best_index = -1;
        int best_vehicle = -1;
        double best_delta = std::numeric_limits<double>::infinity();

        for (int index = 0; index < static_cast<int>(removed_demands.size()); ++index) {
            const Demand& demand = removed_demands[index];
            if (solution.assigned_vehicle[demand.item_id][demand.client_id] >= 0) {
                continue;
            }

            const std::vector<int> promising_vehicles =
                getPromisingVehiclesForDemand(solution, demand.item_id, demand.client_id, 4);

            for (int vehicle_id : promising_vehicles) {
                const double delta = computeAssignmentDelta(solution, demand.item_id, demand.client_id, vehicle_id);
                if (delta < best_delta) {
                    best_delta = delta;
                    best_index = index;
                    best_vehicle = vehicle_id;
                }
            }
        }

        if (best_index < 0 || best_vehicle < 0 || !std::isfinite(best_delta)) {
            break;
        }

        assignDemandToVehicle(
            solution,
            removed_demands[best_index].item_id,
            removed_demands[best_index].client_id,
            best_vehicle
        );
        solution.objective += best_delta;
        removed_demands.erase(removed_demands.begin() + best_index);
    }
}

// Calcula a variacao de custo de mover uma demanda do veiculo atual para um veiculo alvo.
// Esse delta e calculado incrementalmente para evitar copias caras de solucao durante a busca local.
double ALNSUM::computeRelocationDelta(
    const Solution& solution,
    int item_id,
    int client_id,
    int target_vehicle
) const {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();

    const int current_vehicle = solution.assigned_vehicle[item_id][client_id];
    if (current_vehicle < 0 || current_vehicle == target_vehicle) {
        return std::numeric_limits<double>::infinity();
    }

    if (!psi_allowed_[item_id][target_vehicle] || !gamma_allowed_[target_vehicle][client_id]) {
        return std::numeric_limits<double>::infinity();
    }
    const int item_weight = itens[item_id].peso;
    const double item_volume = itens[item_id].volume;
    const int target_weight_after = solution.vehicle_weight_load[target_vehicle] + item_weight;
    const double target_volume_after = solution.vehicle_volume_load[target_vehicle] + item_volume;

    if (target_weight_after > veiculos[target_vehicle].capacidade_peso) {
        return std::numeric_limits<double>::infinity();
    }
    if (target_volume_after > veiculos[target_vehicle].capacidade_volume + 1e-9) {
        return std::numeric_limits<double>::infinity();
    }

    // Variacao no veiculo de origem.
    const double origin_dead_freight_before = std::max(
        0.0,
        veiculos[current_vehicle].carga_minima - static_cast<double>(solution.vehicle_weight_load[current_vehicle])
    );

    const int origin_weight_after = solution.vehicle_weight_load[current_vehicle] - item_weight;
    const double origin_dead_freight_after = (solution.vehicle_num_assignments[current_vehicle] == 1)
        ? 0.0
        : std::max(0.0, veiculos[current_vehicle].carga_minima - static_cast<double>(origin_weight_after));

    double delta = 0.0;
    if (solution.vehicle_num_assignments[current_vehicle] == 1) {
        delta -= veiculos[current_vehicle].custo;
        delta -= origin_dead_freight_before;
    } else {
        delta += origin_dead_freight_after - origin_dead_freight_before;
    }

    // Variacao no veiculo de destino.
    const bool target_was_unused = (solution.vehicle_num_assignments[target_vehicle] == 0);
    const double target_dead_freight_before = target_was_unused
        ? 0.0
        : std::max(0.0, veiculos[target_vehicle].carga_minima - static_cast<double>(solution.vehicle_weight_load[target_vehicle]));

    const double target_dead_freight_after = std::max(
        0.0,
        veiculos[target_vehicle].carga_minima - static_cast<double>(target_weight_after)
    );

    if (target_was_unused) {
        delta += veiculos[target_vehicle].custo;
        delta += target_dead_freight_after;
    } else {
        delta += target_dead_freight_after - target_dead_freight_before;
    }

    (void)item_volume;
    return delta;
}

// Calcula incrementalmente a variacao de custo de trocar uma demanda servida por uma nao atendida
// no mesmo veiculo. Assim evitamos copiar a solucao inteira a cada tentativa de swap.
double ALNSUM::computeSwapWithUnservedDelta(
    const Solution& solution,
    int served_item_id,
    int served_client_id,
    int unserved_item_id,
    int unserved_client_id
) const {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();

    const int vehicle_id = solution.assigned_vehicle[served_item_id][served_client_id];
    if (vehicle_id < 0) {
        return std::numeric_limits<double>::infinity();
    }
    if (!psi_allowed_[unserved_item_id][vehicle_id] || !gamma_allowed_[vehicle_id][unserved_client_id]) {
        return std::numeric_limits<double>::infinity();
    }

    const int new_weight =
        solution.vehicle_weight_load[vehicle_id] - itens[served_item_id].peso + itens[unserved_item_id].peso;
    const double new_volume =
        solution.vehicle_volume_load[vehicle_id] - itens[served_item_id].volume + itens[unserved_item_id].volume;

    if (new_weight > veiculos[vehicle_id].capacidade_peso || new_weight < 0) {
        return std::numeric_limits<double>::infinity();
    }
    if (new_volume > veiculos[vehicle_id].capacidade_volume + 1e-9 || new_volume < -1e-9) {
        return std::numeric_limits<double>::infinity();
    }

    const double dead_freight_before = std::max(
        0.0,
        veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id])
    );
    const double dead_freight_after = std::max(
        0.0,
        veiculos[vehicle_id].carga_minima - static_cast<double>(new_weight)
    );

    const double penalty_removed = getDemandPenalty(unserved_item_id);
    const double penalty_added = getDemandPenalty(served_item_id);

    return (dead_freight_after - dead_freight_before) + penalty_added - penalty_removed;
}

// Vizinhanca 1 do VND: tenta inserir demandas atualmente nao atendidas.
// Essa vizinhanca ataca diretamente a parcela de penalidade, que hoje e a principal fonte
// de diferenca entre o ALNS e o modelo exato.
bool ALNSUM::localSearchInsertUnservedDemands(Solution& solution) {
    std::vector<Demand> unassigned_demands = getUnassignedDemands(solution);
    if (unassigned_demands.empty()) {
        return false;
    }

    std::sort(
        unassigned_demands.begin(),
        unassigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
        }
    );

    const int max_demands_to_scan = std::min(
        static_cast<int>(unassigned_demands.size()),
        std::max(8, static_cast<int>(std::ceil(0.08 * unassigned_demands.size())))
    );

    for (int demand_index = 0; demand_index < max_demands_to_scan; ++demand_index) {
        const Demand& demand = unassigned_demands[demand_index];

        int best_vehicle = -1;
        double best_delta = -1e-9;

        const std::vector<int> promising_vehicles =
            getPromisingVehiclesForDemand(solution, demand.item_id, demand.client_id, 3);

        for (int vehicle_id : promising_vehicles) {
            const double delta = computeInsertionDelta(solution, demand.item_id, demand.client_id, vehicle_id);
            if (std::isfinite(delta) && delta < best_delta) {
                best_delta = delta;
                best_vehicle = vehicle_id;
            }
        }

        // First-improvement: assim que achamos uma insercao claramente favoravel, aplicamos.
        if (best_vehicle >= 0) {
            assignDemandToVehicle(solution, demand.item_id, demand.client_id, best_vehicle);
            solution.objective += best_delta;
            return true;
        }
    }

    return false;
}

// Vizinhanca 2 do VND: tenta realocar demandas entre veiculos com uma estrategia first-improvement.
// Para ganhar velocidade, a busca avalia apenas um subconjunto promissor das demandas atendidas.
bool ALNSUM::localSearchRelocateDemands(Solution& solution) {
    std::vector<Demand> assigned_demands = getAssignedDemands(solution);
    if (assigned_demands.empty()) {
        return false;
    }

    // Priorizamos demandas mais caras, porque elas tendem a gerar movimentos de maior impacto.
    std::sort(
        assigned_demands.begin(),
        assigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
        }
    );

    const int max_demands_to_scan = std::min(
        static_cast<int>(assigned_demands.size()),
        std::max(10, static_cast<int>(std::ceil(0.10 * assigned_demands.size())))
    );

    for (int demand_index = 0; demand_index < max_demands_to_scan; ++demand_index) {
        const Demand& demand = assigned_demands[demand_index];
        const int current_vehicle = solution.assigned_vehicle[demand.item_id][demand.client_id];

        int best_target_vehicle = -1;
        double best_delta = -1e-9;

        for (int target_vehicle = 0; target_vehicle < instance_.getMetadata().num_veiculos; ++target_vehicle) {
            if (target_vehicle == current_vehicle) {
                continue;
            }

            const double delta = computeRelocationDelta(solution, demand.item_id, demand.client_id, target_vehicle);
            if (std::isfinite(delta) && delta < best_delta) {
                best_delta = delta;
                best_target_vehicle = target_vehicle;
            }
        }

        // First-improvement: aplicamos imediatamente a primeira melhora razoavel encontrada.
        if (best_target_vehicle >= 0) {
            unassignDemand(solution, demand.item_id, demand.client_id);
            assignDemandToVehicle(solution, demand.item_id, demand.client_id, best_target_vehicle);
            solution.objective += best_delta;
            return true;
        }
    }

    return false;
}

// Vizinhanca 3 do VND: tenta fechar um veiculo ruim e redistribuir todas as suas demandas.
// Esse movimento coordenado e importante porque muitos ganhos de custo fixo/frete morto
// nao aparecem quando movemos apenas uma demanda por vez.
bool ALNSUM::localSearchCloseUnderfilledVehicle(Solution& solution) {
    const auto& veiculos = instance_.getVeiculos();

    struct VehicleCandidate {
        int vehicle_id = -1;
        double score = 0.0;
    };

    std::vector<VehicleCandidate> candidates;
    for (std::size_t vehicle_id = 0; vehicle_id < veiculos.size(); ++vehicle_id) {
        if (solution.vehicle_num_assignments[vehicle_id] <= 0) {
            continue;
        }

        const double dead_freight = std::max(
            0.0,
            veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id])
        );
        const double score =
            (static_cast<double>(veiculos[vehicle_id].custo) + dead_freight) /
            std::max(1, solution.vehicle_num_assignments[vehicle_id]);

        candidates.push_back({static_cast<int>(vehicle_id), score});
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const VehicleCandidate& lhs, const VehicleCandidate& rhs) {
            return lhs.score > rhs.score;
        }
    );

    const int max_vehicles_to_scan = std::min(static_cast<int>(candidates.size()), 6);
    for (int candidate_index = 0; candidate_index < max_vehicles_to_scan; ++candidate_index) {
        const int source_vehicle = candidates[candidate_index].vehicle_id;

        std::vector<Demand> source_demands;
        for (const Demand& demand : demands_) {
            if (solution.assigned_vehicle[demand.item_id][demand.client_id] == source_vehicle) {
                source_demands.push_back(demand);
            }
        }

        if (source_demands.empty() || source_demands.size() > 40U) {
            continue;
        }

        std::sort(
            source_demands.begin(),
            source_demands.end(),
            [this](const Demand& lhs, const Demand& rhs) {
                return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
            }
        );

        Solution trial = solution;
        for (const Demand& demand : source_demands) {
            unassignDemand(trial, demand.item_id, demand.client_id);
        }

        bool all_reassigned = true;
        for (const Demand& demand : source_demands) {
            int best_vehicle = -1;
            double best_delta = std::numeric_limits<double>::infinity();

            for (int target_vehicle = 0; target_vehicle < instance_.getMetadata().num_veiculos; ++target_vehicle) {
                if (target_vehicle == source_vehicle) {
                    continue;
                }

                const double delta = computeAssignmentDelta(
                    trial,
                    demand.item_id,
                    demand.client_id,
                    target_vehicle
                );
                if (std::isfinite(delta) && delta < best_delta) {
                    best_delta = delta;
                    best_vehicle = target_vehicle;
                }
            }

            if (best_vehicle < 0) {
                all_reassigned = false;
                break;
            }

            assignDemandToVehicle(trial, demand.item_id, demand.client_id, best_vehicle);
            trial.objective += best_delta;
        }

        if (!all_reassigned) {
            continue;
        }

        evaluateSolution(trial);
        if (trial.objective + 1e-9 < solution.objective) {
            solution = trial;
            return true;
        }
    }

    return false;
}

// Vizinhanca 3 do VND: tenta trocar uma demanda atendida por uma nao atendida.
// O objetivo e abrir espaco para demandas de penalidade maior quando a capacidade ja esta saturada.
bool ALNSUM::localSearchSwapServedAndUnserved(Solution& solution) {
    std::vector<Demand> unassigned_demands = getUnassignedDemands(solution);
    std::vector<Demand> assigned_demands = getAssignedDemands(solution);

    if (unassigned_demands.empty() || assigned_demands.empty()) {
        return false;
    }

    std::sort(
        unassigned_demands.begin(),
        unassigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
        }
    );

    std::sort(
        assigned_demands.begin(),
        assigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) < getDemandPenalty(rhs.item_id);
        }
    );

    const int max_unassigned_to_scan = std::min(static_cast<int>(unassigned_demands.size()), 6);
    const int max_assigned_to_scan = std::min(static_cast<int>(assigned_demands.size()), 8);

    for (int unassigned_index = 0; unassigned_index < max_unassigned_to_scan; ++unassigned_index) {
        const Demand& outside_demand = unassigned_demands[unassigned_index];
        const double outside_penalty = getDemandPenalty(outside_demand.item_id);

        for (int assigned_index = 0; assigned_index < max_assigned_to_scan; ++assigned_index) {
            const Demand& inside_demand = assigned_demands[assigned_index];
            const double inside_penalty = getDemandPenalty(inside_demand.item_id);

            // So vale testar troca se a demanda de fora for claramente mais valiosa.
            if (outside_penalty <= inside_penalty + 1e-9) {
                continue;
            }

            const double delta = computeSwapWithUnservedDelta(
                solution,
                inside_demand.item_id,
                inside_demand.client_id,
                outside_demand.item_id,
                outside_demand.client_id
            );

            if (std::isfinite(delta) && delta < -1e-9) {
                const int current_vehicle = solution.assigned_vehicle[inside_demand.item_id][inside_demand.client_id];
                unassignDemand(solution, inside_demand.item_id, inside_demand.client_id);
                assignDemandToVehicle(solution, outside_demand.item_id, outside_demand.client_id, current_vehicle);
                solution.objective += delta;
                return true;
            }
        }
    }

    return false;
}

// Vizinhanca 5 do VND: troca duas demandas ja atendidas entre seus veiculos.
// Ela nao muda a quantidade atendida, mas pode melhorar a distribuicao de peso/volume
// e preparar fechamentos de veiculos em chamadas posteriores do VND.
bool ALNSUM::localSearchSwapServedDemands(Solution& solution) {
    std::vector<Demand> assigned_demands = getAssignedDemands(solution);
    if (assigned_demands.size() < 2U) {
        return false;
    }

    std::sort(
        assigned_demands.begin(),
        assigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
        }
    );

    const int max_demands_to_scan = std::min(static_cast<int>(assigned_demands.size()), 28);
    for (int first_index = 0; first_index < max_demands_to_scan; ++first_index) {
        const Demand& first = assigned_demands[first_index];
        const int first_vehicle = solution.assigned_vehicle[first.item_id][first.client_id];

        for (int second_index = first_index + 1; second_index < max_demands_to_scan; ++second_index) {
            const Demand& second = assigned_demands[second_index];
            const int second_vehicle = solution.assigned_vehicle[second.item_id][second.client_id];

            if (first_vehicle == second_vehicle) {
                continue;
            }

            Solution trial = solution;
            unassignDemand(trial, first.item_id, first.client_id);
            unassignDemand(trial, second.item_id, second.client_id);

            if (!canAssignDemandToVehicle(trial, first.item_id, first.client_id, second_vehicle)) {
                continue;
            }
            assignDemandToVehicle(trial, first.item_id, first.client_id, second_vehicle);

            if (!canAssignDemandToVehicle(trial, second.item_id, second.client_id, first_vehicle)) {
                continue;
            }
            assignDemandToVehicle(trial, second.item_id, second.client_id, first_vehicle);

            evaluateSolution(trial);
            if (trial.objective + 1e-9 < solution.objective) {
                solution = trial;
                return true;
            }
        }
    }

    return false;
}

// Vizinhanca 6 do VND: tenta inserir uma demanda nao atendida removendo uma ou duas
// demandas menos vantajosas do veiculo escolhido. Isso e mais forte do que o swap 1-por-1
// quando uma demanda valiosa precisa liberar capacidade combinada.
bool ALNSUM::localSearchEjectForUnservedDemand(Solution& solution) {
    std::vector<Demand> unassigned_demands = getUnassignedDemands(solution);
    if (unassigned_demands.empty()) {
        return false;
    }

    std::sort(
        unassigned_demands.begin(),
        unassigned_demands.end(),
        [this](const Demand& lhs, const Demand& rhs) {
            return getDemandPenalty(lhs.item_id) > getDemandPenalty(rhs.item_id);
        }
    );

    const int max_unassigned_to_scan = std::min(static_cast<int>(unassigned_demands.size()), 8);
    for (int unassigned_index = 0; unassigned_index < max_unassigned_to_scan; ++unassigned_index) {
        const Demand& outside = unassigned_demands[unassigned_index];

        for (int vehicle_id = 0; vehicle_id < instance_.getMetadata().num_veiculos; ++vehicle_id) {
            if (!psi_allowed_[outside.item_id][vehicle_id] || !gamma_allowed_[vehicle_id][outside.client_id]) {
                continue;
            }

            std::vector<Demand> inside_demands;
            for (const Demand& demand : demands_) {
                if (solution.assigned_vehicle[demand.item_id][demand.client_id] == vehicle_id) {
                    inside_demands.push_back(demand);
                }
            }

            if (inside_demands.empty()) {
                continue;
            }

            std::sort(
                inside_demands.begin(),
                inside_demands.end(),
                [this](const Demand& lhs, const Demand& rhs) {
                    return getDemandPenalty(lhs.item_id) < getDemandPenalty(rhs.item_id);
                }
            );

            const int max_inside_to_scan = std::min(static_cast<int>(inside_demands.size()), 8);

            for (int first_inside = 0; first_inside < max_inside_to_scan; ++first_inside) {
                Solution trial = solution;
                unassignDemand(trial, inside_demands[first_inside].item_id, inside_demands[first_inside].client_id);

                if (canAssignDemandToVehicle(trial, outside.item_id, outside.client_id, vehicle_id)) {
                    assignDemandToVehicle(trial, outside.item_id, outside.client_id, vehicle_id);
                    evaluateSolution(trial);
                    if (trial.objective + 1e-9 < solution.objective) {
                        solution = trial;
                        return true;
                    }
                }

                for (int second_inside = first_inside + 1; second_inside < max_inside_to_scan; ++second_inside) {
                    Solution pair_trial = trial;
                    unassignDemand(
                        pair_trial,
                        inside_demands[second_inside].item_id,
                        inside_demands[second_inside].client_id
                    );

                    if (!canAssignDemandToVehicle(pair_trial, outside.item_id, outside.client_id, vehicle_id)) {
                        continue;
                    }

                    assignDemandToVehicle(pair_trial, outside.item_id, outside.client_id, vehicle_id);
                    evaluateSolution(pair_trial);
                    if (pair_trial.objective + 1e-9 < solution.objective) {
                        solution = pair_trial;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

// Orquestra a busca local via VND leve.
// Ordem das vizinhancas:
// 1. inserir demanda nao atendida
// 2. realocar demanda atendida
// 3. fechar veiculo subutilizado
// 4. trocar demanda atendida por nao atendida
// 5. trocar duas demandas atendidas entre veiculos
// 6. inserir demanda nao atendida com ejecao de demandas menos valiosas
// Sempre que ha melhora, o VND volta para a primeira vizinhanca.
void ALNSUM::applyLocalSearch(Solution& solution) {
    int neighborhood_index = 0;
    const int max_neighborhoods = 6;
    const int max_total_improvements = 7;
    int improvements_applied = 0;

    while (neighborhood_index < max_neighborhoods && improvements_applied < max_total_improvements) {
        bool improved = false;

        if (neighborhood_index == 0) {
            improved = localSearchInsertUnservedDemands(solution);
        } else if (neighborhood_index == 1) {
            improved = localSearchRelocateDemands(solution);
        } else if (neighborhood_index == 2) {
            improved = localSearchCloseUnderfilledVehicle(solution);
        } else if (neighborhood_index == 3) {
            improved = localSearchSwapServedAndUnserved(solution);
        } else if (neighborhood_index == 4) {
            improved = localSearchSwapServedDemands(solution);
        } else {
            improved = localSearchEjectForUnservedDemand(solution);
        }

        if (improved) {
            neighborhood_index = 0;
            ++improvements_applied;
        } else {
            ++neighborhood_index;
        }
    }
}

// Seleciona um operador por roleta proporcional ao peso adaptativo.
int ALNSUM::selectOperatorIndex(const std::vector<OperatorStats>& operators) {
    double total_weight = 0.0;
    for (const OperatorStats& op : operators) {
        total_weight += op.weight;
    }

    if (total_weight <= 1e-12) {
        return 0;
    }

    std::uniform_real_distribution<double> distribution(0.0, total_weight);
    const double draw = distribution(rng_);

    double cumulative = 0.0;
    for (int index = 0; index < static_cast<int>(operators.size()); ++index) {
        cumulative += operators[index].weight;
        if (draw <= cumulative) {
            return index;
        }
    }

    return static_cast<int>(operators.size()) - 1;
}

// Regra de aceitacao estilo simulated annealing.
bool ALNSUM::acceptCandidate(double candidate_objective, double current_objective, double temperature) {
    if (candidate_objective <= current_objective) {
        return true;
    }

    const double delta = candidate_objective - current_objective;
    const double probability = std::exp(-delta / std::max(temperature, 1e-9));
    std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(rng_) <= probability;
}

// Acumula recompensa de um operador dentro do segmento adaptativo atual.
void ALNSUM::updateOperatorScore(OperatorStats& operator_stats, double reward) {
    operator_stats.score += reward;
    operator_stats.uses += 1;
}

// Atualiza os pesos ao final de cada segmento, privilegiando operadores mais eficazes.
void ALNSUM::updateAdaptiveWeights(std::vector<OperatorStats>& operators) {
    double total_weight = 0.0;

    for (OperatorStats& op : operators) {
        // Atualizacao classica do ALNS por segmentos:
        // pi_(i,j+1) = (1 - r) * pi_(i,j) + r * (p_i / theta_i)
        //
        // onde:
        // - pi_(i,j)     = peso do operador i no segmento j
        // - r            = reaction_factor
        // - p_i          = pontuacao acumulada do operador i no segmento atual
        // - theta_i      = quantidade de usos do operador i no segmento atual
        //
        // O reaction_factor controla quanto o novo peso herda do passado e quanto reage
        // ao desempenho recente:
        // - r = 0 -> repete integralmente o peso anterior
        // - r = 1 -> descarta o peso anterior e usa apenas o desempenho do ultimo segmento
        //
        // Quando theta_i = 0, o operador nao foi usado no segmento. Nesse caso, evitamos
        // a divisao invalida e simplesmente preservamos seu peso anterior antes da normalizacao.
        const double previous_weight = op.weight;
        double updated_weight = previous_weight;

        if (op.uses > 0) {
            const double average_segment_score = op.score / static_cast<double>(op.uses);
            updated_weight =
                (1.0 - parameters_.reaction_factor) * previous_weight +
                parameters_.reaction_factor * average_segment_score;
        }

        op.weight = updated_weight;
        op.weight = std::max(1e-6, op.weight);
        total_weight += op.weight;
        op.score = 0.0;
        op.uses = 0;
    }

    // Normalizamos os pesos para que eles passem a representar probabilidades da roleta,
    // isto e, a soma final do vetor de pesos fica igual a 1.
    if (total_weight > 1e-12) {
        double adjusted_total_weight = 0.0;
        for (const OperatorStats& op : operators) {
            adjusted_total_weight += op.weight;
        }

        if (adjusted_total_weight > 1e-12) {
            for (OperatorStats& op : operators) {
                op.weight /= adjusted_total_weight;
            }
        }
    }
}

// Inicializa a memoria do Clustering Search.
// Os centros ainda nao recebem solucoes neste momento; eles serao preenchidos sob demanda
// pelas primeiras solucoes aceitas pelo ALNS.
void ALNSUM::initializeClusters() {
    clusters_.assign(std::max(1, parameters_.num_clusters), Cluster{});
}

// Mede a distancia entre duas solucoes usando apenas o padrao de veiculos ativos.
// Essa escolha e deliberada: nas instancias em que todas as demandas sao atendidas e
// o frete morto zera, a qualidade passa a depender fortemente da frota ativada.
int ALNSUM::computeVehiclePatternDistance(const Solution& lhs, const Solution& rhs) const {
    int distance = 0;
    const int num_vehicles = instance_.getMetadata().num_veiculos;

    for (int vehicle_id = 0; vehicle_id < num_vehicles; ++vehicle_id) {
        const bool lhs_used = lhs.vehicle_num_assignments[vehicle_id] > 0;
        const bool rhs_used = rhs.vehicle_num_assignments[vehicle_id] > 0;
        if (lhs_used != rhs_used) {
            ++distance;
        }
    }

    return distance;
}

// Encontra o cluster que melhor representa a solucao recebida.
// Se ainda existir cluster vazio, ele e usado antes de agrupar a solucao em centros ja formados,
// garantindo diversidade inicial entre as regioes monitoradas pelo CS.
int ALNSUM::findNearestClusterIndex(const Solution& solution) const {
    int first_empty_cluster = -1;
    int best_cluster = -1;
    int best_distance = std::numeric_limits<int>::max();

    for (int cluster_index = 0; cluster_index < static_cast<int>(clusters_.size()); ++cluster_index) {
        const Cluster& cluster = clusters_[cluster_index];
        if (!cluster.initialized) {
            if (first_empty_cluster < 0) {
                first_empty_cluster = cluster_index;
            }
            continue;
        }

        const int distance = computeVehiclePatternDistance(solution, cluster.center);
        if (distance < best_distance ||
            (distance == best_distance && solution.objective < clusters_[best_cluster].center.objective)) {
            best_distance = distance;
            best_cluster = cluster_index;
        }
    }

    return (first_empty_cluster >= 0) ? first_empty_cluster : best_cluster;
}

// Envia uma solucao aceita pelo ALNS para a memoria do Clustering Search.
// A solucao aumenta o volume do cluster mais parecido; quando esse volume passa o limiar,
// o centro do cluster so e intensificado se estiver suficientemente perto do melhor global.
bool ALNSUM::processSolutionInClusteringSearch(Solution& solution, Solution& best) {
    if (!parameters_.enable_clustering_search || clusters_.empty()) {
        return false;
    }

    // Toda solucao aceita alimenta o pool de elite, base da mineracao de padroes
    // usada para fixar variaveis nas chamadas de Local Branching.
    updateElitePool(solution);

    const int cluster_index = findNearestClusterIndex(solution);
    if (cluster_index < 0) {
        return false;
    }

    Cluster& cluster = clusters_[cluster_index];

    // Primeiro contato com uma regiao: a propria solucao vira o centro inicial.
    if (!cluster.initialized) {
        cluster.center = solution;
        cluster.initialized = true;
        cluster.volume = 1;
        cluster.inefficacy = 0;
        return false;
    }

    // O centro e sempre a melhor solucao conhecida daquela regiao, nao uma media.
    // Para solucoes combinatorias isso evita criar centros inviaveis.
    ++cluster.volume;
    if (solution.objective + 1e-9 < cluster.center.objective) {
        cluster.center = solution;
        cluster.inefficacy = 0;
    }

    if (cluster.volume < std::max(1, parameters_.cluster_activation_volume)) {
        return false;
    }

    cluster.volume = 0;
    // O filtro de promissor foi benefico nas instancias maiores, mas nas menores
    // removeu chamadas uteis ou aumentou tempo sem ganho. Por isso, aplicamos a
    // tolerancia apenas quando o numero de demandas elementares passa o limiar.
    const bool use_promising_filter =
        static_cast<int>(demands_.size()) >= std::max(1, parameters_.large_instance_demand_threshold);
    const double tolerance = use_promising_filter
        ? std::max(0.0, parameters_.cluster_intensification_tolerance)
        : std::numeric_limits<double>::infinity();
    const double promising_threshold = best.objective * (1.0 + tolerance);
    if (cluster.center.objective > promising_threshold + 1e-9) {
        return false;
    }

    return intensifyClusterWithLocalBranching(cluster, best);
}

// Aplica a intensificacao matematica sobre o centro do cluster.
// Se o Local Branching melhorar o centro, a melhoria tambem e propagada para a melhor
// solucao global; caso contrario, aumentamos a ineficacia da regiao.
bool ALNSUM::intensifyClusterWithLocalBranching(Cluster& cluster, Solution& best) {
    ++cluster.local_branching_calls;
    ++cs_local_branching_calls_;

    Solution improved;
    int mined_variables_fixed = 0;
    const auto local_branching_start = std::chrono::steady_clock::now();
    const bool local_branching_found_solution = solveLocalBranchingAroundCenter(
        cluster.center,
        improved,
        parameters_.local_branching_radius,
        parameters_.demand_local_branching_radius,
        parameters_.local_branching_time_limit,
        &mined_variables_fixed
    );
    const auto local_branching_end = std::chrono::steady_clock::now();
    cs_local_branching_seconds_ +=
        std::chrono::duration<double>(local_branching_end - local_branching_start).count();

    if (mined_variables_fixed > 0) {
        ++cs_pattern_mining_calls_;
        cs_pattern_mining_variables_fixed_ += mined_variables_fixed;
    }

    if (!local_branching_found_solution) {
        ++cluster.inefficacy;
    } else if (improved.objective + 1e-9 < cluster.center.objective) {
        cluster.center = improved;
        cluster.inefficacy = 0;
        ++cluster.local_branching_improvements;
        ++cs_local_branching_improvements_;

        if (improved.objective + 1e-9 < best.objective) {
            best = improved;
        }
        return true;
    } else {
        ++cluster.inefficacy;
    }

    // Se o centro ja foi explorado varias vezes sem ganho, abrimos espaco para que
    // a proxima solucao aceita reinicialize essa regiao com outro padrao de frota.
    if (cluster.inefficacy >= std::max(1, parameters_.cluster_max_inefficacy)) {
        cluster.initialized = false;
        cluster.volume = 0;
        cluster.inefficacy = 0;
    }

    return false;
}

// Atualiza o pool de solucoes de elite usado pela mineracao de padroes.
// O pool guarda ate pattern_mining_pool_size solucoes distintas (por objetivo), sempre
// substituindo a pior quando uma solucao melhor aparece. Isso mantem uma amostra das
// regioes mais promissoras ja visitadas pelo ALNS, sem se limitar a um unico centro.
void ALNSUM::updateElitePool(const Solution& solution) {
    if (!parameters_.enable_pattern_mining) {
        return;
    }

    for (const Solution& existing : elite_pool_) {
        if (std::abs(existing.objective - solution.objective) < 1e-6) {
            return;
        }
    }

    const int max_pool_size = std::max(1, parameters_.pattern_mining_pool_size);
    if (static_cast<int>(elite_pool_.size()) < max_pool_size) {
        elite_pool_.push_back(solution);
        return;
    }

    auto worst_it = std::max_element(
        elite_pool_.begin(),
        elite_pool_.end(),
        [](const Solution& lhs, const Solution& rhs) { return lhs.objective < rhs.objective; }
    );
    if (worst_it != elite_pool_.end() && solution.objective + 1e-9 < worst_it->objective) {
        *worst_it = solution;
    }
}

// Mineracao de padroes frequentes sobre o pool de elite (Data Mining aplicado ao LB).
// Para cada demanda (item,cliente), conta por votacao qual veiculo a atende (ou se ela
// fica sem atendimento) em cada solucao do pool. Quando o suporte de um padrao supera
// o limiar configurado, a variavel correspondente e fixada por bound, reduzindo o
// tamanho do MIP resolvido pelo Gurobi e liberando tempo/raio para o restante do espaco.
int ALNSUM::applyMinedVariableFixings(
    std::vector<GRBVar>& y,
    std::vector<std::vector<std::vector<GRBVar>>>& x,
    const std::vector<std::vector<std::vector<bool>>>& x_exists
) const {
    const int pool_size = static_cast<int>(elite_pool_.size());
    if (!parameters_.enable_pattern_mining ||
        pool_size < std::max(1, parameters_.pattern_mining_min_pool_size)) {
        return 0;
    }

    const double support_threshold =
        std::min(1.0, std::max(0.5, parameters_.pattern_mining_support_threshold));
    const auto& metadata = instance_.getMetadata();
    const auto& psi = instance_.getPsi();

    int fixed_count = 0;
    std::vector<bool> vehicle_forced_active(metadata.num_veiculos, false);

    // Fixa por demanda o veiculo majoritario (ou o nao atendimento) quando o suporte
    // no pool de elite for alto o suficiente.
    for (const Demand& demand : demands_) {
        std::unordered_map<int, int> votes;
        int unserved_votes = 0;
        for (const Solution& elite : elite_pool_) {
            const int assigned = elite.assigned_vehicle[demand.item_id][demand.client_id];
            if (assigned >= 0) {
                ++votes[assigned];
            } else if (assigned == -1) {
                ++unserved_votes;
            }
        }

        int majority_vehicle = -1;
        int majority_votes = 0;
        for (const auto& vote : votes) {
            if (vote.second > majority_votes) {
                majority_votes = vote.second;
                majority_vehicle = vote.first;
            }
        }

        if (majority_vehicle >= 0 &&
            x_exists[demand.item_id][majority_vehicle][demand.client_id] &&
            static_cast<double>(majority_votes) / pool_size >= support_threshold) {
            x[demand.item_id][majority_vehicle][demand.client_id].set(GRB_DoubleAttr_LB, 1.0);
            x[demand.item_id][majority_vehicle][demand.client_id].set(GRB_DoubleAttr_UB, 1.0);
            vehicle_forced_active[majority_vehicle] = true;
            ++fixed_count;

            for (int vehicle_id : psi[demand.item_id]) {
                if (vehicle_id != majority_vehicle && x_exists[demand.item_id][vehicle_id][demand.client_id]) {
                    x[demand.item_id][vehicle_id][demand.client_id].set(GRB_DoubleAttr_UB, 0.0);
                }
            }
        } else if (static_cast<double>(unserved_votes) / pool_size >= support_threshold) {
            for (int vehicle_id : psi[demand.item_id]) {
                if (x_exists[demand.item_id][vehicle_id][demand.client_id]) {
                    x[demand.item_id][vehicle_id][demand.client_id].set(GRB_DoubleAttr_UB, 0.0);
                }
            }
            ++fixed_count;
        }
    }

    // Fixa veiculos que o pool de elite nunca usa ou sempre usa, reduzindo o espaco de
    // busca sobre y. Veiculos com uma demanda ja forcada a ativa sao preservados para
    // nao gerar um par de fixacoes contraditorio (x=1 exigindo y=1, com y fixado em 0).
    for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
        if (vehicle_forced_active[vehicle_id]) {
            continue;
        }

        int active_votes = 0;
        for (const Solution& elite : elite_pool_) {
            if (elite.vehicle_num_assignments[vehicle_id] > 0) {
                ++active_votes;
            }
        }

        const double active_support = static_cast<double>(active_votes) / pool_size;
        if (active_support <= 1.0 - support_threshold) {
            y[vehicle_id].set(GRB_DoubleAttr_UB, 0.0);
            ++fixed_count;
        } else if (active_support >= support_threshold) {
            y[vehicle_id].set(GRB_DoubleAttr_LB, 1.0);
            ++fixed_count;
        }
    }

    return fixed_count;
}

// Resolve um subproblema de Local Branching ao redor do centro do cluster.
// Se ainda ha veiculos inativos, usamos a vizinhanca de frota:
//   sum_{ativos}(1-y_v) + sum_{inativos} y_v <= k_y.
// Se todos os veiculos ja estao ativos, a frota nao diferencia bem as solucoes; nesse
// caso usamos a vizinhanca de atendimento:
//   sum_{servidos}(1-s_ic) + sum_{nao_servidos}s_ic <= k_s,
// onde s_ic = sum_v x_ivc indica se a demanda (i,c) foi atendida por algum veiculo.
bool ALNSUM::solveLocalBranchingAroundCenter(const Solution& center, Solution& improved_solution) const {
    return solveLocalBranchingAroundCenter(
        center,
        improved_solution,
        parameters_.local_branching_radius,
        parameters_.demand_local_branching_radius,
        parameters_.local_branching_time_limit
    );
}

bool ALNSUM::solveLocalBranchingAroundCenter(
    const Solution& center,
    Solution& improved_solution,
    int vehicle_radius,
    int demand_radius,
    double time_limit,
    int* mined_variables_fixed
) const {
    try {
        const auto& metadata = instance_.getMetadata();
        const auto& veiculos = instance_.getVeiculos();
        const auto& itens = instance_.getItens();
        const auto& psi = instance_.getPsi();
        const auto& phi = instance_.getPhi();
        const auto& omega = instance_.getOmega();
        const auto& gamma = instance_.getGamma();

        GRBEnv env(true);
        env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        GRBModel model(env);
        model.set(GRB_StringAttr_ModelName, "CS_Local_Branching_UM");
        model.set(GRB_DoubleParam_TimeLimit, std::max(0.1, time_limit));

        std::vector<GRBVar> y(metadata.num_veiculos);
        std::vector<GRBVar> z(metadata.num_veiculos);
        std::vector<std::vector<std::vector<GRBVar>>> x(
            metadata.num_itens,
            std::vector<std::vector<GRBVar>>(metadata.num_veiculos, std::vector<GRBVar>(metadata.num_clientes))
        );
        std::vector<std::vector<std::vector<bool>>> x_exists(
            metadata.num_itens,
            std::vector<std::vector<bool>>(metadata.num_veiculos, std::vector<bool>(metadata.num_clientes, false))
        );

        // Variaveis y/z do mesmo papel do modelo P2: ativacao de veiculo e frete morto.
        for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
            y[vehicle_id] = model.addVar(
                0.0,
                1.0,
                0.0,
                GRB_BINARY,
                "lb_y_" + std::to_string(vehicle_id)
            );
            z[vehicle_id] = model.addVar(
                0.0,
                GRB_INFINITY,
                0.0,
                GRB_CONTINUOUS,
                "lb_z_" + std::to_string(vehicle_id)
            );

            y[vehicle_id].set(
                GRB_DoubleAttr_Start,
                center.vehicle_num_assignments[vehicle_id] > 0 ? 1.0 : 0.0
            );
        }

        // Variaveis x somente para triplas compativeis item-veiculo-cliente.
        // Mantemos a mesma representacao elementar do ALNS: cada demanda (i,c) pode
        // ser atendida por no maximo um veiculo.
        for (int item_id = 0; item_id < metadata.num_itens; ++item_id) {
            for (int vehicle_id : psi[item_id]) {
                for (int client_id : phi[item_id]) {
                    if (!gamma_allowed_[vehicle_id][client_id]) {
                        continue;
                    }

                    x[item_id][vehicle_id][client_id] = model.addVar(
                        0.0,
                        1.0,
                        0.0,
                        GRB_BINARY,
                        "lb_x_" + std::to_string(item_id) + "_" +
                            std::to_string(vehicle_id) + "_" + std::to_string(client_id)
                    );
                    x_exists[item_id][vehicle_id][client_id] = true;

                    x[item_id][vehicle_id][client_id].set(
                        GRB_DoubleAttr_Start,
                        center.assigned_vehicle[item_id][client_id] == vehicle_id ? 1.0 : 0.0
                    );
                }
            }
        }

        // Mineracao de padroes: fixa variaveis x/y com alto suporte no pool de elite
        // antes de montar o restante do modelo, reduzindo o MIP resolvido pelo Gurobi.
        const int mined_fixed_count = applyMinedVariableFixings(y, x, x_exists);
        if (mined_variables_fixed != nullptr) {
            *mined_variables_fixed = mined_fixed_count;
        }

        // Funcao objetivo identica a avaliacao do ALNS/P2: custo fixo + frete morto
        // + penalidade das demandas nao atendidas.
        GRBLinExpr objective = 0.0;
        for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
            objective += veiculos[vehicle_id].custo * y[vehicle_id];
            objective += z[vehicle_id];
        }

        for (int item_id = 0; item_id < metadata.num_itens; ++item_id) {
            GRBLinExpr served_item = 0.0;
            for (int vehicle_id : psi[item_id]) {
                for (int client_id : phi[item_id]) {
                    if (x_exists[item_id][vehicle_id][client_id]) {
                        served_item += x[item_id][vehicle_id][client_id];
                    }
                }
            }
            objective += itens[item_id].peso * itens[item_id].penalidade *
                (itens[item_id].demanda_total - served_item);
        }

        model.setObjective(objective, GRB_MINIMIZE);

        // Capacidades e frete morto por veiculo, seguindo a estrutura do modelo P2.
        for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
            GRBLinExpr weight_load = 0.0;
            GRBLinExpr volume_load = 0.0;

            for (int item_id : omega[vehicle_id]) {
                for (int client_id : gamma[vehicle_id]) {
                    if (x_exists[item_id][vehicle_id][client_id]) {
                        weight_load += itens[item_id].peso * x[item_id][vehicle_id][client_id];
                        volume_load += itens[item_id].volume * x[item_id][vehicle_id][client_id];
                    }
                }
            }

            model.addConstr(weight_load <= veiculos[vehicle_id].capacidade_peso);
            model.addConstr(volume_load <= veiculos[vehicle_id].capacidade_volume);
            model.addConstr(z[vehicle_id] >= veiculos[vehicle_id].carga_minima * y[vehicle_id] - weight_load);
        }

        // Atendimento total de cada item, ativacao do veiculo e unicidade por par item-cliente.
        for (int item_id = 0; item_id < metadata.num_itens; ++item_id) {
            GRBLinExpr served_item = 0.0;

            for (int vehicle_id : psi[item_id]) {
                for (int client_id : phi[item_id]) {
                    if (!x_exists[item_id][vehicle_id][client_id]) {
                        continue;
                    }

                    served_item += x[item_id][vehicle_id][client_id];
                    model.addConstr(x[item_id][vehicle_id][client_id] <= y[vehicle_id]);
                }
            }

            model.addConstr(served_item <= itens[item_id].demanda_total);

            for (int client_id : phi[item_id]) {
                GRBLinExpr served_client_item = 0.0;
                for (int vehicle_id : psi[item_id]) {
                    if (x_exists[item_id][vehicle_id][client_id]) {
                        served_client_item += x[item_id][vehicle_id][client_id];
                    }
                }
                model.addConstr(served_client_item <= 1.0);
            }
        }

        // Escolha adaptativa do tipo de vizinhanca:
        // - centros com veiculos livres recebem LB em frota;
        // - centros com toda a frota ativa recebem LB em demandas atendidas.
        int active_vehicle_count = 0;
        for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
            if (center.vehicle_num_assignments[vehicle_id] > 0) {
                ++active_vehicle_count;
            }
        }

        if (active_vehicle_count < metadata.num_veiculos) {
            // Restricao de Local Branching sobre o vetor y do centro do cluster.
            // O raio controla o quanto o solver pode se afastar da frota ativa do centro.
            GRBLinExpr vehicle_pattern_distance = 0.0;
            for (int vehicle_id = 0; vehicle_id < metadata.num_veiculos; ++vehicle_id) {
                if (center.vehicle_num_assignments[vehicle_id] > 0) {
                    vehicle_pattern_distance += 1.0 - y[vehicle_id];
                } else {
                    vehicle_pattern_distance += y[vehicle_id];
                }
            }
            model.addConstr(vehicle_pattern_distance <= std::max(1, vehicle_radius));
        } else {
            // Restricao de Local Branching sobre o atendimento das demandas.
            // Ela permite trocar quais pares item-cliente sao atendidos, mas deixa o Gurobi
            // livre para decidir em qual veiculo colocar cada demanda mantida ou inserida.
            GRBLinExpr service_pattern_distance = 0.0;

            for (const Demand& demand : demands_) {
                GRBLinExpr served_demand = 0.0;
                for (int vehicle_id : psi[demand.item_id]) {
                    if (x_exists[demand.item_id][vehicle_id][demand.client_id]) {
                        served_demand += x[demand.item_id][vehicle_id][demand.client_id];
                    }
                }

                if (center.assigned_vehicle[demand.item_id][demand.client_id] >= 0) {
                    service_pattern_distance += 1.0 - served_demand;
                } else {
                    service_pattern_distance += served_demand;
                }
            }

            model.addConstr(
                service_pattern_distance <= std::max(1, demand_radius)
            );
        }

        model.optimize();

        if (model.get(GRB_IntAttr_SolCount) <= 0) {
            return false;
        }

        Solution extracted = createEmptySolution();
        for (const Demand& demand : demands_) {
            for (int vehicle_id : psi[demand.item_id]) {
                if (!x_exists[demand.item_id][vehicle_id][demand.client_id]) {
                    continue;
                }

                if (x[demand.item_id][vehicle_id][demand.client_id].get(GRB_DoubleAttr_X) > 0.5) {
                    assignDemandToVehicle(extracted, demand.item_id, demand.client_id, vehicle_id);
                    break;
                }
            }
        }

        evaluateSolution(extracted);
        improved_solution = extracted;
        return true;
    } catch (const GRBException&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

// Intensificacao final aplicada uma unica vez sobre a melhor solucao global.
// Ela usa uma vizinhanca maior que as chamadas normais do CS para tentar extrair
// uma ultima melhoria da regiao de elite sem encarecer todas as iteracoes.
bool ALNSUM::applyFinalIntensification(Solution& best) {
    if (!parameters_.enable_final_intensification) {
        return false;
    }

    Solution improved;
    int mined_variables_fixed = 0;
    const bool found_solution = solveLocalBranchingAroundCenter(
        best,
        improved,
        parameters_.final_local_branching_radius,
        parameters_.final_demand_local_branching_radius,
        parameters_.final_local_branching_time_limit,
        &mined_variables_fixed
    );

    if (mined_variables_fixed > 0) {
        ++cs_pattern_mining_calls_;
        cs_pattern_mining_variables_fixed_ += mined_variables_fixed;
    }

    if (found_solution && improved.objective + 1e-9 < best.objective) {
        best = improved;
        return true;
    }

    return false;
}

// Executa o ALNS completo.
ALNSUM::Solution ALNSUM::solve() {
    const auto start_time = std::chrono::steady_clock::now();
    initializeClusters();
    cs_local_branching_seconds_ = 0.0;
    cs_local_branching_calls_ = 0;
    cs_local_branching_improvements_ = 0;
    elite_pool_.clear();
    cs_pattern_mining_calls_ = 0;
    cs_pattern_mining_variables_fixed_ = 0;

    const auto initial_solution_start = std::chrono::steady_clock::now();
    Solution current = buildInitialSolution();
    const auto initial_solution_end = std::chrono::steady_clock::now();
    Solution best = current;

    double destruction_seconds = 0.0;
    double repair_pool_seconds = 0.0;
    double repair_seconds = 0.0;
    double local_search_seconds = 0.0;
    double evaluation_seconds = 0.0;
    double acceptance_seconds = 0.0;
    double weight_update_seconds = 0.0;
    double clustering_search_seconds = 0.0;
    double final_intensification_seconds = 0.0;
    int clustering_search_calls = 0;
    int final_intensification_calls = 0;
    int final_intensification_improvements = 0;

    double temperature = parameters_.initial_temperature;
    int iterations_without_improvement = 0;
    int performed_iterations = 0;
    std::string stop_reason = "max_iterations";

    // Recompensas adaptativas:
    // 10 = nova melhor solucao global
    // 4  = melhora a solucao corrente
    // 0  = solucao aceita sem melhorar
    // 0  = solucao rejeitada
    //
    // Com isso, operadores que apenas geram candidatas "aceitaveis", mas sem ganho real,
    // deixam de acumular credito na roleta.
    const double reward_global_best = 10.0;
    const double reward_improvement = 4.0;
    const double reward_accepted = 0.0;
    const double reward_rejected = 0.0;

    if (parameters_.enable_clustering_search) {
        const auto clustering_start = std::chrono::steady_clock::now();
        processSolutionInClusteringSearch(current, best);
        const auto clustering_end = std::chrono::steady_clock::now();
        clustering_search_seconds +=
            std::chrono::duration<double>(clustering_end - clustering_start).count();
        ++clustering_search_calls;
    }

    for (int iteration = 1; iteration <= parameters_.iterations; ++iteration) {
        if (iterations_without_improvement >= parameters_.max_iterations_without_improvement) {
            stop_reason = "max_iterations_without_improvement";
            break;
        }

        performed_iterations = iteration;

        const int destroy_index = selectOperatorIndex(destroy_operators_);
        const int repair_index = selectOperatorIndex(repair_operators_);
        destroy_operators_[destroy_index].total_uses += 1;
        repair_operators_[repair_index].total_uses += 1;

        Solution candidate = current;
        std::vector<Demand> removed_demands;
        const int remove_count = drawRemovalCount(candidate);

        // Escolha do operador de destruicao.
        const auto destruction_start = std::chrono::steady_clock::now();
        if (destroy_index == 0) {
            removed_demands = destroyRandomAssignments(candidate, remove_count);
        } else if (destroy_index == 1) {
            removed_demands = destroyWorstVehicle(candidate, remove_count);
        } else if (destroy_index == 2) {
            removed_demands = destroyHighPenaltyAssignments(candidate, remove_count);
        } else {
            removed_demands = destroyUnderfilledVehicles(candidate, remove_count);
        }
        const auto destruction_end = std::chrono::steady_clock::now();
        destruction_seconds += std::chrono::duration<double>(destruction_end - destruction_start).count();

        // O pool de reparacao recebe tanto as demandas removidas quanto demandas nao atendidas.
        // Assim, os reparos passam a poder reduzir diretamente a parcela de penalidade da solucao.
        const auto repair_pool_start = std::chrono::steady_clock::now();
        std::vector<Demand> repair_demands = buildRepairCandidatePool(candidate, removed_demands);
        const auto repair_pool_end = std::chrono::steady_clock::now();
        repair_pool_seconds += std::chrono::duration<double>(repair_pool_end - repair_pool_start).count();

        // Escolha do operador de reparacao.
        const auto repair_start = std::chrono::steady_clock::now();
        if (repair_index == 0) {
            repairGreedyBestInsertion(candidate, repair_demands);
        } else if (repair_index == 1) {
            repairGreedyRandomOrder(candidate, repair_demands);
        } else if (repair_index == 2) {
            repairRegretInsertion(candidate, repair_demands);
        } else {
            repairBestNetGainInsertion(candidate, repair_demands);
        }
        const auto repair_end = std::chrono::steady_clock::now();
        repair_seconds += std::chrono::duration<double>(repair_end - repair_start).count();

        // O VND pode ser desligado para testes de ablação/desempenho.
        // Quando ativo, ele roda apenas em iteracoes estrategicas: apos melhora da candidata
        // ou periodicamente para manter algum refinamento estrutural.
        const bool candidate_improves_current = (candidate.objective + 1e-9 < current.objective);
        const bool should_run_local_search =
            parameters_.enable_vnd && (candidate_improves_current || (iteration % 8 == 0));
        if (should_run_local_search) {
            const auto local_search_start = std::chrono::steady_clock::now();
            applyLocalSearch(candidate);
            const auto local_search_end = std::chrono::steady_clock::now();
            local_search_seconds += std::chrono::duration<double>(local_search_end - local_search_start).count();
        }

        const auto evaluation_start = std::chrono::steady_clock::now();
        evaluateSolution(candidate);
        const auto evaluation_end = std::chrono::steady_clock::now();
        evaluation_seconds += std::chrono::duration<double>(evaluation_end - evaluation_start).count();

        bool accepted = false;
        double reward = 0.0;
        bool improved_global_best = false;
        bool improved_current_solution = false;

        const auto acceptance_start = std::chrono::steady_clock::now();
        if (acceptCandidate(candidate.objective, current.objective, temperature)) {
            accepted = true;
            reward = reward_accepted;
            destroy_operators_[destroy_index].total_accepts += 1;
            repair_operators_[repair_index].total_accepts += 1;

            if (candidate.objective + 1e-9 < current.objective) {
                reward = reward_improvement;
                improved_current_solution = true;
            }

            current = candidate;

            if (candidate.objective + 1e-9 < best.objective) {
                best = candidate;
                reward = reward_global_best;
                improved_global_best = true;
                iterations_without_improvement = 0;
            }
        }
        const auto acceptance_decision_end = std::chrono::steady_clock::now();
        acceptance_seconds +=
            std::chrono::duration<double>(acceptance_decision_end - acceptance_start).count();

        // A cada solucao aceita, o Clustering Search atualiza a regiao correspondente.
        // Quando um cluster acumula volume suficiente, o centro e intensificado por
        // Local Branching; se essa intensificacao melhora a melhor solucao global,
        // a busca corrente passa a partir desse novo ponto de elite.
        if (accepted && parameters_.enable_clustering_search) {
            const double best_before_cs = best.objective;
            const auto clustering_start = std::chrono::steady_clock::now();
            const bool cs_improved_best = processSolutionInClusteringSearch(current, best);
            const auto clustering_end = std::chrono::steady_clock::now();

            clustering_search_seconds +=
                std::chrono::duration<double>(clustering_end - clustering_start).count();
            ++clustering_search_calls;

            if (cs_improved_best || best.objective + 1e-9 < best_before_cs) {
                current = best;
                reward = reward_global_best;
                improved_global_best = true;
                iterations_without_improvement = 0;
            }
        }

        if (improved_current_solution) {
            destroy_operators_[destroy_index].total_improvements += 1;
            repair_operators_[repair_index].total_improvements += 1;
        }

        if (!improved_global_best) {
            ++iterations_without_improvement;
        }

        // O operador so recebe credito quando sua tentativa foi aceita.
        const auto score_update_start = std::chrono::steady_clock::now();
        if (accepted) {
            updateOperatorScore(destroy_operators_[destroy_index], reward);
            updateOperatorScore(repair_operators_[repair_index], reward);
        } else {
            updateOperatorScore(destroy_operators_[destroy_index], reward_rejected);
            updateOperatorScore(repair_operators_[repair_index], reward_rejected);
        }
        const auto score_update_end = std::chrono::steady_clock::now();
        acceptance_seconds +=
            std::chrono::duration<double>(score_update_end - score_update_start).count();

        // Atualizacao periodica dos pesos, que e o coracao do componente "Adaptive".
        if (iteration % std::max(1, parameters_.segment_size) == 0) {
            const auto weight_update_start = std::chrono::steady_clock::now();
            updateAdaptiveWeights(destroy_operators_);
            updateAdaptiveWeights(repair_operators_);
            const auto weight_update_end = std::chrono::steady_clock::now();
            weight_update_seconds += std::chrono::duration<double>(weight_update_end - weight_update_start).count();
        }

        temperature *= parameters_.cooling_rate;
    }

    if (performed_iterations >= parameters_.iterations) {
        stop_reason = "max_iterations";
    }

    // Refinamento final: uma unica chamada mais forte ao Local Branching usando a melhor
    // solucao global como centro. Mantemos separado do CS para medir o custo/beneficio
    // desta intensificacao de encerramento.
    if (parameters_.enable_final_intensification) {
        const auto final_intensification_start = std::chrono::steady_clock::now();
        ++final_intensification_calls;
        ++cs_local_branching_calls_;

        const bool final_improved = applyFinalIntensification(best);

        const auto final_intensification_end = std::chrono::steady_clock::now();
        const double final_seconds =
            std::chrono::duration<double>(final_intensification_end - final_intensification_start).count();
        final_intensification_seconds += final_seconds;
        cs_local_branching_seconds_ += final_seconds;

        if (final_improved) {
            ++final_intensification_improvements;
            ++cs_local_branching_improvements_;
        }
    }

    const auto end_time = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end_time - start_time;
    best.iterations_performed = performed_iterations;
    best.runtime_seconds = elapsed.count();
    best.stop_reason = stop_reason;
    best.initial_solution_seconds = std::chrono::duration<double>(initial_solution_end - initial_solution_start).count();
    best.destruction_seconds = destruction_seconds;
    best.repair_pool_seconds = repair_pool_seconds;
    best.repair_seconds = repair_seconds;
    best.local_search_seconds = local_search_seconds;
    best.evaluation_seconds = evaluation_seconds;
    best.acceptance_seconds = acceptance_seconds;
    best.weight_update_seconds = weight_update_seconds;
    best.clustering_search_seconds = clustering_search_seconds;
    best.local_branching_seconds = cs_local_branching_seconds_;
    best.clustering_search_calls = clustering_search_calls;
    best.local_branching_calls = cs_local_branching_calls_;
    best.local_branching_improvements = cs_local_branching_improvements_;
    best.final_intensification_seconds = final_intensification_seconds;
    best.final_intensification_calls = final_intensification_calls;
    best.final_intensification_improvements = final_intensification_improvements;
    best.pattern_mining_calls = cs_pattern_mining_calls_;
    best.pattern_mining_variables_fixed = cs_pattern_mining_variables_fixed_;

    return best;
}

// Impressao detalhada da melhor solucao encontrada pelo ALNS.
void ALNSUM::printSolutionSummary(const Solution& solution, std::ostream& out) const {
    const auto& veiculos = instance_.getVeiculos();

    out << "Resultado do ALNS\n";
    out << "Melhor objetivo encontrado: " << solution.objective << '\n';
    out << "Iteracoes executadas: " << solution.iterations_performed << '\n';
    out << "Maximo sem melhoria: " << parameters_.max_iterations_without_improvement << '\n';
    out << "Tempo de execucao (s): " << solution.runtime_seconds << '\n';
    out << "Criterio de parada: " << solution.stop_reason << '\n';
    out << "Seed: " << parameters_.random_seed << "\n\n";

    out << "Tempos por etapa (s):\n";
    out << "  solucao inicial = " << solution.initial_solution_seconds << '\n';
    out << "  destruicao = " << solution.destruction_seconds << '\n';
    out << "  pool de reparo = " << solution.repair_pool_seconds << '\n';
    out << "  reparo = " << solution.repair_seconds << '\n';
    out << "  busca local = " << solution.local_search_seconds << '\n';
    out << "  avaliacao final da candidata = " << solution.evaluation_seconds << '\n';
    out << "  aceitacao e scores = " << solution.acceptance_seconds << '\n';
    out << "  atualizacao de pesos = " << solution.weight_update_seconds << "\n\n";
    out << "  clustering search = " << solution.clustering_search_seconds << '\n';
    out << "  local branching = " << solution.local_branching_seconds << "\n\n";
    out << "  intensificacao final = " << solution.final_intensification_seconds << "\n\n";

    out << "Clustering Search:\n";
    out << "  chamadas ao CS = " << solution.clustering_search_calls << '\n';
    out << "  chamadas ao Local Branching = " << solution.local_branching_calls << '\n';
    out << "  melhorias por Local Branching = " << solution.local_branching_improvements << '\n';
    out << "  chamadas de intensificacao final = " << solution.final_intensification_calls << '\n';
    out << "  melhorias na intensificacao final = " << solution.final_intensification_improvements << "\n\n";

    out << "Mineracao de padroes (Data Mining):\n";
    out << "  chamadas de LB com variaveis fixadas = " << solution.pattern_mining_calls << '\n';
    out << "  total de variaveis fixadas = " << solution.pattern_mining_variables_fixed << "\n\n";

    // Resume a utilizacao da frota.
    for (std::size_t vehicle_id = 0; vehicle_id < veiculos.size(); ++vehicle_id) {
        const bool used = solution.vehicle_num_assignments[vehicle_id] > 0;
        const double dead_freight = used
            ? std::max(0.0, veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id]))
            : 0.0;

        out << "Veiculo " << vehicle_id
            << " | usado=" << (used ? 1 : 0)
            << " | peso=" << solution.vehicle_weight_load[vehicle_id] << "/" << veiculos[vehicle_id].capacidade_peso
            << " | volume=" << solution.vehicle_volume_load[vehicle_id] << "/" << veiculos[vehicle_id].capacidade_volume
            << " | frete morto=" << dead_freight << '\n';
    }

    int loaded_demands = 0;
    int unloaded_demands = 0;
    for (const Demand& demand : demands_) {
        if (solution.assigned_vehicle[demand.item_id][demand.client_id] >= 0) {
            ++loaded_demands;
        } else {
            ++unloaded_demands;
        }
    }

    out << '\n';
    out << "Itens carregados: " << loaded_demands << '\n';
    out << "Itens nao carregados: " << unloaded_demands << '\n';

    out << "\nOperadores de destruicao:\n";
    for (const OperatorStats& op : destroy_operators_) {
        const int failed_acceptance = op.total_uses - op.total_accepts;
        out << op.name
            << " | prob_final=" << op.weight
            << " | usos=" << op.total_uses
            << " | aceitos=" << op.total_accepts
            << " | falhas_aceitacao=" << failed_acceptance
            << " | melhorias=" << op.total_improvements << '\n';
    }

    out << "\nOperadores de reparacao:\n";
    for (const OperatorStats& op : repair_operators_) {
        const int failed_acceptance = op.total_uses - op.total_accepts;
        out << op.name
            << " | prob_final=" << op.weight
            << " | usos=" << op.total_uses
            << " | aceitos=" << op.total_accepts
            << " | falhas_aceitacao=" << failed_acceptance
            << " | melhorias=" << op.total_improvements << '\n';
    }
}

void ALNSUM::exportSolutionCsv(const Solution& solution, const std::string& output_path) const {
    std::ofstream out(output_path);
    if (!out.is_open()) {
        throw std::runtime_error("Nao foi possivel criar o arquivo CSV da solucao do ALNS.");
    }

    const auto& veiculos = instance_.getVeiculos();

    out << "record_type,entity_id,objective,iterations,runtime_seconds,stop_reason,cs_calls,lb_calls,"
           "lb_improvements,cs_seconds,lb_seconds,final_seconds,final_calls,final_improvements,used,dead_freight,"
           "weight_load,weight_capacity,volume_load,volume_capacity,item_id,client_id,vehicle_id,served,"
           "prob_final,total_uses,total_accepts,failed_acceptance,total_improvements,"
           "pattern_mining_calls,pattern_mining_variables_fixed\n";

    out << "summary,global," << solution.objective << "," << solution.iterations_performed << ","
        << solution.runtime_seconds << "," << solution.stop_reason
        << "," << solution.clustering_search_calls
        << "," << solution.local_branching_calls
        << "," << solution.local_branching_improvements
        << "," << solution.clustering_search_seconds
        << "," << solution.local_branching_seconds
        << "," << solution.final_intensification_seconds
        << "," << solution.final_intensification_calls
        << "," << solution.final_intensification_improvements
        << ",,,,,,,,,,,,,,"
        << "," << solution.pattern_mining_calls
        << "," << solution.pattern_mining_variables_fixed << "\n";

    for (std::size_t vehicle_id = 0; vehicle_id < veiculos.size(); ++vehicle_id) {
        const bool used = solution.vehicle_num_assignments[vehicle_id] > 0;
        const double dead_freight = used
            ? std::max(0.0, veiculos[vehicle_id].carga_minima - static_cast<double>(solution.vehicle_weight_load[vehicle_id]))
            : 0.0;

        out << "vehicle," << vehicle_id << ",,,,,,,,,,,,,"
            << (used ? 1 : 0) << "," << dead_freight << ","
            << solution.vehicle_weight_load[vehicle_id] << "," << veiculos[vehicle_id].capacidade_peso << ","
            << solution.vehicle_volume_load[vehicle_id] << "," << veiculos[vehicle_id].capacidade_volume
            << ",,,,,,,,,,\n";
    }

    for (const Demand& demand : demands_) {
        const int assigned_vehicle = solution.assigned_vehicle[demand.item_id][demand.client_id];
        out << "demand,,,,,,,,,,,,,,,,,,,,"
            << demand.item_id << "," << demand.client_id << "," << assigned_vehicle << ","
            << (assigned_vehicle >= 0 ? 1 : 0) << ",,,,,,\n";
    }

    for (const OperatorStats& op : destroy_operators_) {
        const int failed_acceptance = op.total_uses - op.total_accepts;
        out << "destroy_operator," << op.name << ",,,,,,,,,,,,,,,,,,,,,,,,"
            << op.weight << "," << op.total_uses << "," << op.total_accepts << ","
            << failed_acceptance << "," << op.total_improvements << ",,\n";
    }

    for (const OperatorStats& op : repair_operators_) {
        const int failed_acceptance = op.total_uses - op.total_accepts;
        out << "repair_operator," << op.name << ",,,,,,,,,,,,,,,,,,,,,,,,"
            << op.weight << "," << op.total_uses << "," << op.total_accepts << ","
            << failed_acceptance << "," << op.total_improvements << ",,\n";
    }
}
