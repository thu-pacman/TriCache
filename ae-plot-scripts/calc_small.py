import sys
import pathlib
import re
import numpy as np

ligra_re = re.compile(r"^Running time : (.*)$")
flashgraph_re = re.compile(r"^The graph engine takes (.*) seconds.*$")
terasort_re = re.compile(r"^exec (.*) s.*$")
spark_re = re.compile(r"^Compute time: (.*) sec$")
rocksdb_re = re.compile(r"^mixgraph.*op (.*) ops/sec.*$")
livegraph_re = re.compile(r"""^.*"throughput" : (.*)$""")
micro_re = re.compile(r"^.*HitRate.*Request : (.*) ops/s$")

min_reduce = lambda reduce, a: min(reduce, a)
sum_reduce = lambda reduce, a: reduce + a
append_reduce = lambda reduce, a: reduce + [a]

def read_result(filename, result_re, reduce, reduce_init):
    result = reduce_init
    with open(filename, "r") as f:
        lines = f.readlines()
        for line in lines:
            if result_re.match(line):
                result = reduce(result, float(result_re.match(line).group(1)))
    return result

def print_figure(results, config, lines):
    name, text, is_time, systems = config
    print("{}:".format(text))
    for line in lines:
        if isinstance(line, tuple):
            sys = line[0]
            base = line[1]
            if isinstance(results[name][base], list):
                print("\t{}'s speedup over {}':".format(sys, base))
                for i in range(1, 20):
                    print("\t\tPrivate Hit Rate = {:.0%}:\t{:.2f}x".format(i * 0.05, results[name][sys][i] / results[name][base][i]))
            elif is_time:
                print("\t{}'s speedup over {}': {:.2f}x".format(sys, base, results[name][base] / results[name][sys]))
            else:
                print("\t{}'s speedup over {}': {:.2f}x".format(sys, base, results[name][sys] / results[name][base]))
        else:
            if is_time:
                print("\t{}: {:.2e} seconds".format(line, results[name][line]))
            else:
                print("\t{}: {:.2e} ops/sec".format(line, results[name][line]))
    print()


if __name__ == '__main__':

    result_dir = pathlib.Path(sys.argv[1]) / 'results_small'

    exps = ["PageRank", "RocksDB", "TeraSort", "LiveGraph", "MicroBenchmark"]
    configs = [
        ("PageRank", "Graph Processing, PageRank on UK-2014 with 128GB memory", True, [
            ("Ligra (TriCache)",
             result_dir / "PageRank_ligra_cache.txt",
             ligra_re,
             min_reduce,
             np.inf),
            ("Ligra (Swapping)",
             result_dir / "PageRank_ligra_swap.txt",
             ligra_re,
             min_reduce,
             np.inf),
            ("FlashGraph",
             result_dir / "PageRank_flashgraph.txt",
             flashgraph_re,
             sum_reduce,
             0.)
        ]),
        ("RocksDB", "Key-Value Store, RocksDB with 64GB memory", False, [
            ("PlainTable (TriCache)",
             result_dir / "rocksdb_plain_cache.txt",
             rocksdb_re,
             min_reduce,
             np.inf),
            ("BlockBasedTable",
             result_dir / "rocksdb_block.txt",
             rocksdb_re,
             min_reduce,
             np.inf),
            ("PlainTable (mmap)",
             result_dir / "rocksdb_plain_mmap.txt",
             rocksdb_re,
             min_reduce,
             np.inf),
        ]),
        ("TeraSort", "Big-Data Analytics, 150GB TeraSort with 64GB memory", True, [
            ("ManualSort (TriCache)",
             result_dir / "terasort_manual_cache.txt",
             terasort_re,
             min_reduce,
             np.inf),
            ("GNUSort (TriCache)",
             result_dir / "terasort_gnu_cache.txt",
             terasort_re,
             min_reduce,
             np.inf),
            ("ManualSort (Swapping)",
             result_dir / "terasort_manual_swap.txt",
             terasort_re,
             min_reduce,
             np.inf),
            ("GNUSort (Swapping)",
             result_dir / "terasort_gnu_swap.txt",
             terasort_re,
             min_reduce,
             np.inf),
            ("Spark",
             result_dir / "terasort_spark.txt",
             spark_re,
             min_reduce,
             np.inf)
        ]),
        ("LiveGraph", "Graph Database, SF30 SNB with 32GB memory", False, [
            ("LiveGraph (TriCache)",
             result_dir / "livegraph_cache" / "LDBC-SNB-results.json",
             livegraph_re,
             min_reduce,
             np.inf),
            ("LiveGraph (mmap)",
             result_dir / "livegraph_mmap" / "LDBC-SNB-results.json",
             livegraph_re,
             min_reduce,
             np.inf)
        ]),
        ("MicroBenchmark", "Micro-Benchmark, Memory Hit Rate = 90%", True, [
            ("8B Random (TriCache)",
             result_dir / "micro_cache.txt",
             micro_re,
             append_reduce,
             []),
            ("8B Random (mmap)",
             result_dir / "micro_mmap.txt",
             micro_re,
             append_reduce,
             []),
            ("4KB Random (TriCache)",
             result_dir / "micro_allpage_cache.txt",
             micro_re,
             append_reduce,
             []),
            ("4KB Random (mmap)",
             result_dir / "micro_allpage_mmap.txt",
             micro_re,
             append_reduce,
             [])
        ]),
    ]

    results = {}

    for name, text, is_time, systems in configs:
        results[name] = {}
        for sysname, filename, result_re, reduce, reduce_init in systems:
            results[name][sysname] = read_result(filename, result_re, reduce, reduce_init)

    print_figure(results, configs[exps.index("PageRank")], ["Ligra (TriCache)",
                                                            "Ligra (Swapping)",
                                                            "FlashGraph",
                                                            ("Ligra (TriCache)", "Ligra (Swapping)"),
                                                            ("Ligra (TriCache)", "FlashGraph"),
                                                            ])
    print_figure(results, configs[exps.index("RocksDB")], ["PlainTable (TriCache)",
                                                           "BlockBasedTable",
                                                           "PlainTable (mmap)",
                                                           ("PlainTable (TriCache)", "BlockBasedTable"),
                                                           ("PlainTable (TriCache)", "PlainTable (mmap)"),
                                                           ])
    print_figure(results, configs[exps.index("TeraSort")], ["ManualSort (TriCache)",
                                                            "ManualSort (Swapping)",
                                                            "GNUSort (TriCache)",
                                                            "GNUSort (Swapping)",
                                                            "Spark",
                                                           ("ManualSort (TriCache)", "ManualSort (Swapping)"),
                                                           ("GNUSort (TriCache)", "GNUSort (Swapping)"),
                                                           ("ManualSort (TriCache)", "Spark"),
                                                            ])
    print_figure(results, configs[exps.index("LiveGraph")], ["LiveGraph (TriCache)",
                                                             "LiveGraph (mmap)",
                                                             ("LiveGraph (TriCache)", "LiveGraph (mmap)")
                                                             ])
    print_figure(results, configs[exps.index("MicroBenchmark")], [("8B Random (TriCache)", "8B Random (mmap)"),
                                                                  ("4KB Random (TriCache)", "4KB Random (mmap)"),
                                                                  ])
