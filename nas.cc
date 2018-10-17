//------------------------------------------------------------------------------
// License: MIT
// Copyright: H2O.ai 2018
//
// (some of the code borrowed from https://github.com/wesm/bitmaps-vs-sentinels,
//  licensed MIT)
//------------------------------------------------------------------------------
#include <chrono>
#include <iostream>
#include <random>
#include <getopt.h>  // option
#include <unistd.h>  // getopt_long
#include <stdlib.h>  // atol

using T = int32_t;


struct config {
  size_t seed;
  size_t n;
  double p;

  config() {
    seed = 1;
    n = 1000000;
    p = 0.1;
  }

  void parse(int argc, char** argv) {
    struct option longopts[] = {
      {"seed", 1, 0, 0},
      {"n", 1, 0, 0},
      {"p", 1, 0, 0},
      // {"nthreads", 1, 0, 0},
      {nullptr, 0, nullptr, 0}  // sentinel
    };

    while (1) {
      int option_index;
      int ret = getopt_long(argc, argv, "", longopts, &option_index);
      if (ret == -1) break;
      if (ret == 0) {
        if (optarg) {
          if (option_index == 0) seed = atol(optarg);
          if (option_index == 1) n = atol(optarg);
          if (option_index == 2) p = strtod(optarg, nullptr);
        }
      }
    }
  }

  void report() {
    printf("\nInput parameters:\n");
    printf("  seed     = %zu\n", seed);
    printf("  n        = %zu\n", n);
    printf("  p        = %f\n", p);
    // printf("  nthreads = %d\n", nthreads);
    printf("\n");
  }
};


struct input_data {
  size_t n;
  std::vector<T> data;
  std::vector<uint8_t> namask;

  input_data(size_t _n) : n(_n) {}

  void generate(size_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<T> dist(0, 100);
    data.resize(n);
    std::generate(data.begin(), data.end(),
                  [&]() { return dist(rng); });
  }

  void fill_nas(double p, size_t seed) {
    constexpr T na_value = std::numeric_limits<T>::min();
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0, 1);
    namask.resize((n + 7) / 8);
    for (size_t i = 0; i < n; ++i) {
      if (dist(rng) < p) {
        namask[i/8] |= uint8_t(1 << (i & 7));
        data[i] = na_value;
      }
    }
  }
};

struct task {
  using time_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
  const std::string task_name;
  int64_t total;
  time_t time0, time1;
  double time_taken;

  task(const std::string& name) : task_name(name), total(0) {}

  virtual void run_once(const input_data& data) = 0;

  void run(const input_data& data) {
    const int n_iterations = 10;
    time0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n_iterations; ++i) {
      run_once(data);
    }
    time1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = time1 - time0;
    time_taken = diff.count() / n_iterations;
    std::cout << task_name << ": ";
    for (size_t i = task_name.size(); i < 30; ++i) std::cout << ' ';
    std::cout << time_taken << " s\n";
  }
};


struct sum_ignore_nulls : public task {
  sum_ignore_nulls() : task("sum_ignore_nulls") {}

  void run_once(const input_data& data) override {
    const size_t n = data.n;
    const T* x = data.data.data();
    for (size_t i = 0; i < n; ++i) {
      total += x[i];
    }
  }
};


struct sum_ignore_nulls_batched : public task {
  sum_ignore_nulls_batched() : task("sum_ignore_nulls_batched") {}

  void run_once(const input_data& data) override {
    const size_t n = data.n;
    const T* x = data.data.data();
    size_t batches = n / 8;
    for (size_t i = 0; i < batches; ++i) {
      total += x[0] + x[1] + x[2] + x[3] + x[4] + x[5] + x[6] + x[7];
      x += 8;
    }
    for (size_t i = batches * 8; i < n; ++i) {
      total += *x++;
    }
  }
};


struct sum_sentinel_nulls_if : public task {
  sum_sentinel_nulls_if() : task("sum_sentinel_nulls_if") {}

  void run_once(const input_data& data) override {
    constexpr T NA = std::numeric_limits<T>::min();
    const size_t n = data.n;
    const T* x = data.data.data();
    for (size_t i = 0; i < n; ++i) {
      if (x[i] != NA) total += x[i];
    }
  }
};


struct sum_sentinel_nulls_mul : public task {
  sum_sentinel_nulls_mul() : task("sum_sentinel_nulls_mul") {}

  void run_once(const input_data& data) override {
    constexpr T NA = std::numeric_limits<T>::min();
    const size_t n = data.n;
    const T* x = data.data.data();
    for (size_t i = 0; i < n; ++i) {
      total += x[i] * (x[i] != NA);
    }
  }
};


struct sum_sentinel_nulls_batched : public task {
  sum_sentinel_nulls_batched() : task("sum_sentinel_nulls_batched") {}

  void run_once(const input_data& data) override {
    constexpr T NA = std::numeric_limits<T>::min();
    const size_t n = data.n;
    const size_t nbatches = n / 8;
    const T* x = data.data.data();
    for (size_t i = 0; i < nbatches; ++i) {
      total += x[0] * (x[0] != NA) +
               x[1] * (x[1] != NA) +
               x[2] * (x[2] != NA) +
               x[3] * (x[3] != NA) +
               x[4] * (x[4] != NA) +
               x[5] * (x[5] != NA) +
               x[6] * (x[6] != NA) +
               x[7] * (x[7] != NA);
      x += 8;
    }
    for (size_t i = nbatches * 8; i < n; ++i) {
      T val = *x++;
      total += val * (val != NA);
    }
  }
};


struct sum_bitmask_nulls : public task {
  sum_bitmask_nulls() : task("sum_bitmask_nulls") {}

  void run_once(const input_data& data) override {
    const size_t n = data.n;
    const T* x = data.data.data();
    const uint8_t* valid_bitmap = data.namask.data();
    for (size_t i = 0; i < n; ++i) {
      total += x[i] * ((valid_bitmap[i/8] >> (i & 7)) & 1);
    }
  }
};


struct sum_bitmask_nulls_batched : public task {
  sum_bitmask_nulls_batched() : task("sum_bitmask_nulls_batched") {}

  void run_once(const input_data& data) override {
    const size_t n = data.n;
    const size_t nbatches = n / 8;
    const T* x = data.data.data();
    const uint8_t* valid_bitmap = data.namask.data();
    for (size_t i = 0; i < nbatches; ++i) {
      uint8_t valid_byte = valid_bitmap[i];
      total += x[0] * (valid_byte & 1) +
               x[1] * ((valid_byte >> 1) & 1) +
               x[2] * ((valid_byte >> 2) & 1) +
               x[3] * ((valid_byte >> 3) & 1) +
               x[4] * ((valid_byte >> 4) & 1) +
               x[5] * ((valid_byte >> 5) & 1) +
               x[6] * ((valid_byte >> 6) & 1) +
               x[7] * ((valid_byte >> 7) & 1);
      x += 8;
    }
    for (size_t i = nbatches * 8; i < n; ++i) {
      T val = *x++;
      total += val * ((valid_bitmap[i/8] >> (i & 7)) & 1);
    }
  }
};



int main(int argc, char** argv) {
  config cfg;
  cfg.parse(argc, argv);
  cfg.report();

  input_data data(cfg.n);
  data.generate(cfg.seed);
  data.fill_nas(cfg.p, cfg.seed);

  sum_ignore_nulls task0;            task0.run(data);
  sum_ignore_nulls_batched task1;    task1.run(data);
  sum_sentinel_nulls_if task2;       task2.run(data);
  sum_sentinel_nulls_mul task3;      task3.run(data);
  sum_sentinel_nulls_batched task4;  task4.run(data);
  sum_bitmask_nulls task5;           task5.run(data);
  sum_bitmask_nulls_batched task6;   task6.run(data);

  return 0;
}
