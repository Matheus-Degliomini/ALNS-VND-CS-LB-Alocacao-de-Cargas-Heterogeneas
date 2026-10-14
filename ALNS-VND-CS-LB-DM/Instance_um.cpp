#include "Instance_um.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

// Carrega o arquivo de instancia completo, processa secoes e valida consistencia final.
// path: caminho do arquivo a ser lido.
// error_message: recebe a mensagem de erro, quando informado.
bool InstanceUM::loadFromFile(const std::string& path, std::string* error_message) {
    try {
        // Garante estado limpo para evitar dados residuais de carregamentos anteriores.
        clear();

        std::ifstream input(path);
        if (!input.is_open()) {
            throw std::runtime_error("Nao foi possivel abrir o arquivo: " + path);
        }

        std::string line;
        std::string current_section;
        while (std::getline(input, line)) {
            // Remove espacos e ignora linhas em branco.
            line = trim(line);
            if (line.empty()) {
                continue;
            }

            // Atualiza o contexto da secao atual ao encontrar um cabecalho [SECAO].
            if (isSectionHeader(line)) {
                current_section = line;
                continue;
            }

            if (current_section.empty()) {
                throw std::runtime_error("Linha fora de secao: " + line);
            }

            parseSectionLine(current_section, line);
        }

        // Valida referencias cruzadas e cardinalidades declaradas no metadata.
        validate();
        return true;
    } catch (const std::exception& ex) {
        // Propaga mensagem amigavel ao chamador e restaura estado vazio.
        if (error_message != nullptr) {
            *error_message = ex.what();
        }
        clear();
        return false;
    }
}

// Getters de acesso somente leitura para os dados parseados.
const InstanceUM::Metadata& InstanceUM::getMetadata() const { return metadata_; }
const std::unordered_map<int, std::string>& InstanceUM::getTiposItens() const { return tipos_itens_; }
const std::vector<int>& InstanceUM::getRegioes() const { return regioes_; }
const std::vector<std::vector<int>>& InstanceUM::getItensPorCliente() const { return itens_por_cliente_; }
const std::vector<InstanceUM::Veiculo>& InstanceUM::getVeiculos() const { return veiculos_; }
const std::vector<InstanceUM::Oferta>& InstanceUM::getOfertas() const { return ofertas_; }
const std::vector<InstanceUM::Item>& InstanceUM::getItens() const { return itens_; }
const std::vector<std::vector<int>>& InstanceUM::getPhi() const { return phi_; }
const std::vector<std::vector<int>>& InstanceUM::getPsi() const { return psi_; }
const std::vector<std::vector<int>>& InstanceUM::getGamma() const { return gamma_; }
const std::vector<std::vector<int>>& InstanceUM::getOmega() const { return omega_; }

// Retorna o nome textual associado a um codigo de tipo de item.
// tipo_codigo: chave numerica esperada no mapa tipos_itens_.
const std::string& InstanceUM::getTipoNome(int tipo_codigo) const {
    auto it = tipos_itens_.find(tipo_codigo);
    if (it == tipos_itens_.end()) {
        throw std::out_of_range("Tipo de item inexistente: " + std::to_string(tipo_codigo));
    }
    return it->second;
}

// Limpa todas as estruturas internas para preparar novo parse.
void InstanceUM::clear() {
    metadata_ = Metadata{};
    tipos_itens_.clear();
    regioes_.clear();
    itens_por_cliente_.clear();
    veiculos_.clear();
    ofertas_.clear();
    itens_.clear();
    phi_.clear();
    psi_.clear();
    gamma_.clear();
    omega_.clear();
}

// Remove espacos em branco das extremidades da string.
// text: texto de entrada a ser normalizado.
std::string InstanceUM::trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

// Identifica cabecalhos no formato [NOME_DA_SECAO].
bool InstanceUM::isSectionHeader(const std::string& line) {
    return line.size() >= 3 && line.front() == '[' && line.back() == ']';
}

// Separa uma linha em tokens usando o delimitador informado.
// text: texto completo da linha.
// delimiter: caractere usado como separador (ex.: ';' ou '=').
std::vector<std::string> InstanceUM::split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, delimiter)) {
        parts.push_back(part);
    }
    return parts;
}

// Converte texto em inteiro com validacao estrita.
// text: token a converter.
// field_name: nome do campo para contextualizar mensagens de erro.
int InstanceUM::toInt(const std::string& text, const std::string& field_name) {
    try {
        std::size_t processed = 0;
        int value = std::stoi(text, &processed);
        if (processed != text.size()) {
            throw std::runtime_error("");
        }
        return value;
    } catch (...) {
        throw std::runtime_error("Valor inteiro invalido para " + field_name + ": " + text);
    }
}

// Converte texto em ponto flutuante com validacao estrita.
// text: token a converter.
// field_name: nome do campo para contextualizar mensagens de erro.
double InstanceUM::toDouble(const std::string& text, const std::string& field_name) {
    try {
        std::size_t processed = 0;
        double value = std::stod(text, &processed);
        if (processed != text.size()) {
            throw std::runtime_error("");
        }
        return value;
    } catch (...) {
        throw std::runtime_error("Valor real invalido para " + field_name + ": " + text);
    }
}

// Parse de listas separadas por virgula (ex.: "1,3,5").
// text: conteudo da lista (pode ser vazio).
std::vector<int> InstanceUM::parseIntList(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    std::vector<int> values;
    for (const std::string& token : split(text, ',')) {
        if (!token.empty()) {
            values.push_back(toInt(token, "lista"));
        }
    }
    return values;
}

// Valida que os ids chegam em ordem 0..N-1 para manter alinhamento com indices de vetor.
// actual_id: id lido da linha.
// expected_id: proximo id esperado com base no estado atual.
// label: nome da entidade para montagem da mensagem de erro.
void InstanceUM::ensureSequentialId(int actual_id, int expected_id, const std::string& label) {
    if (actual_id != expected_id) {
        throw std::runtime_error(
            "Ids de " + label + " devem ser sequenciais e baseados em 0. Esperado " +
            std::to_string(expected_id) + ", recebido " + std::to_string(actual_id)
        );
    }
}

// Encaminha a linha para o parser da secao ativa.
// section: nome da secao entre colchetes.
// line: conteudo da linha sem cabecalho.
void InstanceUM::parseSectionLine(const std::string& section, const std::string& line) {
    if (section == "[METADATA]") {
        parseMetadata(line);
    } else if (section == "[TIPOS_ITENS]") {
        parseTiposItens(line);
    } else if (section == "[REGIOES]") {
        parseRegioes(line);
    } else if (section == "[CLIENTES]") {
        parseClientes(line);
    } else if (section == "[VEICULOS]") {
        parseVeiculos(line);
    } else if (section == "[OFERTAS]") {
        parseOfertas(line);
    } else if (section == "[ITENS]") {
        parseItens(line);
    } else if (section == "[PHI]") {
        parsePhi(line);
    } else if (section == "[PSI]") {
        parsePsi(line);
    } else if (section == "[GAMMA]") {
        parseGamma(line);
    } else if (section == "[OMEGA]") {
        parseOmega(line);
    } else {
        throw std::runtime_error("Secao desconhecida: " + section);
    }
}

// Parse de uma linha da secao [METADATA] no formato CHAVE=VALOR.
// line: conteudo bruto da linha.
void InstanceUM::parseMetadata(const std::string& line) {
    const auto parts = split(line, '=');
    if (parts.size() != 2U) {
        throw std::runtime_error("Linha de metadata invalida: " + line);
    }

    const std::string key = trim(parts[0]);
    const int value = toInt(trim(parts[1]), key);

    if (key == "NUM_REGIOES") {
        metadata_.num_regioes = value;
    } else if (key == "NUM_CLIENTES") {
        metadata_.num_clientes = value;
    } else if (key == "NUM_VEICULOS") {
        metadata_.num_veiculos = value;
    } else if (key == "NUM_OFERTAS") {
        metadata_.num_ofertas = value;
    } else if (key == "NUM_ITENS") {
        metadata_.num_itens = value;
    } else {
        throw std::runtime_error("Chave de metadata desconhecida: " + key);
    }
}

// Parse de uma linha da secao [TIPOS_ITENS] no formato tipo_codigo;nome.
// line: conteudo bruto da linha.
void InstanceUM::parseTiposItens(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 2U) {
        throw std::runtime_error("Linha de tipos de itens invalida: " + line);
    }

    const int code = toInt(parts[0], "tipo_codigo");
    tipos_itens_[code] = parts[1];
}

// Parse de uma linha da secao [REGIOES], contendo apenas regiao_id.
// line: conteudo bruto da linha.
void InstanceUM::parseRegioes(const std::string& line) {
    regioes_.push_back(toInt(line, "regiao_id"));
}

// Parse de uma linha da secao [CLIENTES] no formato cliente_id;lista_de_itens.
// line: conteudo bruto da linha.
void InstanceUM::parseClientes(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 2U) {
        throw std::runtime_error("Linha de clientes invalida: " + line);
    }

    const int cliente_id = toInt(parts[0], "cliente_id");
    ensureSequentialId(cliente_id, static_cast<int>(itens_por_cliente_.size()), "cliente");
    itens_por_cliente_.push_back(parseIntList(parts[1]));
}

// Parse de uma linha da secao [VEICULOS] no formato id;cap_peso;cap_volume;custo;carga_minima.
// line: conteudo bruto da linha.
void InstanceUM::parseVeiculos(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 5U) {
        throw std::runtime_error("Linha de veiculos invalida: " + line);
    }

    Veiculo veiculo;
    veiculo.id = toInt(parts[0], "veiculo_id");
    veiculo.capacidade_peso = toInt(parts[1], "capacidade_peso");
    veiculo.capacidade_volume = toInt(parts[2], "capacidade_volume");
    veiculo.custo = toInt(parts[3], "custo");
    veiculo.carga_minima = toDouble(parts[4], "carga_minima");
    ensureSequentialId(veiculo.id, static_cast<int>(veiculos_.size()), "veiculo");
    veiculos_.push_back(veiculo);
}

// Parse de uma linha da secao [OFERTAS] no formato id;tipo;peso;volume;penalidade.
// line: conteudo bruto da linha.
void InstanceUM::parseOfertas(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 5U) {
        throw std::runtime_error("Linha de ofertas invalida: " + line);
    }

    Oferta oferta;
    oferta.id = toInt(parts[0], "oferta_id");
    oferta.tipo_codigo = toInt(parts[1], "tipo_codigo");
    oferta.peso = toInt(parts[2], "peso");
    oferta.volume = toDouble(parts[3], "volume");
    oferta.penalidade = toDouble(parts[4], "penalidade");
    ensureSequentialId(oferta.id, static_cast<int>(ofertas_.size()), "oferta");
    ofertas_.push_back(oferta);
}

// Parse de uma linha da secao [ITENS] no formato id;oferta;tipo;peso;volume;penalidade.
// line: conteudo bruto da linha.
void InstanceUM::parseItens(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 6U) {
        throw std::runtime_error("Linha de itens invalida: " + line);
    }

    Item item;
    item.id = toInt(parts[0], "item_id");
    item.oferta_id = toInt(parts[1], "oferta_id");
    item.tipo_codigo = toInt(parts[2], "tipo_codigo");
    item.peso = toInt(parts[3], "peso");
    item.volume = toDouble(parts[4], "volume");
    item.penalidade = toDouble(parts[5], "penalidade");
    ensureSequentialId(item.id, static_cast<int>(itens_.size()), "item");
    itens_.push_back(item);
}

// Parse de uma linha da secao [PHI] no formato item_id;quantidade_clientes;lista_clientes.
// line: conteudo bruto da linha.
void InstanceUM::parsePhi(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 3U) {
        throw std::runtime_error("Linha de PHI invalida: " + line);
    }

    const int item_id = toInt(parts[0], "item_id");
    const int quantidade = toInt(parts[1], "quantidade_clientes");
    const std::vector<int> clientes = parseIntList(parts[2]);
    ensureSequentialId(item_id, static_cast<int>(phi_.size()), "item_phi");
    if (quantidade != static_cast<int>(clientes.size())) {
        throw std::runtime_error("Quantidade de PHI inconsistente para item " + std::to_string(item_id));
    }
    if (item_id < 0 || item_id >= static_cast<int>(itens_.size())) {
        throw std::runtime_error("PHI referencia item inexistente: " + std::to_string(item_id));
    }
    itens_[item_id].demanda_total = quantidade;
    phi_.push_back(clientes);
}

// Parse de uma linha da secao [PSI] no formato item_id;quantidade;lista_veiculos.
// line: conteudo bruto da linha.
void InstanceUM::parsePsi(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 3U) {
        throw std::runtime_error("Linha de PSI invalida: " + line);
    }

    const int item_id = toInt(parts[0], "item_id");
    const int quantidade = toInt(parts[1], "quantidade_veiculos");
    const std::vector<int> lista = parseIntList(parts[2]);
    ensureSequentialId(item_id, static_cast<int>(psi_.size()), "item_psi");
    if (quantidade != static_cast<int>(lista.size())) {
        throw std::runtime_error("Quantidade de PSI inconsistente para item " + std::to_string(item_id));
    }
    psi_.push_back(lista);
}

// Parse de uma linha da secao [GAMMA] no formato veiculo_id;quantidade;lista_clientes.
// line: conteudo bruto da linha.
void InstanceUM::parseGamma(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 3U) {
        throw std::runtime_error("Linha de GAMMA invalida: " + line);
    }

    const int veiculo_id = toInt(parts[0], "veiculo_id");
    const int quantidade = toInt(parts[1], "quantidade_clientes");
    const std::vector<int> lista = parseIntList(parts[2]);
    ensureSequentialId(veiculo_id, static_cast<int>(gamma_.size()), "veiculo_gamma");
    if (quantidade != static_cast<int>(lista.size())) {
        throw std::runtime_error("Quantidade de GAMMA inconsistente para veiculo " + std::to_string(veiculo_id));
    }
    gamma_.push_back(lista);
}

// Parse de uma linha da secao [OMEGA] no formato veiculo_id;quantidade;lista_itens.
// line: conteudo bruto da linha.
void InstanceUM::parseOmega(const std::string& line) {
    const auto parts = split(line, ';');
    if (parts.size() != 3U) {
        throw std::runtime_error("Linha de OMEGA invalida: " + line);
    }

    const int veiculo_id = toInt(parts[0], "veiculo_id");
    const int quantidade = toInt(parts[1], "quantidade_itens");
    const std::vector<int> lista = parseIntList(parts[2]);
    ensureSequentialId(veiculo_id, static_cast<int>(omega_.size()), "veiculo_omega");
    if (quantidade != static_cast<int>(lista.size())) {
        throw std::runtime_error("Quantidade de OMEGA inconsistente para veiculo " + std::to_string(veiculo_id));
    }
    omega_.push_back(lista);
}

// Executa verificacoes finais de consistencia estrutural e referencial.
void InstanceUM::validate() const {
    // Confere se os totais lidos batem com os valores declarados em [METADATA].
    if (metadata_.num_clientes != static_cast<int>(itens_por_cliente_.size())) {
        throw std::runtime_error("NUM_CLIENTES inconsistente com a secao [CLIENTES]");
    }
    if (metadata_.num_veiculos != static_cast<int>(veiculos_.size())) {
        throw std::runtime_error("NUM_VEICULOS inconsistente com a secao [VEICULOS]");
    }
    if (metadata_.num_ofertas != static_cast<int>(ofertas_.size())) {
        throw std::runtime_error("NUM_OFERTAS inconsistente com a secao [OFERTAS]");
    }
    if (metadata_.num_itens != static_cast<int>(itens_.size())) {
        throw std::runtime_error("NUM_ITENS inconsistente com a secao [ITENS]");
    }
    if (!regioes_.empty() && metadata_.num_regioes != static_cast<int>(regioes_.size())) {
        throw std::runtime_error("NUM_REGIOES inconsistente com a secao [REGIOES]");
    }
    if (static_cast<int>(phi_.size()) != metadata_.num_itens) {
        throw std::runtime_error("Secao [PHI] inconsistente com NUM_ITENS");
    }
    if (static_cast<int>(psi_.size()) != metadata_.num_itens) {
        throw std::runtime_error("Secao [PSI] inconsistente com NUM_ITENS");
    }
    if (static_cast<int>(gamma_.size()) != metadata_.num_veiculos) {
        throw std::runtime_error("Secao [GAMMA] inconsistente com NUM_VEICULOS");
    }
    if (static_cast<int>(omega_.size()) != metadata_.num_veiculos) {
        throw std::runtime_error("Secao [OMEGA] inconsistente com NUM_VEICULOS");
    }

    // Confere referencias internas de cada item (cliente, oferta e tipo de item).
    for (const Item& item : itens_) {
        if (item.oferta_id < 0 || item.oferta_id >= metadata_.num_ofertas) {
            throw std::runtime_error("Item com oferta_id fora do intervalo: " + std::to_string(item.id));
        }
        if (tipos_itens_.find(item.tipo_codigo) == tipos_itens_.end()) {
            throw std::runtime_error("Item com tipo_codigo sem mapeamento: " + std::to_string(item.id));
        }
    }

    for (int item_id = 0; item_id < metadata_.num_itens; ++item_id) {
        for (int cliente_id : phi_[item_id]) {
            if (cliente_id < 0 || cliente_id >= metadata_.num_clientes) {
                throw std::runtime_error("Item com cliente em PHI fora do intervalo: " + std::to_string(item_id));
            }
        }
    }

    for (int cliente_id = 0; cliente_id < metadata_.num_clientes; ++cliente_id) {
        for (int item_id : itens_por_cliente_[cliente_id]) {
            if (item_id < 0 || item_id >= metadata_.num_itens) {
                throw std::runtime_error("Cliente com item fora do intervalo: " + std::to_string(cliente_id));
            }
        }
    }

    for (int item_id = 0; item_id < metadata_.num_itens; ++item_id) {
        for (int cliente_id : phi_[item_id]) {
            const auto& itens_cliente = itens_por_cliente_[cliente_id];
            if (std::find(itens_cliente.begin(), itens_cliente.end(), item_id) == itens_cliente.end()) {
                throw std::runtime_error(
                    "Inconsistencia entre [CLIENTES] e [PHI] para item " + std::to_string(item_id)
                );
            }
        }
    }
}
