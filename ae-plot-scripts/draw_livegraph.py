from common import *

MMAP = "LiveGraph (mmap)"
TRICACHE = "LiveGraph (TriCache)"

sizes = ["256", "128", "64", "32", "16"]
exps = ["SF30", "SF100"]

def draw_livegraph(data):
    dat_SF30_cache = np.log10(data[TRICACHE][exps.index("SF30")])
    dat_SF30_mmap = np.log10(data[MMAP][exps.index("SF30")])
    dat_SF100_cache = np.log10(data[TRICACHE][exps.index("SF100")])
    dat_SF100_mmap = np.log10(data[MMAP][exps.index("SF100")])

    dat = [[dat_SF30_mmap, dat_SF30_cache], [dat_SF100_mmap, dat_SF100_cache]]
    case_labels = ["(a) SF30", "(b) SF100"]

    sz = (8, 5)
    figsz = {"figure.figsize": sz}
    plt.rcParams.update(figsz)

    fig, axes = plt.subplots(nrows=2)
    color_vec = [color_def[1], color_def[2]]
    hatch_vec = [None, hatch_def[0]]

    num_cases = len(dat)
    num_bars = len(dat[0])
    num_groups = len(dat[0][0])

    bar_width, bar_gap = 0.6, 0
    group_width = (bar_width + bar_gap) * num_bars - bar_gap
    group_gap = 1

    for k in range(num_cases):
        for i in range(num_groups):
            for j in range(num_bars):
                offset = (
                    group_gap
                    + (group_width + group_gap) * i
                    + bar_width / 2
                    + (bar_width + bar_gap) * j
                )
                axes[k].bar(
                    offset,
                    dat[k][j][i],
                    bar_width,
                    color=color_vec[j],
                    hatch=hatch_vec[j],
                    edgecolor="black",
                )

        axes[k].set_xlim(0, (group_width + group_gap) * num_groups + group_gap)
        axes[k].set_xticks(
            [
                group_gap + (group_width + group_gap) * x + group_width / 2
                for x in range(num_groups)
            ]
        )
        axes[k].set_xticklabels([])
        axes[k].set_xticklabels(np.flip(["16GB", "32GB", "64GB", "128GB", "256GB"]))

        axes[k].set_ylabel("Throughput")
        axes[k].set_ylim(1, 5)
        axes[k].set_yticks(range(1, 6))
        axes[k].set_yticklabels([0, "1E2", "1E3", "1E4", "1E5"])
        # axes[k].set_title(case_labels[k])
        axes[k].set_xlabel(case_labels[k])

    labels = [MMAP, TRICACHE]
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
        handles=legend_handles, ncol=2, loc="upper center", bbox_to_anchor=(0.5, 0.97)
    )

    # plt.show()
    fig.subplots_adjust(hspace=0.33)
    fig.savefig(dirbase + "livegraph.pdf", bbox_inches="tight")


if __name__ == "__main__":

    import re
    import sys
    import pathlib

    result_dir = pathlib.Path(sys.argv[1])

    configs = [
        {
            "name": MMAP,
            "dir": result_dir / 'results_livegraph_mmap',
            "name_extrator": re.compile(r".*results_.*_(sf.*)_mmap_(.*)G.*"),
            "result_extrator": re.compile(r"""^.*"throughput" : (.*)$"""),
        },
        {
            "name": TRICACHE,
            "dir": result_dir / 'results_livegraph_cache',
            "name_extrator": re.compile(r".*results_.*_(sf.*)_cache_(.*)G.*"),
            "result_extrator": re.compile(r"""^.*"throughput" : (.*)$"""),
        },
    ]

    def read_result(c):

        results = np.zeros((len(exps), len(sizes)), np.float64)
        # results[:] = np.nan

        for p in c["dir"].rglob("*/LDBC-SNB-results.json"):
            match = c["name_extrator"].match(str(p))
            exp, size = match.group(1), match.group(2)

            exp = exp.upper()
            
            time_extrator = c["result_extrator"]

            with open(p.resolve(), "r") as f:
                lines = f.readlines()
                for line in lines:
                    if time_extrator.match(line):
                        result = float(time_extrator.match(line).group(1))
            
            results[exps.index(exp), sizes.index(size)] = result
            result = 0

        return c["name"], results

    plot_results = {}

    for c in configs:
        name, results = read_result(c)
        plot_results[name] = results

    # print(plot_results)

    print("With 256GB memory & SF30 dataset, TriCache brings {:.1%} overhead compared with mmap.".format(1 - plot_results[TRICACHE][exps.index("SF30"), sizes.index("256")] / plot_results[MMAP][exps.index("SF30"), sizes.index("256")]))
    print("With 32GB memory & SF30 dataset, TriCache outperforms mmap by {:.2f}x.".format(plot_results[TRICACHE][exps.index("SF30"), sizes.index("32")] / plot_results[MMAP][exps.index("SF30"), sizes.index("32")]))

    print()

    print("With 256GB memory & SF100 dataset, TriCache outperforms mmap by {:.2f}x.".format(plot_results[TRICACHE][exps.index("SF100"), sizes.index("256")] / plot_results[MMAP][exps.index("SF100"), sizes.index("256")]))
    print("With 16GB memory & SF100 dataset, TriCache outperforms mmap by {:.2f}x.".format(plot_results[TRICACHE][exps.index("SF100"), sizes.index("16")] / plot_results[MMAP][exps.index("SF100"), sizes.index("16")]))

    draw_livegraph(plot_results)
