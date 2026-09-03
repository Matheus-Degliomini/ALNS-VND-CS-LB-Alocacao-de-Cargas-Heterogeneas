### Run Executable with all instances on Instancias folder
run:
	./metodo_hibrido Instancias/instancia_r10_c48_v20_d300.txt
	./metodo_hibrido Instancias/instancia_r10_c48_v20_d400.txt
	./metodo_hibrido Instancias/instancia_r10_c48_v20_d500.txt
	./metodo_hibrido Instancias/instancia_r10_c48_v30_d300.txt
	./metodo_hibrido Instancias/instancia_r10_c48_v30_d400.txt
	./metodo_hibrido Instancias/instancia_r10_c48_v30_d500.txt
	./metodo_hibrido Instancias/instancia_r20_c193_v20_d300.txt
	./metodo_hibrido Instancias/instancia_r20_c193_v20_d400.txt
	./metodo_hibrido Instancias/instancia_r20_c193_v20_d500.txt
	./metodo_hibrido Instancias/instancia_r20_c193_v30_d300.txt
	./metodo_hibrido Instancias/instancia_r20_c193_v30_d400.txt
	./metodo_hibrido Instancias/instancia_r20_c193_v30_d500.txt

	@echo "Running main with all instances in Instancias folder"


### Run all instances with seeds 1 to 30
run_all_seeds:
	@for instance in Instancias/*.txt; do \
		for seed in $$(seq 1 30); do \
			echo "Running $$instance with seed $$seed"; \
			./metodo_hibrido "$$instance" $$seed; \
		done \
	done

	@echo "All executions completed!"


### Run all instances with seeds 1 to 30 in parallel
INSTANCES := $(wildcard Instancias/*.txt)
SEEDS := $(shell seq 1 30)

run_all_seeds_parallel:
	@for instance in $(INSTANCES); do \
		for seed in $(SEEDS); do \
			echo "$$instance $$seed"; \
		done \
	done | xargs -n 2 -P 8 sh -c './metodo_hibrido "$$0" "$$1"'

	@echo "All parallel executions completed!"


.PHONY: run run_all_seeds run_all_seeds_parallel
