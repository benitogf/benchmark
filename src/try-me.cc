#include <benchmark/benchmark.h>
#include <string>
#include <unordered_map>
#include <list>

using std::string;
using std::unordered_map;
using std::list;

static void BM_StringCopy(benchmark::State& state) {
    string x = "hello";
    for (auto _ : state) {
        string copy(x);
    }
}

static void BM_UnorderedMapInsert(benchmark::State& state) {
    unordered_map<int, string> instrument;
    for (auto _ : state) {
	for (int i = state.range(0); i--;)
            instrument.insert({ instrument.size(), "hello" });
        for (auto _ : instrument)
            string copy(_.second);
    }
}

static void BM_ListInsert(benchmark::State& state) {
    list<string> instrument;
    for (auto _ : state) {
	for (int i = state.range(0); i--;)
            instrument.push_back("hello");
        for (auto _ : instrument)
            string copy(_);
    }
}

BENCHMARK(BM_StringCopy);
BENCHMARK(BM_UnorderedMapInsert)->Range(1, 1<<16);
BENCHMARK(BM_ListInsert)->Range(1, 1<<16);
BENCHMARK_MAIN();
