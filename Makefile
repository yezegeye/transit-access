# Rebuild everything from a clean clone (inside the `transit` conda env):
#
#   make            build, run all tests, download data, regenerate every result
#   make results    just the analysis (data, job access, maps, equity, sensitivity)
#   make benchmark  C++ vs. Python timings and the full-workload timing

PYTHON ?= python
BUILD  ?= build

.PHONY: all build test data results benchmark clean

all: build test results

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD) \
	  -DPython_EXECUTABLE="$$(which $(PYTHON))" \
	  -Dpybind11_DIR="$$($(PYTHON) -m pybind11 --cmakedir)"

build: $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD) -j

test: build
	ctest --test-dir $(BUILD) --output-on-failure
	$(PYTHON) -m pytest -q

data:
	$(PYTHON) analysis/prep_data.py

results: build data
	$(PYTHON) analysis/job_access.py
	$(PYTHON) analysis/plot_job_access.py
	$(PYTHON) analysis/equity.py
	$(PYTHON) analysis/sensitivity.py

benchmark: build
	$(PYTHON) analysis/benchmark.py
	$(BUILD)/bench_matrix data/raw/cta/gtfs 20261118 data/processed/cook_tracts.csv 793 0

clean:
	rm -rf $(BUILD) data/processed/*.gpkg data/processed/*.csv data/processed/*.npz
