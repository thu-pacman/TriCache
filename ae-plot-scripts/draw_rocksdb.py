from common import *

BLOCK = "BlockBasedTable"
PLAIN_MMAP = "PlainTable (mmap)"
PLAIN_TRICACHE = "PlainTable (TriCache)"

exps = [""]
sizes = ["256", "128", "64", "32", "16"]

def draw_rocksdb(data):
    dat_plain_mmap = np.log10(data[PLAIN_MMAP]).flatten()
    dat_block = np.log10(data[BLOCK]).flatten()
    dat_plain_cache = np.log10(data[PLAIN_TRICACHE]).flatten()

    dat = [dat_block, dat_plain_mmap, dat_plain_cache]

    labels = [BLOCK, PLAIN_MMAP, PLAIN_TRICACHE]

    sz = (8, 2.5)
    figsz = {"figure.figsize": sz}
    plt.rcParams.update(figsz)

    fig, ax = plt.subplots()
    color_vec = [color_def[3], color_def[1], color_def[2]]
    hatch_vec = [None, None, hatch_def[0]]

    num_bars = len(dat)
    num_groups = len(dat[0])

    bar_width, bar_gap = 0.6, 0
    group_width = (bar_width + bar_gap) * num_bars - bar_gap
    group_gap = 1

    for i in range(num_groups):
        for j in range(num_bars):
            offset = (
                group_gap
                + (group_width + group_gap) * i
                + bar_width / 2
                + (bar_width + bar_gap) * j
            )
            ax.bar(
                offset,
                dat[j][i],
                bar_width,
                color=color_vec[j],
                hatch=hatch_vec[j],
                edgecolor="black",
            )

    ax.set_xlabel("Memory Quota")
    ax.set_xlim(0, (group_width + group_gap) * num_groups + group_gap)
    ax.set_xticks(
        [
            group_gap + (group_width + group_gap) * x + group_width / 2
            for x in range(num_groups)
        ]
    )
    ax.set_xticklabels(["256GB", "128GB", "64GB", "32GB", "16GB"])

    ax.set_ylabel("Throughput")
    ax.set_ylim(1, 7)
    ax.set_yticks(range(1, 8))
    ax.set_yticklabels([0, "1E2", "1E3", "1E4", "1E5", "1E6", "1E7"])

    num_type = len(labels)
    legend_handles = [
        mpatches.Patch(
            facecolor=color_vec[i],
            edgecolor="black",
            hatch=hatch_vec[i],
            label=labels[i],
        )
        for i in range(num_type)
    ]
    plt.legend(
        handles=legend_handles, ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.25)
    )

    # plt.show()
    fig.savefig(dirbase + "rocksdb.pdf", bbox_inches="tight")


if __name__ == "__main__":

    import re
    import sys
    import pathlib

    result_dir = pathlib.Path(sys.argv[1])

    name_re = re.compile(r".*/(.*)_(.*)G_.*.txt$")
    result_re = re.compile(r"^mixgraph.*op (.*) ops/sec.*$")

    configs = [
        {
            "name": BLOCK,
            "dir": result_dir / 'results_rocksdb_block',
            "name_extrator": name_re,
            "result_extrator": result_re,
        },
        {
            "name": PLAIN_MMAP,
            "dir": result_dir / 'results_rocksdb_mmap',
            "name_extrator": name_re,
            "result_extrator": result_re,
        },
        {
            "name": PLAIN_TRICACHE,
            "dir": result_dir / 'results_rocksdb_cache',
            "name_extrator": name_re,
            "result_extrator": result_re,
        },
    ]

    def read_result(c):

        results = np.zeros((len(exps), len(sizes)), np.float64)
        # results[:] = np.nan

        for p in c["dir"].glob("*.txt"):
            match = c["name_extrator"].match(str(p))
            _, size = match.group(1), match.group(2)
            
            time_extrator = c["result_extrator"]

            with open(p.resolve(), "r") as f:
                lines = f.readlines()
                for line in lines:
                    if time_extrator.match(line):
                        result = float(time_extrator.match(line).group(1))
            
            results[0, sizes.index(size)] = result
            result = 0

        return c["name"], results

    plot_results = {}

    for c in configs:
        name, results = read_result(c)
        plot_results[name] = results

    # print(plot_results)
    
    for i, s in enumerate(sizes):
        b = plot_results[BLOCK][0, i]
        m = plot_results[PLAIN_MMAP][0, i]
        t = plot_results[PLAIN_TRICACHE][0, i]
        print(f"With {s}G memory, PlainTable(TriCache)'s speedup over PlainTable(mmap): {t/m:.2f}, over BlockBasedTable {t/b:.2f}")


    draw_rocksdb(plot_results)
