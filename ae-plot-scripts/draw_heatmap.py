from common import *

exps = ["8B", "4KB"]
sizes = ["0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0"]

MMAP = "mmap"
TRICACHE = "cache"
FASTMAP = "fastmap"

def draw_heatmap(data):
    dat_mmap_8B = np.log10(data[TRICACHE][exps.index("8B")] / data[MMAP][exps.index("8B")]).T

    dat_fastmap_8B = np.log10(data[TRICACHE][exps.index("8B")] / data[FASTMAP][exps.index("8B")]).T

    dat_mmap_4k = np.log10(data[TRICACHE][exps.index("4KB")] / data[MMAP][exps.index("4KB")]).T

    dat_fastmap_4k = np.log10(data[TRICACHE][exps.index("4KB")] / data[FASTMAP][exps.index("4KB")]).T

    dat = np.array([dat_mmap_8B, dat_fastmap_8B, dat_mmap_4k, dat_fastmap_4k])
    dat = np.flip(np.array([dat, dat]).transpose(1, 2, 3, 0).reshape(4, 20, 20), axis=1)

    sz = (12, 3)
    figsz = {"figure.figsize": sz}
    plt.rcParams.update(figsz)

    fig, axes = plt.subplots(ncols=4)
    color_vec = [color_def[3], color_def[0], color_def[4], color_def[1], color_def[2]]
    hatch_vec = [None, None, None, None, hatch_def[0]]
    titles = [
        "8B Random over mmap",
        "8B Random over FastMap",
        "4KB Random over mmap",
        "4KB Random over FastMap",
    ]

    custom_cm = "RdYlBu"

    for i in range(4):
        im = axes[i].imshow(dat[i], cmap=custom_cm, norm=Normalize(vmin=-2, vmax=2))
        # Create colorbar
        axes[i].set_xticks(np.arange(0, 21, 5) - 0.5)
        axes[i].set_yticks(np.arange(0, 21, 5) - 0.5)
        axes[i].set_xticklabels(["0", "25%", "50%", "75%", "100%"], size=10)
        if i == 0:
            axes[i].set_yticklabels(["100%", "75%", "50%", "25%", "0"], size=10)
            axes[i].set_ylabel("Private Hit Rate")
        else:
            axes[i].set_yticklabels([])
        axes[i].set_title(titles[i], y=1.05)

    fig.text(0.5, 0, "Memory Hit Rate", ha="center", size=12, color="black")
    cb_ax = fig.add_axes([0.92, 0.17, 0.01, 0.66])
    cbar = fig.colorbar(
        mappable=cm.ScalarMappable(cmap=custom_cm, norm=Normalize(vmin=-2, vmax=2)),
        ticks=[-1, 0, 1, 2],
        cax=cb_ax,
    )
    cbar.set_label("Speedup")
    # cbar.set_clim(0.1, 100)
    cbar.set_ticklabels([0.1, 1, 10, 100])

    # fig.subplots_adjust(wspace=0.1)
    # fig.tight_layout()

    # plt.show()

    fig.savefig(dirbase + "heatmap.pdf", bbox_inches="tight")


if __name__ == "__main__":

    import re
    import sys
    import pathlib
    from statistics import geometric_mean

    result_dir = pathlib.Path(sys.argv[1])

    name_re = re.compile(r".*micro_(.*)(\d\.\d)_(\d+)\.log")
    result_re = re.compile(r"^.*HitRate.*Request : (.*) ops/s$")

    configs = [
        {
            "name": MMAP,
            "dir": result_dir / 'results_microbenchmark_mmap',
            "name_extrator": name_re,
            "result_extrator": result_re,
        },
        {
            "name": TRICACHE,
            "dir": result_dir / 'results_microbenchmark_cache',
            "name_extrator": name_re,
            "result_extrator": result_re,
        },
        {
            "name": FASTMAP,
            "dir": result_dir / 'results_microbenchmark_fastmap',
            "name_extrator": name_re,
            "result_extrator": result_re,
        },
    ]

    def read_result(c):

        results = np.zeros((len(exps), len(sizes), 20), np.float64)
        # results[:] = np.nan

        for p in c["dir"].glob("*.log"):
            match = c["name_extrator"].match(str(p))
            exp, size = match.group(1), match.group(2)

            if exp == "":
                exp = "8B"
            elif exp == "allpage_":
                exp = "4KB"
            else:
                raise

            time_extrator = c["result_extrator"]
            skipped = False

            with open(p.resolve(), "r") as f:
                result = []
                lines = f.readlines()
                for line in lines:
                    if time_extrator.match(line):
                        if not skipped:
                            skipped = True
                            continue
                        result.append(float(time_extrator.match(line).group(1)))

            results[exps.index(exp), sizes.index(size), :] = result
            result = []

        return c["name"], results


    plot_results = {}

    for c in configs:
        name, results = read_result(c)
        plot_results[name] = results
    # print(plot_results)

    i4KB = exps.index("4KB")
    i8B = exps.index("8B")

    print(f"For 8B Random workloads, Memory Hit Rate = 100%, Private Hit Rate = 0.05%, TriCache' speedup over mmap is {plot_results[TRICACHE][i8B,-1,0] / plot_results[MMAP][i8B,-1,0]:.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 100%, Private Hit Rate = 100%, TriCache' speedup over mmap is {plot_results[TRICACHE][i8B,-1,-1] / plot_results[MMAP][i8B,-1,-1]:.2f}")

    print()

    print(f"For 8B Random workloads, Memory Hit Rate = 90%, TriCache' minimal speedup over mmap is {np.min(plot_results[TRICACHE][i8B,-2,:] / plot_results[MMAP][i8B,-2,:]):.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 90%, TriCache' maximal speedup over mmap is {np.max(plot_results[TRICACHE][i8B,-2,:] / plot_results[MMAP][i8B,-2,:]):.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 90%, TriCache' average speedup over FastMap is {geometric_mean((plot_results[TRICACHE][i8B,-2,:] / plot_results[FASTMAP][i8B,-2,:])):.2f}")

    print()

    print(f"For 8B Random workloads, Memory Hit Rate = 80%, TriCache' average speedup over mmap is {geometric_mean((plot_results[TRICACHE][i8B,-3,:] / plot_results[MMAP][i8B,-3,:])):.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 80%, TriCache' average speedup over FastMap is {geometric_mean((plot_results[TRICACHE][i8B,-3,:] / plot_results[FASTMAP][i8B,-3,:])):.2f}")

    print()

    print(f"For 8B Random workloads, Memory Hit Rate = 10%, TriCache' minimal speedup over mmap is {np.min(plot_results[TRICACHE][i8B,0,:] / plot_results[MMAP][i8B,0,:]):.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 10%, TriCache' maximal speedup over mmap is {np.max(plot_results[TRICACHE][i8B,0,:] / plot_results[MMAP][i8B,0,:]):.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 10%, TriCache' minimal speedup over FastMap is {np.min(plot_results[TRICACHE][i8B,0,:] / plot_results[FASTMAP][i8B,0,:]):.2f}")
    print(f"For 8B Random workloads, Memory Hit Rate = 10%, TriCache' maximal speedup over FastMap is {np.max(plot_results[TRICACHE][i8B,0,:] / plot_results[FASTMAP][i8B,0,:]):.2f}")

    print("\n")

    print(f"For 4KB Random workloads, Memory Hit Rate = 100%, Private Hit Rate = 0.05%, TriCache' speedup over mmap is {plot_results[TRICACHE][i4KB,-1,0] / plot_results[MMAP][i4KB,-1,0]:.2f}")
    print(f"For 4KB Random workloads, Memory Hit Rate = 100%, Private Hit Rate = 100%, TriCache' speedup over mmap is {plot_results[TRICACHE][i4KB,-1,-1] / plot_results[MMAP][i4KB,-1,-1]:.2f}")

    print()

    print(f"For 4KB Random workloads, Memory Hit Rate = 90%, TriCache' average speedup over mmap is {geometric_mean((plot_results[TRICACHE][i4KB,-2,:] / plot_results[MMAP][i4KB,-2,:])):.2f}")
    print(f"For 4KB Random workloads, Memory Hit Rate = 90%, TriCache' average speedup over FastMap is {geometric_mean((plot_results[TRICACHE][i4KB,-2,:] / plot_results[FASTMAP][i4KB,-2,:])):.2f}")

    print()

    print(f"For 4KB Random workloads, Memory Hit Rate = 10%, TriCache' minimal speedup over mmap is {np.min(plot_results[TRICACHE][i4KB,0,:] / plot_results[MMAP][i4KB,0,:]):.2f}")
    print(f"For 4KB Random workloads, Memory Hit Rate = 10%, TriCache' maximal speedup over mmap is {np.max(plot_results[TRICACHE][i4KB,0,:] / plot_results[MMAP][i4KB,0,:]):.2f}")
    print(f"For 4KB Random workloads, Memory Hit Rate = 10%, TriCache' minimal speedup over FastMap is {np.min(plot_results[TRICACHE][i4KB,0,:] / plot_results[FASTMAP][i4KB,0,:]):.2f}")
    print(f"For 4KB Random workloads, Memory Hit Rate = 10%, TriCache' maximal speedup over FastMap is {np.max(plot_results[TRICACHE][i4KB,0,:] / plot_results[FASTMAP][i4KB,0,:]):.2f}")

    draw_heatmap(plot_results)
