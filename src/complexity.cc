// Copyright 2016 Ismael Jimenez Martinez. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Source project : https://github.com/ismaelJimenez/cpp.leastsq
// Adapted to be used with google benchmark

#include "benchmark/benchmark_api.h"

#include "complexity.h"
#include "check.h"
#include "stat.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace benchmark {
  
// Internal function to calculate the different scalability forms
std::function<double(int)> FittingCurve(BigO complexity) {
  switch (complexity) {
    case oN:
      return [](int n) {return n; };
    case oNSquared:
      return [](int n) {return n*n; };
    case oNCubed:
      return [](int n) {return n*n*n; };
    case oLogN:
      return [](int n) {return log2(n); };
    case oNLogN:
      return [](int n) {return n * log2(n); };
    case o1:
    default:
      return [](int) {return 1; };
  }
}

// Function to return an string for the calculated complexity
std::string GetBigOString(BigO complexity) {
  switch (complexity) {
    case oN:
      return "* N";
    case oNSquared:
      return "* N**2";
    case oNCubed:
      return "* N**3";
    case oLogN:
      return "* lgN";
    case oNLogN:
      return "* NlgN";
    case o1:
      return "* 1";
    default:
      return "";
  }
}

// Find the coefficient for the high-order term in the running time, by 
// minimizing the sum of squares of relative error, for the fitting curve 
// given by the lambda expresion.
//   - n             : Vector containing the size of the benchmark tests.
//   - time          : Vector containing the times for the benchmark tests.
//   - fitting_curve : lambda expresion (e.g. [](int n) {return n; };).

// For a deeper explanation on the algorithm logic, look the README file at
// http://github.com/ismaelJimenez/Minimal-Cpp-Least-Squared-Fit

// This interface is currently not used from the oustide, but it has been 
// provided for future upgrades. If in the future it is not needed to support 
// Cxx03, then all the calculations could be upgraded to use lambdas because 
// they are more powerful and provide a cleaner inferface than enumerators, 
// but complete implementation with lambdas will not work for Cxx03 
// (e.g. lack of std::function).
// In case lambdas are implemented, the interface would be like :
//   -> Complexity([](int n) {return n;};)
// and any arbitrary and valid  equation would be allowed, but the option to 
// calculate the best fit to the most common scalability curves will still 
// be kept.

LeastSq CalculateLeastSq(const std::vector<int>& n, 
                         const std::vector<double>& time, 
                         std::function<double(int)> fitting_curve) {
  double sigma_gn = 0.0;
  double sigma_gn_squared = 0.0;
  double sigma_time = 0.0;
  double sigma_time_gn = 0.0;

  // Calculate least square fitting parameter
  for (size_t i = 0; i < n.size(); ++i) {
    double gn_i = fitting_curve(n[i]);
    sigma_gn += gn_i;
    sigma_gn_squared += gn_i * gn_i;
    sigma_time += time[i];
    sigma_time_gn += time[i] * gn_i;
  }

  LeastSq result;

  // Calculate complexity.
  result.coef = sigma_time_gn / sigma_gn_squared;

  // Calculate RMS
  double rms = 0.0;
  for (size_t i = 0; i < n.size(); ++i) {
    double fit = result.coef * fitting_curve(n[i]);
    rms += pow((time[i] - fit), 2);
  }

  // Normalized RMS by the mean of the observed values
  double mean = sigma_time / n.size();
  result.rms = sqrt(rms / n.size()) / mean;

  return result;
}

// Find the coefficient for the high-order term in the running time, by
// minimizing the sum of squares of relative error.
//   - n          : Vector containing the size of the benchmark tests.
//   - time       : Vector containing the times for the benchmark tests.
//   - complexity : If different than oAuto, the fitting curve will stick to
//                  this one. If it is oAuto, it will be calculated the best
//                  fitting curve.
LeastSq MinimalLeastSq(const std::vector<int>& n,
                       const std::vector<double>& time,
                       const BigO complexity) {
  CHECK_EQ(n.size(), time.size());
  CHECK_GE(n.size(), 2);  // Do not compute fitting curve is less than two benchmark runs are given
  CHECK_NE(complexity, oNone);

  LeastSq best_fit;

  if(complexity == oAuto) {
    std::vector<BigO> fit_curves = {
      oLogN, oN, oNLogN, oNSquared, oNCubed };

    // Take o1 as default best fitting curve
    best_fit = CalculateLeastSq(n, time, FittingCurve(o1));
    best_fit.complexity = o1;

    // Compute all possible fitting curves and stick to the best one
    for (const auto& fit : fit_curves) {
      LeastSq current_fit = CalculateLeastSq(n, time, FittingCurve(fit));
      if (current_fit.rms < best_fit.rms) {
        best_fit = current_fit;
        best_fit.complexity = fit;
      }
    }
  } else {
    best_fit = CalculateLeastSq(n, time, FittingCurve(complexity));
    best_fit.complexity = complexity;
  }

  return best_fit;
}

std::vector<BenchmarkReporter::Run> ComputeStats(
    const std::vector<BenchmarkReporter::Run>& reports)
{
  typedef BenchmarkReporter::Run Run;
  std::vector<Run> results;

  auto error_count = std::count_if(
      reports.begin(), reports.end(),
      [](Run const& run) {return run.error_occurred;});

  if (reports.size() - error_count < 2) {
    // We don't report aggregated data if there was a single run.
    return results;
  }
  // Accumulators.
  Stat1_d real_accumulated_time_stat;
  Stat1_d cpu_accumulated_time_stat;
  Stat1_d bytes_per_second_stat;
  Stat1_d items_per_second_stat;
  // All repetitions should be run with the same number of iterations so we
  // can take this information from the first benchmark.
  int64_t const run_iterations = reports.front().iterations;

  // Populate the accumulators.
  for (Run const& run : reports) {
    CHECK_EQ(reports[0].benchmark_name, run.benchmark_name);
    CHECK_EQ(run_iterations, run.iterations);
    if (run.error_occurred)
      continue;
    real_accumulated_time_stat +=
        Stat1_d(run.real_accumulated_time/run.iterations, run.iterations);
    cpu_accumulated_time_stat +=
        Stat1_d(run.cpu_accumulated_time/run.iterations, run.iterations);
    items_per_second_stat += Stat1_d(run.items_per_second, run.iterations);
    bytes_per_second_stat += Stat1_d(run.bytes_per_second, run.iterations);
  }

  // Get the data from the accumulator to BenchmarkReporter::Run's.
  Run mean_data;
  mean_data.benchmark_name = reports[0].benchmark_name + "_mean";
  mean_data.iterations = run_iterations;
  mean_data.real_accumulated_time = real_accumulated_time_stat.Mean() *
                                     run_iterations;
  mean_data.cpu_accumulated_time = cpu_accumulated_time_stat.Mean() *
                                    run_iterations;
  mean_data.bytes_per_second = bytes_per_second_stat.Mean();
  mean_data.items_per_second = items_per_second_stat.Mean();

  // Only add label to mean/stddev if it is same for all runs
  mean_data.report_label = reports[0].report_label;
  for (std::size_t i = 1; i < reports.size(); i++) {
    if (reports[i].report_label != reports[0].report_label) {
      mean_data.report_label = "";
      break;
    }
  }

  Run stddev_data;
  stddev_data.benchmark_name = reports[0].benchmark_name + "_stddev";
  stddev_data.report_label = mean_data.report_label;
  stddev_data.iterations = 0;
  stddev_data.real_accumulated_time =
      real_accumulated_time_stat.StdDev();
  stddev_data.cpu_accumulated_time =
      cpu_accumulated_time_stat.StdDev();
  stddev_data.bytes_per_second = bytes_per_second_stat.StdDev();
  stddev_data.items_per_second = items_per_second_stat.StdDev();

  results.push_back(mean_data);
  results.push_back(stddev_data);
  return results;
}

std::vector<BenchmarkReporter::Run> ComputeBigO(
    const std::vector<BenchmarkReporter::Run>& reports)
{
  typedef BenchmarkReporter::Run Run;
  std::vector<Run> results;

  if (reports.size() < 2) return results;

  // Accumulators.
  std::vector<int> n;
  std::vector<double> real_time;
  std::vector<double> cpu_time;

  // Populate the accumulators.
  for (const Run& run : reports) {
    n.push_back(run.complexity_n);
    real_time.push_back(run.real_accumulated_time/run.iterations);
    cpu_time.push_back(run.cpu_accumulated_time/run.iterations);
  }

  LeastSq result_cpu = MinimalLeastSq(n, cpu_time, reports[0].complexity);

  // result_cpu.complexity is passed as parameter to result_real because in case
  // reports[0].complexity is oAuto, the noise on the measured data could make
  // the best fit function of Cpu and Real differ. In order to solve this, we
  // take the best fitting function for the Cpu, and apply it to Real data.
  LeastSq result_real = MinimalLeastSq(n, real_time, result_cpu.complexity);

  std::string benchmark_name = reports[0].benchmark_name.substr(0, reports[0].benchmark_name.find('/'));

  // Get the data from the accumulator to BenchmarkReporter::Run's.
  Run big_o;
  big_o.benchmark_name = benchmark_name + "_BigO";
  big_o.iterations = 0;
  big_o.real_accumulated_time = result_real.coef;
  big_o.cpu_accumulated_time = result_cpu.coef;
  big_o.report_big_o = true;
  big_o.complexity = result_cpu.complexity;

  double multiplier = GetTimeUnitMultiplier(reports[0].time_unit);

  // Only add label to mean/stddev if it is same for all runs
  Run rms;
  big_o.report_label = reports[0].report_label;
  rms.benchmark_name = benchmark_name + "_RMS";
  rms.report_label = big_o.report_label;
  rms.iterations = 0;
  rms.real_accumulated_time = result_real.rms / multiplier;
  rms.cpu_accumulated_time = result_cpu.rms / multiplier;
  rms.report_rms = true;
  rms.complexity = result_cpu.complexity;

  results.push_back(big_o);
  results.push_back(rms);
  return results;
}

}  // end namespace benchmark
