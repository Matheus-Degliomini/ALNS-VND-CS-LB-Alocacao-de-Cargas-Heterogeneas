# Alocação de Cargas Heterogêneas — ALNS, VND, CS, LB e DM

Repositório de otimização da alocação de cargas heterogêneas a uma frota de veículos. Contém uma formulação de programação inteira mista em C++ com Gurobi, uma metaheurística híbrida e uma extensão com mineração de padrões de soluções de elite.

## Problema tratado

Cada demanda é um par **item–cliente**. Os itens têm peso, volume e penalidade de não atendimento; os veículos têm capacidades de peso e volume, custo de utilização e carga mínima. A alocação respeita as compatibilidades entre itens, veículos e clientes.

O objetivo é minimizar a soma de:

- custos dos veículos utilizados;
- frete morto: peso faltante para atingir a carga mínima de um veículo utilizado;
- penalidades das demandas não atendidas, calculadas como peso do item × penalidade.

A carga mínima gera um custo de frete morto, sem obrigar o veículo a atingir esse peso. Cada par item–cliente pode ser atendido por, no máximo, um veículo. O código resolve a alocação das cargas; não calcula sequências de visitas ou rotas.

## Organização

| Diretório | Conteúdo |
| --- | --- |
| `ALNS-VND-CS-LB/` | Modelo exato e método híbrido base. |
| `ALNS-VND-CS-LB-DM/` | Modelo exato e método híbrido com Data Mining, sementes por linha de comando, Makefile de experimentos e resultados CSV. |

Em cada variante:

| Arquivo ou pasta | Função |
| --- | --- |
| `Instance_um.h`, `Instance_um.cpp` | Leitura, armazenamento e validação das instâncias. |
| `Model_um.h`, `Model_um.cpp` | Variáveis, objetivo e restrições do modelo Gurobi. |
| `main.cpp` | Entrada do modelo exato. |
| `ALNS_um.h`, `ALNS_um.cpp` | Metaheurística e intensificação com Gurobi. |
| `main_alns.cpp` | Entrada e configuração do método híbrido. |
| `new_instance_generator.py` | Gerador de instâncias sintéticas. |
| `requirements.txt` | Dependência Python: `numpy>=1.24`. |
| `Instancias/` | Arquivos de entrada em texto. |

Os executáveis presentes na variante DM não substituem a compilação para o ambiente local. O Makefile dessa pasta contém alvos de execução, sem alvos de compilação.

## Métodos

### Modelo exato

Usa variáveis binárias de alocação `x[i,v,c]` e ativação de veículos `y[v]`, além de variáveis contínuas não negativas de frete morto `z[v]`. As restrições controlam capacidade, ativação, compatibilidade, demanda total e unicidade do atendimento.

Na configuração atual de `Model_um.cpp`, o limite de execução é **3.600 segundos**. Presolve, agregação, cortes e heurísticas internas estão desativados por chamadas explícitas, independentemente da macro `GUROBI_DISABLE_INTERNALS`, cujas diretivas estão comentadas. Uma execução com limite de tempo não garante uma solução ótima.

### Método híbrido

1. Constrói uma solução inicial gulosa.
2. Aplica **ALNS (Adaptive Large Neighborhood Search)**, selecionando adaptativamente operadores de destruição e reparação. Os reparos também consideram demandas ainda não atendidas.
3. Melhora candidatas com **VND (Variable Neighborhood Descent)**: inserções, realocações, fechamento de veículos, trocas e ejeções.
4. Aceita candidatas usando temperatura e resfriamento, permitindo explorar soluções de custo maior.
5. Usa **CS (Clustering Search)** para agrupar soluções pelo padrão de veículos ativos e identificar regiões para intensificação.
6. Resolve vizinhanças com **LB (Local Branching)** no Gurobi. A distância limita mudanças na frota ativa ou no atendimento das demandas, conforme a frota do centro.
7. Executa uma intensificação final, quando habilitada.

Há quatro operadores de destruição (aleatório, pior veículo, demandas de alta penalidade e veículos com alto frete morto) e quatro de reparação (melhor inserção gulosa, ordem aleatória, regret e melhor ganho líquido).

Na variante **DM (Data Mining)**, um pool de elite mantém soluções distintas. Padrões com suporte suficiente fixam variáveis de alocação e ativação antes do Local Branching, restringindo o espaço de busca. Essa etapa não fornece garantia de ótimo global.

## Requisitos e compilação

- Compilador C++ com suporte a **C++17**.
- Gurobi instalado, API C++ disponível e licença válida para as chamadas ao solver.
- Python 3 e NumPy para gerar novas instâncias.

O método híbrido também depende da API C++ do Gurobi. Execute os comandos abaixo a partir da raiz e ajuste o caminho da instalação e o nome da biblioteca principal à sua versão:

```bash
cd ALNS-VND-CS-LB-DM
export GUROBI_HOME="/caminho/para/gurobi/linux64"
# Exemplo: gurobi130 para uma instalação que fornece libgurobi130.so.
export GUROBI_LIB="gurobi130"

g++ -O2 -std=c++17 main.cpp Model_um.cpp Instance_um.cpp \
  -I"$GUROBI_HOME/include" -L"$GUROBI_HOME/lib" \
  -Wl,-rpath,"$GUROBI_HOME/lib" -lgurobi_c++ -l"$GUROBI_LIB" \
  -o modelo_exato

g++ -O2 -std=c++17 main_alns.cpp ALNS_um.cpp Instance_um.cpp \
  -I"$GUROBI_HOME/include" -L"$GUROBI_HOME/lib" \
  -Wl,-rpath,"$GUROBI_HOME/lib" -lgurobi_c++ -l"$GUROBI_LIB" \
  -o metodo_hibrido
```

Para compilar a versão base, use os mesmos comandos dentro de `ALNS-VND-CS-LB/`. Em outros sistemas, adapte os caminhos e as bibliotecas ao compilador e à instalação do Gurobi.

## Execução

Dentro da pasta da variante compilada:

```bash
./modelo_exato Instancias/instancia_r10_c48_v20_d300.txt
./metodo_hibrido Instancias/instancia_r10_c48_v20_d300.txt
```

A variante DM permite informar uma semente adicional:

```bash
./metodo_hibrido Instancias/instancia_r10_c48_v20_d300.txt 1
```

A semente padrão é **42** nas duas variantes. Na versão base, altere `parameters.random_seed` em `main_alns.cpp` para usar outra semente; ela não é lida pela linha de comando.

### Experimentos em lote

Após compilar `metodo_hibrido`, na pasta `ALNS-VND-CS-LB-DM/`:

```bash
make run                     # Todas as 12 instâncias, com a semente padrão
make run_all_seeds           # Todas as instâncias, sementes de 1 a 30
make run_all_seeds_parallel  # Mesmo lote, com até 8 processos simultâneos
```

O paralelismo se aplica a processos completos, que também podem chamar o Gurobi; dimensione-o conforme os recursos disponíveis.

## Parâmetros do híbrido

Os parâmetros são definidos em `ALNSUM::Parameters` (`ALNS_um.h`) e configurados em `main_alns.cpp`. Além da semente na versão DM, não há interface de linha de comando para alterá-los; edite o código e recompile.

| Parâmetro | Valor configurado |
| --- | --- |
| Máximo de iterações | 10.000 |
| Máximo de iterações sem melhoria | 1.000 |
| Segmento de atualização dos pesos | 50 |
| Intervalo solicitado de remoções | 10–40 |
| Temperatura inicial / fator de resfriamento | 5.000 / 0,995 |
| Fator de reação dos pesos | 0,25 |
| Número de clusters / volume de ativação | 8 / 20 |
| Raios de LB: frota / atendimento | 8 / 20 |
| Limite por chamada de LB | 10 s |
| Raios finais: frota / atendimento | 12 / 40 |
| Limite por chamada de LB final | 30 s |
| Pool de elite DM: máximo / mínimo para mineração | 30 / 25 |
| Suporte mínimo DM | 0,75 |

VND, CS e intensificação final estão habilitados; a mineração de padrões também está habilitada por padrão na variante DM. Os limites do Local Branching são por chamada, não um limite global de duração do ALNS.

## Instâncias e geração de dados

Cada variante inclui 12 instâncias: combinações de 10 ou 20 regiões, 20 ou 30 veículos e 300, 400 ou 500 demandas item–cliente. As instâncias incluídas têm 48 clientes para 10 regiões e 193 clientes para 20 regiões.

Para gerar uma instância dentro de uma variante:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
python3 new_instance_generator.py --v 20 --r 10 --i 300
```

`--i` representa o total de pares item–cliente demandados, não a quantidade de itens distintos. O número de clientes é gerado pelo script. A semente do gerador está fixada em `np.random.seed(42)` e não é a semente da metaheurística.

O arquivo é salvo em `Instancias/`, ao lado do script, com o nome:

```text
instancia_r<regioes>_c<clientes>_v<veiculos>_d<demanda_total>.txt
```

Gerar novamente um arquivo com o mesmo nome substitui seu conteúdo.

### Formato de entrada

Os arquivos usam seções, campos separados por ponto e vírgula e listas separadas por vírgula. Os identificadores das entidades indexadas pelo parser são sequenciais, iniciando em zero.

| Seção | Dados |
| --- | --- |
| `[METADATA]` | Quantidades de regiões, clientes, veículos, ofertas e itens. |
| `[TIPOS_ITENS]` | Códigos e nomes dos tipos de carga. |
| `[REGIOES]` | Regiões. |
| `[CLIENTES]` | Itens demandados por cliente. |
| `[VEICULOS]` | Capacidades, custo e carga mínima. |
| `[OFERTAS]` | Tipo, peso, volume e penalidade das ofertas. |
| `[ITENS]` | Dados dos itens e demanda total. |
| `[PHI]` | Clientes que demandam cada item. |
| `[PSI]` | Veículos compatíveis com cada item. |
| `[GAMMA]` | Clientes permitidos para cada veículo. |
| `[OMEGA]` | Itens permitidos para cada veículo. |

`InstanceUM::loadFromFile` realiza o parsing e a validação de consistência. Consulte as instâncias existentes e `Instance_um.cpp` para o formato detalhado de cada linha.

## Resultados

As pastas de saída são criadas no **diretório de trabalho da execução**:

| Método | Saída |
| --- | --- |
| Exato | `results_modelo/modelo_<instancia>.csv` |
| Log do exato | `results_modelo/logs/gurobi_<instancia>.txt` |
| Híbrido base | `results_alns/alns_<instancia>.csv` |
| Híbrido DM | `results_alns/alns_<instancia>_seed<seed>.csv` |

Os CSVs do híbrido contêm registros de resumo, veículos, atendimento das demandas e estatísticas dos operadores. A variante DM acrescenta contadores de mineração e fixação de variáveis. O console também informa tempos por etapa e motivo de parada. Demandas não atendidas são exportadas com veículo `-1`.

O CSV do modelo exato contém resumo e registros dos veículos quando o status é `GRB_OPTIMAL` ou `GRB_SUBOPTIMAL`. A implementação atual não exporta os detalhes de uma solução incumbente em outros status, inclusive `GRB_TIME_LIMIT`; nesses casos, consulte o log do solver para interpretar a execução.

Execuções repetidas com a mesma instância e, na versão DM, a mesma semente usam o mesmo caminho de saída e substituem o CSV anterior. Para comparar variantes, use os mesmos dados de entrada e registre sementes e parâmetros utilizados.
