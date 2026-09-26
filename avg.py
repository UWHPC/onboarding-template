from collections import defaultdict
import json
import subprocess
import sys

BUILD = 'cmake --build --preset benchmark'.split()
TEST = 'ctest --preset benchmark --output-on-failure'.split()
BENCH = './build/benchmark/uwhpc_benchmark'

if __name__ == '__main__':
    build = subprocess.run(BUILD)
    if build.returncode != 0:
        sys.exit(build.returncode)
        
    test = subprocess.run(TEST)
    if test.returncode != 0:
        sys.exit(test.returncode)
    
    totals = defaultdict(float)
    try:
        count = int(sys.argv[1])
    except Exception:
        count = 10
    for _ in range(count):
        result = subprocess.run([BENCH], capture_output=True, text=True, check=True)
        stats = json.loads(result.stdout)
        for k, v in stats.items():
            totals[k] += v
    for k, v in totals.items():
        totals[k] /= count
    print(dict(totals))