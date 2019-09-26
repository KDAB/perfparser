// SPDX-License-Identifier: LGPL-2.1-or-later

#include <vector>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

int main()
{
    std::vector<double> v;
    std::generate_n(std::back_inserter(v), 100000, [i = 0] () mutable {
        auto x = std::sin(i++);
        auto y = std::cos(i++);
        return std::abs(std::complex<double>(x, y));
    });
    auto sum = std::accumulate(v.begin(), v.end(), 0.0);
    return sum > 0;
}
