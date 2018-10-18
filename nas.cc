//------------------------------------------------------------------------------
// License: MIT
// Copyright: H2O.ai 2018
//
// (some of the code borrowed from https://github.com/wesm/bitmaps-vs-sentinels,
//  licensed MIT)
//------------------------------------------------------------------------------
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <getopt.h>  // option
#include <unistd.h>  // getopt_long
#include <stdlib.h>  // atol
#include <omp.h>

using T = int32_t;


struct config {
  size_t seed;
  size_t n;
  double p;
  int nthreads;

  config() {
    seed = 1;
    n = 1000000;
    p = 0.1;
    nthreads = 8;
  }

  void parse(int argc, char** argv) {
    struct option longopts[] = {
      {"seed", 1, 0, 0},
      {"n", 1, 0, 0},
      {"p", 1, 0, 0},
      {"nthreads", 1, 0, 0},
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
          if (option_index == 3) nthreads = atoi(optarg);
        }
      }
    }
  }

  void report() {
    printf("\nInput parameters:\n");
    printf("  seed     = %zu\n", seed);
    printf("  n        = %zu\n", n);
    printf("  p        = %f\n", p);
    printf("  nthreads = %d\n", nthreads);
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
    // Note: for bitmask array, bit=1 for valid values, and 0 for NA values.
    constexpr T na_value = std::numeric_limits<T>::min();
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0, 1);
    namask.resize((n + 7) / 8, uint8_t(0xFF));
    for (size_t i = 0; i < n; ++i) {
      if (dist(rng) < p) {
        namask[i/8] &= ~uint8_t(1 << (i & 7));
        data[i] = na_value;
      }
    }
  }
};


struct task {
  static constexpr int n_iterations = 100;
  const std::string task_name;
  int64_t total;

  task(const std::string& name) : task_name(name), total(0) {}

  virtual void run_once(const input_data& data) = 0;

  void run(const input_data& data) {
    std::vector<double> times;
    for (int i = 0; i < n_iterations; ++i) {
      auto time0 = std::chrono::high_resolution_clock::now();
      run_once(data);
      auto time1 = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> diff = time1 - time0;
      times.push_back(diff.count());  // store the time in seconds
    }
    double mean_time = 0.0;
    for (auto& t : times) mean_time += t;
    mean_time /= n_iterations;
    double msd = 0.0;
    for (auto& t : times) msd += (t - mean_time) * (t - mean_time);
    msd /= n_iterations - 1;
    double stdev = std::sqrt(msd);
    std::cout << task_name << ": ";
    for (size_t i = task_name.size(); i < 30; ++i) std::cout << ' ';
    std::cout << mean_time << " s,  +/- " << stdev << " s\n";
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


struct sum_bitmask_nulls_shortcut : public task {
  sum_bitmask_nulls_shortcut() : task("sum_bitmask_nulls_shortcut") {}

  void run_once(const input_data& data) override {
    const size_t n = data.n;
    const size_t nbatches = n / 8;
    const T* x = data.data.data();
    const uint8_t* valid_bitmap = data.namask.data();
    for (size_t i = 0; i < nbatches; ++i) {
      uint8_t valid_byte = valid_bitmap[i];
      if (valid_byte == 0xFF) {
        total += x[0] + x[1] + x[2] + x[3] + x[4] +
                 x[5] + x[6] + x[7];
      } else {
        total += x[0] * (valid_byte & 1) +
                 x[1] * ((valid_byte >> 1) & 1) +
                 x[2] * ((valid_byte >> 2) & 1) +
                 x[3] * ((valid_byte >> 3) & 1) +
                 x[4] * ((valid_byte >> 4) & 1) +
                 x[5] * ((valid_byte >> 5) & 1) +
                 x[6] * ((valid_byte >> 6) & 1) +
                 x[7] * ((valid_byte >> 7) & 1);
      }
      x += 8;
    }
    for (size_t i = nbatches * 8; i < n; ++i) {
      T val = *x++;
      total += val * ((valid_bitmap[i/8] >> (i & 7)) & 1);
    }
  }
};


struct sum_sentinel_nulls_omp1 : public task {
  int nthreads;
  sum_sentinel_nulls_omp1(int nth) : task("sum_sentinel_nulls_omp1") {
    nthreads = nth;
  }

  void run_once(const input_data& data) override {
    constexpr T NA = std::numeric_limits<T>::min();
    const size_t n = data.n;
    const T* x = data.data.data();
    #pragma omp parallel num_threads(nthreads)
    {
      size_t nth = static_cast<size_t>(omp_get_num_threads());
      size_t ith = static_cast<size_t>(omp_get_thread_num());
      int64_t subtotal = 0;
      for (size_t i = ith; i < n; i += nth) {
        subtotal += x[i] * (x[i] != NA);
      }
      #pragma omp atomic update
      total += subtotal;
    }
  }
};


struct sum_sentinel_nulls_omp2 : public task {
  int nthreads;
  sum_sentinel_nulls_omp2(int nth) : task("sum_sentinel_nulls_omp2") {
    nthreads = nth;
  }

  void run_once(const input_data& data) override {
    constexpr T NA = std::numeric_limits<T>::min();
    const size_t n = data.n;
    const T* x = data.data.data();
    #pragma omp parallel for reduction (+:total) num_threads(nthreads)
    for (size_t i = 0; i < n; ++i) {
      total += x[i] * (x[i] != NA);
    }
  }
};


struct sum_bitmask_nulls_omp2 : public task {
  int nthreads;
  sum_bitmask_nulls_omp2(int nth) : task("sum_bitmask_nulls_omp2") {
    nthreads = nth;
  }

  void run_once(const input_data& data) override {
    const size_t n = data.n;
    const size_t nbatches = n / 8;
    const T* x = data.data.data();
    const uint8_t* valid_bitmap = data.namask.data();
    #pragma omp parallel for reduction (+:total) num_threads(nthreads)
    for (size_t i = 0; i < nbatches; ++i) {
      uint8_t valid_byte = valid_bitmap[i];
      const T* xx = x + (i << 3);
      total += xx[0] * (valid_byte & 1) +
               xx[1] * ((valid_byte >> 1) & 1) +
               xx[2] * ((valid_byte >> 2) & 1) +
               xx[3] * ((valid_byte >> 3) & 1) +
               xx[4] * ((valid_byte >> 4) & 1) +
               xx[5] * ((valid_byte >> 5) & 1) +
               xx[6] * ((valid_byte >> 6) & 1) +
               xx[7] * ((valid_byte >> 7) & 1);
    }
    for (size_t i = nbatches * 8; i < n; ++i) {
      total += x[i] * ((valid_bitmap[i/8] >> (i & 7)) & 1);
    }
  }
};



int main(int argc, char** argv) {
  config cfg;
  cfg.parse(argc, argv);
  cfg.report();

  std::cout << "Generating data...\n";
  input_data data(cfg.n);
  data.generate(cfg.seed);
  data.fill_nas(cfg.p, cfg.seed);
  std::cout << "  done.\n\n";

  int t = cfg.nthreads;
  { // warm up OMP system, in particular this allocates the thread pool
    int z = 0;
    #pragma omp parallel for num_threads(t)
    for (size_t i = 0; i < 10000; ++i) {
      z += i;
    }
  }

  sum_ignore_nulls task0;            task0.run(data);
  sum_ignore_nulls_batched task1;    task1.run(data);
  sum_sentinel_nulls_if task2;       task2.run(data);
  sum_sentinel_nulls_mul task3;      task3.run(data);
  sum_sentinel_nulls_batched task4;  task4.run(data);
  sum_bitmask_nulls task5;           task5.run(data);
  sum_bitmask_nulls_batched task6;   task6.run(data);
  sum_bitmask_nulls_shortcut task7;  task7.run(data);
  sum_sentinel_nulls_omp1 task8(t);  task8.run(data);
  sum_sentinel_nulls_omp2 task9(t);  task9.run(data);
  sum_bitmask_nulls_omp2 taskA(t);   taskA.run(data);

  std::cout << '\n';
  return 0;
}
