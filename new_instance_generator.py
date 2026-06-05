import os
import sys
import numpy as np


# Leitura de parametros de linha de comando.
# --v: quantidade de veiculos
# --r: quantidade de regioes
# --i: quantidade total de demandas item-cliente da instancia
argv = sys.argv[1:]

num_veiculos = None
num_regioes = None
demanda_total = None

for i in range(len(argv)):
	if argv[i] == '--v':
		num_veiculos = int(argv[i + 1])
	elif argv[i] == '--r':
		num_regioes = int(argv[i + 1])
	elif argv[i] == '--i':
		demanda_total = int(argv[i + 1])

if num_veiculos is None or num_regioes is None or demanda_total is None:
	raise ValueError("Uso esperado: python new_instance_generator.py --v <veiculos> --r <regioes> --i <demanda_total>")

if demanda_total <= 0:
	raise ValueError("O parametro --i deve ser um inteiro positivo.")


PASTA_SAIDA = os.path.join(os.path.dirname(__file__), 'Instancias')

VEICULOS_BASE = [
	{'tipo': 'Bi-trem Carga Seca', 'capacidade_peso': 57000, 'capacidade_vol': 90, 'custo_base': 1500},
	{'tipo': 'Bi-trem Especializado', 'capacidade_peso': 57000, 'capacidade_vol': 80, 'custo_base': 1800},
	{'tipo': 'Bi-truck', 'capacidade_peso': 33000, 'capacidade_vol': 50, 'custo_base': 1200},
	{'tipo': 'Carreta L', 'capacidade_peso': 33000, 'capacidade_vol': 70, 'custo_base': 1350},
	{'tipo': 'Carreta trucada (LS)', 'capacidade_peso': 45000, 'capacidade_vol': 85, 'custo_base': 1600},
	{'tipo': 'Rodotrem Carga seca', 'capacidade_peso': 74000, 'capacidade_vol': 110, 'custo_base': 2000},
	{'tipo': 'Rodotrem Especializado', 'capacidade_peso': 74000, 'capacidade_vol': 100, 'custo_base': 2200},
	{'tipo': 'Truck', 'capacidade_peso': 23000, 'capacidade_vol': 40, 'custo_base': 1000},
	{'tipo': 'Vanderleia', 'capacidade_peso': 41000, 'capacidade_vol': 75, 'custo_base': 1700},
]

COMPATIBILIDADE_TIPO = {
	'Bi-trem Carga Seca': ['chapa', 'tira', 'perfil', 'tubo'],
	'Bi-trem Especializado': ['chapa', 'perfil'],
	'Bi-truck': ['perfil', 'tubo', 'tira'],
	'Carreta L': ['chapa', 'tira', 'perfil'],
	'Carreta trucada (LS)': ['chapa', 'tira', 'perfil', 'tubo'],
	'Rodotrem Carga seca': ['chapa', 'tira', 'perfil', 'tubo'],
	'Rodotrem Especializado': ['chapa', 'perfil'],
	'Truck': ['perfil', 'tubo'],
	'Vanderleia': ['tira', 'perfil', 'tubo'],
}

MIN_PESO_UM = 500
MAX_PESO_UM = 5000

np.random.seed(42)


def gerar_regioes(total_regioes):
	return [{"id": i} for i in range(1, total_regioes + 1)]


def gerar_clientes(regioes):
	clientes = []
	num_clientes = 0

	for regiao in regioes:
		limite_inferior = max(1, int(np.floor(0.2 * num_regioes)))
		limite_superior = max(limite_inferior + 1, int(np.ceil(0.7 * num_regioes)) + 1)
		num_clientes_regiao = np.random.randint(limite_inferior, limite_superior)

		for j in range(num_clientes + 1, num_clientes + num_clientes_regiao + 1):
			clientes.append({
				"id": j,
				"regiao_id": regiao['id']
			})

		num_clientes += num_clientes_regiao

	return clientes, num_clientes


def gerar_veiculos(total_veiculos, total_regioes):
	veiculos = []

	for i in range(1, total_veiculos + 1):
		tipo_veiculo = np.random.choice(VEICULOS_BASE)
		atendimento = np.random.randint(1, total_regioes + 1)
		regioes_atendidas = sorted(
			np.random.choice(range(1, total_regioes + 1), size=atendimento, replace=False).tolist()
		)

		veiculo = {
			"id": i,
			"tipo": tipo_veiculo['tipo'],
			"capacidade_peso": tipo_veiculo['capacidade_peso'],
			"capacidade_vol": tipo_veiculo['capacidade_vol'],
			"regioes_atendidas": regioes_atendidas,
			"tipos_um_compativeis": COMPATIBILIDADE_TIPO[tipo_veiculo['tipo']],
			"custo": tipo_veiculo['custo_base'] + len(regioes_atendidas) * 150,
			"carga_minima": tipo_veiculo['capacidade_peso'] * 0.5,
		}
		veiculos.append(veiculo)

	return veiculos


def gerar_ofertas(veiculos, total_clientes):
	ums = []
	for veiculo in veiculos:
		for tipo in veiculo['tipos_um_compativeis']:
			if tipo not in ums:
				ums.append(tipo)

	print(f"Tipos de UM para serem gerados: {ums}")

	volumes_ums = {
		"chapa": [4, 10],
		"tira": [2, 6],
		"perfil": [1.5, 5],
		"tubo": [3, 8]
	}

	penalidades = {
		"leve": [0.5, 2.5],
		"media": [2.5, 5],
		"pesada": [5, 7.5],
		"muito_pesada": [7.5, 10]
	}

	distintas_ums = int(max(4, np.ceil(total_clientes / 2)))
	print(f"Numero de tipos de UM a serem gerados: {distintas_ums}")

	ofertas = []
	for i in range(1, distintas_ums + 1):
		tipo_um = np.random.choice(ums)
		peso_um = int(np.random.normal(np.mean([MIN_PESO_UM, MAX_PESO_UM]), 700))
		peso_um = min(MAX_PESO_UM, max(MIN_PESO_UM, peso_um))

		if peso_um < 1000:
			penalidade = np.random.uniform(*penalidades["leve"])
		elif peso_um < 3000:
			penalidade = np.random.uniform(*penalidades["media"])
		elif peso_um < 4000:
			penalidade = np.random.uniform(*penalidades["pesada"])
		else:
			penalidade = np.random.uniform(*penalidades["muito_pesada"])

		volume_um = np.ceil(np.random.uniform(*volumes_ums[tipo_um]))

		ofertas.append({
			"id": i,
			"tipo": str(tipo_um),
			"peso": int(peso_um),
			"volume": float(volume_um),
			"penalidade": round(penalidade, 2)
		})

	return ofertas


def enriquecer_regioes_com_compatibilidade(regioes, veiculos):
	for regiao in regioes:
		veiculos_atendem_regiao = [v['id'] for v in veiculos if regiao['id'] in v['regioes_atendidas']]
		regiao['veiculos_atendem'] = veiculos_atendem_regiao

		ums_compativeis_da_regiao = []
		for veiculo in veiculos:
			if veiculo['id'] in regiao['veiculos_atendem']:
				for tipo_um in veiculo['tipos_um_compativeis']:
					if tipo_um not in ums_compativeis_da_regiao:
						ums_compativeis_da_regiao.append(tipo_um)

		regiao['tipos_um_compativeis'] = ums_compativeis_da_regiao


def criar_itens_base(ofertas, veiculos):
	itens = []

	for oferta in ofertas:
		veiculos_compativeis = [
			veiculo['id'] for veiculo in veiculos
			if oferta['tipo'] in veiculo['tipos_um_compativeis']
		]

		itens.append({
			"id": oferta['id'],
			"oferta_id": oferta['id'],
			"tipo": oferta['tipo'],
			"peso": oferta['peso'],
			"volume": oferta['volume'],
			"penalidade": oferta['penalidade'],
			"veiculos_compativeis": veiculos_compativeis,
			"clientes": []
		})

	return itens


def distribuir_demanda_entre_clientes(clientes, capacidades_clientes, total_demanda):
	"""
	Distribui a demanda total entre os clientes de forma controlada.

	Estrategia adotada:
	1. Se houver demanda suficiente, cada cliente recebe pelo menos 1 item.
	2. O restante e distribuido iterativamente por amostragem ponderada.
	3. Clientes com mais capacidade residual e menor carga atual ficam mais atrativos,
	   o que produz uma distribuicao relativamente balanceada sem ficar artificialmente uniforme.
	"""
	num_clientes = len(clientes)
	demanda_por_cliente = {cliente['id']: 0 for cliente in clientes}

	total_capacidade = sum(capacidades_clientes.values())
	if total_demanda > total_capacidade:
		raise ValueError(
			f"Demanda total inviavel: foram solicitadas {total_demanda} demandas, "
			f"mas a capacidade maxima desta instancia e {total_capacidade}."
		)

	# O formato atual da instancia e o parser C++ assumem que todo cliente listado em
	# [CLIENTES] possui ao menos um item demandado. Portanto, a demanda total precisa
	# ser suficiente para cobrir todos os clientes com pelo menos uma demanda.
	if total_demanda < num_clientes:
		raise ValueError(
			f"Demanda total insuficiente para o formato atual da instancia: "
			f"foram solicitadas {total_demanda} demandas para {num_clientes} clientes. "
			f"Com o parser atual, cada cliente precisa receber ao menos 1 item. "
			f"Use --i >= {num_clientes} ou reduza --r."
		)

	demanda_restante = total_demanda

	# Fase 1: garante uma cobertura minima de clientes enquanto houver demanda suficiente.
	if demanda_restante >= num_clientes:
		for cliente in clientes:
			if capacidades_clientes[cliente['id']] > 0:
				demanda_por_cliente[cliente['id']] += 1
				demanda_restante -= 1

	# Fase 2: distribui o restante preservando variacao, mas com tendencia ao equilibrio.
	while demanda_restante > 0:
		clientes_elegiveis = [
			cliente for cliente in clientes
			if demanda_por_cliente[cliente['id']] < capacidades_clientes[cliente['id']]
		]

		if not clientes_elegiveis:
			raise ValueError("Nao foi possivel distribuir toda a demanda entre os clientes elegiveis.")

		pesos = []
		for cliente in clientes_elegiveis:
			carga_atual = demanda_por_cliente[cliente['id']]
			capacidade_total_cliente = capacidades_clientes[cliente['id']]
			capacidade_residual = capacidade_total_cliente - carga_atual

			# Clientes com capacidade residual alta e carga atual baixa recebem maior peso.
			peso = capacidade_residual / (1.0 + carga_atual)
			pesos.append(peso)

		pesos = np.array(pesos, dtype=float)
		pesos = pesos / pesos.sum()
		cliente_escolhido = np.random.choice(clientes_elegiveis, p=pesos)
		demanda_por_cliente[cliente_escolhido['id']] += 1
		demanda_restante -= 1

	return demanda_por_cliente


def selecionar_itens_para_cliente(item_ids_compativeis, quantidade, popularidade_itens):
	"""
	Seleciona os itens de um cliente sem repeticao.

	Os pesos de popularidade geram concentracao moderada em alguns itens,
	mas sem impedir que itens menos populares tambem aparecam.
	"""
	pesos = np.array([popularidade_itens[item_id] for item_id in item_ids_compativeis], dtype=float)
	pesos = pesos / pesos.sum()
	return np.random.choice(item_ids_compativeis, size=quantidade, replace=False, p=pesos).tolist()


def gerar_itens(clientes, regioes, veiculos, ofertas, total_demanda):
	itens = criar_itens_base(ofertas, veiculos)
	itens_por_id = {item['id']: item for item in itens}
	clientes_por_id = {cliente['id']: cliente for cliente in clientes}
	regioes_por_id = {regiao['id']: regiao for regiao in regioes}

	# Cada item recebe um escore de popularidade. Isso evita distribuicao totalmente uniforme.
	popularidade_itens = {
		item['id']: np.random.gamma(shape=2.0, scale=1.0)
		for item in itens
	}

	capacidades_clientes = {}
	itens_compativeis_por_cliente = {}

	# Para cada cliente, montamos previamente a lista de itens compativeis com sua regiao.
	for cliente in clientes:
		regiao_cliente = regioes_por_id[cliente['regiao_id']]
		item_ids_compativeis = [
			item['id'] for item in itens
			if item['tipo'] in regiao_cliente['tipos_um_compativeis']
		]

		if not item_ids_compativeis:
			raise ValueError(
				f"O cliente {cliente['id']} da regiao {cliente['regiao_id']} nao possui itens compativeis."
			)

		itens_compativeis_por_cliente[cliente['id']] = item_ids_compativeis
		capacidades_clientes[cliente['id']] = len(item_ids_compativeis)
		cliente['itens'] = []

	demanda_por_cliente = distribuir_demanda_entre_clientes(
		clientes,
		capacidades_clientes,
		total_demanda
	)

	# Depois de definir quantas demandas cada cliente recebe, selecionamos quais itens entrarao.
	for cliente in clientes:
		quantidade_cliente = demanda_por_cliente[cliente['id']]
		if quantidade_cliente <= 0:
			continue

		item_ids_escolhidos = selecionar_itens_para_cliente(
			itens_compativeis_por_cliente[cliente['id']],
			quantidade_cliente,
			popularidade_itens
		)

		cliente['itens'].extend(item_ids_escolhidos)
		for item_id in item_ids_escolhidos:
			itens_por_id[item_id]['clientes'].append(cliente['id'])

	# Garantimos que todo item tenha pelo menos um cliente demandante sem alterar a demanda total.
	# Para isso, realocamos demandas de itens com muitos clientes para itens ainda vazios.
	for item in itens:
		if item['clientes']:
			continue

		realocacao_feita = False

		for cliente in sorted(clientes, key=lambda c: len(c['itens']), reverse=True):
			if item['tipo'] not in regioes_por_id[cliente['regiao_id']]['tipos_um_compativeis']:
				continue
			if item['id'] in cliente['itens']:
				continue

			itens_do_cliente = list(cliente['itens'])
			for item_doador_id in itens_do_cliente:
				item_doador = itens_por_id[item_doador_id]

				# So retiramos um item de um cliente se o item doador ainda continuar demandado por outro cliente.
				if len(item_doador['clientes']) <= 1:
					continue

				cliente['itens'].remove(item_doador_id)
				item_doador['clientes'].remove(cliente['id'])
				cliente['itens'].append(item['id'])
				item['clientes'].append(cliente['id'])
				realocacao_feita = True
				break

			if realocacao_feita:
				break

		if not realocacao_feita:
			raise ValueError(
				f"Nao foi possivel realocar demanda para garantir cliente ao item {item['id']}."
			)

	# Como a demanda total e controlada externamente, removemos apenas duplicidades defensivas
	# e mantemos a representacao exatamente igual a do gerador atual.
	for item in itens:
		item['clientes'] = sorted(set(item['clientes']))

	for cliente in clientes:
		cliente['itens'] = sorted(set(cliente['itens']))

	return itens


def garantir_cobertura_total_regioes(veiculos, total_regioes):
	regioes_atendidas = set()
	for veiculo in veiculos:
		regioes_atendidas.update(veiculo['regioes_atendidas'])

	regioes_sem_cobertura = [
		regiao_id for regiao_id in range(1, total_regioes + 1)
		if regiao_id not in regioes_atendidas
	]

	for regiao_id in regioes_sem_cobertura:
		veiculo_escolhido = min(veiculos, key=lambda veiculo: len(veiculo['regioes_atendidas']))
		veiculo_escolhido['regioes_atendidas'].append(regiao_id)
		veiculo_escolhido['regioes_atendidas'].sort()
		veiculo_escolhido['custo'] += 150

	return regioes_sem_cobertura


def exportar_instancia_txt(caminho_saida, regioes, clientes, veiculos, ofertas, itens):
	"""
	Exporta a instancia para um arquivo texto estruturado em secoes.

	O arquivo gerado usa indices baseados em 0, mesmo que os dados em memoria
	tenham ids iniciando em 1. Isso facilita a leitura no codigo em C++.

	Formato exportado:

	[METADATA]
	NUM_REGIOES=<quantidade_regioes>
	NUM_CLIENTES=<quantidade_clientes>
	NUM_VEICULOS=<quantidade_veiculos>
	NUM_OFERTAS=<quantidade_ofertas>
	NUM_ITENS=<quantidade_itens>

	[TIPOS_ITENS]
	tipo_codigo;tipo_texto

	[REGIOES]
	regiao_id

	[CLIENTES]
	cliente_id;item_1,item_2,...

	[VEICULOS]
	veiculo_id;capacidade_peso;capacidade_volume;custo;carga_minima

	[OFERTAS]
	oferta_id;tipo_codigo;peso;volume;penalidade

	[ITENS]
	item_id;oferta_id;tipo_codigo;peso;volume;penalidade

	[PHI]
	item_id;quantidade_clientes;cliente_1,cliente_2,...

	[PSI]
	item_id;quantidade_veiculos;veiculo_1,veiculo_2,...

	[GAMMA]
	veiculo_id;quantidade_clientes;cliente_1,cliente_2,...

	[OMEGA]
	veiculo_id;quantidade_itens;item_1,item_2,...
	"""
	os.makedirs(os.path.dirname(caminho_saida), exist_ok=True)

	def lista_para_texto(valores):
		return ",".join(str(v) for v in valores)

	regiao_idx = {regiao['id']: idx for idx, regiao in enumerate(regioes)}
	cliente_idx = {cliente['id']: idx for idx, cliente in enumerate(clientes)}
	veiculo_idx = {veiculo['id']: idx for idx, veiculo in enumerate(veiculos)}
	oferta_idx = {oferta['id']: idx for idx, oferta in enumerate(ofertas)}
	item_idx = {item['id']: idx for idx, item in enumerate(itens)}

	tipos_itens = {'chapa': 0, 'tira': 1, 'perfil': 2, 'tubo': 3}

	with open(caminho_saida, "w", encoding="utf-8") as arquivo:
		arquivo.write("[METADATA]\n")
		arquivo.write(f"NUM_REGIOES={len(regioes)}\n")
		arquivo.write(f"NUM_CLIENTES={len(clientes)}\n")
		arquivo.write(f"NUM_VEICULOS={len(veiculos)}\n")
		arquivo.write(f"NUM_OFERTAS={len(ofertas)}\n")
		arquivo.write(f"NUM_ITENS={len(itens)}\n\n")

		arquivo.write("[TIPOS_ITENS]\n")
		for tipo_texto, tipo_codigo in tipos_itens.items():
			arquivo.write(f"{tipo_codigo};{tipo_texto}\n")
		arquivo.write("\n")

		arquivo.write("[REGIOES]\n")
		for regiao in regioes:
			arquivo.write(f"{regiao_idx[regiao['id']]}\n")
		arquivo.write("\n")

		arquivo.write("[CLIENTES]\n")
		for cliente in clientes:
			itens_cliente = [item_idx[item_id] for item_id in cliente['itens']]
			arquivo.write(f"{cliente_idx[cliente['id']]};{lista_para_texto(itens_cliente)}\n")
		arquivo.write("\n")

		arquivo.write("[VEICULOS]\n")
		for veiculo in veiculos:
			arquivo.write(
				f"{veiculo_idx[veiculo['id']]};{veiculo['capacidade_peso']};"
				f"{veiculo['capacidade_vol']};{veiculo['custo']};{veiculo['carga_minima']}\n"
			)
		arquivo.write("\n")

		arquivo.write("[OFERTAS]\n")
		for oferta in ofertas:
			arquivo.write(
				f"{oferta_idx[oferta['id']]};{tipos_itens[oferta['tipo']]};{oferta['peso']};"
				f"{oferta['volume']};{oferta['penalidade']}\n"
			)
		arquivo.write("\n")

		arquivo.write("[ITENS]\n")
		for item in itens:
			arquivo.write(
				f"{item_idx[item['id']]};{oferta_idx[item['oferta_id']]};"
				f"{tipos_itens[item['tipo']]};{item['peso']};{item['volume']};{item['penalidade']}\n"
			)
		arquivo.write("\n")

		arquivo.write("[PHI]\n")
		for item in itens:
			clientes_phi = [cliente_idx[cliente_id] for cliente_id in item['clientes']]
			arquivo.write(f"{item_idx[item['id']]};{len(clientes_phi)};{lista_para_texto(clientes_phi)}\n")
		arquivo.write("\n")

		arquivo.write("[PSI]\n")
		for item in itens:
			veiculos_psi = [veiculo_idx[v] for v in item['veiculos_compativeis']]
			arquivo.write(f"{item_idx[item['id']]};{len(veiculos_psi)};{lista_para_texto(veiculos_psi)}\n")
		arquivo.write("\n")

		arquivo.write("[GAMMA]\n")
		for veiculo in veiculos:
			clientes_gamma = [
				cliente_idx[cliente['id']]
				for cliente in clientes
				if cliente['regiao_id'] in veiculo['regioes_atendidas']
			]
			arquivo.write(
				f"{veiculo_idx[veiculo['id']]};{len(clientes_gamma)};{lista_para_texto(clientes_gamma)}\n"
			)
		arquivo.write("\n")

		arquivo.write("[OMEGA]\n")
		for veiculo in veiculos:
			itens_omega = [
				item_idx[item['id']]
				for item in itens
				if veiculo['id'] in item['veiculos_compativeis']
			]
			arquivo.write(
				f"{veiculo_idx[veiculo['id']]};{len(itens_omega)};{lista_para_texto(itens_omega)}\n"
			)


regioes = gerar_regioes(num_regioes)
clientes, num_clientes = gerar_clientes(regioes)

print(f"Numero total de clientes gerados: {num_clientes}")
print("Numero de clientes por regiao:")
for cliente in clientes:
	print(f"Regiao {cliente['regiao_id']}: Cliente {cliente['id']}")

veiculos = gerar_veiculos(num_veiculos, num_regioes)
regioes_sem_cobertura = garantir_cobertura_total_regioes(veiculos, num_regioes)
if regioes_sem_cobertura:
	print(
		f"Atencao: As regioes {regioes_sem_cobertura} estavam sem atendimento e foram associadas a veiculos existentes."
	)

print(f"Todas as {num_regioes} regioes estao sendo atendidas por veiculos.")

for veiculo in veiculos:
	print(
		f"Veiculo {veiculo['id']} - Tipo: {veiculo['tipo']} - Regioes atendidas: {veiculo['regioes_atendidas']} "
		f"- Tipos de UM compativeis: {veiculo['tipos_um_compativeis']} - Custo: {veiculo['custo']}"
	)

ofertas = gerar_ofertas(veiculos, num_clientes)
for oferta in ofertas:
	print(
		f"Oferta {oferta['id']} - Tipo: {oferta['tipo']} - Peso: {oferta['peso']} kg "
		f"- Volume: {oferta['volume']} m3 - Penalidade: {oferta['penalidade']:.2f}"
	)

enriquecer_regioes_com_compatibilidade(regioes, veiculos)
for regiao in regioes:
	print(f"Regiao {regiao['id']} - Veiculos que atendem: {regiao['veiculos_atendem']}.")
	print(f"Regiao {regiao['id']} - Tipos de UM compativeis: {regiao['tipos_um_compativeis']}.")

itens = gerar_itens(clientes, regioes, veiculos, ofertas, demanda_total)

total_demanda_gerada = sum(len(cliente['itens']) for cliente in clientes)
print(f"Demanda total solicitada: {demanda_total}")
print(f"Demanda total efetivamente gerada: {total_demanda_gerada}")

print(f"\nRegiao 1: {regioes[0]}")
print(f"Veiculo 1: {veiculos[0]}")
print(f"Oferta 1: {ofertas[0]}")
print(f"Item 1: {itens[0]}")
print(f"Cliente 1: {clientes[0]}\n")

os.makedirs(PASTA_SAIDA, exist_ok=True)
print(f"Pasta de saida garantida em: {PASTA_SAIDA}")
nome_arquivo = (
	f"instancia_r{len(regioes)}_c{len(clientes)}_v{len(veiculos)}_d{demanda_total}.txt"
)
caminho_instancia = os.path.join(PASTA_SAIDA, nome_arquivo)
exportar_instancia_txt(caminho_instancia, regioes, clientes, veiculos, ofertas, itens)
print(f"Instancia exportada em: {caminho_instancia}")
