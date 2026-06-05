# ALNS-VND-CS-LB para Alocacao de Cargas Heterogeneas

Este repositorio contem implementacoes em C++ para o problema de alocacao de cargas heterogeneas, com leitura de instancias. Ha duas abordagens principais:

- metodo exato, formulado e resolvido com Gurobi;
- metodo hibrido, baseado em ALNS com VND, Clustering Search e intensificacao por Local Branching.

As instancias presentes em `Instancias/` foram geradas pelo arquivo `new_instance_generator.py`.

## Dependencias

Para gerar novas instancias:

```bash
pip install -r requirements.txt
```

Para compilar e executar os metodos em C++:

- compilador com suporte a C++17;
- Gurobi instalado, licenciado e configurado;
- biblioteca C++ do Gurobi disponivel nos caminhos de include e link.

O arquivo `requirements.txt` lista apenas as dependencias Python usadas pelo gerador de instancias. A dependencia do Gurobi deve ser configurada no ambiente de compilacao C++.

## Estrutura do repositorio

- `new_instance_generator.py`: gerador das instancias.
- `Instancias/`: instancias geradas em arquivos `.txt`.
- `Instance_um.h` e `Instance_um.cpp`: parser, armazenamento e validacao das instancias UM.
- `main.cpp`: ponto de entrada do metodo exato.
- `Model_um.h` e `Model_um.cpp`: formulacao matematica do metodo exato usando Gurobi.
- `main_alns.cpp`: ponto de entrada do metodo hibrido.
- `ALNS_um.h` e `ALNS_um.cpp`: implementacao do ALNS, VND, Clustering Search e Local Branching.

## Arquivos necessarios por metodo

Metodo exato:

- `main.cpp`
- `Model_um.h`
- `Model_um.cpp`
- `Instance_um.h`
- `Instance_um.cpp`
- uma instancia em `Instancias/*.txt`
- Gurobi C++ API

Metodo hibrido:

- `main_alns.cpp`
- `ALNS_um.h`
- `ALNS_um.cpp`
- `Instance_um.h`
- `Instance_um.cpp`
- uma instancia em `Instancias/*.txt`
- Gurobi C++ API, usada nas etapas de Local Branching

## Instancias

As instancias sao geradas pelo script:

```bash
python new_instance_generator.py --v <veiculos> --r <regioes> --i <demanda_total>
```

Exemplo:

```bash
python new_instance_generator.py --v 20 --r 10 --i 300
```

O gerador grava o arquivo na pasta `Instancias/`, usando o padrao de nome:

```text
instancia_r<regioes>_c<clientes>_v<veiculos>_d<demanda_total>.txt
```

As instancias atuais seguem esse padrao, por exemplo:

- `Instancias/instancia_r10_c48_v20_d300.txt`
- `Instancias/instancia_r20_c193_v30_d500.txt`

## Compilacao

Como o repositorio nao inclui um arquivo de build, compile informando os caminhos locais do Gurobi. Em Linux, um exemplo com `g++` seria:

```bash
g++ -std=c++17 main.cpp Model_um.cpp Instance_um.cpp \
  -I/path/to/gurobi/include \
  -L/path/to/gurobi/lib -lgurobi_c++ -lgurobi \
  -o modelo_exato
```

Para o metodo hibrido:

```bash
g++ -std=c++17 main_alns.cpp ALNS_um.cpp Instance_um.cpp \
  -I/path/to/gurobi/include \
  -L/path/to/gurobi/lib -lgurobi_c++ -lgurobi \
  -o metodo_hibrido
```

Substitua `/path/to/gurobi` pelos caminhos da instalacao local do Gurobi. No Windows, use os caminhos de include e bibliotecas correspondentes da instalacao do Gurobi e o compilador configurado no seu ambiente.

## Execucao

Metodo exato:

```bash
./modelo_exato Instancias/instancia_r10_c48_v20_d300.txt
```

O metodo exato cria a pasta `results_modelo/`, grava o CSV da solucao e salva o log do Gurobi em `results_modelo/logs/`.

Metodo hibrido:

```bash
./metodo_hibrido Instancias/instancia_r10_c48_v20_d300.txt
```

O metodo hibrido cria a pasta `results_alns/` e grava o CSV da melhor solucao encontrada.

## Formato das instancias

Cada arquivo de instancia e dividido em secoes:

- `[METADATA]`
- `[TIPOS_ITENS]`
- `[REGIOES]`
- `[CLIENTES]`
- `[VEICULOS]`
- `[OFERTAS]`
- `[ITENS]`
- `[PHI]`
- `[PSI]`
- `[GAMMA]`
- `[OMEGA]`

O carregamento e a validacao dessas secoes sao feitos por `InstanceUM`, implementada em `Instance_um.h` e `Instance_um.cpp`.
