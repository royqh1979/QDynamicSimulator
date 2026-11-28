import sys

import numpy as np
from scipy import stats


def grouped_ks(samples, cdf, discrete=False):
    samples = np.sort(np.asarray(samples))
    n = samples.size
    statistic = 0.0
    first = 0
    while first < n:
        last = first + 1
        while last < n and samples[last] == samples[first]:
            last += 1
        current = np.clip(cdf(samples[first]), 0.0, 1.0)
        previous = 0.0 if first == 0 else np.clip(cdf(samples[first - 1] if discrete else samples[first]), 0.0, 1.0)
        statistic = max(statistic, abs(last / n - current), abs(previous - first / n))
        first = last
    return statistic


def main(path):
    rows = {}
    raw = []
    with open(path, encoding="utf-8") as source:
        next(source)
        for line in source:
            name, _, value = line.strip().split(",")
            if name == "raw":
                raw.append(int(value))
            else:
                rows.setdefault(name, []).append(float(value))

    expected = np.random.RandomState(4487556).randint(0, 2**32, size=len(raw), dtype="u4")
    np.testing.assert_array_equal(np.asarray(raw, dtype=np.uint64), expected)
    print(f"raw_mt19937: {len(raw)} identical")

    sigma_squared = np.log1p(0.01)
    mu = np.log(10.0) - sigma_squared / 2.0
    cases = {
        "uniform": lambda x: np.clip(x / 10.0, 0.0, 1.0),
        "normal_output": lambda x: stats.norm.cdf(x, loc=2.0, scale=1.0),
        "lognormal_output": lambda x: np.where(
            x <= 0.0, 0.0, stats.norm.cdf(np.log(x), loc=mu, scale=np.sqrt(sigma_squared))
        ),
        "poisson_output": lambda x: stats.poisson.cdf(np.floor(x), 3.0),
        "exponential_output": lambda x: 1.0 - np.exp(-x),
    }
    for name, cdf in cases.items():
        grouped = grouped_ks(rows[name], cdf, discrete=name == "poisson_output")
        if name == "poisson_output":
            for value in range(8):
                expected = np.exp(-3.0)
                cumulative = expected
                for index in range(value):
                    expected *= 3.0 / (index + 1)
                    cumulative += expected
                assert abs(cdf(value) - cumulative) < 1e-12
            counts = np.bincount(np.minimum(np.asarray(rows[name], dtype=int), 11), minlength=12)
            probabilities = stats.poisson.pmf(np.arange(12), 3.0)
            probabilities[-1] = 1.0 - stats.poisson.cdf(10, 3.0)
            chi = stats.chisquare(counts, probabilities * len(rows[name]))
            print(f"{name}: grouped_D={grouped:.12g} chi_square_p={chi.pvalue:.6g}")
            assert grouped < 0.02 and chi.pvalue > 0.01, name
        else:
            result = stats.kstest(rows[name], cdf, method="auto")
            print(f"{name}: scipy_D={result.statistic:.12g} grouped_D={grouped:.12g} p={result.pvalue:.6g}")
            assert abs(result.statistic - grouped) < 1e-12, name
            assert result.statistic < 0.02 and result.pvalue > 0.01, name


if __name__ == "__main__":
    main(sys.argv[1])
