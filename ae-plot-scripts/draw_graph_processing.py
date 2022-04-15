from common import *

SWAPPING = "Ligra (Swapping)"
FLASHGRAPH = "FlashGraph"
TRICACHE = "Ligra (TriCache)"
    
sizes = ["512", "256", "128", "64", "32", "16"]
exps = ["PageRank", "WCC", "BFS"]

def draw_graph_processing(data):

    dat_speedup_swap = np.zeros([3, 6]) + 2
    dat_speedup_flash = np.log10(data[SWAPPING] / data[FLASHGRAPH]) + 2
    dat_speedup_cache = np.log10(data[SWAPPING] / data[TRICACHE]) + 2

    dat = [dat_speedup_swap, dat_speedup_flash, dat_speedup_cache]
    labels = [SWAPPING, FLASHGRAPH, TRICACHE]

    sz = (16, 2.5)
    figsz = {"figure.figsize": sz}
    plt.rcParams.update(figsz)
    fig, axes = plt.subplots(ncols=3, nrows=1)

    color_vec = [color_def[3], color_def[1], color_def[2]]
    hatch_vec = [None, None, hatch_def[0]]

    num_bar_per_group = 3
    num_group_per_case = 6
    num_case = 3

    bar_width, bar_gap = 0.6, 0
    group_width = (bar_width + bar_gap) * num_bar_per_group - bar_gap
    group_gap = 1
    case = ["PageRank", "WCC", "BFS"]

    for i in range(num_case):
        for j in range(num_group_per_case):
            for k in range(num_bar_per_group):
                id = i * num_group_per_case + j
                offset = (
                    group_gap
                    + bar_width / 2
                    + (bar_width + bar_gap) * k
                    + (group_width + group_gap) * j
                )
                axes[i].bar(
                    offset,
                    dat[k][i][j],
                    bar_width,
                    color=color_vec[k],
                    hatch=hatch_vec[k],
                    edgecolor="black",
                )
                if math.isnan(dat[k][i][j]):
                    axes[i].text(
                        offset + 0.06,
                        0.2,
                        "OOM",
                        rotation=90,
                        ha="center",
                        size=10,
                        color="red",
                        weight="bold",
                    )
        axes[i].set_ylim(0, 4)
        axes[i].set_yticks(range(5))
        axes[i].set_yticklabels([0, 0.1, 1, 10, 100])

        axes[i].set_xlim(0, (group_width + group_gap) * num_group_per_case + group_gap)
        xticks = [
            group_gap + (group_width + group_gap) * x + group_width / 2
            for x in range(num_group_per_case)
        ]
        axes[i].set_xticks(xticks)
        axes[i].set_xticklabels(["512GB", "256GB", "128GB", "64GB", "32GB", "16GB"])
        axes[i].set_xlabel(case[i])

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
    fig.legend(
        handles=legend_handles, ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.1)
    )

    fig.savefig(dirbase + "graph_processing.pdf", bbox_inches="tight")


if __name__ == "__main__":

    import re
    import sys
    import pathlib
    from functools import reduce

    result_dir = pathlib.Path(sys.argv[1])

    configs = [
        {
            "name": SWAPPING,
            "dir": result_dir / 'results_ligra_swap',
            "name_extrator": re.compile(r"(.*)_uk-2014_(\d*)G_.*.txt"),
            "result_extrator": re.compile(r"^Running time : (.*)$"),
            "result_reducer": lambda a, b: min(a, b),
            "result_intilizer": math.inf
        },
        {
            "name": FLASHGRAPH,
            "dir": result_dir / 'results_flashgraph',
            "name_extrator": re.compile(r"uk2014_(.*)_(\d*)G.txt"),
            "result_extrator": re.compile(r"^The graph engine takes (.*) seconds.*$"),
            "result_reducer": lambda a, b: a + b,
            "result_intilizer": 0.
        },
        {
            "name": TRICACHE,
            "dir": result_dir / 'results_ligra_cache',
            "name_extrator": re.compile(r"(.*)_uk-2014_(\d*)G_.*.txt"),
            "result_extrator": re.compile(r"^Running time : (.*)$"),
            "result_reducer": lambda a, b: min(a, b),
            "result_intilizer": math.inf
        },
    ]

    def read_result(c):

        results = np.ndarray((len(exps), len(sizes)), np.float64)
        results[:] = np.nan

        for p in c["dir"].glob("*.txt"):
            match = c["name_extrator"].match(p.name)
            exp, size = match.group(1), match.group(2)

            if "PR" in exp or "PageRank" in exp:
                exp = "PageRank"
            if "CC" in exp:
                exp = "WCC"
            
            time_extrator = c["result_extrator"]
            time_reducer = c["result_reducer"]
            with open(p.resolve(), "r") as f:
                lines = f.readlines()
                temp = []
                for line in lines:
                    if time_extrator.match(line):
                        temp.append(float(time_extrator.match(line).group(1)))
                if len(temp) == 0:
                    result = np.nan
                else:
                    result = reduce(time_reducer, temp, c["result_intilizer"])
            
            results[exps.index(exp), sizes.index(size)] = result
            result = np.nan

        return c["name"], results

    plot_results = {}
    for c in configs:
        name, results = read_result(c)
        plot_results[name] = results


    def fill_nan(name, exp, size, r):
        results = plot_results[name]
        if math.isnan(results[exps.index(exp), sizes.index(size)]):
            results[exps.index(exp), sizes.index(size)] = r

    fill_nan(SWAPPING, "PageRank", "64", 5380.)
    fill_nan(SWAPPING, "PageRank", "32", 7140.)
    fill_nan(SWAPPING, "PageRank", "16", 6860.)
    fill_nan(SWAPPING, "WCC", "64", 6660.)
    fill_nan(SWAPPING, "WCC", "32", 6030.)
    fill_nan(SWAPPING, "WCC", "16", 4460.)
    fill_nan(SWAPPING, "BFS", "64", 1690.)
    fill_nan(SWAPPING, "BFS", "32", 2170.)
    fill_nan(SWAPPING, "BFS", "16", 1970.)

    # print(plot_results)

    print("With 512GB memory (in memory), TriCache brings {:.1%} overheads for PageRank, {:.1%} for WCC, and {:.1%} for BFS.".format(*list(1 - plot_results[SWAPPING][:,sizes.index("512")] / plot_results[TRICACHE][:,sizes.index("512")])))
    print("With 512GB memory (in memory), TriCache outperforms FlashGraph by {:.2f}x for PageRank, {:.2f}x for WCC, and {:.2f}x for BFS.".format(*list(plot_results[FLASHGRAPH][:,sizes.index("512")] / plot_results[TRICACHE][:,sizes.index("512")])))

    print()

    print("With 256GB memory (out of core), TriCache's speed-up over OS Swapping is {:.2f}x for PageRank, {:.2f}x for WCC, and {:.2f}x for BFS.".format(*list(plot_results[SWAPPING][:,sizes.index("256")] / plot_results[TRICACHE][:,sizes.index("256")])))
    print("With 256GB memory (out of core), TriCache's speed-up over FlashGraph  is {:.2f}x for PageRank, {:.2f}x for WCC, and {:.2f}x for BFS.".format(*list(plot_results[FLASHGRAPH][:,sizes.index("256")] / plot_results[TRICACHE][:,sizes.index("256")])))

    print()
    
    print("With 64GB memory (out of core), TriCache's speed-up over OS Swapping is {:.2f}x for PageRank, {:.2f}x for WCC, and {:.2f}x for BFS.".format(*list(plot_results[SWAPPING][:,sizes.index("64")] / plot_results[TRICACHE][:,sizes.index("64")])))
    print("With 64GB memory (out of core), TriCache's speed-up over FlashGraph  is {:.2f}x for PageRank, {:.2f}x for WCC, and {:.2f}x for BFS.".format(*list(plot_results[FLASHGRAPH][:,sizes.index("64")] / plot_results[TRICACHE][:,sizes.index("64")])))

    draw_graph_processing(plot_results)
