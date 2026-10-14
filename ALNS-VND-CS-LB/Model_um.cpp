#include "Model_um.h"

#include <cmath>
#include <fstream>
#include <stdexcept>

ModelUM::ModelUM(const InstanceUM& instance)
    : instance_(instance) {}

void ModelUM::setLogFilePath(const std::string& log_file_path) {
    log_file_path_ = log_file_path;
}

void ModelUM::build() {
    // Prepara ambiente e objeto principal do Gurobi para montar o modelo.
    const auto& metadata = instance_.getMetadata();

    env_ = std::make_unique<GRBEnv>(true);
    env_->set(GRB_IntParam_OutputFlag, 1);
    if (!log_file_path_.empty()) {
        env_->set(GRB_StringParam_LogFile, log_file_path_);
    }
    env_->start();

    model_ = std::make_unique<GRBModel>(*env_);
    model_->set(GRB_StringAttr_ModelName, "UM_Gurobi_Model");

    // Por padrao, mantemos presolve, cortes e heuristicas internas do Gurobi.
    // Quando compilado com GUROBI_DISABLE_INTERNALS, desligamos esses recursos
    // para testar o comportamento "puro" do modelo.
//#ifdef GUROBI_DISABLE_INTERNALS
    model_->set(GRB_IntParam_Presolve, 0);
    model_->set(GRB_IntParam_PrePasses, 0);
    model_->set(GRB_IntParam_Aggregate, 0);
    model_->set(GRB_IntParam_Cuts, 0);
    model_->set(GRB_DoubleParam_Heuristics, 0.0);
    model_->set(GRB_IntParam_GomoryPasses, 0);
//#endif

    y_.clear();
    z_.clear();
    x_.assign(
        metadata.num_itens,
        std::vector<std::vector<GRBVar>>(metadata.num_veiculos, std::vector<GRBVar>(metadata.num_clientes))
    );
    x_exists_.assign(
        metadata.num_itens,
        std::vector<std::vector<bool>>(metadata.num_veiculos, std::vector<bool>(metadata.num_clientes, false))
    );

    // Etapas de construcao do modelo matematico.
    createVariables();
    addObjective();
    addConstraints();

    built_ = true;
}

void ModelUM::optimize() {
    // Garante que o modelo foi construido antes de chamar o solver.
    ensureBuilt();
    // Colocando um time limit de 3600 segundos (1 hora) para evitar execucoes muito longas.
    model_->set(GRB_DoubleParam_TimeLimit, 3600.0);
    model_->optimize();
}

void ModelUM::printSolutionSummary(std::ostream& out) const {
    // Imprime status, objetivo e variaveis ativas da melhor solucao encontrada.
    ensureBuilt();

    const int status = model_->get(GRB_IntAttr_Status);
    out << "Status do modelo: " << status << '\n';

    if (status == GRB_OPTIMAL || status == GRB_SUBOPTIMAL) {
        out << "Valor objetivo: " << model_->get(GRB_DoubleAttr_ObjVal) << '\n';
        out << "Best bound: " << model_->get(GRB_DoubleAttr_ObjBound) << '\n';
        out << "Gap: " << getGap() << '\n';
        out << "Tempo de execucao (s): " << model_->get(GRB_DoubleAttr_Runtime) << '\n';

        const auto& veiculos = instance_.getVeiculos();
        const auto& itens = instance_.getItens();
        const auto& phi = instance_.getPhi();

        // Mostra ativacao (y_v), frete morto (z_v) e uso de capacidade por veiculo.
        for (std::size_t v = 0; v < y_.size(); ++v) {
            double peso_carregado = 0.0;
            double volume_carregado = 0.0;

            for (std::size_t i = 0; i < x_.size(); ++i) {
                for (int cliente_id : phi[i]) {
                    if (x_exists_[i][v][cliente_id] && x_[i][v][cliente_id].get(GRB_DoubleAttr_X) > 0.5) {
                        peso_carregado += itens[i].peso;
                        volume_carregado += itens[i].volume;
                    }
                }
            }

            out << "Veiculo " << v
                << " -> y=" << y_[v].get(GRB_DoubleAttr_X)
                << ", z=" << z_[v].get(GRB_DoubleAttr_X)
                << ", peso carregado=" << peso_carregado << "/" << veiculos[v].capacidade_peso
                << ", volume carregado=" << volume_carregado << "/" << veiculos[v].capacidade_volume
                << '\n';
        }

        int loaded_demands = 0;
        int unloaded_demands = 0;

        // Agrega a quantidade de demandas atendidas e nao atendidas para facilitar
        // a comparacao com a saida do ALNS.
        for (std::size_t i = 0; i < x_.size(); ++i) {
            for (int cliente_id : phi[i]) {
                double atendimento_cliente = 0.0;
                for (std::size_t v = 0; v < x_[i].size(); ++v) {
                    if (x_exists_[i][v][cliente_id]) {
                        atendimento_cliente += x_[i][v][cliente_id].get(GRB_DoubleAttr_X);
                    }
                }

                if (atendimento_cliente > 0.5) {
                    ++loaded_demands;
                } else {
                    ++unloaded_demands;
                }
            }
        }

        out << "Itens carregados: " << loaded_demands << '\n';
        out << "Itens nao carregados: " << unloaded_demands << '\n';
    }
}

void ModelUM::exportSolutionCsv(const std::string& output_path) const {
    ensureBuilt();

    std::ofstream out(output_path);
    if (!out.is_open()) {
        throw std::runtime_error("Nao foi possivel criar o arquivo CSV da solucao do modelo.");
    }

    const int status = model_->get(GRB_IntAttr_Status);
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();
    const auto& phi = instance_.getPhi();

    out << "record_type,entity_id,status,objective,best_bound,gap,runtime_seconds,used,dead_freight,"
           "weight_load,weight_capacity,volume_load,volume_capacity\n";

    double objective = 0.0;
    double best_bound = 0.0;
    double runtime = 0.0;
    if (status == GRB_OPTIMAL || status == GRB_SUBOPTIMAL) {
        objective = model_->get(GRB_DoubleAttr_ObjVal);
        best_bound = model_->get(GRB_DoubleAttr_ObjBound);
        runtime = model_->get(GRB_DoubleAttr_Runtime);
    }

    out << "summary,global," << status << "," << objective << "," << best_bound << "," << getGap() << ","
        << runtime << ",,,,,,\n";

    if (status != GRB_OPTIMAL && status != GRB_SUBOPTIMAL) {
        return;
    }

    for (std::size_t v = 0; v < y_.size(); ++v) {
        double peso_carregado = 0.0;
        double volume_carregado = 0.0;

        for (std::size_t i = 0; i < x_.size(); ++i) {
            for (int cliente_id : phi[i]) {
                if (x_exists_[i][v][cliente_id] && x_[i][v][cliente_id].get(GRB_DoubleAttr_X) > 0.5) {
                    peso_carregado += itens[i].peso;
                    volume_carregado += itens[i].volume;
                }
            }
        }

        out << "vehicle," << v << ",,,,,,"
            << (y_[v].get(GRB_DoubleAttr_X) > 0.5 ? 1 : 0) << ","
            << z_[v].get(GRB_DoubleAttr_X) << ","
            << peso_carregado << "," << veiculos[v].capacidade_peso << ","
            << volume_carregado << "," << veiculos[v].capacidade_volume << "\n";
    }
}

double ModelUM::getObjectiveValue() const {
    ensureBuilt();
    return model_->get(GRB_DoubleAttr_ObjVal);
}

double ModelUM::getBestBound() const {
    ensureBuilt();
    return model_->get(GRB_DoubleAttr_ObjBound);
}

double ModelUM::getGap() const {
    ensureBuilt();

    const int status = model_->get(GRB_IntAttr_Status);
    if (status != GRB_OPTIMAL && status != GRB_SUBOPTIMAL) {
        return 0.0;
    }

    const double objective = model_->get(GRB_DoubleAttr_ObjVal);
    const double best_bound = model_->get(GRB_DoubleAttr_ObjBound);
    const double denominator = std::max(1.0, std::abs(objective));
    return std::abs(objective - best_bound) / denominator;
}

int ModelUM::getStatus() const {
    ensureBuilt();
    return model_->get(GRB_IntAttr_Status);
}

void ModelUM::createVariables() {
    const auto& metadata = instance_.getMetadata();
    const auto& psi = instance_.getPsi();
    const auto& phi = instance_.getPhi();
    const auto& gamma = instance_.getGamma();

    // Variaveis y_v: ativacao do veiculo v.
    // Variaveis z_v: frete morto/carga faltante do veiculo v.
    y_.reserve(metadata.num_veiculos);
    z_.reserve(metadata.num_veiculos);

    for (int v = 0; v < metadata.num_veiculos; ++v) {
        y_.push_back(model_->addVar(0.0, 1.0, 0.0, GRB_BINARY, "y_" + std::to_string(v)));
        z_.push_back(model_->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, "z_" + std::to_string(v)));
    }

    // Variaveis x_{ivc}: item i entregue ao cliente c pelo veiculo v.
    // So criamos x_{ivc} quando v pertence a Psi(i) e c pertence a Phi(i) e Gamma(v).
    for (int i = 0; i < metadata.num_itens; ++i) {
        for (int v : psi[i]) {
            for (int cliente_id : phi[i]) {
                bool cliente_atendido_pelo_veiculo = false;
                for (int cliente_gamma : gamma[v]) {
                    if (cliente_gamma == cliente_id) {
                        cliente_atendido_pelo_veiculo = true;
                        break;
                    }
                }

                if (cliente_atendido_pelo_veiculo) {
                    x_[i][v][cliente_id] = model_->addVar(
                        0.0,
                        1.0,
                        0.0,
                        GRB_BINARY,
                        "x_" + std::to_string(i) + "_" + std::to_string(v) + "_" + std::to_string(cliente_id)
                    );
                    x_exists_[i][v][cliente_id] = true;
                }
            }
        }
    }
}

void ModelUM::addObjective() {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();
    const auto& psi = instance_.getPsi();
    const auto& phi = instance_.getPhi();

    // Funcao objetivo (min):
    // min [ sum_v (c_v * y_v + z_v)
    //       + sum_i w_i * p_i * (d_i - sum_{v in Psi(i)} sum_{c in Phi(i)} x_{ivc}) ]
    GRBLinExpr objective = 0.0;

    for (std::size_t v = 0; v < veiculos.size(); ++v) {
        objective += veiculos[v].custo * y_[v];
        objective += z_[v];
    }

    for (std::size_t i = 0; i < itens.size(); ++i) {
        GRBLinExpr atendimento_item = 0.0;
        for (int v : psi[i]) {
            for (int cliente_id : phi[i]) {
                if (x_exists_[i][v][cliente_id]) {
                    atendimento_item += x_[i][v][cliente_id];
                }
            }
        }
        objective += itens[i].peso * itens[i].penalidade * (itens[i].demanda_total - atendimento_item);
    }

    model_->setObjective(objective, GRB_MINIMIZE);
}

void ModelUM::addConstraints() {
    const auto& veiculos = instance_.getVeiculos();
    const auto& itens = instance_.getItens();
    const auto& psi = instance_.getPsi();
    const auto& phi = instance_.getPhi();
    const auto& omega = instance_.getOmega();
    const auto& gamma = instance_.getGamma();

    for (std::size_t v = 0; v < veiculos.size(); ++v) {
        GRBLinExpr carga_peso = 0.0;
        GRBLinExpr carga_volume = 0.0;

        // Restricoes de capacidade por veiculo v:
        // sum_{i in Omega(v)} sum_{c in Gamma(v)} w_i * x_{ivc} <= W_v
        // sum_{i in Omega(v)} sum_{c in Gamma(v)} u_i * x_{ivc} <= U_v
        for (int i : omega[v]) {
            for (int cliente_id : gamma[v]) {
                if (x_exists_[i][v][cliente_id]) {
                    carga_peso += itens[i].peso * x_[i][v][cliente_id];
                    carga_volume += itens[i].volume * x_[i][v][cliente_id];
                }
            }
        }

        model_->addConstr(
            carga_peso <= veiculos[v].capacidade_peso,
            "cap_peso_" + std::to_string(v)
        );

        model_->addConstr(
            carga_volume <= veiculos[v].capacidade_volume,
            "cap_volume_" + std::to_string(v)
        );

        // Restricao de frete morto/carga minima:
        // z_v >= W_v^min * y_v - sum_{i in Omega(v)} sum_{c in Gamma(v)} w_i * x_{ivc}
        model_->addConstr(
            z_[v] >= veiculos[v].carga_minima * y_[v] - carga_peso,
            "frete_morto_" + std::to_string(v)
        );
    }

    for (std::size_t i = 0; i < itens.size(); ++i) {
        GRBLinExpr atendimento_item = 0.0;

        for (int v : psi[i]) {
            for (int cliente_id : phi[i]) {
                if (x_exists_[i][v][cliente_id]) {
                    atendimento_item += x_[i][v][cliente_id];

                    // Restricao de ativacao:
                    // x_{ivc} <= y_v
                    model_->addConstr(
                        x_[i][v][cliente_id] <= y_[v],
                        "ativacao_" + std::to_string(i) + "_" + std::to_string(v) + "_" + std::to_string(cliente_id)
                    );
                }
            }
        }

        // Restricao de atendimento total da demanda do item:
        // sum_{v in Psi(i)} sum_{c in Phi(i)} x_{ivc} <= d_i
        model_->addConstr(
            atendimento_item <= itens[i].demanda_total,
            "demanda_item_" + std::to_string(i)
        );
    }

    for (std::size_t i = 0; i < itens.size(); ++i) {
        for (int cliente_id : phi[i]) {
            GRBLinExpr atendimento_cliente_item = 0.0;
            for (int v : psi[i]) {
                if (x_exists_[i][v][cliente_id]) {
                    atendimento_cliente_item += x_[i][v][cliente_id];
                }
            }

            // Restricao para nao atender o mesmo cliente mais de uma vez com o mesmo item:
            // sum_{v in Psi(i)} x_{ivc} <= 1
            model_->addConstr(
                atendimento_cliente_item <= 1.0,
                "unicidade_item_cliente_" + std::to_string(i) + "_" + std::to_string(cliente_id)
            );
        }
    }
}

void ModelUM::ensureBuilt() const {
    // Evita acesso ao modelo antes da etapa build().
    if (!built_ || env_ == nullptr || model_ == nullptr) {
        throw std::runtime_error("O modelo ainda nao foi construido.");
    }
}
