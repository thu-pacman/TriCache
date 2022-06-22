import sys
import pathlib
import re
import numpy as np

def read_result(filename, result_re):
    found_result = False
    try:
        with open(filename, "r") as f:
            lines = f.readlines()
            for line in lines:
                if result_re.match(line):
                    result = float(result_re.match(line).group(1))
                    found_result = True
    except:
        pass
    return result if found_result else np.inf

ligra_re = re.compile(r"^Running time : (.*)$")
terasort_re = re.compile(r"^exec (.*) s.*$")
prfile_re = re.compile(r"""# Profile of Direct SATC:
#   access number: (\d+)
#   access cycles: (\d+)
#     miss number: (\d+)
#     miss cycles: (\d+)
# Profile of Private SATC:
#   access number: (\d+)
#   access cycles: (\d+)
#     miss number: (\d+)
#     miss cycles: (\d+)
# Profile of Shared:
#   access number: (\d+)
#   access cycles: (\d+)
#     miss number: (\d+)
#     miss cycles: (\d+)""")

if __name__ == '__main__':

    result_dir = pathlib.Path(sys.argv[1]) / 'results_breakdown'

    filelist = lambda x: [
        x + ".txt",
        x + "_disable_direct.txt",
        x + "_disable_private.txt",
        x + "_disable_direct_private.txt"
    ]

    configs = [
        ("PageRank", ligra_re, "PageRank_uk-2014"),
        ("ShuffleSort", terasort_re, "terasort_manual"),
        ("GNUSort", terasort_re, "terasort_gnu"),
    ]

    for name, matcher, prefix in configs:
        result = list(map(lambda x: read_result(result_dir / x, matcher), filelist(prefix)))
        print(f"For {name}, slowdown of disabling direct SATC is {result[1] / result[0]:.2f}, disabling private SATC is {result[2] / result[0]:.2f}, disabling direct and private SATC is {result[3] / result[0]:.2f}")

    print()

    for name, _, prefix in configs:
        profile_f = result_dir / (prefix + "_profile.txt")
        with open(profile_f, "r") as f:
            m = list(prfile_re.finditer(f.read()))[-1]
        direct_acc_num  = int(m.group( 1))
        direct_acc_cyc  = int(m.group( 2))
        direct_mis_num  = int(m.group( 3))
        direct_mis_cyc  = int(m.group( 4))
        private_acc_num = int(m.group( 5))
        private_acc_cyc = int(m.group( 6))
        private_mis_num = int(m.group( 7))
        private_mis_cyc = int(m.group( 8))
        shared_acc_num  = int(m.group( 9))
        # shared_acc_cyc  = int(m.group(10))
        shared_acc_cyc  = private_mis_cyc
        shared_mis_num  = int(m.group(11))
        shared_mis_cyc  = int(m.group(12))

        data = [
            ("Direct SATC", direct_mis_num / direct_acc_num, (direct_acc_cyc - direct_mis_cyc) / (direct_acc_num - direct_mis_num)),
            ("Private SATC", private_mis_num / private_acc_num, (private_acc_cyc - private_mis_cyc) / (private_acc_num - private_mis_num)),
            ("Shared Cache", shared_mis_num / shared_acc_num, (shared_acc_cyc - shared_mis_cyc) / shared_acc_num),
        ]

        print(f"Case: {name}")
        for type, miss_rate, avg_cycle in data:
            print(f"{type} miss rate: {miss_rate:f}, average cycle: {avg_cycle:.2f}")
