#ifndef INSTANCE_UM_H
#define INSTANCE_UM_H

#include <string>
#include <unordered_map>
#include <vector>

// Classe responsavel por carregar, armazenar e validar uma instancia no formato UM.
class InstanceUM {
public:
    // Resumo das quantidades declaradas no cabecalho da instancia.
    struct Metadata {
        int num_regioes = 0;
        int num_clientes = 0;
        int num_veiculos = 0;
        int num_ofertas = 0;
        int num_itens = 0;
    };

    // Dados de um veiculo disponivel para alocacao.
    struct Veiculo {
        int id = -1;
        int capacidade_peso = 0;
        int capacidade_volume = 0;
        int custo = 0;
        double carga_minima = 0.0;
    };

    // Dados de uma oferta de transporte para um tipo de item.
    struct Oferta {
        int id = -1;
        int tipo_codigo = -1;
        int peso = 0;
        double volume = 0.0;
        double penalidade = 0.0;
    };

    // Dados consolidados de um item/entrega individual.
    struct Item {
        int id = -1;
        int oferta_id = -1;
        int tipo_codigo = -1;
        int peso = 0;
        double volume = 0.0;
        double penalidade = 0.0;
        int demanda_total = 0;
    };

    // Carrega toda a instancia de arquivo texto para as estruturas internas.
    // path: caminho do arquivo de instancia.
    // error_message: ponteiro opcional para receber a mensagem detalhada em caso de falha.
    // Retorna true em caso de sucesso; false em caso de erro de parse/validacao.
    bool loadFromFile(const std::string& path, std::string* error_message = nullptr);

    // Acesso somente leitura ao bloco [METADATA].
    const Metadata& getMetadata() const;
    // Acesso ao mapeamento tipo_codigo -> nome do tipo ([TIPOS_ITENS]).
    const std::unordered_map<int, std::string>& getTiposItens() const;
    // Acesso a lista de regioes declaradas ([REGIOES]).
    const std::vector<int>& getRegioes() const;
    // Acesso a lista de itens por cliente ([CLIENTES]).
    const std::vector<std::vector<int>>& getItensPorCliente() const;
    // Acesso a lista de veiculos ([VEICULOS]).
    const std::vector<Veiculo>& getVeiculos() const;
    // Acesso a lista de ofertas ([OFERTAS]).
    const std::vector<Oferta>& getOfertas() const;
    // Acesso a lista de itens ([ITENS]).
    const std::vector<Item>& getItens() const;
    // Acesso a relacao item -> clientes demandantes ([PHI]).
    const std::vector<std::vector<int>>& getPhi() const;
    // Acesso a relacao item -> veiculos permitidos ([PSI]).
    const std::vector<std::vector<int>>& getPsi() const;
    // Acesso a relacao veiculo -> clientes permitidos ([GAMMA]).
    const std::vector<std::vector<int>>& getGamma() const;
    // Acesso a relacao veiculo -> itens permitidos ([OMEGA]).
    const std::vector<std::vector<int>>& getOmega() const;

    // Busca o nome textual de um tipo de item.
    // tipo_codigo: codigo inteiro do tipo cadastrado em [TIPOS_ITENS].
    // Lanca excecao se o codigo nao existir.
    const std::string& getTipoNome(int tipo_codigo) const;

private:
    // Estruturas internas que espelham as secoes do arquivo de instancia.
    Metadata metadata_;
    std::unordered_map<int, std::string> tipos_itens_;
    std::vector<int> regioes_;
    std::vector<std::vector<int>> itens_por_cliente_;
    std::vector<Veiculo> veiculos_;
    std::vector<Oferta> ofertas_;
    std::vector<Item> itens_;
    std::vector<std::vector<int>> phi_;
    std::vector<std::vector<int>> psi_;
    std::vector<std::vector<int>> gamma_;
    std::vector<std::vector<int>> omega_;

    // Limpa completamente o estado da instancia para um novo carregamento.
    void clear();

    // Remove espacos no inicio e no fim de uma string.
    static std::string trim(const std::string& text);
    // Verifica se a linha representa um cabecalho de secao no formato [SECAO].
    static bool isSectionHeader(const std::string& line);
    // Divide texto por delimitador sem remover espacos automaticamente.
    static std::vector<std::string> split(const std::string& text, char delimiter);
    // Converte string para int e adiciona nome do campo em mensagens de erro.
    static int toInt(const std::string& text, const std::string& field_name);
    // Converte string para double e adiciona nome do campo em mensagens de erro.
    static double toDouble(const std::string& text, const std::string& field_name);
    // Converte lista separada por virgula para vetor de inteiros.
    static std::vector<int> parseIntList(const std::string& text);
    // Garante ids sequenciais iniciando em 0 para manter consistencia do indice.
    // actual_id: id lido da linha atual.
    // expected_id: id esperado com base no tamanho atual da colecao.
    // label: nome da entidade para mensagem de erro.
    static void ensureSequentialId(int actual_id, int expected_id, const std::string& label);

    // Direciona cada linha para o parser da secao correspondente.
    // section: nome da secao atual (ex.: [ITENS]).
    // line: conteudo bruto da linha da secao.
    void parseSectionLine(const std::string& section, const std::string& line);
    // Parse de linhas KEY=VALUE da secao [METADATA].
    void parseMetadata(const std::string& line);
    // Parse de linhas codigo;nome da secao [TIPOS_ITENS].
    void parseTiposItens(const std::string& line);
    // Parse de linhas com identificador de regiao da secao [REGIOES].
    void parseRegioes(const std::string& line);
    // Parse de linhas cliente_id;item_id,item_id,... da secao [CLIENTES].
    void parseClientes(const std::string& line);
    // Parse de linhas de veiculos da secao [VEICULOS].
    void parseVeiculos(const std::string& line);
    // Parse de linhas de ofertas da secao [OFERTAS].
    void parseOfertas(const std::string& line);
    // Parse de linhas de itens da secao [ITENS].
    void parseItens(const std::string& line);
    // Parse de linhas item_id;cliente_id da secao [PHI].
    void parsePhi(const std::string& line);
    // Parse de linhas item_id;quantidade;veiculo_id,... da secao [PSI].
    void parsePsi(const std::string& line);
    // Parse de linhas veiculo_id;quantidade;cliente_id,... da secao [GAMMA].
    void parseGamma(const std::string& line);
    // Parse de linhas veiculo_id;quantidade;item_id,... da secao [OMEGA].
    void parseOmega(const std::string& line);

    // Executa validacoes de consistencia cruzada apos o carregamento completo.
    void validate() const;
};

#endif
