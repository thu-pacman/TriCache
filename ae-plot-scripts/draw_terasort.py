from common import *

exps = ["150G", "400G"]
sizes = ["512", "256", "128", "64", "32", "16"]
SPARK = "Spark"
SWAP_GNU = "GNUSort (Swapping)"
SWAP_MANUAL = "ShuffleSort (Swapping)"
CACHE_GNU = "GNUSort (TriCache)"
CACHE_MANUAL = "ShuffleSort (TriCache)"

def draw_terasort(data):
    dat_150GB_spark = np.log10(data[SPARK][exps.index("150G")])
    dat_150GB_gnu_swap = np.log10(data[SWAP_GNU][exps.index("150G")])
    dat_150GB_gnu_cache = np.log10(data[CACHE_GNU][exps.index("150G")])
    dat_150GB_man_swap = np.log10(data[SWAP_MANUAL][exps.index("150G")])
    dat_150GB_man_cache = np.log10(data[CACHE_MANUAL][exps.index("150G")])

    dat_400GB_spark = np.log10(data[SPARK][exps.index("400G")])
    dat_400GB_gnu_swap = np.log10(data[SWAP_GNU][exps.index("400G")])
    dat_400GB_gnu_cache = np.log10(data[CACHE_GNU][exps.index("400G")])
    dat_400GB_man_swap = np.log10(data[SWAP_MANUAL][exps.index("400G")])
    dat_400GB_man_cache = np.log10(data[CACHE_MANUAL][exps.index("400G")])

    dat = [
        [
            dat_150GB_spark,
            dat_150GB_gnu_swap,
            dat_150GB_gnu_cache,
            dat_150GB_man_swap,
            dat_150GB_man_cache,
        ],
        [
            dat_400GB_spark,
            dat_400GB_gnu_swap,
            dat_400GB_gnu_cache,
            dat_400GB_man_swap,
            dat_400GB_man_cache,
        ],
    ]
    case_labels = ["(a) 150GB", "(b) 400GB"]

    sz = (8, 5)
    figsz = {"figure.figsize": sz}
    plt.rcParams.update(figsz)

    fig, axes = plt.subplots(nrows=2)
    color_vec = [color_def[3], color_def[0], color_def[4], color_def[1], color_def[2]]
    hatch_vec = [None, None, hatch_def[0], None, hatch_def[1]]

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
                if math.isnan(dat[k][j][i]):
                    axes[k].text(
                        offset + 0.06,
                        0.2,
                        "OOM",
                        rotation=90,
                        ha="center",
                        size=10,
                        color="red",
                        weight="bold",
                    )

            if math.isnan(dat[k][0][i]):
                xmin = (
                    group_gap + (group_gap + group_width) * i + bar_gap + bar_width
                ) / (group_gap + (group_gap + group_width) * num_groups)
            else:
                xmin = (group_gap + (group_gap + group_width) * i) / (
                    group_gap + (group_gap + group_width) * num_groups
                )
            axes[k].axhline(
                xmin=xmin,
                xmax=(group_gap + group_width + (group_gap + group_width) * i)
                / (group_gap + (group_gap + group_width) * num_groups),
                y=dat[k][-1][i],
                color="r",
                linestyle="dashed",
            )

        axes[k].set_xlim(0, (group_width + group_gap) * num_groups + group_gap)
        axes[k].set_xticks(
            [
                group_gap + (group_width + group_gap) * x + group_width / 2
                for x in range(num_groups)
            ]
        )
        axes[k].set_xticklabels([])
        axes[k].set_xticklabels(["512GB", "256GB", "128GB", "64GB", "32GB", "16GB"])

        axes[k].set_ylabel("Time (s)")
        axes[k].set_ylim(0, 4)
        axes[k].set_yticks(range(0, 5))
        axes[k].set_yticklabels(["1E0", "1E1", "1E2", "1E3", "1E4"])
        # # axes[k].set_title(case_labels[k])
        axes[k].set_xlabel(case_labels[k])

    labels = [
        SPARK,
        SWAP_GNU,
        CACHE_GNU,
        SWAP_MANUAL,
        CACHE_MANUAL,
    ]
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
        handles=legend_handles, ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.03)
    )

    # plt.show()
    fig.subplots_adjust(hspace=0.33)
    fig.savefig(dirbase + "terasort.pdf", bbox_inches="tight")


if __name__ == "__main__":

    import re
    import sys
    import pathlib
    from functools import reduce

    result_dir = pathlib.Path(sys.argv[1])

    gnu_name_re = re.compile(r".*/terasort_gnu_(\d+G)_(\d+)G.*\.txt$")
    manual_name_re = re.compile(r".*/terasort_manual_(\d+G)_(\d+)G.*\.txt$")
    spark_name_re = re.compile(r".*/terasort_spark_(\d+G)_(\d+)G.*\.txt$")
    result_re = re.compile(r"^exec (.*) s.*$")
    spark_result_re = re.compile(r"^Compute time: (.*) sec$")

    configs = [
        {
            "name": CACHE_GNU,
            "dir": result_dir / 'results_terasort_cache',
            "name_extrator": gnu_name_re,
            "result_extrator": result_re,
        },
        {
            "name": CACHE_MANUAL,
            "dir": result_dir / 'results_terasort_cache',
            "name_extrator": manual_name_re,
            "result_extrator": result_re,
        },
        {
            "name": SWAP_GNU,
            "dir": result_dir / 'results_terasort_swap',
            "name_extrator": gnu_name_re,
            "result_extrator": result_re,
        },
        {
            "name": SWAP_MANUAL,
            "dir": result_dir / 'results_terasort_swap',
            "name_extrator": manual_name_re,
            "result_extrator": result_re,
        },
        {
            "name": SPARK,
            "dir": result_dir / 'results_terasort_spark',
            "name_extrator": spark_name_re,
            "result_extrator": spark_result_re,
            "result_reducer": lambda a, b: min(a, b),
            "result_intilizer": math.inf
        },
    ]

    def read_result(c):

        results = np.zeros((len(exps), len(sizes)), np.float64)
        results[:] = np.nan

        for p in c["dir"].glob("*.txt"):
            match = c["name_extrator"].match(str(p))
            if match is None:
                continue
            exp, size = match.group(1), match.group(2)

            time_extrator = c["result_extrator"]

            with open(p.resolve(), "r") as f:
                result = []
                lines = f.readlines()
                for line in lines:
                    if time_extrator.match(line):
                        result.append(float(time_extrator.match(line).group(1)))

            if len(result) > 1:
                result = reduce(c["result_reducer"], result, c["result_intilizer"])
            elif len(result) == 1:
                result = result[0]
            else:
                result = np.nan

            results[exps.index(exp), sizes.index(size)] = result
            result = []

        return c["name"], results

    plot_results = {}

    for c in configs:
        name, results = read_result(c)
        plot_results[name] = results

    r = plot_results[SWAP_GNU][exps.index("400G"), :]
    invalid_mask = np.isnan(r)
    previous_run = np.array([2159.947290, 10301.253226, 10301.253226, 10301.253226, 10301.253226, 10301.253226])
    r[invalid_mask] = previous_run[invalid_mask]

    r = plot_results[SWAP_MANUAL][exps.index("400G"), :]
    invalid_mask = np.isnan(r)
    previous_run = np.array([2159.947290, 2179.110409, 1873.130266, 3514.507548, 3497.492071, 3350.423292])
    r[invalid_mask] = previous_run[invalid_mask]

    gnu_cache = plot_results[CACHE_GNU][exps.index("150G"), sizes.index("512")]
    gnu_swap = plot_results[SWAP_GNU][exps.index("150G"), sizes.index("512")]
    manual_cache = plot_results[CACHE_MANUAL][exps.index("150G"), sizes.index("512")]
    manual_swap = plot_results[CACHE_GNU][exps.index("150G"), sizes.index("512")]
    spark = plot_results[SPARK][exps.index("150G"), sizes.index("512")]

    print(f"With 150GB dataset, 512GB memory, TriCache's speedup over swpping is {gnu_swap/gnu_cache:.2f} on GNUSort, {manual_swap/manual_cache:.2f} on ShuffleSort")
    print(f"With 150GB dataset, 512GB memory, TriCache's speedup over Spark is {spark/gnu_cache:.2f} on GNUSort, {spark/manual_cache:.2f} on ShuffleSort")

    print()

    gnu_max = np.nanmax(plot_results[SWAP_GNU] / plot_results[CACHE_GNU], axis=1)
    manual_max = np.nanmax(plot_results[SWAP_MANUAL] / plot_results[CACHE_MANUAL], axis=1)
    spark_max = np.nanmax(plot_results[SPARK] / plot_results[CACHE_MANUAL], axis=1)
    print(f"With 150GB dataset, TriCache's maximal speedup over swpping is {gnu_max[exps.index('150G')]:.2f} on GNUSort, {manual_max[exps.index('150G')]:.2f} on ShuffleSort")
    print(f"With 400GB dataset, TriCache's maximal speedup over swpping is {gnu_max[exps.index('400G')]:.2f} on GNUSort, {manual_max[exps.index('400G')]:.2f} on ShuffleSort")
    print(f"With 150GB dataset, TriCache's maximal speedup over Spark is {spark_max[exps.index('150G')]:.2f} on ShuffleSort")
    print(f"With 400GB dataset, TriCache's maximal speedup over Spark is {spark_max[exps.index('400G')]:.2f} on ShuffleSort")

    # print(plot_results)
    draw_terasort(plot_results)
